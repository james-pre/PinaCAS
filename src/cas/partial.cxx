#include "cas.hxx"

#include "de/internal.hxx"

#include "../work.hxx"

/*Highest degree of a denominator*/
constexpr unsigned MAX_DEGREE = PartialFractions::max_degree;
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

/*Adds the term numerator/divisor^power, where numerator is slope x + constant*/
static void add_term(
	PartialFractions &fractions,
	const Divisor &f,
	unsigned power,
	const num &slope,
	const num &constant
) {
	PartialFractions::Term &term = fractions.terms[fractions.count++];

	term.numerator[0] = constant.copy();
	term.numerator[1] = slope.copy();
	for (unsigned k = 0; k <= f.degree; k++)
		term.divisor[k] = f.c[k]->copy();
	term.degree = f.degree;
	term.power = power;
}

static ast *term_ast(const PartialFractions::Term &term, const ast &x) {
	const num one(1);
	ast *numerator = polynomial(term.numerator, 1, x, one);
	ast *divisor = polynomial(term.divisor, term.degree, x, one);

	if (term.power > 1)
		divisor = ast::make(Op::Pow, divisor, integer(term.power));

	ast *fraction = ast::make(Op::Div, numerator, divisor);
	work::pause();
	simplify(*fraction, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval);
	work::resume();

	return fraction;
}

/*Decomposes r/(lead times the product of the factors) into terms and records the work. Returns false if the constants cannot be found.*/
static bool decompose(
	const ast &e,
	num **r,
	unsigned size,
	const Divisor *factors,
	unsigned count,
	const num &lead,
	ast *denominator,
	const ast &x,
	PartialFractions &fractions
) {
	ast *unknowns[MAX_DEGREE];
	num *m[MAX_DEGREE][MAX_DEGREE + 1], *basis[MAX_DEGREE + 1];
	const num one(1), zero;

	if (!choose_unknowns(e, x, unknowns, size))
		return false;

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

	const bool solved = solve_system(m, size);

	if (solved) {
		for (unsigned k = 0; k < size; k++) {
			ast *value = ast::make(m[k][size]->copy());
			work::step(work::Step::Type::Equation, k == 0 ? "Solve" : nullptr, unknowns[k], value);
			ast::dispose(value);
		}

		column = 0;
		for (unsigned i = 0; i < count; i++) {
			for (unsigned j = 1; j <= factors[i].multiplicity; j++) {
				if (factors[i].degree == 1)
					add_term(fractions, factors[i], j, zero, *m[column][size]);
				else
					add_term(fractions, factors[i], j, *m[column][size], *m[column + 1][size]);
				column += factors[i].degree;
			}
		}
	}

	for (unsigned k = 0; k < size; k++) {
		ast::dispose(unknowns[k]);
		dispose_coefficients(m[k], size);
	}

	return solved;
}

PartialFractions::~PartialFractions() {
	if (quotient_degree >= 0)
		dispose_coefficients(quotient, static_cast<unsigned>(quotient_degree));

	for (unsigned i = 0; i < count; i++) {
		dispose_coefficients(terms[i].numerator, 1);
		dispose_coefficients(terms[i].divisor, terms[i].degree);
	}

	ast::dispose(sum);
}

static bool same_polynomial(num *const *a, int a_degree, num *const *b, int b_degree) {
	if (a_degree != b_degree)
		return false;

	for (int k = 0; k <= a_degree; k++) {
		if (*a[k] != *b[k])
			return false;
	}

	return true;
}

/*Sets sum to a plus b and returns its degree*/
static int add_polynomials(num *const *a, int a_degree, num *const *b, int b_degree, num **sum) {
	const int degree = a_degree > b_degree ? a_degree : b_degree;

	for (int k = 0; k <= degree; k++) {
		sum[k] = num::from(0);
		if (k <= a_degree)
			*sum[k] += *a[k];
		if (k <= b_degree)
			*sum[k] += *b[k];
	}

	return degree;
}

/*Fills n and d with the coefficients of a numerator and denominator of e, adding the terms of a sum over a common denominator when they share one. Returns false if e is not a quotient of polynomials within the degree limits.*/
static bool fraction_coefficients(const ast &e, const ast &x, num **n, int *nd, num **d, int *dd) {
	const unsigned count = e.isOp(Op::Add) ? e.childCount() : 1;
	num *term_n[MAX_NUMERATOR + 1], *term_d[MAX_DEGREE + 1], *first[MAX_NUMERATOR + 1], *second[MAX_NUMERATOR + 1];

	*nd = 0;
	*dd = 0;
	n[0] = num::from(0);
	d[0] = num::from(1);

	for (unsigned i = 0; i < count; i++) {
		ast *numerator, *denominator;
		rational_parts(e.isOp(Op::Add) ? *e.childAt(i) : e, &numerator, &denominator);
		tidy(numerator);
		tidy(denominator);

		const int tn = coefficients(*numerator, x, term_n, MAX_NUMERATOR);
		const int td = tn < 0 ? -1 : coefficients(*denominator, x, term_d, MAX_DEGREE);
		ast::dispose(numerator);
		ast::dispose(denominator);

		bool fits = tn >= 0 && td >= 0;

		if (fits && same_polynomial(term_d, td, d, *dd)) {
			const int degree = add_polynomials(n, *nd, term_n, tn, first);
			dispose_coefficients(n, static_cast<unsigned>(*nd));
			for (int k = 0; k <= degree; k++)
				n[k] = first[k];
			*nd = degree;
		} else if (fits && *dd + td <= static_cast<int>(MAX_DEGREE) &&
				   *nd + td <= static_cast<int>(MAX_NUMERATOR) && tn + *dd <= static_cast<int>(MAX_NUMERATOR)) {
			const int a = static_cast<int>(multiply(n, static_cast<unsigned>(*nd), term_d, static_cast<unsigned>(td), first));
			const int b =
				static_cast<int>(multiply(term_n, static_cast<unsigned>(tn), d, static_cast<unsigned>(*dd), second));
			dispose_coefficients(n, static_cast<unsigned>(*nd));
			*nd = add_polynomials(first, a, second, b, n);
			dispose_coefficients(first, static_cast<unsigned>(a));
			dispose_coefficients(second, static_cast<unsigned>(b));

			const int degree = static_cast<int>(multiply(d, static_cast<unsigned>(*dd), term_d, static_cast<unsigned>(td), first));
			dispose_coefficients(d, static_cast<unsigned>(*dd));
			for (int k = 0; k <= degree; k++)
				d[k] = first[k];
			*dd = degree;
		} else {
			fits = false;
		}

		if (tn >= 0)
			dispose_coefficients(term_n, static_cast<unsigned>(tn));
		if (td >= 0)
			dispose_coefficients(term_d, static_cast<unsigned>(td));

		if (!fits) {
			dispose_coefficients(n, static_cast<unsigned>(*nd));
			dispose_coefficients(d, static_cast<unsigned>(*dd));
			return false;
		}
	}

	while (*nd > 0 && *n[*nd] == 0)
		num::dispose(n[(*nd)--]);

	return true;
}

bool split_fractions(const ast &e, const ast &x, PartialFractions &fractions) {
	num *n[MAX_NUMERATOR + 1], *d[MAX_DEGREE + 1], *r[MAX_DEGREE];
	ast *numerator, *denominator;
	Divisor factors[MAX_DEGREE];
	num lead;

	int nd, dd;

	if (!fraction_coefficients(e, x, n, &nd, d, &dd))
		return false;

	if (dd < 1) {
		dispose_coefficients(n, static_cast<unsigned>(nd));
		dispose_coefficients(d, static_cast<unsigned>(dd));
		return false;
	}

	const num one(1);
	if (e.isOp(Op::Add)) {
		numerator = polynomial(n, static_cast<unsigned>(nd), x, one);
		denominator = polynomial(d, static_cast<unsigned>(dd), x, one);
	} else {
		rational_parts(e, &numerator, &denominator);
		tidy(numerator);
		tidy(denominator);
	}

	const unsigned size = static_cast<unsigned>(dd);
	fractions.quotient_degree = divide(n, static_cast<unsigned>(nd), d, size, fractions.quotient, r);

	num *copy[MAX_DEGREE + 1];
	for (unsigned k = 0; k <= size; k++)
		copy[k] = d[k]->copy();
	const unsigned count = factor_polynomial(copy, size, factors, lead);

	int rd = static_cast<int>(size) - 1;
	while (rd >= 0 && *r[rd] == 0)
		rd--;

	const bool decomposed = count == 1 && (factors[0].multiplicity == 1 || rd < static_cast<int>(factors[0].degree));
	bool split = count > 0;

	if (split) {
		const num one(1);
		ast *remainder = polynomial(r, size - 1, x, one);
		ast *quotient = fractions.quotient_degree >= 0
							? polynomial(fractions.quotient, static_cast<unsigned>(fractions.quotient_degree), x, one)
							: nullptr;

		if (quotient != nullptr) {
			ast *right =
				ast::make(Op::Add, quotient->copy(), ast::make(Op::Div, remainder->copy(), denominator->copy()));
			work::step(work::Step::Type::Equation, "Divide", &e, tidy(right));
			ast::dispose(right);
		}

		ast *product = factored(factors, count, lead, x);
		if (!product->compare(*denominator))
			work::step(work::Step::Type::Equation, "Factor the denominator", denominator, product);

		if (rd >= 0 && decomposed) {
			num slope, constant = *r[0];
			if (factors[0].degree == 2 && size > 1)
				slope = *r[1];
			slope /= lead;
			constant /= lead;
			add_term(fractions, factors[0], factors[0].multiplicity, slope, constant);
		} else if (rd >= 0) {
			split = decompose(e, r, size, factors, count, lead, product, x, fractions);
		}

		if (split) {
			fractions.changed = quotient != nullptr || (rd >= 0 && !decomposed);
			fractions.sum = ast::make(Op::Add);

			if (quotient != nullptr)
				fractions.sum->appendChild(quotient);
			for (unsigned i = 0; i < fractions.count; i++) {
				if (*fractions.terms[i].numerator[0] != 0 || *fractions.terms[i].numerator[1] != 0)
					fractions.sum->appendChild(term_ast(fractions.terms[i], x));
			}
			if (fractions.sum->childCount() == 0)
				fractions.sum->appendChild(integer(0));

			work::pause();
			simplify(*fractions.sum, Simp::Commutative);
			work::resume();

			if (rd >= 0 && !decomposed)
				work::step(work::Step::Type::Equation, nullptr, &e, fractions.sum);
		} else {
			ast::dispose(quotient);
		}

		ast::dispose(product);
		ast::dispose(remainder);
		dispose_factors(factors, count);
	}

	for (unsigned k = 0; k < size; k++)
		num::dispose(r[k]);
	dispose_coefficients(d, size);
	ast::dispose(numerator);
	ast::dispose(denominator);

	return split;
}

ast *partial_fractions(const ast &e, const ast &x) {
	PartialFractions fractions;

	if (!split_fractions(e, x, fractions) || !fractions.changed)
		return nullptr;

	ast *sum = fractions.sum;
	fractions.sum = nullptr;
	return sum;
}
