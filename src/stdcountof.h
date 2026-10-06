#pragma once
#pragma GCC system_header

/*
Reference:
https://www.man7.org/linux/man-pages/man3/countof.3.html
https://gcc.gnu.org/onlinedocs/gcc/_005fCountof.html
*/

#ifdef __has_include_next
#if __has_include_next(<stdcountof.h>)
#include_next <stdcountof.h>
#endif
#endif

#ifndef countof
/*Number of elements in array, failing to compile if array is a pointer*/
#define countof(array)                                                                                                 \
	(sizeof(array) / sizeof((array)[0]) +                                                                              \
	 0 * sizeof(char[1 - 2 * __builtin_types_compatible_p(__typeof__(array), __typeof__(&(array)[0]))]))
#endif
