#pragma once

#ifdef COMPILE_PC

#include <stdio.h>
#include <stdint.h>

struct Header {
	char comment[42];
	uint16_t var_len;
	uint16_t checksum;
};

struct YVar {
	Header header;

	uint16_t len;

	char name[8];
	uint8_t version;
	uint8_t flag;

	uint16_t yvar_data_len;
	uint8_t *data;
};

/*Only works on little endian systems right now.*/

int yvar_Read(YVar *yvar, FILE *file);

void yvar_Cleanup(YVar *yvar);

#endif
