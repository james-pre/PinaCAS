#pragma once

#ifndef COMPILE_PC

#include <stdbool.h>
#include <stdint.h>

#include "../ast.h"
#include "../error.h"

/*Parses the expression in the variable named by the token tok, a Y= variable, string, or Ans*/
pcas_ast_t *parse_from_tok(const char *tok, pcas_error_t *err);
/*Parses a comma separated list from the variable tok, as parse_list does*/
unsigned parse_list_from_tok(const char *tok, pcas_ast_t **items, unsigned max, pcas_error_t *err);
/*Copies the tokens in the variable tok to data and sets length, which is 0 if tok does not exist. Returns false if tok is longer than max.*/
bool read_tokens_from_tok(const char *tok, uint8_t *data, unsigned max, unsigned *length);
/*Replaces the contents of the variable tok with length bytes of tokens, enabling it if it is a Y= variable*/
void write_tokens_to_tok(const char *tok, const uint8_t *data, unsigned length, pcas_error_t *err);
/*Writes expression to the variable tok, enabling it if it is a Y= variable*/
void write_to_tok(const char *tok, const pcas_ast_t *expression, pcas_error_t *err);

#endif
