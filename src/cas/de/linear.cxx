#include "internal.hxx"

#include "../../work.hxx"

static ast *reduced(ast *e) {
	work::pause();
	simplify(*e, Simp::All);
	work::resume();

	return e;
}

/*Simplifies e, keeping the smallest of its simplified, expanded and identity-reduced forms*/
static ast *simplest(ast *e) {
	work::pause();
	simplify(*e, Simp::Basic);
	expand_if_smaller(*e);
	work::resume();

	ast *candidate = reduced(e->copy());

	if (node_count(*candidate) < node_count(*e))
		e->replace(candidate);
	else
		ast::dispose(candidate);

	return e;
}

bool choose_constants(const DiffEq *de, const ast *exclude, ast **constants, unsigned n) {
	const char *candidates = "ABCDFGHJKLNPQRSUVW";
	ast *scope = ast::make(Op::Add);
	unsigned count = 0;

	scope->appendChild(de->equation->copy());
	scope->appendChild(de->x->copy());
	if (exclude != nullptr)
		scope->appendChild(exclude->copy());

	for (; *candidates != '\0' && count < n; candidates++) {
		if (!contains_symbol(*scope, (Sym)*candidates))
			constants[count++] = ast::make(static_cast<Sym>((Sym)*candidates));
	}

	ast::dispose(scope);

	if (count == n)
		return true;

	while (count > 0)
		ast::dispose(constants[--count]);

	return false;
}

/*Writes each initial condition as a linear equation in the constants, as row i of the augmented matrix*/
static void condition_rows(
	DiffEq *de,
	const ast &general,
	ast **constants,
	unsigned n,
	ast *matrix[][DiffEq::max_order + 1]
) {
	ast *derivatives[DiffEq::max_order], *zero = integer(0);
	unsigned highest = 0;

	for (unsigned i = 0; i < de->condition_count; i++) {
		if (de->conditions[i].order > highest)
			highest = de->conditions[i].order;
	}

	derivatives[0] = general.copy();

	for (unsigned k = 1; k <= highest; k++) {
		derivatives[k] = derivatives[k - 1]->copy();

		work::pause();
		derivative(*derivatives[k], *de->x, *de->x);
		work::resume();
		simplest(derivatives[k]);

		ast *e = DiffEq::derivative(*de->y, k);
		work::step(work::Step::Type::Equation, k == 1 ? "Differentiate" : nullptr, e, derivatives[k]);
		ast::dispose(e);
	}

	for (unsigned i = 0; i < de->condition_count; i++) {
		const DiffEq::Condition *c = &de->conditions[i];
		ast *e = derivatives[c->order]->copy();

		work::pause();
		substitute(*e, *de->x, *c->at);
		work::resume();
		reduced(e);

		ast *at = ast::make(Op::At, DiffEq::derivative(*de->y, c->order), c->at->copy());
		ast *chain = ast::make(Op::Equals, e->copy(), c->value->copy());
		work::step(work::Step::Type::Equation, i == 0 ? "Initial conditions" : nullptr, at, chain);
		ast::dispose(at);
		ast::dispose(chain);

		for (unsigned j = 0; j < n; j++) {
			matrix[i][j] = e->copy();

			work::pause();
			derivative(*matrix[i][j], *constants[j], *constants[j]);
			work::resume();
			reduced(matrix[i][j]);
		}

		work::pause();
		for (unsigned j = 0; j < n; j++)
			substitute(*e, *constants[j], *zero);
		work::resume();

		matrix[i][n] = reduced(difference(c->value->copy(), e));
	}

	for (unsigned k = 0; k <= highest; k++)
		ast::dispose(derivatives[k]);
	ast::dispose(zero);
}

/*Reduces the augmented matrix with Gauss-Jordan elimination, setting pivots to the column of each pivot row, and returns the number of pivot rows*/
static unsigned eliminate(ast *matrix[][DiffEq::max_order + 1], unsigned rows, unsigned n, unsigned *pivots) {
	unsigned rank = 0;

	for (unsigned j = 0; j < n && rank < rows; j++) {
		unsigned row = rank;
		while (row < rows && is_zero(*matrix[row][j]))
			row++;

		if (row == rows)
			continue;

		for (unsigned k = 0; k <= n; k++) {
			ast *swap = matrix[row][k];
			matrix[row][k] = matrix[rank][k];
			matrix[rank][k] = swap;
		}

		ast *pivot = matrix[rank][j];
		for (unsigned k = 0; k <= n; k++) {
			if (k != j)
				matrix[rank][k] = reduced(ast::make(Op::Div, matrix[rank][k], pivot->copy()));
		}
		ast::dispose(pivot);
		matrix[rank][j] = integer(1);

		for (unsigned i = 0; i < rows; i++) {
			if (i == rank || is_zero(*matrix[i][j]))
				continue;

			ast *factor = matrix[i][j];
			for (unsigned k = 0; k <= n; k++) {
				if (k != j) {
					matrix[i][k] =
						reduced(difference(matrix[i][k], ast::make(Op::Mult, factor->copy(), matrix[rank][k]->copy())));
				}
			}
			ast::dispose(factor);
			matrix[i][j] = integer(0);
		}

		pivots[rank++] = j;
	}

	return rank;
}

/*Finds the constants in general from the initial conditions and substitutes them. Takes ownership of general.*/
static Error apply_conditions(DiffEq *de, ast *general, ast **constants, unsigned n, ast **solution) {
	ast *matrix[DiffEq::max_conditions][DiffEq::max_order + 1];
	unsigned pivots[DiffEq::max_conditions];
	const unsigned rows = de->condition_count;
	Error err = Error::Success;

	condition_rows(de, *general, constants, n, matrix);
	const unsigned rank = eliminate(matrix, rows, n, pivots);

	for (unsigned i = rank; i < rows; i++) {
		if (!is_zero(*matrix[i][n]))
			err = Error::DeNoSolution;
	}

	for (unsigned i = 0; i < rank && err == Error::Success; i++) {
		ast *value = matrix[i][n]->copy();

		for (unsigned k = 0; k < n; k++) {
			bool pivot = false;
			for (unsigned r = 0; r < rank; r++)
				pivot |= pivots[r] == k;

			if (!pivot && !is_zero(*matrix[i][k]))
				value = difference(value, ast::make(Op::Mult, matrix[i][k]->copy(), constants[k]->copy()));
		}

		reduced(value);
		single_fraction(*value);
		work::step(
			work::Step::Type::Equation, i == 0 ? "Solve for the constants" : nullptr, constants[pivots[i]], value
		);

		work::pause();
		substitute(*general, *constants[pivots[i]], *value);
		work::resume();

		ast::dispose(value);
	}

	for (unsigned i = 0; i < rows; i++) {
		for (unsigned k = 0; k <= n; k++)
			ast::dispose(matrix[i][k]);
	}

	if (err != Error::Success) {
		ast::dispose(general);
		return err;
	}

	work::pause();
	simplify(*general, Simp::Basic);
	work::resume();

	work::step(work::Step::Type::Equation, "Solution", de->y, general);
	*solution = ast::make(Op::Equals, de->y->copy(), general);

	return Error::Success;
}

/*Returns a/b simplified as one fraction with common factors cancelled and an expanded denominator*/
static ast *quotient(const ast &a, const ast &b) {
	ast *q = ast::make(Op::Div, a.copy(), b.copy());

	work::pause();
	simplify(*q, Simp::Basic);
	factor_cancel(*q);

	if (q->isOp(Op::Div)) {
		expand(*q->firstChild()->next(), Expand::All);
		simplify(*q->firstChild()->next(), Simp::Basic);
	}
	work::resume();

	return q;
}

/*Returns a times b, expanded term by term when that leaves fewer nodes*/
static ast *distribute(const ast &a, const ast &b) {
	ast *product = ast::make(Op::Mult, a.copy(), b.copy());

	work::pause();
	simplify(*product, Simp::Basic);

	if (b.isOp(Op::Add)) {
		ast *sum = ast::make(Op::Add);

		for (const ast &term : b.children()) {
			sum->appendChild(ast::make(Op::Mult, a.copy(), term.copy()));
			simplify(*sum->lastChild(), Simp::Basic);
		}

		expand(*sum, Expand::All);

		simplify(*sum, Simp::Basic);

		if (node_count(*sum) < node_count(*product)) {
			ast::dispose(product);
			product = sum;
		} else {
			ast::dispose(sum);
		}
	}
	work::resume();

	return product;
}

/*Returns the sum of each constant times its basis function*/
static ast *combination(ast **constants, ast **basis, unsigned size) {
	ast *sum = ast::make(Op::Add);

	for (unsigned i = 0; i < size; i++)
		sum->appendChild(ast::make(Op::Mult, constants[i]->copy(), basis[i]->copy()));

	return tidy(sum);
}

/*Records y as the combination of the basis with the constants, plus particular unless it is nullptr, and finds the constants from the initial conditions. Takes ownership of particular.*/
static Error solve_with_basis(
	DiffEq *de,
	ast **basis,
	ast **constants,
	unsigned size,
	ast *particular,
	ast **solution
) {
	ast *general = combination(constants, basis, size);

	if (particular != nullptr)
		general = tidy(ast::make(Op::Add, general, particular));

	if (de->condition_count > 0) {
		work::step(work::Step::Type::Equation, "General solution", de->y, general);
		return apply_conditions(de, general, constants, size, solution);
	}

	work::step(work::Step::Type::Equation, "Solution", de->y, general);
	*solution = ast::make(Op::Equals, de->y->copy(), general);

	return Error::Success;
}

/*Returns preferred, or a symbol that does not appear in the equation or taken if preferred does*/
static ast *pick_symbol(const DiffEq *de, Sym preferred, const ast &taken) {
	ast *scope = ast::make(Op::Add);

	scope->appendChild(de->equation->copy());
	scope->appendChild(de->x->copy());
	scope->appendChild(taken.copy());
	const Sym symbol = contains_symbol(*scope, preferred) ? fresh_symbol(*scope) : preferred;
	ast::dispose(scope);

	return ast::make(static_cast<Sym>(symbol));
}

/*Returns the constant slope of e in x if e is linear in x, otherwise nullptr*/
static ast *linear_slope(const DiffEq *de, const ast &e) {
	ast *slope = e.copy();

	work::pause();
	derivative(*slope, *de->x, *de->x);
	simplify(*slope, Simp::Basic);
	ast *second = slope->copy();
	derivative(*second, *de->x, *de->x);
	simplify(*second, Simp::Basic);
	work::resume();

	if (involves(*slope, *de->x) || !second->isInt(0)) {
		ast::dispose(slope);
		slope = nullptr;
	}

	ast::dispose(second);

	return slope;
}

/*Reads term as a constant times x^degree e^(rate*x), times cos or sin of frequency*x unless frequency is zero. Returns false if it has another form.*/
static bool forcing_form(const DiffEq *de, const ast *term, unsigned *degree, ast **rate, ast **frequency) {
	bool form = true;

	if (term->isOp(Op::Div) && !involves(*term->firstChild()->next(), *de->x))
		term = term->firstChild();

	const unsigned count = term->isOp(Op::Mult) ? term->childCount() : 1;
	*degree = 0;
	*rate = integer(0);
	*frequency = nullptr;

	for (unsigned i = 0; i < count && form; i++) {
		const ast *factor = term->isOp(Op::Mult) ? term->childAt(i) : term;

		if (!involves(*factor, *de->x))
			continue;

		ast *slope;
		mp_small n;
		if (factor->compare(*de->x)) {
			(*degree)++;
		} else if (
			factor->isOp(Op::Pow) && factor->firstChild()->compare(*de->x) &&
			factor->firstChild()->next()->isNumber() && factor->firstChild()->next()->num().toInt(n) && n > 0 &&
			n <= DiffEq::max_order
		) {
			*degree += (unsigned)n;
		} else if (
			factor->isOp(Op::Pow) && is_euler(*factor->firstChild()) &&
			(slope = linear_slope(de, *factor->firstChild()->next())) != nullptr
		) {
			*rate = ast::make(Op::Add, *rate, slope);
		} else if (
			(factor->isOp(Op::Sin) || factor->isOp(Op::Cos)) && *frequency == nullptr &&
			(slope = linear_slope(de, *factor->firstChild())) != nullptr
		) {
			*frequency = slope;
		} else {
			form = false;
		}
	}

	if (*frequency == nullptr)
		*frequency = integer(0);

	work::pause();
	simplify(**rate, Simp::Basic);
	if (is_negative_for_sure(**frequency))
		*frequency = negate(*frequency);
	simplify(**frequency, Simp::Basic);
	work::resume();

	if (!form || *degree > DiffEq::max_order) {
		ast::dispose(*rate);
		ast::dispose(*frequency);
		return false;
	}

	return true;
}

/*Returns the multiplicity of rate + frequency*i as a characteristic root*/
static unsigned root_multiplicity(const root_t *roots, unsigned count, const ast &rate, const ast &frequency) {
	unsigned multiplicity = 0;
	const bool real = frequency.isInt(0);

	for (unsigned i = 0; i < count && multiplicity == 0; i++) {
		if ((roots[i].im == nullptr) != real)
			continue;

		ast *re = difference(roots[i].re->copy(), rate.copy());
		ast *im = real ? integer(0) : difference(roots[i].im->copy(), frequency.copy());

		if (is_zero(*re) && is_zero(*im))
			multiplicity = roots[i].multiplicity;

		ast::dispose(re);
		ast::dispose(im);
	}

	return multiplicity;
}

/*Returns the factors of term that involve x, or 1*/
static ast *function_part(const DiffEq *de, const ast &term) {
	if (!involves(term, *de->x))
		return integer(1);

	ast *part;
	if (term.isOp(Op::Mult)) {
		part = ast::make(Op::Mult);
		for (const ast &child : term.children()) {
			if (involves(child, *de->x))
				part->appendChild(child.copy());
		}
	} else if (term.isOp(Op::Div)) {
		part = ast::make(Op::Div, function_part(de, *term.firstChild()), function_part(de, *term.firstChild()->next()));
	} else {
		part = term.copy();
	}

	work::pause();
	simplify(*part, Simp::Basic);
	work::resume();

	return part;
}

#define MAX_ROWS (2 * DiffEq::max_order)

/*Substitutes the trial solution into the equation and solves for its unknowns by equating the coefficients of each function of x, recording the work*/
static Error match_coefficients(DiffEq *de, const ast &trial, ast **unknowns, unsigned size, ast **particular) {
	ast *derivatives[DiffEq::max_order + 1], *functions[MAX_ROWS], *sums[MAX_ROWS],
		*matrix[MAX_ROWS][DiffEq::max_order + 1];
	unsigned pivots[MAX_ROWS], groups = 0, rows = 0;
	Error err = Error::Success;

	derivatives[0] = trial.copy();

	for (unsigned k = 1; k <= de->order; k++) {
		derivatives[k] = derivatives[k - 1]->copy();

		work::pause();
		derivative(*derivatives[k], *de->x, *de->x);
		simplify(*derivatives[k], Simp::Basic);
		expand_if_smaller(*derivatives[k]);
		work::resume();

		ast *prime = DiffEq::derivative(*de->y, k);
		work::step(work::Step::Type::Equation, k == 1 ? "Differentiate" : nullptr, prime, derivatives[k]);
		ast::dispose(prime);
	}

	ast *left = ast::make(Op::Add);
	for (unsigned k = de->order + 1; k-- > 0;) {
		if (!de->a[k]->isInt(0))
			left->appendChild(ast::make(Op::Mult, de->a[k]->copy(), derivatives[k]->copy()));
	}
	work::step(work::Step::Type::Equation, "Substitute", tidy(left), de->g);
	ast *before = left->copy();

	work::pause();
	simplify(*left, Simp::Basic);
	expand(*left, Expand::All);
	simplify(*left, Simp::Basic);
	work::resume();

	if (!left->compare(*before))
		work::step(work::Step::Type::Equation, nullptr, left, de->g);
	ast::dispose(before);

	ast *residual = difference(left, de->g->copy());

	work::pause();
	expand(*residual, Expand::All);
	simplify(*residual, Simp::Basic);
	work::resume();

	for (unsigned i = 0; i < (residual->isOp(Op::Add) ? residual->childCount() : 1) && err == Error::Success; i++) {
		const ast *term = residual->isOp(Op::Add) ? residual->childAt(i) : residual;
		ast *function = function_part(de, *term);

		unsigned j = 0;
		while (j < groups && !functions[j]->compare(*function))
			j++;

		if (j < groups) {
			sums[j]->appendChild(term->copy());
			ast::dispose(function);
		} else if (groups < MAX_ROWS) {
			functions[groups] = function;
			sums[groups++] = ast::make(Op::Add, term->copy());
		} else {
			ast::dispose(function);
			err = Error::DeUnsolved;
		}
	}

	ast *zero = integer(0);

	for (unsigned i = 0; i < groups && err == Error::Success; i++) {
		bool nonzero = false;

		for (unsigned j = 0; j < size; j++) {
			matrix[rows][j] = sums[i]->copy();

			work::pause();
			derivative(*matrix[rows][j], *unknowns[j], *unknowns[j]);
			work::resume();
			matrix[rows][j] = reduced(ast::make(Op::Div, matrix[rows][j], functions[i]->copy()));

			nonzero |= !is_zero(*matrix[rows][j]);
		}

		ast *constant = sums[i]->copy();

		work::pause();
		for (unsigned j = 0; j < size; j++)
			substitute(*constant, *unknowns[j], *zero);
		work::resume();

		matrix[rows][size] = reduced(ast::make(Op::Div, negate(constant), functions[i]->copy()));

		if (!nonzero) {
			if (!is_zero(*matrix[rows][size]))
				err = Error::DeUnsolved;

			for (unsigned j = 0; j <= size; j++)
				ast::dispose(matrix[rows][j]);
			continue;
		}

		ast *sum = ast::make(Op::Add);
		for (unsigned j = 0; j < size; j++)
			sum->appendChild(ast::make(Op::Mult, matrix[rows][j]->copy(), unknowns[j]->copy()));

		work::pause();
		simplify(*sum, Simp::Basic);
		work::resume();

		work::step(work::Step::Type::Equation, rows == 0 ? "Equate coefficients" : nullptr, sum, matrix[rows][size]);
		ast::dispose(sum);
		rows++;
	}

	unsigned rank;
	if (err == Error::Success) {
		rank = eliminate(matrix, rows, size, pivots);

		for (unsigned i = rank; i < rows; i++) {
			if (!is_zero(*matrix[i][size]))
				err = Error::DeUnsolved;
		}
	}

	if (err == Error::Success) {
		*particular = trial.copy();

		for (unsigned j = 0; j < size; j++) {
			ast *value = nullptr;
			for (unsigned i = 0; i < rank && value == nullptr; i++) {
				if (pivots[i] == j)
					value = matrix[i][size]->copy();
			}
			if (value == nullptr)
				value = integer(0);

			single_fraction(*value);
			work::step(work::Step::Type::Equation, j == 0 ? "Solve for the coefficients" : nullptr, unknowns[j], value);

			work::pause();
			substitute(**particular, *unknowns[j], *value);
			work::resume();
			ast::dispose(value);
		}

		simplest(*particular);
		work::step(work::Step::Type::State, "Particular solution", nullptr, *particular);
	}

	for (unsigned i = 0; i < rows; i++) {
		for (unsigned j = 0; j <= size; j++)
			ast::dispose(matrix[i][j]);
	}

	for (unsigned i = 0; i < groups; i++) {
		ast::dispose(functions[i]);
		ast::dispose(sums[i]);
	}

	for (unsigned k = 0; k <= de->order; k++)
		ast::dispose(derivatives[k]);

	ast::dispose(residual);
	ast::dispose(zero);

	return err;
}

/*Returns op(frequency*x)*/
static ast *oscillation(const DiffEq *de, Op op, const ast &frequency) {
	return ast::make(op, ast::make(Op::Mult, frequency.copy(), de->x->copy()));
}

/*Appends unknown*x^j e^(rate*x) f to trial, leaving out f when it is nullptr. Takes ownership of f.*/
static void add_trial_term(const DiffEq *de, ast &trial, const ast &unknown, unsigned j, const ast &rate, ast *f) {
	trial.appendChild(ast::make(Op::Mult, unknown.copy(), basis_function(de, j, rate, f)));
}

/*Finds a particular solution with undetermined coefficients when each term of g is a polynomial times e^(ax) times cos(bx) or sin(bx), recording the work*/
static Error undetermined_coefficients(
	DiffEq *de,
	const root_t *roots,
	unsigned count,
	const ast &exclude,
	ast **particular
) {
	ast *rates[DiffEq::max_order], *frequencies[DiffEq::max_order], *unknowns[DiffEq::max_order];
	unsigned degrees[DiffEq::max_order], groups = 0, size = 0;
	Error err = Error::Success;

	ast *g = de->g->copy();

	work::pause();
	expand(*g, Expand::All);
	simplify(*g, Simp::Basic);
	work::resume();

	for (unsigned i = 0; i < (g->isOp(Op::Add) ? g->childCount() : 1) && err == Error::Success; i++) {
		const ast *term = g->isOp(Op::Add) ? g->childAt(i) : g;
		unsigned degree;
		ast *rate, *frequency;

		if (!forcing_form(de, term, &degree, &rate, &frequency)) {
			err = Error::DeUnsolved;
			continue;
		}

		unsigned j = 0;
		while (j < groups && !(rates[j]->compare(*rate) && frequencies[j]->compare(*frequency)))
			j++;

		if (j < groups) {
			if (degree > degrees[j])
				degrees[j] = degree;
			ast::dispose(rate);
			ast::dispose(frequency);
		} else if (groups < DiffEq::max_order) {
			rates[groups] = rate;
			frequencies[groups] = frequency;
			degrees[groups++] = degree;
		} else {
			ast::dispose(rate);
			ast::dispose(frequency);
			err = Error::DeUnsolved;
		}
	}

	ast::dispose(g);

	for (unsigned i = 0; i < groups; i++)
		size += (degrees[i] + 1) * (frequencies[i]->isInt(0) ? 1 : 2);

	if (err == Error::Success && (size > DiffEq::max_order || !choose_constants(de, &exclude, unknowns, size)))
		err = Error::DeUnsolved;

	if (err == Error::Success) {
		de->method = "Undetermined coefficients";
		work::text("Undetermined coefficients");

		ast *trial = ast::make(Op::Add);
		unsigned k = 0;

		for (unsigned i = 0; i < groups; i++) {
			const unsigned shift = root_multiplicity(roots, count, *rates[i], *frequencies[i]);

			for (unsigned j = degrees[i] + 1; j-- > 0;) {
				if (frequencies[i]->isInt(0)) {
					add_trial_term(de, *trial, *unknowns[k++], j + shift, *rates[i], nullptr);
				} else {
					add_trial_term(
						de, *trial, *unknowns[k++], j + shift, *rates[i], oscillation(de, Op::Cos, *frequencies[i])
					);
					add_trial_term(
						de, *trial, *unknowns[k++], j + shift, *rates[i], oscillation(de, Op::Sin, *frequencies[i])
					);
				}
			}
		}

		tidy(trial);
		work::step(work::Step::Type::Equation, "Trial solution", de->y, trial);

		err = match_coefficients(de, *trial, unknowns, size, particular);

		ast::dispose(trial);
		for (unsigned i = 0; i < size; i++)
			ast::dispose(unknowns[i]);
	}

	for (unsigned i = 0; i < groups; i++) {
		ast::dispose(rates[i]);
		ast::dispose(frequencies[i]);
	}

	return err;
}

/*Finds a particular solution of a second order equation by variation of parameters, recording the work*/
static Error variation_of_parameters(DiffEq *de, ast **basis, unsigned size, const ast &exclude, ast **particular) {
	if (de->order != 2 || size != 2)
		return Error::DeUnsolved;

	de->method = "Variation of parameters";
	work::text("Variation of parameters");

	ast *f = quotient(*de->g, *de->a[2]);

	if (!de->a[2]->isInt(1)) {
		ast *left = ast::make(Op::Add);
		left->appendChild(DiffEq::derivative(*de->y, 2));
		left->appendChild(ast::make(Op::Mult, quotient(*de->a[1], *de->a[2]), DiffEq::derivative(*de->y, 1)));
		left->appendChild(ast::make(Op::Mult, quotient(*de->a[0], *de->a[2]), de->y->copy()));
		work::step(work::Step::Type::Equation, "Standard form", tidy(left), f);
		ast::dispose(left);
	}

	ast *derivatives[2];
	for (unsigned i = 0; i < 2; i++) {
		derivatives[i] = basis[i]->copy();

		work::pause();
		derivative(*derivatives[i], *de->x, *de->x);
		work::resume();
		simplest(derivatives[i]);
	}

	ast *names[3];
	ast *taken = ast::make(Op::Add);
	taken->appendChild(exclude.copy());
	for (unsigned i = 0; i < 3; i++) {
		names[i] = pick_symbol(de, i == 0 ? Sym::W : i == 1 ? Sym::U : Sym::V, *taken);
		taken->appendChild(names[i]->copy());
	}
	ast::dispose(taken);

	ast *left = difference(
		ast::make(Op::Mult, basis[0]->copy(), derivatives[1]->copy()),
		ast::make(Op::Mult, derivatives[0]->copy(), basis[1]->copy())
	);
	ast *W = simplest(left->copy());
	ast *right = ast::make(Op::Equals, tidy(left), W->copy());
	work::step(work::Step::Type::Equation, "Wronskian", names[0], right);
	ast::dispose(right);

	ast *parameters[2];
	for (unsigned i = 0; i < 2; i++) {
		left = ast::make(Op::Mult, basis[1 - i]->copy(), f->copy());
		if (i == 0)
			left = negate(left);
		left = ast::make(Op::Div, left, W->copy());

		parameters[i] = simplest(left->copy());
		right = ast::make(Op::Equals, tidy(left), parameters[i]->copy());
		left = DiffEq::derivative(*names[i + 1], 1);
		work::step(work::Step::Type::Equation, nullptr, left, right);
		ast::dispose(left);
		ast::dispose(right);
	}

	taken = ast::make(Op::Add);
	for (unsigned i = 0; i < 3; i++)
		taken->appendChild(names[i]->copy());
	fresh_Reserve(taken);

	for (unsigned i = 0; i < 2; i++) {
		parameters[i] = ast::make(Op::Integral, parameters[i], de->x->copy());
		eval_integrals(*parameters[i]);
	}

	fresh_Reserve(nullptr);
	ast::dispose(taken);

	if (contains_integral(*parameters[0]) || contains_integral(*parameters[1])) {
		*particular = nullptr;
	} else {
		for (unsigned i = 0; i < 2; i++) {
			work::pause();
			simplify(*parameters[i], Simp::Basic);
			work::resume();
			work::step(work::Step::Type::Equation, nullptr, names[i + 1], parameters[i]);
		}

		left = ast::make(
			Op::Add,
			ast::make(Op::Mult, parameters[0]->copy(), basis[0]->copy()),
			ast::make(Op::Mult, parameters[1]->copy(), basis[1]->copy())
		);
		*particular = ast::make(Op::Add, distribute(*basis[0], *parameters[0]), distribute(*basis[1], *parameters[1]));
		simplest(*particular);
		work::step(work::Step::Type::Equation, "Particular solution", tidy(left), *particular);
		ast::dispose(left);
	}

	for (unsigned i = 0; i < 2; i++) {
		ast::dispose(derivatives[i]);
		ast::dispose(parameters[i]);
	}
	for (unsigned i = 0; i < 3; i++)
		ast::dispose(names[i]);
	ast::dispose(f);
	ast::dispose(W);

	return *particular != nullptr ? Error::Success : Error::DeIntegral;
}

Error solve_constant_coefficients(DiffEq *de, ast **solution) {
	if (!de->linear)
		return Error::DeUnsolved;

	root_t roots[DiffEq::max_order];
	ast *basis[DiffEq::max_order], *constants[DiffEq::max_order], *particular = nullptr;
	unsigned count = 0, size = 0;

	ast *m = substitution_symbol(de, Sym::M);

	Error err = characteristic_roots(de, *m, roots, &count);
	if (err == Error::Success) {
		de->method = "Constant coefficients";
		size = fill_basis(de, roots, count, basis);

		if (!choose_constants(de, m, constants, size)) {
			for (unsigned i = 0; i < size; i++)
				ast::dispose(basis[i]);
			size = 0;
			err = Error::DeUnsolved;
		}
	}

	if (err == Error::Success && !de->g->isInt(0)) {
		ast *complementary = combination(constants, basis, size);
		work::step(work::Step::Type::State, "Complementary solution", nullptr, complementary);
		ast::dispose(complementary);

		ast *exclude = ast::make(Op::Add);
		exclude->appendChild(m->copy());
		for (unsigned i = 0; i < size; i++)
			exclude->appendChild(constants[i]->copy());

		err = undetermined_coefficients(de, roots, count, *exclude, &particular);
		if (err == Error::DeUnsolved)
			err = variation_of_parameters(de, basis, size, *exclude, &particular);

		ast::dispose(exclude);
	}

	if (err == Error::Success)
		err = solve_with_basis(de, basis, constants, size, particular, solution);

	for (unsigned i = 0; i < size; i++) {
		ast::dispose(basis[i]);
		ast::dispose(constants[i]);
	}

	free_roots(roots, count);
	ast::dispose(m);

	return err;
}

Error solve_reduction_of_order(DiffEq *de, ast **solution) {
	const ast &y1 = *de->known;

	if (de->order != 2 || !de->linear || !de->g->isInt(0))
		return Error::DeUnsolved;

	de->method = "Reduction of order";

	bool satisfied;
	Error err = check_solution(de, y1, "Known solution", false, &satisfied);
	if (err != Error::Success)
		return err;
	if (!satisfied)
		return Error::DeNotSolution;

	ast *P = quotient(*de->a[1], *de->a[2]);
	ast *Q = quotient(*de->a[0], *de->a[2]);

	if (!de->a[2]->isInt(1)) {
		ast *left = ast::make(Op::Add);
		left->appendChild(DiffEq::derivative(*de->y, 2));
		left->appendChild(ast::make(Op::Mult, P->copy(), DiffEq::derivative(*de->y, 1)));
		left->appendChild(ast::make(Op::Mult, Q, de->y->copy()));
		ast *right = integer(0);
		work::step(work::Step::Type::Equation, "Standard form", tidy(left), right);
		ast::dispose(left);
		ast::dispose(right);
	} else {
		ast::dispose(Q);
	}

	ast *left = substitution_symbol(de, Sym::P);
	work::step(work::Step::Type::Equation, nullptr, left, P);
	ast::dispose(left);

	work::text("Reduction of order");

	work::pause();
	P = negate(P);
	simplify(*P, Simp::Basic);
	work::resume();

	ast *mu = exponential_of_integral(P, *de->x);
	if (mu == nullptr)
		return Error::DeIntegral;

	ast *integrand = ast::make(Op::Div, mu, ast::make(Op::Pow, y1.copy(), integer(2)));
	left = ast::make(Op::Mult, y1.copy(), ast::make(Op::Integral, integrand->copy(), de->x->copy()));
	single_fraction(*integrand);
	ast *integral = ast::make(Op::Integral, integrand, de->x->copy());
	ast *right = ast::make(Op::Mult, y1.copy(), integral->copy());
	work::step(work::Step::Type::Equation, nullptr, left, right);
	ast::dispose(left);
	ast::dispose(right);

	eval_integrals(*integral);

	if (contains_integral(*integral)) {
		ast::dispose(integral);
		return Error::DeIntegral;
	}

	ast *basis[2], *constants[2];
	basis[0] = y1.copy();
	basis[1] = distribute(y1, *integral);
	left = ast::make(Op::Mult, y1.copy(), integral);

	work::pause();
	simplify(*left, Simp::Normalize);
	work::resume();

	if (left->compare(*basis[1]))
		work::step(work::Step::Type::State, "Second solution", nullptr, basis[1]);
	else
		work::step(work::Step::Type::Equation, "Second solution", left, basis[1]);
	ast::dispose(left);

	if (choose_constants(de, nullptr, constants, 2))
		err = solve_with_basis(de, basis, constants, 2, nullptr, solution);
	else
		err = Error::DeUnsolved;

	for (unsigned i = 0; i < 2; i++) {
		ast::dispose(basis[i]);
		ast::dispose(constants[i]);
	}

	return err;
}
