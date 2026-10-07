#include "cas.hxx"

#include "../work.hxx"

/*Limits how deeply integration rules are chained for one integral*/
#define MAX_METHOD_DEPTH 12
/*Highest degree of a polynomial that is divided*/
#define MAX_DEGREE 8

static ast *integer(mp_small n) {
	return ast::make(num::from(n));
}

static ast *mul(ast *a, ast *b) {
	return ast::make(Op::Mult, a, b);
}

static ast *add(ast *a, ast *b) {
	return ast::make(Op::Add, a, b);
}

static ast *quotient(ast *a, ast *b) {
	return ast::make(Op::Div, a, b);
}

static ast *power(ast *a, ast *b) {
	return ast::make(Op::Pow, a, b);
}

static ast *negate(ast *a) {
	return mul(integer(-1), a);
}

static ast *ln(ast *a) {
	return ast::make(Op::Log, ast::make(Sym::Euler), a);
}

/*Takes ownership of f*/
static ast *integral_node(ast *f, const ast &x) {
	return ast::make(Op::Integral, f, x.copy());
}

/*Returns the value of e if it is a number or a quotient of numbers, otherwise nullptr*/
static num *number_value(const ast &e) {
	if (e.isNumber())
		return e.num().copy();

	if (!e.isOp(Op::Div) || !e.childAt(0)->isNumber() || !e.childAt(1)->isNumber())
		return nullptr;

	num *value = e.childAt(0)->num().copy();
	*value /= e.childAt(1)->num();
	return value;
}

static bool is_ast_fraction(const ast &e, mp_small numer, mp_small denom) {
	num *value = number_value(e);

	if (value == nullptr)
		return false;

	const bool equal = mp_rat_compare_value(value, numer, denom) == 0;
	num::dispose(value);
	return equal;
}

static bool is_ast_symbol(const ast &e, Sym symbol) {
	return e.isSymbol() && e.symbol() == symbol;
}

static void simplify_quietly(ast &e) {
	work::pause();
	simplify(e, Simp::Basic);
	work::resume();
}

static ast *derivative_of(ast &e, ast &x) {
	ast *d = e.copy();

	work::pause();
	derivative(*d, x, x);
	simplify(*d, Simp::Basic);
	work::resume();

	return d;
}

/*Returns the slope of e if e is linear in x, otherwise nullptr*/
static ast *linear_slope(ast &e, ast &x) {
	if (is_constant(e, x))
		return nullptr;

	ast *d = derivative_of(e, x);

	if (is_constant(*d, x) && !d->isInt(0))
		return d;

	ast::dispose(d);
	return nullptr;
}

static bool is_polynomial(ast &e, ast &x) {
	if (is_constant(e, x) || e.compare(x))
		return true;

	if (e.isOp(Op::Add) || e.isOp(Op::Mult)) {
		for (ast &child : e.children()) {
			if (!is_polynomial(child, x))
				return false;
		}
		return true;
	}

	if (e.isOp(Op::Pow)) {
		const ast &exponent = *e.childAt(1);
		return exponent.isNumber() && exponent.num().isInteger() && exponent.num() >= 0 &&
			   is_polynomial(*e.childAt(0), x);
	}

	return false;
}

static ast *reciprocal(ast &e) {
	num *exponent;

	if (e.isOp(Op::Pow) && (exponent = number_value(*e.childAt(1))) != nullptr) {
		mp_rat_neg(exponent, exponent);
		return power(e.childAt(0)->copy(), ast::make(exponent));
	}

	if (e.isOp(Op::Pow) && !e.childAt(0)->isOperator()) {
		ast *r = power(e.childAt(0)->copy(), negate(e.childAt(1)->copy()));
		simplify_quietly(*r->childAt(1));
		return r;
	}

	return power(e.copy(), integer(-1));
}

static void collect_factors(ast &e, ast &product, bool inverted) {
	if (e.isOp(Op::Mult)) {
		for (ast &child : e.children())
			collect_factors(child, product, inverted);
	} else if (e.isOp(Op::Div)) {
		collect_factors(*e.childAt(0), product, inverted);
		collect_factors(*e.childAt(1), product, !inverted);
	} else {
		product.appendChild(inverted ? reciprocal(e) : e.copy());
	}
}

/*Returns a multiplication node of the factors of e, with division written as negative powers*/
static ast *factors_of(ast &e) {
	ast *product = ast::make(Op::Mult);
	collect_factors(e, *product, false);
	return product;
}

/*Takes ownership of both. Returns product * e, or e if product has no factors.*/
static ast *times(ast *product, ast *e) {
	if (product->childCount() == 0) {
		ast::dispose(product);
		return e;
	}

	product->appendChild(e);
	return product;
}

/*Takes ownership of product. Returns its only factor, 1 if it has none, or product itself.*/
static ast *unwrap(ast *product) {
	switch (product->childCount()) {
		case 0: ast::dispose(product); return integer(1);
		case 1: {
			ast *only = product->removeChildAt(0);
			ast::dispose(product);
			return only;
		}
		default: return product;
	}
}

/*Returns v if e is c + k*v^2 where k is 1 or -1, otherwise nullptr*/
static ast *match_square_sum(ast &e, mp_small c, mp_small k) {
	if (!e.isOp(Op::Add) || e.childCount() != 2)
		return nullptr;

	ast *constant = e.childAt(0);
	ast *square = e.childAt(1);

	if (!constant->isInt(c)) {
		ast *swap = constant;
		constant = square;
		square = swap;

		if (!constant->isInt(c))
			return nullptr;
	}

	if (k == -1) {
		if (!square->isOp(Op::Mult) || square->childCount() != 2)
			return nullptr;

		if (square->childAt(0)->isInt(-1))
			square = square->childAt(1);
		else if (square->childAt(1)->isInt(-1))
			square = square->childAt(0);
		else
			return nullptr;
	}

	if (!square->isOp(Op::Pow) || !square->childAt(1)->isInt(2))
		return nullptr;

	return square->childAt(0);
}

/*Returns v if e is c + k*v^2 for positive numbers c and k, setting them, otherwise nullptr*/
static ast *match_positive_square_sum(ast &e, num **c, num **k) {
	if (!e.isOp(Op::Add) || e.childCount() != 2)
		return nullptr;

	for (unsigned i = 0; i < 2; i++) {
		*c = number_value(*e.childAt(i));
		ast *square = e.childAt(1 - i);

		if (*c == nullptr)
			continue;

		if (square->isOp(Op::Mult) && square->childCount() == 2 &&
			(*k = number_value(*square->childAt(0))) != nullptr) {
			square = square->childAt(1);
		} else {
			*k = num::from(1);
		}

		if (*(*c) > 0 && *(*k) > 0 && square->isOp(Op::Pow) && square->childAt(1)->isInt(2))
			return square->childAt(0);

		num::dispose(*c);
		num::dispose(*k);
	}

	return nullptr;
}

/*Takes ownership of F. Divides F by the slope of u, or returns nullptr if u is not linear in x.*/
static ast *over_slope(ast *F, ast &u, ast &x) {
	ast *slope = linear_slope(u, x);

	if (slope == nullptr) {
		ast::dispose(F);
		return nullptr;
	}

	return quotient(F, slope);
}

/*Antiderivatives of u^n where u is not linear*/
static ast *table_special_power(ast &u, ast &n, ast &x) {
	ast *v;

	if (is_ast_fraction(n, 2, 1) && (u.isOp(Op::Sin) || u.isOp(Op::Cos))) {
		v = u.childAt(0);
		ast *F = quotient(ast::make(Op::Sin, mul(integer(2), v->copy())), integer(4));
		if (u.isOp(Op::Sin))
			F = negate(F);
		return over_slope(add(quotient(v->copy(), integer(2)), F), *v, x);
	}

	if (is_ast_fraction(n, -2, 1) && u.isOperator()) {
		v = u.childAt(0);

		switch (u.op()) {
			case Op::Cos: return over_slope(ast::make(Op::Tan, v->copy()), *v, x);
			case Op::Sin: return over_slope(negate(power(ast::make(Op::Tan, v->copy()), integer(-1))), *v, x);
			case Op::CosH: return over_slope(ast::make(Op::TanH, v->copy()), *v, x);
			default: return nullptr;
		}
	}

	if (is_ast_fraction(n, -1, 1) && u.isOp(Op::Tan)) {
		v = u.childAt(0);
		return over_slope(ln(ast::make(Op::Abs, ast::make(Op::Sin, v->copy()))), *v, x);
	}

	if (is_ast_fraction(n, -1, 1) && (v = match_square_sum(u, 1, 1)) != nullptr)
		return over_slope(ast::make(Op::Tan_Inv, v->copy()), *v, x);

	num *c, *k;
	if (is_ast_fraction(n, -1, 1) && (v = match_positive_square_sum(u, &c, &k)) != nullptr) {
		ast *F = ast::make(k->copy());
		F = power(quotient(F, ast::make(c->copy())), ast::make(num::from(1, 2)));
		F = ast::make(Op::Tan_Inv, mul(F, v->copy()));
		*c *= *k;
		F = quotient(F, power(ast::make(c), ast::make(num::from(1, 2))));
		num::dispose(k);
		return over_slope(F, *v, x);
	}

	if (is_ast_fraction(n, -1, 1) && u.isOperator() && (u.op() == Op::Cos || u.op() == Op::Sin)) {
		v = u.childAt(0);
		ast *F = power(u.copy(), integer(-1));

		if (u.op() == Op::Cos)
			F = ln(ast::make(Op::Abs, add(F, ast::make(Op::Tan, v->copy()))));
		else
			F = negate(ln(ast::make(Op::Abs, add(F, power(ast::make(Op::Tan, v->copy()), integer(-1))))));

		return over_slope(F, *v, x);
	}

	if (is_ast_fraction(n, -1, 2)) {
		if ((v = match_square_sum(u, 1, -1)) != nullptr)
			return over_slope(ast::make(Op::Sin_Inv, v->copy()), *v, x);
		if ((v = match_square_sum(u, 1, 1)) != nullptr)
			return over_slope(ast::make(Op::SinH_Inv, v->copy()), *v, x);
		if ((v = match_square_sum(u, -1, 1)) != nullptr)
			return over_slope(ast::make(Op::CosH_Inv, v->copy()), *v, x);
	}

	return nullptr;
}

/*Antiderivative of a single non-constant factor h from the table of elementary integrals*/
static ast *table(ast &h, ast &x) {
	ast *u;

	if (h.compare(x))
		return quotient(power(x.copy(), integer(2)), integer(2));

	if (!h.isOperator())
		return nullptr;

	switch (h.op()) {
		case Op::Pow: {
			u = h.childAt(0);
			ast &n = *h.childAt(1);

			if (is_constant(n, x)) {
				ast *slope = linear_slope(*u, x);

				if (slope == nullptr)
					return table_special_power(*u, n, x);

				if (is_ast_fraction(n, -1, 1))
					return quotient(ln(ast::make(Op::Abs, u->copy())), slope);

				return quotient(power(u->copy(), add(n.copy(), integer(1))), mul(add(n.copy(), integer(1)), slope));
			}

			if (is_constant(*u, x)) {
				if (is_ast_symbol(*u, Sym::Euler))
					return over_slope(h.copy(), n, x);
				return over_slope(quotient(h.copy(), ln(u->copy())), n, x);
			}

			return nullptr;
		}
		case Op::Log: {
			const ast &base = *h.childAt(0);

			u = h.childAt(1);

			if (!is_constant(base, x))
				return nullptr;

			ast *F = add(mul(u->copy(), ln(u->copy())), negate(u->copy()));

			if (!is_ast_symbol(base, Sym::Euler))
				F = quotient(F, ln(base.copy()));

			return over_slope(F, *u, x);
		}
		case Op::Sin: u = h.childAt(0); return over_slope(negate(ast::make(Op::Cos, u->copy())), *u, x);
		case Op::Cos: u = h.childAt(0); return over_slope(ast::make(Op::Sin, u->copy()), *u, x);
		case Op::Tan:
			u = h.childAt(0);
			return over_slope(negate(ln(ast::make(Op::Abs, ast::make(Op::Cos, u->copy())))), *u, x);
		case Op::SinH: u = h.childAt(0); return over_slope(ast::make(Op::CosH, u->copy()), *u, x);
		case Op::CosH: u = h.childAt(0); return over_slope(ast::make(Op::SinH, u->copy()), *u, x);
		case Op::TanH: u = h.childAt(0); return over_slope(ln(ast::make(Op::CosH, u->copy())), *u, x);
		default: return nullptr;
	}
}

static void expand_quietly(ast &e) {
	work::pause();
	expand(e, Expand::All);
	simplify(e, Simp::Basic);
	work::resume();
}

/*Antiderivative using linearity, the table, and polynomial expansion, or nullptr*/
static ast *elementary(ast &f, ast &x) {
	if (is_constant(f, x))
		return mul(f.copy(), x.copy());

	if (f.isOp(Op::Add)) {
		ast *F = ast::make(Op::Add);

		for (ast &child : f.children()) {
			ast *term = elementary(child, x);

			if (term == nullptr) {
				ast::dispose(F);
				return nullptr;
			}

			F->appendChild(term);
		}

		return F;
	}

	ast *product = factors_of(f);
	ast *constants = ast::make(Op::Mult);
	ast *variable = nullptr;
	unsigned variables = 0;

	for (ast &child : product->children()) {
		if (is_constant(child, x)) {
			if (!child.isInt(1))
				constants->appendChild(child.copy());
		} else {
			variable = &child;
			variables++;
		}
	}

	ast *F = nullptr;
	if (variables == 1 && (F = table(*variable, x)) != nullptr)
		F = times(constants, F);
	else
		ast::dispose(constants);

	ast::dispose(product);

	if (F == nullptr && is_polynomial(f, x)) {
		ast *expanded = f.copy();
		expand_quietly(*expanded);

		if (expanded->isOp(Op::Add))
			F = elementary(*expanded, x);

		ast::dispose(expanded);
	}

	return F;
}

/*Returns the integral of f with its constant factors moved outside*/
static ast *pull_constants(ast &f, ast &x, bool *pulled) {
	ast *product = factors_of(f);
	ast *constants = ast::make(Op::Mult);
	ast *rest = ast::make(Op::Mult);

	for (const ast &child : product->children()) {
		if (!is_constant(child, x))
			rest->appendChild(child.copy());
		else if (!child.isInt(1))
			constants->appendChild(child.copy());
	}

	ast::dispose(product);

	if (constants->childCount() > 0)
		*pulled = true;

	return times(constants, integral_node(unwrap(rest), x));
}

/*Integrates the terms of f that are elementary and pulls out constant factors, leaving integral nodes for the rest*/
static ast *split(ast &f, ast &x, bool *progress) {
	if (f.isOp(Op::Add)) {
		ast *sum = ast::make(Op::Add);

		*progress = true;

		for (ast &child : f.children()) {
			ast *F = elementary(child, x);
			sum->appendChild(F != nullptr ? F : pull_constants(child, x, progress));
		}

		return sum;
	}

	return pull_constants(f, x, progress);
}

/*Returns the argument of h that could be the inner function of a substitution*/
static ast *inner_candidate(ast &h, ast &x) {
	if (!h.isOperator())
		return nullptr;

	switch (h.op()) {
		case Op::Pow:
			if (is_constant(*h.childAt(1), x))
				return h.childAt(0);
			if (is_constant(*h.childAt(0), x))
				return h.childAt(1);
			return nullptr;
		case Op::Log: return h.childAt(1);
		default: return is_op_function(h.op()) ? h.childAt(0) : nullptr;
	}
}

/*Integrates g(v(x))v'(x) by substituting u = v(x). Records its steps.*/
static ast *substitution(ast &f, ast &x) {
	ast *product = factors_of(f);
	ast *result = nullptr;
	const unsigned length = product->childCount();
	const Sym symbol = fresh_symbol(f);

	if (symbol == Sym::Invalid) {
		ast::dispose(product);
		return nullptr;
	}

	for (unsigned i = 0; i < length && result == nullptr; i++) {
		ast &h = *product->childAt(i);
		ast *v = inner_candidate(h, x);

		if (v == nullptr || is_constant(*v, x))
			continue;

		ast *slope = linear_slope(*v, x);
		if (slope != nullptr) {
			ast::dispose(slope);
			continue;
		}

		ast *rest = product->copy();
		ast::dispose(rest->removeChildAt(i));

		ast *ratio = quotient(unwrap(rest), derivative_of(*v, x));
		simplify_quietly(*ratio);

		if (!is_constant(*ratio, x)) {
			ast::dispose(ratio);
			continue;
		}

		ast *u = ast::make(static_cast<Sym>(symbol));
		ast *g = h.copy();

		work::pause();
		substitute(*g, *v, *u);
		simplify(*g, Simp::Basic);
		work::resume();

		ast *G;
		if (is_constant(*g, x) && (G = elementary(*g, *u)) != nullptr) {
			ast *before = integral_node(f.copy(), x);
			ast *rewritten = mul(ratio->copy(), integral_node(g->copy(), *u));

			simplify_quietly(*G);
			simplify_quietly(*rewritten);

			work::step(work::Step::Type::Equation, "Substitute", u, v);
			work::step(work::Step::Type::Integral, "Substitution", before, rewritten);

			ast::dispose(rewritten);
			rewritten = integral_node(g->copy(), *u);
			work::step(work::Step::Type::Integral, nullptr, rewritten, G);
			ast::dispose(rewritten);

			result = mul(ratio->copy(), G);

			work::pause();
			substitute(*result, *u, *v);
			work::resume();

			simplify_quietly(*result);
			work::step(work::Step::Type::Integral, "Back-substitute", before, result);

			ast::dispose(before);
		}

		ast::dispose(g);
		ast::dispose(u);
		ast::dispose(ratio);
	}

	ast::dispose(product);
	return result;
}

/*Fills c with the coefficients of the polynomial e in x, constant first, and returns its degree, or -1 if it is higher than MAX_DEGREE*/
static int coefficients(ast &e, ast &x, ast **c) {
	ast *d = e.copy(), *zero = integer(0);
	int k;
	mp_small factorial = 1;

	for (k = 0; k <= MAX_DEGREE + 1 && !d->isInt(0); k++) {
		if (k == MAX_DEGREE + 1) {
			while (k-- > 0)
				ast::dispose(c[k]);
			k = -1;
			break;
		}

		if (k > 0)
			factorial *= k;

		c[k] = d->copy();
		substitute(*c[k], x, *zero);
		c[k] = quotient(c[k], integer(factorial));
		simplify(*c[k], Simp::Basic);

		ast *next = d->copy();
		derivative(*next, x, x);
		simplify(*next, Simp::Basic);
		ast::dispose(d);
		d = next;
	}

	ast::dispose(d);
	ast::dispose(zero);

	return k < 0 ? -1 : k - 1;
}

/*Returns the sum of c[k]*x^k for k up to degree. Takes ownership of the coefficients.*/
static ast *polynomial(ast **c, int degree, ast &x) {
	ast *sum = ast::make(Op::Add);

	for (int k = degree; k >= 0; k--)
		sum->appendChild(mul(c[k], power(x.copy(), integer(k))));

	sum->appendChild(integer(0));
	return sum;
}

/*Rewrites the integral of a quotient of polynomials whose numerator has at least the degree of the denominator as the integral of a polynomial plus a proper fraction. Records the step.*/
static ast *divide(ast &f, ast &x) {
	ast *product = factors_of(f), *numerator = ast::make(Op::Mult), *denominator = ast::make(Op::Mult);
	for (ast &child : product->children()) {
		num *exponent;

		if (child.isOp(Op::Pow) && (exponent = number_value(*child.childAt(1))) != nullptr) {
			if (*exponent < 0)
				denominator->appendChild(reciprocal(child));
			else
				numerator->appendChild(child.copy());
			num::dispose(exponent);
		} else {
			numerator->appendChild(child.copy());
		}
	}

	ast::dispose(product);

	ast *n[MAX_DEGREE + 1], *d[MAX_DEGREE + 1], *q[MAX_DEGREE + 1], *rewritten = nullptr;
	int nd = -1, dd = -1;

	work::pause();

	if (denominator->childCount() > 0 && is_polynomial(*numerator, x) && is_polynomial(*denominator, x) &&
		(dd = coefficients(*denominator, x, d)) >= 1) {
		if ((nd = coefficients(*numerator, x, n)) < dd) {
			for (int k = 0; k <= nd; k++)
				ast::dispose(n[k]);
		} else {
			for (int k = nd - dd; k >= 0; k--) {
				q[k] = quotient(n[k + dd]->copy(), d[dd]->copy());
				simplify(*q[k], Simp::Basic);

				for (int j = 0; j <= dd; j++) {
					n[k + j] = add(n[k + j], negate(mul(q[k]->copy(), d[j]->copy())));
					simplify(*n[k + j], Simp::Basic);
				}
			}

			for (int k = dd; k <= nd; k++)
				ast::dispose(n[k]);

			ast *remainder = quotient(polynomial(n, dd - 1, x), denominator->copy());
			simplify(*remainder, Simp::Basic);
			factor_cancel(*remainder);
			rewritten = add(polynomial(q, nd - dd, x), remainder);
			simplify(*rewritten, Simp::Basic);
		}

		for (int k = 0; k <= dd; k++)
			ast::dispose(d[k]);
	}

	work::resume();

	if (rewritten != nullptr) {
		rewritten = integral_node(rewritten, x);
		ast *before = integral_node(f.copy(), x);
		work::step(work::Step::Type::Integral, "Divide", before, rewritten);
		ast::dispose(before);
	}

	ast::dispose(numerator);
	ast::dispose(denominator);

	return rewritten;
}

static unsigned liate_rank(ast &h, ast &x) {
	if (h.isOp(Op::Log) && is_constant(*h.childAt(0), x))
		return 5;

	if (h.isOperator()) {
		switch (h.op()) {
			case Op::Sin_Inv:
			case Op::Cos_Inv:
			case Op::Tan_Inv:
			case Op::SinH_Inv:
			case Op::CosH_Inv:
			case Op::TanH_Inv: return 4;
			default: break;
		}
	}

	if (is_polynomial(h, x))
		return 3;

	return 0;
}

/*Rewrites the integral of f by parts, choosing u by LIATE. Records the step.*/
static ast *by_parts(ast &f, ast &x) {
	ast *product = factors_of(f);
	unsigned best = 0, best_index = 0;

	for (unsigned i = 0; i < product->childCount(); i++) {
		ast &factor = *product->childAt(i);

		if (is_constant(factor, x))
			continue;

		const unsigned rank = liate_rank(factor, x);
		if (rank > best) {
			best = rank;
			best_index = i;
		}
	}

	if (best == 0) {
		ast::dispose(product);
		return nullptr;
	}

	ast *u = product->removeChildAt(best_index);
	ast *dv = unwrap(product);

	ast *v = elementary(*dv, x);
	if (v == nullptr) {
		ast::dispose(u);
		ast::dispose(dv);
		return nullptr;
	}

	simplify_quietly(*v);
	ast *du = derivative_of(*u, x);

	ast *remaining = mul(v->copy(), du);
	simplify_quietly(*remaining);

	bool pulled = false;
	ast *rewritten = add(mul(u, v), negate(pull_constants(*remaining, x, &pulled)));
	ast::dispose(remaining);
	simplify_quietly(*rewritten);

	ast *before = integral_node(f.copy(), x);
	work::step(work::Step::Type::Integral, "By parts", before, rewritten);
	ast::dispose(before);

	ast::dispose(dv);
	return rewritten;
}

static bool integrate_all(ast &e, unsigned budget);

/*Evaluates the integral node e in place. Returns true if anything was integrated.*/
static bool integrate_node(ast &e, unsigned budget) {
	bool progress = false;

	if (budget == 0)
		return false;

	ast *f = e.childAt(0)->copy();
	ast *x = e.childAt(1)->copy();
	ast *before = e.copy();

	simplify_quietly(*f);

	ast *F;
	if ((F = elementary(*f, *x)) != nullptr) {
		simplify_quietly(*F);
		work::step(work::Step::Type::Integral, nullptr, before, F);
		e.replace(F);
		progress = true;
	} else if (
		!f->isOp(Op::Add) &&
		((F = divide(*f, *x)) != nullptr || (F = substitution(*f, *x)) != nullptr || (F = by_parts(*f, *x)) != nullptr)
	) {
		e.replace(F);
		integrate_all(e, budget - 1);
		simplify_quietly(e);
		progress = true;
	} else {
		F = split(*f, *x, &progress);

		if (progress) {
			simplify_quietly(*F);
			work::step(work::Step::Type::Integral, nullptr, before, F);
			e.replace(F);
			integrate_all(e, budget - 1);
		} else {
			ast::dispose(F);
			F = f->copy();

			work::pause();
			simplify(*F, Simp::All);
			work::resume();

			if (!F->compare(*f)) {
				F = integral_node(F, *x);
				work::step(work::Step::Type::Equation, "Identity", before, F);
				e.replace(F);
				progress = integrate_node(e, budget - 1);
			} else {
				ast::dispose(F);
			}
		}
	}

	ast::dispose(f);
	ast::dispose(x);
	ast::dispose(before);

	return progress;
}

static bool integrate_all(ast &e, unsigned budget) {
	bool changed = false;

	if (!e.isOperator())
		return false;

	for (ast &child : e.children())
		changed |= integrate_all(child, budget);

	if (e.isOp(Op::Integral))
		changed |= integrate_node(e, budget);

	return changed;
}

bool eval_integrals(ast &e) {
	return integrate_all(e, MAX_METHOD_DEPTH);
}

bool contains_integral(const ast &e) {
	if (e.isOp(Op::Integral))
		return true;

	if (e.isOperator()) {
		for (const ast &child : e.children()) {
			if (contains_integral(child))
				return true;
		}
	}

	return false;
}

static void antiderivative(ast &e, const ast &respect_to, bool constant) {
	work::enter(e);

	ast *node = integral_node(e.copy(), respect_to);
	eval_integrals(*node);

	if (constant && !contains_integral(*node)) {
		node = add(node, ast::make(constant_symbol(*node)));
		work::pause();
		simplify(*node, Simp::Commutative);
		work::resume();
	}

	e.replace(node);

	work::leave(e);
}

void integral(ast &e, const ast &respect_to) {
	antiderivative(e, respect_to, false);
}

void integral_Indefinite(ast &e, const ast &respect_to) {
	antiderivative(e, respect_to, true);
}
