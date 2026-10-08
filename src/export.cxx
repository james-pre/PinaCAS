#include "parser.hxx"

/*I don't really like this dependency on cas.h but it only needs it for the absolute_val function
which could be easily copied here. */
#include "cas/cas.hxx"

#include <string.h>

static uint8_t precedence_type(Op type) {
	switch (type) {
		case Op::Equals: return 1;
		case Op::Add: return 5;
		case Op::Mult:
		case Op::Div: return 10;
		case Op::Pow:
		case Op::Root: return 15;
		default: return 20;
	}
}

static uint8_t precedence(const ast *e) {
	if (e->isOperator())
		return precedence_type(e->op());
	return 255;
}

static bool is_right_operator(Op type) {
	return type == Op::Factorial || type == Op::Prime;
}

/*True if child needs parentheses as an operand of parent*/
static bool need_paren(const ast *parent, const ast *child) {
	if (parent->isOperator() && is_op_operator(parent->op()) && !is_op_commutative(parent->op()) &&
		precedence(child) <= precedence(parent))
		return true;

	return precedence(child) < precedence(parent) ||
		   (is_right_operator(parent->op()) && child->isNumber() && child->num() < 0);
}

static const ast *rightmost(const ast &e) {
	if (e.isOperator()) {
		switch (e.op()) {
			case Op::Pow: return rightmost(*e.lastChild());
			default: break;
		}
	}

	return &e;
}
static const ast *leftmost(const ast &e) {
	if (e.isOperator()) {
		switch (e.op()) {
			case Op::Pow:
			case Op::Root:
				/*For the sqrt special case*/
				if (e.childAt(0)->isInt(2))
					return &e;
				/*FALLTHROUGH*/
			case Op::Log:
			case Op::Factorial:
			case Op::Prime: return leftmost(*e.firstChild());
			default: break;
		}
	}

	return &e;
}

/*True if every factor of the multiplication is 1 or -1*/
static bool only_units(const ast &e) {
	for (const ast &child : e.children()) {
		if (!child.isInt(1) && !child.isInt(-1))
			return false;
	}

	return true;
}

/*Returns length of buffer. Writes to buffer is buffer != nullptr*/
static unsigned _to_binary(const ast *e, uint8_t *data, unsigned index, const TokenTable &lookup, Error *err) {
	auto add_byte = [&](uint8_t byte) {
		if (data != nullptr)
			data[index] = byte;
		index++;
	};

	auto add_token = [&](Tok token) {
		for (uint8_t i = 0; i < lookup[token].length; i++)
			add_byte(lookup[token].bytes[i]);
	};

	switch (e->type()) {
		case ast::Type::Number: {
			char *buffer = e->num().toString(6);

			for (unsigned i = 0; i < strlen(buffer); i++) {
				uint8_t c = static_cast<uint8_t>(buffer[i]);
				if (c == '.')
					c = lookup[Tok::Period].bytes[0];
				else if (c == '-')
					c = lookup[Tok::Negate].bytes[0];
				add_byte(c);
			}

			free(buffer);
			break;
		}
		case ast::Type::Symbol:

			switch (e->symbol()) {
				case Sym::Imag: add_token(Tok::Imag); break;
				case Sym::Pi: add_token(Tok::Pi); break;
				case Sym::Euler: add_token(Tok::Euler); break;
				case Sym::Theta: add_token(Tok::Theta); break;
				default: add_byte(static_cast<uint8_t>(e->symbol())); break;
			}

			break;
		case ast::Type::Operator: {
			switch (e->op()) {
				case Op::Add: {
					ast *e_copy = e->copy();

					for (unsigned i = 0; i < e_copy->childCount() - 1; i++) {
						ast *child = e_copy->childAt(i);
						ast &next = *child->next();

						if (need_paren(e_copy, child))
							add_token(Tok::OpenPar);
						index = _to_binary(child, data, index, lookup, err);
						if (need_paren(e_copy, child))
							add_token(Tok::ClosePar);

						if (absolute_val(next))
							add_token(Tok::Minus);
						else
							add_token(Tok::Plus);
					}

					const ast *child = e_copy->lastChild();

					if (need_paren(e_copy, child))
						add_token(Tok::OpenPar);
					index = _to_binary(child, data, index, lookup, err);
					if (need_paren(e_copy, child))
						add_token(Tok::ClosePar);

					ast::dispose(e_copy);

					break;
				}
				case Op::Mult: {
					for (unsigned i = 0; i < e->childCount() - 1; i++) {
						const ast *child = e->childAt(i);
						const ast &next = *child->next();

						/*Always put parentheses around root operator unless it is sqrt(
                For example, -1 * 3root2 should be -(3root2) */
						const bool root_special_case = child->isOp(Op::Root) && !child->childAt(0)->isInt(2);

						if (child->isInt(-1)) {
							add_token(Tok::Negate);
						} else if (child->isInt(1)) {
							/*You'd think this would be fixed by the simplifier, but
                    the problem is reintroduced when we abs(-1) during addition exporting*/
							continue;
						} else {
							/*For example, */
							if (need_paren(e, child) || root_special_case)
								add_token(Tok::OpenPar);
							index = _to_binary(child, data, index, lookup, err);
							if (need_paren(e, child) || root_special_case)
								add_token(Tok::ClosePar);
						}

						const bool needs_mult =
							!need_paren(e, child) && !root_special_case &&
							((rightmost(*child)->isNumber() && !rightmost(*child)->isInt(-1) &&
							  leftmost(next)->isNumber())
							 /*Should never happen, because the tree should have been flattened. This is just in case*/
							 || child->isOp(Op::Mult));

						if (needs_mult && !next.isOp(Op::Root))
							add_token(Tok::Multiply);
					}

					const ast *child = e->lastChild();
					const bool root_special_case = child->isOp(Op::Root) && !child->childAt(0)->isInt(2);

					if (!child->isInt(1) || only_units(*e)) {
						if (need_paren(e, child) || root_special_case)
							add_token(Tok::OpenPar);
						index = _to_binary(child, data, index, lookup, err);
						if (need_paren(e, child) || root_special_case)
							add_token(Tok::ClosePar);
					}

					break;
				}
				case Op::Div:
				case Op::Pow: {
					const ast *a = e->childAt(0);
					const ast *b = e->childAt(1);

					if (need_paren(e, a))
						add_token(Tok::OpenPar);
					index = _to_binary(a, data, index, lookup, err);
					if (need_paren(e, a))
						add_token(Tok::ClosePar);

					add_token(e->op() == Op::Div ? Tok::Fraction : Tok::Power);

					if ((e->op() == Op::Pow && !b->isNumber() && !b->isSymbol()) || need_paren(e, b))
						add_token(Tok::OpenPar);
					index = _to_binary(b, data, index, lookup, err);
					if ((e->op() == Op::Pow && !b->isNumber() && !b->isSymbol()) || need_paren(e, b))
						add_token(Tok::ClosePar);

					break;
				}
				case Op::Root: {
					const ast *a = e->childAt(0);
					const ast *b = e->childAt(1);

					if (a->isInt(2)) {
						add_token(Tok::Sqrt);

						if (need_paren(e, b))
							add_token(Tok::OpenPar);
						index = _to_binary(b, data, index, lookup, err);
						if (need_paren(e, b))
							add_token(Tok::ClosePar);

						add_token(Tok::ClosePar);
					} else {
						if (need_paren(e, a))
							add_token(Tok::OpenPar);
						index = _to_binary(a, data, index, lookup, err);
						if (need_paren(e, a))
							add_token(Tok::ClosePar);

						add_token(Tok::Root);

						if (need_paren(e, b))
							add_token(Tok::OpenPar);
						index = _to_binary(b, data, index, lookup, err);
						if (need_paren(e, b))
							add_token(Tok::ClosePar);
					}

					break;
				}
				case Op::Log: {
					const ast &a = *e->childAt(0);
					const ast *b = e->childAt(1);

					if (a.isSymbol() && a.symbol() == Sym::Euler) {
						add_token(Tok::Ln);
						index = _to_binary(b, data, index, lookup, err);
						add_token(Tok::ClosePar);
						break;
					} else if (a.isNumber() && a.num() == 10) {
						add_token(Tok::Log);
						index = _to_binary(b, data, index, lookup, err);
						add_token(Tok::ClosePar);
						break;
					}

					add_token(Tok::LogBase);
					index = _to_binary(b, data, index, lookup, err);
					add_token(Tok::Comma);
					index = _to_binary(&a, data, index, lookup, err);
					add_token(Tok::ClosePar);

					break;
				}
				case Op::Deriv: {
					const ast *a = e->childAt(0);
					const ast *b = e->childAt(1);
					const ast *c = e->childAt(2);

					add_token(Tok::Deriv);
					index = _to_binary(a, data, index, lookup, err);
					add_token(Tok::Comma);
					index = _to_binary(b, data, index, lookup, err);
					add_token(Tok::Comma);
					index = _to_binary(c, data, index, lookup, err);
					add_token(Tok::ClosePar);

					break;
				}
				case Op::Integral: {
					add_token(Tok::Integral);
					index = _to_binary(e->childAt(0), data, index, lookup, err);
					add_token(Tok::Comma);
					index = _to_binary(e->childAt(1), data, index, lookup, err);
					add_token(Tok::ClosePar);
					break;
				}
				case Op::Equals: {
					index = _to_binary(e->childAt(0), data, index, lookup, err);
					add_token(Tok::Equals);
					index = _to_binary(e->childAt(1), data, index, lookup, err);
					break;
				}
				case Op::At: {
					index = _to_binary(e->childAt(0), data, index, lookup, err);
					add_token(Tok::OpenPar);
					index = _to_binary(e->childAt(1), data, index, lookup, err);
					add_token(Tok::ClosePar);
					break;
				}
				case Op::Factorial:
				case Op::Prime: {
					const ast *a = e->childAt(0);

					if (need_paren(e, a))
						add_token(Tok::OpenPar);
					index = _to_binary(a, data, index, lookup, err);
					if (need_paren(e, a))
						add_token(Tok::ClosePar);

					add_token(e->op() == Op::Prime ? Tok::Prime : Tok::Factorial);

					break;
				}
				default:

					switch (e->op()) {
						case Op::Int: add_token(Tok::Int); break;
						case Op::Abs: add_token(Tok::Abs); break;
						case Op::Sin: add_token(Tok::Sin); break;
						case Op::Sin_Inv: add_token(Tok::Sin_Inv); break;
						case Op::Cos: add_token(Tok::Cos); break;
						case Op::Cos_Inv: add_token(Tok::Cos_Inv); break;
						case Op::Tan: add_token(Tok::Tan); break;
						case Op::Tan_Inv: add_token(Tok::Tan_Inv); break;
						case Op::SinH: add_token(Tok::SinH); break;
						case Op::SinH_Inv: add_token(Tok::SinH_Inv); break;
						case Op::CosH: add_token(Tok::CosH); break;
						case Op::CosH_Inv: add_token(Tok::CosH_Inv); break;
						case Op::TanH: add_token(Tok::TanH); break;
						case Op::TanH_Inv: add_token(Tok::TanH_Inv); break;
						default: break;
					}

					index = _to_binary(e->childAt(0), data, index, lookup, err);
					add_token(Tok::ClosePar);
					break;
			}

			break;
		}
	}

	return index;
}

uint8_t *export_to_binary(const ast &e, unsigned *len, const TokenTable &lookup, Error *err) {
	*err = Error::Success;

	*len = _to_binary(&e, nullptr, 0, lookup, err);

	if (*err != Error::Success) {
		*len = 0;
		return nullptr;
	}

	uint8_t *data = static_cast<uint8_t *>(malloc(*len));
	_to_binary(&e, data, 0, lookup, err);

	return data;
}
