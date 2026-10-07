#pragma once

#ifndef COMPILE_PC

#include <stdbool.h>
#include <stdint.h>

#include "../ast.hxx"
#include "../error.hxx"

/*Parses the expression in the variable named by the token tok, a Y= variable, string, or Ans*/
ast *parse_from_tok(const char *tok, Error *err);
/*Parses a comma separated list from the variable tok, as parse_list does*/
unsigned parse_list_from_tok(const char *tok, ast **items, unsigned max, Error *err);
/*Copies the tokens in the variable tok to data and sets length, which is 0 if tok does not exist. Returns false if tok is longer than max.*/
bool read_tokens_from_tok(const char *tok, uint8_t *data, unsigned max, unsigned *length);
/*Replaces the contents of the variable tok with length bytes of tokens, enabling it if it is a Y= variable*/
void write_tokens_to_tok(const char *tok, const uint8_t *data, unsigned length, Error *err);
/*Writes expression to the variable tok, enabling it if it is a Y= variable*/
void write_to_tok(const char *tok, const ast *expression, Error *err);

#endif
