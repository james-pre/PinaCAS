#include "internal.hxx"

#include "../../work.hxx"

/*Highest degree of a coefficient or right side*/
constexpr int MAX_SERIES_DEGREE = 12;
/*Highest index of a coefficient that is found*/
constexpr int MAX_SERIES_INDEX = 30;

/*The term p (x - x0)^j y^(k) of the equation*/
struct SeriesTerm {
	unsigned k, j;
	num *p;
};

struct Series {
	DiffEq *de;
	/*x, or x - x0 when the center x0 is not zero*/
	ast *base;
	/*Name of the coefficients and the summation index*/
	ast *name, *index;
	SeriesTerm *terms;
	unsigned count;
	/*The right side in powers of the base, up to g_degree, which is -1 when it is zero*/
	num *g[MAX_SERIES_DEGREE + 1];
	int g_degree;
	/*Lowest k - j of a term*/
	int lowest_shift;
	/*Index from which the recurrence relation holds*/
	unsigned start;
	/*Coefficient of y^(order) at the center*/
	num *lead;
};

/*Fills p with the coefficients of e in powers of x - center and returns its degree, -1 if e is zero, or -2 if e is not a polynomial with rational coefficients*/
static int taylor_coefficients(const DiffEq *de, const ast &e, const ast &center, num **p) {
	ast *d = e.copy();
	num factorial(1), scale;
	int count = 0, degree = -1;
	bool polynomial = false;

	for (int j = 0; j <= MAX_SERIES_DEGREE && !polynomial; j++) {
		ast *value = d->copy();

		work::pause();
		substitute(*value, *de->x, center);
		simplify(*value, Simp::Basic);
		work::resume();

		p[j] = rational_value(*value);
		ast::dispose(value);

		if (p[j] == nullptr)
			break;

		*p[j] /= factorial;
		count = j + 1;

		if (*p[j] != 0)
			degree = j;

		polynomial = !involves(*d, *de->x);

		if (!polynomial) {
			work::pause();
			derivative(*d, *de->x, *de->x);
			simplify(*d, Simp::Basic);
			work::resume();

			mp_rat_set_value(&scale, j + 1, 1);
			factorial *= scale;
		}
	}

	ast::dispose(d);

	for (int j = polynomial ? degree + 1 : 0; j < count; j++)
		num::dispose(p[j]);

	return polynomial ? degree : -2;
}

/*Reads the terms of the equation and its right side in powers of x - center. Returns false if they are not polynomials with rational coefficients.*/
static bool load_terms(Series *s, const ast &center) {
	num *p[MAX_SERIES_DEGREE + 1];

	s->terms = static_cast<SeriesTerm *>(malloc(sizeof(SeriesTerm) * (s->de->order + 1) * (MAX_SERIES_DEGREE + 1)));
	s->count = 0;
	s->g_degree = -1;

	for (unsigned k = s->de->order + 1; k-- > 0;) {
		const int degree = taylor_coefficients(s->de, *s->de->a[k], center, p);
		if (degree == -2)
			return false;

		for (int j = 0; j <= degree; j++) {
			if (*p[j] == 0) {
				num::dispose(p[j]);
				continue;
			}

			s->terms[s->count].k = k;
			s->terms[s->count].j = (unsigned)j;
			s->terms[s->count++].p = p[j];
		}
	}

	s->g_degree = taylor_coefficients(s->de, *s->de->g, center, s->g);

	return s->g_degree != -2;
}

static void free_series(Series *s) {
	for (unsigned i = 0; i < s->count; i++)
		num::dispose(s->terms[i].p);
	for (int j = 0; j <= s->g_degree; j++)
		num::dispose(s->g[j]);

	free(s->terms);
	ast::dispose(s->base);
	ast::dispose(s->name);
	ast::dispose(s->index);
}

/*Returns the index plus offset*/
static ast *index_plus(const Series *s, int offset) {
	if (offset == 0)
		return s->index->copy();

	return ast::make(Op::Add, s->index->copy(), integer(offset));
}

/*Returns (n + top)(n + top - 1)...(n + top - k + 1) for the index n*/
static ast *falling(const Series *s, int top, unsigned k) {
	ast *product = ast::make(Op::Mult);

	product->appendChild(integer(1));
	for (unsigned i = 0; i < k; i++)
		product->appendChild(index_plus(s, top - (int)i));

	return product;
}

/*Sets r to (m + top)(m + top - 1)...(m + top - k + 1)*/
static void falling_value(num &r, int m, int top, unsigned k) {
	num factor;

	mp_rat_set_value(&r, 1, 1);
	for (unsigned i = 0; i < k; i++) {
		mp_rat_set_value(&factor, m + top - (int)i, 1);
		r *= factor;
	}
}

/*Returns the coefficient with the subscript. Takes ownership of subscript.*/
static ast *coefficient(const Series *s, ast *subscript) {
	return ast::make(Op::Subscript, s->name->copy(), subscript);
}

static ast *base_power(const Series *s, ast *exponent) {
	return ast::make(Op::Pow, s->base->copy(), exponent);
}

/*Returns the sum of term from the index equal to lower to infinity, written with a minus sign in front when p is negative. Takes ownership of term.*/
static ast *series_sum(const Series *s, const num &p, ast *term, unsigned lower) {
	ast *sum = ast::make(Op::Sum);
	num *magnitude = p.copy();

	mp_rat_abs(magnitude, magnitude);
	term = tidy(ast::make(Op::Mult, ast::make(magnitude), term));

	sum->appendChild(term);
	sum->appendChild(s->index->copy());
	sum->appendChild(integer(lower));

	return p < 0 ? negate(sum) : sum;
}

/*Returns the right side as a polynomial in the base*/
static ast *right_side(const Series *s) {
	if (s->g_degree < 0)
		return integer(0);

	num one(1);
	ast *g = polynomial((num **)s->g, (unsigned)s->g_degree, *s->base, one);

	return g;
}

static void record_sums(const Series *s, const char *text, bool shifted) {
	ast *left = ast::make(Op::Add);

	for (unsigned i = 0; i < s->count; i++) {
		const SeriesTerm *t = &s->terms[i];
		const int shift = (int)t->k - (int)t->j;

		ast *term;
		if (shifted) {
			term = ast::make(Op::Mult, falling(s, shift, t->k), coefficient(s, index_plus(s, shift)));
			term = ast::make(Op::Mult, term, base_power(s, s->index->copy()));
			left->appendChild(series_sum(s, *t->p, term, t->j));
		} else {
			term = ast::make(Op::Mult, falling(s, 0, t->k), coefficient(s, s->index->copy()));
			term = ast::make(Op::Mult, term, base_power(s, tidy(index_plus(s, -shift))));
			left->appendChild(series_sum(s, *t->p, term, t->k));
		}
	}

	ast *right = right_side(s);
	work::step(work::Step::Type::Equation, text, tidy(left), right);
	ast::dispose(left);
	ast::dispose(right);
}

/*Records y = sum of c_n (x - x0)^n, its derivatives, and the equation they make*/
static void record_substitution(const Series *s) {
	if (!s->base->compare(*s->de->x)) {
		ast *left = ast::make(Op::Add);

		for (unsigned k = s->de->order + 1; k-- > 0;) {
			ast *sum = ast::make(Op::Add);

			for (unsigned i = 0; i < s->count; i++) {
				if (s->terms[i].k == k)
					sum->appendChild(
						ast::make(Op::Mult, ast::make(s->terms[i].p->copy()), base_power(s, integer(s->terms[i].j)))
					);
			}

			if (sum->childCount() > 0)
				left->appendChild(ast::make(Op::Mult, tidy(sum), DiffEq::derivative(*s->de->y, k)));
			else
				ast::dispose(sum);
		}

		ast *right = right_side(s);
		work::step(work::Step::Type::Equation, "Powers of the center", tidy(left), right);
		ast::dispose(left);
		ast::dispose(right);
	}

	num one(1);

	for (unsigned k = 0; k <= s->de->order; k++) {
		ast *term = ast::make(Op::Mult, falling(s, 0, k), coefficient(s, s->index->copy()));
		term = ast::make(Op::Mult, term, base_power(s, tidy(index_plus(s, -(int)k))));
		ast *sum = series_sum(s, one, term, k);
		ast *left = DiffEq::derivative(*s->de->y, k);
		work::step(work::Step::Type::Equation, k == 0 ? "Power series" : k == 1 ? "Differentiate" : nullptr, left, sum);
		ast::dispose(left);
		ast::dispose(sum);
	}

	record_sums(s, "Substitute", false);
	record_sums(s, "Shift indices", true);
}

/*Adds p times the polynomial in the index n, of degree n_degree, to sum, multiplying by (n + top)...(n + top - k + 1)*/
static void add_falling(num **sum, const num &p, int top, unsigned k) {
	num *product[DiffEq::max_order + 1], *factor = num::from(0);

	for (unsigned d = 0; d <= DiffEq::max_order; d++)
		product[d] = num::from(0);
	mp_rat_copy(&p, product[0]);

	for (unsigned i = 0; i < k; i++) {
		for (unsigned d = i + 1; d > 0; d--) {
			mp_rat_set_value(factor, top - (int)i, 1);
			*product[d] *= *factor;
			*product[d] += *product[d - 1];
		}
		mp_rat_set_value(factor, top - (int)i, 1);
		*product[0] *= *factor;
	}

	for (unsigned d = 0; d <= DiffEq::max_order; d++) {
		*sum[d] += *product[d];
		num::dispose(product[d]);
	}

	num::dispose(factor);
}

/*Returns the degree of p, or -1 if it is zero*/
static int degree_of(num **p) {
	int d = DiffEq::max_order;

	while (d >= 0 && *p[d] == 0)
		d--;

	return d;
}

/*Returns the polynomial p of degree n in the index with its rational roots factored out*/
static ast *factored_polynomial(const Series *s, num **p, unsigned n) {
	num *copy[DiffEq::max_order + 1];
	Root roots[DiffEq::max_order];
	unsigned count = 0;

	for (unsigned d = 0; d <= n; d++)
		copy[d] = p[d]->copy();

	find_roots(copy, &n, roots, &count);
	ast *e = factored_form(copy, n, roots, count, *s->index);

	for (unsigned d = 0; d <= n; d++)
		num::dispose(copy[d]);
	free_roots(roots, count);

	return e;
}

/*Records the first equations, the sum from the start of the recurrence, and the recurrence relation*/
static void record_recurrence(Series *s) {
	num *P[DiffEq::max_order + 1], *r = num::from(0), *value = num::from(0);
	ast *left = ast::make(Op::Add), *right = ast::make(Op::Add), *zero = integer(0);
	const unsigned order = s->de->order;

	for (unsigned m = 0; m < s->start; m++) {
		ast *e = ast::make(Op::Add);

		for (unsigned i = 0; i < s->count; i++) {
			if (s->terms[i].j > m)
				continue;

			const int shift = (int)s->terms[i].k - (int)s->terms[i].j;
			falling_value(*value, (int)m, shift, s->terms[i].k);
			*value *= *s->terms[i].p;
			e->appendChild(ast::make(Op::Mult, ast::make(value->copy()), coefficient(s, integer((int)m + shift))));
		}

		work::pause();
		simplify(*e, Simp::Basic);
		work::resume();

		ast *sum = (int)m <= s->g_degree ? ast::make(s->g[m]->copy()) : integer(0);
		work::step(work::Step::Type::Equation, m == 0 ? "Separate the first terms" : nullptr, e, sum);
		ast::dispose(e);
		ast::dispose(sum);
	}

	for (int shift = (int)order; shift >= s->lowest_shift; shift--) {
		for (unsigned d = 0; d <= DiffEq::max_order; d++)
			P[d] = num::from(0);

		for (unsigned i = 0; i < s->count; i++) {
			if ((int)s->terms[i].k - (int)s->terms[i].j == shift)
				add_falling(P, *s->terms[i].p, shift, s->terms[i].k);
		}

		int n = degree_of(P);
		if (n >= 0) {
			left->appendChild(
				ast::make(Op::Mult, factored_polynomial(s, P, (unsigned)n), coefficient(s, tidy(index_plus(s, shift))))
			);

			if (shift < (int)order) {
				bool cancelled[DiffEq::max_order + 1];
				for (unsigned d = 1; d <= order; d++) {
					mp_rat_set_value(r, -(int)d, 1);
					cancelled[d] = n > 0 && is_root(P, (unsigned)n, *r);
					if (cancelled[d])
						deflate(P, (unsigned)n--, *r);
				}

				ast *denominator = ast::make(Op::Mult);
				denominator->appendChild(ast::make(s->lead->copy()));
				for (unsigned d = order; d >= 1; d--) {
					if (!cancelled[d])
						denominator->appendChild(index_plus(s, (int)d));
				}

				ast *e = ast::make(
					Op::Mult, factored_polynomial(s, P, (unsigned)n), coefficient(s, tidy(index_plus(s, shift)))
				);
				right->appendChild(negate(ast::make(Op::Div, e, denominator)));
			}
		}

		for (unsigned d = 0; d <= DiffEq::max_order; d++)
			num::dispose(P[d]);
	}

	ast *sum = ast::make(Op::Sum);
	sum->appendChild(ast::make(Op::Mult, tidy(left), base_power(s, s->index->copy())));
	sum->appendChild(s->index->copy());
	sum->appendChild(integer((int)s->start));
	work::step(work::Step::Type::Equation, nullptr, sum, zero);
	ast::dispose(sum);

	left = coefficient(s, tidy(index_plus(s, (int)order)));
	if (right->childCount() == 0)
		right->appendChild(integer(0));

	work::step(work::Step::Type::Equation, "Recurrence relation", left, tidy(right));
	ast::dispose(left);
	ast::dispose(right);
	ast::dispose(zero);
	num::dispose(r);
	num::dispose(value);
}

/*Returns the coefficient with the components c, the multiples of the constants followed by a constant part*/
static ast *combination_of(num **c, ast **constants, unsigned free_count) {
	ast *sum = ast::make(Op::Add);

	for (unsigned b = 0; b < free_count; b++) {
		if (*c[b] != 0)
			sum->appendChild(ast::make(Op::Mult, ast::make(c[b]->copy()), constants[b]->copy()));
	}

	sum->appendChild(ast::make(c[free_count]->copy()));

	work::pause();
	simplify(*sum, Simp::Basic);
	work::resume();

	return sum;
}

/*Returns c times the base to the power i, keeping a factor of 1 on a sum so that it stays one term*/
static ast *series_term(const Series *s, const num &c, unsigned i) {
	ast *power = i == 0 ? nullptr : i == 1 ? s->base->copy() : base_power(s, integer((int)i));

	if (power == nullptr)
		return ast::make(c.copy());

	return c == 1 && !power->isOperator() ? power : ast::make(Op::Mult, ast::make(c.copy()), power);
}

/*Returns the only term of sum if it has one, otherwise sum. Takes ownership of sum.*/
static ast *unwrap_sum(ast *sum) {
	if (sum->childCount() != 1)
		return sum;

	ast *only = sum->removeChildAt(0);
	ast::dispose(sum);

	return only;
}

Error solve_power_series(DiffEq *de, ast **solution) {
	if (!de->linear)
		return Error::DeUnsolved;

	const unsigned order = de->order;
	Error err = Error::Success;

	ast *center = de->center != nullptr     ? de->center->copy()
				  : de->condition_count > 0 ? de->conditions[0].at->copy()
											: integer(0);

	const DiffEq::Condition *conditions[DiffEq::max_order] = {nullptr};

	for (unsigned i = 0; i < de->condition_count; i++) {
		ast *e = difference(de->conditions[i].at->copy(), center->copy());
		if (!is_zero(*e))
			err = Error::DeBadCondition;
		ast::dispose(e);
		conditions[de->conditions[i].order] = &de->conditions[i];
	}

	Series s;
	s.de = de;
	s.base = nullptr;
	s.name = nullptr;
	s.index = nullptr;
	s.terms = nullptr;
	s.count = 0;
	s.g_degree = -1;
	s.lead = nullptr;

	if (err == Error::Success && !load_terms(&s, *center))
		err = Error::DeUnsolved;

	for (unsigned i = 0; i < s.count && err == Error::Success; i++) {
		if (s.terms[i].k == order && s.terms[i].j == 0)
			s.lead = s.terms[i].p;
	}

	if (err == Error::Success && s.lead == nullptr)
		err = Error::DeSingular;

	if (err != Error::Success) {
		free_series(&s);
		ast::dispose(center);
		return err;
	}

	de->method = "Power series";

	s.name = substitution_symbol(de, Sym::C);
	ast *scope = ast::make(Op::Add);
	scope->appendChild(de->equation->copy());
	scope->appendChild(de->x->copy());
	scope->appendChild(s.name->copy());
	s.index = ast::make(static_cast<Sym>(contains_symbol(*scope, Sym::N) ? fresh_symbol(*scope) : Sym::N));
	ast::dispose(scope);

	ast *offset = difference(center->copy(), integer(0));
	s.base = is_zero(*offset) ? de->x->copy() : tidy(difference(de->x->copy(), center->copy()));
	ast::dispose(offset);

	s.lowest_shift = (int)order;
	s.start = s.g_degree >= 0 ? (unsigned)s.g_degree + 1 : 0;
	for (unsigned i = 0; i < s.count; i++) {
		if ((int)s.terms[i].k - (int)s.terms[i].j < s.lowest_shift)
			s.lowest_shift = (int)s.terms[i].k - (int)s.terms[i].j;
		if (s.terms[i].j > s.start)
			s.start = s.terms[i].j;
	}

	canonical_SetSeries(s.base);
	record_substitution(&s);
	record_recurrence(&s);

	int free_index[DiffEq::max_order];
	unsigned free_count = 0;
	for (unsigned k = 0; k < order; k++) {
		if (conditions[k] == nullptr)
			free_index[k] = (int)free_count++;
		else
			free_index[k] = -1;
	}

	ast *constants[DiffEq::max_order];
	scope = ast::make(Op::Add, s.name->copy(), s.index->copy());
	if (!choose_constants(de, scope, constants, free_count))
		err = Error::DeUnsolved;
	ast::dispose(scope);

	if (err != Error::Success) {
		free_series(&s);
		ast::dispose(center);
		return err;
	}

	const unsigned components = free_count + 1;
	num value, denominator;
	num **coefficients[MAX_SERIES_INDEX + 1];
	bool labelled_free = false, labelled_conditions = false;

	for (unsigned k = 0; k < order; k++) {
		coefficients[k] = static_cast<num **>(malloc(sizeof(num *) * components));
		for (unsigned b = 0; b < components; b++)
			coefficients[k][b] = num::from(0);

		if (conditions[k] == nullptr) {
			mp_rat_set_value(coefficients[k][free_index[k]], 1, 1);
			ast *e = coefficient(&s, integer((int)k));
			work::step(
				work::Step::Type::Equation, labelled_free ? nullptr : "Arbitrary constants", e, constants[free_index[k]]
			);
			ast::dispose(e);
			labelled_free = true;
			continue;
		}

		num::dispose(coefficients[k][free_count]);
		if ((coefficients[k][free_count] = rational_value(*conditions[k]->value)) == nullptr) {
			coefficients[k][free_count] = num::from(0);
			err = Error::DeUnsolved;
		}

		falling_value(value, (int)k, 0, k);
		*coefficients[k][free_count] /= value;

		ast *e = ast::make(Op::At, DiffEq::derivative(*de->y, k), center->copy());
		if (k > 1)
			e = ast::make(Op::Div, e, ast::make(value.copy()));
		ast *chain = ast::make(Op::Equals, e, ast::make(coefficients[k][free_count]->copy()));
		e = coefficient(&s, integer((int)k));
		work::step(work::Step::Type::Equation, labelled_conditions ? nullptr : "Initial conditions", e, chain);
		ast::dispose(e);
		ast::dispose(chain);
		labelled_conditions = true;
	}

	const unsigned window = (unsigned)((int)order - s.lowest_shift);
	unsigned counts[DiffEq::max_order + 1], last[DiffEq::max_order + 1];
	bool active[DiffEq::max_order + 1];

	for (unsigned b = 0; b < components; b++) {
		counts[b] = 0;
		last[b] = order - 1;
		for (unsigned k = 0; k < order; k++) {
			if (*coefficients[k][b] != 0) {
				counts[b]++;
				last[b] = k;
			}
		}
		active[b] = b < free_count || counts[b] > 0 || s.g_degree >= 0;
	}

	unsigned computed = order;
	bool done = err != Error::Success;

	for (unsigned m = 0; !done && m + order <= MAX_SERIES_INDEX; m++) {
		const unsigned t = m + order;
		coefficients[t] = static_cast<num **>(malloc(sizeof(num *) * components));

		falling_value(denominator, (int)m, (int)order, order);
		denominator *= *s.lead;

		for (unsigned b = 0; b < components; b++) {
			coefficients[t][b] = num::from(0);

			if (b == free_count && (int)m <= s.g_degree)
				mp_rat_copy(s.g[m], coefficients[t][b]);

			for (unsigned i = 0; i < s.count; i++) {
				const int shift = (int)s.terms[i].k - (int)s.terms[i].j;

				if (s.terms[i].j > m || (s.terms[i].k == order && s.terms[i].j == 0))
					continue;

				falling_value(value, (int)m, shift, s.terms[i].k);
				value *= *s.terms[i].p;
				value *= *coefficients[(int)m + shift][b];
				*coefficients[t][b] -= value;
			}

			*coefficients[t][b] /= denominator;
		}

		computed = t + 1;

		ast *e = coefficient(&s, integer((int)t));
		ast *chain = combination_of(coefficients[t], constants, free_count);
		work::step(work::Step::Type::Equation, m == 0 ? "Coefficients" : nullptr, e, chain);
		ast::dispose(e);
		ast::dispose(chain);

		done = true;

		for (unsigned b = 0; b < components; b++) {
			bool zero_window = t + 1 >= window && (int)m > s.g_degree;

			if (*coefficients[t][b] != 0 && counts[b] < de->terms) {
				counts[b]++;
				last[b] = t;
			}

			for (unsigned i = 0; zero_window && i < window; i++)
				zero_window = *coefficients[t - i][b] == 0;

			if (active[b] && counts[b] < de->terms && !zero_window)
				done = false;
		}
	}

	ast *answer = ast::make(Op::Add);

	for (unsigned b = 0; b < components && err == Error::Success; b++) {
		if (!active[b])
			continue;

		ast *part = b < free_count ? ast::make(Op::Add) : answer;
		for (unsigned i = 0; i <= last[b] && i < computed; i++) {
			if (*coefficients[i][b] != 0)
				part->appendChild(series_term(&s, *coefficients[i][b], i));
		}

		if (part == answer)
			continue;

		if (part->childCount() == 0)
			ast::dispose(part);
		else
			answer->appendChild(ast::make(Op::Mult, constants[b]->copy(), unwrap_sum(part)));
	}

	if (answer->childCount() == 0)
		answer->appendChild(integer(0));

	answer = unwrap_sum(answer);

	if (err == Error::Success) {
		work::pause();
		simplify(*answer, Simp::Normalize | Simp::Rational);
		work::resume();
		work::step(work::Step::Type::Equation, "Solution", de->y, answer);
		*solution = ast::make(Op::Equals, de->y->copy(), answer);
	} else {
		ast::dispose(answer);
	}

	for (unsigned i = 0; i < computed; i++) {
		for (unsigned b = 0; b < components; b++)
			num::dispose(coefficients[i][b]);
		free(coefficients[i]);
	}

	for (unsigned b = 0; b < free_count; b++)
		ast::dispose(constants[b]);

	free_series(&s);
	ast::dispose(center);

	return err;
}
