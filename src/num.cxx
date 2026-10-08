#include "num.hxx"

constexpr mp_size radix = 10;

num *num::from(mp_small value) {
	return new num(value);
}

num *num::from(mp_small numer, mp_small denom) {
	return new num(numer, denom);
}

num *num::parse(const char *decimal) {
	num *n = new num();
	mp_rat_read_decimal(n, radix, decimal);
	return n;
}

void num::dispose(num *n) {
	delete n;
}

int num::compare(const num &a, const num &b) {
	return mp_rat_compare(&a, &b);
}

int num::compare(const num &a, mp_small b) {
	return mp_rat_compare_value(&a, b, 1);
}

num::num(mp_small value) {
	mp_rat_init(this);
	mp_int_set_value(MP_NUMER_P(this), value);
}

num::num(mp_small numer, mp_small denom) {
	mp_rat_init(this);
	mp_rat_set_value(this, numer, denom);
}

num::num(const num &other) {
	mp_rat_init_copy(this, &other);
}

num::num(num &&other) noexcept {
	mp_rat_init(this);
	mp_int_swap(MP_NUMER_P(this), MP_NUMER_P(&other));
	mp_int_swap(MP_DENOM_P(this), MP_DENOM_P(&other));
}

num &num::operator=(const num &other) {
	mp_rat_copy(&other, this);
	return *this;
}

num &num::operator=(num &&other) noexcept {
	mp_int_swap(MP_NUMER_P(this), MP_NUMER_P(&other));
	mp_int_swap(MP_DENOM_P(this), MP_DENOM_P(&other));
	return *this;
}

num::~num() {
	mp_rat_clear(this);
}

num *num::copy() const {
	return new num(*this);
}

bool num::isInteger() const {
	return mp_rat_is_integer(this);
}

bool num::toInt(mp_small &value) const {
	return isInteger() && mp_int_to_int(MP_NUMER_P(this), &value) == MP_OK;
}

char *num::toString(mp_size precision) const {
	char *str;

	if (isInteger()) {
		const mp_size len = mp_int_string_len(MP_NUMER_P(this), radix);
		str = static_cast<char *>(malloc(len * sizeof(char)));
		if (mp_int_to_string(MP_NUMER_P(this), radix, str, static_cast<int>(len)) != MP_OK) {
			free(str);
			return nullptr;
		}
	} else {
		const mp_size len = mp_rat_decimal_len(this, radix, precision);
		str = static_cast<char *>(malloc(len * sizeof(char)));
		if (mp_rat_to_decimal(this, radix, precision, MP_ROUND_HALF_UP, str, static_cast<int>(len)) != MP_OK) {
			free(str);
			return nullptr;
		}
	}

	return str;
}

num &num::operator+=(const num &other) {
	mp_rat_add(this, &other, this);
	return *this;
}

num &num::operator-=(const num &other) {
	mp_rat_sub(this, &other, this);
	return *this;
}

num &num::operator*=(const num &other) {
	mp_rat_mul(this, &other, this);
	return *this;
}

num &num::operator/=(const num &other) {
	mp_rat_div(this, &other, this);
	return *this;
}

num num::operator-() const {
	num result;
	mp_rat_neg(this, &result);
	return result;
}

num operator+(const num &a, const num &b) {
	num result;
	mp_rat_add(&a, &b, &result);
	return result;
}

num operator-(const num &a, const num &b) {
	num result;
	mp_rat_sub(&a, &b, &result);
	return result;
}

num operator*(const num &a, const num &b) {
	num result;
	mp_rat_mul(&a, &b, &result);
	return result;
}

num operator/(const num &a, const num &b) {
	num result;
	mp_rat_div(&a, &b, &result);
	return result;
}
