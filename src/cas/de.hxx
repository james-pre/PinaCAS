#pragma once

#include "../ast.hxx"
#include "../error.hxx"

/*How to solve an equation or a system*/
enum class Method : unsigned char {
	Automatic,
	UndeterminedCoefficients,
	VariationOfParameters,
	PowerSeries,
	/*Laplace transforms, or for a system the matrix exponential found with them*/
	Laplace,
	/*Elimination with the differential operator D, for a system*/
	Elimination
};

class DiffEq {
  public:
	static constexpr unsigned max_order = 8;
	static constexpr unsigned default_terms = 4;
	static constexpr unsigned max_conditions = max_order;

	/*The order-th derivative of the unknown function at the point at equals value*/
	struct Condition {
		unsigned order;
		ast *at, *value;
	};

	/*Equals node as entered*/
	ast *equation = nullptr;
	/*Independent variable and unknown function*/
	ast *x = nullptr, *y = nullptr;
	unsigned order = 0;
	bool linear = false;
	/*When linear, the equation is a[order]y^(order) + ... + a[1]y' + a[0]y = g*/
	ast *a[max_order + 1] = {};
	ast *g = nullptr;

	Condition conditions[max_conditions] = {};
	unsigned condition_count = 0;

	/*A solution given with the equation for reduction of order, or nullptr*/
	ast *known = nullptr;

	/*Point to expand a power series about, or nullptr*/
	ast *center = nullptr;
	/*Method chosen by the user. solve() also uses a power series when no other method applies.*/
	Method requested = Method::Automatic;
	/*Symbols the constants of the solution must not be, or nullptr*/
	ast *reserved = nullptr;
	/*Number of nonzero terms of each power series solution*/
	unsigned terms = default_terms;

	/*Name of the method solve() used*/
	const char *method = nullptr;
	/*True for the equation of a substitution, whose solution solve() does not label as final*/
	bool nested = false;

	DiffEq() = default;
	DiffEq(const DiffEq &) = delete;
	DiffEq &operator=(const DiffEq &) = delete;
	~DiffEq();

	/*Frees the equation and resets the canonical form settings it made, if it holds one*/
	void clear();

	/*Returns y with order primes*/
	static ast *derivative(const ast &y, unsigned order);

	/*Classifies equation, an equals node or an expression equal to zero, and records it. Copies its arguments.*/
	Error load(const ast &equation, const ast &x);
	/*Records the order and linearity*/
	void classify() const;

	/*Returns the left side of the linear standard form*/
	ast *standardForm() const;

	/*Loads the equation in items[0] and the initial conditions, known solution and center in the rest, as parse_list returns them*/
	Error loadList(ast **items, unsigned count, const ast &x);

	/*Adds an initial condition written like Y(0)=1 or Y'(2)=-3, as parsed and before simplification*/
	Error addCondition(const ast &condition);

	/*Sets the point to expand a power series about, written like X=1, as parsed and before simplification*/
	Error addCenter(const ast &center);

	/*Adds a known solution written like Y=e^X, as parsed and before simplification*/
	Error addKnownSolution(const ast &solution);

	/* 
	Checks whether solution, written as y = f or as f, satisfies the equation and the initial conditions, and records the check.
	If it does, the equation is homogeneous, second order and linear without initial conditions, and f has no constants of its own,
	sets general to the general solution from reduction of order when it is not nullptr, otherwise to nullptr.
	*/
	Error verify(const ast &solution, bool *satisfied, ast **general = nullptr);

	/*Solves the equation, using the initial conditions, and records the work. The solution is y = f when explicit and an implicit equation otherwise.*/
	Error solve(ast **solution);
};

/*A system x' = Ax + f of two linear first order equations with constant coefficients*/
class DiffEqSystem {
  public:
	static constexpr unsigned size = 2;

	/*Independent variable*/
	ast *variable = nullptr;
	/*Unknown functions, or nullptr when the system is entered in matrix form*/
	ast *unknowns[size] = {};
	/*Unknown vector when the system is entered in matrix form, otherwise nullptr*/
	ast *vector = nullptr;
	ast *matrix[size][size] = {};
	ast *forcing[size] = {};
	/*Point of the initial conditions and the values of the unknowns there, or nullptr without conditions*/
	ast *point = nullptr;
	ast *values[size] = {};

	Method requested = Method::Automatic;
	/*Name of the method solve() used*/
	const char *method = nullptr;

	DiffEqSystem() = default;
	DiffEqSystem(const DiffEqSystem &) = delete;
	DiffEqSystem &operator=(const DiffEqSystem &) = delete;
	~DiffEqSystem();

	void clear();

	/*True if items, as parse_list returns them, hold a system rather than one equation*/
	static bool detect(ast *const *items, unsigned count);

	/* Loads two equations like X'=X+Y and Y'=4X+Y, or one in matrix form like X'=[[1,1][4,1]]X,
	followed by initial conditions like X(0)=1 and Y(0)=0, or X(0)=[[1][0]] in matrix form */
	Error load(ast *const *items, unsigned count, const ast &variable);

	/*Solves the system and records the work. The solution is a list like X = f, Y = g, or X = F in matrix form.*/
	Error solve(ast **solution);
};
