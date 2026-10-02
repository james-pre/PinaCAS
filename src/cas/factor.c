#include "cas.h"

#include "../work.h"

/*Handles gcd for AB, BC gcd = B*/
static pcas_ast_t *gcd_mult(pcas_ast_t *mult, pcas_ast_t *b) {
	unsigned i;
	pcas_ast_t *current_gcd, *copy;

	copy = ast_Copy(b);

	current_gcd = ast_MakeOperator(OP_MULT);

	for (i = 0; i < ast_ChildLength(mult); i++) {
		pcas_ast_t *inner_gcd;
		pcas_ast_t *child = ast_ChildGet(mult, i);

		inner_gcd = gcd(child, copy);

		ast_ChildAppend(current_gcd, inner_gcd);

		replace_node(copy, ast_MakeBinary(OP_DIV, ast_Copy(copy), ast_Copy(inner_gcd)));

		simplify(copy, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL);
	}

	ast_Cleanup(copy);

	return current_gcd;
}

/*Handles gcd for A + AB, A gcd = A*/
static pcas_ast_t *gcd_add(pcas_ast_t *add, pcas_ast_t *b) {
	unsigned i;
	pcas_ast_t *current_gcd;

	current_gcd = gcd(ast_ChildGet(add, 0), b);

	for (i = 1; i < ast_ChildLength(add); i++) {
		pcas_ast_t *temp_gcd, *child;

		child = ast_ChildGet(add, i);
		temp_gcd = gcd(current_gcd, child);

		ast_Cleanup(current_gcd);
		current_gcd = temp_gcd;
	}

	return current_gcd;
}

static pcas_ast_t *gcd_div(pcas_ast_t *div, pcas_ast_t *b) {
	pcas_ast_t *num1, *num2, *den1, *den2, *num_g, *den_g;

	num1 = ast_ChildGet(div, 0);
	den1 = ast_ChildGet(div, 1);

	if (isoptype(b, OP_DIV)) {
		num2 = ast_ChildGet(b, 0);
		den2 = ast_ChildGet(b, 1);
	} else {
		num2 = b;
		den2 = ast_MakeNumber(num_FromInt(1));
	}

	num_g = gcd(num1, num2);
	den_g = gcd(den1, den2);

	if (!isoptype(b, OP_DIV))
		ast_Cleanup(den2);

	return ast_MakeBinary(OP_DIV, num_g, den_g);
}

/*gcd of both bases, raised to smallest power*/
static pcas_ast_t *gcd_pow(pcas_ast_t *pow, pcas_ast_t *b) {
	pcas_ast_t *base1, *power1;

	base1 = ast_ChildGet(pow, 0);
	power1 = ast_ChildGet(pow, 1);

	/*We purposefully ignore A^X, A^Y and only yield when
    we can determine the numerical powers*/

	if (isoptype(b, OP_POW)) {
		pcas_ast_t *power2, *base2;

		power1 = ast_ChildGet(pow, 1);
		base2 = ast_ChildGet(b, 0);
		power2 = ast_ChildGet(b, 1);

		if (power1->type == NODE_NUMBER && power2->type == NODE_NUMBER) {
			bool use_first;
			pcas_ast_t *current_gcd;

			use_first = mp_rat_compare(power1->op.num, power2->op.num) < 0;

			current_gcd = gcd(base1, base2);

			return ast_MakeBinary(OP_POW, current_gcd, use_first ? ast_Copy(power1) : ast_Copy(power2));
		}

	} else if (
		power1->type == NODE_NUMBER && mp_rat_compare_value(power1->op.num, 1, 1) >= 0 && ast_Compare(base1, b)
	) {
		return ast_Copy(b);
	}

	return ast_MakeNumber(num_FromInt(1));
}

pcas_ast_t *gcd(pcas_ast_t *a, pcas_ast_t *b) {
	pcas_ast_t *ret = NULL;

	if (ast_Compare(a, b))
		return ast_Copy(a);

	if (a->type == NODE_NUMBER && b->type == NODE_NUMBER) {
		if (mp_rat_is_integer(a->op.num) && mp_rat_is_integer(b->op.num)) {
			mp_rat gcd;
			gcd = num_FromInt(1);

			mp_int_gcd(&a->op.num->num, &b->op.num->num, &gcd->num);

			return ast_MakeNumber(gcd);
		}
	}

	if (isoptype(a, OP_MULT))
		ret = gcd_mult(a, b);
	else if (isoptype(b, OP_MULT))
		ret = gcd_mult(b, a);
	else if (isoptype(a, OP_ADD))
		ret = gcd_add(a, b);
	else if (isoptype(b, OP_ADD))
		ret = gcd_add(b, a);
	else if (isoptype(a, OP_POW))
		ret = gcd_pow(a, b);
	else if (isoptype(b, OP_POW))
		ret = gcd_pow(b, a);
	else if (isoptype(a, OP_DIV))
		ret = gcd_div(a, b);
	else if (isoptype(b, OP_DIV))
		ret = gcd_div(b, a);

	if (ret != NULL) {
		/*Don't simplify identities*/
		simplify(ret, SIMP_ALL ^ SIMP_ID_ALL);
		return ret;
	}

	return ast_MakeNumber(num_FromInt(1));
}

/*Collects the non-numeric factors of e, with divisors in den*/
static void collect_symbolic_factors(pcas_ast_t *e, pcas_ast_t *num, pcas_ast_t *den) {
	pcas_ast_t *child;

	if (isoptype(e, OP_MULT)) {
		for (child = opbase(e); child != NULL; child = child->next)
			collect_symbolic_factors(child, num, den);
	} else if (isoptype(e, OP_DIV)) {
		collect_symbolic_factors(opbase(e), num, den);
		collect_symbolic_factors(opbase(e)->next, den, num);
	} else if (e->type != NODE_NUMBER) {
		ast_ChildAppend(num, ast_Copy(e));
	}
}

/*Takes ownership of neither. True if a and b have the same children in any order.*/
static bool same_children(pcas_ast_t *a, pcas_ast_t *b) {
	pcas_ast_t *remaining = ast_Copy(b);
	pcas_ast_t *child;
	bool same = ast_ChildLength(a) == ast_ChildLength(b);

	for (child = opbase(a); child != NULL && same; child = child->next) {
		unsigned j;

		same = false;
		for (j = 0; j < ast_ChildLength(remaining); j++) {
			if (ast_Compare(child, ast_ChildGet(remaining, j))) {
				ast_Cleanup(ast_ChildRemoveIndex(remaining, j));
				same = true;
				break;
			}
		}
	}

	ast_Cleanup(remaining);
	return same;
}

/*True if a and b differ only by numeric factors, so they can be like terms*/
static bool same_symbolic_part(pcas_ast_t *a, pcas_ast_t *b) {
	pcas_ast_t *a_num = ast_MakeOperator(OP_MULT), *a_den = ast_MakeOperator(OP_MULT);
	pcas_ast_t *b_num = ast_MakeOperator(OP_MULT), *b_den = ast_MakeOperator(OP_MULT);
	bool same;

	collect_symbolic_factors(a, a_num, a_den);
	collect_symbolic_factors(b, b_num, b_den);

	same = same_children(a_num, b_num) && same_children(a_den, b_den);

	ast_Cleanup(a_num);
	ast_Cleanup(a_den);
	ast_Cleanup(b_num);
	ast_Cleanup(b_den);

	return same;
}

bool factor_addition(pcas_ast_t *e, unsigned char flags) {
	pcas_ast_t *child;
	bool changed = false;

	if (e->type != NODE_OPERATOR)
		return false;

	for (child = ast_ChildGet(e, 0); child != NULL; child = child->next)
		changed |= factor_addition(child, flags);

	if (isoptype(e, OP_ADD)) {
		unsigned i, j;
		pcas_ast_t *g;

		for (i = 0; i < ast_ChildLength(e); i++) {
			pcas_ast_t *a = ast_ChildGet(e, i);

			for (j = i + 1; j < ast_ChildLength(e); j++) {
				pcas_ast_t *b = ast_ChildGet(e, j);

				if (!(flags & FAC_SIMPLE_ADDITION_NONEVALUATEABLE) && !same_symbolic_part(a, b))
					continue;

				g = gcd(a, b);

				if (!is_ast_int(g, 1)) {
					bool can_factor;
					pcas_ast_t *append, *first, *second;

					first = ast_MakeBinary(OP_DIV, ast_Copy(a), ast_Copy(g));
					second = ast_MakeBinary(OP_DIV, ast_Copy(b), ast_Copy(g));

					append = ast_MakeBinary(OP_MULT, ast_Copy(g), ast_MakeBinary(OP_ADD, first, second));

					simplify(first, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL);
					simplify(second, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL);

					if (first->type == NODE_NUMBER && second->type == NODE_NUMBER)
						can_factor = flags & FAC_SIMPLE_ADDITION_EVALUATEABLE;
					else
						can_factor = flags & FAC_SIMPLE_ADDITION_NONEVALUATEABLE;

					if (can_factor) {
						simplify(append, SIMP_NORMALIZE | SIMP_RATIONAL | SIMP_EVAL);
						ast_ChildAppend(e, append);

						ast_Cleanup(ast_ChildRemove(e, a));
						ast_Cleanup(ast_ChildRemove(e, b));

						ast_Cleanup(g);

						changed = true;
						break;
					} else {
						ast_Cleanup(append);
						ast_Cleanup(g);
					}

				} else {
					ast_Cleanup(g);
				}
			}
		}
	}

	/*Take care of add nodes with one child*/
	simplify(e, SIMP_COMMUTATIVE);

	return changed;
}

static bool _factor(pcas_ast_t *e, unsigned char flags) {
	bool changed = false;

	if (flags & (FAC_SIMPLE_ADDITION_EVALUATEABLE | FAC_SIMPLE_ADDITION_NONEVALUATEABLE))
		changed |= factor_addition(e, flags);
	/*Need to implement polynomial factoring*/
	return changed;
}

bool factor(pcas_ast_t *e, unsigned char flags) {
	bool changed;
	work_Enter(e);
	changed = _factor(e, flags);
	work_Leave(e);
	return changed;
}
