#include "typeset.hxx"

#include <cstdlib>
#include <cstring>

#include "cas/cas.hxx"

namespace ts {

Box::Box(Type type) : type(type) {
}

Box::~Box() {
	Box *child = first;

	while (child != nullptr) {
		Box *next = child->next;
		delete child;
		child = next;
	}

	free(text);
}

Box *Box::append(Box *child) {
	Box **slot = &first;

	while (*slot != nullptr)
		slot = &(*slot)->next;

	*slot = child;
	return this;
}

Box *text(const char *text) {
	Box *b = new Box(Box::Type::Text);
	b->text = static_cast<char *>(malloc(strlen(text) + 1));
	strcpy(b->text, text);
	return b;
}

Box *row() {
	return new Box(Box::Type::Row);
}

static Box *pair(Box::Type type, Box *a, Box *b) {
	return (new Box(type))->append(a)->append(b);
}

static Box *row2(Box *a, Box *b) {
	return pair(Box::Type::Row, a, b);
}

static Box *delimited(Box *child, char left, char right) {
	Box *b = (new Box(Box::Type::Delimited))->append(child);
	b->left = left;
	b->right = right;
	return b;
}

static Box *integer_box(const mpz_t *z) {
	const mp_size len = mp_int_string_len(z, 10);
	char *digits = static_cast<char *>(malloc(len));

	mp_int_to_string(z, 10, digits, static_cast<int>(len));
	Box *b = text(digits);
	free(digits);

	return b;
}

static Box *number_box(const num &r) {
	if (r.isInteger())
		return integer_box(MP_NUMER_P(&r));

	return pair(Box::Type::Fraction, integer_box(MP_NUMER_P(&r)), integer_box(MP_DENOM_P(&r)));
}

static Box *symbol_box(Sym symbol) {
	char name[2] = {0};

	switch (symbol) {
		case Sym::Pi: name[0] = Pi; break;
		case Sym::Theta: name[0] = Theta; break;
		case Sym::Euler: name[0] = 'e'; break;
		case Sym::Imag: name[0] = 'i'; break;
		default: name[0] = static_cast<char>(symbol); break;
	}

	return text(name);
}

/*True if e does not need parentheses as a base or before a postfix operator*/
static bool is_atom(const ast &e) {
	switch (e.type()) {
		case ast::Type::Number: return e.num().isInteger() && e.num() >= 0;
		case ast::Type::Symbol: return true;
		case ast::Type::Operator:
			return e.op() == Op::Prime || e.op() == Op::Log || e.op() == Op::Subscript || is_op_function(e.op());
	}

	return false;
}

/*True if e is drawn starting with a number, so a product needs a dot before it*/
static bool starts_with_number(const ast &e) {
	if (e.isNumber())
		return true;

	if (!e.isOperator())
		return false;

	switch (e.op()) {
		case Op::Div: return true;
		case Op::Pow:
		case Op::Mult:
		case Op::Factorial: return starts_with_number(*e.firstChild());
		default: return false;
	}
}

static Box *convert(const ast &e);

/*Writes a numeric fraction on one line, as in an exponent, or returns nullptr if e is not one*/
static Box *inline_fraction(const ast &e) {
	num value;

	if (e.isNumber() && !e.num().isInteger())
		value = e.num();
	else if (e.isOp(Op::Div) && e.firstChild()->isNumber() && e.firstChild()->next()->isNumber())
		value = e.firstChild()->num() / e.firstChild()->next()->num();
	else
		return nullptr;

	Box *line = row();
	if (value < 0) {
		line->append(text("-"));
		value = -value;
	}

	line->append(integer_box(MP_NUMER_P(&value)));
	if (!value.isInteger()) {
		line->append(text("/"));
		line->append(integer_box(MP_DENOM_P(&value)));
	}

	return line;
}

static Box *parenthesized(const ast &e, bool parens) {
	Box *b = convert(e);
	return parens ? delimited(b, '(', ')') : b;
}

static Box *function_box(const ast &e) {
	const char *name;
	bool inverse = false;

	switch (e.op()) {
		case Op::Int: name = "int"; break;
		case Op::Sin_Inv: inverse = true; /*FALLTHROUGH*/
		case Op::Sin: name = "sin"; break;
		case Op::Cos_Inv: inverse = true; /*FALLTHROUGH*/
		case Op::Cos: name = "cos"; break;
		case Op::Tan_Inv: inverse = true; /*FALLTHROUGH*/
		case Op::Tan: name = "tan"; break;
		case Op::SinH_Inv: inverse = true; /*FALLTHROUGH*/
		case Op::SinH: name = "sinh"; break;
		case Op::CosH_Inv: inverse = true; /*FALLTHROUGH*/
		case Op::CosH: name = "cosh"; break;
		case Op::TanH_Inv: inverse = true; /*FALLTHROUGH*/
		case Op::TanH: name = "tanh"; break;
		default: name = "?"; break;
	}

	return row2(
		inverse ? pair(Box::Type::Superscript, text(name), text("-1")) : text(name),
		delimited(convert(*e.firstChild()), '(', ')')
	);
}

static Box *sum_box(const ast &e) {
	Box *line = row();

	for (const ast &term : e.children()) {
		if (&term != e.firstChild() && is_negative_for_sure(term)) {
			ast *magnitude = term.copy();
			absolute_val(*magnitude);
			line->append(text(" - "));
			line->append(convert(*magnitude));
			delete magnitude;
		} else {
			if (&term != e.firstChild())
				line->append(text(" + "));
			line->append(convert(term));
		}
	}

	return line;
}

static Box *product_box(const ast &e) {
	Box *line = row();
	bool empty = true;

	for (const ast &factor : e.children()) {
		if (factor.isInt(1))
			continue;

		if (!empty && starts_with_number(factor)) {
			char dot[2] = {Dot, '\0'};
			line->append(text(dot));
		}

		line->append(parenthesized(
			factor, factor.isOp(Op::Add) || factor.isOp(Op::Equals) || (!empty && is_negative_for_sure(factor))
		));
		empty = false;
	}

	if (empty)
		line->append(text("1"));

	return line;
}

static Box *log_box(const ast &e) {
	const ast &base = *e.childAt(0);
	Box *name;

	if (base.isSymbol() && base.symbol() == Sym::Euler)
		name = text("ln");
	else if (base.isInt(10))
		name = text("log");
	else
		name = pair(Box::Type::Subscript, text("log"), convert(base));

	return row2(name, delimited(convert(*e.childAt(1)), '(', ')'));
}

static Box *operator_box(const ast &e) {
	const ast *a = e.firstChild();
	const ast *b = a != nullptr ? a->next() : nullptr;

	switch (e.op()) {
		case Op::Add: return sum_box(e);
		case Op::Mult: return product_box(e);
		case Op::Div: return pair(Box::Type::Fraction, convert(*a), convert(*b));
		case Op::Pow: {
			Box *exponent = inline_fraction(*b);
			return pair(
				Box::Type::Superscript, parenthesized(*a, !is_atom(*a)), exponent != nullptr ? exponent : convert(*b)
			);
		}
		case Op::Root:
			if (a->isInt(2))
				return (new Box(Box::Type::Root))->append(convert(*b));
			return pair(
				Box::Type::Superscript,
				parenthesized(*b, !is_atom(*b)),
				pair(Box::Type::Fraction, text("1"), convert(*a))
			);
		case Op::Log: return log_box(e);
		case Op::Deriv: {
			Box *d = row2(
				pair(Box::Type::Fraction, text("d"), row2(text("d"), convert(*b))), delimited(convert(*a), '(', ')')
			);

			if (!b->compare(*b->next())) {
				Box *at = row2(convert(*b), text("="));
				at->append(convert(*b->next()));
				d = pair(Box::Type::Subscript, d, at);
			}

			return d;
		}
		case Op::Integral: {
			Box *integrand = row2(parenthesized(*a, a->isOp(Op::Add)), text(" d"));
			integrand->append(convert(*b));
			return (new Box(Box::Type::Integral))->append(integrand);
		}
		case Op::Equals: return row2(convert(*a), text(" = "))->append(convert(*b));
		case Op::Prime: return row2(parenthesized(*a, !is_atom(*a)), text("'"));
		case Op::At: return row2(parenthesized(*a, !is_atom(*a)), delimited(convert(*b), '(', ')'));
		case Op::Sum: {
			char infinity[2] = {Infinity, '\0'};
			Box *under = row2(convert(*b), text("="));
			under->append(convert(*b->next()));
			return row2(pair(Box::Type::Summation, under, text(infinity)), parenthesized(*a, a->isOp(Op::Add)));
		}
		case Op::Subscript: return pair(Box::Type::Subscript, convert(*a), convert(*b));
		case Op::Factorial: return row2(parenthesized(*a, !is_atom(*a)), text("!"));
		case Op::Abs: return delimited(convert(*a), '|', '|');
		default: return function_box(e);
	}
}

static Box *convert(const ast &e) {
	if (is_negative_for_sure(e)) {
		ast *magnitude = e.copy();

		absolute_val(*magnitude);
		Box *b = row2(text("-"), convert(*magnitude));
		delete magnitude;

		return b;
	}

	switch (e.type()) {
		case ast::Type::Number: return number_box(e.num());
		case ast::Type::Symbol: return symbol_box(e.symbol());
		default: return operator_box(e);
	}
}

Box *fromAst(const ast &e) {
	return convert(e);
}

static int max(int a, int b) {
	return a > b ? a : b;
}

static int height(const Box *b) {
	return b->ascent + b->descent;
}

/*Distance from the baseline of a superscript's base up to the baseline of its script*/
static int script_shift(Box *base, Box *script, const Metrics &m) {
	return max(base->ascent - m.script_drop, script->descent + m.script_drop);
}

void measure(Box *b, const Metrics &m) {
	Box *first = b->first;

	for (Box *child = first; child != nullptr; child = child->next)
		measure(child, m);

	Box *second = first != nullptr ? first->next : nullptr;

	switch (b->type) {
		case Box::Type::Text:
			b->width = static_cast<int>(m.text_width(b->text));
			b->ascent = m.ascent;
			b->descent = m.descent;
			break;
		case Box::Type::Row:
			b->width = 0;
			b->ascent = m.ascent;
			b->descent = m.descent;
			for (Box *child = first; child != nullptr; child = child->next) {
				b->width += child->width;
				b->ascent = max(b->ascent, child->ascent);
				b->descent = max(b->descent, child->descent);
			}
			break;
		case Box::Type::Fraction:
			b->width = max(first->width, second->width) + 2 * m.fraction_pad;
			b->ascent = m.axis + m.rule + m.gap + height(first);
			b->descent = max(m.descent, m.gap + height(second) - m.axis);
			break;
		case Box::Type::Superscript: {
			const int shift = script_shift(first, second, m);
			b->width = first->width + second->width;
			b->ascent = max(first->ascent, shift + second->ascent);
			b->descent = max(first->descent, second->descent - shift);
			break;
		}
		case Box::Type::Subscript:
			b->width = first->width + second->width;
			b->ascent = max(first->ascent, second->ascent - m.subscript_drop);
			b->descent = max(first->descent, m.subscript_drop + second->descent);
			break;
		case Box::Type::Root:
			b->ascent = first->ascent + m.gap + m.rule;
			b->descent = first->descent;
			b->width = m.radical_width(height(b)) + first->width;
			break;
		case Box::Type::Delimited: {
			const int delimiter = m.delimiter_width(height(first));
			b->width = first->width + (b->left ? delimiter : 0) + (b->right ? delimiter : 0);
			b->ascent = first->ascent;
			b->descent = first->descent;
			break;
		}
		case Box::Type::Integral:
			b->ascent = first->ascent + 2 * m.gap;
			b->descent = first->descent + 2 * m.gap;
			b->width = m.integral_width(height(b)) + first->width;
			break;
		case Box::Type::Summation:
			b->width = max(m.summation_width, max(first->width, second->width));
			b->ascent = m.ascent + m.gap + height(second);
			b->descent = m.descent + m.gap + height(first);
			break;
	}
}

void draw(const Box *b, int x, int baseline, const Metrics &m, const Renderer &r) {
	Box *first = b->first, *second = first != nullptr ? first->next : nullptr;
	const int top = baseline - b->ascent;

	switch (b->type) {
		case Box::Type::Text: r.text(x, top, b->text); break;
		case Box::Type::Row:
			for (Box *child = first; child != nullptr; child = child->next) {
				draw(child, x, baseline, m, r);
				x += child->width;
			}
			break;
		case Box::Type::Fraction: {
			const int bar = baseline - m.axis - m.rule;
			r.bar(x, bar, b->width);
			draw(first, x + (b->width - first->width) / 2, bar - m.gap - first->descent, m, r);
			draw(second, x + (b->width - second->width) / 2, baseline - m.axis + m.gap + second->ascent, m, r);
			break;
		}
		case Box::Type::Superscript:
			draw(first, x, baseline, m, r);
			draw(second, x + first->width, baseline - script_shift(first, second, m), m, r);
			break;
		case Box::Type::Subscript:
			draw(first, x, baseline, m, r);
			draw(second, x + first->width, baseline + m.subscript_drop, m, r);
			break;
		case Box::Type::Root: {
			const int radical = m.radical_width(height(b));
			r.radical(x, top, height(b));
			r.overline(x + radical, top, first->width);
			draw(first, x + radical, baseline, m, r);
			break;
		}
		case Box::Type::Delimited: {
			const int delimiter = m.delimiter_width(height(b));
			if (b->left) {
				r.delimiter(x, top, height(b), b->left);
				x += delimiter;
			}
			draw(first, x, baseline, m, r);
			if (b->right)
				r.delimiter(x + first->width, top, height(b), b->right);
			break;
		}
		case Box::Type::Integral:
			r.integral(x, top, height(b));
			draw(first, x + m.integral_width(height(b)), baseline, m, r);
			break;
		case Box::Type::Summation: {
			const int sign_top = baseline - m.ascent - m.gap, sign_bottom = baseline + m.descent + m.gap;
			r.summation(x + (b->width - m.summation_width) / 2, sign_top, sign_bottom - sign_top);
			draw(second, x + (b->width - second->width) / 2, sign_top - second->descent, m, r);
			draw(first, x + (b->width - first->width) / 2, sign_bottom + first->ascent, m, r);
			break;
		}
	}
}

bool equal(const Box *a, const Box *b) {
	for (; a != nullptr && b != nullptr; a = a->next, b = b->next) {
		if (a->type != b->type || a->left != b->left || a->right != b->right)
			return false;
		if ((a->text == nullptr) != (b->text == nullptr) || (a->text != nullptr && strcmp(a->text, b->text) != 0))
			return false;
		if (!equal(a->first, b->first))
			return false;
	}

	return a == nullptr && b == nullptr;
}

} // namespace ts
