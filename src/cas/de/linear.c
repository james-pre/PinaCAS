#include "internal.h"

#include "../../work.h"

static pcas_ast_t *reduced(pcas_ast_t *e) {
	work_Pause();
	simplify(e, SIMP_ALL);
	work_Resume();

	return e;
}

/*Fills constants with n letters that do not appear in the equation or in m unless it is NULL. Returns false if there are not enough.*/
static bool choose_constants(const pcas_de_t *de, const pcas_ast_t *m, pcas_ast_t **constants, unsigned n) {
	const char *candidates = "ABCDFGHJKLNPQRSUVW";
	pcas_ast_t *scope = ast_MakeOperator(OP_ADD);
	unsigned count = 0;

	ast_ChildAppend(scope, ast_Copy(de->equation));
	ast_ChildAppend(scope, ast_Copy(de->x));
	if (m != NULL)
		ast_ChildAppend(scope, ast_Copy(m));

	for (; *candidates != '\0' && count < n; candidates++) {
		if (!contains_symbol(scope, (Symbol)*candidates))
			constants[count++] = ast_MakeSymbol((Symbol)*candidates);
	}

	ast_Cleanup(scope);

	if (count == n)
		return true;

	while (count > 0)
		ast_Cleanup(constants[--count]);

	return false;
}

/*Writes each initial condition as a linear equation in the constants, as row i of the augmented matrix*/
static void condition_rows(
	pcas_de_t *de,
	const pcas_ast_t *general,
	pcas_ast_t **constants,
	unsigned n,
	pcas_ast_t *matrix[][DE_MAX_ORDER + 1]
) {
	pcas_ast_t *derivatives[DE_MAX_ORDER], *e, *at, *chain, *zero = integer(0);
	unsigned highest = 0, i, j, k;
	pcas_condition_t *c;

	for (i = 0; i < de->condition_count; i++) {
		if (de->conditions[i].order > highest)
			highest = de->conditions[i].order;
	}

	derivatives[0] = ast_Copy(general);

	for (k = 1; k <= highest; k++) {
		derivatives[k] = ast_Copy(derivatives[k - 1]);

		work_Pause();
		derivative(derivatives[k], de->x, de->x);
		simplify(derivatives[k], SIMP_BASIC);
		work_Resume();

		e = de_Derivative(de->y, k);
		work_Step(STEP_EQUATION, k == 1 ? "Differentiate" : NULL, e, derivatives[k]);
		ast_Cleanup(e);
	}

	for (i = 0; i < de->condition_count; i++) {
		c = &de->conditions[i];
		e = ast_Copy(derivatives[c->order]);

		work_Pause();
		substitute(e, de->x, c->at);
		work_Resume();
		reduced(e);

		at = ast_MakeBinary(OP_AT, de_Derivative(de->y, c->order), ast_Copy(c->at));
		chain = ast_MakeBinary(OP_EQUALS, ast_Copy(e), ast_Copy(c->value));
		work_Step(STEP_EQUATION, i == 0 ? "Initial conditions" : NULL, at, chain);
		ast_Cleanup(at);
		ast_Cleanup(chain);

		for (j = 0; j < n; j++) {
			matrix[i][j] = ast_Copy(e);

			work_Pause();
			derivative(matrix[i][j], constants[j], constants[j]);
			work_Resume();
			reduced(matrix[i][j]);
		}

		work_Pause();
		for (j = 0; j < n; j++)
			substitute(e, constants[j], zero);
		work_Resume();

		matrix[i][n] = reduced(difference(ast_Copy(c->value), e));
	}

	for (k = 0; k <= highest; k++)
		ast_Cleanup(derivatives[k]);
	ast_Cleanup(zero);
}

/*Reduces the augmented matrix with Gauss-Jordan elimination, setting pivots to the column of each pivot row, and returns the number of pivot rows*/
static unsigned eliminate(pcas_ast_t *matrix[][DE_MAX_ORDER + 1], unsigned rows, unsigned n, unsigned *pivots) {
	pcas_ast_t *swap, *pivot, *factor;
	unsigned rank = 0, i, j, k;

	for (j = 0; j < n && rank < rows; j++) {
		for (i = rank; i < rows && is_zero(matrix[i][j]); i++)
			;

		if (i == rows)
			continue;

		for (k = 0; k <= n; k++) {
			swap = matrix[i][k];
			matrix[i][k] = matrix[rank][k];
			matrix[rank][k] = swap;
		}

		pivot = matrix[rank][j];
		for (k = 0; k <= n; k++) {
			if (k != j)
				matrix[rank][k] = reduced(ast_MakeBinary(OP_DIV, matrix[rank][k], ast_Copy(pivot)));
		}
		ast_Cleanup(pivot);
		matrix[rank][j] = integer(1);

		for (i = 0; i < rows; i++) {
			if (i == rank || is_zero(matrix[i][j]))
				continue;

			factor = matrix[i][j];
			for (k = 0; k <= n; k++) {
				if (k != j) {
					matrix[i][k] = reduced(
						difference(matrix[i][k], ast_MakeBinary(OP_MULT, ast_Copy(factor), ast_Copy(matrix[rank][k])))
					);
				}
			}
			ast_Cleanup(factor);
			matrix[i][j] = integer(0);
		}

		pivots[rank++] = j;
	}

	return rank;
}

/*Finds the constants in general from the initial conditions and substitutes them. Takes ownership of general.*/
static pcas_error_t apply_conditions(
	pcas_de_t *de,
	pcas_ast_t *general,
	pcas_ast_t **constants,
	unsigned n,
	pcas_ast_t **solution
) {
	pcas_ast_t *matrix[DE_MAX_CONDITIONS][DE_MAX_ORDER + 1], *value;
	unsigned pivots[DE_MAX_CONDITIONS], rows = de->condition_count, rank, i, k, r;
	pcas_error_t err = E_SUCCESS;
	bool pivot;

	condition_rows(de, general, constants, n, matrix);
	rank = eliminate(matrix, rows, n, pivots);

	for (i = rank; i < rows; i++) {
		if (!is_zero(matrix[i][n]))
			err = E_DE_NO_SOLUTION;
	}

	for (i = 0; i < rank && err == E_SUCCESS; i++) {
		value = ast_Copy(matrix[i][n]);

		for (k = 0; k < n; k++) {
			pivot = false;
			for (r = 0; r < rank; r++)
				pivot |= pivots[r] == k;

			if (!pivot && !is_zero(matrix[i][k]))
				value = difference(value, ast_MakeBinary(OP_MULT, ast_Copy(matrix[i][k]), ast_Copy(constants[k])));
		}

		reduced(value);
		single_fraction(value);
		work_Step(STEP_EQUATION, i == 0 ? "Solve for the constants" : NULL, constants[pivots[i]], value);

		work_Pause();
		substitute(general, constants[pivots[i]], value);
		work_Resume();

		ast_Cleanup(value);
	}

	for (i = 0; i < rows; i++) {
		for (k = 0; k <= n; k++)
			ast_Cleanup(matrix[i][k]);
	}

	if (err != E_SUCCESS) {
		ast_Cleanup(general);
		return err;
	}

	work_Pause();
	simplify(general, SIMP_BASIC);
	work_Resume();

	work_Step(STEP_EQUATION, "Solution", de->y, general);
	*solution = ast_MakeBinary(OP_EQUALS, ast_Copy(de->y), general);

	return E_SUCCESS;
}

/*Records y as a combination of the basis with arbitrary constants that are not m, and finds the constants from the initial conditions. Takes ownership of the basis.*/
static pcas_error_t solve_with_basis(
	pcas_de_t *de,
	pcas_ast_t **basis,
	unsigned size,
	const pcas_ast_t *m,
	pcas_ast_t **solution
) {
	pcas_ast_t *constants[DE_MAX_ORDER], *general;
	pcas_error_t err = E_SUCCESS;
	unsigned i;

	if (!choose_constants(de, m, constants, size)) {
		for (i = 0; i < size; i++)
			ast_Cleanup(basis[i]);
		return E_DE_UNSOLVED;
	}

	general = ast_MakeOperator(OP_ADD);
	for (i = 0; i < size; i++)
		ast_ChildAppend(general, ast_MakeBinary(OP_MULT, ast_Copy(constants[i]), basis[i]));
	tidy(general);

	if (de->condition_count > 0) {
		work_Step(STEP_EQUATION, "General solution", de->y, general);
		err = apply_conditions(de, general, constants, size, solution);
	} else {
		work_Step(STEP_EQUATION, "Solution", de->y, general);
		*solution = ast_MakeBinary(OP_EQUALS, ast_Copy(de->y), general);
	}

	for (i = 0; i < size; i++)
		ast_Cleanup(constants[i]);

	return err;
}

pcas_error_t solve_constant_coefficients(pcas_de_t *de, pcas_ast_t **solution) {
	mp_rat p[DE_MAX_ORDER + 1], one;
	root_t roots[DE_MAX_ORDER];
	pcas_ast_t *basis[DE_MAX_ORDER], *m, *left, *zero;
	unsigned n = de->order, count = 0, i, k;
	pcas_error_t err = E_SUCCESS;

	if (!de->linear || !is_ast_int(de->g, 0))
		return E_DE_UNSOLVED;

	for (k = 0; k <= de->order; k++) {
		if ((p[k] = rational_value(de->a[k])) == NULL) {
			while (k > 0)
				num_Cleanup(p[--k]);
			return E_DE_UNSOLVED;
		}
	}

	de->method = "Constant coefficients";

	m = substitution_symbol(de, SYM_M);
	zero = integer(0);
	one = num_FromInt(1);
	canonical_SetFunction(m->op.symbol);

	left = polynomial(p, n, m, one);
	work_Step(STEP_EQUATION, "Characteristic equation", left, zero);
	ast_Cleanup(left);

	if (!find_roots(p, &n, roots, &count))
		err = E_DE_ROOTS;

	if (n < de->order) {
		left = factored_form(p, n, roots, count, m);
		work_Step(STEP_EQUATION, NULL, left, zero);
		ast_Cleanup(left);
	}

	if (err == E_SUCCESS)
		record_roots(roots, count, m);

	canonical_SetFunction(de->y->op.symbol);

	if (err == E_SUCCESS)
		err = solve_with_basis(de, basis, fill_basis(de, roots, count, basis), m, solution);

	for (k = 0; k <= de->order; k++)
		num_Cleanup(p[k]);

	for (i = 0; i < count; i++) {
		ast_Cleanup(roots[i].re);
		ast_Cleanup(roots[i].im);
		num_Cleanup(roots[i].value);
	}

	num_Cleanup(one);
	ast_Cleanup(m);
	ast_Cleanup(zero);

	return err;
}

/*Returns a/b simplified as one fraction with common factors cancelled and an expanded denominator*/
static pcas_ast_t *quotient(const pcas_ast_t *a, const pcas_ast_t *b) {
	pcas_ast_t *q = ast_MakeBinary(OP_DIV, ast_Copy(a), ast_Copy(b));

	work_Pause();
	simplify(q, SIMP_BASIC);
	factor_cancel(q);

	if (isoptype(q, OP_DIV)) {
		expand(opbase(q)->next, EXP_ALL);
		simplify(opbase(q)->next, SIMP_BASIC);
	}
	work_Resume();

	return q;
}

/*Returns a times b, expanded term by term when that leaves fewer nodes*/
static pcas_ast_t *distribute(const pcas_ast_t *a, const pcas_ast_t *b) {
	pcas_ast_t *product = ast_MakeBinary(OP_MULT, ast_Copy(a), ast_Copy(b)), *sum, *term;

	work_Pause();
	simplify(product, SIMP_BASIC);

	if (isoptype(b, OP_ADD)) {
		sum = ast_MakeOperator(OP_ADD);

		for (term = opbase(b); term != NULL; term = term->next) {
			ast_ChildAppend(sum, ast_MakeBinary(OP_MULT, ast_Copy(a), ast_Copy(term)));
			simplify(ast_ChildGetLast(sum), SIMP_BASIC);
		}

		expand(sum, EXP_ALL);

		simplify(sum, SIMP_BASIC);

		if (node_count(sum) < node_count(product)) {
			ast_Cleanup(product);
			product = sum;
		} else {
			ast_Cleanup(sum);
		}
	}
	work_Resume();

	return product;
}

pcas_error_t solve_reduction_of_order(pcas_de_t *de, pcas_ast_t **solution) {
	pcas_ast_t *y1 = de->known, *P, *Q, *left, *right, *mu, *integrand, *integral, *basis[2];
	pcas_error_t err;
	bool satisfied;

	if (de->order != 2 || !de->linear || !is_ast_int(de->g, 0))
		return E_DE_UNSOLVED;

	de->method = "Reduction of order";

	if ((err = check_solution(de, y1, "Known solution", false, &satisfied)) != E_SUCCESS)
		return err;
	if (!satisfied)
		return E_DE_NOT_SOLUTION;

	P = quotient(de->a[1], de->a[2]);
	Q = quotient(de->a[0], de->a[2]);

	if (!is_ast_int(de->a[2], 1)) {
		left = ast_MakeOperator(OP_ADD);
		ast_ChildAppend(left, de_Derivative(de->y, 2));
		ast_ChildAppend(left, ast_MakeBinary(OP_MULT, ast_Copy(P), de_Derivative(de->y, 1)));
		ast_ChildAppend(left, ast_MakeBinary(OP_MULT, Q, ast_Copy(de->y)));
		right = integer(0);
		work_Step(STEP_EQUATION, "Standard form", tidy(left), right);
		ast_Cleanup(left);
		ast_Cleanup(right);
	} else {
		ast_Cleanup(Q);
	}

	left = substitution_symbol(de, SYM_P);
	work_Step(STEP_EQUATION, NULL, left, P);
	ast_Cleanup(left);

	work_Text("Reduction of order");

	work_Pause();
	P = negate(P);
	simplify(P, SIMP_BASIC);
	work_Resume();

	if ((mu = exponential_of_integral(P, de->x)) == NULL)
		return E_DE_INTEGRAL;

	integrand = ast_MakeBinary(OP_DIV, mu, ast_MakeBinary(OP_POW, ast_Copy(y1), integer(2)));
	left = ast_MakeBinary(OP_MULT, ast_Copy(y1), ast_MakeBinary(OP_INTEGRAL, ast_Copy(integrand), ast_Copy(de->x)));
	single_fraction(integrand);
	integral = ast_MakeBinary(OP_INTEGRAL, integrand, ast_Copy(de->x));
	right = ast_MakeBinary(OP_MULT, ast_Copy(y1), ast_Copy(integral));
	work_Step(STEP_EQUATION, NULL, left, right);
	ast_Cleanup(left);
	ast_Cleanup(right);

	eval_integrals(integral);

	if (contains_integral(integral)) {
		ast_Cleanup(integral);
		return E_DE_INTEGRAL;
	}

	basis[0] = ast_Copy(y1);
	basis[1] = distribute(y1, integral);
	left = ast_MakeBinary(OP_MULT, ast_Copy(y1), integral);

	work_Pause();
	simplify(left, SIMP_NORMALIZE);
	work_Resume();

	if (ast_Compare(left, basis[1]))
		work_Step(STEP_STATE, "Second solution", NULL, basis[1]);
	else
		work_Step(STEP_EQUATION, "Second solution", left, basis[1]);
	ast_Cleanup(left);

	return solve_with_basis(de, basis, 2, NULL, solution);
}
