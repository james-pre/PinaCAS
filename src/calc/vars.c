#ifndef COMPILE_PC

#include "vars.h"

#include <fileioc.h>

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

void write_to_tok(const char *tok, const pcas_ast_t *expression, pcas_error_t *err) {
	ti_var_t var = ti_OpenVar(tok, "w", tok[0] == 0x5Eu ? OS_TYPE_EQU : OS_TYPE_STR);

	if (var != 0) {
		unsigned bin_len;
		/*Write to var*/
		uint8_t *bin = export_to_binary(expression, &bin_len, ti_table, err);
		ti_Write(bin, bin_len, 1, var);

		/*If var is a yvar, enable it*/
		if (var <= 9) {
			/*Thanks Mateo: https://www.cemetech.net/forum/viewtopic.php?t=15947*/
			uint8_t *status;
			status = ti_GetVATPtr(var);
			status--;
			*status |= 1;
		}

		ti_Close(var);
	} else {
		*err = E_GENERIC;
		return;
	}

	*err = E_SUCCESS;
}

#else
typedef int make_iso_compilers_happy;
#endif
