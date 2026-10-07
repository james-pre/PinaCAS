#pragma once

#include "imath/imrat.h"

/* Overloads of imath functions that take their inputs as const */

inline const mpz_t *MP_NUMER_P(const mpq_t *q) {
	return &q->num;
}

inline const mpz_t *MP_DENOM_P(const mpq_t *q) {
	return &q->den;
}

inline mp_result mp_rat_init_copy(mp_rat r, const mpq_t *old) {
	return mp_rat_init_copy(r, const_cast<mp_rat>(old));
}

inline mp_result mp_rat_numer(const mpq_t *r, mp_int z) {
	return mp_rat_numer(const_cast<mp_rat>(r), z);
}

inline mp_result mp_rat_denom(const mpq_t *r, mp_int z) {
	return mp_rat_denom(const_cast<mp_rat>(r), z);
}

inline mp_sign mp_rat_sign(const mpq_t *r) {
	return mp_rat_sign(const_cast<mp_rat>(r));
}

inline mp_result mp_rat_copy(const mpq_t *a, mp_rat c) {
	return mp_rat_copy(const_cast<mp_rat>(a), c);
}

inline mp_result mp_rat_abs(const mpq_t *a, mp_rat c) {
	return mp_rat_abs(const_cast<mp_rat>(a), c);
}

inline mp_result mp_rat_neg(const mpq_t *a, mp_rat c) {
	return mp_rat_neg(const_cast<mp_rat>(a), c);
}

inline mp_result mp_rat_recip(const mpq_t *a, mp_rat c) {
	return mp_rat_recip(const_cast<mp_rat>(a), c);
}

inline mp_result mp_rat_add(const mpq_t *a, const mpq_t *b, mp_rat c) {
	return mp_rat_add(const_cast<mp_rat>(a), const_cast<mp_rat>(b), c);
}

inline mp_result mp_rat_sub(const mpq_t *a, const mpq_t *b, mp_rat c) {
	return mp_rat_sub(const_cast<mp_rat>(a), const_cast<mp_rat>(b), c);
}

inline mp_result mp_rat_mul(const mpq_t *a, const mpq_t *b, mp_rat c) {
	return mp_rat_mul(const_cast<mp_rat>(a), const_cast<mp_rat>(b), c);
}

inline mp_result mp_rat_div(const mpq_t *a, const mpq_t *b, mp_rat c) {
	return mp_rat_div(const_cast<mp_rat>(a), const_cast<mp_rat>(b), c);
}

inline mp_result mp_rat_add_int(const mpq_t *a, const mpz_t *b, mp_rat c) {
	return mp_rat_add_int(const_cast<mp_rat>(a), const_cast<mp_int>(b), c);
}

inline mp_result mp_rat_sub_int(const mpq_t *a, const mpz_t *b, mp_rat c) {
	return mp_rat_sub_int(const_cast<mp_rat>(a), const_cast<mp_int>(b), c);
}

inline mp_result mp_rat_mul_int(const mpq_t *a, const mpz_t *b, mp_rat c) {
	return mp_rat_mul_int(const_cast<mp_rat>(a), const_cast<mp_int>(b), c);
}

inline mp_result mp_rat_div_int(const mpq_t *a, const mpz_t *b, mp_rat c) {
	return mp_rat_div_int(const_cast<mp_rat>(a), const_cast<mp_int>(b), c);
}

inline mp_result mp_rat_expt(const mpq_t *a, mp_small b, mp_rat c) {
	return mp_rat_expt(const_cast<mp_rat>(a), b, c);
}

inline int mp_rat_compare(const mpq_t *a, const mpq_t *b) {
	return mp_rat_compare(const_cast<mp_rat>(a), const_cast<mp_rat>(b));
}

inline int mp_rat_compare_unsigned(const mpq_t *a, const mpq_t *b) {
	return mp_rat_compare_unsigned(const_cast<mp_rat>(a), const_cast<mp_rat>(b));
}

inline int mp_rat_compare_zero(const mpq_t *r) {
	return mp_rat_compare_zero(const_cast<mp_rat>(r));
}

inline int mp_rat_compare_value(const mpq_t *r, mp_small n, mp_small d) {
	return mp_rat_compare_value(const_cast<mp_rat>(r), n, d);
}

inline bool mp_rat_is_integer(const mpq_t *r) {
	return mp_rat_is_integer(const_cast<mp_rat>(r));
}

inline mp_result mp_rat_to_ints(const mpq_t *r, mp_small *num, mp_small *den) {
	return mp_rat_to_ints(const_cast<mp_rat>(r), num, den);
}

inline mp_result mp_rat_to_string(const mpq_t *r, mp_size radix, char *str, int limit) {
	return mp_rat_to_string(const_cast<mp_rat>(r), radix, str, limit);
}

inline mp_result mp_rat_to_decimal(
	const mpq_t *r,
	mp_size radix,
	mp_size prec,
	mp_round_mode round,
	char *str,
	int limit
) {
	return mp_rat_to_decimal(const_cast<mp_rat>(r), radix, prec, round, str, limit);
}

inline mp_size mp_rat_string_len(const mpq_t *r, mp_size radix) {
	return mp_rat_string_len(const_cast<mp_rat>(r), radix);
}

inline mp_size mp_rat_decimal_len(const mpq_t *r, mp_size radix, mp_size prec) {
	return mp_rat_decimal_len(const_cast<mp_rat>(r), radix, prec);
}

inline mp_result mp_int_init_copy(mp_int z, const mpz_t *old) {
	return mp_int_init_copy(z, const_cast<mp_int>(old));
}

inline mp_result mp_int_copy(const mpz_t *a, mp_int c) {
	return mp_int_copy(const_cast<mp_int>(a), c);
}

inline mp_result mp_int_abs(const mpz_t *a, mp_int c) {
	return mp_int_abs(const_cast<mp_int>(a), c);
}

inline mp_result mp_int_neg(const mpz_t *a, mp_int c) {
	return mp_int_neg(const_cast<mp_int>(a), c);
}

inline mp_result mp_int_add(const mpz_t *a, const mpz_t *b, mp_int c) {
	return mp_int_add(const_cast<mp_int>(a), const_cast<mp_int>(b), c);
}

inline mp_result mp_int_sub(const mpz_t *a, const mpz_t *b, mp_int c) {
	return mp_int_sub(const_cast<mp_int>(a), const_cast<mp_int>(b), c);
}

inline mp_result mp_int_mul(const mpz_t *a, const mpz_t *b, mp_int c) {
	return mp_int_mul(const_cast<mp_int>(a), const_cast<mp_int>(b), c);
}

inline mp_result mp_int_mod(const mpz_t *a, const mpz_t *b, mp_int c) {
	return mp_int_mod(const_cast<mp_int>(a), const_cast<mp_int>(b), c);
}

inline mp_result mp_int_expt_full(const mpz_t *a, const mpz_t *b, mp_int c) {
	return mp_int_expt_full(const_cast<mp_int>(a), const_cast<mp_int>(b), c);
}

inline mp_result mp_int_gcd(const mpz_t *a, const mpz_t *b, mp_int c) {
	return mp_int_gcd(const_cast<mp_int>(a), const_cast<mp_int>(b), c);
}

inline mp_result mp_int_lcm(const mpz_t *a, const mpz_t *b, mp_int c) {
	return mp_int_lcm(const_cast<mp_int>(a), const_cast<mp_int>(b), c);
}

inline mp_result mp_int_add_value(const mpz_t *a, mp_small value, mp_int c) {
	return mp_int_add_value(const_cast<mp_int>(a), value, c);
}

inline mp_result mp_int_sub_value(const mpz_t *a, mp_small value, mp_int c) {
	return mp_int_sub_value(const_cast<mp_int>(a), value, c);
}

inline mp_result mp_int_mul_value(const mpz_t *a, mp_small value, mp_int c) {
	return mp_int_mul_value(const_cast<mp_int>(a), value, c);
}

inline mp_result mp_int_sqr(const mpz_t *a, mp_int c) {
	return mp_int_sqr(const_cast<mp_int>(a), c);
}

inline mp_result mp_int_div(const mpz_t *a, const mpz_t *b, mp_int q, mp_int r) {
	return mp_int_div(const_cast<mp_int>(a), const_cast<mp_int>(b), q, r);
}

inline mp_result mp_int_div_value(const mpz_t *a, mp_small value, mp_int q, mp_small *r) {
	return mp_int_div_value(const_cast<mp_int>(a), value, q, r);
}

inline mp_result mp_int_expt(const mpz_t *a, mp_small b, mp_int c) {
	return mp_int_expt(const_cast<mp_int>(a), b, c);
}

inline mp_result mp_int_root(const mpz_t *a, mp_small b, mp_int c) {
	return mp_int_root(const_cast<mp_int>(a), b, c);
}

inline int mp_int_compare(const mpz_t *a, const mpz_t *b) {
	return mp_int_compare(const_cast<mp_int>(a), const_cast<mp_int>(b));
}

inline int mp_int_compare_unsigned(const mpz_t *a, const mpz_t *b) {
	return mp_int_compare_unsigned(const_cast<mp_int>(a), const_cast<mp_int>(b));
}

inline int mp_int_compare_zero(const mpz_t *z) {
	return mp_int_compare_zero(const_cast<mp_int>(z));
}

inline int mp_int_compare_value(const mpz_t *z, mp_small v) {
	return mp_int_compare_value(const_cast<mp_int>(z), v);
}

inline bool mp_int_divisible_value(const mpz_t *a, mp_small v) {
	return mp_int_divisible_value(const_cast<mp_int>(a), v);
}

inline mp_result mp_int_to_int(const mpz_t *z, mp_small *out) {
	return mp_int_to_int(const_cast<mp_int>(z), out);
}

inline mp_result mp_int_to_string(const mpz_t *z, mp_size radix, char *str, int limit) {
	return mp_int_to_string(const_cast<mp_int>(z), radix, str, limit);
}

inline mp_size mp_int_string_len(const mpz_t *z, mp_size radix) {
	return mp_int_string_len(const_cast<mp_int>(z), radix);
}

inline bool mp_int_is_odd(const mpz_t *z) {
	return mp_int_is_odd(const_cast<mp_int>(z));
}

inline bool mp_int_is_even(const mpz_t *z) {
	return mp_int_is_even(const_cast<mp_int>(z));
}
