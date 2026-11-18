#pragma once

#include "../cas.hxx"

/*A root re, or the pair re ± im*i when im is not nullptr. value is the root when it is rational, otherwise nullptr.*/
struct Root {
	ast *re, *im;
	Num *value;
	unsigned multiplicity;
};

ast *integer(mp_small n);

ast *negate(ast *a);

ast *difference(ast *a, ast *b);

/*Replaces y, y', ..., y^(order) in f with distinct symbols that are not x, so the derivatives can be treated as variables*/
void derivatives_to_symbols(DiffEq *de, ast &f, ast **symbols);

bool is_euler(const ast &e);

ast *ln(ast *a);

/*Returns base^e, writing e^(A+B) as e^A*e^B, e^ln(A) as A and e^(n*ln(A)) as A^n. Takes ownership of e.*/
ast *exponential(const ast &base, ast *e);

/*Writes e as one fraction num/den, without simplifying*/
void rational_parts(const ast &e, ast **numer, ast **denom);

/*True if e simplifies to zero, trying identities only when needed*/
bool is_zero(const ast &e);

bool involves(const ast &e, const ast &v);

/*Expands e when that leaves fewer nodes*/
void expand_if_smaller(ast &e);

/*Returns d/dv(e) as an unevaluated derivative node*/
ast *derivative_node(ast *e, const ast &v);

/*Returns e^(integral of P dv) without absolute values, recording the integral, or nullptr if it cannot be found. Takes ownership of P.*/
ast *exponential_of_integral(ast *P, const ast &v);

/*Simplifies e lightly for display and returns it*/
ast *tidy(ast *e);

/*Writes e as one fraction with an expanded numerator and cancels common factors*/
void single_fraction(ast &e);

/*Returns preferred, or a symbol that does not appear in the equation if preferred does*/
ast *substitution_symbol(const DiffEq *de, Sym preferred);

/*Checks whether solution, written as y = f or as f, satisfies the equation, and the initial conditions if conditions, recording the check under text*/
Error check_solution(DiffEq *de, const ast &solution, const char *text, bool conditions, bool *satisfied);

/*Returns e with the point of the initial condition substituted*/
ast *at_condition(DiffEq *de, const ast &e, DiffEq::Condition *c);

/*Solves lhs = rhs for y and records the solution. Takes ownership of lhs and rhs.*/
void conclude(DiffEq *de, ast *lhs, ast *rhs, const ast &constant, DiffEq::Condition *c, ast **solution);

/*Records lhs = antiderivative + C, finds C from the initial condition and solves for y. Takes ownership of lhs and antiderivative.*/
void finish(DiffEq *de, ast *lhs, ast *antiderivative, ast **solution);

Error solve_first_order(DiffEq *de, ast **solution);

/*Finds the roots of the characteristic polynomial in m with their multiplicities, recording the work*/
Error characteristic_roots(DiffEq *de, const ast &m, Root *roots, unsigned *count);

void free_roots(Root *roots, unsigned count);

/*Returns x^j e^(rx) f, leaving out f when it is nullptr. Takes ownership of f.*/
ast *basis_function(const DiffEq *de, unsigned j, const ast &r, ast *f);

/* Fills basis with x^j e^(rx) for each root r, or x^j e^(ax)cos(bx) and x^j e^(ax)sin(bx) for each pair a ± bi, and returns how many there are.
With euler, the functions are ln(x)^j x^r, ln(x)^j x^a cos(b ln(x)) and ln(x)^j x^a sin(b ln(x)). */
unsigned fill_basis(const DiffEq *de, const Root *roots, unsigned count, ast **basis, bool euler);

/*Records p = 0 under text, a polynomial of degree n in m, and finds its roots with their multiplicities, recording the work. Divides the rational roots out of p.*/
Error polynomial_roots(Num **p, unsigned n, const ast &m, const char *text, Root *roots, unsigned *count);

/*Solves a linear equation with constant coefficients from the roots of its characteristic polynomial, finding a particular solution by undetermined coefficients or variation of parameters when it is not homogeneous*/
Error solve_constant_coefficients(DiffEq *de, ast **solution);

/* Solves a Cauchy-Euler equation, with each a[k] a constant times x^k, from the roots of its auxiliary equation,
finding a particular solution by variation of parameters when it is not homogeneous */
Error solve_cauchy_euler(DiffEq *de, ast **solution);

/*Finds a second solution of a homogeneous second order linear equation from the known solution with the reduction of order formula, first checking the known solution unless checked*/
Error solve_reduction_of_order(DiffEq *de, bool checked, ast **solution);

/*Returns the value of e if it is a rational number, otherwise nullptr*/
Num *rational_value(const ast &e);

/*Sets value to p(r), where p has degree n*/
void evaluate(Num **p, unsigned n, const Num &r, Num &value);

bool is_root(Num **p, unsigned n, const Num &r);

/*Divides p of degree n by m - r, which must be a factor*/
void deflate(Num **p, unsigned n, const Num &r);

/*Finds the roots of p of degree n, dividing out the rational ones. Returns false if some cannot be found.*/
bool find_roots(Num **p, unsigned *n, Root *roots, unsigned *count);

/*Returns the polynomial in m with coefficients p[k]/divisor*/
ast *polynomial(Num *const *p, unsigned n, const ast &m, const Num &divisor);

/*Returns the polynomial in m with coefficients c[k]*/
ast *polynomial(ast *const *c, unsigned n, const ast &m);

/*Returns the polynomial as the factors (bm - a)^k of its rational roots a/b times p, what remains of it after dividing them out*/
ast *factored_form(Num **p, unsigned n, const Root *roots, unsigned count, const ast &m);

/*Fills constants with n letters that do not appear in the equation or in exclude unless it is nullptr. Returns false if there are not enough.*/
bool choose_constants(const DiffEq *de, const ast *exclude, ast **constants, unsigned n);

/*Returns preferred, or a symbol that does not appear in the equation or taken if preferred does*/
ast *pick_symbol(const DiffEq *de, Sym preferred, const ast &taken);

/*Simplifies e with every identity, without recording work, and returns it*/
ast *reduced(ast *e);

/*Simplifies e, keeping the smallest of its simplified, expanded and identity-reduced forms*/
ast *simplest(ast *e);

/*Returns a/b simplified as one fraction with common factors cancelled and an expanded denominator*/
ast *quotient(const ast &a, const ast &b);

/*Returns a times b, expanded term by term when that leaves fewer nodes*/
ast *distribute(const ast &a, const ast &b);

/*Reduces the augmented matrix with Gauss-Jordan elimination, setting pivots to the column of each pivot row, and returns the number of pivot rows*/
unsigned eliminate(ast *matrix[][DiffEq::max_order + 1], unsigned rows, unsigned n, unsigned *pivots);

/*Reads term as a constant times x^degree e^(rate*x), times cos or sin of frequency*x unless frequency is zero. Returns false if it has another form.*/
bool forcing_form(const DiffEq *de, const ast *term, unsigned *degree, ast **rate, ast **frequency);

/*Returns the multiplicity of rate + frequency*i as a characteristic root*/
unsigned root_multiplicity(const Root *roots, unsigned count, const ast &rate, const ast &frequency);

/*Returns op(frequency*x)*/
ast *oscillation(const DiffEq *de, Op op, const ast &frequency);

/*Equates the coefficients of each function of x in each residual to zero and solves for the unknowns, recording the work. Sets values to the unknowns when it succeeds.*/
Error solve_coefficients(
	DiffEq *de,
	ast *const *residuals,
	unsigned count,
	ast **unknowns,
	unsigned size,
	ast **values
);

/*Solves a linear equation with polynomial coefficients and right side with a power series about an ordinary point, recording the recurrence relation and the coefficients*/
Error solve_power_series(DiffEq *de, ast **solution);
