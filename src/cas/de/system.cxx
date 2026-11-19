#include "internal.hxx"

#include "../../work.hxx"
#include "../integral.hxx"

constexpr unsigned SIZE = DiffEqSystem::size;

static ast *basic(ast *e) {
	work::pause();
	simplify(*e, Simp::Basic);
	work::resume();

	return e;
}

/*Simplifies e without identities, expanding it when that leaves fewer nodes, and returns it*/
static ast *compact(ast *e) {
	work::pause();
	simplify(*e, Simp::Basic);
	expand_if_smaller(*e);
	work::resume();

	return e;
}

static bool has_op(const ast &e, Op op);

/*Simplifies e like compact, also with identities when it has a sine or cosine, and returns it*/
static ast *settle(ast *e) {
	if (has_op(*e, Op::Sin) || has_op(*e, Op::Cos))
		return simplest(e);
	return compact(e);
}

static bool has_op(const ast &e, Op op) {
	if (e.isOp(op))
		return true;

	if (!e.isOperator())
		return false;

	for (const ast &child : e.children()) {
		if (has_op(child, op))
			return true;
	}

	return false;
}

/*True if e is a matrix with SIZE rows of columns entries each*/
static bool is_matrix(const ast &e, unsigned columns) {
	if (!e.isOp(Op::Matrix) || e.childCount() != SIZE)
		return false;

	for (const ast &row : e.children()) {
		if (!row.isOp(Op::Matrix) || row.childCount() != columns)
			return false;
	}

	return true;
}

/*Returns the column vector with the entries. Takes ownership of them.*/
static ast *column(ast *top, ast *bottom) {
	return ast::make(Op::Matrix, ast::make(Op::Matrix, top), ast::make(Op::Matrix, bottom));
}

static ast *column_of(ast *const *entries) {
	return column(entries[0]->copy(), entries[1]->copy());
}

static ast *square(ast *const entries[][SIZE]) {
	ast *matrix = ast::make(Op::Matrix);

	for (unsigned i = 0; i < SIZE; i++)
		matrix->appendChild(ast::make(Op::Matrix, entries[i][0]->copy(), entries[i][1]->copy()));

	return matrix;
}

static void dispose_square(ast *entries[][SIZE]) {
	for (unsigned i = 0; i < SIZE; i++) {
		for (unsigned j = 0; j < SIZE; j++) {
			ast::dispose(entries[i][j]);
			entries[i][j] = nullptr;
		}
	}
}

static ast *determinant(ast *const m[][SIZE]) {
	return difference(
		ast::make(Op::Mult, m[0][0]->copy(), m[1][1]->copy()), ast::make(Op::Mult, m[0][1]->copy(), m[1][0]->copy())
	);
}

/*Returns the entry of the adjugate of m, which is its inverse times its determinant*/
static ast *adjugate_entry(ast *const m[][SIZE], unsigned i, unsigned j) {
	return i == j ? m[1 - i][1 - i]->copy() : negate(m[i][j]->copy());
}

/*Returns a times b, leaving out factors of 1. Takes ownership of a and b.*/
static ast *product(ast *a, ast *b) {
	if (a->isInt(1)) {
		ast::dispose(a);
		return b;
	}

	if (b->isInt(1)) {
		ast::dispose(b);
		return a;
	}

	ast *result = a->isOp(Op::Mult) ? a : ast::make(Op::Mult, a);

	if (b->isOp(Op::Mult)) {
		result->takeChildren(*b);
		ast::dispose(b);
	} else {
		result->appendChild(b);
	}

	return result;
}

/*Returns the entry in row of the column vector that form stands for, where form is made of column vectors, sums and scalar factors*/
static ast *entry_of(const ast &form, unsigned row) {
	if (form.isOp(Op::Matrix))
		return form.childAt(row)->firstChild()->copy();

	if (!form.isOp(Op::Add) && !form.isOp(Op::Mult))
		return form.copy();

	ast *result = ast::make(form.op());

	for (const ast &child : form.children())
		result->appendChild(form.isOp(Op::Add) || has_op(child, Op::Matrix) ? entry_of(child, row) : child.copy());

	return result;
}

/*Returns e^(rate*t), or 1 when rate is zero*/
static ast *growth(const ast &rate, const ast &t) {
	if (rate.isInt(0))
		return integer(1);

	return ast::make(Op::Pow, ast::make(Sym::Euler), tidy(ast::make(Op::Mult, rate.copy(), t.copy())));
}

static ast *subscripted(Sym name, ast *subscript) {
	return ast::make(Op::Subscript, ast::make(name), subscript);
}

/*Returns the name of the solution vector*/
static ast *vector_name(const DiffEqSystem &system) {
	return system.vector != nullptr ? system.vector->copy() : ast::make(Sym::X);
}

/*Returns the unknown vector, written as a column of the unknowns unless it was entered in matrix form*/
static ast *unknown_vector(const DiffEqSystem &system) {
	if (system.vector != nullptr)
		return system.vector->copy();

	return column(system.unknowns[0]->copy(), system.unknowns[1]->copy());
}

static bool homogeneous(const DiffEqSystem &system) {
	return system.forcing[0]->isInt(0) && system.forcing[1]->isInt(0);
}

/*Adds the symbols that have primes in e to symbols. Returns false if there are more than SIZE.*/
static bool primed_symbols(const ast &e, ast **symbols, unsigned *count) {
	if (e.isOp(Op::Prime)) {
		const ast *inner = &e;
		while (inner->isOp(Op::Prime))
			inner = inner->firstChild();

		if (!inner->isSymbol())
			return true;

		for (unsigned i = 0; i < *count; i++) {
			if (symbols[i]->compare(*inner))
				return true;
		}

		if (*count == SIZE)
			return false;

		symbols[(*count)++] = inner->copy();
		return true;
	}

	if (e.isOperator()) {
		for (const ast &child : e.children()) {
			if (!primed_symbols(child, symbols, count))
				return false;
		}
	}

	return true;
}

bool DiffEqSystem::detect(ast *const *items, unsigned count) {
	if (count == 0)
		return false;

	if (has_op(*items[0], Op::Matrix))
		return true;

	ast *symbols[SIZE];
	unsigned found = 0;
	bool fits = true;

	for (unsigned i = 0; i < count && fits; i++)
		fits = primed_symbols(*items[i], symbols, &found);

	for (unsigned i = 0; i < found; i++)
		ast::dispose(symbols[i]);

	return !fits || found == SIZE;
}

DiffEqSystem::~DiffEqSystem() {
	clear();
}

void DiffEqSystem::clear() {
	ast::dispose(variable);
	ast::dispose(vector);
	ast::dispose(point);
	variable = vector = point = nullptr;

	for (unsigned i = 0; i < SIZE; i++) {
		ast::dispose(unknowns[i]);
		ast::dispose(forcing[i]);
		ast::dispose(values[i]);
		unknowns[i] = forcing[i] = values[i] = nullptr;
	}

	dispose_square(matrix);
	method = nullptr;
}

/*Records the equation, an equals node or an expression equal to zero, as entered*/
static void record_equation(const ast &equation) {
	ast *zero = integer(0);
	const bool equals = equation.isOp(Op::Equals);

	work::step(
		work::Step::Type::Equation,
		nullptr,
		equals ? equation.firstChild() : &equation,
		equals ? equation.firstChild()->next() : zero
	);

	ast::dispose(zero);
}

/*Returns the derivative of f with respect to symbol, simplified*/
static ast *coefficient(const ast &f, const ast &symbol) {
	ast *slope = f.copy();

	work::pause();
	derivative(*slope, symbol, symbol);
	simplify(*slope, Simp::Basic);
	work::resume();

	return slope;
}

/*Reads a condition like X(0)=1 for an unknown, or X(0)=[[1][0]] for the vector, setting the point and the values*/
static Error load_condition(DiffEqSystem &system, const ast &condition) {
	if (!condition.isOp(Op::Equals))
		return Error::DeBadCondition;

	/*X(0) is parsed as X*0*/
	const ast &left = *condition.firstChild();
	const ast &value = *left.next();
	if (!left.isOp(Op::Mult) || left.childCount() != 2)
		return Error::DeBadCondition;

	const ast &function = *left.firstChild();
	ast *at = basic(function.next()->copy());

	if (system.point != nullptr && !system.point->compare(*at)) {
		ast::dispose(at);
		return Error::DeBadCondition;
	}

	ast::dispose(system.point);
	system.point = at;

	for (unsigned k = 0; k < SIZE; k++) {
		const ast *given = nullptr;

		if (system.vector != nullptr && function.compare(*system.vector) && is_matrix(value, 1))
			given = value.childAt(k)->firstChild();
		else if (system.unknowns[k] != nullptr && function.compare(*system.unknowns[k]))
			given = &value;

		if (given == nullptr)
			continue;

		if (system.values[k] != nullptr)
			return Error::DeBadCondition;

		system.values[k] = basic(given->copy());
	}

	return Error::Success;
}

/*Replaces the independent variable with T, or a symbol not in the items if T is an unknown, when it is an unknown*/
static void choose_variable(DiffEqSystem &system, ast *const *items, unsigned count) {
	const ast *names[SIZE + 1] = {system.unknowns[0], system.unknowns[1], system.vector};
	bool unknown = false, taken = false;
	ast *t = ast::make(Sym::T);

	for (const ast *name : names) {
		unknown |= name != nullptr && name->compare(*system.variable);
		taken |= name != nullptr && name->compare(*t);
	}

	if (!unknown) {
		ast::dispose(t);
		return;
	}

	if (taken) {
		ast *scope = ast::make(Op::Add);
		for (unsigned i = 0; i < count; i++)
			scope->appendChild(items[i]->copy());
		t->replace(ast::make(fresh_symbol(*scope)));
		ast::dispose(scope);
	}

	ast::dispose(system.variable);
	system.variable = t;
}

/*Reads two linear first order equations as x' = Ax + f, solving B x' + C x + r = 0 for x'*/
static Error load_equations(DiffEqSystem &system, ast *const *items, unsigned count) {
	unsigned found = 0;

	if (count < SIZE)
		return Error::DeSystem;

	for (unsigned i = 0; i < SIZE; i++) {
		if (!primed_symbols(*items[i], system.unknowns, &found))
			return Error::DeSystem;
	}

	if (found != SIZE)
		return Error::DeSystem;

	choose_variable(system, items, count);

	ast *slots[SIZE], *derivatives[SIZE][SIZE] = {}, *coefficients[SIZE][SIZE] = {}, *rest[SIZE] = {};
	ast *scope = ast::make(Op::Add, items[0]->copy(), items[1]->copy());
	scope->appendChild(system.variable->copy());

	for (unsigned k = 0; k < SIZE; k++) {
		slots[k] = ast::make(fresh_symbol(*scope));
		scope->appendChild(slots[k]->copy());
	}

	Error err = Error::Success;

	for (unsigned i = 0; i < SIZE && err == Error::Success; i++) {
		const ast &equation = *items[i];
		canonical_SetFunction(system.unknowns[i]->symbol());
		record_equation(equation);
		canonical_SetFunction(Sym::Invalid);

		ast *f = equation.isOp(Op::Equals)
					 ? difference(equation.firstChild()->copy(), equation.firstChild()->next()->copy())
					 : equation.copy();

		work::pause();
		for (unsigned k = 0; k < SIZE; k++) {
			ast *prime = DiffEq::derivative(*system.unknowns[k], 1);
			substitute(*f, *prime, *slots[k]);
			ast::dispose(prime);
		}
		simplify(*f, Simp::Basic);
		work::resume();

		if (has_op(*f, Op::Prime))
			err = Error::DeSystem;

		for (unsigned k = 0; k < SIZE && err == Error::Success; k++) {
			derivatives[i][k] = coefficient(*f, *slots[k]);
			coefficients[i][k] = coefficient(*f, *system.unknowns[k]);
		}

		rest[i] = f;
		ast *zero = integer(0);

		work::pause();
		for (unsigned k = 0; k < SIZE; k++) {
			substitute(*rest[i], *slots[k], *zero);
			substitute(*rest[i], *system.unknowns[k], *zero);
		}
		simplify(*rest[i], Simp::Basic);
		work::resume();

		ast::dispose(zero);
	}

	for (unsigned i = 0; i < SIZE && err == Error::Success; i++) {
		for (unsigned j = 0; j < SIZE && err == Error::Success; j++) {
			for (unsigned k = 0; k < SIZE; k++) {
				if (involves(*derivatives[i][j], *slots[k]) || involves(*coefficients[i][j], *slots[k]) ||
					involves(*derivatives[i][j], *system.unknowns[k]) ||
					involves(*coefficients[i][j], *system.unknowns[k]) ||
					involves(*derivatives[i][j], *system.variable) || involves(*coefficients[i][j], *system.variable))
					err = Error::DeSystem;
			}
		}
	}

	ast *det = err == Error::Success ? basic(determinant(derivatives)) : nullptr;
	if (det != nullptr && is_zero(*det))
		err = Error::DeSystem;

	for (unsigned i = 0; i < SIZE && err == Error::Success; i++) {
		for (unsigned j = 0; j < SIZE; j++) {
			ast *sum = ast::make(Op::Add);
			for (unsigned k = 0; k < SIZE; k++)
				sum->appendChild(ast::make(Op::Mult, adjugate_entry(derivatives, i, k), coefficients[k][j]->copy()));
			system.matrix[i][j] = basic(negate(ast::make(Op::Div, sum, det->copy())));
		}

		ast *sum = ast::make(Op::Add);
		for (unsigned k = 0; k < SIZE; k++)
			sum->appendChild(ast::make(Op::Mult, adjugate_entry(derivatives, i, k), rest[k]->copy()));
		system.forcing[i] = basic(negate(ast::make(Op::Div, sum, det->copy())));
	}

	for (unsigned i = 0; i < SIZE; i++) {
		ast::dispose(slots[i]);
		ast::dispose(rest[i]);
	}
	dispose_square(derivatives);
	dispose_square(coefficients);
	ast::dispose(det);
	ast::dispose(scope);

	for (unsigned i = SIZE; i < count && err == Error::Success; i++)
		err = load_condition(system, *items[i]);

	return err;
}

/*Reads x' = Ax + f written with matrices, as a sum of terms that are each a square matrix times the vector or a column vector, times scalars*/
static Error load_matrix_form(DiffEqSystem &system, ast *const *items, unsigned count) {
	const ast &equation = *items[0];

	if (!equation.isOp(Op::Equals) || !equation.firstChild()->isOp(Op::Prime))
		return Error::DeSystem;

	const ast &vector = *equation.firstChild()->firstChild();
	if (!vector.isSymbol())
		return Error::DeSystem;

	system.vector = vector.copy();
	choose_variable(system, items, count);
	record_equation(equation);

	ast *sums[SIZE][SIZE + 1];
	for (unsigned i = 0; i < SIZE; i++) {
		for (unsigned j = 0; j <= SIZE; j++)
			sums[i][j] = ast::make(Op::Add, integer(0));
	}

	const ast &right = *equation.firstChild()->next();
	const unsigned terms = right.isOp(Op::Add) ? right.childCount() : 1;
	Error err = Error::Success;

	for (unsigned t = 0; t < terms && err == Error::Success; t++) {
		const ast &term = right.isOp(Op::Add) ? *right.childAt(t) : right;
		const unsigned factors = term.isOp(Op::Mult) ? term.childCount() : 1;
		const ast *matrix = nullptr;
		ast *scalar = ast::make(Op::Mult, integer(1));
		bool times_vector = false;

		for (unsigned f = 0; f < factors && err == Error::Success; f++) {
			const ast &factor = term.isOp(Op::Mult) ? *term.childAt(f) : term;

			if (factor.isOp(Op::Matrix) && matrix == nullptr)
				matrix = &factor;
			else if (factor.compare(vector) && !times_vector)
				times_vector = true;
			else if (involves(factor, vector) || has_op(factor, Op::Matrix))
				err = Error::DeSystem;
			else
				scalar->appendChild(factor.copy());
		}

		if (matrix == nullptr || !is_matrix(*matrix, times_vector ? SIZE : 1) ||
			(times_vector && involves(*scalar, *system.variable)))
			err = Error::DeSystem;

		for (unsigned i = 0; i < SIZE && err == Error::Success; i++) {
			const ast &row = *matrix->childAt(i);
			for (unsigned j = 0; j < (times_vector ? SIZE : 1); j++) {
				sums[i][times_vector ? j : SIZE]->appendChild(
					ast::make(Op::Mult, scalar->copy(), row.childAt(j)->copy())
				);
			}
		}

		ast::dispose(scalar);
	}

	for (unsigned i = 0; i < SIZE; i++) {
		for (unsigned j = 0; j < SIZE; j++)
			system.matrix[i][j] = basic(sums[i][j]);
		system.forcing[i] = basic(sums[i][SIZE]);
	}

	for (unsigned i = 0; i < SIZE && err == Error::Success; i++) {
		for (unsigned j = 0; j < SIZE; j++) {
			if (involves(*system.matrix[i][j], *system.variable) || involves(*system.matrix[i][j], vector))
				err = Error::DeSystem;
		}
	}

	for (unsigned i = 1; i < count && err == Error::Success; i++)
		err = load_condition(system, *items[i]);

	return err;
}

Error DiffEqSystem::load(ast *const *items, unsigned count, const ast &t) {
	clear();
	variable = t.copy();

	if (count == 0)
		return Error::DeSystem;

	Error err =
		has_op(*items[0], Op::Matrix) ? load_matrix_form(*this, items, count) : load_equations(*this, items, count);

	if (err == Error::Success && point != nullptr && (values[0] == nullptr || values[1] == nullptr))
		err = Error::DeBadCondition;

	return err;
}

/*Records x' = Ax + f with matrices, unless the system was entered that way*/
static void record_matrix_form(const DiffEqSystem &system) {
	if (system.vector != nullptr)
		return;

	ast *right = ast::make(Op::Mult, square(system.matrix), unknown_vector(system));
	if (!homogeneous(system))
		right = ast::make(Op::Add, right, column_of(system.forcing));

	ast *left = ast::make(Op::Prime, unknown_vector(system));
	work::step(work::Step::Type::Equation, "Matrix form", left, right);

	ast::dispose(left);
	ast::dispose(right);
}

/*Fills shifted with A - value I*/
static void shift(const DiffEqSystem &system, const ast &value, ast *shifted[][SIZE]) {
	for (unsigned i = 0; i < SIZE; i++) {
		for (unsigned j = 0; j < SIZE; j++) {
			shifted[i][j] = system.matrix[i][j]->copy();
			if (i == j)
				shifted[i][j] = difference(shifted[i][j], value.copy());
			basic(shifted[i][j]);
		}
	}
}

static mp_small greatest_divisor(mp_small a, mp_small b) {
	a = a < 0 ? -a : a;
	b = b < 0 ? -b : b;

	while (b != 0) {
		const mp_small rest = a % b;
		a = b;
		b = rest;
	}

	return a;
}

/*Scales parts by the same number so that they are integers without a common factor and the first nonzero one is positive, when they are all small rational numbers*/
static void normalize(ast **parts, unsigned count) {
	mp_small numerators = 0, denominators = 1, sign = 0;

	for (unsigned i = 0; i < count; i++) {
		Num *value = rational_value(*parts[i]);
		mp_small numerator, denominator;
		const bool small = value != nullptr && mp_int_to_int(MP_NUMER_P(value), &numerator) == MP_OK &&
						   mp_int_to_int(MP_DENOM_P(value), &denominator) == MP_OK;
		Num::dispose(value);

		if (!small)
			return;

		if (sign == 0 && numerator != 0)
			sign = numerator < 0 ? -1 : 1;

		numerators = greatest_divisor(numerators, numerator);
		denominators = denominators / greatest_divisor(denominators, denominator) * denominator;
	}

	if (numerators == 0)
		return;

	for (unsigned i = 0; i < count; i++)
		parts[i] = basic(ast::make(Op::Mult, ast::make(Num::from(sign * denominators, numerators)), parts[i]));
}

/*Records the eigenvalue and the equation (A - λI)K = 0, and sets row to the coefficients of its first nonzero row. Returns false if A - λI is zero.*/
static bool eigen_equation(
	const DiffEqSystem &system,
	const ast &eigenvalue,
	const char *text,
	Sym name,
	const ast *right,
	ast **row,
	unsigned *which
) {
	ast *shifted[SIZE][SIZE];
	shift(system, eigenvalue, shifted);

	unsigned i = 0;
	while (i < SIZE && is_zero(*shifted[i][0]) && is_zero(*shifted[i][1]))
		i++;

	if (i < SIZE) {
		ast *unknown = column(subscripted(name, integer(1)), subscripted(name, integer(2)));
		ast *zero = column(integer(0), integer(0));
		ast *left = ast::make(Op::Mult, square(shifted), unknown->copy());
		work::step(work::Step::Type::Equation, text, left, right != nullptr ? right : zero);
		ast::dispose(left);

		left = tidy(
			ast::make(
				Op::Add,
				ast::make(Op::Mult, shifted[i][0]->copy(), unknown->childAt(0)->firstChild()->copy()),
				ast::make(Op::Mult, shifted[i][1]->copy(), unknown->childAt(1)->firstChild()->copy())
			)
		);
		ast *value = right != nullptr ? right->childAt(i)->firstChild()->copy() : integer(0);
		work::step(work::Step::Type::Equation, nullptr, left, value);

		ast::dispose(left);
		ast::dispose(value);
		ast::dispose(zero);
		ast::dispose(unknown);

		row[0] = shifted[i][0]->copy();
		row[1] = shifted[i][1]->copy();
		*which = i;
	}

	dispose_square(shifted);

	return i < SIZE;
}

/*Records the eigenvalue as λ with the index*/
static void record_eigenvalue(const ast &eigenvalue, unsigned index) {
	ast *name = subscripted(Sym::Lambda, integer(index));
	work::step(work::Step::Type::Equation, "Eigenvector", name, &eigenvalue);
	ast::dispose(name);
}

/*Finds the eigenvector of a real eigenvalue, recording the work. Returns false if every vector is one.*/
static bool real_eigenvector(const DiffEqSystem &system, const ast &eigenvalue, unsigned index, ast **vector) {
	ast *row[SIZE];
	unsigned which;

	if (!eigen_equation(system, eigenvalue, nullptr, Sym::K, nullptr, row, &which))
		return false;

	vector[0] = row[1];
	vector[1] = basic(negate(row[0]));
	normalize(vector, SIZE);

	ast *name = subscripted(Sym::K, integer(index)), *value = column_of(vector);
	work::step(work::Step::Type::Equation, nullptr, name, value);
	ast::dispose(name);
	ast::dispose(value);

	return true;
}

/*Finds P with (A - λI)P = K, recording the work*/
static void generalized_eigenvector(
	const DiffEqSystem &system,
	const ast &eigenvalue,
	ast *const *vector,
	ast **generalized
) {
	ast *row[SIZE], *right = column_of(vector);
	unsigned which;

	eigen_equation(system, eigenvalue, "Generalized eigenvector", Sym::P, right, row, &which);

	const unsigned free = is_zero(*row[0]) ? 0 : 1;
	generalized[free] = integer(0);
	generalized[1 - free] = basic(ast::make(Op::Div, vector[which]->copy(), row[1 - free]->copy()));

	ast *name = ast::make(Sym::P), *value = column_of(generalized);
	work::step(work::Step::Type::Equation, nullptr, name, value);
	ast::dispose(name);
	ast::dispose(value);

	ast::dispose(row[0]);
	ast::dispose(row[1]);
	ast::dispose(right);
}

/*Finds the eigenvalues of A and a fundamental solution for each, recording the work, and fills forms with them written with constant vectors*/
static Error eigen_solutions(DiffEqSystem &system, Root *roots, unsigned *count, ast **forms) {
	Num *entries[SIZE][SIZE] = {}, *p[SIZE + 1] = {};
	bool rational = true;

	for (unsigned i = 0; i < SIZE; i++) {
		for (unsigned j = 0; j < SIZE; j++) {
			entries[i][j] = rational_value(*system.matrix[i][j]);
			rational &= entries[i][j] != nullptr;
		}
	}

	if (rational) {
		p[0] = Num::from(0);
		*p[0] = *entries[0][0] * *entries[1][1] - *entries[0][1] * *entries[1][0];
		p[1] = Num::from(0);
		*p[1] = -(*entries[0][0] + *entries[1][1]);
		p[2] = Num::from(1);
	}

	for (unsigned i = 0; i < SIZE; i++) {
		for (unsigned j = 0; j < SIZE; j++)
			Num::dispose(entries[i][j]);
	}

	if (!rational)
		return Error::DeUnsolved;

	system.method = "Eigenvalues";

	ast *lambda = ast::make(Sym::Lambda), *shifted[SIZE][SIZE];
	shift(system, *lambda, shifted);
	for (unsigned i = 0; i < SIZE; i++)
		tidy(shifted[i][i]);

	ast *left = ast::make(Op::Abs, square(shifted)), *zero = integer(0);
	work::step(work::Step::Type::Equation, "Characteristic equation", left, zero);
	ast::dispose(left);
	ast::dispose(zero);
	dispose_square(shifted);

	Error err = polynomial_roots(p, SIZE, *lambda, nullptr, roots, count);

	for (unsigned k = 0; k <= SIZE; k++)
		Num::dispose(p[k]);
	ast::dispose(lambda);

	if (err != Error::Success)
		return err;

	const ast &t = *system.variable;
	unsigned made = 0;

	for (unsigned r = 0; r < *count; r++) {
		const Root &root = roots[r];

		if (root.im != nullptr) {
			ast *eigenvalue =
				tidy(ast::make(Op::Add, root.re->copy(), ast::make(Op::Mult, root.im->copy(), ast::make(Sym::Imag))));
			record_eigenvalue(*eigenvalue, made + 1);

			ast *row[SIZE];
			unsigned which;
			eigen_equation(system, *eigenvalue, nullptr, Sym::K, nullptr, row, &which);
			ast::dispose(row[0]);
			ast::dispose(row[1]);

			/*Parts of K = (b, λ - a) as real and imaginary parts*/
			ast *parts[2 * SIZE] = {
				system.matrix[0][1]->copy(),
				basic(difference(root.re->copy(), system.matrix[0][0]->copy())),
				integer(0),
				root.im->copy()
			};
			normalize(parts, 2 * SIZE);

			ast *vector[SIZE];
			for (unsigned k = 0; k < SIZE; k++) {
				vector[k] = tidy(
					ast::make(
						Op::Add, parts[k]->copy(), ast::make(Op::Mult, parts[SIZE + k]->copy(), ast::make(Sym::Imag))
					)
				);
			}

			ast *name = subscripted(Sym::K, integer(made + 1)), *value = column_of(vector);
			work::step(work::Step::Type::Equation, nullptr, name, value);
			ast::dispose(name);
			ast::dispose(value);

			ast *real = column_of(parts), *imaginary = column_of(parts + SIZE);
			name = subscripted(Sym::B, integer(1));
			work::step(work::Step::Type::Equation, nullptr, name, real);
			ast::dispose(name);
			name = subscripted(Sym::B, integer(2));
			work::step(work::Step::Type::Equation, nullptr, name, imaginary);
			ast::dispose(name);

			ast *frequency = tidy(ast::make(Op::Mult, root.im->copy(), t.copy()));
			ast *cosine = ast::make(Op::Cos, frequency->copy()), *sine = ast::make(Op::Sin, frequency);

			forms[made++] = product(
				ast::make(
					Op::Add,
					ast::make(Op::Mult, real->copy(), cosine->copy()),
					product(integer(-1), ast::make(Op::Mult, imaginary->copy(), sine->copy()))
				),
				growth(*root.re, t)
			);
			forms[made++] = product(
				ast::make(Op::Add, ast::make(Op::Mult, imaginary, cosine), ast::make(Op::Mult, real, sine)),
				growth(*root.re, t)
			);

			for (unsigned k = 0; k < SIZE; k++) {
				ast::dispose(vector[k]);
				ast::dispose(parts[k]);
				ast::dispose(parts[SIZE + k]);
			}
			ast::dispose(eigenvalue);
			continue;
		}

		record_eigenvalue(*root.re, made + 1);

		ast *vector[SIZE];
		if (!real_eigenvector(system, *root.re, made + 1, vector)) {
			for (unsigned k = 0; k < SIZE; k++) {
				ast *standard = column(integer(k == 0 ? 1 : 0), integer(k == 0 ? 0 : 1));
				ast *name = subscripted(Sym::K, integer(k + 1));
				work::step(work::Step::Type::Equation, nullptr, name, standard);
				ast::dispose(name);
				forms[made++] = product(standard, growth(*root.re, t));
			}
			continue;
		}

		forms[made++] = product(column_of(vector), growth(*root.re, t));

		if (root.multiplicity == SIZE) {
			ast *generalized[SIZE];
			generalized_eigenvector(system, *root.re, vector, generalized);

			forms[made++] = ast::make(
				Op::Add,
				product(column_of(vector), product(t.copy(), growth(*root.re, t))),
				product(column_of(generalized), growth(*root.re, t))
			);

			for (unsigned k = 0; k < SIZE; k++)
				ast::dispose(generalized[k]);
		}

		for (unsigned k = 0; k < SIZE; k++)
			ast::dispose(vector[k]);
	}

	return made == SIZE ? Error::Success : Error::DeRoots;
}

/*Finds e^(At) as the inverse Laplace transform of (sI - A)^-1, recording the work, and fills forms with its columns and inverse with e^(-At)*/
static Error exponential_solutions(
	DiffEqSystem &system,
	DiffEq &context,
	const ast &taken,
	ast **forms,
	ast *inverse[][SIZE]
) {
	system.method = "Matrix exponential";

	const ast &t = *system.variable;
	ast *s = pick_symbol(&context, Sym::S, taken);
	ast *shifted[SIZE][SIZE], *resolvent[SIZE][SIZE] = {}, *exponential[SIZE][SIZE] = {};

	for (unsigned i = 0; i < SIZE; i++) {
		for (unsigned j = 0; j < SIZE; j++) {
			shifted[i][j] = negate(system.matrix[i][j]->copy());
			if (i == j)
				shifted[i][j] = ast::make(Op::Add, s->copy(), shifted[i][j]);
			tidy(shifted[i][j]);
		}
	}

	ast *unit[SIZE][SIZE] = {{integer(1), integer(0)}, {integer(0), integer(1)}};
	ast *left = difference(ast::make(Op::Mult, s->copy(), square(unit)), square(system.matrix));
	ast *right = square(shifted);
	work::step(work::Step::Type::Equation, "Matrix exponential", left, right);
	ast::dispose(left);
	dispose_square(unit);

	ast *det = determinant(shifted);
	work::pause();
	simplify(*det, Simp::Basic);
	expand(*det, Expand::All);
	simplify(*det, Simp::Basic);
	work::resume();

	left = ast::make(Op::Abs, right->copy());
	work::step(work::Step::Type::Equation, nullptr, left, det);
	ast::dispose(left);

	for (unsigned i = 0; i < SIZE; i++) {
		for (unsigned j = 0; j < SIZE; j++) {
			ast *adjugate = basic(adjugate_entry(shifted, i, j));
			resolvent[i][j] = adjugate->isInt(0) ? integer(0) : quotient(*adjugate, *det);
			ast::dispose(adjugate);
		}
	}

	left = ast::make(Op::Pow, right, integer(-1));
	right = square(resolvent);
	work::step(work::Step::Type::Equation, nullptr, left, right);
	ast::dispose(left);
	ast::dispose(right);

	Error err = Error::Success;

	for (unsigned i = 0; i < SIZE && err == Error::Success; i++) {
		for (unsigned j = 0; j < SIZE && err == Error::Success; j++) {
			if (resolvent[i][j]->isInt(0)) {
				exponential[i][j] = integer(0);
				continue;
			}

			ast *transform = ast::make(Op::LaplaceInverse, resolvent[i][j]->copy());
			work::step(work::Step::Type::State, nullptr, nullptr, transform);
			ast::dispose(transform);

			exponential[i][j] = laplace_inverse(*resolvent[i][j], *s, t);
			if (exponential[i][j] == nullptr)
				err = Error::DeUnsolved;
			else
				compact(exponential[i][j]);
		}
	}

	if (err == Error::Success) {
		left = ast::make(Op::Pow, ast::make(Sym::Euler), ast::make(Op::Mult, ast::make(Sym::A), t.copy()));
		right = square(exponential);
		work::step(work::Step::Type::Equation, "Matrix exponential", left, right);
		ast::dispose(left);
		ast::dispose(right);

		ast *negative = negate(t.copy());

		for (unsigned i = 0; i < SIZE; i++) {
			forms[i] = column(exponential[0][i]->copy(), exponential[1][i]->copy());

			for (unsigned j = 0; j < SIZE; j++) {
				inverse[i][j] = exponential[i][j]->copy();
				work::pause();
				substitute(*inverse[i][j], t, *negative);
				work::resume();
				compact(inverse[i][j]);
			}
		}

		ast::dispose(negative);
	}

	dispose_square(shifted);
	dispose_square(resolvent);
	dispose_square(exponential);
	ast::dispose(det);
	ast::dispose(s);

	return err;
}

/*Finds a particular solution as Φ ∫Φ^-1 f dt from the fundamental solutions, using inverse for Φ^-1 unless it is nullptr, recording the work*/
static Error variation_of_parameters(
	DiffEqSystem &system,
	DiffEq &context,
	ast *const fundamental[][SIZE],
	ast *const given[][SIZE],
	ast **form,
	ast **particular
) {
	const ast &t = *system.variable;
	ast *phi[SIZE][SIZE], *inverse[SIZE][SIZE], *rates[SIZE], *parameters[SIZE];

	if (given == nullptr)
		system.method = "Variation of parameters";
	work::text("Variation of parameters");

	for (unsigned i = 0; i < SIZE; i++) {
		for (unsigned j = 0; j < SIZE; j++)
			phi[i][j] = fundamental[j][i]->copy();
	}

	ast *name = ast::make(Sym::Phi), *right = square(phi);
	work::step(work::Step::Type::Equation, "Fundamental matrix", name, right);
	ast::dispose(right);

	if (given != nullptr) {
		for (unsigned i = 0; i < SIZE; i++) {
			for (unsigned j = 0; j < SIZE; j++)
				inverse[i][j] = given[i][j]->copy();
		}
	} else {
		ast *det = settle(determinant(phi));
		for (unsigned i = 0; i < SIZE; i++) {
			for (unsigned j = 0; j < SIZE; j++)
				inverse[i][j] = compact(ast::make(Op::Div, adjugate_entry(phi, i, j), det->copy()));
		}
		ast::dispose(det);
	}

	ast *named_inverse = ast::make(Op::Pow, name->copy(), integer(-1));
	right = square(inverse);
	work::step(work::Step::Type::Equation, nullptr, named_inverse, right);
	ast::dispose(right);

	for (unsigned i = 0; i < SIZE; i++) {
		ast *sum = ast::make(Op::Add);
		for (unsigned k = 0; k < SIZE; k++)
			sum->appendChild(ast::make(Op::Mult, inverse[i][k]->copy(), system.forcing[k]->copy()));
		rates[i] = compact(sum);
	}

	ast *integrand = ast::make(Op::Mult, named_inverse->copy(), column_of(system.forcing));
	right = column_of(rates);
	work::step(work::Step::Type::Equation, nullptr, integrand, right);
	ast::dispose(right);

	fresh_Reserve(context.equation);
	for (unsigned i = 0; i < SIZE; i++) {
		parameters[i] = ast::make(Op::Integral, rates[i]->copy(), t.copy());
		eval_integrals(*parameters[i]);
		basic(parameters[i]);
	}
	fresh_Reserve(nullptr);

	Error err = Error::Success;
	if (contains_integral(*parameters[0]) || contains_integral(*parameters[1]))
		err = Error::DeIntegral;

	if (err == Error::Success) {
		ast *left = ast::make(Op::Integral, integrand->copy(), t.copy());
		right = column_of(parameters);
		work::step(work::Step::Type::Equation, nullptr, left, right);
		ast::dispose(left);
		ast::dispose(right);

		for (unsigned i = 0; i < SIZE; i++) {
			ast *sum = ast::make(Op::Add);
			for (unsigned k = 0; k < SIZE; k++)
				sum->appendChild(distribute(*phi[i][k], *parameters[k]));
			particular[i] = settle(sum);
		}

		*form = column_of(particular);
		left = subscripted(system.vector != nullptr ? system.vector->symbol() : Sym::X, ast::make(Sym::P));
		right = ast::make(Op::Equals, ast::make(Op::Mult, name->copy(), column_of(parameters)), (*form)->copy());
		work::step(work::Step::Type::Equation, "Particular solution", left, right);
		ast::dispose(left);
		ast::dispose(right);
	}

	for (unsigned i = 0; i < SIZE; i++) {
		ast::dispose(rates[i]);
		ast::dispose(parameters[i]);
	}
	dispose_square(phi);
	dispose_square(inverse);
	ast::dispose(name);
	ast::dispose(named_inverse);
	ast::dispose(integrand);

	return err;
}

/*Finds a particular solution with a trial vector for each function in f, recording the work*/
static Error undetermined_coefficients(
	DiffEqSystem &system,
	DiffEq &context,
	const Root *roots,
	unsigned count,
	const ast &exclude,
	ast **form,
	ast **particular
) {
	constexpr unsigned max_functions = DiffEq::max_order / SIZE;
	ast *rates[max_functions], *frequencies[max_functions], *functions[max_functions], *unknowns[DiffEq::max_order],
		*values[DiffEq::max_order];
	unsigned degrees[max_functions], groups = 0, size = 0;
	Error err = Error::Success;

	for (unsigned k = 0; k < SIZE && err == Error::Success; k++) {
		ast *g = system.forcing[k]->copy();

		work::pause();
		expand(*g, Expand::All);
		simplify(*g, Simp::Basic);
		work::resume();

		for (unsigned i = 0; i < (g->isOp(Op::Add) ? g->childCount() : 1) && err == Error::Success; i++) {
			const ast *term = g->isOp(Op::Add) ? g->childAt(i) : g;
			unsigned degree;
			ast *rate, *frequency;

			if (term->isInt(0))
				continue;

			if (!forcing_form(&context, term, &degree, &rate, &frequency)) {
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
			} else if (groups < max_functions) {
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
	}

	for (unsigned i = 0; i < groups && err == Error::Success; i++) {
		const unsigned degree = degrees[i] + root_multiplicity(roots, count, *rates[i], *frequencies[i]);
		const unsigned oscillations = frequencies[i]->isInt(0) ? 1 : 2;

		for (unsigned j = degree + 1; j-- > 0 && err == Error::Success;) {
			for (unsigned o = 0; o < oscillations; o++) {
				if (size == max_functions) {
					err = Error::DeUnsolved;
					break;
				}

				ast *f =
					oscillations == 1 ? nullptr : oscillation(&context, o == 0 ? Op::Cos : Op::Sin, *frequencies[i]);
				functions[size++] = basis_function(&context, j, *rates[i], f);
			}
		}
	}

	for (unsigned i = 0; i < groups; i++) {
		ast::dispose(rates[i]);
		ast::dispose(frequencies[i]);
	}

	if (err == Error::Success && !choose_constants(&context, &exclude, unknowns, SIZE * size))
		err = Error::DeUnsolved;

	if (err != Error::Success) {
		for (unsigned i = 0; i < size; i++)
			ast::dispose(functions[i]);
		return err;
	}

	system.method = "Undetermined coefficients";
	work::text("Undetermined coefficients");

	ast *trial = ast::make(Op::Add), *entries[SIZE], *derivatives[SIZE], *residuals[SIZE];

	for (unsigned k = 0; k < SIZE; k++)
		entries[k] = ast::make(Op::Add);

	for (unsigned i = 0; i < size; i++) {
		trial->appendChild(product(column_of(unknowns + SIZE * i), functions[i]->copy()));
		for (unsigned k = 0; k < SIZE; k++)
			entries[k]->appendChild(ast::make(Op::Mult, unknowns[SIZE * i + k]->copy(), functions[i]->copy()));
	}

	if (trial->childCount() == 1)
		trial->replace(trial->removeChildAt(0));

	ast *name = subscripted(system.vector != nullptr ? system.vector->symbol() : Sym::X, ast::make(Sym::P));
	work::step(work::Step::Type::Equation, "Trial solution", name, trial);

	for (unsigned k = 0; k < SIZE; k++) {
		tidy(entries[k]);
		derivatives[k] = entries[k]->copy();

		work::pause();
		derivative(*derivatives[k], *system.variable, *system.variable);
		simplify(*derivatives[k], Simp::Basic);
		expand_if_smaller(*derivatives[k]);
		work::resume();
	}

	ast *left = column_of(derivatives);
	ast *right =
		ast::make(Op::Add, ast::make(Op::Mult, square(system.matrix), column_of(entries)), column_of(system.forcing));
	work::step(work::Step::Type::Equation, "Substitute", left, right);
	ast::dispose(left);
	ast::dispose(right);

	for (unsigned k = 0; k < SIZE; k++) {
		residuals[k] = difference(derivatives[k]->copy(), system.forcing[k]->copy());
		for (unsigned j = 0; j < SIZE; j++)
			residuals[k] =
				difference(residuals[k], ast::make(Op::Mult, system.matrix[k][j]->copy(), entries[j]->copy()));

		work::pause();
		simplify(*residuals[k], Simp::Basic);
		expand(*residuals[k], Expand::All);
		simplify(*residuals[k], Simp::Basic);
		work::resume();
	}

	err = solve_coefficients(&context, residuals, SIZE, unknowns, SIZE * size, values);

	if (err == Error::Success) {
		for (unsigned k = 0; k < SIZE; k++) {
			particular[k] = entries[k]->copy();

			work::pause();
			for (unsigned j = 0; j < SIZE * size; j++)
				substitute(*particular[k], *unknowns[j], *values[j]);
			work::resume();

			compact(particular[k]);
		}

		for (unsigned j = 0; j < SIZE * size; j++)
			ast::dispose(values[j]);

		*form = column_of(particular);
		work::step(work::Step::Type::Equation, "Particular solution", name, *form);
	}

	for (unsigned k = 0; k < SIZE; k++) {
		ast::dispose(entries[k]);
		ast::dispose(derivatives[k]);
		ast::dispose(residuals[k]);
	}
	for (unsigned i = 0; i < size; i++)
		ast::dispose(functions[i]);
	for (unsigned j = 0; j < SIZE * size; j++)
		ast::dispose(unknowns[j]);
	ast::dispose(trial);
	ast::dispose(name);

	return err;
}

/*Returns the sum of each nonzero coefficient times its form, plus particular unless it is nullptr*/
static ast *combine(ast *const *coefficients, ast *const *forms, const ast *particular) {
	ast *sum = ast::make(Op::Add);

	for (unsigned i = 0; i < SIZE; i++) {
		if (!coefficients[i]->isInt(0))
			sum->appendChild(product(coefficients[i]->copy(), forms[i]->copy()));
	}

	if (particular != nullptr)
		sum->appendChild(particular->copy());

	if (sum->childCount() == 0) {
		ast::dispose(sum);
		return column(integer(0), integer(0));
	}

	if (sum->childCount() == 1)
		sum->replace(sum->removeChildAt(0));

	return sum;
}

/*Fills entries with the components of the combination of the fundamental solutions with the coefficients plus particular*/
static void combine_entries(
	ast *const *coefficients,
	ast *const fundamental[][SIZE],
	ast *const *particular,
	ast **entries
) {
	for (unsigned k = 0; k < SIZE; k++) {
		ast *sum = ast::make(Op::Add);
		for (unsigned i = 0; i < SIZE; i++)
			sum->appendChild(ast::make(Op::Mult, coefficients[i]->copy(), fundamental[i][k]->copy()));
		if (particular[k] != nullptr)
			sum->appendChild(particular[k]->copy());
		entries[k] = compact(sum);
	}
}

/*Finds the constants from the initial conditions, recording the work, and replaces them with their values*/
static Error apply_conditions(const DiffEqSystem &system, ast **constants, ast *const *entries) {
	ast *matrix[SIZE][DiffEq::max_order + 1], *at[SIZE];
	unsigned pivots[SIZE];
	ast *zero = integer(0);

	for (unsigned k = 0; k < SIZE; k++) {
		at[k] = entries[k]->copy();
		work::pause();
		substitute(*at[k], *system.variable, *system.point);
		work::resume();
		reduced(at[k]);
	}

	ast *left = ast::make(Op::At, unknown_vector(system), system.point->copy());
	ast *right = ast::make(Op::Equals, column_of(at), column_of(system.values));
	work::step(work::Step::Type::Equation, "Initial conditions", left, right);
	ast::dispose(left);
	ast::dispose(right);

	for (unsigned k = 0; k < SIZE; k++) {
		for (unsigned j = 0; j < SIZE; j++) {
			matrix[k][j] = at[k]->copy();
			work::pause();
			derivative(*matrix[k][j], *constants[j], *constants[j]);
			work::resume();
			reduced(matrix[k][j]);
		}

		work::pause();
		for (unsigned j = 0; j < SIZE; j++)
			substitute(*at[k], *constants[j], *zero);
		work::resume();

		matrix[k][SIZE] = reduced(difference(system.values[k]->copy(), at[k]));
	}

	const unsigned rank = eliminate(matrix, SIZE, SIZE, pivots);
	Error err = rank == SIZE ? Error::Success : Error::DeNoSolution;

	for (unsigned i = 0; i < rank && err == Error::Success; i++) {
		ast *value = matrix[i][SIZE]->copy();
		single_fraction(*value);
		work::step(
			work::Step::Type::Equation, i == 0 ? "Solve for the constants" : nullptr, constants[pivots[i]], value
		);
		ast::dispose(constants[pivots[i]]);
		constants[pivots[i]] = value;
	}

	for (unsigned k = 0; k < SIZE; k++) {
		for (unsigned j = 0; j <= SIZE; j++)
			ast::dispose(matrix[k][j]);
	}
	ast::dispose(zero);

	return err;
}

/*Writes the solution as X = F in matrix form, or as a list of each unknown equal to its component, recording it*/
static ast *record_solution(const DiffEqSystem &system, const ast &form, ast *const *entries) {
	if (system.vector != nullptr) {
		work::step(work::Step::Type::Equation, "Solution", system.vector, &form);
		return ast::make(Op::Equals, system.vector->copy(), form.copy());
	}

	ast *list = ast::make(Op::List);

	for (unsigned k = 0; k < SIZE; k++) {
		work::step(work::Step::Type::Equation, k == 0 ? "Solution" : nullptr, system.unknowns[k], entries[k]);
		list->appendChild(ast::make(Op::Equals, system.unknowns[k]->copy(), entries[k]->copy()));
	}

	return list;
}

static Error solve_with_matrices(DiffEqSystem &system, DiffEq &context, ast **solution) {
	ast *forms[SIZE] = {}, *fundamental[SIZE][SIZE] = {}, *inverse[SIZE][SIZE] = {}, *constants[SIZE] = {};
	ast *particular_form = nullptr, *particular[SIZE] = {}, *entries[SIZE] = {};
	Root roots[SIZE];
	unsigned count = 0;
	const bool exponential = system.requested == Method::Laplace;

	if (exponential)
		context.reserved = ast::make(Sym::A);

	if (!choose_constants(&context, nullptr, constants, SIZE))
		return Error::DeUnsolved;

	ast *taken = ast::make(Op::Add, constants[0]->copy(), constants[1]->copy());

	record_matrix_form(system);

	Error err = exponential ? exponential_solutions(system, context, *taken, forms, inverse)
							: eigen_solutions(system, roots, &count, forms);

	for (unsigned i = 0; i < SIZE && err == Error::Success; i++) {
		for (unsigned k = 0; k < SIZE; k++)
			fundamental[i][k] = compact(entry_of(*forms[i], k));
	}

	if (err == Error::Success && !homogeneous(system)) {
		ast *name = subscripted(system.vector != nullptr ? system.vector->symbol() : Sym::X, ast::make(Sym::C));
		ast *complementary = combine(constants, forms, nullptr);
		work::step(work::Step::Type::Equation, "Complementary solution", name, complementary);
		ast::dispose(name);
		ast::dispose(complementary);

		if (exponential) {
			err = variation_of_parameters(system, context, fundamental, inverse, &particular_form, particular);
		} else {
			err = Error::DeUnsolved;
			if (system.requested != Method::VariationOfParameters)
				err = undetermined_coefficients(system, context, roots, count, *taken, &particular_form, particular);
			if (err == Error::DeUnsolved && system.requested != Method::UndeterminedCoefficients)
				err = variation_of_parameters(system, context, fundamental, nullptr, &particular_form, particular);
		}
	}

	if (err == Error::Success) {
		ast *form = combine(constants, forms, particular_form);
		ast *name = vector_name(system);

		if (exponential) {
			ast *exponential_form = ast::make(
				Op::Mult,
				ast::make(
					Op::Pow, ast::make(Sym::Euler), ast::make(Op::Mult, ast::make(Sym::A), system.variable->copy())
				),
				column_of(constants)
			);
			work::step(work::Step::Type::Equation, nullptr, name, exponential_form);
			ast::dispose(exponential_form);
		}

		if (system.point != nullptr) {
			work::step(work::Step::Type::Equation, "General solution", name, form);
			combine_entries(constants, fundamental, particular, entries);
			err = apply_conditions(system, constants, entries);
			ast::dispose(form);
			form = combine(constants, forms, particular_form);
			for (unsigned k = 0; k < SIZE; k++)
				ast::dispose(entries[k]);
		} else if (system.vector == nullptr) {
			work::step(work::Step::Type::Equation, "General solution", name, form);
		}

		if (err == Error::Success) {
			combine_entries(constants, fundamental, particular, entries);
			*solution = record_solution(system, *form, entries);
		}

		ast::dispose(form);
		ast::dispose(name);
	}

	if (!exponential)
		free_roots(roots, count);

	for (unsigned i = 0; i < SIZE; i++) {
		ast::dispose(forms[i]);
		ast::dispose(constants[i]);
		ast::dispose(particular[i]);
		ast::dispose(entries[i]);
	}
	dispose_square(fundamental);
	dispose_square(inverse);
	ast::dispose(particular_form);
	ast::dispose(taken);

	return err;
}

/*Returns D - a, or D when a is zero*/
static ast *operator_minus(const ast &a) {
	if (a.isInt(0))
		return ast::make(Sym::D);
	return tidy(difference(ast::make(Sym::D), a.copy()));
}

/*Solves the scalar equation for unknown with the conditions, recording the work, and sets value to its solution*/
static Error solve_scalar(
	const DiffEqSystem &system,
	ast *equation,
	const ast &unknown,
	ast *const *conditions,
	unsigned order,
	ast *reserved,
	ast **value
) {
	DiffEq de;
	Error err = de.load(*equation, *system.variable);
	ast::dispose(equation);

	for (unsigned k = 0; k < order && err == Error::Success && system.point != nullptr; k++) {
		DiffEq::Condition *c = &de.conditions[de.condition_count++];
		c->order = k;
		c->at = system.point->copy();
		c->value = conditions[k]->copy();
	}

	ast *solution = nullptr;

	if (err == Error::Success) {
		de.nested = true;
		de.reserved = reserved;
		reserved = nullptr;
		err = de.solve(&solution);
	}

	ast::dispose(reserved);

	if (err == Error::Success && !solution->firstChild()->compare(unknown))
		err = Error::DeImplicit;

	if (err == Error::Success)
		*value = solution->firstChild()->next()->copy();

	ast::dispose(solution);
	de.clear();

	return err;
}

static Error solve_by_elimination(DiffEqSystem &system, ast **solution) {
	if (system.vector != nullptr)
		return Error::DeElimination;

	system.method = "Elimination";

	ast *operators[SIZE];
	for (unsigned i = 0; i < SIZE; i++) {
		operators[i] = operator_minus(*system.matrix[i][i]);
		canonical_SetFunction(system.unknowns[i]->symbol());

		ast *left = ast::make(Op::Mult, operators[i]->copy(), system.unknowns[i]->copy());
		if (!system.matrix[i][1 - i]->isInt(0)) {
			left = ast::make(
				Op::Add,
				left,
				tidy(ast::make(Op::Mult, negate(system.matrix[i][1 - i]->copy()), system.unknowns[1 - i]->copy()))
			);
		}

		work::step(work::Step::Type::Equation, i == 0 ? "Operator form" : nullptr, left, system.forcing[i]);
		ast::dispose(left);
	}

	canonical_SetFunction(Sym::Invalid);

	const unsigned first = !is_zero(*system.matrix[0][1]) || is_zero(*system.matrix[1][0]) ? 0 : 1, second = 1 - first;
	const bool coupled = !is_zero(*system.matrix[first][second]);
	const ast &t = *system.variable;
	ast *solved[SIZE] = {};
	Error err = Error::Success;

	if (coupled) {
		const ast &a = *system.matrix[first][first], &b = *system.matrix[first][second],
				  &c = *system.matrix[second][first], &d = *system.matrix[second][second], &f = *system.forcing[first],
				  &g = *system.forcing[second];
		const ast &u = *system.unknowns[first], &v = *system.unknowns[second];

		canonical_SetFunction(u.symbol());
		ast *left = ast::make(
			Op::Mult,
			ast::make(
				Op::Add,
				ast::make(Op::Mult, operators[first]->copy(), operators[second]->copy()),
				tidy(negate(ast::make(Op::Mult, b.copy(), c.copy())))
			),
			u.copy()
		);
		ast *right = ast::make(Op::Add);
		if (!f.isInt(0))
			right->appendChild(ast::make(Op::Mult, operators[second]->copy(), f.copy()));
		if (!g.isInt(0))
			right->appendChild(tidy(ast::make(Op::Mult, b.copy(), g.copy())));
		if (right->childCount() == 0)
			right->appendChild(integer(0));
		work::step(work::Step::Type::Equation, "Elimination", left, right);
		ast::dispose(left);
		ast::dispose(right);

		ast *trace = basic(ast::make(Op::Add, a.copy(), d.copy()));
		ast *constant =
			basic(difference(ast::make(Op::Mult, a.copy(), d.copy()), ast::make(Op::Mult, b.copy(), c.copy())));

		ast *slope = f.copy();
		work::pause();
		derivative(*slope, t, t);
		work::resume();
		right = settle(
			ast::make(
				Op::Add,
				difference(slope, ast::make(Op::Mult, d.copy(), f.copy())),
				ast::make(Op::Mult, b.copy(), g.copy())
			)
		);

		ast *D = ast::make(Sym::D);
		left = ast::make(
			Op::Add, ast::make(Op::Pow, D->copy(), integer(2)), ast::make(Op::Mult, negate(trace->copy()), D->copy())
		);
		left->appendChild(constant->copy());
		left = ast::make(Op::Mult, tidy(left), u.copy());
		work::step(work::Step::Type::Equation, nullptr, left, right);
		ast::dispose(left);

		left = ast::make(
			Op::Add, DiffEq::derivative(u, 2), ast::make(Op::Mult, negate(trace->copy()), DiffEq::derivative(u, 1))
		);
		left->appendChild(ast::make(Op::Mult, constant->copy(), u.copy()));

		ast *conditions[SIZE] = {};
		if (system.point != nullptr) {
			conditions[0] = system.values[first]->copy();

			ast *at = f.copy();
			work::pause();
			substitute(*at, t, *system.point);
			work::resume();

			ast *expression = tidy(
				ast::make(
					Op::Add,
					ast::make(Op::Mult, a.copy(), system.values[first]->copy()),
					ast::make(Op::Mult, b.copy(), system.values[second]->copy())
				)
			);
			if (!is_zero(*at))
				expression = ast::make(Op::Add, expression, tidy(at->copy()));
			conditions[1] = reduced(expression->copy());

			ast *prime = ast::make(Op::At, DiffEq::derivative(u, 1), system.point->copy());
			ast *chain = ast::make(Op::Equals, expression, conditions[1]->copy());
			work::step(work::Step::Type::Equation, "Initial conditions", prime, chain);
			ast::dispose(prime);
			ast::dispose(chain);
			ast::dispose(at);
		}

		err = solve_scalar(system, ast::make(Op::Equals, tidy(left), right), u, conditions, SIZE, D, &solved[first]);

		ast::dispose(conditions[0]);
		ast::dispose(conditions[1]);
		ast::dispose(trace);
		ast::dispose(constant);

		if (err == Error::Success) {
			canonical_SetFunction(u.symbol());
			ast *expression =
				ast::make(Op::Add, DiffEq::derivative(u, 1), negate(ast::make(Op::Mult, a.copy(), u.copy())));
			expression->appendChild(negate(f.copy()));
			expression = tidy(ast::make(Op::Div, expression, b.copy()));
			work::step(
				work::Step::Type::Equation,
				first == 0 ? "From the first equation" : "From the second equation",
				&v,
				expression
			);
			ast::dispose(expression);

			slope = solved[first]->copy();
			work::pause();
			derivative(*slope, t, t);
			work::resume();

			expression = ast::make(Op::Add, slope, negate(ast::make(Op::Mult, a.copy(), solved[first]->copy())));
			expression->appendChild(negate(f.copy()));
			solved[second] = settle(ast::make(Op::Div, expression, b.copy()));
			canonical_SetFunction(Sym::Invalid);
			work::step(work::Step::Type::Equation, nullptr, &v, solved[second]);
		}
	} else {
		for (unsigned k = 0; k < SIZE && err == Error::Success; k++) {
			const ast &u = *system.unknowns[k];
			ast *left = tidy(
				ast::make(
					Op::Add,
					DiffEq::derivative(u, 1),
					negate(ast::make(Op::Mult, system.matrix[k][k]->copy(), u.copy()))
				)
			);
			ast *reserved = ast::make(Op::Add, ast::make(Sym::D));
			if (k > 0)
				reserved->appendChild(solved[0]->copy());

			err = solve_scalar(
				system,
				ast::make(Op::Equals, left, system.forcing[k]->copy()),
				u,
				system.values + k,
				1,
				reserved,
				&solved[k]
			);
		}
	}

	if (err == Error::Success) {
		ast *list = ast::make(Op::List);

		for (unsigned k = 0; k < SIZE; k++) {
			work::step(work::Step::Type::Equation, k == 0 ? "Solution" : nullptr, system.unknowns[k], solved[k]);
			list->appendChild(ast::make(Op::Equals, system.unknowns[k]->copy(), solved[k]->copy()));
		}

		*solution = list;
	}

	for (unsigned k = 0; k < SIZE; k++) {
		ast::dispose(operators[k]);
		ast::dispose(solved[k]);
	}

	return err;
}

Error DiffEqSystem::solve(ast **solution) {
	*solution = nullptr;

	if (requested == Method::PowerSeries)
		return Error::DeUnsolved;

	if (requested == Method::Elimination)
		return solve_by_elimination(*this, solution);

	DiffEq context;
	ast *scope = ast::make(Op::Add, variable->copy(), vector != nullptr ? vector->copy() : unknowns[0]->copy());

	if (vector == nullptr)
		scope->appendChild(unknowns[1]->copy());
	if (point != nullptr)
		scope->appendChild(point->copy());

	for (unsigned i = 0; i < SIZE; i++) {
		scope->appendChild(forcing[i]->copy());
		if (values[i] != nullptr)
			scope->appendChild(values[i]->copy());
		for (unsigned j = 0; j < SIZE; j++)
			scope->appendChild(matrix[i][j]->copy());
	}

	context.x = variable->copy();
	context.y = vector != nullptr ? vector->copy() : unknowns[0]->copy();
	context.equation = scope;
	context.order = 1;
	context.linear = true;

	return solve_with_matrices(*this, context, solution);
}
