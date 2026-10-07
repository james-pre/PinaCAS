#pragma once

#ifndef COMPILE_PC

#include <stdbool.h>

#include "../ast.h"
#include "../cas/de.h"

typedef enum : unsigned char { CALCULUS_DERIVATIVE, CALCULUS_INTEGRAL, CALCULUS_DE } Calculus;

/*Most expressions in one input: an equation and its initial conditions*/
#define MAX_ITEMS (DE_MAX_CONDITIONS + 1)

/*Replaces items[0] with the result of kind with respect to respect_to, ready to export. The other items are initial conditions for CALCULUS_DE, which uses a power series with terms nonzero terms per solution when series is true. Writes a one line summary to summary if it is not NULL.*/
pcas_error_t calculus_Run(
	Calculus kind,
	pcas_ast_t **items,
	unsigned count,
	pcas_ast_t *respect_to,
	bool series,
	unsigned terms,
	char *summary
);
/*Checks whether solution satisfies the equation and initial conditions in items*/
pcas_error_t calculus_Verify(
	pcas_ast_t **items,
	unsigned count,
	pcas_ast_t *respect_to,
	pcas_ast_t *solution,
	bool *satisfied
);

#endif
