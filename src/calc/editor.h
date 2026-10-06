#pragma once

#ifndef COMPILE_PC

#include <stdbool.h>

/*Lets the user edit the variable tok, called name, full screen, unless it is too long. Returns true if it was saved. Expects graphx to be running.*/
bool editor_Run(const char *name, const char *tok);

#endif
