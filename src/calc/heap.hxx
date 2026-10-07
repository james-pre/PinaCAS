#pragma once

#include <stddef.h>

namespace heap {

/*Adds the free RAM the OS can spare to the heap*/
void init();

/*Calls failure when an allocation cannot be satisfied, instead of returning nullptr*/
void setFailure(void (*failure)());

/*Returns the number of bytes in the heap that are not allocated*/
size_t available();

} // namespace heap
