#pragma once

#include "../ast.h"
#include "../error.h"

#define DE_MAX_ORDER 8
#define DE_DEFAULT_TERMS 4
#define DE_MAX_CONDITIONS DE_MAX_ORDER

/*The order-th derivative of the unknown function at the point at equals value*/
typedef struct {
	unsigned order;
	pcas_ast_t *at, *value;
} pcas_condition_t;

typedef struct {
	/*Equals node as entered*/
	pcas_ast_t *equation;
	/*Independent variable and unknown function*/
	pcas_ast_t *x, *y;
	unsigned order;
	bool linear;
	/*When linear, the equation is a[order]y^(order) + ... + a[1]y' + a[0]y = g*/
	pcas_ast_t *a[DE_MAX_ORDER + 1];
	pcas_ast_t *g;

	pcas_condition_t conditions[DE_MAX_CONDITIONS];
	unsigned condition_count;

	/*A solution given with the equation for reduction of order, or NULL*/
	pcas_ast_t *known;

	/*Point to expand a power series about, or NULL*/
	pcas_ast_t *center;
	/*True to solve with a power series, which de_Solve also uses when no other method applies*/
	bool series;
	/*Number of nonzero terms of each power series solution*/
	unsigned terms;

	/*Name of the method de_Solve used*/
	const char *method;
	/*True for the equation of a substitution, whose solution de_Solve does not label as final*/
	bool nested;
} pcas_de_t;

/*Returns y with order primes*/
pcas_ast_t *de_Derivative(const pcas_ast_t *y, unsigned order);

/*Classifies equation, an equals node or an expression equal to zero, and records it. Copies its arguments. de_Cleanup must be called even on failure.*/
pcas_error_t de_Load(pcas_de_t *de, const pcas_ast_t *equation, const pcas_ast_t *x);
/*Records the order and linearity*/
void de_Classify(pcas_de_t *de);
void de_Cleanup(pcas_de_t *de);

/*Returns the left side of the linear standard form*/
pcas_ast_t *de_StandardForm(pcas_de_t *de);

/*Loads the equation in items[0] and the initial conditions, known solution and center in the rest, as parse_list returns them*/
pcas_error_t de_LoadList(pcas_de_t *de, pcas_ast_t **items, unsigned count, const pcas_ast_t *x);

/*Adds an initial condition written like Y(0)=1 or Y'(2)=-3, as parsed and before simplification*/
pcas_error_t de_AddCondition(pcas_de_t *de, const pcas_ast_t *condition);

/*Sets the point to expand a power series about, written like X=1, as parsed and before simplification*/
pcas_error_t de_AddCenter(pcas_de_t *de, const pcas_ast_t *center);

/*Adds a known solution written like Y=e^X, as parsed and before simplification*/
pcas_error_t de_AddKnownSolution(pcas_de_t *de, const pcas_ast_t *solution);

/*Checks whether solution, written as y = f or as f, satisfies the equation and the initial conditions, and records the check*/
pcas_error_t de_Verify(pcas_de_t *de, const pcas_ast_t *solution, bool *satisfied);

/*Solves the equation, using the initial conditions, and records the work. The solution is y = f when explicit and an implicit equation otherwise.*/
pcas_error_t de_Solve(pcas_de_t *de, pcas_ast_t **solution);
