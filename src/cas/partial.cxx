#include "cas.hxx"

#include "de/internal.hxx"

#include "../work.hxx"

/*Highest degree of a denominator*/
constexpr unsigned MAX_DEGREE = DiffEq::max_order;
/*Highest degree of a numerator*/
constexpr unsigned MAX_NUMERATOR = 2 * MAX_DEGREE;

/*A factor of a denominator with integer coefficients, constant first, raised to multiplicity*/
struct Divisor {
	num *c[3];
	unsigned degree, multiplicity;
};

static void dispose_coefficients(num **p, unsigned degree) {
	for (unsigned k = 0; k <= degree; k++)
		num::dispose(p[k]);
}

static bool is_polynomial(const ast &e, const ast &x) {
	if (!involves(e, x))
		return true;

	if (e.compare(x))
		return true;

	if (e.isOp(Op::Add) || e.isOp(Op::Mult)) {
		for (const ast &child : e.children()) {
			if (!is_polynomial(child, x))
				return false;
		}
		return true;
	}

	if (e.isOp(Op::Pow)) {
		const ast &exponent = *e.childAt(1);
		return exponent.isNumber() && exponent.num().isInteger() && exponent.num() >= 0 &&
			   is_polynomial(*e.childAt(0), x);
	}

	return false;
}

/*Fills p with the coefficients of the polynomial e in x, constant first, and returns its degree, or -1 if e is not a polynomial with rational coefficients of at most degree max*/
static int coefficients(const ast &e, const ast &x, num **p, unsigned max) {
	if (!is_polynomial(e, x))
		return -1;

	ast *remaining = e.copy(), *zero = integer(0);
	num factorial(1), index;
	int degree = -1;

	work::pause();
	expand(*remaining, Expand::All);
	simplify(*remaining, Simp::Basic);

	for (unsigned k = 0; !remaining->isInt(0); k++) {
		ast *value = nullptr;

		if (k <= max) {
			value = remaining->copy();
			substitute(*value, x, *zero);
			simplify(*value, Simp::Basic);
			p[k] = rational_value(*value);
			ast::dispose(value);
		}

		if (k > max || p[k] == nullptr) {
			if (k > 0)
				dispose_coefficients(p, k - 1);
			degree = -2;
			break;
		}

		if (k > 1) {
			mp_rat_set_value(&index, static_cast<mp_small>(k), 1);
			factorial *= index;
		}
		*p[k] /= factorial;
		degree = static_cast<int>(k);

		derivative(*remaining, x, x);
		simplify(*remaining, Simp::Basic);
	}

	work::resume();

	ast::dispose(remaining);
	ast::dispose(zero);

	if (degree == -2)
		return -1;

	if (degree == -1) {
		p[0] = num::from(0);
		degree = 0;
	}

	return degree;
}

/*Scales p to integer coefficients with no common factor and a positive leading coefficient*/
static void make_primitive(num **p, unsigned degree) {
	num scale(1), content, t;

	for (unsigned k = 0; k <= degree; k++)
		mp_int_lcm(MP_NUMER_P(&scale), MP_DENOM_P(p[k]), MP_NUMER_P(&scale));

	for (unsigned k = 0; k <= degree; k++) {
		*p[k] *= scale;
		if (*p[k] != 0)
			mp_int_gcd(MP_NUMER_P(&content), MP_NUMER_P(p[k]), MP_NUMER_P(&content));
	}

	if (*p[degree] < 0)
		mp_rat_neg(&content, &content);

	for (unsigned k = 0; k <= degree; k++)
		*p[k] /= content;
}

/*Sets root to the square root of q and returns true if it is rational*/
static bool exact_sqrt(const num &q, num &root) {
	num square;

	if (q < 0)
		return false;

	mp_int_sqrt(MP_NUMER_P(&q), MP_NUMER_P(&root));
	mp_int_sqrt(MP_DENOM_P(&q), MP_DENOM_P(&root));
	mp_rat_mul(&root, &root, &square);

	return square == q;
}

static void add_factor(Divisor *factors, unsigned *count, num *c0, num *c1, num *c2, unsigned multiplicity) {
	Divisor &f = factors[(*count)++];

	f.c[0] = c0;
	f.c[1] = c1;
	f.c[2] = c2;
	f.degree = c2 != nullptr ? 2 : 1;
	f.multiplicity = multiplicity;
	make_primitive(f.c, f.degree);
}

/*Adds x^2 - u as a factor*/
static void add_square_minus(Divisor *factors, unsigned *count, const num &u, unsigned multiplicity) {
	num *c0 = u.copy();

	mp_rat_neg(c0, c0);
	add_factor(factors, count, c0, num::from(0), num::from(1), multiplicity);
}

static void dispose_factors(Divisor *factors, unsigned count) {
	for (unsigned i = 0; i < count; i++)
		dispose_coefficients(factors[i].c, factors[i].degree);
}

/*Factors p of degree n into factors of degree 1 and 2 with integer coefficients and sets lead so that p is lead times their product. Returns how many factors there are, or 0 if p cannot be factored that way. Takes ownership of p.*/
static unsigned factor_polynomial(num **p, unsigned n, Divisor *factors, num &lead) {
	Root roots[MAX_DEGREE];
	unsigned root_count = 0, rest = n, count = 0;

	lead = *p[n];
	bool found = find_roots(p, &rest, roots, &root_count);

	for (unsigned i = 0; found && i < root_count; i++) {
		if (roots[i].value == nullptr)
			continue;

		num *c0 = roots[i].value->copy();
		mp_rat_neg(c0, c0);
		add_factor(factors, &count, c0, num::from(1), nullptr, roots[i].multiplicity);
	}

	if (found && rest == 2) {
		add_factor(factors, &count, p[0]->copy(), p[1]->copy(), p[2]->copy(), 1);
	} else if (found && rest == 4) {
		num d, s, t, u;

		mp_rat_mul(p[2], p[2], &d);
		mp_rat_mul(p[4], p[0], &t);
		mp_rat_set_value(&u, 4, 1);
		t *= u;
		d -= t;
		found = exact_sqrt(d, s);

		if (found) {
			mp_rat_add(p[4], p[4], &t);
			mp_rat_sub(&s, p[2], &u);
			u /= t;

			if (s == 0) {
				add_square_minus(factors, &count, u, 2);
			} else {
				add_square_minus(factors, &count, u, 1);
				mp_rat_neg(&s, &s);
				mp_rat_sub(&s, p[2], &u);
				u /= t;
				add_square_minus(factors, &count, u, 1);
			}
		}
	} else if (rest != 0) {
		found = false;
	}

	free_roots(roots, root_count);
	dispose_coefficients(p, rest);

	if (!found) {
		dispose_factors(factors, count);
		return 0;
	}

	for (unsigned i = 0; i < count; i++) {
		for (unsigned j = 0; j < factors[i].multiplicity; j++)
			lead /= *factors[i].c[factors[i].degree];
	}

	return count;
}

/*Sets product to a times b and returns its degree*/
static unsigned multiply(num *const *a, unsigned a_degree, num *const *b, unsigned b_degree, num **product) {
	num t;

	for (unsigned k = 0; k <= a_degree + b_degree; k++)
		product[k] = num::from(0);

	for (unsigned i = 0; i <= a_degree; i++) {
		for (unsigned j = 0; j <= b_degree; j++) {
			mp_rat_mul(a[i], b[j], &t);
			*product[i + j] += t;
		}
	}

	return a_degree + b_degree;
}

/*Sets p to the product of the factors, each to its multiplicity, except factor skip to its multiplicity minus power. Returns the degree of p.*/
static unsigned product_of(const Divisor *factors, unsigned count, unsigned skip, unsigned power, num **p) {
	num *next[MAX_DEGREE + 1];
	unsigned degree = 0;

	p[0] = num::from(1);

	for (unsigned i = 0; i < count; i++) {
		const unsigned exponent = factors[i].multiplicity - (i == skip ? power : 0);

		for (unsigned j = 0; j < exponent; j++) {
			const unsigned next_degree = multiply(p, degree, factors[i].c, factors[i].degree, next);
			dispose_coefficients(p, degree);
			degree = next_degree;
			for (unsigned k = 0; k <= degree; k++)
				p[k] = next[k];
		}
	}

	return degree;
}

/*Solves the size equations in the rows of m by Gauss-Jordan elimination, leaving the solution in the last column. Returns false if they have no unique solution.*/
static bool solve_system(num *(*m)[MAX_DEGREE + 1], unsigned size) {
	num t;

	for (unsigned column = 0; column < size; column++) {
		unsigned pivot = column;
		while (pivot < size && *m[pivot][column] == 0)
			pivot++;
		if (pivot == size)
			return false;

		for (unsigned k = 0; k <= size; k++) {
			num *swap = m[pivot][k];
			m[pivot][k] = m[column][k];
			m[column][k] = swap;
		}

		for (unsigned k = size + 1; k-- > column;)
			*m[column][k] /= *m[column][column];

		for (unsigned row = 0; row < size; row++) {
			if (row == column || *m[row][column] == 0)
				continue;

			for (unsigned k = size + 1; k-- > column;) {
				mp_rat_mul(m[row][column], m[column][k], &t);
				*m[row][k] -= t;
			}
		}
	}

	return true;
}

/*Fills unknowns with n letters that are not in e or x. Returns false if there are not enough.*/
static bool choose_unknowns(const ast &e, const ast &x, ast **unknowns, unsigned n) {
	const char *candidates = "ABCDFGHJKLNPQRSUVW";
	unsigned count = 0;

	for (; *candidates != '\0' && count < n; candidates++) {
		const Sym symbol = static_cast<Sym>(*candidates);
		if (!contains_symbol(e, symbol) && !contains_symbol(x, symbol))
			unknowns[count++] = ast::make(symbol);
	}

	if (count == n)
		return true;

	while (count > 0)
		ast::dispose(unknowns[--count]);

	return false;
}

static ast *factor_power(const Divisor &f, unsigned power, const ast &x) {
	const num one(1);
	ast *base = polynomial(f.c, f.degree, x, one);

	return power == 1 ? base : ast::make(Op::Pow, base, integer(power));
}

/*Returns lead times the product of the factors, each to its multiplicity*/
static ast *factored(const Divisor *factors, unsigned count, const num &lead, const ast &x) {
	ast *product = ast::make(Op::Mult);

	if (lead != 1)
		product->appendChild(ast::make(lead.copy()));

	for (unsigned i = 0; i < count; i++)
		product->appendChild(factor_power(factors[i], factors[i].multiplicity, x));

	return tidy(product);
}

/*Returns the numerator of the term with unknowns starting at u: u for a linear factor and ux + v for a quadratic one*/
static ast *term_numerator(const Divisor &f, ast *const *u, const ast &x) {
	if (f.degree == 1)
		return u[0]->copy();

	return ast::make(Op::Add, ast::make(Op::Mult, u[0]->copy(), x.copy()), u[1]->copy());
}

/*Returns the sum over the factors and their powers of the term numerators, given by the unknowns, over the powers*/
static ast *fraction_sum(const Divisor *factors, unsigned count, ast *const *unknowns, const ast &x) {
	ast *sum = ast::make(Op::Add);
	unsigned u = 0;

	for (unsigned i = 0; i < count; i++) {
		for (unsigned j = 1; j <= factors[i].multiplicity; j++) {
			sum->appendChild(
				ast::make(Op::Div, term_numerator(factors[i], unknowns + u, x), factor_power(factors[i], j, x))
			);
			u += factors[i].degree;
		}
	}

	return sum;
}

/*Sets q and r to the quotient and remainder of n of degree nd divided by d of degree dd, and returns the degree of q, or -1 if it is zero. Takes ownership of n.*/
static int divide(num **n, unsigned nd, num *const *d, unsigned dd, num **q, num **r) {
	num t;
	int qd = static_cast<int>(nd) - static_cast<int>(dd);

	for (int k = qd; k >= 0; k--) {
		q[k] = n[static_cast<unsigned>(k) + dd]->copy();
		*q[k] /= *d[dd];

		for (unsigned j = 0; j <= dd; j++) {
			mp_rat_mul(q[k], d[j], &t);
			*n[static_cast<unsigned>(k) + j] -= t;
		}
	}

	for (unsigned k = 0; k < dd; k++)
		r[k] = k <= nd ? n[k] : num::from(0);
	for (unsigned k = dd; k <= nd; k++)
		num::dispose(n[k]);

	return qd;
}

/*Records each nonzero row of m as an equation in the unknowns*/
static void record_rows(num *(*m)[MAX_DEGREE + 1], unsigned size, ast *const *unknowns) {
	bool first = true;

	for (unsigned row = size; row-- > 0;) {
		ast *left = ast::make(Op::Add);

		for (unsigned k = 0; k < size; k++) {
			if (*m[row][k] != 0)
				left->appendChild(ast::make(Op::Mult, ast::make(m[row][k]->copy()), unknowns[k]->copy()));
		}

		if (left->childCount() > 0) {
			ast *right = ast::make(m[row][size]->copy());
			work::step(work::Step::Type::Equation, first ? "Equate coefficients" : nullptr, tidy(left), right);
			ast::dispose(right);
			first = false;
		}

		ast::dispose(left);
	}
}

/*Returns the sum of the terms with the values of the unknowns, leaving out zero terms*/
static ast *result_sum(const Divisor *factors, unsigned count, ast *const *values, const ast &x) {
	ast *sum = ast::make(Op::Add);
	unsigned u = 0;

	for (unsigned i = 0; i < count; i++) {
		for (unsigned j = 1; j <= factors[i].multiplicity; j++) {
			ast *numerator = tidy(term_numerator(factors[i], values + u, x));
			u += factors[i].degree;

			if (numerator->isInt(0)) {
				ast::dispose(numerator);
				continue;
			}

			ast *term = ast::make(Op::Div, numerator, factor_power(factors[i], j, x));
			work::pause();
			simplify(*term, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval);
			work::resume();
			sum->appendChild(term);
		}
	}

	return sum;
}

/*Decomposes r/(lead times the product of the factors) and records the work. Returns the sum of the partial fractions.*/
static ast *decompose(
	const ast &e,
	num **r,
	unsigned size,
	const Divisor *factors,
	unsigned count,
	const num &lead,
	ast *denominator,
	const ast &x
) {
	ast *unknowns[MAX_DEGREE], *values[MAX_DEGREE];
	num *m[MAX_DEGREE][MAX_DEGREE + 1], *basis[MAX_DEGREE + 1];
	const num one(1);

	if (!choose_unknowns(e, x, unknowns, size))
		return nullptr;

	ast *remainder = polynomial(r, size - 1, x, one);
	ast *left = ast::make(Op::Div, remainder->copy(), denominator->copy());
	ast *form = fraction_sum(factors, count, unknowns, x);
	work::step(work::Step::Type::Equation, "Partial fractions", left, form);
	ast::dispose(left);
	ast::dispose(form);

	ast *combination = ast::make(Op::Add);
	unsigned column = 0;

	for (unsigned i = 0; i < count; i++) {
		for (unsigned j = 1; j <= factors[i].multiplicity; j++) {
			ast *rest = ast::make(Op::Mult);
			for (unsigned k = 0; k < count; k++) {
				const unsigned exponent = factors[k].multiplicity - (k == i ? j : 0);
				if (exponent > 0)
					rest->appendChild(factor_power(factors[k], exponent, x));
			}

			const unsigned degree = product_of(factors, count, i, j, basis);

			for (unsigned shift = factors[i].degree; shift-- > 0;) {
				for (unsigned k = 0; k < size; k++) {
					m[k][column] = k >= shift && k - shift <= degree ? basis[k - shift]->copy() : num::from(0);
				}

				ast *term = ast::make(Op::Mult);
				term->appendChild(unknowns[column]->copy());
				if (shift == 1)
					term->appendChild(x.copy());
				for (const ast &factor : rest->children())
					term->appendChild(factor.copy());
				combination->appendChild(term);
				column++;
			}

			dispose_coefficients(basis, degree);
			ast::dispose(rest);
		}
	}

	for (unsigned k = 0; k < size; k++) {
		m[k][size] = r[k]->copy();
		*m[k][size] /= lead;
	}

	left = ast::make(Op::Div, remainder, ast::make(lead.copy()));
	work::step(work::Step::Type::Equation, "Multiply by the denominator", tidy(left), tidy(combination));
	ast::dispose(left);
	ast::dispose(combination);

	record_rows(m, size, unknowns);

	ast *sum = nullptr;

	if (solve_system(m, size)) {
		for (unsigned k = 0; k < size; k++) {
			values[k] = ast::make(m[k][size]->copy());
			work::step(work::Step::Type::Equation, k == 0 ? "Solve" : nullptr, unknowns[k], values[k]);
		}

		sum = result_sum(factors, count, values, x);

		for (unsigned k = 0; k < size; k++)
			ast::dispose(values[k]);
	}

	for (unsigned k = 0; k < size; k++) {
		ast::dispose(unknowns[k]);
		dispose_coefficients(m[k], size);
	}

	return sum;
}

ast *partial_fractions(const ast &e, const ast &x) {
	num *n[MAX_NUMERATOR + 1], *d[MAX_DEGREE + 1], *q[MAX_NUMERATOR + 1], *r[MAX_DEGREE];
	ast *numerator, *denominator;
	Divisor factors[MAX_DEGREE];
	num lead;

	rational_parts(e, &numerator, &denominator);
	tidy(numerator);
	tidy(denominator);

	const int nd = coefficients(*numerator, x, n, MAX_NUMERATOR);
	const int dd = nd < 0 ? -1 : coefficients(*denominator, x, d, MAX_DEGREE);

	if (nd < 0 || dd < 1) {
		if (nd >= 0)
			dispose_coefficients(n, static_cast<unsigned>(nd));
		if (dd >= 0)
			dispose_coefficients(d, static_cast<unsigned>(dd));
		ast::dispose(numerator);
		ast::dispose(denominator);
		return nullptr;
	}

	const unsigned size = static_cast<unsigned>(dd);
	const int qd = divide(n, static_cast<unsigned>(nd), d, size, q, r);

	num *copy[MAX_DEGREE + 1];
	for (unsigned k = 0; k <= size; k++)
		copy[k] = d[k]->copy();
	const unsigned count = factor_polynomial(copy, size, factors, lead);

	int rd = static_cast<int>(size) - 1;
	while (rd >= 0 && *r[rd] == 0)
		rd--;

	const bool decomposed = count == 1 && (factors[0].multiplicity == 1 || rd < static_cast<int>(factors[0].degree));
	ast *result = nullptr;

	if (count > 0 && (!decomposed || qd >= 0)) {
		const num one(1);
		ast *quotient = qd >= 0 ? polynomial(q, static_cast<unsigned>(qd), x, one) : nullptr;
		ast *remainder = polynomial(r, size - 1, x, one);

		if (quotient != nullptr) {
			ast *right =
				ast::make(Op::Add, quotient->copy(), ast::make(Op::Div, remainder->copy(), denominator->copy()));
			work::step(work::Step::Type::Equation, "Divide", &e, tidy(right));
			ast::dispose(right);
		}

		ast *product = factored(factors, count, lead, x);
		if (!product->compare(*denominator))
			work::step(work::Step::Type::Equation, "Factor the denominator", denominator, product);

		ast *sum;
		if (rd < 0)
			sum = ast::make(Op::Add);
		else if (decomposed)
			sum = ast::make(Op::Add, ast::make(Op::Div, remainder->copy(), product->copy()));
		else
			sum = decompose(e, r, size, factors, count, lead, product, x);

		if (sum != nullptr) {
			if (quotient != nullptr)
				sum->insertChild(quotient, 0);
			else if (sum->childCount() == 0)
				sum->appendChild(integer(0));

			work::pause();
			simplify(*sum, Simp::Commutative);
			work::resume();

			result = sum;
			if (rd >= 0 && !decomposed)
				work::step(work::Step::Type::Equation, nullptr, &e, result);
		} else {
			ast::dispose(quotient);
		}

		ast::dispose(product);
		ast::dispose(remainder);
		dispose_factors(factors, count);
	}

	if (qd >= 0)
		dispose_coefficients(q, static_cast<unsigned>(qd));
	for (unsigned k = 0; k < size; k++)
		num::dispose(r[k]);
	dispose_coefficients(d, size);
	ast::dispose(numerator);
	ast::dispose(denominator);

	return result;
}
