#include "cas.hxx"

#include "../work.hxx"

/*Returns the sum of the products of each term of sum with each term of other, or with other if it is not a sum*/
static ast *combine(const ast &sum, const ast &other) {
	ast *expanded = ast::make(Op::Add);

	for (const ast &term : sum.children()) {
		if (other.isOp(Op::Add)) {
			for (const ast &other_term : other.children())
				expanded->appendChild(ast::make(Op::Mult, term.copy(), other_term.copy()));
		} else {
			expanded->appendChild(ast::make(Op::Mult, term.copy(), other.copy()));
		}
	}

	return expanded;
}

static bool should_distribute(const ast &factor, Expand flags) {
	if (factor.isNumber())
		return has(flags, Expand::DistribNumbers);
	if (factor.isOp(Op::Add))
		return has(flags, Expand::DistribAddition);
	return has(flags, Expand::DistribMultiplication);
}

/*Multiplies the first sum in the product e by another factor that flags allow*/
static bool distribute_product(ast &e, Expand flags) {
	for (ast &sum : e.children()) {
		if (!sum.isOp(Op::Add))
			continue;

		for (ast &factor : e.children()) {
			if (&factor == &sum || !should_distribute(factor, flags))
				continue;

			e.appendChild(combine(sum, factor));
			ast::dispose(e.removeChild(sum));
			ast::dispose(e.removeChild(factor));
			return true;
		}
	}

	return false;
}

/*Writes A/B as A(1/B)*/
static void split_division(ast &e) {
	const ast &num = *e.childAt(0);
	const ast &den = *num.next();

	e.replace(ast::make(Op::Mult, num.copy(), ast::make(Op::Div, ast::make(num::from(1)), den.copy())));
}

/*Writes (AB)^n as A^nB^n and (A/B)^n as A^n/B^n*/
static bool distribute_power(ast &e) {
	const ast &base = *e.childAt(0);
	const ast &power = *base.next();

	if (base.isOp(Op::Mult)) {
		ast *product = ast::make(Op::Mult);

		for (const ast &factor : base.children())
			product->appendChild(ast::make(Op::Pow, factor.copy(), power.copy()));

		e.replace(product);
		return true;
	}

	if (base.isOp(Op::Div)) {
		ast *num = ast::make(Op::Pow, base.childAt(0)->copy(), power.copy());
		ast *den = ast::make(Op::Pow, base.childAt(1)->copy(), power.copy());

		e.replace(ast::make(Op::Div, num, den));
		return true;
	}

	return false;
}

/*Writes (A+B)^n with a positive integer n as n factors of A+B*/
static bool expand_power(ast &e) {
	const ast &base = *e.childAt(0);
	const ast &power = *base.next();

	mp_small count;
	if (!base.isOp(Op::Add) || !power.isNumber() || !power.num().toInt(count) || count <= 0)
		return false;

	ast *product = ast::make(Op::Mult);
	while (count-- > 0)
		product->appendChild(base.copy());

	e.replace(product);
	return true;
}

static bool _expand(ast &e, Expand flags) {
	if (e.isSymbol() || (e.isNumber() && e.num().isInteger()))
		return false;

	bool changed = false;

	for (ast &child : e.children())
		changed |= _expand(child, flags);

	bool repeat;

	do {
		simplify(e, Simp::Commutative);

		if (e.isOp(Op::Mult)) {
			repeat = distribute_product(e, flags);
		} else if (e.isOp(Op::Div) && has(flags, Expand::DistribDivision)) {
			split_division(e);
			_expand(
				e,
				Expand::DistribNumbers | Expand::DistribAddition | Expand::DistribMultiplication | Expand::DistribPowers
			);
			repeat = true;
		} else if (e.isOp(Op::Pow)) {
			repeat = (has(flags, Expand::DistribPowers) && distribute_power(e)) ||
					 (has(flags, Expand::Powers) && expand_power(e));
		} else {
			repeat = false;
		}

		changed |= repeat;
	} while (repeat);

	simplify(e, Simp::Normalize | Simp::Commutative);

	return changed;
}

bool expand(ast &e, Expand flags) {
	bool changed = false;

	work::enter(e);
	/*Expand powers first to make things faster*/
	if (has(flags, Expand::Powers))
		changed = _expand(e, Expand::Powers);
	changed |= _expand(e, flags & ~Expand::Powers);
	work::leave(e);

	return changed;
}
