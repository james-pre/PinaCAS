#ifndef COMPILE_PC

#include "vars.h"

#include <fileioc.h>
#include <stdlib.h>
#include <string.h>

#include "../parser.h"

/*Expects var to be properly opened*/
static bool is_var_string_type(ti_var_t var) {
	/*Check if 4 low bits from first byte of vat data is 0x4*/
	return (*((uint8_t *)ti_GetVATPtr(var)) & 0xFu) == 0x4u;
}

unsigned parse_list_from_tok(const char *tok, pcas_ast_t **items, unsigned max, pcas_error_t *err) {
	ti_var_t var = ti_OpenVar(tok, "r", tok[0] == 0x5Eu ? OS_TYPE_EQU : OS_TYPE_STR);

	/*If we're opening Ans or a string and the type is not a string type*/
	if (var == 0 || (tok[0] != 0x5Eu && !is_var_string_type(var))) {
		if (var != 0)
			ti_Close(var);
		*err = E_GENERIC;
		return 0;
	}

	unsigned count = parse_list(ti_GetDataPtr(var), ti_GetSize(var), ti_table, items, max, err);

	ti_Close(var);

	return count;
}

pcas_ast_t *parse_from_tok(const char *tok, pcas_error_t *err) {
	pcas_ast_t *result;
	return parse_list_from_tok(tok, &result, 1, err) == 1 ? result : NULL;
}

unsigned read_tokens_from_tok(const char *tok, uint8_t *data, unsigned max) {
	ti_var_t var = ti_OpenVar(tok, "r", tok[0] == 0x5Eu ? OS_TYPE_EQU : OS_TYPE_STR);
	unsigned length = 0;

	if (var == 0)
		return 0;

	if (ti_GetSize(var) <= max) {
		length = ti_GetSize(var);
		memcpy(data, ti_GetDataPtr(var), length);
	}

	ti_Close(var);

	return length;
}

void write_tokens_to_tok(const char *tok, const uint8_t *data, unsigned length, pcas_error_t *err) {
	ti_var_t var = ti_OpenVar(tok, "w", tok[0] == 0x5Eu ? OS_TYPE_EQU : OS_TYPE_STR);

	if (var == 0) {
		*err = E_GENERIC;
		return;
	}

	if (length > 0)
		ti_Write(data, length, 1, var);

	/*If var is a yvar, enable it*/
	if (var <= 9) {
		/*Thanks Mateo: https://www.cemetech.net/forum/viewtopic.php?t=15947*/
		uint8_t *status;
		status = ti_GetVATPtr(var);
		status--;
		*status |= 1;
	}

	ti_Close(var);

	*err = E_SUCCESS;
}

void write_to_tok(const char *tok, const pcas_ast_t *expression, pcas_error_t *err) {
	unsigned length;
	uint8_t *data = export_to_binary(expression, &length, ti_table, err);

	if (data == NULL)
		return;

	write_tokens_to_tok(tok, data, length, err);
	free(data);
}

#else
typedef int make_iso_compilers_happy;
#endif
