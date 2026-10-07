#include "internal.hxx"

#include "../../work.hxx"

/*Limits the size of the expansions that check whether an expression is zero*/
#define MAX_EXPANDED_TERMS 4096

ast *integer(mp_small n) {
	return ast::make(num::from(n));
}

ast *negate(ast *a) {
	return ast::make(Op::Mult, integer(-1), a);
}

ast *difference(ast *a, ast *b) {
	return ast::make(Op::Add, a, negate(b));
}

static const char *order_names[DiffEq::max_order] = {
	"First order",
	"Second order",
	"Third order",
	"Fourth order",
	"Fifth order",
	"Sixth order",
	"Seventh order",
	"Eighth order"
};

ast *DiffEq::derivative(const ast *y, unsigned order) {
	ast *d = y->copy();

	while (order-- > 0)
		d = ast::make(Op::Prime, d);

	return d;
}

/*Finds the function that primes are applied to and the highest number of primes on it*/
static Error find_function(ast *e, ast **y, unsigned *order) {
	if (e->isOp(Op::Prime)) {
		unsigned primes = 0;

		while (e->isOp(Op::Prime)) {
			e = e->childAt(0);
			primes++;
		}

		if (!e->isSymbol() || (*y != nullptr && !(*y)->compare(*e)))
			return Error::DeBadFunction;

		*y = e;
		if (primes > *order)
			*order = primes;

		return Error::Success;
	}

	if (e->isOperator()) {
		for (ast *child : e->children()) {
			const Error err = find_function(child, y, order);
			if (err != Error::Success)
				return err;
		}
	}

	return Error::Success;
}

void derivatives_to_symbols(DiffEq *de, ast *f, ast **symbols) {
	ast *scope = ast::make(Op::Add, f->copy(), de->x->copy());

	for (unsigned k = de->order; k > 0; k--) {
		ast *derivative = DiffEq::derivative(de->y, k);

		symbols[k] = ast::make(fresh_symbol(scope));
		scope->appendChild(symbols[k]->copy());
		substitute(f, derivative, symbols[k]);

		ast::dispose(derivative);
	}

	symbols[0] = de->y->copy();
	ast::dispose(scope);
}

/*Fills in the coefficients if f, the left side minus the right side, is linear in the derivatives*/
static bool linear_form(DiffEq *de, ast *f) {
	ast *symbols[DiffEq::max_order + 1];
	bool linear = true;

	derivatives_to_symbols(de, f, symbols);
	simplify(f, Simp::Basic);

	for (unsigned k = 0; k <= de->order; k++) {
		de->a[k] = f->copy();
		derivative(de->a[k], symbols[k], symbols[k]);
		simplify(de->a[k], Simp::Basic);

		for (unsigned j = 0; j <= de->order; j++)
			linear &= is_constant(de->a[k], symbols[j]);
	}

	if (linear) {
		de->g = negate(f->copy());

		for (unsigned k = 0; k <= de->order; k++) {
			ast *zero = ast::make(num::from(0));
			substitute(de->g, symbols[k], zero);
			ast::dispose(zero);
		}

		simplify(de->g, Simp::Basic);
	} else {
		for (unsigned k = 0; k <= de->order; k++) {
			ast::dispose(de->a[k]);
			de->a[k] = nullptr;
		}
	}

	for (unsigned k = 0; k <= de->order; k++)
		ast::dispose(symbols[k]);

	return linear;
}

Error DiffEq::load(const ast *entered, const ast *variable) {
	x = variable->copy();
	order = 0;
	linear = false;
	series = false;
	terms = default_terms;
	method = nullptr;
	nested = false;

	if (entered->isOp(Op::Equals))
		equation = entered->copy();
	else
		equation = ast::make(Op::Equals, entered->copy(), ast::make(num::from(0)));

	ast *function = nullptr;
	const Error err = find_function(equation, &function, &order);
	if (err != Error::Success)
		return err;
	if (function == nullptr)
		return Error::DeNoDerivative;
	if (function->compare(*variable))
		return Error::DeBadFunction;
	if (order > DiffEq::max_order)
		return Error::DeOrder;

	y = function->copy();
	canonical_SetFunction(function->symbol());

	work::step(work::Step::Type::Equation, nullptr, equation->childAt(0), equation->childAt(1));

	ast *f = difference(equation->childAt(0)->copy(), equation->childAt(1)->copy());

	work::pause();
	linear = linear_form(this, f);
	work::resume();

	ast::dispose(f);

	return Error::Success;
}

void DiffEq::classify() const {
	work::text(order_names[order - 1]);

	if (linear) {
		ast *standard = standardForm();
		work::step(work::Step::Type::Equation, "Linear", standard, g);
		ast::dispose(standard);
	} else {
		work::text("Nonlinear");
	}
}

DiffEq::~DiffEq() {
	clear();
}

void DiffEq::clear() {
	if (x == nullptr)
		return;

	ast::dispose(equation);
	ast::dispose(x);
	ast::dispose(y);
	ast::dispose(g);
	ast::dispose(known);
	ast::dispose(center);

	for (ast *&coefficient : a) {
		ast::dispose(coefficient);
		coefficient = nullptr;
	}

	for (unsigned k = 0; k < condition_count; k++) {
		ast::dispose(conditions[k].at);
		ast::dispose(conditions[k].value);
	}

	equation = x = y = g = known = center = nullptr;
	condition_count = 0;

	canonical_SetFunction(Sym::Invalid);
	canonical_SetSeries(nullptr);
}

ast *DiffEq::standardForm() const {
	ast *sum = ast::make(Op::Add);

	for (unsigned k = order + 1; k-- > 0;) {
		if (!a[k]->isInt(0))
			sum->appendChild(ast::make(Op::Mult, a[k]->copy(), derivative(y, k)));
	}

	work::pause();
	simplify(sum, Simp::Normalize | Simp::Commutative | Simp::Eval);
	work::resume();

	return sum;
}

/*Returns the number of primes if e is a derivative of y, or -1*/
static int derivative_order(const ast *e, const ast *y) {
	int order = 0;

	while (e->isOp(Op::Prime)) {
		e = e->firstChild();
		order++;
	}

	return e->compare(*y) ? order : -1;
}

Error DiffEq::loadList(ast **items, unsigned count, const ast *variable) {
	Error err = load(items[0], variable);

	for (unsigned i = 1; i < count && err == Error::Success; i++) {
		if (items[i]->isOp(Op::Equals) && items[i]->firstChild()->compare(*y))
			err = addKnownSolution(items[i]);
		else if (items[i]->isOp(Op::Equals) && items[i]->firstChild()->compare(*x))
			err = addCenter(items[i]);
		else
			err = addCondition(items[i]);
	}

	return err;
}

Error DiffEq::addCenter(const ast *point) {
	const ast *x0 = point->firstChild()->next();

	if (center != nullptr || involves(x0, x) || involves(x0, y))
		return Error::DeBadCondition;

	center = x0->copy();

	work::pause();
	simplify(center, Simp::Basic);
	work::resume();

	return Error::Success;
}

Error DiffEq::addKnownSolution(const ast *solution) {
	const ast *f = solution->firstChild()->next();

	if (known != nullptr || involves(f, y))
		return Error::DeBadCondition;

	known = f->copy();

	work::pause();
	simplify(known, Simp::Basic);
	work::resume();

	return Error::Success;
}

Error DiffEq::addCondition(const ast *condition) {
	if (condition_count == max_conditions || !condition->isOp(Op::Equals))
		return Error::DeBadCondition;

	/*Y(0) is parsed as Y*0*/
	const ast *left = condition->firstChild();
	if (!left->isOp(Op::Mult) || left->childCount() != 2)
		return Error::DeBadCondition;

	const int order = derivative_order(left->firstChild(), y);
	if (order < 0)
		return Error::DeBadCondition;

	Condition *c = &conditions[condition_count++];
	c->order = static_cast<unsigned>(order);
	c->at = left->firstChild()->next()->copy();
	c->value = left->next()->copy();

	work::pause();
	simplify(c->at, Simp::Basic);
	simplify(c->value, Simp::Basic);
	work::resume();

	return Error::Success;
}

bool is_euler(const ast *e) {
	return e->isSymbol() && e->symbol() == Sym::Euler;
}

ast *ln(ast *a) {
	return ast::make(Op::Log, ast::make(Sym::Euler), a);
}

ast *exponential(const ast *base, ast *e) {
	ast *result;

	if (is_euler(base) && e->isOp(Op::Log) && is_euler(e->firstChild())) {
		result = e->firstChild()->next()->copy();
	} else if (
		is_euler(base) && e->isOp(Op::Mult) && e->childCount() == 2 && e->firstChild()->isNumber() &&
		e->firstChild()->next()->isOp(Op::Log) && is_euler(e->firstChild()->next()->firstChild())
	) {
		result = ast::make(Op::Pow, e->firstChild()->next()->firstChild()->next()->copy(), e->firstChild()->copy());
	} else if (is_euler(base) && e->isOp(Op::Add)) {
		result = ast::make(Op::Mult);
		for (const ast *child : e->children())
			result->appendChild(exponential(base, child->copy()));
	} else {
		return ast::make(Op::Pow, base->copy(), e);
	}

	ast::dispose(e);
	return result;
}

/*Rewrites every power of e with exponential*/
static void split_exponentials(ast *e) {
	if (!e->isOperator())
		return;

	for (ast *child : e->children())
		split_exponentials(child);

	if (e->isOp(Op::Pow) && is_euler(e->firstChild()))
		e->replace(exponential(e->firstChild(), e->firstChild()->next()->copy()));
}

void rational_parts(const ast *e, ast **numer, ast **denom) {
	if (e->isOp(Op::Div)) {
		ast *n1, *d1, *n2, *d2;

		rational_parts(e->firstChild(), &n1, &d1);
		rational_parts(e->firstChild()->next(), &n2, &d2);

		*numer = ast::make(Op::Mult, n1, d2);
		*denom = ast::make(Op::Mult, d1, n2);
	} else if (e->isOp(Op::Mult)) {
		*numer = ast::make(Op::Mult);
		*denom = ast::make(Op::Mult);

		for (const ast *child : e->children()) {
			ast *n, *d;
			rational_parts(child, &n, &d);
			(*numer)->appendChild(n);
			(*denom)->appendChild(d);
		}
	} else if (e->isOp(Op::Add)) {
		ast *nums = ast::make(Op::Add), *dens = ast::make(Op::Mult);
		ast *distinct = ast::make(Op::Mult);

		for (const ast *child : e->children()) {
			ast *n, *d;
			rational_parts(child, &n, &d);
			nums->appendChild(n);
			dens->appendChild(d);

			const ast *other;
			for (other = distinct->firstChild(); other != nullptr && !other->compare(*d); other = other->next())
				;
			if (other == nullptr)
				distinct->appendChild(d->copy());
		}

		*numer = ast::make(Op::Add);

		for (const ast *n = nums->firstChild(), *d = dens->firstChild(); n != nullptr; n = n->next(), d = d->next()) {
			ast *term = ast::make(Op::Mult);

			term->appendChild(n->copy());
			bool skipped = false;
			for (const ast *other : distinct->children()) {
				if (!skipped && other->compare(*d))
					skipped = true;
				else
					term->appendChild(other->copy());
			}

			(*numer)->appendChild(term);
		}

		ast::dispose(nums);
		ast::dispose(dens);
		*denom = distinct;
	} else if (e->isOp(Op::Pow) && e->firstChild()->next()->isNumber()) {
		ast *n, *d;
		num *exponent = e->firstChild()->next()->num().copy();
		const bool negative = *exponent < 0;

		mp_rat_abs(exponent, exponent);
		rational_parts(e->firstChild(), &n, &d);

		*numer = ast::make(Op::Pow, negative ? d : n, ast::make(exponent->copy()));
		*denom = ast::make(Op::Pow, negative ? n : d, ast::make(exponent));
	} else {
		*numer = e->copy();
		*denom = ast::make(num::from(1));
	}
}

/*Rewrites each tan(u) in e as sin(u)/cos(u)*/
static void tangents_to_sines(ast *e) {
	if (!e->isOperator())
		return;

	for (ast *child : e->children())
		tangents_to_sines(child);

	if (e->isOp(Op::Tan))
		e->replace(
			ast::make(Op::Div, ast::make(Op::Sin, e->firstChild()->copy()), ast::make(Op::Cos, e->firstChild()->copy()))
		);
}

/*Returns how many terms e has once expanded, or more than limit if that is over limit*/
static unsigned long expanded_terms(const ast *e, unsigned long limit) {
	if (e->isOp(Op::Add) || e->isOp(Op::Mult)) {
		unsigned long terms = e->isOp(Op::Add) ? 0 : 1;

		for (const ast *child = e->firstChild(); child != nullptr && terms <= limit; child = child->next()) {
			const unsigned long base = expanded_terms(child, limit);
			terms = e->isOp(Op::Add) ? terms + base : terms * base;
		}

		return terms;
	}

	mp_small n;
	if (e->isOp(Op::Pow) && e->firstChild()->next()->isNumber() && e->firstChild()->next()->num().toInt(n) && n > 1) {
		const unsigned long base = expanded_terms(e->firstChild(), limit);
		unsigned long terms = 1;

		while (n-- > 0 && terms <= limit)
			terms *= base;

		return terms;
	}

	return 1;
}

/*Replaces each sin(u)^n with n at least 2 by (1 - cos(u)^2)sin(u)^(n - 2), returning whether it changed e*/
static bool reduce_sine_powers(ast *e) {
	bool changed = false;

	if (!e->isOperator())
		return false;

	for (ast *child : e->children())
		changed |= reduce_sine_powers(child);

	mp_small n;
	if (e->isOp(Op::Pow) && e->firstChild()->isOp(Op::Sin) && e->firstChild()->next()->isNumber() &&
		e->firstChild()->next()->num().toInt(n) && n >= 2) {
		const ast *sine = e->firstChild();
		e->replace(
			ast::make(
				Op::Mult,
				difference(integer(1), ast::make(Op::Pow, ast::make(Op::Cos, sine->firstChild()->copy()), integer(2))),
				ast::make(Op::Pow, sine->copy(), integer(n - 2))
			)
		);
		changed = true;
	}

	return changed;
}

/*True if the numerator of e over a common denominator expands to zero after simplifying e with flags, giving up when it would expand past MAX_EXPANDED_TERMS*/
static bool numerator_vanishes(const ast *e, Simp flags) {
	ast *copy = e->copy(), *numerator, *denominator;

	simplify(copy, flags);
	tangents_to_sines(copy);
	split_exponentials(copy);
	rational_parts(copy, &numerator, &denominator);

	if (expanded_terms(numerator, MAX_EXPANDED_TERMS) > MAX_EXPANDED_TERMS) {
		ast::dispose(copy);
		ast::dispose(numerator);
		ast::dispose(denominator);
		return false;
	}

	expand(numerator, Expand::All);
	simplify(numerator, Simp::Basic);
	expand(numerator, Expand::All);
	simplify(numerator, Simp::Basic);

	while (!numerator->isInt(0) && reduce_sine_powers(numerator) &&
		   expanded_terms(numerator, MAX_EXPANDED_TERMS) <= MAX_EXPANDED_TERMS) {
		expand(numerator, Expand::All);
		simplify(numerator, Simp::Basic);
	}

	if (!numerator->isInt(0)) {
		simplify_canonical_form(numerator, Canonical::CombinePowers);
		simplify(numerator, Simp::Basic);
	}

	const bool zero = numerator->isInt(0);

	ast::dispose(copy);
	ast::dispose(numerator);
	ast::dispose(denominator);

	return zero;
}

bool is_zero(const ast *e) {
	work::pause();
	const bool zero = numerator_vanishes(e, Simp::Basic) || numerator_vanishes(e, Simp::All);
	work::resume();

	return zero;
}

bool involves(const ast *e, const ast *v) {
	if (e->compare(*v))
		return true;

	if (e->isOperator()) {
		for (const ast *child : e->children()) {
			if (involves(child, v))
				return true;
		}
	}

	return false;
}

/*Replaces each derivative of y in e with its value in derivatives*/
static void substitute_derivatives(DiffEq *de, ast *e, ast **derivatives) {
	for (unsigned k = de->order + 1; k-- > 0;) {
		ast *d = DiffEq::derivative(de->y, k);
		substitute(e, d, derivatives[k]);
		ast::dispose(d);
	}
}

/*Substitutes the derivatives into one side of the equation and simplifies it, recording each form*/
static ast *evaluate_side(DiffEq *de, const ast *side, ast **derivatives, const char *label) {
	ast *e = side->copy();

	if (!involves(e, de->y))
		return e;

	work::text(label);
	work::step(work::Step::Type::State, nullptr, nullptr, e);

	work::pause();
	substitute_derivatives(de, e, derivatives);
	work::resume();

	work::step(work::Step::Type::State, nullptr, nullptr, e);
	simplify(e, Simp::Basic);

	return e;
}

static bool check_condition(DiffEq *de, DiffEq::Condition *c, ast **derivatives) {
	ast *at = ast::make(Op::At, DiffEq::derivative(de->y, c->order), c->at->copy());
	ast *substituted = derivatives[c->order]->copy();

	work::pause();
	substitute(substituted, de->x, c->at);
	ast *value = substituted->copy();
	simplify(value, Simp::Basic);
	if (!value->compare(*c->value))
		simplify(value, Simp::All);
	work::resume();

	ast *chain = difference(value->copy(), c->value->copy());
	const bool holds = is_zero(chain);
	ast::dispose(chain);

	chain = ast::make(Op::Equals, substituted, value);
	work::step(work::Step::Type::Equation, holds ? "Initial condition holds" : "Initial condition fails", at, chain);

	ast::dispose(at);
	ast::dispose(chain);

	return holds;
}

void expand_if_smaller(ast *e) {
	ast *expanded = e->copy();

	expand(expanded, Expand::All);
	simplify(expanded, Simp::Basic);

	if (node_count(expanded) < node_count(e))
		e->replace(expanded);
	else
		ast::dispose(expanded);
}

ast *derivative_node(ast *e, const ast *v) {
	ast *node = ast::make(Op::Deriv);

	node->appendChild(e);
	node->appendChild(v->copy());
	node->appendChild(v->copy());

	return node;
}

/*Replaces each |u| in e with u*/
static void drop_absolute_values(ast *e) {
	if (!e->isOperator())
		return;

	for (ast *child : e->children())
		drop_absolute_values(child);

	if (e->isOp(Op::Abs))
		e->replace(e->firstChild()->copy());
}

ast *exponential_of_integral(ast *P, const ast *v) {
	ast *G = ast::make(Op::Integral, P, v->copy());

	eval_integrals(G);

	if (contains_integral(G)) {
		ast::dispose(G);
		return nullptr;
	}

	work::pause();
	simplify(G, Simp::Basic);
	ast *power = ast::make(Op::Pow, ast::make(Sym::Euler), G->copy());
	drop_absolute_values(G);
	ast *mu = exponential(power->firstChild(), G);
	simplify(mu, Simp::Basic);
	G = power->copy();
	simplify(G, Simp::Basic);
	work::resume();

	if (G->compare(*mu))
		work::step(work::Step::Type::State, nullptr, nullptr, mu);
	else
		work::step(work::Step::Type::Equation, nullptr, power, mu);
	ast::dispose(power);
	ast::dispose(G);

	return mu;
}

ast *tidy(ast *e) {
	work::pause();
	simplify(e, Simp::Normalize | Simp::Commutative | Simp::Eval);
	work::resume();

	return e;
}

void single_fraction(ast *e) {
	ast *numerator, *denominator;

	work::pause();
	simplify(e, Simp::Basic);
	rational_parts(e, &numerator, &denominator);
	expand(numerator, Expand::All);
	e->replace(ast::make(Op::Div, numerator, denominator));
	simplify(e, Simp::Basic);

	ast *cancelled = e->copy();
	factor_cancel(cancelled);
	work::resume();

	if (e->isOp(Op::Div) && cancelled->isOp(Op::Div) &&
		e->firstChild()->next()->compare(*cancelled->firstChild()->next()))
		ast::dispose(cancelled);
	else
		e->replace(cancelled);
}

ast *substitution_symbol(const DiffEq *de, Sym preferred) {
	ast *scope = ast::make(Op::Add, de->equation->copy(), de->x->copy());
	const Sym symbol = contains_symbol(scope, preferred) ? fresh_symbol(scope) : preferred;

	ast::dispose(scope);

	return ast::make(static_cast<Sym>(symbol));
}

Error check_solution(DiffEq *de, const ast *solution, const char *text, bool conditions, bool *satisfied) {
	const ast *f = solution;

	if (f->isOp(Op::Equals) && f->firstChild()->compare(*de->y))
		f = f->firstChild()->next();

	if (involves(f, de->y))
		return Error::DeImplicit;

	work::step(work::Step::Type::Equation, text, de->y, f);

	ast *derivatives[DiffEq::max_order + 1];
	derivatives[0] = f->copy();

	work::pause();
	simplify(derivatives[0], Simp::Normalize);
	work::resume();

	for (unsigned k = 1; k <= de->order; k++) {
		ast *d = derivatives[k - 1]->copy();
		ast *prime = DiffEq::derivative(de->y, k);

		work::pause();
		derivative(d, de->x, de->x);
		simplify(d, Simp::Basic);
		expand_if_smaller(d);
		work::resume();

		ast *chain = ast::make(Op::Equals, derivative_node(derivatives[k - 1]->copy(), de->x), d->copy());

		work::step(work::Step::Type::Equation, k == 1 ? "Differentiate" : nullptr, prime, chain);

		ast::dispose(prime);
		ast::dispose(chain);

		derivatives[k] = d;
	}

	ast *left = evaluate_side(de, de->equation->firstChild(), derivatives, "Left side");
	ast *right = evaluate_side(de, de->equation->firstChild()->next(), derivatives, "Right side");

	ast *remainder = difference(left->copy(), right->copy());
	*satisfied = is_zero(remainder);
	ast::dispose(remainder);

	work::step(
		work::Step::Type::Equation, *satisfied ? "Satisfies the equation" : "Does not satisfy the equation", left, right
	);

	for (unsigned k = 0; conditions && k < de->condition_count; k++)
		*satisfied &= check_condition(de, &de->conditions[k], derivatives);

	work::text(*satisfied ? "It is a solution" : "It is not a solution");

	for (unsigned k = 0; k <= de->order; k++)
		ast::dispose(derivatives[k]);
	ast::dispose(left);
	ast::dispose(right);

	return Error::Success;
}

Error DiffEq::verify(const ast *solution, bool *satisfied) {
	return check_solution(this, solution, "Solution", true, satisfied);
}

Error DiffEq::solve(ast **solution) {
	*solution = nullptr;

	if (condition_count > order)
		return Error::DeBadCondition;

	for (unsigned k = 0; k < condition_count; k++) {
		if (conditions[k].order >= order)
			return Error::DeBadCondition;
	}

	if (series)
		return solve_power_series(this, solution);

	if (order == 1)
		return solve_first_order(this, solution);

	if (known != nullptr)
		return solve_reduction_of_order(this, solution);

	Error err = solve_constant_coefficients(this, solution);
	if (err != Error::DeUnsolved || !linear)
		return err;

	err = solve_power_series(this, solution);

	return err == Error::DeSingular ? Error::DeUnsolved : err;
}
