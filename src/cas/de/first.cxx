#include "internal.hxx"

#include "../../work.hxx"

/*Writes the first order equation as M + Ny' = 0, without simplifying M. Returns false if y' does not appear linearly.*/
static bool differential_form(DiffEq *de, ast **M, ast **N) {
	ast *symbols[DiffEq::max_order + 1];
	ast *f = difference(de->equation->firstChild()->copy(), de->equation->firstChild()->next()->copy());
	derivatives_to_symbols(de, f, symbols);
	simplify(f, Simp::Basic);

	*N = f->copy();
	derivative(*N, symbols[1], symbols[1]);
	simplify(*N, Simp::Basic);

	const bool linear = is_constant(*N, symbols[1]) && !(*N)->isInt(0);

	if (linear) {
		ast *zero = integer(0);
		substitute(f, symbols[1], zero);
		ast::dispose(zero);
		*M = f;
	} else {
		ast::dispose(f);
		ast::dispose(*N);
	}

	ast::dispose(symbols[0]);
	ast::dispose(symbols[1]);

	return linear;
}

/*Returns the right side of the first order equation solved for y', or nullptr if y' does not appear linearly*/
static ast *solve_for_prime(DiffEq *de) {
	ast *M, *N;

	if (!differential_form(de, &M, &N))
		return nullptr;

	ast *F = ast::make(Op::Div, negate(M), N);
	simplify(F, Simp::Basic);

	return F;
}

/*Appends the factors of e, raised to exponent, to g when they do not involve y and to h when they do not involve x. Returns false if e is not such a product.*/
static bool separate(DiffEq *de, const ast *e, const ast *exponent, ast *g, ast *h) {
	const bool in_x = involves(e, de->x), in_y = involves(e, de->y);
	bool separable = true;

	if (!in_y || (!in_x && !e->isOp(Op::Mult) && !e->isOp(Op::Div))) {
		(in_y ? h : g)->appendChild(ast::make(Op::Pow, e->copy(), exponent->copy()));
		return true;
	}

	if (e->isOp(Op::Mult)) {
		for (const ast *child = e->firstChild(); child != nullptr && separable; child = child->next())
			separable = separate(de, child, exponent, g, h);
		return separable;
	}

	if (e->isOp(Op::Div)) {
		ast *next = negate(exponent->copy());
		separable = separate(de, e->firstChild(), exponent, g, h) && separate(de, e->firstChild()->next(), next, g, h);
		ast::dispose(next);
		return separable;
	}

	if (e->isOp(Op::Pow)) {
		const ast *base = e->firstChild();
		const ast *power = base->next();

		if (!involves(power, de->x) && !involves(power, de->y)) {
			ast *next = ast::make(Op::Mult, exponent->copy(), power->copy());
			separable = separate(de, base, next, g, h);
			ast::dispose(next);
			return separable;
		}

		if (involves(base, de->x) || involves(base, de->y) || !power->isOp(Op::Add))
			return false;

		for (const ast *child = power->firstChild(); child != nullptr && separable; child = child->next()) {
			ast *next = ast::make(Op::Pow, base->copy(), child->copy());
			separable = separate(de, next, exponent, g, h);
			ast::dispose(next);
		}

		return separable;
	}

	if (e->isOp(Op::Add)) {
		ast *next = e->copy();
		factor(next, Factor::All);
		separable = !next->isOp(Op::Add) && separate(de, next, exponent, g, h);
		ast::dispose(next);
		return separable;
	}

	return false;
}

/*True if h is zero at the initial value, which makes y constant*/
static bool is_equilibrium(DiffEq *de, const ast *h, DiffEq::Condition *c) {
	ast *value = h->copy();

	work::pause();
	substitute(value, de->y, c->value);
	ast *zero = value->copy();
	simplify(zero, Simp::All);
	work::resume();

	const bool equilibrium = zero->isInt(0);

	if (equilibrium) {
		work::step(work::Step::Type::Equation, "Zero at the initial value", value, zero);
		work::step(work::Step::Type::Equation, "Constant solution", de->y, c->value);
	}

	ast::dispose(value);
	ast::dispose(zero);

	return equilibrium;
}

static Error solve_separable(DiffEq *de, ast **solution) {
	DiffEq::Condition *c = de->condition_count > 0 ? &de->conditions[0] : nullptr;

	work::pause();
	ast *F = solve_for_prime(de), *g = ast::make(Op::Mult), *h = ast::make(Op::Mult), *one = integer(1);
	const bool separable = F != nullptr && separate(de, F, one, g, h);
	simplify(g, Simp::Basic);
	simplify(h, Simp::Basic);
	work::resume();

	ast::dispose(F);
	ast::dispose(one);

	if (!separable) {
		ast::dispose(g);
		ast::dispose(h);
		return Error::DeUnsolved;
	}

	de->method = "Separable";

	ast *prime = DiffEq::derivative(de->y, 1);
	F = ast::make(Op::Mult, g->copy(), h->copy());
	work::step(work::Step::Type::Equation, "Separable", prime, F);
	ast::dispose(prime);
	ast::dispose(F);

	if (c != nullptr && is_equilibrium(de, h, c)) {
		*solution = ast::make(Op::Equals, de->y->copy(), c->value->copy());
		ast::dispose(g);
		ast::dispose(h);
		return Error::Success;
	}

	work::pause();
	ast *lhs = ast::make(Op::Div, integer(1), h);
	simplify(lhs, Simp::Basic);
	work::resume();

	lhs = ast::make(Op::Integral, lhs, de->y->copy());
	ast *antiderivative = ast::make(Op::Integral, g, de->x->copy());
	work::step(work::Step::Type::Equation, "Separate variables", lhs, antiderivative);

	ast *equation = ast::make(Op::Equals, lhs, antiderivative);
	eval_integrals(equation);

	if (contains_integral(equation)) {
		ast::dispose(equation);
		return Error::DeIntegral;
	}

	finish(de, equation->firstChild()->copy(), equation->firstChild()->next()->copy(), solution);
	ast::dispose(equation);

	return Error::Success;
}

/*Solves y' + Py = Q by multiplying by the integrating factor e^(integral of P)*/
static Error solve_linear_first(DiffEq *de, ast **solution) {
	work::pause();
	ast *P = ast::make(Op::Div, de->a[0]->copy(), de->a[1]->copy());
	simplify(P, Simp::Basic);
	factor_cancel(P);
	ast *Q = ast::make(Op::Div, de->g->copy(), de->a[1]->copy());
	simplify(Q, Simp::Basic);
	factor_cancel(Q);
	work::resume();

	de->method = "Linear";

	if (!de->a[1]->isInt(1)) {
		ast *left = ast::make(Op::Add, DiffEq::derivative(de->y, 1), ast::make(Op::Mult, P->copy(), de->y->copy()));
		work::step(work::Step::Type::Equation, "Standard form", left, Q);
		ast::dispose(left);
	}

	work::text("Integrating factor");

	ast *mu = exponential_of_integral(P, de->x);
	if (mu == nullptr) {
		ast::dispose(Q);
		return Error::DeIntegral;
	}

	work::pause();
	ast *product = ast::make(Op::Mult, mu->copy(), de->y->copy());
	simplify(product, Simp::Basic);
	ast *right = ast::make(Op::Mult, mu, Q);
	simplify(right, Simp::Basic);
	factor_cancel(right);
	work::resume();

	ast *left = derivative_node(product->copy(), de->x);
	work::step(work::Step::Type::Equation, "Multiply by the integrating factor", left, right);
	ast::dispose(left);

	right = ast::make(Op::Integral, right, de->x->copy());
	eval_integrals(right);

	if (contains_integral(right)) {
		ast::dispose(product);
		ast::dispose(right);
		return Error::DeIntegral;
	}

	finish(de, product, right, solution);

	return Error::Success;
}

typedef Error (*solver_t)(DiffEq *de, ast **solution);

/*Solves equation, a first order equation in u = back, with solver, then substitutes back and solves for y. Takes ownership of back and equation.*/
static Error solve_substituted(DiffEq *de, ast *back, ast *equation, solver_t solver, ast **solution) {
	DiffEq::Condition *c = de->condition_count > 0 ? &de->conditions[0] : nullptr;
	ast *inner = nullptr, *u = nullptr, *constant = nullptr;
	DiffEq sub;

	work::pause();
	Error err = sub.load(equation, de->x);
	work::resume();
	ast::dispose(equation);

	if (err == Error::Success) {
		if (c != nullptr) {
			DiffEq::Condition *s = &sub.conditions[sub.condition_count++];
			s->order = 0;
			s->at = c->at->copy();
			s->value = at_condition(de, back, c);

			work::pause();
			simplify(s->value, Simp::All);
			work::resume();
		}

		sub.nested = true;
		u = sub.y->copy();
		constant = ast::make(constant_symbol(sub.equation));
		err = solver(&sub, &inner);
	}

	sub.clear();
	canonical_SetFunction(de->y->symbol());

	if (err == Error::Success) {
		ast *lhs = inner->firstChild()->copy();
		ast *rhs = inner->firstChild()->next()->copy();

		work::pause();
		substitute(lhs, u, back);
		substitute(rhs, u, back);
		simplify(lhs, Simp::Basic);
		simplify(rhs, Simp::Basic);
		work::resume();

		work::step(work::Step::Type::Equation, "Substitute back", lhs, rhs);
		conclude(de, lhs, rhs, constant, c, solution);
	}

	ast::dispose(inner);
	ast::dispose(u);
	ast::dispose(constant);
	ast::dispose(back);

	return err;
}

/*Returns the power of y that e is, or nullptr if e is not a constant power of y*/
static ast *power_of_function(const DiffEq *de, const ast *e) {
	if (e->compare(*de->y))
		return integer(1);

	if (e->isOp(Op::Pow) && e->firstChild()->compare(*de->y) && e->firstChild()->next()->isNumber())
		return e->firstChild()->next()->copy();

	return nullptr;
}

/*Writes y' = F as y' + Py = Qy^n with n not 0 or 1. Returns false if F has another form.*/
static bool bernoulli_form(DiffEq *de, const ast *F, ast **P, ast **Q, ast **n) {
	ast *terms = F->copy(), *one = integer(1);

	expand(terms, Expand::All);
	simplify(terms, Simp::Basic);

	*P = ast::make(Op::Add);
	*Q = ast::make(Op::Add);
	*n = nullptr;
	bool bernoulli = terms->isOp(Op::Add);

	for (const ast *term = terms->firstChild(); bernoulli && term != nullptr; term = term->next()) {
		ast *g = ast::make(Op::Mult);
		ast *h = ast::make(Op::Mult);

		bernoulli = separate(de, term, one, g, h);
		simplify(h, Simp::Basic);
		ast *power = bernoulli ? power_of_function(de, h) : nullptr;

		if (power == nullptr || power->isInt(0) || (*n != nullptr && !power->isInt(1) && !(*n)->compare(*power))) {
			bernoulli = false;
			ast::dispose(g);
		} else if (power->isInt(1)) {
			(*P)->appendChild(negate(g));
		} else {
			(*Q)->appendChild(g);
			if (*n == nullptr) {
				*n = power;
				power = nullptr;
			}
		}

		ast::dispose(power);
		ast::dispose(h);
	}

	bernoulli &= *n != nullptr && (*P)->childCount() > 0;

	ast::dispose(terms);
	ast::dispose(one);

	if (!bernoulli) {
		ast::dispose(*P);
		ast::dispose(*Q);
		ast::dispose(*n);
		return false;
	}

	simplify(*P, Simp::Basic);
	simplify(*Q, Simp::Basic);

	return true;
}

/*Solves y' + Py = Qy^n with the substitution v = y^(1-n), which makes it linear*/
static Error solve_bernoulli(DiffEq *de, ast **solution) {
	ast *P, *Q, *n;

	work::pause();
	ast *F = solve_for_prime(de);
	const bool bernoulli = F != nullptr && bernoulli_form(de, F, &P, &Q, &n);
	work::resume();

	ast::dispose(F);

	if (!bernoulli)
		return Error::DeUnsolved;

	de->method = "Bernoulli";

	ast *left = ast::make(Op::Add, DiffEq::derivative(de->y, 1), ast::make(Op::Mult, P->copy(), de->y->copy()));
	ast *right = ast::make(Op::Mult, Q->copy(), ast::make(Op::Pow, de->y->copy(), n->copy()));
	work::step(work::Step::Type::Equation, "Bernoulli", tidy(left), tidy(right));
	ast::dispose(left);
	ast::dispose(right);

	work::pause();
	ast *m = difference(integer(1), n->copy());
	simplify(m, Simp::Basic);
	work::resume();

	ast *v = substitution_symbol(de, Sym::V);
	ast *back = ast::make(Op::Pow, de->y->copy(), m->copy());
	work::step(work::Step::Type::Equation, "Substitute", v, back);

	left = DiffEq::derivative(v, 1);
	right = ast::make(
		Op::Mult,
		m->copy(),
		ast::make(Op::Mult, ast::make(Op::Pow, de->y->copy(), negate(n)), DiffEq::derivative(de->y, 1))
	);
	work::step(work::Step::Type::Equation, nullptr, left, tidy(right));
	ast::dispose(right);

	work::pause();
	P = ast::make(Op::Mult, m->copy(), P);
	simplify(P, Simp::Basic);
	factor_cancel(P);
	Q = ast::make(Op::Mult, m, Q);
	simplify(Q, Simp::Basic);
	factor_cancel(Q);
	work::resume();

	left = ast::make(Op::Add, left, ast::make(Op::Mult, P, v->copy()));
	work::step(work::Step::Type::Equation, "Linear", tidy(left), Q);
	ast::dispose(v);

	return solve_substituted(de, back, ast::make(Op::Equals, left, Q), solve_linear_first, solution);
}

/*Solves y' = F(x, y), where F is unchanged by scaling x and y, with the substitution y = ux*/
static Error solve_homogeneous(DiffEq *de, ast **solution) {
	work::pause();
	ast *F = solve_for_prime(de);
	work::resume();

	if (F == nullptr || !involves(F, de->x) || !involves(F, de->y)) {
		ast::dispose(F);
		return Error::DeUnsolved;
	}

	ast *u = substitution_symbol(de, Sym::U);
	ast *ux = ast::make(Op::Mult, u->copy(), de->x->copy());

	work::pause();
	ast *G = F->copy();
	substitute(G, de->y, ux);
	simplify(G, Simp::Basic);
	ast *d = G->copy();
	derivative(d, de->x, de->x);
	work::resume();

	const bool homogeneous = is_zero(d);
	ast::dispose(d);

	if (!homogeneous) {
		ast::dispose(F);
		ast::dispose(G);
		ast::dispose(u);
		ast::dispose(ux);
		return Error::DeUnsolved;
	}

	de->method = "Homogeneous";

	work::pause();
	ast *one = integer(1);
	substitute(G, de->x, one);
	ast::dispose(one);
	work::resume();

	single_fraction(G);

	ast *left = DiffEq::derivative(de->y, 1);
	work::step(work::Step::Type::Equation, "Homogeneous", left, F);
	work::step(work::Step::Type::Equation, "Substitute", de->y, ux);

	ast *right = ast::make(Op::Add, u->copy(), ast::make(Op::Mult, de->x->copy(), DiffEq::derivative(u, 1)));
	work::step(work::Step::Type::Equation, nullptr, left, right);
	work::step(work::Step::Type::Equation, nullptr, right, G);
	ast::dispose(left);
	ast::dispose(right);
	ast::dispose(F);
	ast::dispose(ux);

	right = ast::make(Op::Div, difference(G, u->copy()), de->x->copy());
	single_fraction(right);
	left = DiffEq::derivative(u, 1);
	ast::dispose(u);

	return solve_substituted(
		de,
		ast::make(Op::Div, de->y->copy(), de->x->copy()),
		ast::make(Op::Equals, left, right),
		solve_separable,
		solution
	);
}

/*Returns d/dv(e), or nullptr if it depends on x or y or is zero*/
static ast *constant_slope(const DiffEq *de, const ast *e, const ast *v) {
	ast *slope = e->copy();

	derivative(slope, v, v);
	simplify(slope, Simp::Basic);

	if (involves(slope, de->x) || involves(slope, de->y) || slope->isInt(0)) {
		ast::dispose(slope);
		return nullptr;
	}

	return slope;
}

/*Finds a sum ax + by in e, with constants a and b, whose substitution as u leaves only u in F, which becomes G*/
static ast *linear_argument(const DiffEq *de, const ast *e, const ast *F, const ast *u, ast **a, ast **b, ast **G) {
	if (!e->isOperator())
		return nullptr;

	if (e->isOp(Op::Add)) {
		ast *sum = ast::make(Op::Add);

		for (const ast *child : e->children()) {
			if (involves(child, de->x) || involves(child, de->y))
				sum->appendChild(child->copy());
		}

		*a = constant_slope(de, sum, de->x);
		*b = constant_slope(de, sum, de->y);

		if (*a != nullptr && *b != nullptr) {
			ast *y = ast::make(
				Op::Div, difference(u->copy(), ast::make(Op::Mult, (*a)->copy(), de->x->copy())), (*b)->copy()
			);
			*G = F->copy();
			substitute(*G, de->y, y);
			simplify(*G, Simp::Basic);
			ast::dispose(y);

			if (!involves(*G, de->x)) {
				simplify(sum, Simp::Basic);
				return sum;
			}

			ast::dispose(*G);
		}

		ast::dispose(*a);
		ast::dispose(*b);
		ast::dispose(sum);
	}

	ast *found = nullptr;
	for (const ast *child = e->firstChild(); child != nullptr && found == nullptr; child = child->next())
		found = linear_argument(de, child, F, u, a, b, G);

	return found;
}

/*Solves y' = F(ax + by) with the substitution u = ax + by, which makes it separable*/
static Error solve_linear_argument(DiffEq *de, ast **solution) {
	ast *G, *a, *b;

	work::pause();
	ast *F = solve_for_prime(de);
	ast *u = substitution_symbol(de, Sym::U);
	ast *sum = F != nullptr ? linear_argument(de, F, F, u, &a, &b, &G) : nullptr;
	work::resume();

	ast::dispose(F);

	if (sum == nullptr) {
		ast::dispose(u);
		return Error::DeUnsolved;
	}

	de->method = "Substitution";

	work::step(work::Step::Type::Equation, "Substitute", u, sum);

	ast *left = DiffEq::derivative(u, 1);
	ast *right = ast::make(Op::Add, a->copy(), ast::make(Op::Mult, b->copy(), DiffEq::derivative(de->y, 1)));
	work::step(work::Step::Type::Equation, nullptr, left, tidy(right));
	ast::dispose(right);

	right = ast::make(Op::Add, a, ast::make(Op::Mult, b, G));
	work::step(work::Step::Type::Equation, nullptr, left, tidy(right));
	single_fraction(right);
	ast::dispose(u);

	return solve_substituted(de, sum, ast::make(Op::Equals, left, right), solve_separable, solution);
}

static ast *partial(const ast *e, const ast *v) {
	ast *d = e->copy();

	work::pause();
	derivative(d, v, v);
	simplify(d, Simp::Basic);
	work::resume();

	return d;
}

/*Records d/dv(e) = value*/
static void record_partial(const ast *e, const ast *v, const ast *value, const char *text) {
	ast *node = derivative_node(e->copy(), v);

	work::step(work::Step::Type::Equation, text, node, value);
	ast::dispose(node);
}

/*True if M + Ny' = 0 is exact, recording the check if record*/
static bool is_exact(DiffEq *de, const ast *M, const ast *N, bool record) {
	ast *My = partial(M, de->y), *Nx = partial(N, de->x);
	ast *remainder = difference(My->copy(), Nx->copy());
	const bool exact = is_zero(remainder);

	if (record) {
		record_partial(M, de->y, My, nullptr);
		record_partial(N, de->x, Nx, nullptr);
		work::text(exact ? "Exact" : "Not exact");
	}

	ast::dispose(remainder);
	ast::dispose(My);
	ast::dispose(Nx);

	return exact;
}

/*Returns (a_v - b_w)/c simplified if it involves only v, otherwise nullptr*/
static ast *factor_rate(DiffEq *de, const ast *a, const ast *w, const ast *b, const ast *v, const ast *c) {
	ast *rate = ast::make(Op::Div, difference(partial(a, w), partial(b, v)), c->copy());
	const ast *other = v->compare(*de->x) ? de->y : de->x;

	single_fraction(rate);

	if (involves(rate, other)) {
		ast::dispose(rate);
		return nullptr;
	}

	return rate;
}

/*Solves M + Ny' = 0 when it is exact, or becomes exact after multiplying by an integrating factor of x or y alone*/
static Error solve_exact(DiffEq *de, ast **solution) {
	ast *M, *N;

	work::pause();
	if (!differential_form(de, &M, &N)) {
		work::resume();
		return Error::DeUnsolved;
	}

	simplify(M, Simp::Basic);
	const bool exact = is_exact(de, M, N, false);

	ast *rate = nullptr;
	const ast *v = nullptr;
	if (!exact) {
		if ((rate = factor_rate(de, M, de->y, N, de->x, N)) != nullptr)
			v = de->x;
		else if ((rate = factor_rate(de, N, de->x, M, de->y, M)) != nullptr)
			v = de->y;
	}
	work::resume();

	if (!exact && rate == nullptr) {
		ast::dispose(M);
		ast::dispose(N);
		return Error::DeUnsolved;
	}

	de->method = "Exact";

	ast *left = ast::make(Sym::M);
	work::step(work::Step::Type::Equation, nullptr, left, M);
	ast::dispose(left);
	left = ast::make(Sym::N);
	work::step(work::Step::Type::Equation, nullptr, left, N);
	ast::dispose(left);

	is_exact(de, M, N, true);

	if (!exact) {
		work::text("Integrating factor");

		left = v == de->x ? ast::make(Op::Div, difference(partial(M, de->y), partial(N, de->x)), N->copy())
						  : ast::make(Op::Div, difference(partial(N, de->x), partial(M, de->y)), M->copy());
		work::step(work::Step::Type::Equation, nullptr, tidy(left), rate);
		ast::dispose(left);

		ast *mu = exponential_of_integral(rate, v);
		if (mu == nullptr) {
			ast::dispose(M);
			ast::dispose(N);
			return Error::DeIntegral;
		}

		work::pause();
		M = ast::make(Op::Mult, mu->copy(), M);
		N = ast::make(Op::Mult, mu, N);
		expand(M, Expand::All);
		expand(N, Expand::All);
		simplify(M, Simp::Basic);
		simplify(N, Simp::Basic);
		left = ast::make(Op::Add, M->copy(), ast::make(Op::Mult, N->copy(), DiffEq::derivative(de->y, 1)));
		ast *zero = integer(0);
		work::resume();

		work::step(work::Step::Type::Equation, "Multiply by the integrating factor", tidy(left), zero);
		ast::dispose(left);
		ast::dispose(zero);

		if (!is_exact(de, M, N, true)) {
			ast::dispose(M);
			ast::dispose(N);
			return Error::DeUnsolved;
		}
	}

	ast *P = ast::make(Op::Integral, M, de->x->copy());
	eval_integrals(P);

	if (contains_integral(P)) {
		ast::dispose(P);
		ast::dispose(N);
		return Error::DeIntegral;
	}

	work::pause();
	simplify(P, Simp::Basic);
	work::resume();

	ast *Q = partial(P, de->y);
	record_partial(P, de->y, Q, "Differentiate with respect to the function");

	work::pause();
	ast *r = difference(N->copy(), Q->copy());
	expand(r, Expand::All);
	simplify(r, Simp::Basic);
	work::resume();

	if (involves(r, de->x)) {
		ast::dispose(r);
		ast::dispose(P);
		ast::dispose(Q);
		ast::dispose(N);
		return Error::DeUnsolved;
	}

	ast *G = substitution_symbol(de, Sym::G);
	left = ast::make(Op::Add, Q, DiffEq::derivative(G, 1));
	work::step(work::Step::Type::Equation, nullptr, left, N);
	ast::dispose(left);
	ast::dispose(N);
	left = DiffEq::derivative(G, 1);
	work::step(work::Step::Type::Equation, nullptr, left, r);
	ast::dispose(left);
	ast::dispose(G);

	if (r->isInt(0)) {
		G = r;
	} else {
		G = ast::make(Op::Integral, r, de->y->copy());
		eval_integrals(G);

		if (contains_integral(G)) {
			ast::dispose(G);
			ast::dispose(P);
			return Error::DeIntegral;
		}
	}

	finish(de, ast::make(Op::Add, P, G), integer(0), solution);

	return Error::Success;
}

Error solve_first_order(DiffEq *de, ast **solution) {
	if (de->linear && !de->a[0]->isInt(0) && !de->g->isInt(0))
		return solve_linear_first(de, solution);

	Error err;
	if ((err = solve_separable(de, solution)) != Error::DeUnsolved)
		return err;
	if ((err = solve_exact(de, solution)) != Error::DeUnsolved)
		return err;
	if ((err = solve_bernoulli(de, solution)) != Error::DeUnsolved)
		return err;
	if ((err = solve_homogeneous(de, solution)) != Error::DeUnsolved)
		return err;
	return solve_linear_argument(de, solution);
}
