#include "internal.h"

#include "../../work.h"

static pcas_ast_t *reduced(pcas_ast_t *e) {
	work_Pause();
	simplify(e, SIMP_ALL);
	work_Resume();

	return e;
}

/*Simplifies e, keeping the smallest of its simplified, expanded and identity-reduced forms*/
static pcas_ast_t *simplest(pcas_ast_t *e) {
	work_Pause();
	simplify(e, SIMP_BASIC);
	expand_if_smaller(e);
	work_Resume();

	pcas_ast_t *candidate = reduced(ast_Copy(e));

	if (node_count(candidate) < node_count(e))
		replace_node(e, candidate);
	else
		ast_Cleanup(candidate);

	return e;
}

bool choose_constants(const pcas_de_t *de, const pcas_ast_t *exclude, pcas_ast_t **constants, unsigned n) {
	const char *candidates = "ABCDFGHJKLNPQRSUVW";
	pcas_ast_t *scope = ast_MakeOperator(OP_ADD);
	unsigned count = 0;

	ast_ChildAppend(scope, ast_Copy(de->equation));
	ast_ChildAppend(scope, ast_Copy(de->x));
	if (exclude != NULL)
		ast_ChildAppend(scope, ast_Copy(exclude));

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
	pcas_ast_t *derivatives[DE_MAX_ORDER], *zero = integer(0);
	unsigned highest = 0;

	for (unsigned i = 0; i < de->condition_count; i++) {
		if (de->conditions[i].order > highest)
			highest = de->conditions[i].order;
	}

	derivatives[0] = ast_Copy(general);

	for (unsigned k = 1; k <= highest; k++) {
		derivatives[k] = ast_Copy(derivatives[k - 1]);

		work_Pause();
		derivative(derivatives[k], de->x, de->x);
		work_Resume();
		simplest(derivatives[k]);

		pcas_ast_t *e = de_Derivative(de->y, k);
		work_Step(STEP_EQUATION, k == 1 ? "Differentiate" : NULL, e, derivatives[k]);
		ast_Cleanup(e);
	}

	for (unsigned i = 0; i < de->condition_count; i++) {
		const pcas_condition_t *c = &de->conditions[i];
		pcas_ast_t *e = ast_Copy(derivatives[c->order]);

		work_Pause();
		substitute(e, de->x, c->at);
		work_Resume();
		reduced(e);

		pcas_ast_t *at = ast_MakeBinary(OP_AT, de_Derivative(de->y, c->order), ast_Copy(c->at));
		pcas_ast_t *chain = ast_MakeBinary(OP_EQUALS, ast_Copy(e), ast_Copy(c->value));
		work_Step(STEP_EQUATION, i == 0 ? "Initial conditions" : NULL, at, chain);
		ast_Cleanup(at);
		ast_Cleanup(chain);

		for (unsigned j = 0; j < n; j++) {
			matrix[i][j] = ast_Copy(e);

			work_Pause();
			derivative(matrix[i][j], constants[j], constants[j]);
			work_Resume();
			reduced(matrix[i][j]);
		}

		work_Pause();
		for (unsigned j = 0; j < n; j++)
			substitute(e, constants[j], zero);
		work_Resume();

		matrix[i][n] = reduced(difference(ast_Copy(c->value), e));
	}

	for (unsigned k = 0; k <= highest; k++)
		ast_Cleanup(derivatives[k]);
	ast_Cleanup(zero);
}

/*Reduces the augmented matrix with Gauss-Jordan elimination, setting pivots to the column of each pivot row, and returns the number of pivot rows*/
static unsigned eliminate(pcas_ast_t *matrix[][DE_MAX_ORDER + 1], unsigned rows, unsigned n, unsigned *pivots) {
	unsigned rank = 0;

	for (unsigned j = 0; j < n && rank < rows; j++) {
		unsigned row = rank;
		while (row < rows && is_zero(matrix[row][j]))
			row++;

		if (row == rows)
			continue;

		for (unsigned k = 0; k <= n; k++) {
			pcas_ast_t *swap = matrix[row][k];
			matrix[row][k] = matrix[rank][k];
			matrix[rank][k] = swap;
		}

		pcas_ast_t *pivot = matrix[rank][j];
		for (unsigned k = 0; k <= n; k++) {
			if (k != j)
				matrix[rank][k] = reduced(ast_MakeBinary(OP_DIV, matrix[rank][k], ast_Copy(pivot)));
		}
		ast_Cleanup(pivot);
		matrix[rank][j] = integer(1);

		for (unsigned i = 0; i < rows; i++) {
			if (i == rank || is_zero(matrix[i][j]))
				continue;

			pcas_ast_t *factor = matrix[i][j];
			for (unsigned k = 0; k <= n; k++) {
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
	pcas_ast_t *matrix[DE_MAX_CONDITIONS][DE_MAX_ORDER + 1];
	unsigned pivots[DE_MAX_CONDITIONS];
	const unsigned rows = de->condition_count;
	pcas_error_t err = E_SUCCESS;

	condition_rows(de, general, constants, n, matrix);
	const unsigned rank = eliminate(matrix, rows, n, pivots);

	for (unsigned i = rank; i < rows; i++) {
		if (!is_zero(matrix[i][n]))
			err = E_DE_NO_SOLUTION;
	}

	for (unsigned i = 0; i < rank && err == E_SUCCESS; i++) {
		pcas_ast_t *value = ast_Copy(matrix[i][n]);

		for (unsigned k = 0; k < n; k++) {
			bool pivot = false;
			for (unsigned r = 0; r < rank; r++)
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

	for (unsigned i = 0; i < rows; i++) {
		for (unsigned k = 0; k <= n; k++)
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
	pcas_ast_t *product = ast_MakeBinary(OP_MULT, ast_Copy(a), ast_Copy(b));

	work_Pause();
	simplify(product, SIMP_BASIC);

	if (isoptype(b, OP_ADD)) {
		pcas_ast_t *sum = ast_MakeOperator(OP_ADD);

		for (const pcas_ast_t *term = opbase(b); term != NULL; term = term->next) {
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

/*Returns the sum of each constant times its basis function*/
static pcas_ast_t *combination(pcas_ast_t **constants, pcas_ast_t **basis, unsigned size) {
	pcas_ast_t *sum = ast_MakeOperator(OP_ADD);

	for (unsigned i = 0; i < size; i++)
		ast_ChildAppend(sum, ast_MakeBinary(OP_MULT, ast_Copy(constants[i]), ast_Copy(basis[i])));

	return tidy(sum);
}

/*Records y as the combination of the basis with the constants, plus particular unless it is NULL, and finds the constants from the initial conditions. Takes ownership of particular.*/
static pcas_error_t solve_with_basis(
	pcas_de_t *de,
	pcas_ast_t **basis,
	pcas_ast_t **constants,
	unsigned size,
	pcas_ast_t *particular,
	pcas_ast_t **solution
) {
	pcas_ast_t *general = combination(constants, basis, size);

	if (particular != NULL)
		general = tidy(ast_MakeBinary(OP_ADD, general, particular));

	if (de->condition_count > 0) {
		work_Step(STEP_EQUATION, "General solution", de->y, general);
		return apply_conditions(de, general, constants, size, solution);
	}

	work_Step(STEP_EQUATION, "Solution", de->y, general);
	*solution = ast_MakeBinary(OP_EQUALS, ast_Copy(de->y), general);

	return E_SUCCESS;
}

/*Returns preferred, or a symbol that does not appear in the equation or taken if preferred does*/
static pcas_ast_t *pick_symbol(const pcas_de_t *de, Symbol preferred, const pcas_ast_t *taken) {
	pcas_ast_t *scope = ast_MakeOperator(OP_ADD);

	ast_ChildAppend(scope, ast_Copy(de->equation));
	ast_ChildAppend(scope, ast_Copy(de->x));
	ast_ChildAppend(scope, ast_Copy(taken));
	const Symbol symbol = contains_symbol(scope, preferred) ? fresh_symbol(scope) : preferred;
	ast_Cleanup(scope);

	return ast_MakeSymbol(symbol);
}

/*Returns the constant slope of e in x if e is linear in x, otherwise NULL*/
static pcas_ast_t *linear_slope(const pcas_de_t *de, const pcas_ast_t *e) {
	pcas_ast_t *slope = ast_Copy(e);

	work_Pause();
	derivative(slope, de->x, de->x);
	simplify(slope, SIMP_BASIC);
	pcas_ast_t *second = ast_Copy(slope);
	derivative(second, de->x, de->x);
	simplify(second, SIMP_BASIC);
	work_Resume();

	if (involves(slope, de->x) || !is_ast_int(second, 0)) {
		ast_Cleanup(slope);
		slope = NULL;
	}

	ast_Cleanup(second);

	return slope;
}

/*Reads term as a constant times x^degree e^(rate*x), times cos or sin of frequency*x unless frequency is zero. Returns false if it has another form.*/
static bool forcing_form(
	const pcas_de_t *de,
	const pcas_ast_t *term,
	unsigned *degree,
	pcas_ast_t **rate,
	pcas_ast_t **frequency
) {
	bool form = true;

	if (isoptype(term, OP_DIV) && !involves(opbase(term)->next, de->x))
		term = opbase(term);

	const unsigned count = isoptype(term, OP_MULT) ? ast_ChildLength(term) : 1;
	*degree = 0;
	*rate = integer(0);
	*frequency = NULL;

	for (unsigned i = 0; i < count && form; i++) {
		const pcas_ast_t *factor = isoptype(term, OP_MULT) ? ast_ChildGet(term, i) : term;

		if (!involves(factor, de->x))
			continue;

		pcas_ast_t *slope;
		mp_small n;
		if (ast_Compare(factor, de->x)) {
			(*degree)++;
		} else if (
			isoptype(factor, OP_POW) && ast_Compare(opbase(factor), de->x) &&
			opbase(factor)->next->type == NODE_NUMBER && mp_rat_is_integer(opbase(factor)->next->op.num) &&
			mp_int_to_int(MP_NUMER_P(opbase(factor)->next->op.num), &n) == MP_OK && n > 0 && n <= DE_MAX_ORDER
		) {
			*degree += (unsigned)n;
		} else if (
			isoptype(factor, OP_POW) && is_euler(opbase(factor)) &&
			(slope = linear_slope(de, opbase(factor)->next)) != NULL
		) {
			*rate = ast_MakeBinary(OP_ADD, *rate, slope);
		} else if (
			(isoptype(factor, OP_SIN) || isoptype(factor, OP_COS)) && *frequency == NULL &&
			(slope = linear_slope(de, opbase(factor))) != NULL
		) {
			*frequency = slope;
		} else {
			form = false;
		}
	}

	if (*frequency == NULL)
		*frequency = integer(0);

	work_Pause();
	simplify(*rate, SIMP_BASIC);
	if (is_negative_for_sure(*frequency))
		*frequency = negate(*frequency);
	simplify(*frequency, SIMP_BASIC);
	work_Resume();

	if (!form || *degree > DE_MAX_ORDER) {
		ast_Cleanup(*rate);
		ast_Cleanup(*frequency);
		return false;
	}

	return true;
}

/*Returns the multiplicity of rate + frequency*i as a characteristic root*/
static unsigned root_multiplicity(
	const root_t *roots,
	unsigned count,
	const pcas_ast_t *rate,
	const pcas_ast_t *frequency
) {
	unsigned multiplicity = 0;
	const bool real = is_ast_int(frequency, 0);

	for (unsigned i = 0; i < count && multiplicity == 0; i++) {
		if ((roots[i].im == NULL) != real)
			continue;

		pcas_ast_t *re = difference(ast_Copy(roots[i].re), ast_Copy(rate));
		pcas_ast_t *im = real ? integer(0) : difference(ast_Copy(roots[i].im), ast_Copy(frequency));

		if (is_zero(re) && is_zero(im))
			multiplicity = roots[i].multiplicity;

		ast_Cleanup(re);
		ast_Cleanup(im);
	}

	return multiplicity;
}

/*Returns the factors of term that involve x, or 1*/
static pcas_ast_t *function_part(const pcas_de_t *de, const pcas_ast_t *term) {
	if (!involves(term, de->x))
		return integer(1);

	pcas_ast_t *part;
	if (isoptype(term, OP_MULT)) {
		part = ast_MakeOperator(OP_MULT);
		for (const pcas_ast_t *child = opbase(term); child != NULL; child = child->next) {
			if (involves(child, de->x))
				ast_ChildAppend(part, ast_Copy(child));
		}
	} else if (isoptype(term, OP_DIV)) {
		part = ast_MakeBinary(OP_DIV, function_part(de, opbase(term)), function_part(de, opbase(term)->next));
	} else {
		part = ast_Copy(term);
	}

	work_Pause();
	simplify(part, SIMP_BASIC);
	work_Resume();

	return part;
}

#define MAX_ROWS (2 * DE_MAX_ORDER)

/*Substitutes the trial solution into the equation and solves for its unknowns by equating the coefficients of each function of x, recording the work*/
static pcas_error_t match_coefficients(
	pcas_de_t *de,
	const pcas_ast_t *trial,
	pcas_ast_t **unknowns,
	unsigned size,
	pcas_ast_t **particular
) {
	pcas_ast_t *derivatives[DE_MAX_ORDER + 1], *functions[MAX_ROWS], *sums[MAX_ROWS],
		*matrix[MAX_ROWS][DE_MAX_ORDER + 1];
	unsigned pivots[MAX_ROWS], groups = 0, rows = 0;
	pcas_error_t err = E_SUCCESS;

	derivatives[0] = ast_Copy(trial);

	for (unsigned k = 1; k <= de->order; k++) {
		derivatives[k] = ast_Copy(derivatives[k - 1]);

		work_Pause();
		derivative(derivatives[k], de->x, de->x);
		simplify(derivatives[k], SIMP_BASIC);
		expand_if_smaller(derivatives[k]);
		work_Resume();

		pcas_ast_t *prime = de_Derivative(de->y, k);
		work_Step(STEP_EQUATION, k == 1 ? "Differentiate" : NULL, prime, derivatives[k]);
		ast_Cleanup(prime);
	}

	pcas_ast_t *left = ast_MakeOperator(OP_ADD);
	for (unsigned k = de->order + 1; k-- > 0;) {
		if (!is_ast_int(de->a[k], 0))
			ast_ChildAppend(left, ast_MakeBinary(OP_MULT, ast_Copy(de->a[k]), ast_Copy(derivatives[k])));
	}
	work_Step(STEP_EQUATION, "Substitute", tidy(left), de->g);
	pcas_ast_t *before = ast_Copy(left);

	work_Pause();
	simplify(left, SIMP_BASIC);
	expand(left, EXP_ALL);
	simplify(left, SIMP_BASIC);
	work_Resume();

	if (!ast_Compare(left, before))
		work_Step(STEP_EQUATION, NULL, left, de->g);
	ast_Cleanup(before);

	pcas_ast_t *residual = difference(left, ast_Copy(de->g));

	work_Pause();
	expand(residual, EXP_ALL);
	simplify(residual, SIMP_BASIC);
	work_Resume();

	for (unsigned i = 0; i < (isoptype(residual, OP_ADD) ? ast_ChildLength(residual) : 1) && err == E_SUCCESS; i++) {
		const pcas_ast_t *term = isoptype(residual, OP_ADD) ? ast_ChildGet(residual, i) : residual;
		pcas_ast_t *function = function_part(de, term);

		unsigned j = 0;
		while (j < groups && !ast_Compare(functions[j], function))
			j++;

		if (j < groups) {
			ast_ChildAppend(sums[j], ast_Copy(term));
			ast_Cleanup(function);
		} else if (groups < MAX_ROWS) {
			functions[groups] = function;
			sums[groups++] = ast_MakeUnary(OP_ADD, ast_Copy(term));
		} else {
			ast_Cleanup(function);
			err = E_DE_UNSOLVED;
		}
	}

	pcas_ast_t *zero = integer(0);

	for (unsigned i = 0; i < groups && err == E_SUCCESS; i++) {
		bool nonzero = false;

		for (unsigned j = 0; j < size; j++) {
			matrix[rows][j] = ast_Copy(sums[i]);

			work_Pause();
			derivative(matrix[rows][j], unknowns[j], unknowns[j]);
			work_Resume();
			matrix[rows][j] = reduced(ast_MakeBinary(OP_DIV, matrix[rows][j], ast_Copy(functions[i])));

			nonzero |= !is_zero(matrix[rows][j]);
		}

		pcas_ast_t *constant = ast_Copy(sums[i]);

		work_Pause();
		for (unsigned j = 0; j < size; j++)
			substitute(constant, unknowns[j], zero);
		work_Resume();

		matrix[rows][size] = reduced(ast_MakeBinary(OP_DIV, negate(constant), ast_Copy(functions[i])));

		if (!nonzero) {
			if (!is_zero(matrix[rows][size]))
				err = E_DE_UNSOLVED;

			for (unsigned j = 0; j <= size; j++)
				ast_Cleanup(matrix[rows][j]);
			continue;
		}

		pcas_ast_t *sum = ast_MakeOperator(OP_ADD);
		for (unsigned j = 0; j < size; j++)
			ast_ChildAppend(sum, ast_MakeBinary(OP_MULT, ast_Copy(matrix[rows][j]), ast_Copy(unknowns[j])));

		work_Pause();
		simplify(sum, SIMP_BASIC);
		work_Resume();

		work_Step(STEP_EQUATION, rows == 0 ? "Equate coefficients" : NULL, sum, matrix[rows][size]);
		ast_Cleanup(sum);
		rows++;
	}

	unsigned rank;
	if (err == E_SUCCESS) {
		rank = eliminate(matrix, rows, size, pivots);

		for (unsigned i = rank; i < rows; i++) {
			if (!is_zero(matrix[i][size]))
				err = E_DE_UNSOLVED;
		}
	}

	if (err == E_SUCCESS) {
		*particular = ast_Copy(trial);

		for (unsigned j = 0; j < size; j++) {
			pcas_ast_t *value = NULL;
			for (unsigned i = 0; i < rank && value == NULL; i++) {
				if (pivots[i] == j)
					value = ast_Copy(matrix[i][size]);
			}
			if (value == NULL)
				value = integer(0);

			single_fraction(value);
			work_Step(STEP_EQUATION, j == 0 ? "Solve for the coefficients" : NULL, unknowns[j], value);

			work_Pause();
			substitute(*particular, unknowns[j], value);
			work_Resume();
			ast_Cleanup(value);
		}

		simplest(*particular);
		work_Step(STEP_STATE, "Particular solution", NULL, *particular);
	}

	for (unsigned i = 0; i < rows; i++) {
		for (unsigned j = 0; j <= size; j++)
			ast_Cleanup(matrix[i][j]);
	}

	for (unsigned i = 0; i < groups; i++) {
		ast_Cleanup(functions[i]);
		ast_Cleanup(sums[i]);
	}

	for (unsigned k = 0; k <= de->order; k++)
		ast_Cleanup(derivatives[k]);

	ast_Cleanup(residual);
	ast_Cleanup(zero);

	return err;
}

/*Returns op(frequency*x)*/
static pcas_ast_t *oscillation(const pcas_de_t *de, OperatorType op, const pcas_ast_t *frequency) {
	return ast_MakeUnary(op, ast_MakeBinary(OP_MULT, ast_Copy(frequency), ast_Copy(de->x)));
}

/*Appends unknown*x^j e^(rate*x) f to trial, leaving out f when it is NULL. Takes ownership of f.*/
static void add_trial_term(
	const pcas_de_t *de,
	pcas_ast_t *trial,
	const pcas_ast_t *unknown,
	unsigned j,
	const pcas_ast_t *rate,
	pcas_ast_t *f
) {
	ast_ChildAppend(trial, ast_MakeBinary(OP_MULT, ast_Copy(unknown), basis_function(de, j, rate, f)));
}

/*Finds a particular solution with undetermined coefficients when each term of g is a polynomial times e^(ax) times cos(bx) or sin(bx), recording the work*/
static pcas_error_t undetermined_coefficients(
	pcas_de_t *de,
	const root_t *roots,
	unsigned count,
	const pcas_ast_t *exclude,
	pcas_ast_t **particular
) {
	pcas_ast_t *rates[DE_MAX_ORDER], *frequencies[DE_MAX_ORDER], *unknowns[DE_MAX_ORDER];
	unsigned degrees[DE_MAX_ORDER], groups = 0, size = 0;
	pcas_error_t err = E_SUCCESS;

	pcas_ast_t *g = ast_Copy(de->g);

	work_Pause();
	expand(g, EXP_ALL);
	simplify(g, SIMP_BASIC);
	work_Resume();

	for (unsigned i = 0; i < (isoptype(g, OP_ADD) ? ast_ChildLength(g) : 1) && err == E_SUCCESS; i++) {
		const pcas_ast_t *term = isoptype(g, OP_ADD) ? ast_ChildGet(g, i) : g;
		unsigned degree;
		pcas_ast_t *rate, *frequency;

		if (!forcing_form(de, term, &degree, &rate, &frequency)) {
			err = E_DE_UNSOLVED;
			continue;
		}

		unsigned j = 0;
		while (j < groups && !(ast_Compare(rates[j], rate) && ast_Compare(frequencies[j], frequency)))
			j++;

		if (j < groups) {
			if (degree > degrees[j])
				degrees[j] = degree;
			ast_Cleanup(rate);
			ast_Cleanup(frequency);
		} else if (groups < DE_MAX_ORDER) {
			rates[groups] = rate;
			frequencies[groups] = frequency;
			degrees[groups++] = degree;
		} else {
			ast_Cleanup(rate);
			ast_Cleanup(frequency);
			err = E_DE_UNSOLVED;
		}
	}

	ast_Cleanup(g);

	for (unsigned i = 0; i < groups; i++)
		size += (degrees[i] + 1) * (is_ast_int(frequencies[i], 0) ? 1 : 2);

	if (err == E_SUCCESS && (size > DE_MAX_ORDER || !choose_constants(de, exclude, unknowns, size)))
		err = E_DE_UNSOLVED;

	if (err == E_SUCCESS) {
		de->method = "Undetermined coefficients";
		work_Text("Undetermined coefficients");

		pcas_ast_t *trial = ast_MakeOperator(OP_ADD);
		unsigned k = 0;

		for (unsigned i = 0; i < groups; i++) {
			const unsigned shift = root_multiplicity(roots, count, rates[i], frequencies[i]);

			for (unsigned j = degrees[i] + 1; j-- > 0;) {
				if (is_ast_int(frequencies[i], 0)) {
					add_trial_term(de, trial, unknowns[k++], j + shift, rates[i], NULL);
				} else {
					add_trial_term(
						de, trial, unknowns[k++], j + shift, rates[i], oscillation(de, OP_COS, frequencies[i])
					);
					add_trial_term(
						de, trial, unknowns[k++], j + shift, rates[i], oscillation(de, OP_SIN, frequencies[i])
					);
				}
			}
		}

		tidy(trial);
		work_Step(STEP_EQUATION, "Trial solution", de->y, trial);

		err = match_coefficients(de, trial, unknowns, size, particular);

		ast_Cleanup(trial);
		for (unsigned i = 0; i < size; i++)
			ast_Cleanup(unknowns[i]);
	}

	for (unsigned i = 0; i < groups; i++) {
		ast_Cleanup(rates[i]);
		ast_Cleanup(frequencies[i]);
	}

	return err;
}

/*Finds a particular solution of a second order equation by variation of parameters, recording the work*/
static pcas_error_t variation_of_parameters(
	pcas_de_t *de,
	pcas_ast_t **basis,
	unsigned size,
	const pcas_ast_t *exclude,
	pcas_ast_t **particular
) {
	if (de->order != 2 || size != 2)
		return E_DE_UNSOLVED;

	de->method = "Variation of parameters";
	work_Text("Variation of parameters");

	pcas_ast_t *f = quotient(de->g, de->a[2]);

	if (!is_ast_int(de->a[2], 1)) {
		pcas_ast_t *left = ast_MakeOperator(OP_ADD);
		ast_ChildAppend(left, de_Derivative(de->y, 2));
		ast_ChildAppend(left, ast_MakeBinary(OP_MULT, quotient(de->a[1], de->a[2]), de_Derivative(de->y, 1)));
		ast_ChildAppend(left, ast_MakeBinary(OP_MULT, quotient(de->a[0], de->a[2]), ast_Copy(de->y)));
		work_Step(STEP_EQUATION, "Standard form", tidy(left), f);
		ast_Cleanup(left);
	}

	pcas_ast_t *derivatives[2];
	for (unsigned i = 0; i < 2; i++) {
		derivatives[i] = ast_Copy(basis[i]);

		work_Pause();
		derivative(derivatives[i], de->x, de->x);
		work_Resume();
		simplest(derivatives[i]);
	}

	pcas_ast_t *names[3];
	pcas_ast_t *taken = ast_MakeOperator(OP_ADD);
	ast_ChildAppend(taken, ast_Copy(exclude));
	for (unsigned i = 0; i < 3; i++) {
		names[i] = pick_symbol(de, i == 0 ? SYM_W : i == 1 ? SYM_U : SYM_V, taken);
		ast_ChildAppend(taken, ast_Copy(names[i]));
	}
	ast_Cleanup(taken);

	pcas_ast_t *left = difference(
		ast_MakeBinary(OP_MULT, ast_Copy(basis[0]), ast_Copy(derivatives[1])),
		ast_MakeBinary(OP_MULT, ast_Copy(derivatives[0]), ast_Copy(basis[1]))
	);
	pcas_ast_t *W = simplest(ast_Copy(left));
	pcas_ast_t *right = ast_MakeBinary(OP_EQUALS, tidy(left), ast_Copy(W));
	work_Step(STEP_EQUATION, "Wronskian", names[0], right);
	ast_Cleanup(right);

	pcas_ast_t *parameters[2];
	for (unsigned i = 0; i < 2; i++) {
		left = ast_MakeBinary(OP_MULT, ast_Copy(basis[1 - i]), ast_Copy(f));
		if (i == 0)
			left = negate(left);
		left = ast_MakeBinary(OP_DIV, left, ast_Copy(W));

		parameters[i] = simplest(ast_Copy(left));
		right = ast_MakeBinary(OP_EQUALS, tidy(left), ast_Copy(parameters[i]));
		left = de_Derivative(names[i + 1], 1);
		work_Step(STEP_EQUATION, NULL, left, right);
		ast_Cleanup(left);
		ast_Cleanup(right);
	}

	taken = ast_MakeOperator(OP_ADD);
	for (unsigned i = 0; i < 3; i++)
		ast_ChildAppend(taken, ast_Copy(names[i]));
	fresh_Reserve(taken);

	for (unsigned i = 0; i < 2; i++) {
		parameters[i] = ast_MakeBinary(OP_INTEGRAL, parameters[i], ast_Copy(de->x));
		eval_integrals(parameters[i]);
	}

	fresh_Reserve(NULL);
	ast_Cleanup(taken);

	if (contains_integral(parameters[0]) || contains_integral(parameters[1])) {
		*particular = NULL;
	} else {
		for (unsigned i = 0; i < 2; i++) {
			work_Pause();
			simplify(parameters[i], SIMP_BASIC);
			work_Resume();
			work_Step(STEP_EQUATION, NULL, names[i + 1], parameters[i]);
		}

		left = ast_MakeBinary(
			OP_ADD,
			ast_MakeBinary(OP_MULT, ast_Copy(parameters[0]), ast_Copy(basis[0])),
			ast_MakeBinary(OP_MULT, ast_Copy(parameters[1]), ast_Copy(basis[1]))
		);
		*particular = ast_MakeBinary(OP_ADD, distribute(basis[0], parameters[0]), distribute(basis[1], parameters[1]));
		simplest(*particular);
		work_Step(STEP_EQUATION, "Particular solution", tidy(left), *particular);
		ast_Cleanup(left);
	}

	for (unsigned i = 0; i < 2; i++) {
		ast_Cleanup(derivatives[i]);
		ast_Cleanup(parameters[i]);
	}
	for (unsigned i = 0; i < 3; i++)
		ast_Cleanup(names[i]);
	ast_Cleanup(f);
	ast_Cleanup(W);

	return *particular != NULL ? E_SUCCESS : E_DE_INTEGRAL;
}

pcas_error_t solve_constant_coefficients(pcas_de_t *de, pcas_ast_t **solution) {
	if (!de->linear)
		return E_DE_UNSOLVED;

	root_t roots[DE_MAX_ORDER];
	pcas_ast_t *basis[DE_MAX_ORDER], *constants[DE_MAX_ORDER], *particular = NULL;
	unsigned count = 0, size = 0;

	pcas_ast_t *m = substitution_symbol(de, SYM_M);

	pcas_error_t err = characteristic_roots(de, m, roots, &count);
	if (err == E_SUCCESS) {
		de->method = "Constant coefficients";
		size = fill_basis(de, roots, count, basis);

		if (!choose_constants(de, m, constants, size)) {
			for (unsigned i = 0; i < size; i++)
				ast_Cleanup(basis[i]);
			size = 0;
			err = E_DE_UNSOLVED;
		}
	}

	if (err == E_SUCCESS && !is_ast_int(de->g, 0)) {
		pcas_ast_t *complementary = combination(constants, basis, size);
		work_Step(STEP_STATE, "Complementary solution", NULL, complementary);
		ast_Cleanup(complementary);

		pcas_ast_t *exclude = ast_MakeOperator(OP_ADD);
		ast_ChildAppend(exclude, ast_Copy(m));
		for (unsigned i = 0; i < size; i++)
			ast_ChildAppend(exclude, ast_Copy(constants[i]));

		err = undetermined_coefficients(de, roots, count, exclude, &particular);
		if (err == E_DE_UNSOLVED)
			err = variation_of_parameters(de, basis, size, exclude, &particular);

		ast_Cleanup(exclude);
	}

	if (err == E_SUCCESS)
		err = solve_with_basis(de, basis, constants, size, particular, solution);

	for (unsigned i = 0; i < size; i++) {
		ast_Cleanup(basis[i]);
		ast_Cleanup(constants[i]);
	}

	free_roots(roots, count);
	ast_Cleanup(m);

	return err;
}

pcas_error_t solve_reduction_of_order(pcas_de_t *de, pcas_ast_t **solution) {
	const pcas_ast_t *y1 = de->known;

	if (de->order != 2 || !de->linear || !is_ast_int(de->g, 0))
		return E_DE_UNSOLVED;

	de->method = "Reduction of order";

	bool satisfied;
	pcas_error_t err = check_solution(de, y1, "Known solution", false, &satisfied);
	if (err != E_SUCCESS)
		return err;
	if (!satisfied)
		return E_DE_NOT_SOLUTION;

	pcas_ast_t *P = quotient(de->a[1], de->a[2]);
	pcas_ast_t *Q = quotient(de->a[0], de->a[2]);

	if (!is_ast_int(de->a[2], 1)) {
		pcas_ast_t *left = ast_MakeOperator(OP_ADD);
		ast_ChildAppend(left, de_Derivative(de->y, 2));
		ast_ChildAppend(left, ast_MakeBinary(OP_MULT, ast_Copy(P), de_Derivative(de->y, 1)));
		ast_ChildAppend(left, ast_MakeBinary(OP_MULT, Q, ast_Copy(de->y)));
		pcas_ast_t *right = integer(0);
		work_Step(STEP_EQUATION, "Standard form", tidy(left), right);
		ast_Cleanup(left);
		ast_Cleanup(right);
	} else {
		ast_Cleanup(Q);
	}

	pcas_ast_t *left = substitution_symbol(de, SYM_P);
	work_Step(STEP_EQUATION, NULL, left, P);
	ast_Cleanup(left);

	work_Text("Reduction of order");

	work_Pause();
	P = negate(P);
	simplify(P, SIMP_BASIC);
	work_Resume();

	pcas_ast_t *mu = exponential_of_integral(P, de->x);
	if (mu == NULL)
		return E_DE_INTEGRAL;

	pcas_ast_t *integrand = ast_MakeBinary(OP_DIV, mu, ast_MakeBinary(OP_POW, ast_Copy(y1), integer(2)));
	left = ast_MakeBinary(OP_MULT, ast_Copy(y1), ast_MakeBinary(OP_INTEGRAL, ast_Copy(integrand), ast_Copy(de->x)));
	single_fraction(integrand);
	pcas_ast_t *integral = ast_MakeBinary(OP_INTEGRAL, integrand, ast_Copy(de->x));
	pcas_ast_t *right = ast_MakeBinary(OP_MULT, ast_Copy(y1), ast_Copy(integral));
	work_Step(STEP_EQUATION, NULL, left, right);
	ast_Cleanup(left);
	ast_Cleanup(right);

	eval_integrals(integral);

	if (contains_integral(integral)) {
		ast_Cleanup(integral);
		return E_DE_INTEGRAL;
	}

	pcas_ast_t *basis[2], *constants[2];
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

	if (choose_constants(de, NULL, constants, 2))
		err = solve_with_basis(de, basis, constants, 2, NULL, solution);
	else
		err = E_DE_UNSOLVED;

	for (unsigned i = 0; i < 2; i++) {
		ast_Cleanup(basis[i]);
		ast_Cleanup(constants[i]);
	}

	return err;
}
