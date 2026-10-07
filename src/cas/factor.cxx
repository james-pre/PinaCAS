#include "cas.hxx"

#include "../work.hxx"

/*Handles gcd for AB, BC gcd = B*/
static ast *gcd_mult(const ast &mult, const ast &b) {
	ast *copy = b.copy();

	ast *current_gcd = ast::make(Op::Mult);

	for (unsigned i = 0; i < mult.childCount(); i++) {
		const ast &child = *mult.childAt(i);

		ast *inner_gcd = gcd(child, *copy);

		current_gcd->appendChild(inner_gcd);

		copy->replace(ast::make(Op::Div, copy->copy(), inner_gcd->copy()));

		simplify(*copy, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval);
	}

	ast::dispose(copy);

	return current_gcd;
}

/*Handles gcd for A + AB, A gcd = A*/
static ast *gcd_add(const ast &add, const ast &b) {
	ast *current_gcd = gcd(*add.childAt(0), b);

	for (unsigned i = 1; i < add.childCount(); i++) {
		const ast &child = *add.childAt(i);
		ast *temp_gcd = gcd(*current_gcd, child);

		ast::dispose(current_gcd);
		current_gcd = temp_gcd;
	}

	return current_gcd;
}

static ast *gcd_div(const ast &div, const ast &b) {
	const ast &num1 = *div.childAt(0);
	const ast &den1 = *div.childAt(1);
	const ast *num2, *den2;
	ast *one = nullptr;

	if (b.isOp(Op::Div)) {
		num2 = b.childAt(0);
		den2 = b.childAt(1);
	} else {
		num2 = &b;
		den2 = one = ast::make(num::from(1));
	}

	ast *num_g = gcd(num1, *num2);
	ast *den_g = gcd(den1, *den2);

	ast::dispose(one);

	return ast::make(Op::Div, num_g, den_g);
}

/*gcd of both bases, raised to smallest power*/
static ast *gcd_pow(const ast &pow, const ast &b) {
	const ast &base1 = *pow.childAt(0);
	const ast &power1 = *pow.childAt(1);

	/*We purposefully ignore A^X, A^Y and only yield when
    we can determine the numerical powers*/

	if (b.isOp(Op::Pow)) {
		const ast &base2 = *b.childAt(0);
		const ast &power2 = *b.childAt(1);

		if (power1.isNumber() && power2.isNumber()) {
			const bool use_first = power1.num() < power2.num();

			ast *current_gcd = gcd(base1, base2);

			return ast::make(Op::Pow, current_gcd, use_first ? power1.copy() : power2.copy());
		}

	} else if (power1.isNumber() && power1.num() >= 1 && base1.compare(b)) {
		return b.copy();
	}

	return ast::make(num::from(1));
}

ast *gcd(const ast &a, const ast &b) {
	ast *ret = nullptr;

	if (a.compare(b))
		return a.copy();

	if (a.isNumber() && b.isNumber()) {
		if (a.num().isInteger() && b.num().isInteger()) {
			num *gcd = num::from(1);

			mp_int_gcd(MP_NUMER_P(&a.num()), MP_NUMER_P(&b.num()), MP_NUMER_P(gcd));

			return ast::make(gcd);
		}
	}

	if (a.isOp(Op::Mult))
		ret = gcd_mult(a, b);
	else if (b.isOp(Op::Mult))
		ret = gcd_mult(b, a);
	else if (a.isOp(Op::Add))
		ret = gcd_add(a, b);
	else if (b.isOp(Op::Add))
		ret = gcd_add(b, a);
	else if (a.isOp(Op::Pow))
		ret = gcd_pow(a, b);
	else if (b.isOp(Op::Pow))
		ret = gcd_pow(b, a);
	else if (a.isOp(Op::Div))
		ret = gcd_div(a, b);
	else if (b.isOp(Op::Div))
		ret = gcd_div(b, a);

	if (ret != nullptr) {
		/*Don't simplify identities*/
		simplify(*ret, Simp::All & ~Simp::IdAll);
		return ret;
	}

	return ast::make(num::from(1));
}

/*Collects the non-numeric factors of e, with divisors in den*/
static void collect_symbolic_factors(ast &e, ast &num, ast &den) {
	if (e.isOp(Op::Mult)) {
		for (ast &child : e.children())
			collect_symbolic_factors(child, num, den);
	} else if (e.isOp(Op::Div)) {
		collect_symbolic_factors(*e.firstChild(), num, den);
		collect_symbolic_factors(*e.firstChild()->next(), den, num);
	} else if (!e.isNumber()) {
		num.appendChild(e.copy());
	}
}

/*Takes ownership of neither. True if a and b have the same children in any order.*/
static bool same_children(ast &a, ast &b) {
	ast *remaining = b.copy();
	bool same = a.childCount() == b.childCount();

	for (const ast *child = a.firstChild(); child != nullptr && same; child = child->next()) {
		same = false;
		for (unsigned j = 0; j < remaining->childCount(); j++) {
			if (child->compare(*remaining->childAt(j))) {
				ast::dispose(remaining->removeChildAt(j));
				same = true;
				break;
			}
		}
	}

	ast::dispose(remaining);
	return same;
}

/*True if a and b differ only by numeric factors, so they can be like terms*/
static bool same_symbolic_part(ast &a, ast &b) {
	ast *a_num = ast::make(Op::Mult), *a_den = ast::make(Op::Mult);
	ast *b_num = ast::make(Op::Mult), *b_den = ast::make(Op::Mult);

	collect_symbolic_factors(a, *a_num, *a_den);
	collect_symbolic_factors(b, *b_num, *b_den);

	const bool same = same_children(*a_num, *b_num) && same_children(*a_den, *b_den);

	ast::dispose(a_num);
	ast::dispose(a_den);
	ast::dispose(b_num);
	ast::dispose(b_den);

	return same;
}

bool factor_addition(ast &e, Factor flags) {
	bool changed = false;

	if (!e.isOperator())
		return false;

	for (ast &child : e.children())
		changed |= factor_addition(child, flags);

	if (e.isOp(Op::Add)) {
		for (unsigned i = 0; i < e.childCount(); i++) {
			ast &a = *e.childAt(i);

			for (unsigned j = i + 1; j < e.childCount(); j++) {
				ast &b = *e.childAt(j);

				if (!has(flags, Factor::SimpleAdditionNonevaluateable) && !same_symbolic_part(a, b))
					continue;

				ast *g = gcd(a, b);

				if (!g->isInt(1)) {
					ast *first = ast::make(Op::Div, a.copy(), g->copy());
					ast *second = ast::make(Op::Div, b.copy(), g->copy());

					ast *append = ast::make(Op::Mult, g->copy(), ast::make(Op::Add, first, second));

					simplify(*first, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval);
					simplify(*second, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval);

					bool can_factor;
					if (first->isNumber() && second->isNumber())
						can_factor = has(flags, Factor::SimpleAdditionEvaluateable);
					else
						can_factor = has(flags, Factor::SimpleAdditionNonevaluateable);

					if (can_factor) {
						simplify(*append, Simp::Normalize | Simp::Rational | Simp::Eval);
						e.appendChild(append);

						ast::dispose(e.removeChild(a));
						ast::dispose(e.removeChild(b));

						ast::dispose(g);

						changed = true;
						break;
					} else {
						ast::dispose(append);
						ast::dispose(g);
					}

				} else {
					ast::dispose(g);
				}
			}
		}
	}

	/*Take care of add nodes with one child*/
	simplify(e, Simp::Commutative);

	return changed;
}

static bool _factor(ast &e, Factor flags) {
	bool changed = false;

	if (has(flags, (Factor::SimpleAdditionEvaluateable | Factor::SimpleAdditionNonevaluateable)))
		changed |= factor_addition(e, flags);
	/*Need to implement polynomial factoring*/
	return changed;
}

bool factor(ast &e, Factor flags) {
	work::enter(e);
	const bool changed = _factor(e, flags);
	work::leave(e);
	return changed;
}

void factor_cancel(ast &e) {
	ast *factored = e.copy();

	work::pause();
	factor(*factored, Factor::All);
	simplify(*factored, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval | Simp::LikeTerms);
	work::resume();

	if (node_count(*factored) < node_count(e))
		e.replace(factored);
	else
		ast::dispose(factored);
}
