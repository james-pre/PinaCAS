#pragma once

#include <type_traits>

/* Specialize to true to give an enum class bitwise operators */
template <class E> inline constexpr bool is_flags = false;

template <class E>
concept Flags = is_flags<E>;

template <Flags E> constexpr E operator|(E a, E b) {
	using U = std::underlying_type_t<E>;
	return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
}

template <Flags E> constexpr E operator&(E a, E b) {
	using U = std::underlying_type_t<E>;
	return static_cast<E>(static_cast<U>(a) & static_cast<U>(b));
}

template <Flags E> constexpr E operator~(E a) {
	using U = std::underlying_type_t<E>;
	return static_cast<E>(static_cast<U>(~static_cast<U>(a)));
}

template <Flags E> constexpr E &operator|=(E &a, E b) {
	return a = a | b;
}

template <Flags E> constexpr E &operator&=(E &a, E b) {
	return a = a & b;
}

/* True if flags has any of the bits in flag */
template <Flags E> constexpr bool has(E flags, E flag) {
	return (flags & flag) != E{};
}
