#include <stdcountof.h>
#include "internal.h"

#include "../../work.h"

/*Limits how many operations are undone to solve for the function*/
#define MAX_ISOLATE_STEPS 12

/*Returns the logarithm of e to base. Takes ownership of e.*/
static pcas_ast_t *logarithm(const pcas_ast_t *base, pcas_ast_t *e) {
	return is_euler(base) ? ln(e) : ast_MakeBinary(OP_DIV, ln(e), ln(ast_Copy(base)));
}

/*Returns 1 or -1 when the sign of e is known, otherwise 0*/
static int sign_of(const pcas_ast_t *e) {
	if (e->type == NODE_NUMBER) {
		const int compared = mp_rat_compare_zero(e->op.num);
		return (compared > 0) - (compared < 0);
	}

	if (e->type == NODE_SYMBOL)
		return e->op.symbol == SYM_PI || e->op.symbol == SYM_EULER;

	if (isoptype(e, OP_POW))
		return sign_of(opbase(e)) > 0;

	if (isoptype(e, OP_MULT) || isoptype(e, OP_DIV)) {
		int sign = 1;
		for (const pcas_ast_t *child = opbase(e); child != NULL; child = child->next)
			sign *= sign_of(child);
		return sign;
	}

	return 0;
}

/*Returns the sign of e at the initial condition, or 0 if it is unknown*/
static int sign_at(pcas_de_t *de, const pcas_ast_t *e, pcas_condition_t *c) {
	pcas_ast_t *value = ast_Copy(e);

	work_Pause();
	substitute(value, de->y, c->value);
	substitute(value, de->x, c->at);
	simplify(value, SIMP_ALL);
	work_Resume();

	const int sign = sign_of(value);
	ast_Cleanup(value);

	return sign;
}

/*Returns the index of the only child of e that involves v, or -1*/
static int only_child_with(const pcas_ast_t *e, const pcas_ast_t *v) {
	int found = -1, i = 0;

	for (const pcas_ast_t *child = opbase(e); child != NULL; child = child->next, i++) {
		if (involves(child, v)) {
			if (found >= 0)
				return -1;
			found = i;
		}
	}

	return found;
}

/*Merges constants that are added to or multiply the arbitrary constant into it*/
static void absorb(pcas_de_t *de, pcas_ast_t *e, const pcas_ast_t *constant) {
	if (e->type != NODE_OPERATOR || !involves(e, constant))
		return;

	if (!involves(e, de->x) && !involves(e, de->y)) {
		replace_node(e, ast_Copy(constant));
		return;
	}

	if (isoptype(e, OP_POW) && !involves(opbase(e), de->x) && !involves(opbase(e), de->y)) {
		pcas_ast_t *kept = ast_Copy(opbase(e)->next);
		absorb(de, kept, constant);

		int i;
		if (isoptype(kept, OP_ADD) && (i = only_child_with(kept, constant)) >= 0 &&
			ast_Compare(ast_ChildGet(kept, i), constant)) {
			ast_Cleanup(ast_ChildRemoveIndex(kept, i));
			replace_node(
				e, ast_MakeBinary(OP_MULT, ast_Copy(constant), ast_MakeBinary(OP_POW, ast_Copy(opbase(e)), kept))
			);
		} else {
			ast_Cleanup(kept);
		}

		return;
	}

	int i;
	if ((isoptype(e, OP_ADD) || isoptype(e, OP_MULT)) && (i = only_child_with(e, constant)) >= 0) {
		const pcas_ast_t *term = ast_ChildGet(e, i);

		if (!involves(term, de->x) && !involves(term, de->y)) {
			pcas_ast_t *kept = ast_MakeOperator(optype(e));

			for (const pcas_ast_t *child = opbase(e); child != NULL; child = child->next) {
				if (involves(child, de->x) || involves(child, de->y))
					ast_ChildAppend(kept, ast_Copy(child));
			}

			ast_ChildAppend(kept, ast_Copy(constant));
			replace_node(e, kept);
			return;
		}
	}

	for (pcas_ast_t *child = opbase(e); child != NULL; child = child->next)
		absorb(de, child, constant);
}

/*True if e is the arbitrary constant or a multiple of it*/
static bool is_arbitrary_multiple(const pcas_ast_t *e, const pcas_ast_t *constant) {
	if (constant == NULL)
		return false;

	if (ast_Compare(e, constant))
		return true;

	if (isoptype(e, OP_DIV))
		return is_arbitrary_multiple(opbase(e), constant);

	if (isoptype(e, OP_MULT)) {
		for (const pcas_ast_t *child = opbase(e); child != NULL; child = child->next) {
			if (ast_Compare(child, constant))
				return true;
		}
	}

	return false;
}

static const OperatorType inverse_pairs[][2] = {
	{OP_SIN, OP_SIN_INV},
	{OP_COS, OP_COS_INV},
	{OP_TAN, OP_TAN_INV},
	{OP_SINH, OP_SINH_INV},
	{OP_COSH, OP_COSH_INV},
	{OP_TANH, OP_TANH_INV}
};

/*Returns the inverse of the function op, or AMOUNT_OPS if it has none*/
static OperatorType inverse_of(OperatorType op) {
	for (unsigned i = 0; i < countof(inverse_pairs); i++) {
		if (inverse_pairs[i][0] == op)
			return inverse_pairs[i][1];
		if (inverse_pairs[i][1] == op)
			return inverse_pairs[i][0];
	}

	return AMOUNT_OPS;
}

static pcas_ast_t *reciprocal(const pcas_ast_t *e) {
	if (isoptype(e, OP_POW))
		return ast_MakeBinary(OP_POW, ast_Copy(opbase(e)), negate(ast_Copy(opbase(e)->next)));

	return ast_MakeBinary(OP_POW, ast_Copy(e), integer(-1));
}

/*Writes the quotient a/(b*c) as a*b^-1*c^-1*/
static pcas_ast_t *as_product(const pcas_ast_t *quotient) {
	pcas_ast_t *product = ast_MakeOperator(OP_MULT);

	ast_ChildAppend(product, ast_Copy(opbase(quotient)));
	for (const pcas_ast_t *child = opbase(opbase(quotient)->next); child != NULL; child = child->next)
		ast_ChildAppend(product, reciprocal(child));

	return product;
}

/*Undoes the outermost operation applied to y in lhs = rhs. Returns false if it cannot.*/
static bool isolate_step(
	pcas_de_t *de,
	pcas_ast_t **lhs,
	pcas_ast_t **rhs,
	const pcas_ast_t *constant,
	pcas_condition_t *c
) {
	pcas_ast_t *L = *lhs, *R = *rhs;

	if (L->type != NODE_OPERATOR)
		return false;

	if (isoptype(L, OP_DIV) && !involves(opbase(L), de->y) && isoptype(opbase(L)->next, OP_MULT)) {
		pcas_ast_t *product = as_product(L);
		ast_Cleanup(L);
		*lhs = L = product;
	}

	pcas_ast_t *left, *right;
	OperatorType inverse;
	int sign = 1;

	if (isoptype(L, OP_ADD) || isoptype(L, OP_MULT)) {
		const int i = only_child_with(L, de->y);
		if (i < 0)
			return false;

		pcas_ast_t *rest = ast_Copy(L);
		left = ast_ChildRemoveIndex(rest, i);
		right = isoptype(L, OP_ADD) ? difference(ast_Copy(R), rest) : ast_MakeBinary(OP_DIV, ast_Copy(R), rest);
	} else if (isoptype(L, OP_DIV)) {
		const pcas_ast_t *base = opbase(L);

		if (!involves(base, de->y) && isoptype(base->next, OP_POW) && !involves(opbase(base->next), de->y)) {
			pcas_ast_t *power = negate(ast_Copy(opbase(base->next)->next));

			if (is_ast_int(base, 1)) {
				left = power;
				right = logarithm(opbase(base->next), ast_Copy(R));
			} else {
				left = ast_MakeBinary(OP_POW, ast_Copy(opbase(base->next)), power);
				right = ast_MakeBinary(OP_DIV, ast_Copy(R), ast_Copy(base));
			}
		} else if (!involves(base->next, de->y)) {
			left = ast_Copy(base);
			right = ast_MakeBinary(OP_MULT, ast_Copy(R), ast_Copy(base->next));
		} else if (!involves(base, de->y)) {
			left = ast_Copy(base->next);
			right = ast_MakeBinary(OP_DIV, ast_Copy(base), ast_Copy(R));
		} else {
			return false;
		}
	} else if (isoptype(L, OP_POW)) {
		const pcas_ast_t *base = opbase(L);
		const pcas_ast_t *power = base->next;

		if (!involves(power, de->y)) {
			if (power->type == NODE_NUMBER && mp_int_is_even(MP_NUMER_P(power->op.num))) {
				sign = c != NULL ? sign_at(de, base, c) : 0;
				if (sign == 0)
					return false;
			}

			left = ast_Copy(base);
			right = ast_MakeBinary(OP_POW, ast_Copy(R), ast_MakeBinary(OP_DIV, integer(1), ast_Copy(power)));
		} else if (!involves(base, de->y)) {
			left = ast_Copy(power);
			right = logarithm(base, ast_Copy(R));
		} else {
			return false;
		}
	} else if (isoptype(L, OP_LOG)) {
		const pcas_ast_t *base = opbase(L);

		if (involves(base, de->y))
			return false;

		left = ast_Copy(base->next);
		right = exponential(base, ast_Copy(R));
	} else if (isoptype(L, OP_ABS)) {
		sign = c != NULL ? sign_at(de, opbase(L), c) : is_arbitrary_multiple(R, constant);
		if (sign == 0)
			return false;

		left = ast_Copy(opbase(L));
		right = ast_Copy(R);
	} else if ((inverse = inverse_of(optype(L))) != AMOUNT_OPS) {
		left = ast_Copy(opbase(L));
		right = ast_MakeUnary(inverse, ast_Copy(R));
	} else {
		return false;
	}

	if (sign < 0)
		right = negate(right);

	ast_Cleanup(L);
	ast_Cleanup(R);
	*lhs = left;
	*rhs = right;

	return true;
}

/*Solves lhs = rhs for y, recording each step. The arbitrary constant absorbs other constants unless it is NULL. Returns false if y is left implicit.*/
static bool isolate(
	pcas_de_t *de,
	pcas_ast_t **lhs,
	pcas_ast_t **rhs,
	const pcas_ast_t *constant,
	pcas_condition_t *c
) {
	const char *text = "Solve for the function";
	unsigned steps = 0;
	bool solved = true;

	while (solved && !ast_Compare(*lhs, de->y)) {
		pcas_ast_t *left = ast_Copy(*lhs);
		pcas_ast_t *right = ast_Copy(*rhs);

		solved = steps++ < MAX_ISOLATE_STEPS && isolate_step(de, lhs, rhs, constant, c);

		if (solved) {
			work_Pause();
			simplify(*lhs, SIMP_BASIC);
			simplify(*rhs, SIMP_BASIC);

			if (constant != NULL) {
				expand(*rhs, EXP_DISTRIB_NUMBERS);
				absorb(de, *rhs, constant);
				simplify(*rhs, SIMP_BASIC);
			}
			work_Resume();

			if (steps > 1) {
				work_Step(STEP_EQUATION, text, left, right);
				text = NULL;
			}
		}

		ast_Cleanup(left);
		ast_Cleanup(right);
	}

	return solved;
}

pcas_ast_t *at_condition(pcas_de_t *de, const pcas_ast_t *e, pcas_condition_t *c) {
	pcas_ast_t *copy = ast_Copy(e);

	work_Pause();
	substitute(copy, de->y, c->value);
	substitute(copy, de->x, c->at);
	work_Resume();

	return copy;
}

/*Finds the constant in lhs = antiderivative + constant from the initial condition and substitutes it into rhs*/
static void apply_condition(
	pcas_de_t *de,
	pcas_condition_t *c,
	pcas_ast_t *lhs,
	pcas_ast_t *antiderivative,
	pcas_ast_t **rhs,
	pcas_ast_t *constant
) {
	pcas_ast_t *at = ast_MakeBinary(OP_AT, ast_Copy(de->y), ast_Copy(c->at));
	work_Step(STEP_EQUATION, "Initial condition", at, c->value);
	ast_Cleanup(at);

	pcas_ast_t *left = at_condition(de, lhs, c);
	pcas_ast_t *right = at_condition(de, *rhs, c);
	work_Step(STEP_EQUATION, NULL, left, right);
	ast_Cleanup(right);

	right = at_condition(de, antiderivative, c);

	work_Pause();
	pcas_ast_t *value = difference(left, right);
	simplify(value, SIMP_ALL);
	work_Resume();

	work_Step(STEP_EQUATION, NULL, constant, value);

	work_Pause();
	substitute(*rhs, constant, value);
	simplify(*rhs, SIMP_BASIC);
	work_Resume();

	work_Step(STEP_EQUATION, NULL, lhs, *rhs);
	ast_Cleanup(value);
}

static bool is_monomial(const pcas_ast_t *e) {
	return e->type != NODE_OPERATOR || isoptype(e, OP_POW);
}

/*True if e is one sum multiplied or divided by numbers, symbols and powers*/
static bool is_sum_times_monomials(const pcas_ast_t *e) {
	const pcas_ast_t *numerator = e;
	unsigned sums = 0;

	if (isoptype(e, OP_DIV)) {
		if (!is_monomial(opbase(e)->next))
			return false;
		numerator = opbase(e);
	}

	if (isoptype(numerator, OP_ADD))
		return numerator != e;

	if (!isoptype(numerator, OP_MULT))
		return false;

	for (const pcas_ast_t *child = opbase(numerator); child != NULL; child = child->next) {
		if (isoptype(child, OP_ADD))
			sums++;
		else if (!is_monomial(child))
			return false;
	}

	return sums == 1;
}

static void distribute_over_monomials(pcas_ast_t *e) {
	if (!is_sum_times_monomials(e))
		return;

	work_Pause();
	expand(e, EXP_ALL);
	simplify(e, SIMP_BASIC);
	work_Resume();
}

/*Writes products of absolute values in e as one absolute value*/
static void merge_absolute_values(pcas_ast_t *e) {
	unsigned count = 0;

	if (e->type != NODE_OPERATOR)
		return;

	for (pcas_ast_t *child = opbase(e); child != NULL; child = child->next) {
		merge_absolute_values(child);
		count += isoptype(child, OP_ABS);
	}

	if (!isoptype(e, OP_MULT) || count < 2)
		return;

	pcas_ast_t *merged = ast_MakeOperator(OP_MULT);
	pcas_ast_t *inside = ast_MakeOperator(OP_MULT);

	for (const pcas_ast_t *child = opbase(e); child != NULL; child = child->next) {
		if (isoptype(child, OP_ABS))
			ast_ChildAppend(inside, ast_Copy(opbase(child)));
		else
			ast_ChildAppend(merged, ast_Copy(child));
	}

	ast_ChildAppend(merged, ast_MakeUnary(OP_ABS, inside));
	replace_node(e, merged);
}

/*Returns the least common multiple of the integer denominators of the terms of e*/
static pcas_ast_t *common_denominator(const pcas_ast_t *e) {
	mp_rat multiple = num_FromInt(1);

	for (pcas_ast_t *child = opbase(e); child != NULL; child = child->next) {
		pcas_ast_t *numerator, *denominator;
		rational_parts(child, &numerator, &denominator);
		simplify(denominator, SIMP_BASIC);

		if (denominator->type == NODE_NUMBER && mp_rat_is_integer(denominator->op.num))
			mp_int_lcm(MP_NUMER_P(multiple), MP_NUMER_P(denominator->op.num), MP_NUMER_P(multiple));

		ast_Cleanup(numerator);
		ast_Cleanup(denominator);
	}

	return ast_MakeNumber(multiple);
}

/*Multiplies both sides by factor, distributing it over sums*/
static void scale_sides(pcas_ast_t **lhs, pcas_ast_t **rhs, const pcas_ast_t *factor) {
	*lhs = ast_MakeBinary(OP_MULT, ast_Copy(factor), *lhs);
	*rhs = ast_MakeBinary(OP_MULT, ast_Copy(factor), *rhs);
	expand(*lhs, EXP_DISTRIB_NUMBERS);
	expand(*rhs, EXP_DISTRIB_NUMBERS);
	simplify(*lhs, SIMP_BASIC);
	simplify(*rhs, SIMP_BASIC);
}

/*Moves the terms of lhs = rhs that involve x or y to the left, then clears numeric denominators and a leading minus sign*/
static void collect(pcas_de_t *de, pcas_ast_t **lhs, pcas_ast_t **rhs, const pcas_ast_t *constant) {
	if (!isoptype(*lhs, OP_ADD) || !involves(*lhs, de->x))
		return;

	work_Pause();
	pcas_ast_t *L = ast_Copy(*lhs);
	pcas_ast_t *R = ast_MakeOperator(OP_ADD);
	pcas_ast_t *terms = ast_MakeBinary(OP_ADD, ast_Copy(*rhs), integer(0));
	simplify(terms, SIMP_COMMUTATIVE);

	for (const pcas_ast_t *child = opbase(terms); child != NULL; child = child->next) {
		if (involves(child, de->x) || involves(child, de->y))
			L = difference(L, ast_Copy(child));
		else
			ast_ChildAppend(R, ast_Copy(child));
	}

	ast_Cleanup(terms);
	ast_ChildAppend(R, integer(0));
	simplify(L, SIMP_BASIC);
	simplify(R, SIMP_BASIC);

	if (isoptype(L, OP_ADD)) {
		pcas_ast_t *scale = common_denominator(L);
		scale_sides(&L, &R, scale);
		ast_Cleanup(scale);
	}

	if (isoptype(L, OP_ADD)) {
		bool negative = true;
		for (const pcas_ast_t *child = opbase(L); child != NULL; child = child->next)
			negative &= is_negative_for_sure(child);

		if (negative) {
			pcas_ast_t *scale = integer(-1);
			scale_sides(&L, &R, scale);
			ast_Cleanup(scale);
		}
	}

	if (constant != NULL)
		absorb(de, R, constant);

	pcas_ast_t *combined = ast_Copy(L);
	simplify(combined, SIMP_ALL);
	merge_absolute_values(combined);
	simplify(combined, SIMP_ALL);
	work_Resume();

	if (node_count(combined) < node_count(L)) {
		ast_Cleanup(L);
		L = combined;
	} else {
		ast_Cleanup(combined);
	}

	ast_Cleanup(*lhs);
	ast_Cleanup(*rhs);
	*lhs = L;
	*rhs = R;
}

void conclude(
	pcas_de_t *de,
	pcas_ast_t *lhs,
	pcas_ast_t *rhs,
	const pcas_ast_t *constant,
	pcas_condition_t *c,
	pcas_ast_t **solution
) {
	pcas_ast_t *left = ast_Copy(lhs), *right = ast_Copy(rhs);
	const bool explicit = isolate(de, &lhs, &rhs, c == NULL ? constant : NULL, c);

	if (explicit)
		distribute_over_monomials(rhs);
	else
		collect(de, &lhs, &rhs, c == NULL ? constant : NULL);

	if (!de->nested || !ast_Compare(left, lhs) || !ast_Compare(right, rhs))
		work_Step(STEP_EQUATION, de->nested ? NULL : explicit ? "Solution" : "Implicit solution", lhs, rhs);

	*solution = ast_MakeBinary(OP_EQUALS, lhs, rhs);

	ast_Cleanup(left);
	ast_Cleanup(right);
}

void finish(pcas_de_t *de, pcas_ast_t *lhs, pcas_ast_t *antiderivative, pcas_ast_t **solution) {
	pcas_condition_t *c = de->condition_count > 0 ? &de->conditions[0] : NULL;
	pcas_ast_t *constant = ast_MakeSymbol(constant_symbol(de->equation));

	work_Pause();
	simplify(lhs, SIMP_BASIC);
	simplify(antiderivative, SIMP_BASIC);
	pcas_ast_t *rhs;
	if (is_ast_int(antiderivative, 0)) {
		rhs = ast_Copy(constant);
	} else {
		rhs = ast_MakeBinary(OP_ADD, ast_Copy(antiderivative), ast_Copy(constant));
		simplify(rhs, SIMP_COMMUTATIVE);
	}
	work_Resume();

	work_Step(STEP_EQUATION, NULL, lhs, rhs);

	if (c != NULL)
		apply_condition(de, c, lhs, antiderivative, &rhs, constant);

	conclude(de, lhs, rhs, constant, c, solution);

	ast_Cleanup(antiderivative);
	ast_Cleanup(constant);
}
