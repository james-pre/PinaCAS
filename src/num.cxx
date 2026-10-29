#include "num.hxx"

constexpr mp_size radix = 10;

Num *Num::from(mp_small value) {
	return new Num(value);
}

Num *Num::from(mp_small numer, mp_small denom) {
	return new Num(numer, denom);
}

Num *Num::parse(const char *decimal) {
	Num *n = new Num();
	mp_rat_read_decimal(n, radix, decimal);
	return n;
}

void Num::dispose(Num *n) {
	delete n;
}

int Num::compare(const Num &a, const Num &b) {
	return mp_rat_compare(&a, &b);
}

int Num::compare(const Num &a, mp_small b) {
	return mp_rat_compare_value(&a, b, 1);
}

Num::Num(mp_small value) {
	mp_rat_init(this);
	mp_int_set_value(MP_NUMER_P(this), value);
}

Num::Num(mp_small numer, mp_small denom) {
	mp_rat_init(this);
	mp_rat_set_value(this, numer, denom);
}

Num::Num(const Num &other) {
	mp_rat_init_copy(this, &other);
}

Num::Num(Num &&other) noexcept {
	mp_rat_init(this);
	mp_int_swap(MP_NUMER_P(this), MP_NUMER_P(&other));
	mp_int_swap(MP_DENOM_P(this), MP_DENOM_P(&other));
}

Num &Num::operator=(const Num &other) {
	mp_rat_copy(&other, this);
	return *this;
}

Num &Num::operator=(Num &&other) noexcept {
	mp_int_swap(MP_NUMER_P(this), MP_NUMER_P(&other));
	mp_int_swap(MP_DENOM_P(this), MP_DENOM_P(&other));
	return *this;
}

Num::~Num() {
	mp_rat_clear(this);
}

Num *Num::copy() const {
	return new Num(*this);
}

bool Num::isInteger() const {
	return mp_rat_is_integer(this);
}

bool Num::toInt(mp_small &value) const {
	return isInteger() && mp_int_to_int(MP_NUMER_P(this), &value) == MP_OK;
}

char *Num::toString(mp_size precision) const {
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

Num &Num::operator+=(const Num &other) {
	mp_rat_add(this, &other, this);
	return *this;
}

Num &Num::operator-=(const Num &other) {
	mp_rat_sub(this, &other, this);
	return *this;
}

Num &Num::operator*=(const Num &other) {
	mp_rat_mul(this, &other, this);
	return *this;
}

Num &Num::operator/=(const Num &other) {
	mp_rat_div(this, &other, this);
	return *this;
}

Num Num::operator-() const {
	Num result;
	mp_rat_neg(this, &result);
	return result;
}

Num operator+(const Num &a, const Num &b) {
	Num result;
	mp_rat_add(&a, &b, &result);
	return result;
}

Num operator-(const Num &a, const Num &b) {
	Num result;
	mp_rat_sub(&a, &b, &result);
	return result;
}

Num operator*(const Num &a, const Num &b) {
	Num result;
	mp_rat_mul(&a, &b, &result);
	return result;
}

Num operator/(const Num &a, const Num &b) {
	Num result;
	mp_rat_div(&a, &b, &result);
	return result;
}
