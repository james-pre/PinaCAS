#pragma once

#include "../cas.h"

#define SIMP_BASIC (SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL | SIMP_LIKE_TERMS)

pcas_ast_t *integer(mp_small n);

pcas_ast_t *negate(pcas_ast_t *a);

pcas_ast_t *difference(pcas_ast_t *a, pcas_ast_t *b);

/*Replaces y, y', ..., y^(order) in f with distinct symbols that are not x, so the derivatives can be treated as variables*/
void derivatives_to_symbols(pcas_de_t *de, pcas_ast_t *f, pcas_ast_t **symbols);

bool is_euler(const pcas_ast_t *e);

pcas_ast_t *ln(pcas_ast_t *a);

/*Returns base^e, writing e^(A+B) as e^A*e^B, e^ln(A) as A and e^(n*ln(A)) as A^n. Takes ownership of e.*/
pcas_ast_t *exponential(const pcas_ast_t *base, pcas_ast_t *e);

/*Writes e as one fraction num/den, without simplifying*/
void rational_parts(pcas_ast_t *e, pcas_ast_t **num, pcas_ast_t **den);

/*True if e simplifies to zero, trying identities only when needed*/
bool is_zero(const pcas_ast_t *e);

bool involves(const pcas_ast_t *e, const pcas_ast_t *v);

/*Returns d/dv(e) as an unevaluated derivative node*/
pcas_ast_t *derivative_node(pcas_ast_t *e, const pcas_ast_t *v);

/*Simplifies e lightly for display and returns it*/
pcas_ast_t *tidy(pcas_ast_t *e);

/*Writes e as one fraction with an expanded numerator and cancels common factors*/
void single_fraction(pcas_ast_t *e);

/*Returns preferred, or a symbol that does not appear in the equation if preferred does*/
pcas_ast_t *substitution_symbol(const pcas_de_t *de, Symbol preferred);

/*Returns e with the point of the initial condition substituted*/
pcas_ast_t *at_condition(pcas_de_t *de, const pcas_ast_t *e, pcas_condition_t *c);

/*Solves lhs = rhs for y and records the solution. Takes ownership of lhs and rhs.*/
void conclude(
	pcas_de_t *de,
	pcas_ast_t *lhs,
	pcas_ast_t *rhs,
	const pcas_ast_t *constant,
	pcas_condition_t *c,
	pcas_ast_t **solution
);

/*Records lhs = antiderivative + C, finds C from the initial condition and solves for y. Takes ownership of lhs and antiderivative.*/
void finish(pcas_de_t *de, pcas_ast_t *lhs, pcas_ast_t *antiderivative, pcas_ast_t **solution);

pcas_error_t solve_first_order(pcas_de_t *de, pcas_ast_t **solution);

/*Solves a homogeneous linear equation with constant coefficients from the roots of its characteristic polynomial*/
pcas_error_t solve_constant_coefficients(pcas_de_t *de, pcas_ast_t **solution);
