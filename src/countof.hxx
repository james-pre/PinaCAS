#pragma once

#include <cstddef>

/* Number of elements in array, failing to compile if array is a pointer */
template <class T, size_t N> constexpr size_t countof(const T (&)[N]) {
	return N;
}
