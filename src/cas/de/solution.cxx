#include "../../countof.hxx"
#include "internal.hxx"

#include "../../work.hxx"

/*Limits how many operations are undone to solve for the function*/
#define MAX_ISOLATE_STEPS 12

/*Returns the logarithm of e to base. Takes ownership of e.*/
static ast *logarithm(const ast *base, ast *e) {
	return is_euler(base) ? ln(e) : ast::make(Op::Div, ln(e), ln(base->copy()));
}

/*Returns 1 or -1 when the sign of e is known, otherwise 0*/
static int sign_of(const ast *e) {
	if (e->isNumber()) {
		const int compared = num::compare(e->num(), 0);
		return (compared > 0) - (compared < 0);
	}

	if (e->isSymbol())
		return e->symbol() == Sym::Pi || e->symbol() == Sym::Euler;

	if (e->isOp(Op::Pow))
		return sign_of(e->firstChild()) > 0;

	if (e->isOp(Op::Mult) || e->isOp(Op::Div)) {
		int sign = 1;
		for (const ast *child : e->children())
			sign *= sign_of(child);
		return sign;
	}

	return 0;
}

/*Returns the sign of e at the initial condition, or 0 if it is unknown*/
static int sign_at(DiffEq *de, const ast *e, DiffEq::Condition *c) {
	ast *value = e->copy();

	work::pause();
	substitute(value, de->y, c->value);
	substitute(value, de->x, c->at);
	simplify(value, Simp::All);
	work::resume();

	const int sign = sign_of(value);
	ast::dispose(value);

	return sign;
}

/*Returns the index of the only child of e that involves v, or -1*/
static int only_child_with(const ast *e, const ast *v) {
	int found = -1, i = 0;

	for (const ast *child = e->firstChild(); child != nullptr; child = child->next(), i++) {
		if (involves(child, v)) {
			if (found >= 0)
				return -1;
			found = i;
		}
	}

	return found;
}

/*Merges constants that are added to or multiply the arbitrary constant into it*/
static void absorb(DiffEq *de, ast *e, const ast *constant) {
	if (!e->isOperator() || !involves(e, constant))
		return;

	if (!involves(e, de->x) && !involves(e, de->y)) {
		e->replace(constant->copy());
		return;
	}

	if (e->isOp(Op::Pow) && !involves(e->firstChild(), de->x) && !involves(e->firstChild(), de->y)) {
		ast *kept = e->firstChild()->next()->copy();
		absorb(de, kept, constant);

		int i;
		if (kept->isOp(Op::Add) && (i = only_child_with(kept, constant)) >= 0 && kept->childAt(i)->compare(*constant)) {
			ast::dispose(kept->removeChildAt(i));
			e->replace(ast::make(Op::Mult, constant->copy(), ast::make(Op::Pow, e->firstChild()->copy(), kept)));
		} else {
			ast::dispose(kept);
		}

		return;
	}

	int i;
	if ((e->isOp(Op::Add) || e->isOp(Op::Mult)) && (i = only_child_with(e, constant)) >= 0) {
		const ast *term = e->childAt(i);

		if (!involves(term, de->x) && !involves(term, de->y)) {
			ast *kept = ast::make(e->op());

			for (const ast *child : e->children()) {
				if (involves(child, de->x) || involves(child, de->y))
					kept->appendChild(child->copy());
			}

			kept->appendChild(constant->copy());
			e->replace(kept);
			return;
		}
	}

	for (ast *child : e->children())
		absorb(de, child, constant);
}

/*True if e is the arbitrary constant or a multiple of it*/
static bool is_arbitrary_multiple(const ast *e, const ast *constant) {
	if (constant == nullptr)
		return false;

	if (e->compare(*constant))
		return true;

	if (e->isOp(Op::Div))
		return is_arbitrary_multiple(e->firstChild(), constant);

	if (e->isOp(Op::Mult)) {
		for (const ast *child : e->children()) {
			if (child->compare(*constant))
				return true;
		}
	}

	return false;
}

static const Op inverse_pairs[][2] = {
	{Op::Sin, Op::Sin_Inv},
	{Op::Cos, Op::Cos_Inv},
	{Op::Tan, Op::Tan_Inv},
	{Op::SinH, Op::SinH_Inv},
	{Op::CosH, Op::CosH_Inv},
	{Op::TanH, Op::TanH_Inv}
};

/*Returns the inverse of the function op, or Op::Amount if it has none*/
static Op inverse_of(Op op) {
	for (unsigned i = 0; i < countof(inverse_pairs); i++) {
		if (inverse_pairs[i][0] == op)
			return inverse_pairs[i][1];
		if (inverse_pairs[i][1] == op)
			return inverse_pairs[i][0];
	}

	return Op::Amount;
}

static ast *reciprocal(const ast *e) {
	if (e->isOp(Op::Pow))
		return ast::make(Op::Pow, e->firstChild()->copy(), negate(e->firstChild()->next()->copy()));

	return ast::make(Op::Pow, e->copy(), integer(-1));
}

/*Writes the quotient a/(b*c) as a*b^-1*c^-1*/
static ast *as_product(const ast *quotient) {
	ast *product = ast::make(Op::Mult);

	product->appendChild(quotient->firstChild()->copy());
	for (const ast *child : quotient->firstChild()->next()->children())
		product->appendChild(reciprocal(child));

	return product;
}

/*Undoes the outermost operation applied to y in lhs = rhs. Returns false if it cannot.*/
static bool isolate_step(DiffEq *de, ast **lhs, ast **rhs, const ast *constant, DiffEq::Condition *c) {
	ast *L = *lhs, *R = *rhs;

	if (!L->isOperator())
		return false;

	if (L->isOp(Op::Div) && !involves(L->firstChild(), de->y) && L->firstChild()->next()->isOp(Op::Mult)) {
		ast *product = as_product(L);
		ast::dispose(L);
		*lhs = L = product;
	}

	ast *left, *right;
	Op inverse;
	int sign = 1;

	if (L->isOp(Op::Add) || L->isOp(Op::Mult)) {
		const int i = only_child_with(L, de->y);
		if (i < 0)
			return false;

		ast *rest = L->copy();
		left = rest->removeChildAt(i);
		right = L->isOp(Op::Add) ? difference(R->copy(), rest) : ast::make(Op::Div, R->copy(), rest);
	} else if (L->isOp(Op::Div)) {
		const ast *base = L->firstChild();

		if (!involves(base, de->y) && base->next()->isOp(Op::Pow) && !involves(base->next()->firstChild(), de->y)) {
			ast *power = negate(base->next()->firstChild()->next()->copy());

			if (base->isInt(1)) {
				left = power;
				right = logarithm(base->next()->firstChild(), R->copy());
			} else {
				left = ast::make(Op::Pow, base->next()->firstChild()->copy(), power);
				right = ast::make(Op::Div, R->copy(), base->copy());
			}
		} else if (!involves(base->next(), de->y)) {
			left = base->copy();
			right = ast::make(Op::Mult, R->copy(), base->next()->copy());
		} else if (!involves(base, de->y)) {
			left = base->next()->copy();
			right = ast::make(Op::Div, base->copy(), R->copy());
		} else {
			return false;
		}
	} else if (L->isOp(Op::Pow)) {
		const ast *base = L->firstChild();
		const ast *power = base->next();

		if (!involves(power, de->y)) {
			if (power->isNumber() && mp_int_is_even(MP_NUMER_P(&power->num()))) {
				sign = c != nullptr ? sign_at(de, base, c) : 0;
				if (sign == 0)
					return false;
			}

			left = base->copy();
			right = ast::make(Op::Pow, R->copy(), ast::make(Op::Div, integer(1), power->copy()));
		} else if (!involves(base, de->y)) {
			left = power->copy();
			right = logarithm(base, R->copy());
		} else {
			return false;
		}
	} else if (L->isOp(Op::Log)) {
		const ast *base = L->firstChild();

		if (involves(base, de->y))
			return false;

		left = base->next()->copy();
		right = exponential(base, R->copy());
	} else if (L->isOp(Op::Abs)) {
		sign = c != nullptr ? sign_at(de, L->firstChild(), c) : is_arbitrary_multiple(R, constant);
		if (sign == 0)
			return false;

		left = L->firstChild()->copy();
		right = R->copy();
	} else if ((inverse = inverse_of(L->op())) != Op::Amount) {
		left = L->firstChild()->copy();
		right = ast::make(inverse, R->copy());
	} else {
		return false;
	}

	if (sign < 0)
		right = negate(right);

	ast::dispose(L);
	ast::dispose(R);
	*lhs = left;
	*rhs = right;

	return true;
}

/*Solves lhs = rhs for y, recording each step. The arbitrary constant absorbs other constants unless it is nullptr. Returns false if y is left implicit.*/
static bool isolate(DiffEq *de, ast **lhs, ast **rhs, const ast *constant, DiffEq::Condition *c) {
	const char *text = "Solve for the function";
	unsigned steps = 0;
	bool solved = true;

	while (solved && !(*lhs)->compare(*de->y)) {
		ast *left = (*lhs)->copy();
		ast *right = (*rhs)->copy();

		solved = steps++ < MAX_ISOLATE_STEPS && isolate_step(de, lhs, rhs, constant, c);

		if (solved) {
			work::pause();
			simplify(*lhs, Simp::Basic);
			simplify(*rhs, Simp::Basic);

			if (constant != nullptr) {
				expand(*rhs, Expand::DistribNumbers);
				absorb(de, *rhs, constant);
				simplify(*rhs, Simp::Basic);
			}
			work::resume();

			if (steps > 1) {
				work::step(work::Step::Type::Equation, text, left, right);
				text = nullptr;
			}
		}

		ast::dispose(left);
		ast::dispose(right);
	}

	return solved;
}

ast *at_condition(DiffEq *de, const ast *e, DiffEq::Condition *c) {
	ast *copy = e->copy();

	work::pause();
	substitute(copy, de->y, c->value);
	substitute(copy, de->x, c->at);
	work::resume();

	return copy;
}

/*Finds the constant in lhs = antiderivative + constant from the initial condition and substitutes it into rhs*/
static void apply_condition(DiffEq *de, DiffEq::Condition *c, ast *lhs, ast *antiderivative, ast **rhs, ast *constant) {
	ast *at = ast::make(Op::At, de->y->copy(), c->at->copy());
	work::step(work::Step::Type::Equation, "Initial condition", at, c->value);
	ast::dispose(at);

	ast *left = at_condition(de, lhs, c);
	ast *right = at_condition(de, *rhs, c);
	work::step(work::Step::Type::Equation, nullptr, left, right);
	ast::dispose(right);

	right = at_condition(de, antiderivative, c);

	work::pause();
	ast *value = difference(left, right);
	simplify(value, Simp::All);
	work::resume();

	work::step(work::Step::Type::Equation, nullptr, constant, value);

	work::pause();
	substitute(*rhs, constant, value);
	simplify(*rhs, Simp::Basic);
	work::resume();

	work::step(work::Step::Type::Equation, nullptr, lhs, *rhs);
	ast::dispose(value);
}

static bool is_monomial(const ast *e) {
	return !e->isOperator() || e->isOp(Op::Pow);
}

/*True if e is one sum multiplied or divided by numbers, symbols and powers*/
static bool is_sum_times_monomials(const ast *e) {
	const ast *numerator = e;
	unsigned sums = 0;

	if (e->isOp(Op::Div)) {
		if (!is_monomial(e->firstChild()->next()))
			return false;
		numerator = e->firstChild();
	}

	if (numerator->isOp(Op::Add))
		return numerator != e;

	if (!numerator->isOp(Op::Mult))
		return false;

	for (const ast *child : numerator->children()) {
		if (child->isOp(Op::Add))
			sums++;
		else if (!is_monomial(child))
			return false;
	}

	return sums == 1;
}

static void distribute_over_monomials(ast *e) {
	if (!is_sum_times_monomials(e))
		return;

	work::pause();
	expand(e, Expand::All);
	simplify(e, Simp::Basic);
	work::resume();
}

/*Writes products of absolute values in e as one absolute value*/
static void merge_absolute_values(ast *e) {
	unsigned count = 0;

	if (!e->isOperator())
		return;

	for (ast *child : e->children()) {
		merge_absolute_values(child);
		count += child->isOp(Op::Abs);
	}

	if (!e->isOp(Op::Mult) || count < 2)
		return;

	ast *merged = ast::make(Op::Mult);
	ast *inside = ast::make(Op::Mult);

	for (const ast *child : e->children()) {
		if (child->isOp(Op::Abs))
			inside->appendChild(child->firstChild()->copy());
		else
			merged->appendChild(child->copy());
	}

	merged->appendChild(ast::make(Op::Abs, inside));
	e->replace(merged);
}

/*Returns the least common multiple of the integer denominators of the terms of e*/
static ast *common_denominator(const ast *e) {
	num *multiple = num::from(1);

	for (const ast *child : e->children()) {
		ast *numerator, *denominator;
		rational_parts(child, &numerator, &denominator);
		simplify(denominator, Simp::Basic);

		if (denominator->isNumber() && denominator->num().isInteger())
			mp_int_lcm(MP_NUMER_P(multiple), MP_NUMER_P(&denominator->num()), MP_NUMER_P(multiple));

		ast::dispose(numerator);
		ast::dispose(denominator);
	}

	return ast::make(multiple);
}

/*Multiplies both sides by factor, distributing it over sums*/
static void scale_sides(ast **lhs, ast **rhs, const ast *factor) {
	*lhs = ast::make(Op::Mult, factor->copy(), *lhs);
	*rhs = ast::make(Op::Mult, factor->copy(), *rhs);
	expand(*lhs, Expand::DistribNumbers);
	expand(*rhs, Expand::DistribNumbers);
	simplify(*lhs, Simp::Basic);
	simplify(*rhs, Simp::Basic);
}

/*Moves the terms of lhs = rhs that involve x or y to the left, then clears numeric denominators and a leading minus sign*/
static void collect(DiffEq *de, ast **lhs, ast **rhs, const ast *constant) {
	if (!(*lhs)->isOp(Op::Add) || !involves(*lhs, de->x))
		return;

	work::pause();
	ast *L = (*lhs)->copy();
	ast *R = ast::make(Op::Add);
	ast *terms = ast::make(Op::Add, (*rhs)->copy(), integer(0));
	simplify(terms, Simp::Commutative);

	for (const ast *child : terms->children()) {
		if (involves(child, de->x) || involves(child, de->y))
			L = difference(L, child->copy());
		else
			R->appendChild(child->copy());
	}

	ast::dispose(terms);
	R->appendChild(integer(0));
	simplify(L, Simp::Basic);
	simplify(R, Simp::Basic);

	if (L->isOp(Op::Add)) {
		ast *scale = common_denominator(L);
		scale_sides(&L, &R, scale);
		ast::dispose(scale);
	}

	if (L->isOp(Op::Add)) {
		bool negative = true;
		for (const ast *child : L->children())
			negative &= is_negative_for_sure(child);

		if (negative) {
			ast *scale = integer(-1);
			scale_sides(&L, &R, scale);
			ast::dispose(scale);
		}
	}

	if (constant != nullptr)
		absorb(de, R, constant);

	ast *combined = L->copy();
	simplify(combined, Simp::All);
	merge_absolute_values(combined);
	simplify(combined, Simp::All);
	work::resume();

	if (node_count(combined) < node_count(L)) {
		ast::dispose(L);
		L = combined;
	} else {
		ast::dispose(combined);
	}

	ast::dispose(*lhs);
	ast::dispose(*rhs);
	*lhs = L;
	*rhs = R;
}

void conclude(DiffEq *de, ast *lhs, ast *rhs, const ast *constant, DiffEq::Condition *c, ast **solution) {
	ast *left = lhs->copy(), *right = rhs->copy();
	const bool is_explicit = isolate(de, &lhs, &rhs, c == nullptr ? constant : nullptr, c);

	if (is_explicit)
		distribute_over_monomials(rhs);
	else
		collect(de, &lhs, &rhs, c == nullptr ? constant : nullptr);

	if (!de->nested || !left->compare(*lhs) || !right->compare(*rhs))
		work::step(
			work::Step::Type::Equation,
			de->nested    ? nullptr
			: is_explicit ? "Solution"
						  : "Implicit solution",
			lhs,
			rhs
		);

	*solution = ast::make(Op::Equals, lhs, rhs);

	ast::dispose(left);
	ast::dispose(right);
}

void finish(DiffEq *de, ast *lhs, ast *antiderivative, ast **solution) {
	DiffEq::Condition *c = de->condition_count > 0 ? &de->conditions[0] : nullptr;
	ast *constant = ast::make(constant_symbol(de->equation));

	work::pause();
	simplify(lhs, Simp::Basic);
	simplify(antiderivative, Simp::Basic);
	ast *rhs;
	if (antiderivative->isInt(0)) {
		rhs = constant->copy();
	} else {
		rhs = ast::make(Op::Add, antiderivative->copy(), constant->copy());
		simplify(rhs, Simp::Commutative);
	}
	work::resume();

	work::step(work::Step::Type::Equation, nullptr, lhs, rhs);

	if (c != nullptr)
		apply_condition(de, c, lhs, antiderivative, &rhs, constant);

	conclude(de, lhs, rhs, constant, c, solution);

	ast::dispose(antiderivative);
	ast::dispose(constant);
}
