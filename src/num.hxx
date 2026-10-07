#pragma once

#include "imath.hxx"

#include <concepts>
#include <cstddef>
#include <cstdlib>

/* An exact rational number, usable anywhere imath takes an mp_rat */
class num : public mpq_t {
  public:
	static void *operator new(size_t size) {
		return malloc(size);
	}

	static void operator delete(void *n) {
		free(n);
	}

	/* Allocates a number, which dispose frees */
	static num *from(mp_small value);
	static num *from(mp_small numer, mp_small denom);
	/* Expects a null terminated decimal string */
	template <std::same_as<char> C> static num *from(const C *decimal) {
		return parse(decimal);
	}
	/* Does nothing if n is null */
	static void dispose(num *n);

	/* Negative, zero or positive as a is less than, equal to or greater than b */
	static int compare(const num &a, const num &b);
	static int compare(const num &a, mp_small b);

	explicit num(mp_small value = 0);
	num(mp_small numer, mp_small denom);
	num(const num &other);
	num(num &&other) noexcept;
	num &operator=(const num &other);
	num &operator=(num &&other) noexcept;
	~num();

	num *copy() const;
	bool isInteger() const;
	/* False if the number is not an integer or does not fit in value */
	bool toInt(mp_small &value) const;
	/* The caller frees the string, which is nullptr on failure */
	char *toString(mp_size precision) const;

	num &operator+=(const num &other);
	num &operator-=(const num &other);
	num &operator*=(const num &other);
	num &operator/=(const num &other);
	num operator-() const;

	friend num operator+(const num &a, const num &b);
	friend num operator-(const num &a, const num &b);
	friend num operator*(const num &a, const num &b);
	friend num operator/(const num &a, const num &b);

	friend bool operator==(const num &a, const num &b) {
		return compare(a, b) == 0;
	}

	friend bool operator<(const num &a, const num &b) {
		return compare(a, b) < 0;
	}

	friend bool operator<=(const num &a, const num &b) {
		return compare(a, b) <= 0;
	}

	friend bool operator>(const num &a, const num &b) {
		return compare(a, b) > 0;
	}

	friend bool operator>=(const num &a, const num &b) {
		return compare(a, b) >= 0;
	}

	friend bool operator==(const num &a, mp_small b) {
		return compare(a, b) == 0;
	}

	friend bool operator<(const num &a, mp_small b) {
		return compare(a, b) < 0;
	}

	friend bool operator<=(const num &a, mp_small b) {
		return compare(a, b) <= 0;
	}

	friend bool operator>(const num &a, mp_small b) {
		return compare(a, b) > 0;
	}

	friend bool operator>=(const num &a, mp_small b) {
		return compare(a, b) >= 0;
	}

  private:
	static num *parse(const char *decimal);
};
