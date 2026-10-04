#include "cas.h"

#include "../work.h"

#define SIMP_BASIC (SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL | SIMP_LIKE_TERMS)

/*Limits how many operations are undone to solve for the function*/
#define MAX_ISOLATE_STEPS 12

static pcas_ast_t *integer(mp_small n) {
	return ast_MakeNumber(num_FromInt(n));
}

static pcas_ast_t *negate(pcas_ast_t *a) {
	return ast_MakeBinary(OP_MULT, integer(-1), a);
}

static pcas_ast_t *difference(pcas_ast_t *a, pcas_ast_t *b) {
	return ast_MakeBinary(OP_ADD, a, negate(b));
}

static const char *order_names[DE_MAX_ORDER] = {
	"First order",
	"Second order",
	"Third order",
	"Fourth order",
	"Fifth order",
	"Sixth order",
	"Seventh order",
	"Eighth order"
};

pcas_ast_t *de_Derivative(const pcas_ast_t *y, unsigned order) {
	pcas_ast_t *d = ast_Copy(y);

	while (order-- > 0)
		d = ast_MakeUnary(OP_PRIME, d);

	return d;
}

/*Finds the function that primes are applied to and the highest number of primes on it*/
static pcas_error_t find_function(pcas_ast_t *e, pcas_ast_t **y, unsigned *order) {
	pcas_ast_t *child;
	pcas_error_t err;

	if (isoptype(e, OP_PRIME)) {
		unsigned primes = 0;

		while (isoptype(e, OP_PRIME)) {
			e = ast_ChildGet(e, 0);
			primes++;
		}

		if (e->type != NODE_SYMBOL || (*y != NULL && !ast_Compare(*y, e)))
			return E_DE_BAD_FUNCTION;

		*y = e;
		if (primes > *order)
			*order = primes;

		return E_SUCCESS;
	}

	if (e->type == NODE_OPERATOR) {
		for (child = ast_ChildGet(e, 0); child != NULL; child = child->next) {
			if ((err = find_function(child, y, order)) != E_SUCCESS)
				return err;
		}
	}

	return E_SUCCESS;
}

/*Replaces y, y', ..., y^(order) in f with distinct symbols that are not x, so the derivatives can be treated as variables*/
static void derivatives_to_symbols(pcas_de_t *de, pcas_ast_t *f, pcas_ast_t **symbols) {
	pcas_ast_t *scope = ast_MakeBinary(OP_ADD, ast_Copy(f), ast_Copy(de->x));
	unsigned k;

	for (k = de->order; k > 0; k--) {
		pcas_ast_t *derivative = de_Derivative(de->y, k);

		symbols[k] = ast_MakeSymbol(fresh_symbol(scope));
		substitute(scope, derivative, symbols[k]);
		substitute(f, derivative, symbols[k]);

		ast_Cleanup(derivative);
	}

	symbols[0] = ast_Copy(de->y);
	ast_Cleanup(scope);
}

/*Fills in the coefficients if f, the left side minus the right side, is linear in the derivatives*/
static bool linear_form(pcas_de_t *de, pcas_ast_t *f) {
	pcas_ast_t *symbols[DE_MAX_ORDER + 1];
	unsigned j, k;
	bool linear = true;

	derivatives_to_symbols(de, f, symbols);
	simplify(f, SIMP_BASIC);

	for (k = 0; k <= de->order; k++) {
		de->a[k] = ast_Copy(f);
		derivative(de->a[k], symbols[k], symbols[k]);
		simplify(de->a[k], SIMP_BASIC);

		for (j = 0; j <= de->order; j++)
			linear &= is_constant(de->a[k], symbols[j]);
	}

	if (linear) {
		de->g = negate(ast_Copy(f));

		for (k = 0; k <= de->order; k++) {
			pcas_ast_t *zero = ast_MakeNumber(num_FromInt(0));
			substitute(de->g, symbols[k], zero);
			ast_Cleanup(zero);
		}

		simplify(de->g, SIMP_BASIC);
	} else {
		for (k = 0; k <= de->order; k++) {
			ast_Cleanup(de->a[k]);
			de->a[k] = NULL;
		}
	}

	for (k = 0; k <= de->order; k++)
		ast_Cleanup(symbols[k]);

	return linear;
}

pcas_error_t de_Load(pcas_de_t *de, const pcas_ast_t *equation, const pcas_ast_t *x) {
	pcas_ast_t *f, *y = NULL;
	pcas_error_t err;
	unsigned k;

	de->x = ast_Copy(x);
	de->y = NULL;
	de->order = 0;
	de->linear = false;
	de->g = NULL;
	de->condition_count = 0;
	de->method = NULL;
	de->nested = false;
	for (k = 0; k <= DE_MAX_ORDER; k++)
		de->a[k] = NULL;

	if (isoptype(equation, OP_EQUALS))
		de->equation = ast_Copy(equation);
	else
		de->equation = ast_MakeBinary(OP_EQUALS, ast_Copy(equation), ast_MakeNumber(num_FromInt(0)));

	if ((err = find_function(de->equation, &y, &de->order)) != E_SUCCESS)
		return err;
	if (y == NULL)
		return E_DE_NO_DERIVATIVE;
	if (ast_Compare(y, x))
		return E_DE_BAD_FUNCTION;
	if (de->order > DE_MAX_ORDER)
		return E_DE_ORDER;

	de->y = ast_Copy(y);
	canonical_SetFunction(y->op.symbol);

	work_Step(STEP_EQUATION, NULL, ast_ChildGet(de->equation, 0), ast_ChildGet(de->equation, 1));

	f = difference(ast_Copy(ast_ChildGet(de->equation, 0)), ast_Copy(ast_ChildGet(de->equation, 1)));

	work_Pause();
	de->linear = linear_form(de, f);
	work_Resume();

	ast_Cleanup(f);

	return E_SUCCESS;
}

void de_Classify(pcas_de_t *de) {
	work_Text(order_names[de->order - 1]);

	if (de->linear) {
		pcas_ast_t *standard = de_StandardForm(de);
		work_Step(STEP_EQUATION, "Linear", standard, de->g);
		ast_Cleanup(standard);
	} else {
		work_Text("Nonlinear");
	}
}

void de_Cleanup(pcas_de_t *de) {
	unsigned k;

	ast_Cleanup(de->equation);
	ast_Cleanup(de->x);
	ast_Cleanup(de->y);
	ast_Cleanup(de->g);

	for (k = 0; k <= DE_MAX_ORDER; k++)
		ast_Cleanup(de->a[k]);

	for (k = 0; k < de->condition_count; k++) {
		ast_Cleanup(de->conditions[k].at);
		ast_Cleanup(de->conditions[k].value);
	}

	canonical_SetFunction(SYM_INVALID);
}

pcas_ast_t *de_StandardForm(pcas_de_t *de) {
	pcas_ast_t *sum = ast_MakeOperator(OP_ADD);
	unsigned k;

	for (k = de->order + 1; k-- > 0;) {
		if (!is_ast_int(de->a[k], 0))
			ast_ChildAppend(sum, ast_MakeBinary(OP_MULT, ast_Copy(de->a[k]), de_Derivative(de->y, k)));
	}

	work_Pause();
	simplify(sum, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_EVAL);
	work_Resume();

	return sum;
}

/*Returns the number of primes if e is a derivative of y, or -1*/
static int derivative_order(const pcas_ast_t *e, const pcas_ast_t *y) {
	int order = 0;

	while (isoptype(e, OP_PRIME)) {
		e = opbase(e);
		order++;
	}

	return ast_Compare(e, y) ? order : -1;
}

pcas_error_t de_LoadList(pcas_de_t *de, pcas_ast_t **items, unsigned count, const pcas_ast_t *x) {
	pcas_error_t err;
	unsigned i;

	err = de_Load(de, items[0], x);

	for (i = 1; i < count && err == E_SUCCESS; i++)
		err = de_AddCondition(de, items[i]);

	return err;
}

pcas_error_t de_AddCondition(pcas_de_t *de, const pcas_ast_t *condition) {
	pcas_ast_t *left;
	pcas_condition_t *c;
	int order;

	if (de->condition_count == DE_MAX_CONDITIONS || !isoptype(condition, OP_EQUALS))
		return E_DE_BAD_CONDITION;

	/*Y(0) is parsed as Y*0*/
	left = opbase(condition);
	if (!isoptype(left, OP_MULT) || ast_ChildLength(left) != 2)
		return E_DE_BAD_CONDITION;

	order = derivative_order(opbase(left), de->y);
	if (order < 0)
		return E_DE_BAD_CONDITION;

	c = &de->conditions[de->condition_count++];
	c->order = (unsigned)order;
	c->at = ast_Copy(opbase(left)->next);
	c->value = ast_Copy(left->next);

	work_Pause();
	simplify(c->at, SIMP_BASIC);
	simplify(c->value, SIMP_BASIC);
	work_Resume();

	return E_SUCCESS;
}

static bool is_euler(const pcas_ast_t *e) {
	return e->type == NODE_SYMBOL && e->op.symbol == SYM_EULER;
}

static pcas_ast_t *ln(pcas_ast_t *a) {
	return ast_MakeBinary(OP_LOG, ast_MakeSymbol(SYM_EULER), a);
}

/*Returns base^e, writing e^(A+B) as e^A*e^B, e^ln(A) as A and e^(n*ln(A)) as A^n. Takes ownership of e.*/
static pcas_ast_t *exponential(const pcas_ast_t *base, pcas_ast_t *e) {
	pcas_ast_t *result, *child;

	if (is_euler(base) && isoptype(e, OP_LOG) && is_euler(opbase(e))) {
		result = ast_Copy(opbase(e)->next);
	} else if (
		is_euler(base) && isoptype(e, OP_MULT) && ast_ChildLength(e) == 2 && opbase(e)->type == NODE_NUMBER &&
		isoptype(opbase(e)->next, OP_LOG) && is_euler(opbase(opbase(e)->next))
	) {
		result = ast_MakeBinary(OP_POW, ast_Copy(opbase(opbase(e)->next)->next), ast_Copy(opbase(e)));
	} else if (is_euler(base) && isoptype(e, OP_ADD)) {
		result = ast_MakeOperator(OP_MULT);
		for (child = opbase(e); child != NULL; child = child->next)
			ast_ChildAppend(result, exponential(base, ast_Copy(child)));
	} else {
		return ast_MakeBinary(OP_POW, ast_Copy(base), e);
	}

	ast_Cleanup(e);
	return result;
}

/*Rewrites every power of e with exponential*/
static void split_exponentials(pcas_ast_t *e) {
	pcas_ast_t *child;

	if (e->type != NODE_OPERATOR)
		return;

	for (child = opbase(e); child != NULL; child = child->next)
		split_exponentials(child);

	if (isoptype(e, OP_POW) && is_euler(opbase(e)))
		replace_node(e, exponential(opbase(e), ast_Copy(opbase(e)->next)));
}

/*Writes e as one fraction num/den, without simplifying*/
static void rational_parts(pcas_ast_t *e, pcas_ast_t **num, pcas_ast_t **den) {
	pcas_ast_t *child;

	if (isoptype(e, OP_DIV)) {
		pcas_ast_t *n1, *d1, *n2, *d2;

		rational_parts(opbase(e), &n1, &d1);
		rational_parts(opbase(e)->next, &n2, &d2);

		*num = ast_MakeBinary(OP_MULT, n1, d2);
		*den = ast_MakeBinary(OP_MULT, d1, n2);
	} else if (isoptype(e, OP_MULT)) {
		*num = ast_MakeOperator(OP_MULT);
		*den = ast_MakeOperator(OP_MULT);

		for (child = opbase(e); child != NULL; child = child->next) {
			pcas_ast_t *n, *d;
			rational_parts(child, &n, &d);
			ast_ChildAppend(*num, n);
			ast_ChildAppend(*den, d);
		}
	} else if (isoptype(e, OP_ADD)) {
		pcas_ast_t *nums = ast_MakeOperator(OP_ADD), *dens = ast_MakeOperator(OP_MULT);
		pcas_ast_t *n, *d, *other;

		for (child = opbase(e); child != NULL; child = child->next) {
			rational_parts(child, &n, &d);
			ast_ChildAppend(nums, n);
			ast_ChildAppend(dens, d);
		}

		*num = ast_MakeOperator(OP_ADD);

		for (n = opbase(nums), d = opbase(dens); n != NULL; n = n->next, d = d->next) {
			pcas_ast_t *term = ast_MakeOperator(OP_MULT);

			ast_ChildAppend(term, ast_Copy(n));
			for (other = opbase(dens); other != NULL; other = other->next) {
				if (other != d)
					ast_ChildAppend(term, ast_Copy(other));
			}

			ast_ChildAppend(*num, term);
		}

		ast_Cleanup(nums);
		*den = dens;
	} else if (isoptype(e, OP_POW) && opbase(e)->next->type == NODE_NUMBER) {
		pcas_ast_t *n, *d;
		mp_rat exponent = num_Copy(opbase(e)->next->op.num);
		bool negative = mp_rat_compare_zero(exponent) < 0;

		mp_rat_abs(exponent, exponent);
		rational_parts(opbase(e), &n, &d);

		*num = ast_MakeBinary(OP_POW, negative ? d : n, ast_MakeNumber(num_Copy(exponent)));
		*den = ast_MakeBinary(OP_POW, negative ? n : d, ast_MakeNumber(exponent));
	} else {
		*num = ast_Copy(e);
		*den = ast_MakeNumber(num_FromInt(1));
	}
}

/*True if the numerator of e over a common denominator expands to zero after simplifying e with flags*/
static bool numerator_vanishes(const pcas_ast_t *e, unsigned short flags) {
	pcas_ast_t *copy = ast_Copy(e), *numerator, *denominator;
	bool zero;

	simplify(copy, flags);
	split_exponentials(copy);
	rational_parts(copy, &numerator, &denominator);

	expand(numerator, EXP_ALL);
	simplify(numerator, SIMP_BASIC);
	expand(numerator, EXP_ALL);
	simplify(numerator, SIMP_BASIC);

	if (!is_ast_int(numerator, 0)) {
		simplify_canonical_form(numerator, CANONICAL_COMBINE_POWERS);
		simplify(numerator, SIMP_BASIC);
	}

	zero = is_ast_int(numerator, 0);

	ast_Cleanup(copy);
	ast_Cleanup(numerator);
	ast_Cleanup(denominator);

	return zero;
}

/*True if e simplifies to zero, trying identities only when needed*/
static bool is_zero(const pcas_ast_t *e) {
	bool zero;

	work_Pause();
	zero = numerator_vanishes(e, SIMP_BASIC) || numerator_vanishes(e, SIMP_ALL);
	work_Resume();

	return zero;
}

static bool involves(const pcas_ast_t *e, const pcas_ast_t *v) {
	pcas_ast_t *child;

	if (ast_Compare(e, v))
		return true;

	if (e->type == NODE_OPERATOR) {
		for (child = opbase(e); child != NULL; child = child->next) {
			if (involves(child, v))
				return true;
		}
	}

	return false;
}

/*Replaces each derivative of y in e with its value in derivatives*/
static void substitute_derivatives(pcas_de_t *de, pcas_ast_t *e, pcas_ast_t **derivatives) {
	unsigned k;

	for (k = de->order + 1; k-- > 0;) {
		pcas_ast_t *d = de_Derivative(de->y, k);
		substitute(e, d, derivatives[k]);
		ast_Cleanup(d);
	}
}

/*Substitutes the derivatives into one side of the equation and simplifies it, recording each form*/
static pcas_ast_t *evaluate_side(pcas_de_t *de, const pcas_ast_t *side, pcas_ast_t **derivatives, const char *label) {
	pcas_ast_t *e = ast_Copy(side);

	if (!involves(e, de->y))
		return e;

	work_Text(label);
	work_Step(STEP_STATE, NULL, NULL, e);

	work_Pause();
	substitute_derivatives(de, e, derivatives);
	work_Resume();

	work_Step(STEP_STATE, NULL, NULL, e);
	simplify(e, SIMP_BASIC);

	return e;
}

static bool check_condition(pcas_de_t *de, pcas_condition_t *c, pcas_ast_t **derivatives) {
	pcas_ast_t *at, *substituted, *value, *chain;
	bool holds;

	at = ast_MakeBinary(OP_AT, de_Derivative(de->y, c->order), ast_Copy(c->at));
	substituted = ast_Copy(derivatives[c->order]);

	work_Pause();
	substitute(substituted, de->x, c->at);
	value = ast_Copy(substituted);
	simplify(value, SIMP_BASIC);
	if (!ast_Compare(value, c->value))
		simplify(value, SIMP_ALL);
	work_Resume();

	holds = is_zero(chain = difference(ast_Copy(value), ast_Copy(c->value)));
	ast_Cleanup(chain);

	chain = ast_MakeBinary(OP_EQUALS, substituted, value);
	work_Step(STEP_EQUATION, holds ? "Initial condition holds" : "Initial condition fails", at, chain);

	ast_Cleanup(at);
	ast_Cleanup(chain);

	return holds;
}

/*Returns d/dv(e) as an unevaluated derivative node*/
static pcas_ast_t *derivative_node(pcas_ast_t *e, const pcas_ast_t *v) {
	pcas_ast_t *node = ast_MakeOperator(OP_DERIV);

	ast_ChildAppend(node, e);
	ast_ChildAppend(node, ast_Copy(v));
	ast_ChildAppend(node, ast_Copy(v));

	return node;
}

pcas_error_t de_Verify(pcas_de_t *de, const pcas_ast_t *solution, bool *satisfied) {
	pcas_ast_t *derivatives[DE_MAX_ORDER + 1];
	pcas_ast_t *left, *right, *remainder;
	const pcas_ast_t *f = solution;
	unsigned k;

	if (isoptype(f, OP_EQUALS) && ast_Compare(opbase(f), de->y))
		f = opbase(f)->next;

	if (involves(f, de->y))
		return E_DE_IMPLICIT;

	work_Step(STEP_EQUATION, "Solution", de->y, f);

	derivatives[0] = ast_Copy(f);

	work_Pause();
	simplify(derivatives[0], SIMP_NORMALIZE);
	work_Resume();

	for (k = 1; k <= de->order; k++) {
		pcas_ast_t *d = ast_Copy(derivatives[k - 1]);
		pcas_ast_t *prime = de_Derivative(de->y, k);
		pcas_ast_t *chain;

		work_Pause();
		derivative(d, de->x, de->x);
		simplify(d, SIMP_BASIC);
		work_Resume();

		chain = ast_MakeBinary(OP_EQUALS, derivative_node(ast_Copy(derivatives[k - 1]), de->x), ast_Copy(d));

		work_Step(STEP_EQUATION, k == 1 ? "Differentiate" : NULL, prime, chain);

		ast_Cleanup(prime);
		ast_Cleanup(chain);

		derivatives[k] = d;
	}

	left = evaluate_side(de, opbase(de->equation), derivatives, "Left side");
	right = evaluate_side(de, opbase(de->equation)->next, derivatives, "Right side");

	remainder = difference(ast_Copy(left), ast_Copy(right));
	*satisfied = is_zero(remainder);
	ast_Cleanup(remainder);

	work_Step(STEP_EQUATION, *satisfied ? "Satisfies the equation" : "Does not satisfy the equation", left, right);

	for (k = 0; k < de->condition_count; k++)
		*satisfied &= check_condition(de, &de->conditions[k], derivatives);

	work_Text(*satisfied ? "It is a solution" : "It is not a solution");

	for (k = 0; k <= de->order; k++)
		ast_Cleanup(derivatives[k]);
	ast_Cleanup(left);
	ast_Cleanup(right);

	return E_SUCCESS;
}

/*Returns the logarithm of e to base. Takes ownership of e.*/
static pcas_ast_t *logarithm(const pcas_ast_t *base, pcas_ast_t *e) {
	return is_euler(base) ? ln(e) : ast_MakeBinary(OP_DIV, ln(e), ln(ast_Copy(base)));
}

/*Writes the first order equation as M + Ny' = 0, without simplifying M. Returns false if y' does not appear linearly.*/
static bool differential_form(pcas_de_t *de, pcas_ast_t **M, pcas_ast_t **N) {
	pcas_ast_t *symbols[DE_MAX_ORDER + 1];
	pcas_ast_t *f, *zero;
	bool linear;

	f = difference(ast_Copy(opbase(de->equation)), ast_Copy(opbase(de->equation)->next));
	derivatives_to_symbols(de, f, symbols);
	simplify(f, SIMP_BASIC);

	*N = ast_Copy(f);
	derivative(*N, symbols[1], symbols[1]);
	simplify(*N, SIMP_BASIC);

	linear = is_constant(*N, symbols[1]) && !is_ast_int(*N, 0);

	if (linear) {
		zero = integer(0);
		substitute(f, symbols[1], zero);
		ast_Cleanup(zero);
		*M = f;
	} else {
		ast_Cleanup(f);
		ast_Cleanup(*N);
	}

	ast_Cleanup(symbols[0]);
	ast_Cleanup(symbols[1]);

	return linear;
}

/*Returns the right side of the first order equation solved for y', or NULL if y' does not appear linearly*/
static pcas_ast_t *solve_for_prime(pcas_de_t *de) {
	pcas_ast_t *M, *N, *F;

	if (!differential_form(de, &M, &N))
		return NULL;

	F = ast_MakeBinary(OP_DIV, negate(M), N);
	simplify(F, SIMP_BASIC);

	return F;
}

/*Appends the factors of e, raised to exponent, to g when they do not involve y and to h when they do not involve x. Returns false if e is not such a product.*/
static bool separate(pcas_de_t *de, const pcas_ast_t *e, const pcas_ast_t *exponent, pcas_ast_t *g, pcas_ast_t *h) {
	pcas_ast_t *child, *base, *power, *next;
	bool in_x = involves(e, de->x), in_y = involves(e, de->y), separable = true;

	if (!in_y || (!in_x && !isoptype(e, OP_MULT) && !isoptype(e, OP_DIV))) {
		ast_ChildAppend(in_y ? h : g, ast_MakeBinary(OP_POW, ast_Copy(e), ast_Copy(exponent)));
		return true;
	}

	if (isoptype(e, OP_MULT)) {
		for (child = opbase(e); child != NULL && separable; child = child->next)
			separable = separate(de, child, exponent, g, h);
		return separable;
	}

	if (isoptype(e, OP_DIV)) {
		next = negate(ast_Copy(exponent));
		separable = separate(de, opbase(e), exponent, g, h) && separate(de, opbase(e)->next, next, g, h);
		ast_Cleanup(next);
		return separable;
	}

	if (isoptype(e, OP_POW)) {
		base = opbase(e);
		power = base->next;

		if (!involves(power, de->x) && !involves(power, de->y)) {
			next = ast_MakeBinary(OP_MULT, ast_Copy(exponent), ast_Copy(power));
			separable = separate(de, base, next, g, h);
			ast_Cleanup(next);
			return separable;
		}

		if (involves(base, de->x) || involves(base, de->y) || !isoptype(power, OP_ADD))
			return false;

		for (child = opbase(power); child != NULL && separable; child = child->next) {
			next = ast_MakeBinary(OP_POW, ast_Copy(base), ast_Copy(child));
			separable = separate(de, next, exponent, g, h);
			ast_Cleanup(next);
		}

		return separable;
	}

	if (isoptype(e, OP_ADD)) {
		next = ast_Copy(e);
		factor(next, FAC_ALL);
		separable = !isoptype(next, OP_ADD) && separate(de, next, exponent, g, h);
		ast_Cleanup(next);
		return separable;
	}

	return false;
}

/*Returns 1 or -1 when the sign of e is known, otherwise 0*/
static int sign_of(const pcas_ast_t *e) {
	pcas_ast_t *child;
	int sign = 1, compared;

	if (e->type == NODE_NUMBER) {
		compared = mp_rat_compare_zero(e->op.num);
		return (compared > 0) - (compared < 0);
	}

	if (e->type == NODE_SYMBOL)
		return e->op.symbol == SYM_PI || e->op.symbol == SYM_EULER;

	if (isoptype(e, OP_POW))
		return sign_of(opbase(e)) > 0;

	if (isoptype(e, OP_MULT) || isoptype(e, OP_DIV)) {
		for (child = opbase(e); child != NULL; child = child->next)
			sign *= sign_of(child);
		return sign;
	}

	return 0;
}

/*Returns the sign of e at the initial condition, or 0 if it is unknown*/
static int sign_at(pcas_de_t *de, const pcas_ast_t *e, pcas_condition_t *c) {
	pcas_ast_t *value = ast_Copy(e);
	int sign;

	work_Pause();
	substitute(value, de->y, c->value);
	substitute(value, de->x, c->at);
	simplify(value, SIMP_ALL);
	work_Resume();

	sign = sign_of(value);
	ast_Cleanup(value);

	return sign;
}

/*Returns the index of the only child of e that involves v, or -1*/
static int only_child_with(const pcas_ast_t *e, const pcas_ast_t *v) {
	pcas_ast_t *child;
	int i, found = -1;

	for (child = opbase(e), i = 0; child != NULL; child = child->next, i++) {
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
	pcas_ast_t *child, *kept;
	int i;

	if (e->type != NODE_OPERATOR || !involves(e, constant))
		return;

	if (!involves(e, de->x) && !involves(e, de->y)) {
		replace_node(e, ast_Copy(constant));
		return;
	}

	if (isoptype(e, OP_POW) && !involves(opbase(e), de->x) && !involves(opbase(e), de->y)) {
		kept = ast_Copy(opbase(e)->next);
		absorb(de, kept, constant);

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

	if ((isoptype(e, OP_ADD) || isoptype(e, OP_MULT)) && (i = only_child_with(e, constant)) >= 0) {
		child = ast_ChildGet(e, i);

		if (!involves(child, de->x) && !involves(child, de->y)) {
			kept = ast_MakeOperator(optype(e));

			for (child = opbase(e); child != NULL; child = child->next) {
				if (involves(child, de->x) || involves(child, de->y))
					ast_ChildAppend(kept, ast_Copy(child));
			}

			ast_ChildAppend(kept, ast_Copy(constant));
			replace_node(e, kept);
			return;
		}
	}

	for (child = opbase(e); child != NULL; child = child->next)
		absorb(de, child, constant);
}

/*True if e is the arbitrary constant or a multiple of it*/
static bool is_arbitrary_multiple(const pcas_ast_t *e, const pcas_ast_t *constant) {
	pcas_ast_t *child;

	if (constant == NULL)
		return false;

	if (ast_Compare(e, constant))
		return true;

	if (isoptype(e, OP_DIV))
		return is_arbitrary_multiple(opbase(e), constant);

	if (isoptype(e, OP_MULT)) {
		for (child = opbase(e); child != NULL; child = child->next) {
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
	unsigned i;

	for (i = 0; i < sizeof(inverse_pairs) / sizeof(inverse_pairs[0]); i++) {
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
	pcas_ast_t *product = ast_MakeOperator(OP_MULT), *child;

	ast_ChildAppend(product, ast_Copy(opbase(quotient)));
	for (child = opbase(opbase(quotient)->next); child != NULL; child = child->next)
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
	pcas_ast_t *L = *lhs, *R = *rhs, *left, *right, *rest, *base, *power;
	OperatorType inverse;
	int i, sign = 1;

	if (L->type != NODE_OPERATOR)
		return false;

	if (isoptype(L, OP_DIV) && !involves(opbase(L), de->y) && isoptype(opbase(L)->next, OP_MULT)) {
		rest = as_product(L);
		ast_Cleanup(L);
		*lhs = L = rest;
	}

	if (isoptype(L, OP_ADD) || isoptype(L, OP_MULT)) {
		if ((i = only_child_with(L, de->y)) < 0)
			return false;

		rest = ast_Copy(L);
		left = ast_ChildRemoveIndex(rest, i);
		right = isoptype(L, OP_ADD) ? difference(ast_Copy(R), rest) : ast_MakeBinary(OP_DIV, ast_Copy(R), rest);
	} else if (isoptype(L, OP_DIV)) {
		base = opbase(L);

		if (!involves(base, de->y) && isoptype(base->next, OP_POW) && !involves(opbase(base->next), de->y)) {
			power = negate(ast_Copy(opbase(base->next)->next));

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
		base = opbase(L);
		power = base->next;

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
		base = opbase(L);

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
	pcas_ast_t *left, *right;
	unsigned steps = 0;
	bool solved = true;

	while (solved && !ast_Compare(*lhs, de->y)) {
		left = ast_Copy(*lhs);
		right = ast_Copy(*rhs);

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

/*Returns e with the point of the initial condition substituted*/
static pcas_ast_t *at_condition(pcas_de_t *de, const pcas_ast_t *e, pcas_condition_t *c) {
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

/*True if h is zero at the initial value, which makes y constant*/
static bool is_equilibrium(pcas_de_t *de, const pcas_ast_t *h, pcas_condition_t *c) {
	pcas_ast_t *value = ast_Copy(h), *zero;
	bool equilibrium;

	work_Pause();
	substitute(value, de->y, c->value);
	zero = ast_Copy(value);
	simplify(zero, SIMP_ALL);
	work_Resume();

	equilibrium = is_ast_int(zero, 0);

	if (equilibrium) {
		work_Step(STEP_EQUATION, "Zero at the initial value", value, zero);
		work_Step(STEP_EQUATION, "Constant solution", de->y, c->value);
	}

	ast_Cleanup(value);
	ast_Cleanup(zero);

	return equilibrium;
}

static bool is_monomial(const pcas_ast_t *e) {
	return e->type != NODE_OPERATOR || isoptype(e, OP_POW);
}

/*True if e is one sum multiplied or divided by numbers, symbols and powers*/
static bool is_sum_times_monomials(const pcas_ast_t *e) {
	const pcas_ast_t *child, *numerator = e;
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

	for (child = opbase(numerator); child != NULL; child = child->next) {
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
	pcas_ast_t *child, *merged, *inside;
	unsigned count = 0;

	if (e->type != NODE_OPERATOR)
		return;

	for (child = opbase(e); child != NULL; child = child->next) {
		merge_absolute_values(child);
		count += isoptype(child, OP_ABS);
	}

	if (!isoptype(e, OP_MULT) || count < 2)
		return;

	merged = ast_MakeOperator(OP_MULT);
	inside = ast_MakeOperator(OP_MULT);

	for (child = opbase(e); child != NULL; child = child->next) {
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
	pcas_ast_t *child, *numerator, *denominator;
	mp_rat multiple = num_FromInt(1);

	for (child = opbase(e); child != NULL; child = child->next) {
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
	pcas_ast_t *L, *R, *terms, *child, *scale, *combined;
	bool negative = true;

	if (!isoptype(*lhs, OP_ADD) || !involves(*lhs, de->x))
		return;

	work_Pause();
	L = ast_Copy(*lhs);
	R = ast_MakeOperator(OP_ADD);
	terms = ast_MakeBinary(OP_ADD, ast_Copy(*rhs), integer(0));
	simplify(terms, SIMP_COMMUTATIVE);

	for (child = opbase(terms); child != NULL; child = child->next) {
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
		scale = common_denominator(L);
		scale_sides(&L, &R, scale);
		ast_Cleanup(scale);
	}

	if (isoptype(L, OP_ADD)) {
		for (child = opbase(L); child != NULL; child = child->next)
			negative &= is_negative_for_sure(child);

		if (negative) {
			scale = integer(-1);
			scale_sides(&L, &R, scale);
			ast_Cleanup(scale);
		}
	}

	if (constant != NULL)
		absorb(de, R, constant);

	combined = ast_Copy(L);
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

/*Solves lhs = rhs for y and records the solution. Takes ownership of lhs and rhs.*/
static void conclude(
	pcas_de_t *de,
	pcas_ast_t *lhs,
	pcas_ast_t *rhs,
	const pcas_ast_t *constant,
	pcas_condition_t *c,
	pcas_ast_t **solution
) {
	pcas_ast_t *left = ast_Copy(lhs), *right = ast_Copy(rhs);
	bool explicit = isolate(de, &lhs, &rhs, c == NULL ? constant : NULL, c);

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

/*Records lhs = antiderivative + C, finds C from the initial condition and solves for y. Takes ownership of lhs and antiderivative.*/
static void finish(pcas_de_t *de, pcas_ast_t *lhs, pcas_ast_t *antiderivative, pcas_ast_t **solution) {
	pcas_condition_t *c = de->condition_count > 0 ? &de->conditions[0] : NULL;
	pcas_ast_t *constant = ast_MakeSymbol(constant_symbol(de->equation)), *rhs;

	work_Pause();
	simplify(lhs, SIMP_BASIC);
	simplify(antiderivative, SIMP_BASIC);
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

static pcas_error_t solve_separable(pcas_de_t *de, pcas_ast_t **solution) {
	pcas_condition_t *c = de->condition_count > 0 ? &de->conditions[0] : NULL;
	pcas_ast_t *prime, *equation, *lhs, *antiderivative;

	work_Pause();
	pcas_ast_t *F = solve_for_prime(de), *g = ast_MakeOperator(OP_MULT), *h = ast_MakeOperator(OP_MULT),
			   *one = integer(1);
	bool separable = F != NULL && separate(de, F, one, g, h);
	simplify(g, SIMP_BASIC);
	simplify(h, SIMP_BASIC);
	work_Resume();

	ast_Cleanup(F);
	ast_Cleanup(one);

	if (!separable) {
		ast_Cleanup(g);
		ast_Cleanup(h);
		return E_DE_UNSOLVED;
	}

	de->method = "Separable";

	prime = de_Derivative(de->y, 1);
	F = ast_MakeBinary(OP_MULT, ast_Copy(g), ast_Copy(h));
	work_Step(STEP_EQUATION, "Separable", prime, F);
	ast_Cleanup(prime);
	ast_Cleanup(F);

	if (c != NULL && is_equilibrium(de, h, c)) {
		*solution = ast_MakeBinary(OP_EQUALS, ast_Copy(de->y), ast_Copy(c->value));
		ast_Cleanup(g);
		ast_Cleanup(h);
		return E_SUCCESS;
	}

	work_Pause();
	lhs = ast_MakeBinary(OP_DIV, integer(1), h);
	simplify(lhs, SIMP_BASIC);
	work_Resume();

	lhs = ast_MakeBinary(OP_INTEGRAL, lhs, ast_Copy(de->y));
	antiderivative = ast_MakeBinary(OP_INTEGRAL, g, ast_Copy(de->x));
	work_Step(STEP_EQUATION, "Separate variables", lhs, antiderivative);

	equation = ast_MakeBinary(OP_EQUALS, lhs, antiderivative);
	eval_integrals(equation);

	if (contains_integral(equation)) {
		ast_Cleanup(equation);
		return E_DE_INTEGRAL;
	}

	finish(de, ast_Copy(opbase(equation)), ast_Copy(opbase(equation)->next), solution);
	ast_Cleanup(equation);

	return E_SUCCESS;
}

/*Replaces each |u| in e with u*/
static void drop_absolute_values(pcas_ast_t *e) {
	pcas_ast_t *child;

	if (e->type != NODE_OPERATOR)
		return;

	for (child = opbase(e); child != NULL; child = child->next)
		drop_absolute_values(child);

	if (isoptype(e, OP_ABS))
		replace_node(e, ast_Copy(opbase(e)));
}

/*Returns e^(integral of P dv) without absolute values, recording the integral, or NULL if it cannot be found. Takes ownership of P.*/
static pcas_ast_t *exponential_of_integral(pcas_ast_t *P, const pcas_ast_t *v) {
	pcas_ast_t *G = ast_MakeBinary(OP_INTEGRAL, P, ast_Copy(v)), *power, *mu;

	eval_integrals(G);

	if (contains_integral(G)) {
		ast_Cleanup(G);
		return NULL;
	}

	work_Pause();
	simplify(G, SIMP_BASIC);
	power = ast_MakeBinary(OP_POW, ast_MakeSymbol(SYM_EULER), ast_Copy(G));
	drop_absolute_values(G);
	mu = exponential(opbase(power), G);
	simplify(mu, SIMP_BASIC);
	work_Resume();

	work_Step(STEP_EQUATION, NULL, power, mu);
	ast_Cleanup(power);

	return mu;
}

/*Solves y' + Py = Q by multiplying by the integrating factor e^(integral of P)*/
static pcas_error_t solve_linear_first(pcas_de_t *de, pcas_ast_t **solution) {
	pcas_ast_t *P, *Q, *left, *mu, *product, *right;

	work_Pause();
	P = ast_MakeBinary(OP_DIV, ast_Copy(de->a[0]), ast_Copy(de->a[1]));
	simplify(P, SIMP_BASIC);
	factor_cancel(P);
	Q = ast_MakeBinary(OP_DIV, ast_Copy(de->g), ast_Copy(de->a[1]));
	simplify(Q, SIMP_BASIC);
	factor_cancel(Q);
	work_Resume();

	de->method = "Linear";

	if (!is_ast_int(de->a[1], 1)) {
		left = ast_MakeBinary(OP_ADD, de_Derivative(de->y, 1), ast_MakeBinary(OP_MULT, ast_Copy(P), ast_Copy(de->y)));
		work_Step(STEP_EQUATION, "Standard form", left, Q);
		ast_Cleanup(left);
	}

	work_Text("Integrating factor");

	if ((mu = exponential_of_integral(P, de->x)) == NULL) {
		ast_Cleanup(Q);
		return E_DE_INTEGRAL;
	}

	work_Pause();
	product = ast_MakeBinary(OP_MULT, ast_Copy(mu), ast_Copy(de->y));
	simplify(product, SIMP_BASIC);
	right = ast_MakeBinary(OP_MULT, mu, Q);
	simplify(right, SIMP_BASIC);
	factor_cancel(right);
	work_Resume();

	left = derivative_node(ast_Copy(product), de->x);
	work_Step(STEP_EQUATION, "Multiply by the integrating factor", left, right);
	ast_Cleanup(left);

	right = ast_MakeBinary(OP_INTEGRAL, right, ast_Copy(de->x));
	eval_integrals(right);

	if (contains_integral(right)) {
		ast_Cleanup(product);
		ast_Cleanup(right);
		return E_DE_INTEGRAL;
	}

	finish(de, product, right, solution);

	return E_SUCCESS;
}

static pcas_ast_t *tidy(pcas_ast_t *e) {
	work_Pause();
	simplify(e, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_EVAL);
	work_Resume();

	return e;
}

/*Writes e as one fraction with an expanded numerator and cancels common factors*/
static void combine(pcas_ast_t *e) {
	pcas_ast_t *numerator, *denominator, *cancelled;

	work_Pause();
	simplify(e, SIMP_BASIC);
	rational_parts(e, &numerator, &denominator);
	expand(numerator, EXP_ALL);
	replace_node(e, ast_MakeBinary(OP_DIV, numerator, denominator));
	simplify(e, SIMP_BASIC);

	cancelled = ast_Copy(e);
	factor_cancel(cancelled);
	work_Resume();

	if (isoptype(e, OP_DIV) && isoptype(cancelled, OP_DIV) && ast_Compare(opbase(e)->next, opbase(cancelled)->next))
		ast_Cleanup(cancelled);
	else
		replace_node(e, cancelled);
}

/*Returns preferred, or a symbol that does not appear in the equation if preferred does*/
static pcas_ast_t *substitution_symbol(const pcas_de_t *de, Symbol preferred) {
	pcas_ast_t *scope = ast_MakeBinary(OP_ADD, ast_Copy(de->equation), ast_Copy(de->x));
	Symbol symbol = contains_symbol(scope, preferred) ? fresh_symbol(scope) : preferred;

	ast_Cleanup(scope);

	return ast_MakeSymbol(symbol);
}

typedef pcas_error_t (*solver_t)(pcas_de_t *de, pcas_ast_t **solution);

/*Solves equation, a first order equation in u = back, with solver, then substitutes back and solves for y. Takes ownership of back and equation.*/
static pcas_error_t solve_substituted(
	pcas_de_t *de,
	pcas_ast_t *back,
	pcas_ast_t *equation,
	solver_t solver,
	pcas_ast_t **solution
) {
	pcas_condition_t *c = de->condition_count > 0 ? &de->conditions[0] : NULL, *s;
	pcas_ast_t *inner = NULL, *u = NULL, *constant = NULL, *lhs, *rhs;
	pcas_error_t err;
	pcas_de_t sub;

	work_Pause();
	err = de_Load(&sub, equation, de->x);
	work_Resume();
	ast_Cleanup(equation);

	if (err == E_SUCCESS) {
		if (c != NULL) {
			s = &sub.conditions[sub.condition_count++];
			s->order = 0;
			s->at = ast_Copy(c->at);
			s->value = at_condition(de, back, c);

			work_Pause();
			simplify(s->value, SIMP_ALL);
			work_Resume();
		}

		sub.nested = true;
		u = ast_Copy(sub.y);
		constant = ast_MakeSymbol(constant_symbol(sub.equation));
		err = solver(&sub, &inner);
	}

	de_Cleanup(&sub);
	canonical_SetFunction(de->y->op.symbol);

	if (err == E_SUCCESS) {
		lhs = ast_Copy(opbase(inner));
		rhs = ast_Copy(opbase(inner)->next);

		work_Pause();
		substitute(lhs, u, back);
		substitute(rhs, u, back);
		simplify(lhs, SIMP_BASIC);
		simplify(rhs, SIMP_BASIC);
		work_Resume();

		work_Step(STEP_EQUATION, "Substitute back", lhs, rhs);
		conclude(de, lhs, rhs, constant, c, solution);
	}

	ast_Cleanup(inner);
	ast_Cleanup(u);
	ast_Cleanup(constant);
	ast_Cleanup(back);

	return err;
}

/*Returns the power of y that e is, or NULL if e is not a constant power of y*/
static pcas_ast_t *power_of_function(const pcas_de_t *de, const pcas_ast_t *e) {
	if (ast_Compare(e, de->y))
		return integer(1);

	if (isoptype(e, OP_POW) && ast_Compare(opbase(e), de->y) && opbase(e)->next->type == NODE_NUMBER)
		return ast_Copy(opbase(e)->next);

	return NULL;
}

/*Writes y' = F as y' + Py = Qy^n with n not 0 or 1. Returns false if F has another form.*/
static bool bernoulli_form(pcas_de_t *de, const pcas_ast_t *F, pcas_ast_t **P, pcas_ast_t **Q, pcas_ast_t **n) {
	pcas_ast_t *terms = ast_Copy(F), *term, *g, *h, *power, *one = integer(1);
	bool bernoulli;

	expand(terms, EXP_ALL);
	simplify(terms, SIMP_BASIC);

	*P = ast_MakeOperator(OP_ADD);
	*Q = ast_MakeOperator(OP_ADD);
	*n = NULL;
	bernoulli = isoptype(terms, OP_ADD);

	for (term = opbase(terms); bernoulli && term != NULL; term = term->next) {
		g = ast_MakeOperator(OP_MULT);
		h = ast_MakeOperator(OP_MULT);

		bernoulli = separate(de, term, one, g, h);
		simplify(h, SIMP_BASIC);
		power = bernoulli ? power_of_function(de, h) : NULL;

		if (power == NULL || is_ast_int(power, 0) || (*n != NULL && !is_ast_int(power, 1) && !ast_Compare(*n, power))) {
			bernoulli = false;
			ast_Cleanup(g);
		} else if (is_ast_int(power, 1)) {
			ast_ChildAppend(*P, negate(g));
		} else {
			ast_ChildAppend(*Q, g);
			if (*n == NULL) {
				*n = power;
				power = NULL;
			}
		}

		ast_Cleanup(power);
		ast_Cleanup(h);
	}

	bernoulli &= *n != NULL && ast_ChildLength(*P) > 0;

	ast_Cleanup(terms);
	ast_Cleanup(one);

	if (!bernoulli) {
		ast_Cleanup(*P);
		ast_Cleanup(*Q);
		ast_Cleanup(*n);
		return false;
	}

	simplify(*P, SIMP_BASIC);
	simplify(*Q, SIMP_BASIC);

	return true;
}

/*Solves y' + Py = Qy^n with the substitution v = y^(1-n), which makes it linear*/
static pcas_error_t solve_bernoulli(pcas_de_t *de, pcas_ast_t **solution) {
	pcas_ast_t *F, *P, *Q, *n, *m, *v, *back, *left, *right;
	bool bernoulli;

	work_Pause();
	F = solve_for_prime(de);
	bernoulli = F != NULL && bernoulli_form(de, F, &P, &Q, &n);
	work_Resume();

	ast_Cleanup(F);

	if (!bernoulli)
		return E_DE_UNSOLVED;

	de->method = "Bernoulli";

	left = ast_MakeBinary(OP_ADD, de_Derivative(de->y, 1), ast_MakeBinary(OP_MULT, ast_Copy(P), ast_Copy(de->y)));
	right = ast_MakeBinary(OP_MULT, ast_Copy(Q), ast_MakeBinary(OP_POW, ast_Copy(de->y), ast_Copy(n)));
	work_Step(STEP_EQUATION, "Bernoulli", tidy(left), tidy(right));
	ast_Cleanup(left);
	ast_Cleanup(right);

	work_Pause();
	m = difference(integer(1), ast_Copy(n));
	simplify(m, SIMP_BASIC);
	work_Resume();

	v = substitution_symbol(de, SYM_V);
	back = ast_MakeBinary(OP_POW, ast_Copy(de->y), ast_Copy(m));
	work_Step(STEP_EQUATION, "Substitute", v, back);

	left = de_Derivative(v, 1);
	right = ast_MakeBinary(
		OP_MULT,
		ast_Copy(m),
		ast_MakeBinary(OP_MULT, ast_MakeBinary(OP_POW, ast_Copy(de->y), negate(n)), de_Derivative(de->y, 1))
	);
	work_Step(STEP_EQUATION, NULL, left, tidy(right));
	ast_Cleanup(right);

	work_Pause();
	P = ast_MakeBinary(OP_MULT, ast_Copy(m), P);
	simplify(P, SIMP_BASIC);
	factor_cancel(P);
	Q = ast_MakeBinary(OP_MULT, m, Q);
	simplify(Q, SIMP_BASIC);
	factor_cancel(Q);
	work_Resume();

	left = ast_MakeBinary(OP_ADD, left, ast_MakeBinary(OP_MULT, P, ast_Copy(v)));
	work_Step(STEP_EQUATION, "Linear", tidy(left), Q);
	ast_Cleanup(v);

	return solve_substituted(de, back, ast_MakeBinary(OP_EQUALS, left, Q), solve_linear_first, solution);
}

/*Solves y' = F(x, y), where F is unchanged by scaling x and y, with the substitution y = ux*/
static pcas_error_t solve_homogeneous(pcas_de_t *de, pcas_ast_t **solution) {
	pcas_ast_t *F, *G, *u, *ux, *d, *one, *left, *right;
	bool homogeneous;

	work_Pause();
	F = solve_for_prime(de);
	work_Resume();

	if (F == NULL || !involves(F, de->x) || !involves(F, de->y)) {
		ast_Cleanup(F);
		return E_DE_UNSOLVED;
	}

	u = substitution_symbol(de, SYM_U);
	ux = ast_MakeBinary(OP_MULT, ast_Copy(u), ast_Copy(de->x));

	work_Pause();
	G = ast_Copy(F);
	substitute(G, de->y, ux);
	simplify(G, SIMP_BASIC);
	d = ast_Copy(G);
	derivative(d, de->x, de->x);
	work_Resume();

	homogeneous = is_zero(d);
	ast_Cleanup(d);

	if (!homogeneous) {
		ast_Cleanup(F);
		ast_Cleanup(G);
		ast_Cleanup(u);
		ast_Cleanup(ux);
		return E_DE_UNSOLVED;
	}

	de->method = "Homogeneous";

	work_Pause();
	one = integer(1);
	substitute(G, de->x, one);
	ast_Cleanup(one);
	work_Resume();

	combine(G);

	left = de_Derivative(de->y, 1);
	work_Step(STEP_EQUATION, "Homogeneous", left, F);
	work_Step(STEP_EQUATION, "Substitute", de->y, ux);

	right = ast_MakeBinary(OP_ADD, ast_Copy(u), ast_MakeBinary(OP_MULT, ast_Copy(de->x), de_Derivative(u, 1)));
	work_Step(STEP_EQUATION, NULL, left, right);
	work_Step(STEP_EQUATION, NULL, right, G);
	ast_Cleanup(left);
	ast_Cleanup(right);
	ast_Cleanup(F);
	ast_Cleanup(ux);

	right = ast_MakeBinary(OP_DIV, difference(G, ast_Copy(u)), ast_Copy(de->x));
	combine(right);
	left = de_Derivative(u, 1);
	ast_Cleanup(u);

	return solve_substituted(
		de,
		ast_MakeBinary(OP_DIV, ast_Copy(de->y), ast_Copy(de->x)),
		ast_MakeBinary(OP_EQUALS, left, right),
		solve_separable,
		solution
	);
}

/*Returns d/dv(e), or NULL if it depends on x or y or is zero*/
static pcas_ast_t *constant_slope(const pcas_de_t *de, const pcas_ast_t *e, const pcas_ast_t *v) {
	pcas_ast_t *slope = ast_Copy(e);

	derivative(slope, v, v);
	simplify(slope, SIMP_BASIC);

	if (involves(slope, de->x) || involves(slope, de->y) || is_ast_int(slope, 0)) {
		ast_Cleanup(slope);
		return NULL;
	}

	return slope;
}

/*Finds a sum ax + by in e, with constants a and b, whose substitution as u leaves only u in F, which becomes G*/
static pcas_ast_t *linear_argument(
	const pcas_de_t *de,
	const pcas_ast_t *e,
	const pcas_ast_t *F,
	const pcas_ast_t *u,
	pcas_ast_t **a,
	pcas_ast_t **b,
	pcas_ast_t **G
) {
	pcas_ast_t *child, *sum, *y, *found = NULL;

	if (e->type != NODE_OPERATOR)
		return NULL;

	if (isoptype(e, OP_ADD)) {
		sum = ast_MakeOperator(OP_ADD);

		for (child = opbase(e); child != NULL; child = child->next) {
			if (involves(child, de->x) || involves(child, de->y))
				ast_ChildAppend(sum, ast_Copy(child));
		}

		*a = constant_slope(de, sum, de->x);
		*b = constant_slope(de, sum, de->y);

		if (*a != NULL && *b != NULL) {
			y = ast_MakeBinary(
				OP_DIV, difference(ast_Copy(u), ast_MakeBinary(OP_MULT, ast_Copy(*a), ast_Copy(de->x))), ast_Copy(*b)
			);
			*G = ast_Copy(F);
			substitute(*G, de->y, y);
			simplify(*G, SIMP_BASIC);
			ast_Cleanup(y);

			if (!involves(*G, de->x)) {
				simplify(sum, SIMP_BASIC);
				return sum;
			}

			ast_Cleanup(*G);
		}

		ast_Cleanup(*a);
		ast_Cleanup(*b);
		ast_Cleanup(sum);
	}

	for (child = opbase(e); child != NULL && found == NULL; child = child->next)
		found = linear_argument(de, child, F, u, a, b, G);

	return found;
}

/*Solves y' = F(ax + by) with the substitution u = ax + by, which makes it separable*/
static pcas_error_t solve_linear_argument(pcas_de_t *de, pcas_ast_t **solution) {
	pcas_ast_t *F, *G, *a, *b, *u, *sum, *left, *right;

	work_Pause();
	F = solve_for_prime(de);
	u = substitution_symbol(de, SYM_U);
	sum = F != NULL ? linear_argument(de, F, F, u, &a, &b, &G) : NULL;
	work_Resume();

	ast_Cleanup(F);

	if (sum == NULL) {
		ast_Cleanup(u);
		return E_DE_UNSOLVED;
	}

	de->method = "Substitution";

	work_Step(STEP_EQUATION, "Substitute", u, sum);

	left = de_Derivative(u, 1);
	right = ast_MakeBinary(OP_ADD, ast_Copy(a), ast_MakeBinary(OP_MULT, ast_Copy(b), de_Derivative(de->y, 1)));
	work_Step(STEP_EQUATION, NULL, left, tidy(right));
	ast_Cleanup(right);

	right = ast_MakeBinary(OP_ADD, a, ast_MakeBinary(OP_MULT, b, G));
	work_Step(STEP_EQUATION, NULL, left, tidy(right));
	combine(right);
	ast_Cleanup(u);

	return solve_substituted(de, sum, ast_MakeBinary(OP_EQUALS, left, right), solve_separable, solution);
}

static pcas_ast_t *partial(const pcas_ast_t *e, const pcas_ast_t *v) {
	pcas_ast_t *d = ast_Copy(e);

	work_Pause();
	derivative(d, v, v);
	simplify(d, SIMP_BASIC);
	work_Resume();

	return d;
}

/*Records d/dv(e) = value*/
static void record_partial(const pcas_ast_t *e, const pcas_ast_t *v, const pcas_ast_t *value, const char *text) {
	pcas_ast_t *node = derivative_node(ast_Copy(e), v);

	work_Step(STEP_EQUATION, text, node, value);
	ast_Cleanup(node);
}

/*True if M + Ny' = 0 is exact, recording the check if record*/
static bool is_exact(pcas_de_t *de, const pcas_ast_t *M, const pcas_ast_t *N, bool record) {
	pcas_ast_t *My = partial(M, de->y), *Nx = partial(N, de->x), *remainder;
	bool exact = is_zero(remainder = difference(ast_Copy(My), ast_Copy(Nx)));

	if (record) {
		record_partial(M, de->y, My, NULL);
		record_partial(N, de->x, Nx, NULL);
		work_Text(exact ? "Exact" : "Not exact");
	}

	ast_Cleanup(remainder);
	ast_Cleanup(My);
	ast_Cleanup(Nx);

	return exact;
}

/*Returns (a_v - b_w)/c simplified if it involves only v, otherwise NULL*/
static pcas_ast_t *factor_rate(
	pcas_de_t *de,
	const pcas_ast_t *a,
	const pcas_ast_t *w,
	const pcas_ast_t *b,
	const pcas_ast_t *v,
	const pcas_ast_t *c
) {
	pcas_ast_t *rate = ast_MakeBinary(OP_DIV, difference(partial(a, w), partial(b, v)), ast_Copy(c));
	const pcas_ast_t *other = ast_Compare(v, de->x) ? de->y : de->x;

	combine(rate);

	if (involves(rate, other)) {
		ast_Cleanup(rate);
		return NULL;
	}

	return rate;
}

/*Solves M + Ny' = 0 when it is exact, or becomes exact after multiplying by an integrating factor of x or y alone*/
static pcas_error_t solve_exact(pcas_de_t *de, pcas_ast_t **solution) {
	pcas_ast_t *M, *N, *rate = NULL, *v = NULL, *mu, *P, *Q, *r, *G, *left, *zero;
	bool exact;

	work_Pause();
	if (!differential_form(de, &M, &N)) {
		work_Resume();
		return E_DE_UNSOLVED;
	}

	simplify(M, SIMP_BASIC);
	exact = is_exact(de, M, N, false);

	if (!exact) {
		if ((rate = factor_rate(de, M, de->y, N, de->x, N)) != NULL)
			v = de->x;
		else if ((rate = factor_rate(de, N, de->x, M, de->y, M)) != NULL)
			v = de->y;
	}
	work_Resume();

	if (!exact && rate == NULL) {
		ast_Cleanup(M);
		ast_Cleanup(N);
		return E_DE_UNSOLVED;
	}

	de->method = "Exact";

	left = ast_MakeSymbol(SYM_M);
	work_Step(STEP_EQUATION, NULL, left, M);
	ast_Cleanup(left);
	left = ast_MakeSymbol(SYM_N);
	work_Step(STEP_EQUATION, NULL, left, N);
	ast_Cleanup(left);

	is_exact(de, M, N, true);

	if (!exact) {
		work_Text("Integrating factor");

		left = v == de->x ? ast_MakeBinary(OP_DIV, difference(partial(M, de->y), partial(N, de->x)), ast_Copy(N))
						  : ast_MakeBinary(OP_DIV, difference(partial(N, de->x), partial(M, de->y)), ast_Copy(M));
		work_Step(STEP_EQUATION, NULL, tidy(left), rate);
		ast_Cleanup(left);

		if ((mu = exponential_of_integral(rate, v)) == NULL) {
			ast_Cleanup(M);
			ast_Cleanup(N);
			return E_DE_INTEGRAL;
		}

		work_Pause();
		M = ast_MakeBinary(OP_MULT, ast_Copy(mu), M);
		N = ast_MakeBinary(OP_MULT, mu, N);
		expand(M, EXP_ALL);
		expand(N, EXP_ALL);
		simplify(M, SIMP_BASIC);
		simplify(N, SIMP_BASIC);
		left = ast_MakeBinary(OP_ADD, ast_Copy(M), ast_MakeBinary(OP_MULT, ast_Copy(N), de_Derivative(de->y, 1)));
		zero = integer(0);
		work_Resume();

		work_Step(STEP_EQUATION, "Multiply by the integrating factor", tidy(left), zero);
		ast_Cleanup(left);
		ast_Cleanup(zero);

		if (!is_exact(de, M, N, true)) {
			ast_Cleanup(M);
			ast_Cleanup(N);
			return E_DE_UNSOLVED;
		}
	}

	P = ast_MakeBinary(OP_INTEGRAL, M, ast_Copy(de->x));
	eval_integrals(P);

	if (contains_integral(P)) {
		ast_Cleanup(P);
		ast_Cleanup(N);
		return E_DE_INTEGRAL;
	}

	work_Pause();
	simplify(P, SIMP_BASIC);
	work_Resume();

	Q = partial(P, de->y);
	record_partial(P, de->y, Q, "Differentiate with respect to the function");

	work_Pause();
	r = difference(ast_Copy(N), ast_Copy(Q));
	expand(r, EXP_ALL);
	simplify(r, SIMP_BASIC);
	work_Resume();

	if (involves(r, de->x)) {
		ast_Cleanup(r);
		ast_Cleanup(P);
		ast_Cleanup(Q);
		ast_Cleanup(N);
		return E_DE_UNSOLVED;
	}

	G = substitution_symbol(de, SYM_G);
	left = ast_MakeBinary(OP_ADD, Q, de_Derivative(G, 1));
	work_Step(STEP_EQUATION, NULL, left, N);
	ast_Cleanup(left);
	ast_Cleanup(N);
	left = de_Derivative(G, 1);
	work_Step(STEP_EQUATION, NULL, left, r);
	ast_Cleanup(left);
	ast_Cleanup(G);

	if (is_ast_int(r, 0)) {
		G = r;
	} else {
		G = ast_MakeBinary(OP_INTEGRAL, r, ast_Copy(de->y));
		eval_integrals(G);

		if (contains_integral(G)) {
			ast_Cleanup(G);
			ast_Cleanup(P);
			return E_DE_INTEGRAL;
		}
	}

	finish(de, ast_MakeBinary(OP_ADD, P, G), integer(0), solution);

	return E_SUCCESS;
}

pcas_error_t de_Solve(pcas_de_t *de, pcas_ast_t **solution) {
	pcas_error_t err;
	unsigned k;

	*solution = NULL;

	if (de->condition_count > de->order)
		return E_DE_BAD_CONDITION;

	for (k = 0; k < de->condition_count; k++) {
		if (de->conditions[k].order >= de->order)
			return E_DE_BAD_CONDITION;
	}

	if (de->order == 1) {
		if (de->linear && !is_ast_int(de->a[0], 0) && !is_ast_int(de->g, 0))
			return solve_linear_first(de, solution);
		if ((err = solve_separable(de, solution)) != E_DE_UNSOLVED)
			return err;
		if ((err = solve_exact(de, solution)) != E_DE_UNSOLVED)
			return err;
		if ((err = solve_bernoulli(de, solution)) != E_DE_UNSOLVED)
			return err;
		if ((err = solve_homogeneous(de, solution)) != E_DE_UNSOLVED)
			return err;
		return solve_linear_argument(de, solution);
	}

	return E_DE_UNSOLVED;
}
