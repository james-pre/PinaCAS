#pragma once

#include "../ast.hxx"
#include "../error.hxx"

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
	/*True to solve with a power series, which solve() also uses when no other method applies*/
	bool series = false;
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

	/*Checks whether solution, written as y = f or as f, satisfies the equation and the initial conditions, and records the check*/
	Error verify(const ast &solution, bool *satisfied);

	/*Solves the equation, using the initial conditions, and records the work. The solution is y = f when explicit and an implicit equation otherwise.*/
	Error solve(ast **solution);
};
