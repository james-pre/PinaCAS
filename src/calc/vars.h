#pragma once

#ifndef COMPILE_PC

#include <stdint.h>

#include "../ast.h"
#include "../error.h"

/*Parses the expression in the variable named by the token tok, a Y= variable, string, or Ans*/
pcas_ast_t *parse_from_tok(const char *tok, pcas_error_t *err);
/*Parses a comma separated list from the variable tok, as parse_list does*/
unsigned parse_list_from_tok(const char *tok, pcas_ast_t **items, unsigned max, pcas_error_t *err);
/*Writes expression to the variable tok, enabling it if it is a Y= variable*/
void write_to_tok(const char *tok, const pcas_ast_t *expression, pcas_error_t *err);

#endif
