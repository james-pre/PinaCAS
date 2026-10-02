#pragma once

#ifndef COMPILE_PC

#include <stdbool.h>

#include <tice.h>
#include <fileioc.h>

#include "../ast.h"

typedef enum {
    CALCULUS_DERIVATIVE,
    CALCULUS_INTEGRAL,
    CALCULUS_DE
} Calculus;

/*Replaces e with the result of kind with respect to respect_to, ready to export. Writes a one line summary to summary if it is not NULL.*/
pcas_error_t calculus_Run(Calculus kind, pcas_ast_t *e, pcas_ast_t *respect_to, char *summary);

/*Checks if Ans is trying to call a function of PCAS*/
bool interface_Valid(void);
/*Run function specified by Ans variable without gui*/
void interface_Run(void);

/*Helper functions*/
pcas_ast_t *parse_from_tok(uint8_t *tok, pcas_error_t *err);
void write_to_tok(uint8_t *tok, pcas_ast_t *expression, pcas_error_t *err);
/*Expects var to be properly opened*/
bool is_var_string_type(ti_var_t var);

#endif
