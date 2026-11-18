#pragma once

#ifndef COMPILE_PC

#include <stdbool.h>

#include "../ast.hxx"
#include "../cas/de.hxx"

/*Most expressions in one input: an equation and its initial conditions*/
constexpr unsigned MAX_ITEMS = DiffEq::max_conditions + 1;

/*Replaces items[0] with the result of kind with respect to respect_to, ready to export. A transform is to the variable transform. For Kind::DiffEq the items are an equation or a system with initial conditions, solved with method when it applies, with terms nonzero terms per power series solution and with Laplace transforms when the equation needs them. Writes a one line summary to summary if it is not nullptr.*/
namespace calculus {

enum class Kind : unsigned char { Derivative, Integral, PartialFractions, Laplace, LaplaceInverse, DiffEq };

Error run(
	Kind kind,
	ast **items,
	unsigned count,
	ast &respect_to,
	const ast *transform,
	Method method,
	unsigned terms,
	char *summary
);
/*Checks whether solution satisfies the equation and initial conditions in items.
Replaces items[0] with the general solution and sets extended when reduction of order finds a second solution.*/
Error verify(ast **items, unsigned count, ast &respect_to, ast &solution, bool *satisfied, bool *extended);

} // namespace calculus

#endif
