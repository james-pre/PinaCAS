#include "internal.h"

#include "../../work.h"

/*Limits the size of the expansions that check whether an expression is zero*/
#define MAX_EXPANDED_TERMS 4096

pcas_ast_t *integer(mp_small n) {
	return ast_MakeNumber(num_FromInt(n));
}

pcas_ast_t *negate(pcas_ast_t *a) {
	return ast_MakeBinary(OP_MULT, integer(-1), a);
}

pcas_ast_t *difference(pcas_ast_t *a, pcas_ast_t *b) {
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

void derivatives_to_symbols(pcas_de_t *de, pcas_ast_t *f, pcas_ast_t **symbols) {
	pcas_ast_t *scope = ast_MakeBinary(OP_ADD, ast_Copy(f), ast_Copy(de->x));
	unsigned k;

	for (k = de->order; k > 0; k--) {
		pcas_ast_t *derivative = de_Derivative(de->y, k);

		symbols[k] = ast_MakeSymbol(fresh_symbol(scope));
		ast_ChildAppend(scope, ast_Copy(symbols[k]));
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
	de->known = NULL;
	de->center = NULL;
	de->series = false;
	de->terms = DE_DEFAULT_TERMS;
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
	ast_Cleanup(de->known);
	ast_Cleanup(de->center);

	for (k = 0; k <= DE_MAX_ORDER; k++)
		ast_Cleanup(de->a[k]);

	for (k = 0; k < de->condition_count; k++) {
		ast_Cleanup(de->conditions[k].at);
		ast_Cleanup(de->conditions[k].value);
	}

	canonical_SetFunction(SYM_INVALID);
	canonical_SetSeries(NULL);
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

	for (i = 1; i < count && err == E_SUCCESS; i++) {
		if (isoptype(items[i], OP_EQUALS) && ast_Compare(opbase(items[i]), de->y))
			err = de_AddKnownSolution(de, items[i]);
		else if (isoptype(items[i], OP_EQUALS) && ast_Compare(opbase(items[i]), de->x))
			err = de_AddCenter(de, items[i]);
		else
			err = de_AddCondition(de, items[i]);
	}

	return err;
}

pcas_error_t de_AddCenter(pcas_de_t *de, const pcas_ast_t *center) {
	const pcas_ast_t *x0 = opbase(center)->next;

	if (de->center != NULL || involves(x0, de->x) || involves(x0, de->y))
		return E_DE_BAD_CONDITION;

	de->center = ast_Copy(x0);

	work_Pause();
	simplify(de->center, SIMP_BASIC);
	work_Resume();

	return E_SUCCESS;
}

pcas_error_t de_AddKnownSolution(pcas_de_t *de, const pcas_ast_t *solution) {
	const pcas_ast_t *f = opbase(solution)->next;

	if (de->known != NULL || involves(f, de->y))
		return E_DE_BAD_CONDITION;

	de->known = ast_Copy(f);

	work_Pause();
	simplify(de->known, SIMP_BASIC);
	work_Resume();

	return E_SUCCESS;
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

bool is_euler(const pcas_ast_t *e) {
	return e->type == NODE_SYMBOL && e->op.symbol == SYM_EULER;
}

pcas_ast_t *ln(pcas_ast_t *a) {
	return ast_MakeBinary(OP_LOG, ast_MakeSymbol(SYM_EULER), a);
}

pcas_ast_t *exponential(const pcas_ast_t *base, pcas_ast_t *e) {
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

void rational_parts(pcas_ast_t *e, pcas_ast_t **num, pcas_ast_t **den) {
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
		pcas_ast_t *distinct = ast_MakeOperator(OP_MULT), *n, *d, *other;
		bool skipped;

		for (child = opbase(e); child != NULL; child = child->next) {
			rational_parts(child, &n, &d);
			ast_ChildAppend(nums, n);
			ast_ChildAppend(dens, d);

			for (other = opbase(distinct); other != NULL && !ast_Compare(other, d); other = other->next)
				;
			if (other == NULL)
				ast_ChildAppend(distinct, ast_Copy(d));
		}

		*num = ast_MakeOperator(OP_ADD);

		for (n = opbase(nums), d = opbase(dens); n != NULL; n = n->next, d = d->next) {
			pcas_ast_t *term = ast_MakeOperator(OP_MULT);

			ast_ChildAppend(term, ast_Copy(n));
			skipped = false;
			for (other = opbase(distinct); other != NULL; other = other->next) {
				if (!skipped && ast_Compare(other, d))
					skipped = true;
				else
					ast_ChildAppend(term, ast_Copy(other));
			}

			ast_ChildAppend(*num, term);
		}

		ast_Cleanup(nums);
		ast_Cleanup(dens);
		*den = distinct;
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

/*Rewrites each tan(u) in e as sin(u)/cos(u)*/
static void tangents_to_sines(pcas_ast_t *e) {
	pcas_ast_t *child;

	if (e->type != NODE_OPERATOR)
		return;

	for (child = opbase(e); child != NULL; child = child->next)
		tangents_to_sines(child);

	if (isoptype(e, OP_TAN))
		replace_node(
			e,
			ast_MakeBinary(
				OP_DIV, ast_MakeUnary(OP_SIN, ast_Copy(opbase(e))), ast_MakeUnary(OP_COS, ast_Copy(opbase(e)))
			)
		);
}

/*Returns how many terms e has once expanded, or more than limit if that is over limit*/
static unsigned long expanded_terms(const pcas_ast_t *e, unsigned long limit) {
	pcas_ast_t *child;
	unsigned long terms, base;
	mp_small n;

	if (isoptype(e, OP_ADD) || isoptype(e, OP_MULT)) {
		terms = isoptype(e, OP_ADD) ? 0 : 1;

		for (child = opbase(e); child != NULL && terms <= limit; child = child->next) {
			base = expanded_terms(child, limit);
			terms = isoptype(e, OP_ADD) ? terms + base : terms * base;
		}

		return terms;
	}

	if (isoptype(e, OP_POW) && opbase(e)->next->type == NODE_NUMBER && mp_rat_is_integer(opbase(e)->next->op.num) &&
		mp_int_to_int(MP_NUMER_P(opbase(e)->next->op.num), &n) == MP_OK && n > 1) {
		base = expanded_terms(opbase(e), limit);

		for (terms = 1; n-- > 0 && terms <= limit;)
			terms *= base;

		return terms;
	}

	return 1;
}

/*Replaces each sin(u)^n with n at least 2 by (1 - cos(u)^2)sin(u)^(n - 2), returning whether it changed e*/
static bool reduce_sine_powers(pcas_ast_t *e) {
	pcas_ast_t *child, *sine;
	mp_small n;
	bool changed = false;

	if (e->type != NODE_OPERATOR)
		return false;

	for (child = opbase(e); child != NULL; child = child->next)
		changed |= reduce_sine_powers(child);

	if (isoptype(e, OP_POW) && isoptype(opbase(e), OP_SIN) && opbase(e)->next->type == NODE_NUMBER &&
		mp_rat_is_integer(opbase(e)->next->op.num) && mp_int_to_int(MP_NUMER_P(opbase(e)->next->op.num), &n) == MP_OK &&
		n >= 2) {
		sine = opbase(e);
		replace_node(
			e,
			ast_MakeBinary(
				OP_MULT,
				difference(
					integer(1), ast_MakeBinary(OP_POW, ast_MakeUnary(OP_COS, ast_Copy(opbase(sine))), integer(2))
				),
				ast_MakeBinary(OP_POW, ast_Copy(sine), integer(n - 2))
			)
		);
		changed = true;
	}

	return changed;
}

/*True if the numerator of e over a common denominator expands to zero after simplifying e with flags, giving up when it would expand past MAX_EXPANDED_TERMS*/
static bool numerator_vanishes(const pcas_ast_t *e, unsigned short flags) {
	pcas_ast_t *copy = ast_Copy(e), *numerator, *denominator;
	bool zero;

	simplify(copy, flags);
	tangents_to_sines(copy);
	split_exponentials(copy);
	rational_parts(copy, &numerator, &denominator);

	if (expanded_terms(numerator, MAX_EXPANDED_TERMS) > MAX_EXPANDED_TERMS) {
		ast_Cleanup(copy);
		ast_Cleanup(numerator);
		ast_Cleanup(denominator);
		return false;
	}

	expand(numerator, EXP_ALL);
	simplify(numerator, SIMP_BASIC);
	expand(numerator, EXP_ALL);
	simplify(numerator, SIMP_BASIC);

	while (!is_ast_int(numerator, 0) && reduce_sine_powers(numerator) &&
		   expanded_terms(numerator, MAX_EXPANDED_TERMS) <= MAX_EXPANDED_TERMS) {
		expand(numerator, EXP_ALL);
		simplify(numerator, SIMP_BASIC);
	}

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

bool is_zero(const pcas_ast_t *e) {
	bool zero;

	work_Pause();
	zero = numerator_vanishes(e, SIMP_BASIC) || numerator_vanishes(e, SIMP_ALL);
	work_Resume();

	return zero;
}

bool involves(const pcas_ast_t *e, const pcas_ast_t *v) {
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

void expand_if_smaller(pcas_ast_t *e) {
	pcas_ast_t *expanded = ast_Copy(e);

	expand(expanded, EXP_ALL);
	simplify(expanded, SIMP_BASIC);

	if (node_count(expanded) < node_count(e))
		replace_node(e, expanded);
	else
		ast_Cleanup(expanded);
}

pcas_ast_t *derivative_node(pcas_ast_t *e, const pcas_ast_t *v) {
	pcas_ast_t *node = ast_MakeOperator(OP_DERIV);

	ast_ChildAppend(node, e);
	ast_ChildAppend(node, ast_Copy(v));
	ast_ChildAppend(node, ast_Copy(v));

	return node;
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

pcas_ast_t *exponential_of_integral(pcas_ast_t *P, const pcas_ast_t *v) {
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
	G = ast_Copy(power);
	simplify(G, SIMP_BASIC);
	work_Resume();

	if (ast_Compare(G, mu))
		work_Step(STEP_STATE, NULL, NULL, mu);
	else
		work_Step(STEP_EQUATION, NULL, power, mu);
	ast_Cleanup(power);
	ast_Cleanup(G);

	return mu;
}

pcas_ast_t *tidy(pcas_ast_t *e) {
	work_Pause();
	simplify(e, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_EVAL);
	work_Resume();

	return e;
}

void single_fraction(pcas_ast_t *e) {
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

pcas_ast_t *substitution_symbol(const pcas_de_t *de, Symbol preferred) {
	pcas_ast_t *scope = ast_MakeBinary(OP_ADD, ast_Copy(de->equation), ast_Copy(de->x));
	Symbol symbol = contains_symbol(scope, preferred) ? fresh_symbol(scope) : preferred;

	ast_Cleanup(scope);

	return ast_MakeSymbol(symbol);
}

pcas_error_t check_solution(
	pcas_de_t *de,
	const pcas_ast_t *solution,
	const char *text,
	bool conditions,
	bool *satisfied
) {
	pcas_ast_t *derivatives[DE_MAX_ORDER + 1];
	pcas_ast_t *left, *right, *remainder;
	const pcas_ast_t *f = solution;
	unsigned k;

	if (isoptype(f, OP_EQUALS) && ast_Compare(opbase(f), de->y))
		f = opbase(f)->next;

	if (involves(f, de->y))
		return E_DE_IMPLICIT;

	work_Step(STEP_EQUATION, text, de->y, f);

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
		expand_if_smaller(d);
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

	for (k = 0; conditions && k < de->condition_count; k++)
		*satisfied &= check_condition(de, &de->conditions[k], derivatives);

	work_Text(*satisfied ? "It is a solution" : "It is not a solution");

	for (k = 0; k <= de->order; k++)
		ast_Cleanup(derivatives[k]);
	ast_Cleanup(left);
	ast_Cleanup(right);

	return E_SUCCESS;
}

pcas_error_t de_Verify(pcas_de_t *de, const pcas_ast_t *solution, bool *satisfied) {
	return check_solution(de, solution, "Solution", true, satisfied);
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

	if (de->series)
		return solve_power_series(de, solution);

	if (de->order == 1)
		return solve_first_order(de, solution);

	if (de->known != NULL)
		return solve_reduction_of_order(de, solution);

	if ((err = solve_constant_coefficients(de, solution)) != E_DE_UNSOLVED || !de->linear)
		return err;

	err = solve_power_series(de, solution);

	return err == E_DE_SINGULAR ? E_DE_UNSOLVED : err;
}
