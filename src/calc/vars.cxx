#ifndef COMPILE_PC

#include "vars.hxx"

#include <fileioc.h>
#include <stdlib.h>
#include <string.h>

#include "../parser.hxx"

/*Expects var to be properly opened*/
static bool is_var_string_type(ti_var_t var) {
	/*Check if 4 low bits from first byte of vat data is 0x4*/
	return (*(reinterpret_cast<uint8_t *>(ti_GetVATPtr(var))) & 0xFu) == 0x4u;
}

unsigned parse_list_from_tok(const char *tok, ast **items, unsigned max, Error *err) {
	ti_var_t var = ti_OpenVar(tok, "r", tok[0] == 0x5Eu ? OS_TYPE_EQU : OS_TYPE_STR);

	/*If we're opening Ans or a string and the type is not a string type*/
	if (var == 0 || (tok[0] != 0x5Eu && !is_var_string_type(var))) {
		if (var != 0)
			ti_Close(var);
		*err = Error::Generic;
		return 0;
	}

	unsigned count =
		parse_list(static_cast<const char *>(ti_GetDataPtr(var)), ti_GetSize(var), ti_table, items, max, err);

	ti_Close(var);

	return count;
}

ast *parse_from_tok(const char *tok, Error *err) {
	ast *result;
	return parse_list_from_tok(tok, &result, 1, err) == 1 ? result : nullptr;
}

bool read_tokens_from_tok(const char *tok, char *data, unsigned max, unsigned *length) {
	ti_var_t var = ti_OpenVar(tok, "r", tok[0] == 0x5Eu ? OS_TYPE_EQU : OS_TYPE_STR);
	bool fits = true;

	*length = 0;

	if (var == 0)
		return true;

	if (ti_GetSize(var) <= max) {
		*length = ti_GetSize(var);
		memcpy(data, ti_GetDataPtr(var), *length);
	} else {
		fits = false;
	}

	ti_Close(var);

	return fits;
}

void write_tokens_to_tok(const char *tok, const uint8_t *data, unsigned length, Error *err) {
	ti_var_t var = ti_OpenVar(tok, "w", tok[0] == 0x5Eu ? OS_TYPE_EQU : OS_TYPE_STR);

	if (var == 0) {
		*err = Error::Generic;
		return;
	}

	if (length > 0)
		ti_Write(data, length, 1, var);

	/*If var is a yvar, enable it*/
	if (var <= 9) {
		/*Thanks Mateo: https://www.cemetech.net/forum/viewtopic.php?t=15947*/
		uint8_t *status = static_cast<uint8_t *>(ti_GetVATPtr(var));
		status--;
		*status |= 1;
	}

	ti_Close(var);

	*err = Error::Success;
}

void write_to_tok(const char *tok, const ast &expression, Error *err) {
	unsigned length;
	uint8_t *data = export_to_binary(expression, &length, ti_table, err);

	if (data == nullptr)
		return;

	write_tokens_to_tok(tok, data, length, err);
	free(data);
}

#endif
