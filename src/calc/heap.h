#pragma once

#include <stddef.h>

/*Adds the free RAM the OS can spare to the heap*/
void heap_Init(void);

/*Calls failure when an allocation cannot be satisfied, instead of returning NULL*/
void heap_SetFailure(void (*failure)(void));

/*Returns the number of bytes in the heap that are not allocated*/
size_t heap_Available(void);
