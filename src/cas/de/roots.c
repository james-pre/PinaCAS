#include "internal.h"

#include "../../work.h"

#include <limits.h>

static const char *multiplicity_names[] = {NULL, NULL, "Double root", "Triple root"};

static pcas_ast_t *integer_node(mp_int z) {
	mp_rat n = num_FromInt(0);

	mp_int_copy(z, MP_NUMER_P(n));

	return ast_MakeNumber(n);
}

mp_rat rational_value(const pcas_ast_t *e) {
	pcas_ast_t *child;
	mp_rat value, factor;

	if (e->type == NODE_NUMBER)
		return num_Copy(e->op.num);

	if ((!isoptype(e, OP_DIV) && !isoptype(e, OP_MULT)) || (value = rational_value(opbase(e))) == NULL)
		return NULL;

	for (child = opbase(e)->next; child != NULL; child = child->next) {
		if ((factor = rational_value(child)) == NULL) {
			num_Cleanup(value);
			return NULL;
		}

		if (isoptype(e, OP_DIV))
			mp_rat_div(value, factor, value);
		else
			mp_rat_mul(value, factor, value);
		num_Cleanup(factor);
	}

	return value;
}

void evaluate(mp_rat *p, unsigned n, mp_rat r, mp_rat value) {
	unsigned k;

	mp_rat_copy(p[n], value);

	for (k = n; k-- > 0;) {
		mp_rat_mul(value, r, value);
		mp_rat_add(value, p[k], value);
	}
}

bool is_root(mp_rat *p, unsigned n, mp_rat r) {
	mp_rat value = num_FromInt(0);
	bool root;

	evaluate(p, n, r, value);
	root = mp_rat_compare_zero(value) == 0;
	num_Cleanup(value);

	return root;
}

void deflate(mp_rat *p, unsigned n, mp_rat r) {
	mp_rat t = num_FromInt(0);
	unsigned k;

	for (k = n; k-- > 0;) {
		mp_rat_mul(p[k + 1], r, t);
		mp_rat_add(p[k], t, p[k]);
	}

	num_Cleanup(p[0]);
	num_Cleanup(t);

	for (k = 0; k < n; k++)
		p[k] = p[k + 1];
	p[n] = NULL;
}

/*Sets lead and constant to the absolute values of the end coefficients of p scaled to integers. Returns false if they do not fit.*/
static bool integer_ends(mp_rat *p, unsigned n, mp_small *lead, mp_small *constant) {
	mp_rat scale = num_FromInt(1), t = num_FromInt(0);
	unsigned k;
	bool fits;

	for (k = 0; k <= n; k++)
		mp_int_lcm(MP_NUMER_P(scale), MP_DENOM_P(p[k]), MP_NUMER_P(scale));

	mp_rat_mul(p[n], scale, t);
	fits = mp_int_to_int(MP_NUMER_P(t), lead) == MP_OK;
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
	mp_small lead, constant, a, b;

	if (!integer_ends(p, n, &lead, &constant))
		return false;

	for (a = 1; a <= constant / a; a++) {
		if (constant % a != 0)
			continue;

		for (b = 1; b <= lead / b; b++) {
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
	bool exact;

	mp_int_sqrt(MP_NUMER_P(q), MP_NUMER_P(root));
	mp_int_sqrt(MP_DENOM_P(q), MP_DENOM_P(root));
	mp_rat_mul(root, root, square);
	exact = mp_rat_compare(square, q) == 0;
	num_Cleanup(square);

	return exact;
}

/*Returns the square root of q, which is positive, as a rational multiple of the square root of a square-free integer*/
static pcas_ast_t *square_root(mp_rat q) {
	mp_rat root = num_FromInt(0);
	mp_small numerator, denominator, radicand, scale = 1, k;

	if (rational_sqrt(q, root))
		return ast_MakeNumber(root);

	num_Cleanup(root);

	if (mp_rat_to_ints(q, &numerator, &denominator) != MP_OK || numerator > LONG_MAX / denominator)
		return ast_MakeBinary(OP_POW, ast_MakeNumber(num_Copy(q)), ast_MakeNumber(num_FromFraction(1, 2)));

	radicand = numerator * denominator;

	for (k = 2; k <= radicand / k; k++) {
		while (radicand % (k * k) == 0) {
			radicand /= k * k;
			scale *= k;
		}
	}

	return ast_MakeBinary(
		OP_MULT,
		ast_MakeNumber(num_FromFraction(scale, denominator)),
		ast_MakeBinary(OP_POW, integer(radicand), ast_MakeNumber(num_FromFraction(1, 2)))
	);
}

/*Records a root, merging it with an equal real root. Takes ownership of re and im.*/
static void add_root(
	root_t *roots,
	unsigned *count,
	pcas_ast_t *re,
	pcas_ast_t *im,
	mp_rat value,
	unsigned multiplicity
) {
	unsigned i;

	work_Pause();
	simplify(re, SIMP_BASIC);
	if (im != NULL)
		simplify(im, SIMP_BASIC);
	work_Resume();

	for (i = 0; i < *count; i++) {
		if (im == NULL && roots[i].im == NULL && ast_Compare(roots[i].re, re)) {
			roots[i].multiplicity += multiplicity;
			ast_Cleanup(re);
			return;
		}
	}

	roots[*count].re = re;
	roots[*count].im = im;
	roots[*count].value = value != NULL ? num_Copy(value) : NULL;
	roots[*count].multiplicity = multiplicity;
	(*count)++;
}

/*Records the rational root r of p and divides it out*/
static void divide_root(mp_rat *p, unsigned *n, mp_rat r, root_t *roots, unsigned *count) {
	deflate(p, (*n)--, r);
	add_root(roots, count, ast_MakeNumber(num_Copy(r)), NULL, r, 1);
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
	pcas_ast_t *w;
	bool real = mp_rat_compare_zero(d) > 0;

	mp_rat_abs(magnitude, magnitude);
	mp_rat_add(a, a, denominator);
	mp_rat_abs(denominator, denominator);
	w = ast_MakeBinary(OP_DIV, square_root(magnitude), ast_MakeNumber(denominator));

	if (real) {
		add_root(
			roots, count, ast_MakeBinary(OP_ADD, ast_MakeNumber(num_Copy(v)), ast_Copy(w)), NULL, NULL, multiplicity
		);
		add_root(roots, count, difference(ast_MakeNumber(num_Copy(v)), w), NULL, NULL, multiplicity);
	} else {
		add_root(roots, count, ast_MakeNumber(num_Copy(v)), w, NULL, multiplicity);
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
	unsigned multiplicity, i;
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
		multiplicity = mp_rat_compare_zero(s) == 0 ? 2 : 1;

		for (i = 0; i < 3 - multiplicity; i++) {
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

pcas_ast_t *polynomial(mp_rat *p, unsigned n, const pcas_ast_t *m, mp_rat divisor) {
	pcas_ast_t *sum = ast_MakeOperator(OP_ADD), *term;
	mp_rat c;
	unsigned k;

	for (k = n + 1; k-- > 0;) {
		if (mp_rat_compare_zero(p[k]) == 0)
			continue;

		c = num_Copy(p[k]);
		mp_rat_div(c, divisor, c);

		if (k == 0) {
			term = ast_MakeNumber(c);
		} else {
			term = k == 1 ? ast_Copy(m) : ast_MakeBinary(OP_POW, ast_Copy(m), integer(k));

			if (num_IsInt(c, 1))
				num_Cleanup(c);
			else
				term = ast_MakeBinary(OP_MULT, ast_MakeNumber(c), term);
		}

		ast_ChildAppend(sum, term);
	}

	return tidy(sum);
}

pcas_ast_t *factored_form(mp_rat *p, unsigned n, const root_t *roots, unsigned count, const pcas_ast_t *m) {
	pcas_ast_t *product = ast_MakeOperator(OP_MULT), *factor, *rest;
	mp_rat scale = num_FromInt(1), t = num_FromInt(0), r;
	unsigned i, k;

	for (i = 0; i < count; i++) {
		if ((r = roots[i].value) == NULL)
			continue;

		factor = difference(
			mp_int_compare_value(MP_DENOM_P(r), 1) == 0
				? ast_Copy(m)
				: ast_MakeBinary(OP_MULT, integer_node(MP_DENOM_P(r)), ast_Copy(m)),
			integer_node(MP_NUMER_P(r))
		);

		if (roots[i].multiplicity > 1)
			factor = ast_MakeBinary(OP_POW, factor, integer(roots[i].multiplicity));

		ast_ChildAppend(product, factor);

		mp_rat_set_value(t, 1, 1);
		mp_int_copy(MP_DENOM_P(r), MP_NUMER_P(t));
		for (k = 0; k < roots[i].multiplicity; k++)
			mp_rat_mul(scale, t, scale);
	}

	rest = polynomial(p, n, m, scale);

	if (n > 0)
		ast_ChildAppend(product, rest);
	else if (!is_ast_int(rest, 1))
		ast_ChildInsert(product, rest, 0);
	else
		ast_Cleanup(rest);

	num_Cleanup(scale);
	num_Cleanup(t);

	return tidy(product);
}

static void record_roots(const root_t *roots, unsigned count, const pcas_ast_t *m) {
	pcas_ast_t *value, *imaginary;
	const char *text;
	unsigned i;
	int sign;

	for (i = 0; i < count; i++) {
		text = roots[i].multiplicity < 4 ? multiplicity_names[roots[i].multiplicity] : "Repeated root";

		if (roots[i].im == NULL) {
			work_Step(STEP_EQUATION, text, m, roots[i].re);
			continue;
		}

		for (sign = 1; sign >= -1; sign -= 2) {
			imaginary = ast_MakeBinary(OP_MULT, ast_Copy(roots[i].im), ast_MakeSymbol(SYM_IMAG));
			if (sign < 0)
				imaginary = negate(imaginary);

			value = is_ast_int(roots[i].re, 0) ? imaginary : ast_MakeBinary(OP_ADD, ast_Copy(roots[i].re), imaginary);
			work_Step(STEP_EQUATION, sign > 0 ? text : NULL, m, tidy(value));
			ast_Cleanup(value);
		}
	}
}

pcas_ast_t *basis_function(const pcas_de_t *de, unsigned j, const pcas_ast_t *r, pcas_ast_t *f) {
	pcas_ast_t *product = ast_MakeOperator(OP_MULT);

	ast_ChildAppend(product, integer(1));

	if (j > 0)
		ast_ChildAppend(product, ast_MakeBinary(OP_POW, ast_Copy(de->x), integer(j)));

	if (!is_ast_int(r, 0)) {
		ast_ChildAppend(
			product,
			ast_MakeBinary(OP_POW, ast_MakeSymbol(SYM_EULER), ast_MakeBinary(OP_MULT, ast_Copy(r), ast_Copy(de->x)))
		);
	}

	if (f != NULL)
		ast_ChildAppend(product, f);

	work_Pause();
	simplify(product, SIMP_BASIC);
	work_Resume();

	return product;
}

unsigned fill_basis(const pcas_de_t *de, const root_t *roots, unsigned count, pcas_ast_t **basis) {
	const root_t *root;
	unsigned i, j, n = 0;

	for (i = 0; i < count; i++) {
		root = &roots[i];

		for (j = 0; j < root->multiplicity; j++) {
			if (root->im == NULL) {
				basis[n++] = basis_function(de, j, root->re, NULL);
			} else {
				basis[n++] = basis_function(
					de, j, root->re, ast_MakeUnary(OP_COS, ast_MakeBinary(OP_MULT, ast_Copy(root->im), ast_Copy(de->x)))
				);
				basis[n++] = basis_function(
					de, j, root->re, ast_MakeUnary(OP_SIN, ast_MakeBinary(OP_MULT, ast_Copy(root->im), ast_Copy(de->x)))
				);
			}
		}
	}

	return n;
}

/*Finds the roots of a2m^2 + a0 = 0 as ±i*sqrt(a0/a2), assuming that a0/a2 is positive, when the coefficients are constants that are not all rational*/
static pcas_error_t oscillator_roots(pcas_de_t *de, const pcas_ast_t *m, root_t *roots, unsigned *count) {
	pcas_ast_t *left, *zero;

	if (de->order != 2 || !is_ast_int(de->a[1], 0) || involves(de->a[0], de->x) || involves(de->a[2], de->x))
		return E_DE_UNSOLVED;

	left = ast_MakeBinary(
		OP_ADD,
		ast_MakeBinary(OP_MULT, ast_Copy(de->a[2]), ast_MakeBinary(OP_POW, ast_Copy(m), integer(2))),
		ast_Copy(de->a[0])
	);
	zero = integer(0);
	work_Step(STEP_EQUATION, "Characteristic equation", tidy(left), zero);
	ast_Cleanup(left);
	ast_Cleanup(zero);

	add_root(
		roots,
		count,
		integer(0),
		ast_MakeBinary(
			OP_POW,
			ast_MakeBinary(OP_DIV, ast_Copy(de->a[0]), ast_Copy(de->a[2])),
			ast_MakeNumber(num_FromFraction(1, 2))
		),
		NULL,
		1
	);
	record_roots(roots, *count, m);

	return E_SUCCESS;
}

pcas_error_t characteristic_roots(pcas_de_t *de, const pcas_ast_t *m, root_t *roots, unsigned *count) {
	mp_rat p[DE_MAX_ORDER + 1], one;
	pcas_ast_t *left, *zero;
	unsigned n = de->order, k;
	pcas_error_t err = E_SUCCESS;

	*count = 0;
	canonical_SetFunction(m->op.symbol);

	for (k = 0; k <= de->order; k++) {
		if ((p[k] = rational_value(de->a[k])) == NULL) {
			while (k > 0)
				num_Cleanup(p[--k]);

			err = oscillator_roots(de, m, roots, count);
			canonical_SetFunction(de->y->op.symbol);
			return err;
		}
	}

	zero = integer(0);
	one = num_FromInt(1);

	left = polynomial(p, n, m, one);
	work_Step(STEP_EQUATION, "Characteristic equation", left, zero);
	ast_Cleanup(left);

	if (!find_roots(p, &n, roots, count))
		err = E_DE_ROOTS;

	if (n < de->order) {
		left = factored_form(p, n, roots, *count, m);
		work_Step(STEP_EQUATION, NULL, left, zero);
		ast_Cleanup(left);
	}

	if (err == E_SUCCESS)
		record_roots(roots, *count, m);

	canonical_SetFunction(de->y->op.symbol);

	for (k = 0; k <= de->order; k++)
		num_Cleanup(p[k]);

	num_Cleanup(one);
	ast_Cleanup(zero);

	return err;
}

void free_roots(root_t *roots, unsigned count) {
	unsigned i;

	for (i = 0; i < count; i++) {
		ast_Cleanup(roots[i].re);
		ast_Cleanup(roots[i].im);
		num_Cleanup(roots[i].value);
	}
}
