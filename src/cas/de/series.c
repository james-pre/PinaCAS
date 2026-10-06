#include "internal.h"

#include "../../work.h"

/*Highest degree of a coefficient or right side*/
#define MAX_SERIES_DEGREE 12
/*Highest index of a coefficient that is found*/
#define MAX_SERIES_INDEX 30

/*The term p (x - x0)^j y^(k) of the equation*/
typedef struct {
	unsigned k, j;
	mp_rat p;
} series_term_t;

typedef struct {
	pcas_de_t *de;
	/*x, or x - x0 when the center x0 is not zero*/
	pcas_ast_t *base;
	/*Name of the coefficients and the summation index*/
	pcas_ast_t *name, *index;
	series_term_t *terms;
	unsigned count;
	/*The right side in powers of the base, up to g_degree, which is -1 when it is zero*/
	mp_rat g[MAX_SERIES_DEGREE + 1];
	int g_degree;
	/*Lowest k - j of a term*/
	int lowest_shift;
	/*Index from which the recurrence relation holds*/
	unsigned start;
	/*Coefficient of y^(order) at the center*/
	mp_rat lead;
} series_t;

/*Fills p with the coefficients of e in powers of x - center and returns its degree, -1 if e is zero, or -2 if e is not a polynomial with rational coefficients*/
static int taylor_coefficients(const pcas_de_t *de, const pcas_ast_t *e, const pcas_ast_t *center, mp_rat *p) {
	pcas_ast_t *d = ast_Copy(e), *value;
	mp_rat factorial = num_FromInt(1), scale = num_FromInt(0);
	int j, count = 0, degree = -1;
	bool polynomial = false;

	for (j = 0; j <= MAX_SERIES_DEGREE && !polynomial; j++) {
		value = ast_Copy(d);

		work_Pause();
		substitute(value, de->x, center);
		simplify(value, SIMP_BASIC);
		work_Resume();

		p[j] = rational_value(value);
		ast_Cleanup(value);

		if (p[j] == NULL)
			break;

		mp_rat_div(p[j], factorial, p[j]);
		count = j + 1;

		if (mp_rat_compare_zero(p[j]) != 0)
			degree = j;

		polynomial = !involves(d, de->x);

		if (!polynomial) {
			work_Pause();
			derivative(d, de->x, de->x);
			simplify(d, SIMP_BASIC);
			work_Resume();

			mp_rat_set_value(scale, j + 1, 1);
			mp_rat_mul(factorial, scale, factorial);
		}
	}

	ast_Cleanup(d);
	num_Cleanup(factorial);
	num_Cleanup(scale);

	for (j = polynomial ? degree + 1 : 0; j < count; j++)
		num_Cleanup(p[j]);

	return polynomial ? degree : -2;
}

/*Reads the terms of the equation and its right side in powers of x - center. Returns false if they are not polynomials with rational coefficients.*/
static bool load_terms(series_t *s, const pcas_ast_t *center) {
	mp_rat p[MAX_SERIES_DEGREE + 1];
	unsigned k;
	int degree, j;

	s->terms = malloc(sizeof(series_term_t) * (s->de->order + 1) * (MAX_SERIES_DEGREE + 1));
	s->count = 0;
	s->g_degree = -1;

	for (k = s->de->order + 1; k-- > 0;) {
		if ((degree = taylor_coefficients(s->de, s->de->a[k], center, p)) == -2)
			return false;

		for (j = 0; j <= degree; j++) {
			if (mp_rat_compare_zero(p[j]) == 0) {
				num_Cleanup(p[j]);
				continue;
			}

			s->terms[s->count].k = k;
			s->terms[s->count].j = (unsigned)j;
			s->terms[s->count++].p = p[j];
		}
	}

	s->g_degree = taylor_coefficients(s->de, s->de->g, center, s->g);

	return s->g_degree != -2;
}

static void free_series(series_t *s) {
	unsigned i;
	int j;

	for (i = 0; i < s->count; i++)
		num_Cleanup(s->terms[i].p);
	for (j = 0; j <= s->g_degree; j++)
		num_Cleanup(s->g[j]);

	free(s->terms);
	ast_Cleanup(s->base);
	ast_Cleanup(s->name);
	ast_Cleanup(s->index);
}

/*Returns the index plus offset*/
static pcas_ast_t *index_plus(const series_t *s, int offset) {
	if (offset == 0)
		return ast_Copy(s->index);

	return ast_MakeBinary(OP_ADD, ast_Copy(s->index), integer(offset));
}

/*Returns (n + top)(n + top - 1)...(n + top - k + 1) for the index n*/
static pcas_ast_t *falling(const series_t *s, int top, unsigned k) {
	pcas_ast_t *product = ast_MakeOperator(OP_MULT);
	unsigned i;

	ast_ChildAppend(product, integer(1));
	for (i = 0; i < k; i++)
		ast_ChildAppend(product, index_plus(s, top - (int)i));

	return product;
}

/*Sets r to (m + top)(m + top - 1)...(m + top - k + 1)*/
static void falling_value(mp_rat r, int m, int top, unsigned k) {
	mp_rat factor = num_FromInt(0);
	unsigned i;

	mp_rat_set_value(r, 1, 1);
	for (i = 0; i < k; i++) {
		mp_rat_set_value(factor, m + top - (int)i, 1);
		mp_rat_mul(r, factor, r);
	}

	num_Cleanup(factor);
}

/*Returns the coefficient with the subscript. Takes ownership of subscript.*/
static pcas_ast_t *coefficient(const series_t *s, pcas_ast_t *subscript) {
	return ast_MakeBinary(OP_SUBSCRIPT, ast_Copy(s->name), subscript);
}

static pcas_ast_t *base_power(const series_t *s, pcas_ast_t *exponent) {
	return ast_MakeBinary(OP_POW, ast_Copy(s->base), exponent);
}

/*Returns the sum of term from the index equal to lower to infinity, written with a minus sign in front when p is negative. Takes ownership of term.*/
static pcas_ast_t *series_sum(const series_t *s, mp_rat p, pcas_ast_t *term, unsigned lower) {
	pcas_ast_t *sum = ast_MakeOperator(OP_SUM);
	mp_rat magnitude = num_Copy(p);

	mp_rat_abs(magnitude, magnitude);
	term = tidy(ast_MakeBinary(OP_MULT, ast_MakeNumber(magnitude), term));

	ast_ChildAppend(sum, term);
	ast_ChildAppend(sum, ast_Copy(s->index));
	ast_ChildAppend(sum, integer(lower));

	return mp_rat_compare_zero(p) < 0 ? negate(sum) : sum;
}

/*Returns the right side as a polynomial in the base*/
static pcas_ast_t *right_side(const series_t *s) {
	mp_rat one;
	pcas_ast_t *g;

	if (s->g_degree < 0)
		return integer(0);

	one = num_FromInt(1);
	g = polynomial((mp_rat *)s->g, (unsigned)s->g_degree, s->base, one);
	num_Cleanup(one);

	return g;
}

static void record_sums(const series_t *s, const char *text, bool shifted) {
	pcas_ast_t *left = ast_MakeOperator(OP_ADD), *term, *right;
	const series_term_t *t;
	unsigned i;
	int shift;

	for (i = 0; i < s->count; i++) {
		t = &s->terms[i];
		shift = (int)t->k - (int)t->j;

		if (shifted) {
			term = ast_MakeBinary(OP_MULT, falling(s, shift, t->k), coefficient(s, index_plus(s, shift)));
			term = ast_MakeBinary(OP_MULT, term, base_power(s, ast_Copy(s->index)));
			ast_ChildAppend(left, series_sum(s, t->p, term, t->j));
		} else {
			term = ast_MakeBinary(OP_MULT, falling(s, 0, t->k), coefficient(s, ast_Copy(s->index)));
			term = ast_MakeBinary(OP_MULT, term, base_power(s, tidy(index_plus(s, -shift))));
			ast_ChildAppend(left, series_sum(s, t->p, term, t->k));
		}
	}

	right = right_side(s);
	work_Step(STEP_EQUATION, text, tidy(left), right);
	ast_Cleanup(left);
	ast_Cleanup(right);
}

/*Records y = sum of c_n (x - x0)^n, its derivatives, and the equation they make*/
static void record_substitution(const series_t *s) {
	pcas_ast_t *left, *right, *term, *sum;
	mp_rat one = num_FromInt(1);
	unsigned i, k;

	if (!ast_Compare(s->base, s->de->x)) {
		left = ast_MakeOperator(OP_ADD);

		for (k = s->de->order + 1; k-- > 0;) {
			sum = ast_MakeOperator(OP_ADD);

			for (i = 0; i < s->count; i++) {
				if (s->terms[i].k == k)
					ast_ChildAppend(
						sum,
						ast_MakeBinary(
							OP_MULT, ast_MakeNumber(num_Copy(s->terms[i].p)), base_power(s, integer(s->terms[i].j))
						)
					);
			}

			if (ast_ChildLength(sum) > 0)
				ast_ChildAppend(left, ast_MakeBinary(OP_MULT, tidy(sum), de_Derivative(s->de->y, k)));
			else
				ast_Cleanup(sum);
		}

		right = right_side(s);
		work_Step(STEP_EQUATION, "Powers of the center", tidy(left), right);
		ast_Cleanup(left);
		ast_Cleanup(right);
	}

	for (k = 0; k <= s->de->order; k++) {
		term = ast_MakeBinary(OP_MULT, falling(s, 0, k), coefficient(s, ast_Copy(s->index)));
		term = ast_MakeBinary(OP_MULT, term, base_power(s, tidy(index_plus(s, -(int)k))));
		sum = series_sum(s, one, term, k);
		left = de_Derivative(s->de->y, k);
		work_Step(STEP_EQUATION, k == 0 ? "Power series" : k == 1 ? "Differentiate" : NULL, left, sum);
		ast_Cleanup(left);
		ast_Cleanup(sum);
	}

	num_Cleanup(one);

	record_sums(s, "Substitute", false);
	record_sums(s, "Shift indices", true);
}

/*Adds p times the polynomial in the index n, of degree n_degree, to sum, multiplying by (n + top)...(n + top - k + 1)*/
static void add_falling(mp_rat *sum, mp_rat p, int top, unsigned k) {
	mp_rat product[DE_MAX_ORDER + 1], factor = num_FromInt(0);
	unsigned i, d;

	for (d = 0; d <= DE_MAX_ORDER; d++)
		product[d] = num_FromInt(0);
	mp_rat_copy(p, product[0]);

	for (i = 0; i < k; i++) {
		for (d = i + 1; d > 0; d--) {
			mp_rat_set_value(factor, top - (int)i, 1);
			mp_rat_mul(product[d], factor, product[d]);
			mp_rat_add(product[d], product[d - 1], product[d]);
		}
		mp_rat_set_value(factor, top - (int)i, 1);
		mp_rat_mul(product[0], factor, product[0]);
	}

	for (d = 0; d <= DE_MAX_ORDER; d++) {
		mp_rat_add(sum[d], product[d], sum[d]);
		num_Cleanup(product[d]);
	}

	num_Cleanup(factor);
}

/*Returns the degree of p, or -1 if it is zero*/
static int degree_of(mp_rat *p) {
	int d;

	for (d = DE_MAX_ORDER; d >= 0 && mp_rat_compare_zero(p[d]) == 0; d--)
		;

	return d;
}

/*Returns the polynomial p of degree n in the index with its rational roots factored out*/
static pcas_ast_t *factored_polynomial(const series_t *s, mp_rat *p, unsigned n) {
	mp_rat copy[DE_MAX_ORDER + 1];
	root_t roots[DE_MAX_ORDER];
	pcas_ast_t *e;
	unsigned count = 0, d;

	for (d = 0; d <= n; d++)
		copy[d] = num_Copy(p[d]);

	find_roots(copy, &n, roots, &count);
	e = factored_form(copy, n, roots, count, s->index);

	for (d = 0; d <= n; d++)
		num_Cleanup(copy[d]);
	free_roots(roots, count);

	return e;
}

/*Records the first equations, the sum from the start of the recurrence, and the recurrence relation*/
static void record_recurrence(series_t *s) {
	mp_rat P[DE_MAX_ORDER + 1], r = num_FromInt(0), value = num_FromInt(0);
	pcas_ast_t *left = ast_MakeOperator(OP_ADD), *right = ast_MakeOperator(OP_ADD), *sum, *zero = integer(0), *e,
			   *denominator;
	unsigned order = s->de->order, m, i, d;
	int shift, n;
	bool cancelled[DE_MAX_ORDER + 1];

	for (m = 0; m < s->start; m++) {
		e = ast_MakeOperator(OP_ADD);

		for (i = 0; i < s->count; i++) {
			if (s->terms[i].j > m)
				continue;

			shift = (int)s->terms[i].k - (int)s->terms[i].j;
			falling_value(value, (int)m, shift, s->terms[i].k);
			mp_rat_mul(value, s->terms[i].p, value);
			ast_ChildAppend(
				e, ast_MakeBinary(OP_MULT, ast_MakeNumber(num_Copy(value)), coefficient(s, integer((int)m + shift)))
			);
		}

		work_Pause();
		simplify(e, SIMP_BASIC);
		work_Resume();

		sum = (int)m <= s->g_degree ? ast_MakeNumber(num_Copy(s->g[m])) : integer(0);
		work_Step(STEP_EQUATION, m == 0 ? "Separate the first terms" : NULL, e, sum);
		ast_Cleanup(e);
		ast_Cleanup(sum);
	}

	for (shift = (int)order; shift >= s->lowest_shift; shift--) {
		for (d = 0; d <= DE_MAX_ORDER; d++)
			P[d] = num_FromInt(0);

		for (i = 0; i < s->count; i++) {
			if ((int)s->terms[i].k - (int)s->terms[i].j == shift)
				add_falling(P, s->terms[i].p, shift, s->terms[i].k);
		}

		if ((n = degree_of(P)) >= 0) {
			ast_ChildAppend(
				left,
				ast_MakeBinary(OP_MULT, factored_polynomial(s, P, (unsigned)n), coefficient(s, tidy(index_plus(s, shift))))
			);

			if (shift < (int)order) {
				for (d = 1; d <= order; d++) {
					mp_rat_set_value(r, -(int)d, 1);
					cancelled[d] = n > 0 && is_root(P, (unsigned)n, r);
					if (cancelled[d])
						deflate(P, (unsigned)n--, r);
				}

				denominator = ast_MakeOperator(OP_MULT);
				ast_ChildAppend(denominator, ast_MakeNumber(num_Copy(s->lead)));
				for (d = order; d >= 1; d--) {
					if (!cancelled[d])
						ast_ChildAppend(denominator, index_plus(s, (int)d));
				}

				e = ast_MakeBinary(OP_DIV, factored_polynomial(s, P, (unsigned)n), denominator);
				ast_ChildAppend(
					right, negate(ast_MakeBinary(OP_MULT, e, coefficient(s, tidy(index_plus(s, shift)))))
				);
			}
		}

		for (d = 0; d <= DE_MAX_ORDER; d++)
			num_Cleanup(P[d]);
	}

	sum = ast_MakeOperator(OP_SUM);
	ast_ChildAppend(sum, ast_MakeBinary(OP_MULT, tidy(left), base_power(s, ast_Copy(s->index))));
	ast_ChildAppend(sum, ast_Copy(s->index));
	ast_ChildAppend(sum, integer((int)s->start));
	work_Step(STEP_EQUATION, NULL, sum, zero);
	ast_Cleanup(sum);

	left = coefficient(s, tidy(index_plus(s, (int)order)));
	if (ast_ChildLength(right) == 0)
		ast_ChildAppend(right, integer(0));

	work_Step(STEP_EQUATION, "Recurrence relation", left, tidy(right));
	ast_Cleanup(left);
	ast_Cleanup(right);
	ast_Cleanup(zero);
	num_Cleanup(r);
	num_Cleanup(value);
}

/*Returns the coefficient with the components c, the multiples of the constants followed by a constant part*/
static pcas_ast_t *combination_of(mp_rat *c, pcas_ast_t **constants, unsigned free_count) {
	pcas_ast_t *sum = ast_MakeOperator(OP_ADD);
	unsigned b;

	for (b = 0; b < free_count; b++) {
		if (mp_rat_compare_zero(c[b]) != 0)
			ast_ChildAppend(sum, ast_MakeBinary(OP_MULT, ast_MakeNumber(num_Copy(c[b])), ast_Copy(constants[b])));
	}

	ast_ChildAppend(sum, ast_MakeNumber(num_Copy(c[free_count])));

	work_Pause();
	simplify(sum, SIMP_BASIC);
	work_Resume();

	return sum;
}

pcas_error_t solve_power_series(pcas_de_t *de, pcas_ast_t **solution) {
	series_t s;
	pcas_ast_t *center, *scope, *constants[DE_MAX_ORDER], *e, *chain, *answer, *part;
	mp_rat *coefficients[MAX_SERIES_INDEX + 1], value, denominator;
	pcas_condition_t *conditions[DE_MAX_ORDER] = {NULL};
	unsigned order = de->order, components = order + 1, free_count = 0, counts[DE_MAX_ORDER + 1],
			 last[DE_MAX_ORDER + 1], computed, window, i, b, k, m, t;
	int free_index[DE_MAX_ORDER];
	bool active[DE_MAX_ORDER + 1], done, labelled_free = false, labelled_conditions = false;
	pcas_error_t err = E_SUCCESS;

	if (!de->linear)
		return E_DE_UNSOLVED;

	center = de->center != NULL ? ast_Copy(de->center)
			 : de->condition_count > 0 ? ast_Copy(de->conditions[0].at)
									   : integer(0);

	for (i = 0; i < de->condition_count; i++) {
		e = difference(ast_Copy(de->conditions[i].at), ast_Copy(center));
		if (!is_zero(e))
			err = E_DE_BAD_CONDITION;
		ast_Cleanup(e);
		conditions[de->conditions[i].order] = &de->conditions[i];
	}

	s.de = de;
	s.base = NULL;
	s.name = NULL;
	s.index = NULL;
	s.terms = NULL;
	s.count = 0;
	s.g_degree = -1;
	s.lead = NULL;

	if (err == E_SUCCESS && !load_terms(&s, center))
		err = E_DE_UNSOLVED;

	for (i = 0; i < s.count && err == E_SUCCESS; i++) {
		if (s.terms[i].k == order && s.terms[i].j == 0)
			s.lead = s.terms[i].p;
	}

	if (err == E_SUCCESS && s.lead == NULL)
		err = E_DE_SINGULAR;

	if (err != E_SUCCESS) {
		free_series(&s);
		ast_Cleanup(center);
		return err;
	}

	de->method = "Power series";

	s.name = substitution_symbol(de, SYM_C);
	scope = ast_MakeOperator(OP_ADD);
	ast_ChildAppend(scope, ast_Copy(de->equation));
	ast_ChildAppend(scope, ast_Copy(de->x));
	ast_ChildAppend(scope, ast_Copy(s.name));
	s.index = ast_MakeSymbol(contains_symbol(scope, SYM_N) ? fresh_symbol(scope) : SYM_N);
	ast_Cleanup(scope);

	e = difference(ast_Copy(center), integer(0));
	s.base = is_zero(e) ? ast_Copy(de->x) : tidy(difference(ast_Copy(de->x), ast_Copy(center)));
	ast_Cleanup(e);

	s.lowest_shift = (int)order;
	s.start = s.g_degree >= 0 ? (unsigned)s.g_degree + 1 : 0;
	for (i = 0; i < s.count; i++) {
		if ((int)s.terms[i].k - (int)s.terms[i].j < s.lowest_shift)
			s.lowest_shift = (int)s.terms[i].k - (int)s.terms[i].j;
		if (s.terms[i].j > s.start)
			s.start = s.terms[i].j;
	}

	record_substitution(&s);
	record_recurrence(&s);

	for (k = 0; k < order; k++) {
		if (conditions[k] == NULL)
			free_index[k] = (int)free_count++;
		else
			free_index[k] = -1;
	}

	scope = ast_MakeBinary(OP_ADD, ast_Copy(s.name), ast_Copy(s.index));
	if (!choose_constants(de, scope, constants, free_count))
		err = E_DE_UNSOLVED;
	ast_Cleanup(scope);

	if (err != E_SUCCESS) {
		free_series(&s);
		ast_Cleanup(center);
		return err;
	}

	components = free_count + 1;
	value = num_FromInt(0);
	denominator = num_FromInt(0);

	for (k = 0; k < order; k++) {
		coefficients[k] = malloc(sizeof(mp_rat) * components);
		for (b = 0; b < components; b++)
			coefficients[k][b] = num_FromInt(0);

		if (conditions[k] == NULL) {
			mp_rat_set_value(coefficients[k][free_index[k]], 1, 1);
			e = coefficient(&s, integer((int)k));
			work_Step(STEP_EQUATION, labelled_free ? NULL : "Arbitrary constants", e, constants[free_index[k]]);
			ast_Cleanup(e);
			labelled_free = true;
			continue;
		}

		num_Cleanup(coefficients[k][free_count]);
		if ((coefficients[k][free_count] = rational_value(conditions[k]->value)) == NULL) {
			coefficients[k][free_count] = num_FromInt(0);
			err = E_DE_UNSOLVED;
		}

		falling_value(value, (int)k, 0, k);
		mp_rat_div(coefficients[k][free_count], value, coefficients[k][free_count]);

		e = ast_MakeBinary(OP_AT, de_Derivative(de->y, k), ast_Copy(center));
		if (k > 1)
			e = ast_MakeBinary(OP_DIV, e, ast_MakeNumber(num_Copy(value)));
		chain = ast_MakeBinary(OP_EQUALS, e, ast_MakeNumber(num_Copy(coefficients[k][free_count])));
		e = coefficient(&s, integer((int)k));
		work_Step(STEP_EQUATION, labelled_conditions ? NULL : "Initial conditions", e, chain);
		ast_Cleanup(e);
		ast_Cleanup(chain);
		labelled_conditions = true;
	}

	window = (unsigned)((int)order - s.lowest_shift);

	for (b = 0; b < components; b++) {
		counts[b] = 0;
		last[b] = order - 1;
		for (k = 0; k < order; k++) {
			if (mp_rat_compare_zero(coefficients[k][b]) != 0) {
				counts[b]++;
				last[b] = k;
			}
		}
		active[b] = b < free_count || counts[b] > 0 || s.g_degree >= 0;
	}

	computed = order;
	done = err != E_SUCCESS;

	for (m = 0; !done && m + order <= MAX_SERIES_INDEX; m++) {
		t = m + order;
		coefficients[t] = malloc(sizeof(mp_rat) * components);

		falling_value(denominator, (int)m, (int)order, order);
		mp_rat_mul(denominator, s.lead, denominator);

		for (b = 0; b < components; b++) {
			coefficients[t][b] = num_FromInt(0);

			if (b == free_count && (int)m <= s.g_degree)
				mp_rat_copy(s.g[m], coefficients[t][b]);

			for (i = 0; i < s.count; i++) {
				int shift = (int)s.terms[i].k - (int)s.terms[i].j;

				if (s.terms[i].j > m || (s.terms[i].k == order && s.terms[i].j == 0))
					continue;

				falling_value(value, (int)m, shift, s.terms[i].k);
				mp_rat_mul(value, s.terms[i].p, value);
				mp_rat_mul(value, coefficients[(int)m + shift][b], value);
				mp_rat_sub(coefficients[t][b], value, coefficients[t][b]);
			}

			mp_rat_div(coefficients[t][b], denominator, coefficients[t][b]);
		}

		computed = t + 1;

		e = coefficient(&s, integer((int)t));
		chain = combination_of(coefficients[t], constants, free_count);
		work_Step(STEP_EQUATION, m == 0 ? "Coefficients" : NULL, e, chain);
		ast_Cleanup(e);
		ast_Cleanup(chain);

		done = true;

		for (b = 0; b < components; b++) {
			bool zero_window = t + 1 >= window && (int)m > s.g_degree;

			if (mp_rat_compare_zero(coefficients[t][b]) != 0 && counts[b] < de->terms) {
				counts[b]++;
				last[b] = t;
			}

			for (i = 0; zero_window && i < window; i++)
				zero_window = mp_rat_compare_zero(coefficients[t - i][b]) == 0;

			if (active[b] && counts[b] < de->terms && !zero_window)
				done = false;
		}
	}

	answer = ast_MakeOperator(OP_ADD);

	for (b = 0; b < components && err == E_SUCCESS; b++) {
		if (!active[b])
			continue;

		part = ast_MakeOperator(OP_ADD);
		for (i = 0; i <= last[b] && i < computed; i++) {
			if (mp_rat_compare_zero(coefficients[i][b]) != 0)
				ast_ChildAppend(
					part, ast_MakeBinary(OP_MULT, ast_MakeNumber(num_Copy(coefficients[i][b])), base_power(&s, integer((int)i)))
				);
		}

		if (ast_ChildLength(part) == 0) {
			ast_Cleanup(part);
			continue;
		}

		work_Pause();
		simplify(part, SIMP_BASIC);
		work_Resume();

		ast_ChildAppend(answer, b < free_count ? ast_MakeBinary(OP_MULT, ast_Copy(constants[b]), part) : part);
	}

	if (ast_ChildLength(answer) == 0)
		ast_ChildAppend(answer, integer(0));

	if (err == E_SUCCESS) {
		tidy(answer);
		work_Step(STEP_EQUATION, "Solution", de->y, answer);
		*solution = ast_MakeBinary(OP_EQUALS, ast_Copy(de->y), answer);
	} else {
		ast_Cleanup(answer);
	}

	for (i = 0; i < computed; i++) {
		for (b = 0; b < components; b++)
			num_Cleanup(coefficients[i][b]);
		free(coefficients[i]);
	}

	for (b = 0; b < free_count; b++)
		ast_Cleanup(constants[b]);

	num_Cleanup(value);
	num_Cleanup(denominator);
	free_series(&s);
	ast_Cleanup(center);

	return err;
}
