#pragma once

#include "../ast.hxx"
#include "../flags.hxx"
#include "derivative.hxx"
#include "integral.hxx"
#include "de.hxx"

enum class Simp : unsigned short {
	/*
		Changes ast to a form we work with in the simpilfier.

		Reduces every number node to numerator integer / denominator integer
		Changes all roots to powers.
		Expands (AB)^5 to A^5 * B^5

		Generally speaking, this should be included in every simplify() call
	*/
	Normalize = (1u << 0u),

	/*
		Flattens multiplication and addition nodes
		Also changes a multiplication or addition node with only one child
		into just that one child.
	*/
	Commutative = (1u << 1u),

	/*
		Simplifies rational functions.
		A/B/C/D becomes A/(BCD)
	*/
	Rational = (1u << 2u),

	/*
		Simply executes eval() on the node which will evaluate all constant
		expressions like 5 + 5 to 10.
	*/
	Eval = (1u << 3u),

	/*
		Change A + A to 2A
		Change A*A to A^2
		Change A_A to 0
	*/
	LikeTerms = (1u << 4u),

	/*
		Change derivative nodes to their evaluated derivative
		sin(X) + deriv(X^2,X,2) becomes sin(X) + 4
	*/
	Deriv = (1u << 5u),

	/*
		Change integral nodes to their evaluated antiderivative where one can be found
	*/
	Integral = (1u << 12u),

	/*
		Simplifies inverses like sin(asin(X)) = X
		as well as log identities
	*/
	IdGeneral = (1u << 6u),

	/*
		Simplifies trig identities like sin(X)^2 + cos(X)^2 = 1

		ANY Id WILL AUTOMATICALLY SET THE EVAL FLAG, COMMUTATIVE FLAG, AND THE SIMPLIFY LIKE TERMS FLAG
		ANY Id WILL ALSO CALL TO EXPAND() AND FACTOR()
	*/
	IdTrig = (1u << 7u),

	/*
		Simplifies trig constants like sin(pi/4) = sqrt(2)/2

		ANY Id WILL AUTOMATICALLY SET THE EVAL FLAG, COMMUTATIVE FLAG, AND THE SIMPLIFY LIKE TERMS FLAG
		ANY Id WILL ALSO CALL TO EXPAND() AND FACTOR()
	*/
	IdTrigConstants = (1u << 8u),

	/*
		Simplifies inverse trig constants like atan(sqrt(3)) = pi/3

		ANY Id WILL AUTOMATICALLY SET THE EVAL FLAG, COMMUTATIVE FLAG, AND THE SIMPLIFY LIKE TERMS FLAG
		ANY Id WILL ALSO CALL TO EXPAND() AND FACTOR()
	*/
	IdTrigInvConstants = (1u << 9u),

	/*
		Simplifies hyperbolic identities like sinh(X)/cosh(X) = tanh(X)

		ANY Id WILL AUTOMATICALLY SET THE EVAL FLAG, COMMUTATIVE FLAG, AND THE SIMPLIFY LIKE TERMS FLAG
		ANY Id WILL ALSO CALL TO EXPAND() AND FACTOR()
	*/
	IdHyperbolic = (1u << 10u),

	/*
		Simplifies complex functins like sin(z) and ln(z)

		ANY Id WILL AUTOMATICALLY SET THE EVAL FLAG, COMMUTATIVE FLAG, AND THE SIMPLIFY LIKE TERMS FLAG
		ANY Id WILL ALSO CALL TO EXPAND() AND FACTOR()
	*/
	IdComplex = (1u << 11u),

	/*
		Simplify all identities

		ANY Id WILL AUTOMATICALLY SET THE EVAL FLAG, COMMUTATIVE FLAG, AND THE SIMPLIFY LIKE TERMS FLAG
		ANY Id WILL ALSO CALL TO EXPAND() AND FACTOR()
	*/
	IdAll = (IdGeneral | IdTrig | IdTrigConstants | IdTrigInvConstants | IdHyperbolic | IdComplex),

	Basic = (Normalize | Commutative | Rational | Eval | LikeTerms),

	All = (0xFFFFu)
};

template <> inline constexpr bool is_flags<Simp> = true;

/*Simplifies ast. Returns true if changed*/
bool simplify(ast &e, Simp flags);
/*Simplifies sin(9pi/4) to sin(pi/4), and likewise for cos and tan. Returns true if changed.*/
bool simplify_periodic(ast &e);

enum class Canonical : unsigned char {
	/*Order multiplication and division. Change XAZ to AXZ and 1+sin(X)ZA5 to 5AZsin(X)+1*/
	Sort = (1u << 0u),
	/*Rationalize denominators*/
	Rationalize = (1u << 1u),
	/*Change X^(1/2) to sqrt(X)*/
	PowersToRoots = (1u << 2u),
	/*Combine a^5b^5 to (ab)^5.*/
	CombinePowers = (1u << 3u),
	/*Write 1/e^A as e^(-A)*/
	Exponentials = (1u << 4u),
	All = (0xFFu)
};

template <> inline constexpr bool is_flags<Canonical> = true;

/*
	Use only when exporting and do not plan to simplify again
	or execute any other function on ast. Messes up all other algorithms.

	Sorting for addition and multiplication is insertion sort, but recursive so kinda expensive
*/
bool simplify_canonical_form(ast &e, Canonical flags);

/*Sorts the unknown function of a differential equation after its coefficients. Sym::Invalid clears it.*/
void canonical_SetFunction(Sym symbol);
/*Makes the canonical form write sums of numbers times powers of base in ascending order, until called with nullptr*/
void canonical_SetSeries(const ast *base);

enum class Factor : unsigned char {
	/*Factor A + A to A(1 + 1) where at least one part of the
	resulting multiplication can be evauated numerically*/
	SimpleAdditionEvaluateable = (1u << 0u),
	/*Factor A^3+AX to A(A^2+X)*/
	SimpleAdditionNonevaluateable = (1u << 1u),
	/*Factor  x^2 - 1 to (x-1)(x+1)*/
	Polynomial = (1u << 2u),
	All = 0xFF
};

template <> inline constexpr bool is_flags<Factor> = true;

bool factor(ast &e, Factor flags);
/*Factors e to cancel common factors of its numerator and denominator, keeping the result only if it has fewer nodes*/
void factor_cancel(ast &e);

enum class Expand : unsigned char {
	/*Expand 2(A+B) to 2A + 2B or -(A+B) to -1*A + -1*B*/
	DistribNumbers = (1u << 0u),
	/*Expand any f(x)*g(x)*(A+B) to  f(x)g(x)A + f(x)g(x)B*/
	DistribMultiplication = (1u << 1u),
	/*Expand (AB)/2 to (1/2)AB and (A+B)/2 to 1/2A + 1/2B*/
	DistribDivision = (1u << 2u),
	/*Expand (A+B)(C+D) to AC+AD+BC+BD */
	DistribAddition = (1u << 3u),
	/*Distribute (AB)^2 to A^2B^2 and (1/A)^2 to (1^2/A^2)*/
	DistribPowers = (1u << 4u),
	/*Expand (A+B)^2 to A^2 + 2AB + B^2 */
	Powers = (1u << 5u),
	All = 0xFF
};

template <> inline constexpr bool is_flags<Expand> = true;

bool expand(ast &e, Expand flags);

enum class Eval : unsigned short {
	/*Evaluates identities that are too basic to put in identities.c*/
	BasicIdentities = (1u << 0u),
	/*Evaluates addition and multiplication of constants*/
	Commutative = (1u << 1u),
	/*Evaluates 4/2 to 2 and 15/20 to 3/4*/
	Division = (1u << 2u),
	/*Evaluates a^b if a <= 10 and b <= 10*/
	PowersSmall = (1u << 3u),
	PowersFull = (1u << 4u),
	/*Evaluates int(5.5) = 5*/
	Int = (1u << 5u),
	/*Evaluates absolute value of strictly real constants*/
	Abs = (1u << 6u),
	/*Evaluates X! where X <= 10*/
	FactorialSmall = (1u << 7u),
	FactorialFull = (1u << 8u),
	/*Things that can be computed in a reasonable amount of time*/
	Easy = (BasicIdentities | Commutative | Division | Int | Abs | PowersSmall | FactorialSmall),
	/*Things that take a lot of computation to compute*/
	Hard = (PowersFull | FactorialFull),
	All = 0xFFFF
};

template <> inline constexpr bool is_flags<Eval> = true;

/*Evaluates constants such as 5+5 or 6^5. Also implements basic identities
such as 1A = A, A + 0 = A. Returns true if the ast was changed.*/
bool eval(ast &e, Eval flags);

/*Replaces all instances of from ast to to ast in e*/
bool substitute(ast &e, const ast &from, const ast &to);

/*Returns the greatest common divisor of two expressions.
gcd(X(X+1)^2, AX) = X 
gcd(6AX, 10X) = 2X*/
ast *gcd(const ast &a, const ast &b);

/*Helper functions*/

/*Returns true if the node has an imaginary node.*/
bool has_imaginary_node(const ast &e);

bool contains_symbol(const ast &e, Sym symbol);
/*Returns a symbol that does not appear in e, or Sym::Invalid if every candidate does*/
Sym fresh_symbol(const ast &e);
/*Makes fresh_symbol also avoid the symbols in e until it is called again with nullptr*/
void fresh_Reserve(const ast *e);
/*Returns C, or K, or another symbol that does not appear in e, to name an arbitrary constant*/
Sym constant_symbol(const ast &e);
unsigned node_count(const ast &e);

/*Returns true if the node is being multiplied by at least one negative or is already a negative number node.
The node must be completely simplified for this to work, because it does not detect
multiplying by more than one negative to make a positive. */
bool is_negative_for_sure(const ast &a);

/*Returns true if changed. Expects completely simplified. Removes the negative in the multiplier or number.*/
bool absolute_val(ast &e);
