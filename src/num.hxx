#pragma once

#include "imath.hxx"

#include <concepts>
#include <cstddef>
#include <cstdlib>

/* An exact rational number, usable anywhere imath takes an mp_rat */
class Num : public mpq_t {
  public:
	static void *operator new(size_t size) {
		return malloc(size);
	}

	static void operator delete(void *n) {
		free(n);
	}

	/* Allocates a number, which dispose frees */
	static Num *from(mp_small value);
	static Num *from(mp_small numer, mp_small denom);
	/* Expects a null terminated decimal string */
	template <std::same_as<char> C> static Num *from(const C *decimal) {
		return parse(decimal);
	}
	/* Does nothing if n is null */
	static void dispose(Num *n);

	/* Negative, zero or positive as a is less than, equal to or greater than b */
	static int compare(const Num &a, const Num &b);
	static int compare(const Num &a, mp_small b);

	explicit Num(mp_small value = 0);
	Num(mp_small numer, mp_small denom);
	Num(const Num &other);
	Num(Num &&other) noexcept;
	Num &operator=(const Num &other);
	Num &operator=(Num &&other) noexcept;
	~Num();

	Num *copy() const;
	bool isInteger() const;
	/* False if the number is not an integer or does not fit in value */
	bool toInt(mp_small &value) const;
	/* The caller frees the string, which is nullptr on failure */
	char *toString(mp_size precision) const;

	Num &operator+=(const Num &other);
	Num &operator-=(const Num &other);
	Num &operator*=(const Num &other);
	Num &operator/=(const Num &other);
	Num operator-() const;

	friend Num operator+(const Num &a, const Num &b);
	friend Num operator-(const Num &a, const Num &b);
	friend Num operator*(const Num &a, const Num &b);
	friend Num operator/(const Num &a, const Num &b);

	friend bool operator==(const Num &a, const Num &b) {
		return compare(a, b) == 0;
	}

	friend bool operator<(const Num &a, const Num &b) {
		return compare(a, b) < 0;
	}

	friend bool operator<=(const Num &a, const Num &b) {
		return compare(a, b) <= 0;
	}

	friend bool operator>(const Num &a, const Num &b) {
		return compare(a, b) > 0;
	}

	friend bool operator>=(const Num &a, const Num &b) {
		return compare(a, b) >= 0;
	}

	friend bool operator==(const Num &a, mp_small b) {
		return compare(a, b) == 0;
	}

	friend bool operator<(const Num &a, mp_small b) {
		return compare(a, b) < 0;
	}

	friend bool operator<=(const Num &a, mp_small b) {
		return compare(a, b) <= 0;
	}

	friend bool operator>(const Num &a, mp_small b) {
		return compare(a, b) > 0;
	}

	friend bool operator>=(const Num &a, mp_small b) {
		return compare(a, b) >= 0;
	}

  private:
	static Num *parse(const char *decimal);
};
