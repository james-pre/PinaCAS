#pragma once

#ifdef COMPILE_PC

#include <stdio.h>
#include <stdint.h>

typedef struct {
	char comment[42];
	uint16_t var_len;
	uint16_t checksum;
} header_t;

typedef struct {
	header_t header;

	uint16_t len;

	char name[8];
	uint8_t version;
	uint8_t flag;

	uint16_t yvar_data_len;
	uint8_t *data;
} yvar_t;

/*Only works on little endian systems right now.*/

int yvar_Read(yvar_t *yvar, FILE *file);

void yvar_Cleanup(yvar_t *yvar);

#endif
