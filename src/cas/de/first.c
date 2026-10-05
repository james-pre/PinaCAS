#include "internal.h"

#include "../../work.h"

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

	single_fraction(G);

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
	single_fraction(right);
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
	single_fraction(right);
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

	single_fraction(rate);

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

pcas_error_t solve_first_order(pcas_de_t *de, pcas_ast_t **solution) {
	pcas_error_t err;

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
