#pragma once

#ifndef COMPILE_PC

#include <stdbool.h>

#include <tice.h>
#include <fileioc.h>

#include "../ast.h"
#include "../cas/de.h"

typedef enum { CALCULUS_DERIVATIVE, CALCULUS_INTEGRAL, CALCULUS_DE } Calculus;

/*Most expressions in one input: an equation and its initial conditions*/
#define MAX_ITEMS (DE_MAX_CONDITIONS + 1)

/*Replaces items[0] with the result of kind with respect to respect_to, ready to export. The other items are initial conditions for CALCULUS_DE. Writes a one line summary to summary if it is not NULL.*/
pcas_error_t calculus_Run(Calculus kind, pcas_ast_t **items, unsigned count, pcas_ast_t *respect_to, char *summary);
/*Checks whether solution satisfies the equation and initial conditions in items*/
pcas_error_t calculus_Verify(
	pcas_ast_t **items, unsigned count, pcas_ast_t *respect_to, pcas_ast_t *solution, bool *satisfied
);

/*Checks if Ans is trying to call a function of PCAS*/
bool interface_Valid(void);
/*Run function specified by Ans variable without gui*/
void interface_Run(void);

/*Helper functions*/
pcas_ast_t *parse_from_tok(uint8_t *tok, pcas_error_t *err);
/*Parses a comma separated list from the variable tok, as parse_list does*/
unsigned parse_list_from_tok(uint8_t *tok, pcas_ast_t **items, unsigned max, pcas_error_t *err);
void write_to_tok(uint8_t *tok, pcas_ast_t *expression, pcas_error_t *err);
/*Expects var to be properly opened*/
bool is_var_string_type(ti_var_t var);

#endif
