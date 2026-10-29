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

/*A factor of a denominator with coefficients free of x, constant first, raised to multiplicity*/
struct SymbolicDivisor {
	ast *c[3];
	unsigned degree, multiplicity;
};

static void dispose_coefficients(num **p, unsigned degree) {
	for (unsigned k = 0; k <= degree; k++)
		num::dispose(p[k]);
}

static void dispose_coefficients(ast **c, unsigned degree) {
	for (unsigned k = 0; k <= degree; k++)
		ast::dispose(c[k]);
}

/*Writes e as one fraction in lowest terms and returns it*/
static ast *reduce(ast *e) {
	single_fraction(*e);
	return tidy(e);
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

/*Fills c with the coefficients of the polynomial e in x, constant first, and returns its degree, or -1 if e is not a polynomial of at most degree max*/
static int coefficients(const ast &e, const ast &x, ast **c, unsigned max) {
	if (!is_polynomial(e, x))
		return -1;

	ast *remaining = e.copy(), *zero = integer(0);
	num factorial(1), index;
	int degree = -1;

	work::pause();
	expand(*remaining, Expand::All);
	simplify(*remaining, Simp::Basic);

	for (unsigned k = 0; !remaining->isInt(0); k++) {
		if (k > max) {
			if (k > 0)
				dispose_coefficients(c, k - 1);
			degree = -2;
			break;
		}

		if (k > 1) {
			mp_rat_set_value(&index, static_cast<mp_small>(k), 1);
			factorial *= index;
		}

		c[k] = remaining->copy();
		substitute(*c[k], x, *zero);
		c[k] = ast::make(Op::Div, c[k], ast::make(factorial.copy()));
		simplify(*c[k], Simp::Basic);
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
		c[0] = integer(0);
		degree = 0;
	}

	return degree;
}

/*Fills p with the coefficients of the polynomial e in x, constant first, and returns its degree, or -1 if e is not a polynomial with rational coefficients of at most degree max*/
static int coefficients(const ast &e, const ast &x, num **p, unsigned max) {
	ast *c[MAX_NUMERATOR + 1];
	const int degree = coefficients(e, x, c, max);
	int converted = 0;

	while (converted <= degree && (p[converted] = rational_value(*c[converted])) != nullptr)
		converted++;

	if (degree >= 0)
		dispose_coefficients(c, static_cast<unsigned>(degree));

	if (converted > degree)
		return degree;

	while (converted-- > 0)
		num::dispose(p[converted]);

	return -1;
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

static ast *factor_power(const SymbolicDivisor &f, unsigned power, const ast &x) {
	ast *base = polynomial(f.c, f.degree, x);

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
static ast *term_numerator(unsigned degree, ast *const *u, const ast &x) {
	if (degree == 1)
		return u[0]->copy();

	return ast::make(Op::Add, ast::make(Op::Mult, u[0]->copy(), x.copy()), u[1]->copy());
}

/*Returns the sum over the factors and their powers of the term numerators, given by the unknowns, over the powers*/
template <typename Factor>
static ast *fraction_sum(const Factor *factors, unsigned count, ast *const *unknowns, const ast &x) {
	ast *sum = ast::make(Op::Add);
	unsigned u = 0;

	for (unsigned i = 0; i < count; i++) {
		for (unsigned j = 1; j <= factors[i].multiplicity; j++) {
			sum->appendChild(
				ast::make(Op::Div, term_numerator(factors[i].degree, unknowns + u, x), factor_power(factors[i], j, x))
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

/*Adds the term numerator/divisor^power, where numerator is slope x + constant. Takes ownership of slope and constant.*/
static void add_term(PartialFractions &fractions, const SymbolicDivisor &f, unsigned power, ast *slope, ast *constant) {
	PartialFractions::Term &term = fractions.terms[fractions.count++];

	term.numerator[0] = constant;
	term.numerator[1] = slope;
	for (unsigned k = 0; k <= f.degree; k++)
		term.divisor[k] = f.c[k]->copy();
	term.degree = f.degree;
	term.power = power;
}

static void add_term(
	PartialFractions &fractions,
	const Divisor &f,
	unsigned power,
	const num &slope,
	const num &constant
) {
	SymbolicDivisor divisor = {{}, f.degree, f.multiplicity};

	for (unsigned k = 0; k <= f.degree; k++)
		divisor.c[k] = ast::make(f.c[k]->copy());

	add_term(fractions, divisor, power, ast::make(slope.copy()), ast::make(constant.copy()));
	dispose_coefficients(divisor.c, divisor.degree);
}

static ast *term_ast(const PartialFractions::Term &term, const ast &x) {
	ast *numerator = reduce(polynomial(term.numerator, 1, x));
	ast *divisor = polynomial(term.divisor, term.degree, x);

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
	ast::dispose(quotient);

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
		} else if (
			fits && *dd + td <= static_cast<int>(MAX_DEGREE) && *nd + td <= static_cast<int>(MAX_NUMERATOR) &&
			tn + *dd <= static_cast<int>(MAX_NUMERATOR)
		) {
			const int a =
				static_cast<int>(multiply(n, static_cast<unsigned>(*nd), term_d, static_cast<unsigned>(td), first));
			const int b =
				static_cast<int>(multiply(term_n, static_cast<unsigned>(tn), d, static_cast<unsigned>(*dd), second));
			dispose_coefficients(n, static_cast<unsigned>(*nd));
			*nd = add_polynomials(first, a, second, b, n);
			dispose_coefficients(first, static_cast<unsigned>(a));
			dispose_coefficients(second, static_cast<unsigned>(b));

			const int degree =
				static_cast<int>(multiply(d, static_cast<unsigned>(*dd), term_d, static_cast<unsigned>(td), first));
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

/*Sets the sum of the fractions to the quotient plus the terms, and records that e equals it when show*/
static void collect_sum(PartialFractions &fractions, const ast &e, const ast &x, bool show) {
	fractions.sum = ast::make(Op::Add);

	if (fractions.quotient != nullptr)
		fractions.sum->appendChild(fractions.quotient->copy());
	for (unsigned i = 0; i < fractions.count; i++) {
		if (!fractions.terms[i].numerator[0]->isInt(0) || !fractions.terms[i].numerator[1]->isInt(0))
			fractions.sum->appendChild(term_ast(fractions.terms[i], x));
	}
	if (fractions.sum->childCount() == 0)
		fractions.sum->appendChild(integer(0));

	work::pause();
	simplify(*fractions.sum, Simp::Commutative);
	work::resume();

	if (show)
		work::step(work::Step::Type::Equation, nullptr, &e, fractions.sum);
}

/*Adds f to the factors, or adds its multiplicity to an equal factor, and adds its multiplicity to multiplicities. Takes ownership of the coefficients of f. Returns false if there are too many factors.*/
static bool merge_factor(SymbolicDivisor f, SymbolicDivisor *factors, unsigned *count, unsigned *multiplicities) {
	for (unsigned i = 0; i < *count; i++) {
		bool same = factors[i].degree == f.degree;
		for (unsigned k = 0; same && k <= f.degree; k++)
			same = factors[i].c[k]->compare(*f.c[k]);

		if (same) {
			multiplicities[i] += f.multiplicity;
			dispose_coefficients(f.c, f.degree);
			return true;
		}
	}

	if (*count == MAX_DEGREE) {
		dispose_coefficients(f.c, f.degree);
		return false;
	}

	multiplicities[*count] += f.multiplicity;
	factors[(*count)++] = f;
	return true;
}

/*True if e has a root or i, which a factor of a polynomial with these coefficients would carry*/
static bool irrational(const ast &e) {
	if (e.symbol() == Sym::Imag || e.isOp(Op::Root))
		return true;

	if (e.isOp(Op::Pow) && !(e.childAt(1)->isNumber() && e.childAt(1)->num().isInteger()))
		return true;

	if (e.isOperator()) {
		for (const ast &child : e.children()) {
			if (irrational(child))
				return true;
		}
	}

	return false;
}

/*Adds base^power, a polynomial in x, to the factors, splitting it into linear and quadratic factors and multiplying lead by its constant factor. Sets split if base was split. Returns false if it cannot be split that way.*/
static bool add_factors(
	const ast &base,
	unsigned power,
	const ast &x,
	SymbolicDivisor *factors,
	unsigned *count,
	unsigned *multiplicities,
	ast *lead,
	bool *split
) {
	ast *c[MAX_DEGREE + 1];
	num *p[MAX_DEGREE + 1];
	const int degree = coefficients(base, x, c, MAX_DEGREE);

	if (degree < 1) {
		if (degree == 0)
			ast::dispose(c[0]);
		return false;
	}

	int converted = 0;
	while (converted <= degree && (p[converted] = rational_value(*c[converted])) != nullptr)
		converted++;

	if (converted > degree) {
		Divisor divisors[MAX_DEGREE];
		num constant;
		const unsigned found = factor_polynomial(p, static_cast<unsigned>(degree), divisors, constant);
		bool fits = found > 0;
		*split |= found > 1 ||
				  (found == 1 && (divisors[0].multiplicity > 1 || static_cast<int>(divisors[0].degree) < degree));

		dispose_coefficients(c, static_cast<unsigned>(degree));
		lead->appendChild(ast::make(Op::Pow, ast::make(constant.copy()), integer(power)));

		for (unsigned i = 0; i < found && fits; i++) {
			SymbolicDivisor f = {{}, divisors[i].degree, divisors[i].multiplicity * power};
			for (unsigned k = 0; k <= f.degree; k++)
				f.c[k] = ast::make(divisors[i].c[k]->copy());
			fits = merge_factor(f, factors, count, multiplicities);
		}

		dispose_factors(divisors, found);
		return fits;
	}

	while (converted-- > 0)
		num::dispose(p[converted]);

	if (degree > 2) {
		dispose_coefficients(c, static_cast<unsigned>(degree));
		return false;
	}

	if (degree == 1)
		return merge_factor({{c[0], c[1]}, 1, power}, factors, count, multiplicities);

	ast *discriminant = difference(
		ast::make(Op::Pow, c[1]->copy(), integer(2)),
		ast::make(Op::Mult, integer(4), ast::make(Op::Mult, c[2]->copy(), c[0]->copy()))
	);
	ast *root = ast::make(Op::Pow, discriminant, ast::make(num::from(1, 2)));
	work::pause();
	simplify(*root, Simp::Basic);
	work::resume();

	if (irrational(*root)) {
		ast::dispose(root);
		return merge_factor({{c[0], c[1], c[2]}, 2, power}, factors, count, multiplicities);
	}

	bool fits = true;
	*split = true;
	for (int sign = -1; sign <= 1 && fits; sign += 2) {
		ast *shift = ast::make(Op::Add, c[1]->copy(), ast::make(Op::Mult, integer(sign), root->copy()));
		ast *constant = reduce(ast::make(Op::Div, shift, ast::make(Op::Mult, integer(2), c[2]->copy())));
		fits = merge_factor({{constant, integer(1)}, 1, power}, factors, count, multiplicities);
	}

	lead->appendChild(ast::make(Op::Pow, c[2], integer(power)));
	ast::dispose(c[0]);
	ast::dispose(c[1]);
	ast::dispose(root);

	return fits;
}

/*Returns the product of the factors, each to its multiplicity, leaving out factor skip*/
static ast *product_except(const SymbolicDivisor *factors, unsigned count, unsigned skip, const ast &x) {
	ast *product = ast::make(Op::Mult, integer(1));

	for (unsigned i = 0; i < count; i++) {
		if (i != skip)
			product->appendChild(factor_power(factors[i], factors[i].multiplicity, x));
	}

	return tidy(product);
}

/*Sets residue to the remainder of p of degree n divided by x^2 + slope x + constant, as residue[1]x + residue[0]. Takes ownership of p.*/
static void residue_of(ast **p, unsigned n, const ast &slope, const ast &constant, ast **residue) {
	for (unsigned k = n; k >= 2; k--) {
		p[k - 1] = reduce(difference(p[k - 1], ast::make(Op::Mult, slope.copy(), p[k]->copy())));
		p[k - 2] = reduce(difference(p[k - 2], ast::make(Op::Mult, constant.copy(), p[k])));
	}

	residue[0] = p[0];
	residue[1] = n >= 1 ? p[1] : integer(0);
}

/*Sets a to a times b modulo x^2 + slope x + constant, where both are linear*/
static void multiply_residues(ast **a, ast *const *b, const ast &slope, const ast &constant) {
	ast *square = ast::make(Op::Mult, a[1]->copy(), b[1]->copy());
	ast *linear = ast::make(
		Op::Add,
		ast::make(Op::Mult, a[1]->copy(), b[0]->copy()),
		difference(ast::make(Op::Mult, a[0]->copy(), b[1]->copy()), ast::make(Op::Mult, slope.copy(), square->copy()))
	);
	ast *constant_part =
		difference(ast::make(Op::Mult, a[0], b[0]->copy()), ast::make(Op::Mult, constant.copy(), square));

	ast::dispose(a[1]);
	a[1] = reduce(linear);
	a[0] = reduce(constant_part);
}

/*Finds the numerator of each term of r/(the product of the factors) by the cover-up method, with the constant of a linear factor (ax + b)^m from derivatives of r/(a^m times the other factors) at x = -b/a, and the numerator of a quadratic factor q from r divided by the other factors modulo q. Adds the terms and records the work. Returns false if a quadratic factor is repeated.*/
static bool cover_up(
	const ast &e,
	ast *const *r,
	unsigned size,
	const SymbolicDivisor *factors,
	unsigned count,
	const ast &denominator,
	const ast &x,
	PartialFractions &fractions
) {
	for (unsigned i = 0; i < count; i++) {
		if (factors[i].degree == 2 && factors[i].multiplicity > 1)
			return false;
	}

	ast *unknowns[MAX_DEGREE];
	if (!choose_unknowns(e, x, unknowns, size))
		return false;

	ast *remainder = polynomial(r, size - 1, x);
	ast *left = ast::make(Op::Div, remainder->copy(), denominator.copy());
	ast *form = fraction_sum(factors, count, unknowns, x);
	work::step(work::Step::Type::Equation, "Partial fractions", left, form);
	ast::dispose(left);
	ast::dispose(form);

	unsigned column = 0;

	for (unsigned i = 0; i < count; i++) {
		const SymbolicDivisor &f = factors[i];
		ast *other = product_except(factors, count, i, x);

		if (f.degree == 1) {
			ast *scale = ast::make(Op::Pow, f.c[1]->copy(), integer(f.multiplicity));
			ast *quotient = ast::make(Op::Div, remainder->copy(), ast::make(Op::Mult, other, scale));
			ast *root = reduce(negate(ast::make(Op::Div, f.c[0]->copy(), f.c[1]->copy())));
			ast *values[MAX_DEGREE];
			num factorial(1), index;

			for (unsigned k = 0; k < f.multiplicity; k++) {
				if (k > 0) {
					work::pause();
					derivative(*quotient, x, x);
					simplify(*quotient, Simp::Basic);
					work::resume();
					mp_rat_set_value(&index, static_cast<mp_small>(k), 1);
					factorial *= index;
				}

				ast *value = quotient->copy();
				work::pause();
				substitute(*value, x, *root);
				work::resume();
				values[k] = reduce(
					ast::make(
						Op::Mult,
						ast::make(Op::Div, value, ast::make(factorial.copy())),
						ast::make(Op::Pow, f.c[1]->copy(), integer(f.multiplicity - k))
					)
				);
			}

			for (unsigned power = 1; power <= f.multiplicity; power++)
				add_term(fractions, f, power, integer(0), values[f.multiplicity - power]);

			ast::dispose(quotient);
			ast::dispose(root);
		} else {
			ast *slope = reduce(ast::make(Op::Div, f.c[1]->copy(), f.c[2]->copy()));
			ast *constant = reduce(ast::make(Op::Div, f.c[0]->copy(), f.c[2]->copy()));
			ast *p[MAX_DEGREE], *residue[2], *divisor[2] = {integer(1), integer(0)};

			for (unsigned k = 0; k < size; k++)
				p[k] = r[k]->copy();
			residue_of(p, size - 1, *slope, *constant, residue);

			for (unsigned j = 0; j < count; j++) {
				if (j == i)
					continue;

				for (unsigned m = 0; m < factors[j].multiplicity; m++) {
					ast *c[3], *factor[2];
					for (unsigned k = 0; k <= factors[j].degree; k++)
						c[k] = factors[j].c[k]->copy();
					residue_of(c, factors[j].degree, *slope, *constant, factor);
					multiply_residues(divisor, factor, *slope, *constant);
					ast::dispose(factor[0]);
					ast::dispose(factor[1]);
				}
			}

			ast *determinant = reduce(
				ast::make(
					Op::Add,
					difference(
						ast::make(Op::Mult, ast::make(Op::Pow, divisor[1]->copy(), integer(2)), constant->copy()),
						ast::make(Op::Mult, divisor[0]->copy(), ast::make(Op::Mult, divisor[1]->copy(), slope->copy()))
					),
					ast::make(Op::Pow, divisor[0]->copy(), integer(2))
				)
			);
			ast *adjugate[2] = {
				reduce(difference(divisor[0], ast::make(Op::Mult, divisor[1]->copy(), slope->copy()))),
				reduce(negate(divisor[1])),
			};

			multiply_residues(residue, adjugate, *slope, *constant);
			add_term(
				fractions,
				f,
				1,
				reduce(ast::make(Op::Div, residue[1], determinant->copy())),
				reduce(ast::make(Op::Div, residue[0], determinant))
			);

			ast::dispose(adjugate[0]);
			ast::dispose(adjugate[1]);
			ast::dispose(slope);
			ast::dispose(constant);
			ast::dispose(other);
		}
	}

	for (unsigned i = 0; i < fractions.count; i++) {
		const PartialFractions::Term &term = fractions.terms[i];

		for (unsigned k = term.degree; k-- > 0;) {
			work::step(
				work::Step::Type::Equation, column == 0 ? "Solve" : nullptr, unknowns[column], term.numerator[k]
			);
			ast::dispose(unknowns[column++]);
		}
	}

	ast::dispose(remainder);
	return true;
}

/*Splits e over the factors of its denominators as written, by the cover-up method. Returns false if they are not linear or quadratic in x, or if e is not a quotient of polynomials.*/
static bool split_symbolic(const ast &e, const ast &x, PartialFractions &fractions) {
	const unsigned term_count = e.isOp(Op::Add) ? e.childCount() : 1;
	if (term_count > MAX_DEGREE)
		return false;

	SymbolicDivisor factors[MAX_DEGREE];
	unsigned multiplicities[MAX_DEGREE][MAX_DEGREE] = {}, count = 0, made = 0;
	ast *numerators[MAX_DEGREE], *written = nullptr;
	bool fits = true, split = false;

	for (; made < term_count && fits; made++) {
		ast *numerator, *denominator;
		rational_parts(e.isOp(Op::Add) ? *e.childAt(made) : e, &numerator, &denominator);
		tidy(numerator);
		tidy(denominator);

		ast *lead = ast::make(Op::Mult, integer(1));
		const unsigned parts = denominator->isOp(Op::Mult) ? denominator->childCount() : 1;
		fits = is_polynomial(*numerator, x);

		for (unsigned j = 0; j < parts && fits; j++) {
			const ast &factor = denominator->isOp(Op::Mult) ? *denominator->childAt(j) : *denominator;
			const ast *base = &factor;
			mp_small power = 1;

			if (factor.isOp(Op::Pow) && factor.childAt(1)->isNumber() && factor.childAt(1)->num().toInt(power) &&
				power > 0)
				base = factor.childAt(0);
			else
				power = 1;

			if (!involves(*base, x))
				lead->appendChild(factor.copy());
			else
				fits = add_factors(
					*base, static_cast<unsigned>(power), x, factors, &count, multiplicities[made], lead, &split
				);
		}

		numerators[made] = ast::make(Op::Mult, numerator, ast::make(Op::Pow, lead, integer(-1)));

		if (term_count == 1)
			written = denominator;
		else
			ast::dispose(denominator);
	}

	unsigned size = 0;
	for (unsigned i = 0; i < count; i++) {
		factors[i].multiplicity = 0;
		for (unsigned j = 0; j < made; j++) {
			if (multiplicities[j][i] > factors[i].multiplicity)
				factors[i].multiplicity = multiplicities[j][i];
		}
		size += factors[i].degree * factors[i].multiplicity;
	}

	ast *n[MAX_NUMERATOR + 1], *d[MAX_DEGREE + 1];
	ast *numerator = ast::make(Op::Add), *denominator = product_except(factors, count, count, x);
	int nd = -1, dd = -1;

	for (unsigned j = 0; j < made; j++) {
		ast *term = ast::make(Op::Mult, numerators[j]);
		for (unsigned i = 0; i < count; i++) {
			if (factors[i].multiplicity > multiplicities[j][i])
				term->appendChild(factor_power(factors[i], factors[i].multiplicity - multiplicities[j][i], x));
		}
		numerator->appendChild(term);
	}

	if (fits && count > 0 && size <= MAX_DEGREE) {
		nd = coefficients(*numerator, x, n, MAX_NUMERATOR);
		dd = nd < 0 ? -1 : coefficients(*denominator, x, d, MAX_DEGREE);
	}

	fits = nd >= 0 && dd >= 1;

	if (fits) {
		const unsigned divisor_degree = static_cast<unsigned>(dd);

		if (written != nullptr && split) {
			ast *product = ast::make(Op::Mult, numerators[0]->childAt(1)->childAt(0)->copy(), denominator->copy());
			work::step(work::Step::Type::Equation, "Factor the denominator", written, tidy(product));
			ast::dispose(product);
		}

		if (nd >= dd) {
			ast *q[MAX_NUMERATOR + 1];
			const unsigned quotient_degree = static_cast<unsigned>(nd - dd);

			for (unsigned k = quotient_degree + 1; k-- > 0;) {
				q[k] = reduce(ast::make(Op::Div, n[k + divisor_degree]->copy(), d[divisor_degree]->copy()));
				for (unsigned j = 0; j <= divisor_degree; j++)
					n[k + j] = reduce(difference(n[k + j], ast::make(Op::Mult, q[k]->copy(), d[j]->copy())));
			}

			fractions.quotient = polynomial(q, quotient_degree, x);
			dispose_coefficients(q, quotient_degree);
			for (unsigned k = divisor_degree; k <= static_cast<unsigned>(nd); k++)
				ast::dispose(n[k]);
		}

		for (int k = nd + 1; k < dd; k++)
			n[k] = integer(0);

		int rd = dd - 1;
		while (rd >= 0 && n[rd]->isInt(0))
			rd--;

		ast *remainder = polynomial(n, divisor_degree - 1, x);
		if (fractions.quotient != nullptr) {
			ast *right = ast::make(
				Op::Add, fractions.quotient->copy(), ast::make(Op::Div, remainder->copy(), denominator->copy())
			);
			work::step(work::Step::Type::Equation, "Divide", &e, tidy(right));
			ast::dispose(right);
		}
		ast::dispose(remainder);

		const bool decomposed =
			count == 1 && (factors[0].multiplicity == 1 || rd < static_cast<int>(factors[0].degree));

		if (rd >= 0 && decomposed) {
			add_term(
				fractions,
				factors[0],
				factors[0].multiplicity,
				factors[0].degree == 2 && divisor_degree > 1 ? n[1]->copy() : integer(0),
				n[0]->copy()
			);
		} else if (rd >= 0) {
			fits = cover_up(e, n, divisor_degree, factors, count, *denominator, x, fractions);
		}

		if (fits) {
			fractions.changed = fractions.quotient != nullptr || (rd >= 0 && !decomposed);
			collect_sum(fractions, e, x, rd >= 0 && !decomposed);
		}

		dispose_coefficients(n, divisor_degree - 1);
		dispose_coefficients(d, divisor_degree);
	} else {
		if (nd >= 0)
			dispose_coefficients(n, static_cast<unsigned>(nd));
		if (dd >= 0)
			dispose_coefficients(d, static_cast<unsigned>(dd));
	}

	for (unsigned i = 0; i < count; i++)
		dispose_coefficients(factors[i].c, factors[i].degree);
	ast::dispose(numerator);
	ast::dispose(denominator);
	ast::dispose(written);

	return fits;
}

bool split_fractions(const ast &e, const ast &x, PartialFractions &fractions) {
	num *n[MAX_NUMERATOR + 1], *d[MAX_DEGREE + 1], *r[MAX_DEGREE], *q[MAX_NUMERATOR + 1];
	ast *numerator, *denominator;
	Divisor factors[MAX_DEGREE];
	num lead;

	int nd, dd;

	if (!fraction_coefficients(e, x, n, &nd, d, &dd))
		return split_symbolic(e, x, fractions);

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
	const int quotient_degree = divide(n, static_cast<unsigned>(nd), d, size, q, r);

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
		ast *quotient = quotient_degree >= 0 ? polynomial(q, static_cast<unsigned>(quotient_degree), x, one) : nullptr;

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
			fractions.quotient = quotient;
			fractions.changed = quotient != nullptr || (rd >= 0 && !decomposed);
			collect_sum(fractions, e, x, rd >= 0 && !decomposed);
		} else {
			ast::dispose(quotient);
		}

		ast::dispose(product);
		ast::dispose(remainder);
		dispose_factors(factors, count);
	}

	for (unsigned k = 0; k < size; k++)
		num::dispose(r[k]);
	if (quotient_degree >= 0)
		dispose_coefficients(q, static_cast<unsigned>(quotient_degree));
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
