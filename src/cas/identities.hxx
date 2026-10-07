#pragma once

#include "../parser.hxx"

namespace id {

struct Identity {
	const char *from_text, *to_text;
	ast *from, *to;
};

/*Each table ends with an entry whose from_text is nullptr*/
extern Identity general[];
extern Identity trig_identities[];
extern Identity trig_inv_constants[];
extern Identity trig_constants[];
extern Identity hyperbolic[];
extern Identity complex[];

bool load(Identity *id);
void unload(Identity *id);
bool execute(ast &e, Identity *id, bool recursive);

bool executeTable(ast &e, Identity *table, bool recursive);
void loadTable(Identity *table);
void unloadTable(Identity *table);
/*Calls unloadTable for all tables*/
void unloadAll();

} // namespace id
