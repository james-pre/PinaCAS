#pragma once

#ifndef COMPILE_PC

#include <stdbool.h>

#include "../ast.hxx"
#include "../cas/de.hxx"

/*Most expressions in one input: an equation and its initial conditions*/
constexpr unsigned MAX_ITEMS = DiffEq::max_conditions + 1;

/*Replaces items[0] with the result of kind with respect to respect_to, ready to export. The other items are initial conditions for Kind::DiffEq, which uses a power series with terms nonzero terms per solution when series is true. Writes a one line summary to summary if it is not nullptr.*/
namespace calculus {

enum class Kind : unsigned char { Derivative, Integral, DiffEq };

Error run(Kind kind, ast **items, unsigned count, ast &respect_to, bool series, unsigned terms, char *summary);
/*Checks whether solution satisfies the equation and initial conditions in items*/
Error verify(ast **items, unsigned count, ast &respect_to, ast &solution, bool *satisfied);

} // namespace calculus

#endif
