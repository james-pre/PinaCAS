#include "cas.hxx"

#include "de/internal.hxx"

#include "../work.hxx"

/*Simplifies without expanding*/
static ast *settle(ast *e) {
	work::pause();
	simplify(*e, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval);
	work::resume();
	return e;
}

static ast *number(const num &n) {
	return ast::make(n.copy());
}

/*Sets slope and intercept so that u is slope v + intercept and returns true if u is linear in v*/
static bool linear_parts(const ast &u, const ast &v, ast **slope, ast **intercept) {
	ast *zero = integer(0);

	*slope = u.copy();
	*intercept = u.copy();

	work::pause();
	derivative(**slope, v, v);
	simplify(**slope, Simp::Basic);
	substitute(**intercept, v, *zero);
	simplify(**intercept, Simp::Basic);
	work::resume();

	ast::dispose(zero);

	if (!involves(**slope, v) && !involves(**intercept, v))
		return true;

	ast::dispose(*slope);
	ast::dispose(*intercept);
	return false;
}

/*Returns a if the argument of the unit step or delta function e is a positive multiple of t - a, otherwise nullptr*/
static ast *shift_of(const ast &e, const ast &t) {
	ast *u = e.isOp(Op::Step) ? difference(e.childAt(0)->copy(), e.childAt(1)->copy()) : e.childAt(0)->copy();
	ast *slope, *intercept, *a = nullptr;

	if (linear_parts(*u, t, &slope, &intercept)) {
		num *value = rational_value(*slope);

		if (value != nullptr && *value > 0)
			a = settle(negate(ast::make(Op::Div, intercept->copy(), slope->copy())));

		num::dispose(value);
		ast::dispose(slope);
		ast::dispose(intercept);
	}

	ast::dispose(u);
	return a;
}

static ast *unit_step(const ast &t, const ast &a) {
	return ast::make(Op::Step, t.copy(), a.copy());
}

/*Rewrites U(u) at the start of each factor pair U and u in e, with u linear in t, as the unit step*/
static void steps_from_symbols(ast &e, const ast &t) {
	if (!e.isOperator())
		return;

	for (ast &child : e.children())
		steps_from_symbols(child, t);

	if (!e.isOp(Op::Mult))
		return;

	for (ast *child = e.firstChild(); child != nullptr && child->next() != nullptr;) {
		ast *argument = child->next();

		if (!child->isSymbol() || child->symbol() != Sym::U || !involves(*argument, t)) {
			child = argument;
			continue;
		}

		ast *step = ast::make(Op::Step, argument->copy(), integer(0));
		ast *a = shift_of(*step, t);
		ast::dispose(step);

		if (a == nullptr) {
			child = argument;
			continue;
		}

		ast::dispose(e.removeChild(*argument));
		child->replace(unit_step(t, *a));
		ast::dispose(a);
		child = child->next();
	}
}

/*Rewrites each unit step in e as (t ≥ a)*/
static void normalize_steps(ast &e, const ast &t) {
	if (!e.isOperator())
		return;

	for (ast &child : e.children())
		normalize_steps(child, t);

	if (e.isOp(Op::Step)) {
		ast *a = shift_of(e, t);
		if (a != nullptr)
			e.replace(unit_step(t, *a));
		ast::dispose(a);
	}
}

/*Flattens nested products in e, keeping the order of the factors*/
static void flatten_products(ast &e) {
	if (!e.isOperator())
		return;

	for (ast &child : e.children())
		flatten_products(child);

	if (!e.isOp(Op::Mult))
		return;

	for (unsigned i = 0; i < e.childCount();) {
		if (!e.childAt(i)->isOp(Op::Mult)) {
			i++;
			continue;
		}

		ast *product = e.removeChildAt(i);
		for (unsigned j = i; product->childCount() > 0; j++)
			e.insertChild(product->removeChildAt(0), j);
		ast::dispose(product);
	}
}

void laplace_normalize(ast &e, const ast &t) {
	flatten_products(e);
	steps_from_symbols(e, t);
	normalize_steps(e, t);
}

/*The unknown function while solving an equation and the symbol standing for its transform, or nullptr*/
static const ast *unknown = nullptr, *unknown_transform = nullptr;
/*Values of the unknown function and its derivatives at 0 while solving, or nullptr where not given*/
static ast *initial_values[DiffEq::max_order];

/*Returns a copy of e with the symbol standing for the transform of the unknown written as its transform*/
static ast *shown(const ast &e) {
	ast *copy = e.copy();

	if (unknown_transform != nullptr) {
		ast *transform = ast::make(Op::Laplace, unknown->copy());
		work::pause();
		substitute(*copy, *unknown_transform, *transform);
		work::resume();
		ast::dispose(transform);
	}

	return copy;
}

static void record(const char *text, Op op, const ast &before, const ast &after) {
	ast *transform = ast::make(op, before.copy()), *right = shown(after);
	work::step(work::Step::Type::Equation, text, transform, right);
	ast::dispose(transform);
	ast::dispose(right);
}

/*True if e involves v or the unknown function*/
static bool depends(const ast &e, const ast &v) {
	return involves(e, v) || (unknown != nullptr && involves(e, *unknown));
}

/*Splits the product e into its factors that do not involve v and the rest*/
/*Appends the factors of e, or of 1/e when inverted, to constant when they do not involve v and to rest otherwise, writing 1/e^u as e^(-u)*/
static void collect(const ast &e, const ast &v, bool inverted, ast &constant, ast &rest) {
	if (e.isOp(Op::Mult)) {
		for (const ast &factor : e.children())
			collect(factor, v, inverted, constant, rest);
	} else if (e.isOp(Op::Div)) {
		collect(*e.childAt(0), v, inverted, constant, rest);
		collect(*e.childAt(1), v, !inverted, constant, rest);
	} else if (!depends(e, v)) {
		constant.appendChild(inverted ? ast::make(Op::Div, integer(1), e.copy()) : e.copy());
	} else if (!inverted) {
		rest.appendChild(e.copy());
	} else if (e.isOp(Op::Pow) && is_euler(*e.childAt(0))) {
		rest.appendChild(ast::make(Op::Pow, ast::make(Sym::Euler), negate(e.childAt(1)->copy())));
	} else {
		rest.appendChild(ast::make(Op::Pow, e.copy(), integer(-1)));
	}
}

/*Splits the product e into its factors that do not involve v and the rest*/
static void separate(const ast &e, const ast &v, ast **constant, ast **rest) {
	*constant = ast::make(Op::Mult, integer(1), integer(1));
	*rest = ast::make(Op::Mult);

	collect(e, v, false, **constant, **rest);
	settle(*constant);

	if ((*rest)->childCount() == 0) {
		(*rest)->replace(integer(1));
	} else if ((*rest)->childCount() == 1) {
		ast *only = (*rest)->removeChildAt(0);
		ast::dispose(*rest);
		*rest = only;
	}
}

static ast *transform_sum(const ast &f, const ast &t, const ast &s);

/*Transforms g δ(t - a) as g(a) e^(-as)*/
static ast *transform_delta(const ast &g, const ast &delta, const ast &t, const ast &s) {
	ast *a = shift_of(delta, t);
	if (a == nullptr)
		return nullptr;

	ast *rest = g.copy();
	if (rest->isOp(Op::Mult)) {
		for (ast &factor : rest->children()) {
			if (factor.compare(delta)) {
				factor.replace(integer(1));
				break;
			}
		}
	} else {
		rest->replace(integer(1));
	}

	work::pause();
	substitute(*rest, t, *a);
	simplify(*rest, Simp::Basic);
	work::resume();

	ast *F = settle(
		ast::make(Op::Mult, rest, ast::make(Op::Pow, ast::make(Sym::Euler), negate(ast::make(Op::Mult, a, s.copy()))))
	);
	record("Delta function", Op::Laplace, g, *F);

	return F;
}

/*Rewrites each sin, cos, sinh and cosh in e of kt + c with c not zero by the addition formulas. Returns true if any changed.*/
static bool add_angles(ast &e, const ast &t) {
	bool changed = false;

	if (!e.isOperator())
		return false;

	for (ast &child : e.children())
		changed |= add_angles(child, t);

	const bool circular = e.isOp(Op::Sin) || e.isOp(Op::Cos);
	if (!circular && !e.isOp(Op::SinH) && !e.isOp(Op::CosH))
		return changed;

	ast *slope, *intercept;
	if (!linear_parts(*e.firstChild(), t, &slope, &intercept))
		return changed;

	if (intercept->isInt(0)) {
		ast::dispose(slope);
		ast::dispose(intercept);
		return changed;
	}

	const Op sine = circular ? Op::Sin : Op::SinH, cosine = circular ? Op::Cos : Op::CosH;
	ast *angle = ast::make(Op::Mult, slope, t.copy());
	ast *first, *second;

	if (e.isOp(sine)) {
		first = ast::make(Op::Mult, ast::make(sine, angle->copy()), ast::make(cosine, intercept->copy()));
		second = ast::make(Op::Mult, ast::make(cosine, angle), ast::make(sine, intercept));
	} else {
		first = ast::make(Op::Mult, ast::make(cosine, angle->copy()), ast::make(cosine, intercept->copy()));
		second = ast::make(Op::Mult, ast::make(sine, angle), ast::make(sine, intercept));
		if (circular)
			second = negate(second);
	}

	e.replace(ast::make(Op::Add, first, second));
	return true;
}

/*Transforms g(t)U(t - a) as e^(-as) times the transform of g(t + a)*/
static ast *transform_shifted(const ast &g, const ast &step, const ast &t, const ast &s) {
	ast *a = shift_of(step, t);
	if (a == nullptr)
		return nullptr;

	ast *rest = g.copy();
	for (ast &factor : rest->children()) {
		if (factor.compare(step)) {
			factor.replace(integer(1));
			break;
		}
	}

	ast *moved = ast::make(Op::Add, t.copy(), a->copy());
	work::pause();
	substitute(*rest, t, *moved);
	simplify(*rest, Simp::Basic);
	if (add_angles(*rest, t)) {
		expand(*rest, Expand::All);
		simplify(*rest, Simp::All);
	}
	work::resume();
	ast::dispose(moved);

	ast *exponential = ast::make(Op::Pow, ast::make(Sym::Euler), negate(ast::make(Op::Mult, a->copy(), s.copy())));
	settle(exponential);
	ast::dispose(a);

	ast *shifted = ast::make(Op::Mult, exponential->copy(), ast::make(Op::Laplace, rest->copy()));
	record("Second translation theorem", Op::Laplace, g, *shifted);
	ast::dispose(shifted);

	ast *F = transform_sum(*rest, t, s);
	ast::dispose(rest);

	if (F == nullptr) {
		ast::dispose(exponential);
		return nullptr;
	}

	return settle(ast::make(Op::Mult, exponential, F));
}

/*The factors t^power e^(rate t) trig(frequency t) of a term, times scale*/
struct Shape {
	unsigned power = 0;
	ast *rate = nullptr, *scale = nullptr, *frequency = nullptr;
	const ast *trig = nullptr;

	Shape() = default;
	Shape(const Shape &) = delete;
	Shape &operator=(const Shape &) = delete;
	~Shape() {
		ast::dispose(rate);
		ast::dispose(scale);
		ast::dispose(frequency);
	}
};

/*Fills shape from the factors of g. Returns false if a factor is not in the table.*/
static bool analyze(const ast &g, const ast &t, Shape &shape) {
	shape.rate = ast::make(Op::Add, integer(0));
	shape.scale = ast::make(Op::Mult, integer(1));

	const unsigned count = g.isOp(Op::Mult) ? g.childCount() : 1;

	for (unsigned i = 0; i < count; i++) {
		const ast &factor = g.isOp(Op::Mult) ? *g.childAt(i) : g;
		ast *slope, *intercept;
		num *exponent;
		mp_small value;

		if (!involves(factor, t)) {
			shape.scale->appendChild(factor.copy());
		} else if (factor.compare(t)) {
			shape.power++;
		} else if (
			factor.isOp(Op::Pow) && factor.childAt(0)->compare(t) &&
			(exponent = rational_value(*factor.childAt(1))) != nullptr
		) {
			const bool natural = exponent->toInt(value) && value >= 0;
			num::dispose(exponent);
			if (!natural)
				return false;
			shape.power += static_cast<unsigned>(value);
		} else if (
			factor.isOp(Op::Pow) && is_euler(*factor.childAt(0)) &&
			linear_parts(*factor.childAt(1), t, &slope, &intercept)
		) {
			shape.rate->appendChild(slope);
			shape.scale->appendChild(ast::make(Op::Pow, ast::make(Sym::Euler), intercept));
		} else if (
			shape.trig == nullptr &&
			(factor.isOp(Op::Sin) || factor.isOp(Op::Cos) || factor.isOp(Op::SinH) || factor.isOp(Op::CosH))
		) {
			shape.trig = &factor;
			if (!linear_parts(*factor.firstChild(), t, &shape.frequency, &intercept))
				return false;
			const bool through_zero = intercept->isInt(0);
			ast::dispose(intercept);
			if (!through_zero)
				return false;
		} else {
			return false;
		}
	}

	settle(shape.rate);
	settle(shape.scale);

	return true;
}

/*Returns the transform of t^power trig(frequency t), recording the work*/
static ast *base_transform(const Shape &shape, const ast &t, const ast &s, ast **base) {
	ast *power = shape.power == 0 ? integer(1) : ast::make(Op::Pow, t.copy(), integer(shape.power));
	ast *G;

	if (shape.trig == nullptr) {
		num factorial(1), index;
		for (unsigned k = 2; k <= shape.power; k++) {
			mp_rat_set_value(&index, static_cast<mp_small>(k), 1);
			factorial *= index;
		}
		*base = settle(power);
		return settle(ast::make(Op::Div, number(factorial), ast::make(Op::Pow, s.copy(), integer(shape.power + 1))));
	}

	const bool hyperbolic = shape.trig->isOp(Op::SinH) || shape.trig->isOp(Op::CosH);
	ast *square = ast::make(Op::Pow, shape.frequency->copy(), integer(2));
	ast *quadratic =
		settle(ast::make(Op::Add, ast::make(Op::Pow, s.copy(), integer(2)), hyperbolic ? negate(square) : square));
	ast *numerator = shape.trig->isOp(Op::Sin) || shape.trig->isOp(Op::SinH) ? shape.frequency->copy() : s.copy();
	G = settle(ast::make(Op::Div, numerator->copy(), quadratic->copy()));

	*base = settle(ast::make(Op::Mult, power, shape.trig->copy()));

	if (shape.power == 0) {
		ast::dispose(numerator);
		ast::dispose(quadratic);
		return G;
	}

	record("Table", Op::Laplace, *shape.trig, *G);

	ast *shown = G;
	for (unsigned k = 0; k < shape.power; k++)
		shown = derivative_node(shown, s);
	if (shape.power % 2 == 1)
		shown = negate(shown);

	work::pause();
	for (unsigned m = 1; m <= shape.power; m++) {
		ast *slope = numerator->copy();
		derivative(*slope, s, s);
		numerator = difference(
			ast::make(Op::Mult, slope, quadratic->copy()),
			ast::make(Op::Mult, integer(2 * m), ast::make(Op::Mult, numerator, s.copy()))
		);
		expand(*numerator, Expand::All);
		simplify(*numerator, Simp::Basic);
	}
	if (shape.power % 2 == 1)
		numerator = negate(numerator);
	factor(*numerator, Factor::SimpleAdditionEvaluateable | Factor::SimpleAdditionNonevaluateable);
	simplify(*numerator, Simp::Normalize | Simp::Commutative | Simp::Eval);
	work::resume();

	G = settle(ast::make(Op::Div, numerator, ast::make(Op::Pow, quadratic, integer(shape.power + 1))));

	ast *chain = ast::make(Op::Equals, shown, G->copy());
	record("Derivatives of transforms", Op::Laplace, **base, *chain);
	ast::dispose(chain);

	return G;
}

/*Returns the order of the derivative of y that e is, or -1 if it is not one*/
static int prime_order(const ast *e, const ast &y) {
	int order = 0;

	for (; e->isOp(Op::Prime); e = e->firstChild())
		order++;

	return e->compare(y) ? order : -1;
}

/*Transforms the unknown function or one of its derivatives, using its initial values*/
static ast *transform_unknown(const ast &g, const ast &t, const ast &s) {
	const ast *base = g.isOp(Op::At) && g.childAt(1)->compare(t) ? g.childAt(0) : &g;
	const int order = prime_order(base, *unknown);

	if (order < 0)
		return nullptr;

	ast *power = order == 1 ? s.copy() : ast::make(Op::Pow, s.copy(), integer(order));
	ast *F = ast::make(Op::Add, ast::make(Op::Mult, power, unknown_transform->copy()));
	ast *general = F->copy();

	for (int k = 0; k < order; k++) {
		if (initial_values[k] == nullptr) {
			ast::dispose(F);
			ast::dispose(general);
			return nullptr;
		}

		const int exponent = order - 1 - k;
		ast *power = exponent == 0   ? integer(1)
					 : exponent == 1 ? s.copy()
									 : ast::make(Op::Pow, s.copy(), integer(exponent));
		ast *value = ast::make(Op::At, DiffEq::derivative(*unknown, static_cast<unsigned>(k)), integer(0));
		general->appendChild(negate(ast::make(Op::Mult, power->copy(), value)));
		F->appendChild(negate(ast::make(Op::Mult, power, initial_values[k]->copy())));
	}

	settle(F);
	if (order > 0) {
		work::pause();
		simplify(*general, Simp::Commutative);
		work::resume();
		ast *chain = ast::make(Op::Equals, general, F->copy());
		record("Transform of a derivative", Op::Laplace, g, *chain);
		ast::dispose(chain);
	} else {
		ast::dispose(general);
	}

	return F;
}

static bool is_convolution(const ast &e, const ast &t) {
	return e.isOp(Op::Integral) && e.childCount() == 4 && e.childAt(1)->isSymbol() && e.childAt(2)->isInt(0) &&
		   e.childAt(3)->compare(t);
}

/*Transforms the integral from 0 to t of f(tau)g(t - tau) as F(s)G(s)*/
static ast *transform_convolution(const ast &integral, const ast &t, const ast &s) {
	const ast &tau = *integral.childAt(1);
	ast *constant = ast::make(Op::Mult, integer(1), integer(1)), *factors = ast::make(Op::Mult);
	ast *f = ast::make(Op::Mult, integer(1)), *g = ast::make(Op::Mult, integer(1));
	ast *moved = ast::make(Op::Add, t.copy(), tau.copy());

	collect(*integral.childAt(0), tau, false, *constant, *factors);
	bool split = !involves(*constant, t);

	for (const ast &factor : factors->children()) {
		if (!involves(factor, t)) {
			f->appendChild(factor.copy());
			continue;
		}

		ast *shifted = factor.copy();
		work::pause();
		substitute(*shifted, t, *moved);
		simplify(*shifted, Simp::Basic);
		work::resume();

		split &= !involves(*shifted, tau);
		g->appendChild(shifted);
	}

	work::pause();
	substitute(*f, tau, t);
	work::resume();
	settle(f);
	settle(g);
	settle(constant);
	ast::dispose(factors);
	ast::dispose(moved);

	ast *F = nullptr;

	if (split) {
		ast *product = ast::make(Op::Mult, constant->copy(), ast::make(Op::Laplace, f->copy()));
		product->appendChild(ast::make(Op::Laplace, g->copy()));
		record("Convolution theorem", Op::Laplace, integral, *settle(product));
		ast::dispose(product);

		ast *first = transform_sum(*f, t, s);
		ast *second = first != nullptr ? transform_sum(*g, t, s) : nullptr;

		if (second != nullptr) {
			F = ast::make(Op::Mult, constant->copy(), first);
			F->appendChild(second);
			settle(F);
		} else {
			ast::dispose(first);
		}
	}

	ast::dispose(constant);
	ast::dispose(f);
	ast::dispose(g);

	return F;
}

/*Returns the transform of g, a product of factors that each involve t, recording the work, or nullptr*/
static ast *transform_term(const ast &g, const ast &t, const ast &s) {
	const unsigned count = g.isOp(Op::Mult) ? g.childCount() : 1;

	if (is_convolution(g, t))
		return transform_convolution(g, t, s);

	if (unknown != nullptr && involves(g, *unknown))
		return transform_unknown(g, t, s);

	for (unsigned i = 0; i < count; i++) {
		const ast &factor = g.isOp(Op::Mult) ? *g.childAt(i) : g;

		if (factor.isOp(Op::Delta))
			return transform_delta(g, factor, t, s);
		if (factor.isOp(Op::Step) && g.isOp(Op::Mult))
			return transform_shifted(g, factor, t, s);
		if (factor.isOp(Op::Step)) {
			ast *product = ast::make(Op::Mult, g.copy(), integer(1));
			ast *F = transform_shifted(*product, *product->firstChild(), t, s);
			ast::dispose(product);
			return F;
		}
	}

	Shape shape;
	if (!analyze(g, t, shape))
		return nullptr;

	ast *base;
	ast *G = base_transform(shape, t, s, &base);
	const bool recorded = shape.trig != nullptr && shape.power > 0;

	if (!shape.rate->isInt(0)) {
		if (!recorded)
			record("Table", Op::Laplace, *base, *G);

		ast *moved = difference(s.copy(), shape.rate->copy());
		work::pause();
		substitute(*G, s, *moved);
		work::resume();
		ast::dispose(moved);
		settle(G);
	}

	G = settle(ast::make(Op::Mult, shape.scale->copy(), G));

	if (!shape.rate->isInt(0))
		record("First translation theorem", Op::Laplace, g, *G);
	else if (!recorded)
		record("Table", Op::Laplace, g, *G);

	ast::dispose(base);
	return G;
}

/*True if e is a unit step, delta function, or integral, or a product with one as a factor*/
static bool has_impulse_or_step(const ast &e) {
	if (e.isOp(Op::Step) || e.isOp(Op::Delta) || e.isOp(Op::Integral))
		return true;

	if (e.isOp(Op::Mult) || e.isOp(Op::Div)) {
		for (const ast &factor : e.children()) {
			if (has_impulse_or_step(factor))
				return true;
		}
	}

	return false;
}

static ast *transform_sum(const ast &f, const ast &t, const ast &s) {
	ast *g = f.copy();

	work::pause();
	simplify(*g, Simp::Basic);
	if (!g->isOp(Op::Add))
		g = ast::make(Op::Add, g);
	for (ast &term : g->children()) {
		if (!has_impulse_or_step(term)) {
			expand(term, Expand::All);
			simplify(term, Simp::Basic);
		}
	}
	simplify(*g, Simp::Commutative);
	work::resume();

	const unsigned count = g->isOp(Op::Add) ? g->childCount() : 1;
	ast *constants[PartialFractions::max_degree * 2], *rests[PartialFractions::max_degree * 2];

	if (count > PartialFractions::max_degree * 2) {
		ast::dispose(g);
		return nullptr;
	}

	ast *linear = ast::make(Op::Add);
	for (unsigned i = 0; i < count; i++) {
		separate(g->isOp(Op::Add) ? *g->childAt(i) : *g, t, &constants[i], &rests[i]);
		linear->appendChild(ast::make(Op::Mult, constants[i]->copy(), ast::make(Op::Laplace, rests[i]->copy())));
	}

	if (count > 1 || !constants[0]->isInt(1))
		record("Linearity", Op::Laplace, f, *settle(linear));
	ast::dispose(linear);

	ast *F = ast::make(Op::Add);
	for (unsigned i = 0; i < count && F != nullptr; i++) {
		ast *G = transform_term(*rests[i], t, s);

		if (G == nullptr) {
			ast::dispose(F);
			F = nullptr;
		} else {
			F->appendChild(ast::make(Op::Mult, constants[i]->copy(), G));
		}
	}

	for (unsigned i = 0; i < count; i++) {
		ast::dispose(constants[i]);
		ast::dispose(rests[i]);
	}
	ast::dispose(g);

	return F != nullptr ? settle(F) : nullptr;
}

ast *laplace_transform(const ast &f, const ast &t, const ast &s) {
	ast *F = transform_sum(f, t, s);

	if (F != nullptr)
		record(nullptr, Op::Laplace, f, *F);

	return F;
}

/*Returns the square root of the absolute value of q*/
static ast *root_of(const num &q) {
	num magnitude = q;
	mp_rat_abs(&magnitude, &magnitude);

	ast *root = ast::make(Op::Pow, number(magnitude), ast::make(num::from(1, 2)));
	work::pause();
	simplify(*root, Simp::Basic);
	work::resume();
	return root;
}

/*Returns the inverse transform of one partial fraction, recording the work, or nullptr*/
static ast *inverse_term(const PartialFractions::Term &term, const ast &s, const ast &t) {
	const num one(1), two(2);
	num scale(1);

	for (unsigned k = 0; k < term.power; k++)
		scale *= *term.divisor[term.degree];

	num slope = *term.numerator[1] / scale, constant = *term.numerator[0] / scale;

	ast *divisor = polynomial(term.divisor, term.degree, s, one);
	if (term.power > 1)
		divisor = ast::make(Op::Pow, divisor, integer(term.power));
	ast *fraction = settle(ast::make(Op::Div, polynomial(term.numerator, 1, s, one), divisor));
	ast *f = nullptr;

	if (term.degree == 1) {
		num r = -*term.divisor[0] / *term.divisor[1], factorial(1), index;
		for (unsigned k = 2; k < term.power; k++) {
			mp_rat_set_value(&index, static_cast<mp_small>(k), 1);
			factorial *= index;
		}
		constant /= factorial;

		f = ast::make(Op::Mult, number(constant), ast::make(Op::Pow, t.copy(), integer(term.power - 1)));
		f->appendChild(ast::make(Op::Pow, ast::make(Sym::Euler), ast::make(Op::Mult, number(r), t.copy())));
	} else {
		const num &a = *term.divisor[2];
		num h = *term.divisor[1] / a / two;
		num square = *term.divisor[0] / a - h * h;
		num remainder = constant - slope * h;

		if (h != 0) {
			ast *shifted = ast::make(Op::Add, s.copy(), number(h));
			ast *completed = ast::make(Op::Add, ast::make(Op::Pow, shifted->copy(), integer(2)), number(square));
			if (term.power > 1)
				completed = ast::make(Op::Pow, completed, integer(term.power));
			ast *rewritten = ast::make(Op::Add);
			if (slope != 0)
				rewritten->appendChild(
					settle(ast::make(Op::Div, ast::make(Op::Mult, number(slope), shifted->copy()), completed->copy()))
				);
			if (remainder != 0)
				rewritten->appendChild(settle(ast::make(Op::Div, number(remainder), completed->copy())));
			ast::dispose(shifted);
			ast::dispose(completed);
			work::pause();
			simplify(*rewritten, Simp::Commutative);
			work::resume();
			work::step(work::Step::Type::Equation, "Complete the square", fraction, rewritten);
			ast::dispose(rewritten);
		}

		ast *k = root_of(square);
		ast *kt = ast::make(Op::Mult, k->copy(), t.copy());
		ast *damping = ast::make(Op::Pow, ast::make(Sym::Euler), ast::make(Op::Mult, number(-h), t.copy()));
		const bool oscillating = square > 0;

		if (term.power == 1) {
			ast *sum = ast::make(Op::Mult, number(slope), ast::make(oscillating ? Op::Cos : Op::CosH, kt->copy()));
			ast *second = ast::make(
				Op::Mult,
				ast::make(Op::Div, number(remainder), k->copy()),
				ast::make(oscillating ? Op::Sin : Op::SinH, kt->copy())
			);
			f = ast::make(Op::Mult, damping, ast::make(Op::Add, sum, second));
		} else if (term.power == 2 && oscillating) {
			ast *first = ast::make(
				Op::Div,
				ast::make(Op::Mult, number(slope), ast::make(Op::Mult, t.copy(), ast::make(Op::Sin, kt->copy()))),
				ast::make(Op::Mult, integer(2), k->copy())
			);
			ast *second = ast::make(
				Op::Div,
				ast::make(
					Op::Mult,
					number(remainder),
					difference(
						ast::make(Op::Sin, kt->copy()), ast::make(Op::Mult, kt->copy(), ast::make(Op::Cos, kt->copy()))
					)
				),
				ast::make(Op::Mult, integer(2), ast::make(Op::Pow, k->copy(), integer(3)))
			);
			f = ast::make(Op::Mult, damping, ast::make(Op::Add, first, second));
		} else {
			ast::dispose(damping);
		}

		ast::dispose(k);
		ast::dispose(kt);
	}

	if (f != nullptr) {
		work::pause();
		simplify(*f, Simp::Basic);
		work::resume();
		record(nullptr, Op::LaplaceInverse, *fraction, *f);
	}

	ast::dispose(fraction);
	return f;
}

/*Returns the inverse transform of R, a quotient of polynomials in s, recording the work, or nullptr. Sets whole if the work ends with the inverse transform of R itself.*/
static ast *inverse_rational(const ast &R, const ast &s, const ast &t, bool *whole) {
	PartialFractions fractions;

	if (!split_fractions(R, s, fractions) || fractions.quotient_degree >= 0)
		return nullptr;

	ast *f = ast::make(Op::Add);
	for (unsigned i = 0; i < fractions.count && f != nullptr; i++) {
		if (*fractions.terms[i].numerator[0] == 0 && *fractions.terms[i].numerator[1] == 0)
			continue;

		ast *term = inverse_term(fractions.terms[i], s, t);
		if (term == nullptr) {
			ast::dispose(f);
			f = nullptr;
		} else {
			f->appendChild(term);
		}
	}

	if (f != nullptr && f->childCount() == 0)
		f->appendChild(integer(0));

	*whole = !fractions.changed && fractions.count == 1;

	return f != nullptr ? settle(f) : nullptr;
}

/*Sets a and rest so that e is e^(-as) rest, where a is zero when e has no such factor. Returns false if a factor e^u has u not linear in s.*/
static bool split_exponential(const ast &e, const ast &s, ast **a, ast **rest) {
	ast *factors = ast::make(Op::Mult);
	*a = integer(0);
	*rest = ast::make(Op::Mult);

	collect(e, s, false, **rest, *factors);

	for (const ast &factor : factors->children()) {
		ast *slope, *intercept;

		if (!factor.isOp(Op::Pow) || !is_euler(*factor.childAt(0))) {
			(*rest)->appendChild(factor.copy());
		} else if (linear_parts(*factor.childAt(1), s, &slope, &intercept)) {
			*a = ast::make(Op::Add, *a, negate(slope));
			(*rest)->appendChild(ast::make(Op::Pow, ast::make(Sym::Euler), intercept));
		} else {
			ast::dispose(factors);
			return false;
		}
	}

	ast::dispose(factors);
	(*rest)->appendChild(integer(1));
	settle(*a);
	settle(*rest);

	return true;
}

/*Moves each factor e^u of denominator with u involving s to numerator as e^(-u)*/
static void raise_exponentials(ast **numerator, ast **denominator, const ast &s) {
	work::pause();
	simplify(**denominator, Simp::Normalize | Simp::Commutative | Simp::Eval);
	work::resume();

	if (!(*denominator)->isOp(Op::Mult))
		*denominator = ast::make(Op::Mult, *denominator, integer(1));

	for (unsigned i = 0; i < (*denominator)->childCount();) {
		const ast &factor = *(*denominator)->childAt(i);

		if (factor.isOp(Op::Pow) && is_euler(*factor.childAt(0)) && involves(*factor.childAt(1), s)) {
			ast *removed = (*denominator)->removeChildAt(i);
			*numerator = ast::make(
				Op::Mult, *numerator, ast::make(Op::Pow, ast::make(Sym::Euler), negate(removed->childAt(1)->copy()))
			);
			ast::dispose(removed);
		} else {
			i++;
		}
	}

	settle(*denominator);
}

ast *laplace_inverse(const ast &F, const ast &s, const ast &t) {
	constexpr unsigned max_groups = 8;
	ast *shifts[max_groups], *groups[max_groups];
	unsigned count = 0;
	bool known = true;

	ast *G = F.copy();
	work::pause();
	simplify(*G, Simp::Basic);
	work::resume();

	const unsigned terms = G->isOp(Op::Add) ? G->childCount() : 1;

	for (unsigned i = 0; i < terms && known; i++) {
		ast *numerator, *denominator;
		rational_parts(G->isOp(Op::Add) ? *G->childAt(i) : *G, &numerator, &denominator);
		raise_exponentials(&numerator, &denominator, s);

		work::pause();
		simplify(*numerator, Simp::Basic);
		expand(*numerator, Expand::All);
		simplify(*numerator, Simp::Basic);
		work::resume();

		const unsigned parts = numerator->isOp(Op::Add) ? numerator->childCount() : 1;

		for (unsigned j = 0; j < parts && known; j++) {
			ast *a, *rest;
			known = split_exponential(numerator->isOp(Op::Add) ? *numerator->childAt(j) : *numerator, s, &a, &rest);

			if (!known) {
				ast::dispose(a);
				ast::dispose(rest);
				break;
			}

			unsigned group = 0;
			while (group < count && !shifts[group]->compare(*a))
				group++;

			if (group == count) {
				if (count == max_groups) {
					known = false;
					ast::dispose(a);
					ast::dispose(rest);
					break;
				}
				shifts[count] = a;
				groups[count++] = ast::make(Op::Add);
			} else {
				ast::dispose(a);
			}

			groups[group]->appendChild(ast::make(Op::Div, rest, denominator->copy()));
		}

		ast::dispose(numerator);
		ast::dispose(denominator);
	}

	ast *f = known ? ast::make(Op::Add) : nullptr;
	bool whole = false;

	for (unsigned i = 0; i < count && f != nullptr; i++) {
		settle(groups[i]);
		ast *g = inverse_rational(*groups[i], s, t, &whole);

		if (g == nullptr) {
			ast::dispose(f);
			f = nullptr;
			break;
		}

		if (!shifts[i]->isInt(0)) {
			ast *moved = difference(t.copy(), shifts[i]->copy());
			ast *before = ast::make(
				Op::Mult,
				ast::make(Op::Pow, ast::make(Sym::Euler), negate(ast::make(Op::Mult, shifts[i]->copy(), s.copy()))),
				groups[i]->copy()
			);
			settle(before);

			work::pause();
			substitute(*g, t, *moved);
			work::resume();
			ast::dispose(moved);

			ast *added = g->copy();
			work::pause();
			if (add_angles(*added, t)) {
				simplify(*added, Simp::All);
				if (node_count(*added) < node_count(*g)) {
					ast *simpler = added;
					added = g;
					g = simpler;
				}
			}
			work::resume();
			ast::dispose(added);

			g = settle(ast::make(Op::Mult, g, unit_step(t, *shifts[i])));
			record("Second translation theorem", Op::LaplaceInverse, *before, *g);
			ast::dispose(before);
		}

		f->appendChild(g);
	}

	const bool repeated = count == 1 && (!shifts[0]->isInt(0) || whole);

	for (unsigned i = 0; i < count; i++) {
		ast::dispose(shifts[i]);
		ast::dispose(groups[i]);
	}
	ast::dispose(G);

	if (f != nullptr) {
		settle(f);
		if (!repeated)
			record(nullptr, Op::LaplaceInverse, F, *f);
	}

	return f;
}

static bool has_impulse_or_convolution(const ast &e) {
	if (e.isOp(Op::Delta) || (e.isOp(Op::Integral) && e.childCount() == 4))
		return true;

	if (e.isOperator()) {
		for (const ast &child : e.children()) {
			if (has_impulse_or_convolution(child))
				return true;
		}
	}

	return false;
}

bool laplace_needed(const ast &equation) {
	return has_impulse_or_convolution(equation);
}

/*Returns the unknown function of the equation: the function with primes, or else the left side when it is a single letter other than t*/
static const ast *find_unknown(const ast &e, const ast &t) {
	if (e.isOp(Op::Prime)) {
		const ast *base = &e;
		while (base->isOp(Op::Prime))
			base = base->firstChild();
		return base->isSymbol() ? base : nullptr;
	}

	if (e.isOperator()) {
		for (const ast &child : e.children()) {
			const ast *found = find_unknown(child, t);
			if (found != nullptr)
				return found;
		}
	}

	return nullptr;
}

/*Rewrites each product of y and a following factor that involves t or tau as y evaluated there*/
static void apply_unknown(ast &e, const ast &y, const ast &t) {
	if (!e.isOperator())
		return;

	for (ast &child : e.children())
		apply_unknown(child, y, t);

	if (!e.isOp(Op::Mult))
		return;

	for (ast *child = e.firstChild(); child != nullptr && child->next() != nullptr; child = child->next()) {
		ast *argument = child->next();

		if (child->compare(y) && (involves(*argument, t) || contains_symbol(*argument, Sym::Tau))) {
			ast *point = e.removeChild(*argument);
			child->replace(ast::make(Op::At, y.copy(), point));
		}
	}
}

/*Reads an initial condition like Y(0)=1 or Y'(0)=-3 into initial_values. Returns false if it is not one at 0.*/
static bool read_condition(const ast &condition, const ast &y) {
	if (!condition.isOp(Op::Equals) || !condition.firstChild()->isOp(Op::Mult) ||
		condition.firstChild()->childCount() != 2)
		return false;

	const ast &left = *condition.firstChild();
	const int order = prime_order(left.firstChild(), y);
	num *point = rational_value(*left.childAt(1));
	const bool at_zero = point != nullptr && *point == 0;
	num::dispose(point);

	if (order < 0 || order >= static_cast<int>(DiffEq::max_order) || !at_zero || initial_values[order] != nullptr)
		return false;

	initial_values[order] = settle(condition.childAt(1)->copy());
	return true;
}

/*Returns a letter that is not in scope*/
static ast *free_letter(const ast &scope, Sym preferred) {
	const Sym symbol = contains_symbol(scope, preferred) ? fresh_symbol(scope) : preferred;
	return symbol != Sym::Invalid ? ast::make(symbol) : nullptr;
}

/*True if e has a power of e whose exponent involves s*/
static bool has_exponential(const ast &e, const ast &s) {
	if (e.isOp(Op::Pow) && is_euler(*e.childAt(0)) && involves(*e.childAt(1), s))
		return true;

	if (e.isOperator()) {
		for (const ast &child : e.children()) {
			if (has_exponential(child, s))
				return true;
		}
	}

	return false;
}

Error laplace_solve(ast **items, unsigned count, const ast &t, ast **solution) {
	*solution = nullptr;

	ast *equation = items[0]->copy();
	if (!equation->isOp(Op::Equals))
		equation = ast::make(Op::Equals, equation, integer(0));

	const ast *found = find_unknown(*equation, t);
	if (found == nullptr && equation->firstChild()->isSymbol() && !equation->firstChild()->compare(t))
		found = equation->firstChild();

	if (found == nullptr) {
		ast::dispose(equation);
		return Error::DeBadFunction;
	}

	ast *y = found->copy();
	Error err = Error::Success;

	for (unsigned k = 0; k < DiffEq::max_order; k++)
		initial_values[k] = nullptr;
	for (unsigned i = 1; i < count && err == Error::Success; i++) {
		if (!read_condition(*items[i], *y))
			err = Error::DeBadCondition;
	}

	ast *scope = ast::make(Op::Add, equation->copy(), t.copy());
	ast *s = free_letter(*scope, Sym::S);
	if (s != nullptr)
		scope->appendChild(s->copy());
	ast *transform = s != nullptr ? free_letter(*scope, Sym::L) : nullptr;
	ast::dispose(scope);

	if (err == Error::Success && transform == nullptr)
		err = Error::DeUnsolved;

	ast *left = nullptr, *right = nullptr;

	if (err == Error::Success) {
		laplace_normalize(*equation, t);
		apply_unknown(*equation, *y, t);
		work::step(work::Step::Type::State, nullptr, nullptr, equation);

		ast *sides = ast::make(Op::Laplace, equation->childAt(1)->copy());
		work::text("Laplace transform");
		record("Transform both sides", Op::Laplace, *equation->firstChild(), *sides);
		ast::dispose(sides);

		unknown = y;
		unknown_transform = transform;

		left = transform_sum(*equation->firstChild(), t, *s);
		right = left != nullptr ? transform_sum(*equation->childAt(1), t, *s) : nullptr;

		if (right == nullptr)
			err = Error::Laplace;
	}

	ast *F = nullptr;

	if (err == Error::Success) {
		ast *shown_left = shown(*left), *shown_right = shown(*right);
		work::step(work::Step::Type::Equation, nullptr, shown_left, shown_right);
		ast::dispose(shown_left);
		ast::dispose(shown_right);

		ast *difference_ = settle(difference(left->copy(), right->copy()));
		ast *slope = difference_->copy(), *zero = integer(0);

		work::pause();
		derivative(*slope, *transform, *transform);
		simplify(*slope, Simp::Basic);
		substitute(*difference_, *transform, *zero);
		simplify(*difference_, Simp::Basic);
		work::resume();

		if (involves(*slope, *transform) || is_zero(*slope)) {
			err = Error::DeUnsolved;
		} else {
			F = settle(ast::make(Op::Div, negate(difference_->copy()), slope->copy()));
			if (!has_exponential(*F, *s))
				single_fraction(*F);

			ast *named = ast::make(Op::Laplace, y->copy());
			work::step(work::Step::Type::Equation, "Solve for the transform", named, F);
			ast::dispose(named);
		}

		ast::dispose(difference_);
		ast::dispose(slope);
		ast::dispose(zero);
	}

	unknown = nullptr;
	unknown_transform = nullptr;

	if (F != nullptr) {
		ast *f = laplace_inverse(*F, *s, t);

		if (f == nullptr) {
			err = Error::Laplace;
		} else {
			work::step(work::Step::Type::Equation, "Solution", y, f);
			*solution = ast::make(Op::Equals, y->copy(), f);
		}
	}

	for (unsigned k = 0; k < DiffEq::max_order; k++)
		ast::dispose(initial_values[k]);
	ast::dispose(F);
	ast::dispose(left);
	ast::dispose(right);
	ast::dispose(s);
	ast::dispose(transform);
	ast::dispose(y);
	ast::dispose(equation);

	return err;
}
