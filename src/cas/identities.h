#pragma once

#include "../parser.h"

typedef struct {
	const char *from_text, *to_text;
	pcas_ast_t *from, *to;
} pcas_id_t;

/*Each table ends with an entry whose from_text is NULL*/
extern pcas_id_t id_general[];
extern pcas_id_t id_trig_identities[];
extern pcas_id_t id_trig_inv_constants[];
extern pcas_id_t id_trig_constants[];
extern pcas_id_t id_hyperbolic[];
extern pcas_id_t id_complex[];

bool id_Load(pcas_id_t *id);
void id_Unload(pcas_id_t *id);
bool id_Execute(pcas_ast_t *e, pcas_id_t *id, bool recursive);

bool id_ExecuteTable(pcas_ast_t *e, pcas_id_t *table, bool recursive);
void id_LoadTable(pcas_id_t *table);
void id_UnloadTable(pcas_id_t *table);
/*Calls unload table for all tables*/
void id_UnloadAll(void);
