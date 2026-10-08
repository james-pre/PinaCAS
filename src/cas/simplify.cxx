#include "cas.hxx"

#include <utility>

#include "../work.hxx"
#include "identities.hxx"

/*Executes the Simp::Commutative flag*/
static bool simplify_commutative(ast &e) {
	if (!e.isOperator())
		return false;

	if (e.childCount() == 1 && is_op_commutative(e.op())) {
		e.replace(e.firstChild());
		simplify_commutative(e);
		return true;
	}

	bool changed = false;

	for (ast *child = e.firstChild(); child != nullptr;) {
		/*Flatten a child with the same commutative operator by moving its children to the end*/
		if (is_op_commutative(e.op()) && child->isOp(e.op())) {
			e.takeChildren(*child);
			ast *next = child->next();
			ast::dispose(e.removeChild(*child));
			child = next;
			changed = true;
		} else {
			changed |= simplify_commutative(*child);
			child = child->next();
		}
	}

	return changed;
}

/*Executes the Simp::Rational flag*/
static bool simplify_rational(ast &e) {
	if (!e.isOperator())
		return false;

	bool changed = false;
	unsigned i = 0;

	/*A rewrite changes e, so the child at the same index is checked again*/
	while (i < e.childCount()) {
		ast &child = *e.childAt(i);

		if (!child.isOp(Op::Div) || (!e.isOp(Op::Div) && !e.isOp(Op::Mult))) {
			changed |= simplify_rational(child);
			i++;
			continue;
		}

		const ast &child_num = *child.childAt(0);
		const ast &child_den = *child_num.next();

		if (e.isOp(Op::Div) && i == 0) {
			/*(A/B)/C = A/(BC)*/
			const ast &e_den = *child.next();
			e.replace(ast::make(Op::Div, child_num.copy(), ast::make(Op::Mult, child_den.copy(), e_den.copy())));
		} else if (e.isOp(Op::Div)) {
			/*A/(B/C) = (AC)/B*/
			const ast &e_num = *e.firstChild();
			e.replace(ast::make(Op::Div, ast::make(Op::Mult, e_num.copy(), child_den.copy()), child_num.copy()));
		} else {
			/*A(B/C) = (AB)/C*/
			ast *product = e.copy();
			ast *den = child_den.copy();

			product->appendChild(child_num.copy());
			ast::dispose(product->removeChildAt(i));

			e.replace(ast::make(Op::Div, product, den));

			simplify_rational(*product);
			simplify_rational(*den);
		}

		changed = true;
	}

	return changed;
}

/*Executes the Simp::Normalize flag*/
static bool simplify_normalize(ast &e) {
	bool changed = false;

	if (e.isOperator()) {
		for (ast &child : e.children())
			changed |= simplify_normalize(child);

		if (e.isOp(Op::Root)) {
			/*The root with index a of b is b^(1/a)*/
			ast *a = e.childAt(0)->copy();
			ast *b = e.childAt(1)->copy();

			e.replace(ast::make(Op::Pow, b, ast::make(Op::Div, ast::make(num::from(1)), a)));

			changed = true;
		}
	} else if (e.isNumber() && !e.num().isInteger()) {
		num *numer = num::from(1);
		num *denom = num::from(1);

		mp_rat_reduce(&e.num());
		mp_int_copy(MP_NUMER_P(&e.num()), MP_NUMER_P(numer));
		mp_int_copy(MP_DENOM_P(&e.num()), MP_NUMER_P(denom));

		e.replace(ast::make(Op::Div, ast::make(numer), ast::make(denom)));

		changed = true;
	}

	return changed;
}

bool has_imaginary_node(const ast &e) {
	if (e.isSymbol())
		return e.symbol() == Sym::Imag;

	for (const ast &child : e.children()) {
		if (has_imaginary_node(child))
			return true;
	}

	return false;
}

bool contains_symbol(const ast &e, Sym symbol) {
	if (e.isSymbol())
		return e.symbol() == symbol;

	for (const ast &child : e.children()) {
		if (contains_symbol(child, symbol))
			return true;
	}

	return false;
}

static const ast *reserved = nullptr;

void fresh_Reserve(const ast *e) {
	reserved = e;
}

Sym fresh_symbol(const ast &e) {
	for (const char *candidate = "UVWTSRQPNMKJHGFDCBA"; *candidate != '\0'; candidate++) {
		const Sym symbol = static_cast<Sym>(*candidate);

		if (!contains_symbol(e, symbol) && (reserved == nullptr || !contains_symbol(*reserved, symbol)))
			return symbol;
	}

	return Sym::Invalid;
}

unsigned node_count(const ast &e) {
	unsigned count = 1;

	for (const ast &child : e.children())
		count += node_count(child);

	return count;
}

Sym constant_symbol(const ast &e) {
	return !contains_symbol(e, Sym::C) ? Sym::C : !contains_symbol(e, Sym::K) ? Sym::K : fresh_symbol(e);
}

/*Expects everything to be completely simplified*/
bool is_negative_for_sure(const ast &a) {
	if (a.isNumber())
		return a.num() < 0;

	if (a.isOp(Op::Mult)) {
		for (const ast &child : a.children()) {
			if (is_negative_for_sure(child))
				return true;
		}
	} else if (a.isOp(Op::Div)) {
		return is_negative_for_sure(*a.childAt(0)) || is_negative_for_sure(*a.childAt(1));
	}

	return false;
}

/*Returns true if changed. Expects completely simplified.*/
bool absolute_val(ast &e) {
	if (e.isNumber() && e.num() < 0) {
		mp_rat_abs(&e.num(), &e.num());
		return true;
	}

	if (e.isOp(Op::Mult)) {
		for (ast &child : e.children()) {
			if (absolute_val(child))
				return true;
		}
	} else if (e.isOp(Op::Div)) {
		return absolute_val(*e.childAt(0)) || absolute_val(*e.childAt(1));
	}

	return false;
}

static Sym function_symbol = Sym::Invalid;

void canonical_SetFunction(Sym symbol) {
	function_symbol = symbol;
}

static ast *series_base = nullptr;

void canonical_SetSeries(const ast *base) {
	ast::dispose(series_base);
	series_base = base != nullptr ? base->copy() : nullptr;
}

/*Returns the exponent of e when it is a positive integer below limit, otherwise 0*/
static int small_exponent(const ast &e, mp_small limit) {
	const ast &power = *e.childAt(1);
	mp_small k;

	if (!power.isNumber() || !power.num().toInt(k))
		return 0;

	return k > 0 && k < limit ? static_cast<int>(k) : 0;
}

static bool is_number_fraction(const ast &e) {
	return e.isOp(Op::Div) && e.childAt(0)->isNumber() && e.childAt(1)->isNumber();
}

/*Returns k if e is a number times the series base to the integer power k, 0 if e is a number, otherwise -1*/
static int series_degree(const ast &e) {
	if (series_base == nullptr)
		return -1;

	if (e.isNumber())
		return 0;

	if (e.compare(*series_base))
		return 1;

	if (e.isOp(Op::Pow) && e.firstChild()->compare(*series_base)) {
		const int k = small_exponent(e, 1000);
		if (k > 0)
			return k;
	}

	if (e.isOp(Op::Div))
		return e.childAt(1)->isNumber() ? series_degree(*e.firstChild()) : -1;

	if (e.isOp(Op::Mult)) {
		int degree = 0;

		for (const ast &child : e.children()) {
			if (child.isNumber() || is_number_fraction(child))
				continue;

			if (degree > 0)
				return -1;

			degree = series_degree(child);
			if (degree <= 0)
				return -1;
		}

		return degree;
	}

	return -1;
}

static bool ascending_series = false;

/*True if e is a sum with a term that is a number times a positive power of the series base*/
static bool is_series_sum(const ast &e) {
	if (!e.isOp(Op::Add))
		return false;

	for (const ast &child : e.children()) {
		if (series_degree(child) > 0)
			return true;
	}

	return false;
}

/*Orders factors of a product: numbers, symbols, sums, subscripts, then the rest*/
static int factor_class(const ast &e) {
	if (e.isNumber())
		return 0;
	if (e.isSymbol())
		return 1;
	if (e.isOp(Op::Add))
		return 2;
	if (e.isOp(Op::Subscript))
		return 3;
	return 4;
}

static bool is_function(const ast &e) {
	return e.isSymbol() && function_symbol != Sym::Invalid && e.symbol() == function_symbol;
}

/*0 if e does not involve the unknown function, otherwise one more than the highest derivative of it in e*/
static unsigned function_rank(const ast &e) {
	if (e.isSymbol())
		return is_function(e);

	if (e.isOp(Op::Prime)) {
		const unsigned rank = function_rank(*e.firstChild());
		return (rank == 0 ? 1 : rank) + 1;
	}

	unsigned rank = 0;

	for (const ast &child : e.children()) {
		const unsigned child_rank = function_rank(child);
		if (child_rank > rank)
			rank = child_rank;
	}

	return rank;
}

/*Returns k if e is a number times the unknown function to the positive integer power k, 0 if e is a number, otherwise -1*/
static int function_degree(const ast &e) {
	if (e.isNumber())
		return 0;

	if (is_function(e))
		return 1;

	if (e.isOp(Op::Pow) && is_function(*e.firstChild())) {
		const int k = small_exponent(e, DiffEq::max_order + 1);
		if (k > 0)
			return k;
	}

	if (e.isOp(Op::Mult) && e.childCount() == 2 && e.firstChild()->isNumber()) {
		const int degree = function_degree(*e.childAt(1));
		return degree > 0 ? degree : -1;
	}

	return -1;
}

/*Returns e if it is a symbol, the first symbol factor if it is a product, otherwise Sym::Invalid*/
static Sym leading_symbol(const ast &e) {
	if (e.isSymbol())
		return e.symbol();

	if (e.isOp(Op::Mult)) {
		for (const ast &child : e.children()) {
			if (child.isSymbol())
				return child.symbol();
		}
	}

	return Sym::Invalid;
}

static ast *factors_or_self(const ast &e) {
	return e.isOp(Op::Mult) ? e.copy() : ast::make(Op::Mult, e.copy());
}

/*Takes ownership of product. Returns its only factor, 1 if it has none, or product itself.*/
static ast *unwrap_product(ast *product) {
	switch (product->childCount()) {
		case 0: ast::dispose(product); return ast::make(num::from(1));
		case 1: {
			ast *only = product->removeChildAt(0);
			ast::dispose(product);
			return only;
		}
		default: return product;
	}
}

/*Splits e into a base and a numeric exponent, which is 1 unless e is a power of a number*/
static const ast *power_base(const ast &e, num &exponent) {
	if (e.isOp(Op::Pow) && e.childAt(1)->isNumber()) {
		exponent = e.childAt(1)->num();
		return e.firstChild();
	}

	exponent = num(1);
	return &e;
}

/*Replaces the factor at index i of product with base raised to exponent, or removes it if exponent is 0*/
static void set_power(ast &product, unsigned i, const ast &base, const num &exponent) {
	ast *replacement = nullptr;

	if (exponent == 1)
		replacement = base.copy();
	else if (exponent != 0)
		replacement = ast::make(Op::Pow, base.copy(), ast::make(exponent.copy()));

	ast::dispose(product.removeChildAt(i));
	if (replacement != nullptr)
		product.insertChild(replacement, i);
}

/*Divides the factors that a and b have in common out of both, where X^2 and X share X. Returns false and sets nothing if they share none.*/
static bool remove_common_factors(const ast &a, const ast &b, ast **rest_a, ast **rest_b) {
	ast *fa = factors_or_self(a), *fb = factors_or_self(b);
	bool removed = false;
	unsigned i = 0;

	while (i < fa->childCount()) {
		num exponent_a;
		const ast *base_a = power_base(*fa->childAt(i), exponent_a);
		bool factor_gone = false;

		for (unsigned j = 0; j < fb->childCount(); j++) {
			num exponent_b;
			const ast *base_b = power_base(*fb->childAt(j), exponent_b);

			if (base_a->compare(*base_b) && exponent_a > 0 && exponent_b > 0) {
				const num common = exponent_a < exponent_b ? exponent_a : exponent_b;
				ast *base = base_a->copy();

				exponent_a -= common;
				exponent_b -= common;
				factor_gone = exponent_a == 0;

				set_power(*fb, j, *base, exponent_b);
				set_power(*fa, i, *base, exponent_a);

				ast::dispose(base);
				removed = true;
				break;
			}
		}

		if (!factor_gone)
			i++;
	}

	if (!removed) {
		ast::dispose(fa);
		ast::dispose(fb);
		return false;
	}

	*rest_a = unwrap_product(fa);
	*rest_b = unwrap_product(fb);
	return true;
}

static bool is_constant_of_integration(const ast &e) {
	return e.isSymbol() && e.symbol() == Sym::C;
}

/*Returns negative if a sorts before b, 0 if they are equal and positive if a sorts after b*/
static int sort_order(const ast *a, const ast *b, bool add) {
	int multiplier = 1;
	const int rank_a = static_cast<int>(function_rank(*a)), rank_b = static_cast<int>(function_rank(*b));

	/*An added constant of integration goes last*/
	if (add && is_constant_of_integration(*a) != is_constant_of_integration(*b))
		return is_constant_of_integration(*a) ? 1 : -1;

	/*Highest derivatives first in sums, and the unknown function last in products*/
	if (rank_a != rank_b)
		return add ? rank_b - rank_a : rank_a - rank_b;

	if (!add && (a->isOp(Op::Subscript) || b->isOp(Op::Subscript)) && factor_class(*a) != factor_class(*b))
		return factor_class(*a) - factor_class(*b);

	if (add) {
		const int degree_a = function_degree(*a), degree_b = function_degree(*b);
		const int series_a = ascending_series ? series_degree(*a) : -1;
		const int series_b = ascending_series ? series_degree(*b) : -1;
		const bool imaginary_a = contains_symbol(*a, Sym::Imag), imaginary_b = contains_symbol(*b, Sym::Imag);
		const Sym symbol_a = leading_symbol(*a), symbol_b = leading_symbol(*b);

		/*Power series after the other terms, by ascending degree*/
		if ((series_a >= 0) != (series_b >= 0))
			return series_a >= 0 ? 1 : -1;
		if (series_a != series_b)
			return series_a - series_b;

		/*Polynomials in the unknown function by descending degree*/
		if (degree_a >= 0 && degree_b >= 0 && degree_a != degree_b)
			return degree_b - degree_a;

		/*Imaginary parts after real parts*/
		if (imaginary_a != imaginary_b)
			return imaginary_a ? 1 : -1;

		/*A symbol and a product alphabetically*/
		if (a->isSymbol() != b->isSymbol() && symbol_a != Sym::Invalid && symbol_b != Sym::Invalid &&
			symbol_a != symbol_b)
			return static_cast<int>(symbol_a) - static_cast<int>(symbol_b);
	}

	if (b->isOp(Op::Mult)) {
		std::swap(a, b);
		multiplier = -multiplier;
	}

	if (a->isOp(Op::Mult)) {
		ast *rest_a, *rest_b;

		if (remove_common_factors(*a, *b, &rest_a, &rest_b)) {
			const int order = sort_order(rest_a, rest_b, add);

			ast::dispose(rest_a);
			ast::dispose(rest_b);

			return multiplier * order;
		}
	}

	/*Compare power bases to sort alphabetically when multiplying, and by degree when adding*/
	if (b->isOp(Op::Pow)) {
		std::swap(a, b);
		multiplier = -multiplier;
	}

	if (a->isOp(Op::Pow) && b->isOp(Op::Pow)) {
		const int order =
			add ? -sort_order(a->childAt(1), b->childAt(1), add) : sort_order(a->childAt(0), b->childAt(0), add);
		return multiplier * order;
	}

	const bool negative_a = is_negative_for_sure(*a), negative_b = is_negative_for_sure(*b);

	if (negative_a && !negative_b)
		return multiplier * (add ? 1 : -1);
	if (negative_b && !negative_a)
		return multiplier * (add ? -1 : 1);

	/*Numbers, then symbols, then operators*/
	if (a->type() < b->type())
		return multiplier * (add ? 1 : -1);
	if (a->type() > b->type())
		return multiplier * (add ? -1 : 1);

	/*Don't deal with sorting multivariable polynomials*/
	if (a->isOp(Op::Pow))
		return 0;

	switch (a->type()) {
		case ast::Type::Number: return multiplier * num::compare(a->num(), b->num());
		case ast::Type::Symbol: return multiplier * (static_cast<int>(a->symbol()) - static_cast<int>(b->symbol()));
		case ast::Type::Operator: return multiplier * (static_cast<int>(a->op()) - static_cast<int>(b->op()));
	}

	return 0;
}

static bool is_exponential(const ast &e) {
	return e.isOp(Op::Pow) && e.firstChild()->isSymbol() && e.firstChild()->symbol() == Sym::Euler;
}

/*Moves powers of e out of the denominator of the division e, negating their exponents*/
static bool exponentials_to_numerator(ast &e) {
	const ast &den = *e.childAt(1);
	ast *moved = ast::make(Op::Mult), *rest;

	if (den.isOp(Op::Mult)) {
		rest = ast::make(Op::Mult);
		for (const ast &child : den.children())
			(is_exponential(child) ? moved : rest)->appendChild(child.copy());
	} else if (is_exponential(den)) {
		moved->appendChild(den.copy());
		rest = ast::make(num::from(1));
	} else {
		ast::dispose(moved);
		return false;
	}

	if (moved->childCount() == 0) {
		ast::dispose(moved);
		ast::dispose(rest);
		return false;
	}

	for (ast &child : moved->children()) {
		ast &exponent = *child.childAt(1);
		exponent.replace(ast::make(Op::Mult, ast::make(num::from(-1)), exponent.copy()));
		simplify(exponent, Simp::Normalize | Simp::Commutative | Simp::Eval);
	}

	moved->insertChild(e.firstChild()->copy(), 0);
	simplify(*rest, Simp::Commutative);
	simplify(*moved, Simp::Commutative);

	if (rest->isInt(1)) {
		ast::dispose(rest);
		e.replace(moved);
	} else {
		e.replace(ast::make(Op::Div, moved, rest));
	}

	return true;
}

/*Combines the first two powers in the product e with the same exponent, writing a^5b^5 as (ab)^5*/
static bool combine_powers(ast &e) {
	for (ast &a : e.children()) {
		if (!a.isOp(Op::Pow))
			continue;

		for (ast *b = a.next(); b != nullptr; b = b->next()) {
			if (!b->isOp(Op::Pow) || !a.childAt(1)->compare(*b->childAt(1)))
				continue;

			ast *base = ast::make(Op::Mult, a.childAt(0)->copy(), b->childAt(0)->copy());
			e.appendChild(ast::make(Op::Pow, base, a.childAt(1)->copy()));

			ast::dispose(e.removeChild(a));
			ast::dispose(e.removeChild(*b));

			simplify(e, Simp::Commutative);
			return true;
		}
	}

	return false;
}

/*Multiplies the numerator and denominator of the division e by a root in the denominator*/
static bool rationalize(ast &e) {
	ast &num = *e.childAt(0);
	ast &den = *e.childAt(1);
	const ast *root = nullptr;

	if (den.isOp(Op::Root)) {
		root = &den;
	} else if (den.isOp(Op::Mult)) {
		for (const ast &child : den.children()) {
			if (child.isOp(Op::Root)) {
				root = &child;
				break;
			}
		}
	}

	if (root == nullptr)
		return false;

	ast *factor = root->copy();
	num.replace(ast::make(Op::Mult, num.copy(), factor->copy()));
	den.replace(ast::make(Op::Mult, den.copy(), factor));

	simplify(num, Simp::Normalize | Simp::Commutative | Simp::Eval | Simp::Rational | Simp::LikeTerms);
	simplify(den, Simp::Normalize | Simp::Commutative | Simp::Eval | Simp::Rational | Simp::LikeTerms);

	return true;
}

/*Orders the terms of a sum or product by insertion sort*/
static bool sort_children(ast &e) {
	if (!e.isOp(Op::Mult) && !e.isOp(Op::Add))
		return false;

	const bool add = e.isOp(Op::Add);
	bool changed = false;

	ascending_series = is_series_sum(e);

	for (unsigned i = 0; i < e.childCount(); i++) {
		const ast *child = e.childAt(i);

		for (unsigned j = 0; j < i; j++) {
			if (sort_order(child, e.childAt(j), add) < 0) {
				e.insertChild(e.removeChildAt(i), j);
				changed = true;
			}
		}
	}

	ascending_series = false;

	return changed;
}

/*
    Order multiplication and division.
    Change XAZ to AXZ and 1+sin(X)ZA5 to 5AZsin(X)+1
    Rationalize denominators.
    Change powers to roots.
    Combine a^5b^5 to (ab)^5.

    Use only when exporting and do not plan to simplify again
    or execute any other function on ast. Messes up all other algorithms.

    Sorting for addition and multiplication is O(n^2) by insertion sort
*/
static bool _simplify_canonical_form(ast &e, Canonical flags) {
	bool changed = false, repeat;

	do {
		repeat = false;

		if (has(flags, Canonical::CombinePowers) && e.isOp(Op::Mult)) {
			while (combine_powers(e))
				repeat = true;
		}

		if (has(flags, Canonical::Exponentials) && e.isOp(Op::Div))
			repeat |= exponentials_to_numerator(e);

		if (has(flags, Canonical::PowersToRoots) && e.isOp(Op::Pow)) {
			const ast &base = *e.childAt(0);
			const ast &power = *e.childAt(1);

			/*Write X^(1/n) as the nth root of X*/
			if (power.isOp(Op::Div) && power.childAt(0)->isInt(1)) {
				e.replace(ast::make(Op::Root, power.childAt(1)->copy(), base.copy()));
				repeat = true;
			}
		}

		if (has(flags, Canonical::Rationalize) && e.isOp(Op::Div))
			repeat |= rationalize(e);

		if (has(flags, Canonical::Sort))
			repeat |= sort_children(e);

		changed |= repeat;

		for (ast &child : e.children()) {
			repeat |= _simplify_canonical_form(child, flags);
			changed |= repeat;
		}
	} while (repeat);

	return changed;
}

bool simplify_canonical_form(ast &e, Canonical flags) {
	work::pause();
	const bool changed = _simplify_canonical_form(e, flags);
	work::resume();
	return changed;
}

static const ast *base_of(const ast &e) {
	return e.isOp(Op::Pow) ? e.childAt(0) : &e;
}

/*Returns a copy of the exponent of e, which is 1 unless e is a power*/
static ast *exponent_of(const ast &e) {
	return e.isOp(Op::Pow) ? e.childAt(1)->copy() : ast::make(num::from(1));
}

/*
    Combine things like
    AA to A^(1+1)
    A^2AB to A^(2+1)B
*/
static bool simplify_like_terms_multiplication(ast &e) {
	bool changed = false;

	for (ast &child : e.children())
		changed |= simplify_like_terms_multiplication(child);

	if (!e.isOp(Op::Mult))
		return changed;

	unsigned i = 0;

	/*Combining removes the factor at i, so the factor that takes its place is checked next*/
	while (i < e.childCount()) {
		ast &a = *e.childAt(i);
		bool combined = false;

		for (ast *b = a.next(); b != nullptr; b = b->next()) {
			if (!base_of(a)->compare(*base_of(*b)))
				continue;

			e.appendChild(ast::make(Op::Pow, base_of(a)->copy(), ast::make(Op::Add, exponent_of(a), exponent_of(*b))));

			ast::dispose(e.removeChild(a));
			ast::dispose(e.removeChild(*b));

			combined = changed = true;
			break;
		}

		if (!combined)
			i++;
	}

	return changed;
}

/*
    Simplifies sin(9pi/4) to sin(pi/4)

    Works on sin, cos, and tan
*/
bool simplify_periodic(ast &e) {
	if (!e.isOp(Op::Sin) && !e.isOp(Op::Cos) && !e.isOp(Op::Tan))
		return false;

	ast *copy = ast::make(Op::Div, e.childAt(0)->copy(), ast::make(Sym::Pi));
	simplify(*copy, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval);

	const num *numer = nullptr, *denom = nullptr;

	if (copy->isNumber()) {
		numer = &copy->num();
	} else if (copy->isOp(Op::Div) && copy->childAt(0)->isNumber() && copy->childAt(1)->isNumber()) {
		numer = &copy->childAt(0)->num();
		denom = &copy->childAt(1)->num();
	} else {
		ast::dispose(copy);
		return false;
	}

	const bool is_negative = is_negative_for_sure(*copy);
	bool changed = false;

	mp_int a = mp_int_alloc();
	mp_int_init_copy(a, MP_NUMER_P(numer));
	mp_int b = mp_int_alloc();
	if (denom != nullptr)
		mp_int_init_copy(b, MP_NUMER_P(denom));
	else
		mp_int_init_value(b, 1);

	mp_int_abs(a, a);
	mp_int_abs(b, b);

	mp_int c = mp_int_alloc();
	mp_int_init(c);

	/*c = 2b for sin and cos, c = b for tan*/
	if (e.isOp(Op::Tan))
		mp_int_copy(b, c);
	else
		mp_int_mul_value(b, 2, c);

	mp_int d = mp_int_alloc();
	mp_int_init(d);

	/*d = a/c*/
	mp_int_div(a, c, d, nullptr);

	if (mp_int_compare_value(d, 1) >= 0) {
		/*a = a - cd*/
		mp_int_mul(c, d, c);
		mp_int_sub(a, c, a);

		if (is_negative)
			mp_int_neg(a, a);

		num *new_num = num::from(1);
		num *new_den = num::from(1);

		mp_int_copy(a, MP_NUMER_P(new_num));
		mp_int_copy(b, MP_NUMER_P(new_den));

		ast *fraction = ast::make(Op::Div, ast::make(new_num), ast::make(new_den));
		e.replace(ast::make(e.op(), ast::make(Op::Mult, ast::make(Sym::Pi), fraction)));

		changed = true;
	}

	mp_int_free(a);
	mp_int_free(b);
	mp_int_free(c);
	mp_int_free(d);

	ast::dispose(copy);

	return changed;
}

static bool simplify_identities(ast &e, Simp flags) {
	bool changed = false;

	if (has(flags, Simp::IdGeneral))
		changed |= id::executeTable(e, id::general, true);

	if (has(flags, Simp::IdComplex)) {
		while (id::executeTable(e, id::complex, true))
			changed = true;
	}

	if (has(flags, Simp::IdTrig)) {
		while (simplify_periodic(e))
			changed = true;
		changed |= id::executeTable(e, id::trig_identities, true);
	}

	if (has(flags, Simp::IdTrigConstants))
		changed |= id::executeTable(e, id::trig_constants, true);

	if (has(flags, Simp::IdTrigInvConstants))
		changed |= id::executeTable(e, id::trig_inv_constants, true);

	if (has(flags, Simp::IdHyperbolic))
		changed |= id::executeTable(e, id::hyperbolic, true);

	return changed;
}

/*
    Executes a collection of simplification functions.

    O(infinity - 1)

    Returns true if ast was changed
*/
static bool _simplify(ast &e, Simp flags) {
	bool changed = false, repeat;

	do {
		repeat = false;

		if (has(flags, Simp::Normalize)) {
			while (simplify_normalize(e))
				repeat = true;
		}
		if (has(flags, Simp::Commutative)) {
			while (simplify_commutative(e))
				repeat = true;
		}
		if (has(flags, Simp::Rational)) {
			while (simplify_rational(e))
				repeat = true;
		}
		if (has(flags, Simp::Eval)) {
			while (eval(e, Eval::Easy))
				repeat = true;
		}

		if (has(flags, Simp::Deriv))
			repeat |= eval_derivatives(e);

		if (has(flags, Simp::Integral))
			repeat |= eval_integrals(e);

		/*Simplify identities. First factor the expression and simplify identities.
        Then expand the expression and simplify identities that we missed. Only factor and expand if
        at least one id flag is set. The expression remains in the expanded state at the end of simplify().*/
		if (has(flags, Simp::IdAll)) {
			factor(e, Factor::SimpleAdditionEvaluateable | Factor::SimpleAdditionNonevaluateable);
			repeat |= simplify_identities(e, flags);

			const Expand distribute = Expand::DistribNumbers | Expand::DistribMultiplication | Expand::DistribDivision |
									  Expand::DistribPowers;

			if (expand(e, distribute)) {
				/*Expand leaves constants unevaluated and commutative operators with 1 or 0 children.*/
				simplify(e, Simp::Commutative | Simp::Eval | Simp::LikeTerms);

				repeat |= simplify_identities(e, flags);

				/*Fix the expansion of division here*/
				simplify(e, Simp::Commutative | Simp::Rational | Simp::Eval | Simp::LikeTerms);
			}
		}

		if (has(flags, Simp::LikeTerms)) {
			/*First fix 2A+B-(A+B) to 2A+B-A-B*/
			while (expand(e, Expand::DistribNumbers | Expand::DistribPowers))
				repeat = true;
			/*Then fix 2A+B-A-B to A(2+-1)+B(1+-1)*/
			while (factor(e, Factor::SimpleAdditionEvaluateable))
				repeat = true;
			/*This would fix AA to A^2 if it exists*/
			while (simplify_like_terms_multiplication(e))
				repeat = true;
		}

		changed |= repeat;
	} while (repeat);

	return changed;
}

bool simplify(ast &e, Simp flags) {
	work::enter(e);
	const bool changed = _simplify(e, flags);
	work::leave(e);
	return changed;
}
