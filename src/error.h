#pragma once

typedef enum {
	E_SUCCESS,

	/*Because of bad programming*/
	E_GENERIC,

	/*trying to add nodes to a NODE_NUMBER for instance*/
	E_AST_NOT_ALLOWED,
	E_AST_OUT_OF_BOUNDS,

	/*Tokenizing and parsing*/
	E_TOK_INVALID,

	E_PARSE_BAD_OPERATOR,
	E_PARSE_UNMATCHED_CLOSE_PAR,
	E_PARSE_BAD_COMMA,

	E_EVAL_NO_MAPPING,

	/*Differential equations*/
	E_DE_NO_DERIVATIVE,
	E_DE_BAD_FUNCTION,
	E_DE_ORDER,
	E_DE_BAD_CONDITION,
	E_DE_IMPLICIT,
	E_DE_UNSOLVED,
	E_DE_INTEGRAL,
	E_DE_ROOTS,
	E_DE_NO_SOLUTION,
	E_DE_NOT_SOLUTION,
	E_DE_SINGULAR,

	AMOUNT_ERRORS
} pcas_error_t;

extern const char *error_text[AMOUNT_ERRORS];
