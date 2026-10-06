#pragma once

#include "../cas.h"

#define SIMP_BASIC (SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL | SIMP_LIKE_TERMS)

/*A root re, or the pair re ± im*i when im is not NULL. value is the root when it is rational, otherwise NULL.*/
typedef struct {
	pcas_ast_t *re, *im;
	mp_rat value;
	unsigned multiplicity;
} root_t;

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

/*Expands e when that leaves fewer nodes*/
void expand_if_smaller(pcas_ast_t *e);

/*Returns d/dv(e) as an unevaluated derivative node*/
pcas_ast_t *derivative_node(pcas_ast_t *e, const pcas_ast_t *v);

/*Returns e^(integral of P dv) without absolute values, recording the integral, or NULL if it cannot be found. Takes ownership of P.*/
pcas_ast_t *exponential_of_integral(pcas_ast_t *P, const pcas_ast_t *v);

/*Simplifies e lightly for display and returns it*/
pcas_ast_t *tidy(pcas_ast_t *e);

/*Writes e as one fraction with an expanded numerator and cancels common factors*/
void single_fraction(pcas_ast_t *e);

/*Returns preferred, or a symbol that does not appear in the equation if preferred does*/
pcas_ast_t *substitution_symbol(const pcas_de_t *de, Symbol preferred);

/*Checks whether solution, written as y = f or as f, satisfies the equation, and the initial conditions if conditions, recording the check under text*/
pcas_error_t check_solution(
	pcas_de_t *de,
	const pcas_ast_t *solution,
	const char *text,
	bool conditions,
	bool *satisfied
);

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

/*Finds the roots of the characteristic polynomial in m with their multiplicities, recording the work*/
pcas_error_t characteristic_roots(pcas_de_t *de, const pcas_ast_t *m, root_t *roots, unsigned *count);

void free_roots(root_t *roots, unsigned count);

/*Returns x^j e^(rx) f, leaving out f when it is NULL. Takes ownership of f.*/
pcas_ast_t *basis_function(const pcas_de_t *de, unsigned j, const pcas_ast_t *r, pcas_ast_t *f);

/*Fills basis with x^j e^(rx) for each root r, or x^j e^(ax)cos(bx) and x^j e^(ax)sin(bx) for each pair a ± bi, and returns how many there are*/
unsigned fill_basis(const pcas_de_t *de, const root_t *roots, unsigned count, pcas_ast_t **basis);

/*Solves a linear equation with constant coefficients from the roots of its characteristic polynomial, finding a particular solution by undetermined coefficients or variation of parameters when it is not homogeneous*/
pcas_error_t solve_constant_coefficients(pcas_de_t *de, pcas_ast_t **solution);

/*Finds a second solution of a homogeneous second order linear equation from the known solution with the reduction of order formula*/
pcas_error_t solve_reduction_of_order(pcas_de_t *de, pcas_ast_t **solution);

/*Returns the value of e if it is a rational number, otherwise NULL*/
mp_rat rational_value(const pcas_ast_t *e);

/*Sets value to p(r), where p has degree n*/
void evaluate(mp_rat *p, unsigned n, mp_rat r, mp_rat value);

bool is_root(mp_rat *p, unsigned n, mp_rat r);

/*Divides p of degree n by m - r, which must be a factor*/
void deflate(mp_rat *p, unsigned n, mp_rat r);

/*Finds the roots of p of degree n, dividing out the rational ones. Returns false if some cannot be found.*/
bool find_roots(mp_rat *p, unsigned *n, root_t *roots, unsigned *count);

/*Returns the polynomial in m with coefficients p[k]/divisor*/
pcas_ast_t *polynomial(mp_rat *p, unsigned n, const pcas_ast_t *m, mp_rat divisor);

/*Returns the polynomial as the factors (bm - a)^k of its rational roots a/b times p, what remains of it after dividing them out*/
pcas_ast_t *factored_form(mp_rat *p, unsigned n, const root_t *roots, unsigned count, const pcas_ast_t *m);

/*Fills constants with n letters that do not appear in the equation or in exclude unless it is NULL. Returns false if there are not enough.*/
bool choose_constants(const pcas_de_t *de, const pcas_ast_t *exclude, pcas_ast_t **constants, unsigned n);

/*Solves a linear equation with polynomial coefficients and right side with a power series about an ordinary point, recording the recurrence relation and the coefficients*/
pcas_error_t solve_power_series(pcas_de_t *de, pcas_ast_t **solution);
