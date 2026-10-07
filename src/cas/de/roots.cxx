#include "internal.hxx"

#include "../../work.hxx"

#include <limits.h>

static const char *multiplicity_names[] = {nullptr, nullptr, "Double root", "Triple root"};

static ast *integer_node(mp_int z) {
	mp_rat n = num_FromInt(0);

	mp_int_copy(z, MP_NUMER_P(n));

	return ast::make(n);
}

mp_rat rational_value(const ast *e) {
	if (e->isNumber())
		return num_Copy(e->num());

	if (!e->isOp(Op::Div) && !e->isOp(Op::Mult))
		return nullptr;

	mp_rat value = rational_value(e->firstChild());
	if (value == nullptr)
		return nullptr;

	for (const ast *child = e->firstChild()->next(); child != nullptr; child = child->next()) {
		mp_rat factor = rational_value(child);
		if (factor == nullptr) {
			num_Cleanup(value);
			return nullptr;
		}

		if (e->isOp(Op::Div))
			mp_rat_div(value, factor, value);
		else
			mp_rat_mul(value, factor, value);
		num_Cleanup(factor);
	}

	return value;
}

void evaluate(mp_rat *p, unsigned n, mp_rat r, mp_rat value) {
	mp_rat_copy(p[n], value);

	for (unsigned k = n; k-- > 0;) {
		mp_rat_mul(value, r, value);
		mp_rat_add(value, p[k], value);
	}
}

bool is_root(mp_rat *p, unsigned n, mp_rat r) {
	mp_rat value = num_FromInt(0);

	evaluate(p, n, r, value);
	const bool root = mp_rat_compare_zero(value) == 0;
	num_Cleanup(value);

	return root;
}

void deflate(mp_rat *p, unsigned n, mp_rat r) {
	mp_rat t = num_FromInt(0);

	for (unsigned k = n; k-- > 0;) {
		mp_rat_mul(p[k + 1], r, t);
		mp_rat_add(p[k], t, p[k]);
	}

	num_Cleanup(p[0]);
	num_Cleanup(t);

	for (unsigned k = 0; k < n; k++)
		p[k] = p[k + 1];
	p[n] = nullptr;
}

/*Sets lead and constant to the absolute values of the end coefficients of p scaled to integers. Returns false if they do not fit.*/
static bool integer_ends(mp_rat *p, unsigned n, mp_small *lead, mp_small *constant) {
	mp_rat scale = num_FromInt(1), t = num_FromInt(0);

	for (unsigned k = 0; k <= n; k++)
		mp_int_lcm(MP_NUMER_P(scale), MP_DENOM_P(p[k]), MP_NUMER_P(scale));

	mp_rat_mul(p[n], scale, t);
	bool fits = mp_int_to_int(MP_NUMER_P(t), lead) == MP_OK;
	mp_rat_mul(p[0], scale, t);
	fits = fits && mp_int_to_int(MP_NUMER_P(t), constant) == MP_OK;

	num_Cleanup(scale);
	num_Cleanup(t);

	if (*lead < 0)
		*lead = -*lead;
	if (*constant < 0)
		*constant = -*constant;

	return fits;
}

/*True if a/b or -a/b is a root of p, setting root to it*/
static bool test_candidate(mp_rat *p, unsigned n, mp_small a, mp_small b, mp_rat root) {
	mp_rat_set_value(root, a, b);
	if (is_root(p, n, root))
		return true;

	mp_rat_neg(root, root);
	return is_root(p, n, root);
}

/*Finds a rational root a/b of p, with a dividing the constant term and b the leading coefficient*/
static bool rational_root(mp_rat *p, unsigned n, mp_rat root) {
	mp_small lead, constant;

	if (!integer_ends(p, n, &lead, &constant))
		return false;

	for (mp_small a = 1; a <= constant / a; a++) {
		if (constant % a != 0)
			continue;

		for (mp_small b = 1; b <= lead / b; b++) {
			if (lead % b == 0 &&
				(test_candidate(p, n, a, b, root) || test_candidate(p, n, a, lead / b, root) ||
				 test_candidate(p, n, constant / a, b, root) || test_candidate(p, n, constant / a, lead / b, root)))
				return true;
		}
	}

	return false;
}

/*Sets root to the square root of q, which is not negative, and returns true if it is rational*/
static bool rational_sqrt(mp_rat q, mp_rat root) {
	mp_rat square = num_FromInt(0);

	mp_int_sqrt(MP_NUMER_P(q), MP_NUMER_P(root));
	mp_int_sqrt(MP_DENOM_P(q), MP_DENOM_P(root));
	mp_rat_mul(root, root, square);
	const bool exact = mp_rat_compare(square, q) == 0;
	num_Cleanup(square);

	return exact;
}

/*Returns the square root of q, which is positive, as a rational multiple of the square root of a square-free integer*/
static ast *square_root(mp_rat q) {
	mp_rat root = num_FromInt(0);

	if (rational_sqrt(q, root))
		return ast::make(root);

	num_Cleanup(root);

	mp_small numerator, denominator;
	if (mp_rat_to_ints(q, &numerator, &denominator) != MP_OK || numerator > LONG_MAX / denominator)
		return ast::make(Op::Pow, ast::make(num_Copy(q)), ast::make(num_FromFraction(1, 2)));

	mp_small radicand = numerator * denominator, scale = 1;

	for (mp_small k = 2; k <= radicand / k; k++) {
		while (radicand % (k * k) == 0) {
			radicand /= k * k;
			scale *= k;
		}
	}

	return ast::make(
		Op::Mult,
		ast::make(num_FromFraction(scale, denominator)),
		ast::make(Op::Pow, integer(radicand), ast::make(num_FromFraction(1, 2)))
	);
}

/*Records a root, merging it with an equal real root. Takes ownership of re and im.*/
static void add_root(root_t *roots, unsigned *count, ast *re, ast *im, mp_rat value, unsigned multiplicity) {
	work::pause();
	simplify(re, Simp::Basic);
	if (im != nullptr)
		simplify(im, Simp::Basic);
	work::resume();

	for (unsigned i = 0; i < *count; i++) {
		if (im == nullptr && roots[i].im == nullptr && roots[i].re->compare(*re)) {
			roots[i].multiplicity += multiplicity;
			ast::dispose(re);
			return;
		}
	}

	roots[*count].re = re;
	roots[*count].im = im;
	roots[*count].value = value != nullptr ? num_Copy(value) : nullptr;
	roots[*count].multiplicity = multiplicity;
	(*count)++;
}

/*Records the rational root r of p and divides it out*/
static void divide_root(mp_rat *p, unsigned *n, mp_rat r, root_t *roots, unsigned *count) {
	deflate(p, (*n)--, r);
	add_root(roots, count, ast::make(num_Copy(r)), nullptr, r, 1);
}

/*Sets d to b^2 - 4ac and v to -b/(2a)*/
static void discriminant(mp_rat a, mp_rat b, mp_rat c, mp_rat d, mp_rat v) {
	mp_rat t = num_FromInt(4);

	mp_rat_mul(t, a, t);
	mp_rat_mul(t, c, t);
	mp_rat_mul(b, b, d);
	mp_rat_sub(d, t, d);

	mp_rat_add(a, a, t);
	mp_rat_div(b, t, v);
	mp_rat_neg(v, v);

	num_Cleanup(t);
}

/*Records the roots v ± sqrt(d)/(2a), real when d is positive and complex otherwise*/
static void add_irrational_roots(mp_rat v, mp_rat d, mp_rat a, unsigned multiplicity, root_t *roots, unsigned *count) {
	mp_rat magnitude = num_Copy(d), denominator = num_FromInt(0);
	const bool real = mp_rat_compare_zero(d) > 0;

	mp_rat_abs(magnitude, magnitude);
	mp_rat_add(a, a, denominator);
	mp_rat_abs(denominator, denominator);
	ast *w = ast::make(Op::Div, square_root(magnitude), ast::make(denominator));

	if (real) {
		add_root(roots, count, ast::make(Op::Add, ast::make(num_Copy(v)), w->copy()), nullptr, nullptr, multiplicity);
		add_root(roots, count, difference(ast::make(num_Copy(v)), w), nullptr, nullptr, multiplicity);
	} else {
		add_root(roots, count, ast::make(num_Copy(v)), w, nullptr, multiplicity);
	}

	num_Cleanup(magnitude);
}

/*Finds the roots of the quadratic p with the quadratic formula, dividing out the rational ones*/
static void quadratic(mp_rat *p, unsigned *n, root_t *roots, unsigned *count) {
	mp_rat d = num_FromInt(0), v = num_FromInt(0), s = num_FromInt(0), r = num_FromInt(0);

	discriminant(p[2], p[1], p[0], d, v);

	if (mp_rat_compare_zero(d) >= 0 && rational_sqrt(d, s)) {
		mp_rat_add(p[2], p[2], r);
		mp_rat_div(s, r, s);
		mp_rat_add(v, s, r);
		divide_root(p, n, r, roots, count);
		mp_rat_sub(v, s, r);
		divide_root(p, n, r, roots, count);
	} else {
		add_irrational_roots(v, d, p[2], 1, roots, count);
	}

	num_Cleanup(d);
	num_Cleanup(v);
	num_Cleanup(s);
	num_Cleanup(r);
}

/*Finds the roots of am^4 + bm^2 + c as the square roots of the roots of au^2 + bu + c. Returns false unless those are rational.*/
static bool biquadratic(mp_rat *p, root_t *roots, unsigned *count) {
	mp_rat d = num_FromInt(0), v = num_FromInt(0), s = num_FromInt(0), zero = num_FromInt(0), one = num_FromInt(1);
	mp_rat u[2] = {num_FromInt(0), num_FromInt(0)};
	bool rational = mp_rat_compare_zero(p[1]) == 0 && mp_rat_compare_zero(p[3]) == 0;

	if (rational) {
		discriminant(p[4], p[2], p[0], d, v);
		rational = mp_rat_compare_zero(d) >= 0 && rational_sqrt(d, s);
	}

	if (rational) {
		mp_rat_add(p[4], p[4], d);
		mp_rat_div(s, d, s);
		mp_rat_add(v, s, u[0]);
		mp_rat_sub(v, s, u[1]);
		const unsigned multiplicity = mp_rat_compare_zero(s) == 0 ? 2 : 1;

		for (unsigned i = 0; i < 3 - multiplicity; i++) {
			mp_rat_set_value(d, 4, 1);
			mp_rat_mul(d, u[i], d);
			add_irrational_roots(zero, d, one, multiplicity, roots, count);
		}
	}

	num_Cleanup(d);
	num_Cleanup(v);
	num_Cleanup(s);
	num_Cleanup(zero);
	num_Cleanup(one);
	num_Cleanup(u[0]);
	num_Cleanup(u[1]);

	return rational;
}

bool find_roots(mp_rat *p, unsigned *n, root_t *roots, unsigned *count) {
	mp_rat r = num_FromInt(0);
	bool found = true;

	while (*n > 0 && mp_rat_compare_zero(p[0]) == 0)
		divide_root(p, n, r, roots, count);

	while (*n >= 3 && rational_root(p, *n, r))
		divide_root(p, n, r, roots, count);

	if (*n == 1) {
		mp_rat_div(p[0], p[1], r);
		mp_rat_neg(r, r);
		divide_root(p, n, r, roots, count);
	} else if (*n == 2) {
		quadratic(p, n, roots, count);
	} else if (*n == 4) {
		found = biquadratic(p, roots, count);
	} else {
		found = *n == 0;
	}

	num_Cleanup(r);

	return found;
}

ast *polynomial(mp_rat *p, unsigned n, const ast *m, mp_rat divisor) {
	ast *sum = ast::make(Op::Add);

	for (unsigned k = n + 1; k-- > 0;) {
		if (mp_rat_compare_zero(p[k]) == 0)
			continue;

		mp_rat c = num_Copy(p[k]);
		mp_rat_div(c, divisor, c);

		ast *term;
		if (k == 0) {
			term = ast::make(c);
		} else {
			term = k == 1 ? m->copy() : ast::make(Op::Pow, m->copy(), integer(k));

			if (num_IsInt(c, 1))
				num_Cleanup(c);
			else
				term = ast::make(Op::Mult, ast::make(c), term);
		}

		sum->appendChild(term);
	}

	return tidy(sum);
}

ast *factored_form(mp_rat *p, unsigned n, const root_t *roots, unsigned count, const ast *m) {
	ast *product = ast::make(Op::Mult);
	mp_rat scale = num_FromInt(1), t = num_FromInt(0);

	for (unsigned i = 0; i < count; i++) {
		mp_rat r = roots[i].value;
		if (r == nullptr)
			continue;

		ast *factor = difference(
			mp_int_compare_value(MP_DENOM_P(r), 1) == 0 ? m->copy()
														: ast::make(Op::Mult, integer_node(MP_DENOM_P(r)), m->copy()),
			integer_node(MP_NUMER_P(r))
		);

		if (roots[i].multiplicity > 1)
			factor = ast::make(Op::Pow, factor, integer(roots[i].multiplicity));

		product->appendChild(factor);

		mp_rat_set_value(t, 1, 1);
		mp_int_copy(MP_DENOM_P(r), MP_NUMER_P(t));
		for (unsigned k = 0; k < roots[i].multiplicity; k++)
			mp_rat_mul(scale, t, scale);
	}

	ast *rest = polynomial(p, n, m, scale);

	if (n > 0)
		product->appendChild(rest);
	else if (!rest->isInt(1))
		product->insertChild(rest, 0);
	else
		ast::dispose(rest);

	num_Cleanup(scale);
	num_Cleanup(t);

	return tidy(product);
}

static void record_roots(const root_t *roots, unsigned count, const ast *m) {
	for (unsigned i = 0; i < count; i++) {
		const char *text = roots[i].multiplicity < 4 ? multiplicity_names[roots[i].multiplicity] : "Repeated root";

		if (roots[i].im == nullptr) {
			work::step(work::Step::Type::Equation, text, m, roots[i].re);
			continue;
		}

		for (int sign = 1; sign >= -1; sign -= 2) {
			ast *imaginary = ast::make(Op::Mult, roots[i].im->copy(), ast::make(Sym::Imag));
			if (sign < 0)
				imaginary = negate(imaginary);

			ast *value = roots[i].re->isInt(0) ? imaginary : ast::make(Op::Add, roots[i].re->copy(), imaginary);
			work::step(work::Step::Type::Equation, sign > 0 ? text : nullptr, m, tidy(value));
			ast::dispose(value);
		}
	}
}

ast *basis_function(const DiffEq *de, unsigned j, const ast *r, ast *f) {
	ast *product = ast::make(Op::Mult);

	product->appendChild(integer(1));

	if (j > 0)
		product->appendChild(ast::make(Op::Pow, de->x->copy(), integer(j)));

	if (!r->isInt(0)) {
		product->appendChild(ast::make(Op::Pow, ast::make(Sym::Euler), ast::make(Op::Mult, r->copy(), de->x->copy())));
	}

	if (f != nullptr)
		product->appendChild(f);

	work::pause();
	simplify(product, Simp::Basic);
	work::resume();

	return product;
}

unsigned fill_basis(const DiffEq *de, const root_t *roots, unsigned count, ast **basis) {
	unsigned n = 0;

	for (unsigned i = 0; i < count; i++) {
		const root_t *root = &roots[i];

		for (unsigned j = 0; j < root->multiplicity; j++) {
			if (root->im == nullptr) {
				basis[n++] = basis_function(de, j, root->re, nullptr);
			} else {
				basis[n++] = basis_function(
					de, j, root->re, ast::make(Op::Cos, ast::make(Op::Mult, root->im->copy(), de->x->copy()))
				);
				basis[n++] = basis_function(
					de, j, root->re, ast::make(Op::Sin, ast::make(Op::Mult, root->im->copy(), de->x->copy()))
				);
			}
		}
	}

	return n;
}

/*Finds the roots of a2m^2 + a0 = 0 as ±i*sqrt(a0/a2), assuming that a0/a2 is positive, when the coefficients are constants that are not all rational*/
static Error oscillator_roots(DiffEq *de, const ast *m, root_t *roots, unsigned *count) {
	if (de->order != 2 || !de->a[1]->isInt(0) || involves(de->a[0], de->x) || involves(de->a[2], de->x))
		return Error::DeUnsolved;

	ast *left = ast::make(
		Op::Add, ast::make(Op::Mult, de->a[2]->copy(), ast::make(Op::Pow, m->copy(), integer(2))), de->a[0]->copy()
	);
	ast *zero = integer(0);
	work::step(work::Step::Type::Equation, "Characteristic equation", tidy(left), zero);
	ast::dispose(left);
	ast::dispose(zero);

	add_root(
		roots,
		count,
		integer(0),
		ast::make(Op::Pow, ast::make(Op::Div, de->a[0]->copy(), de->a[2]->copy()), ast::make(num_FromFraction(1, 2))),
		nullptr,
		1
	);
	record_roots(roots, *count, m);

	return Error::Success;
}

Error characteristic_roots(DiffEq *de, const ast *m, root_t *roots, unsigned *count) {
	mp_rat p[DiffEq::max_order + 1];
	unsigned n = de->order;

	*count = 0;
	canonical_SetFunction(m->symbol());

	for (unsigned k = 0; k <= de->order; k++) {
		if ((p[k] = rational_value(de->a[k])) == nullptr) {
			while (k > 0)
				num_Cleanup(p[--k]);

			const Error err = oscillator_roots(de, m, roots, count);
			canonical_SetFunction(de->y->symbol());
			return err;
		}
	}

	ast *zero = integer(0);
	mp_rat one = num_FromInt(1);

	ast *left = polynomial(p, n, m, one);
	work::step(work::Step::Type::Equation, "Characteristic equation", left, zero);
	ast::dispose(left);

	Error err = Error::Success;
	if (!find_roots(p, &n, roots, count))
		err = Error::DeRoots;

	if (n < de->order) {
		left = factored_form(p, n, roots, *count, m);
		work::step(work::Step::Type::Equation, nullptr, left, zero);
		ast::dispose(left);
	}

	if (err == Error::Success)
		record_roots(roots, *count, m);

	canonical_SetFunction(de->y->symbol());

	for (unsigned k = 0; k <= de->order; k++)
		num_Cleanup(p[k]);

	num_Cleanup(one);
	ast::dispose(zero);

	return err;
}

void free_roots(root_t *roots, unsigned count) {
	for (unsigned i = 0; i < count; i++) {
		ast::dispose(roots[i].re);
		ast::dispose(roots[i].im);
		num_Cleanup(roots[i].value);
	}
}
