#include "cas.hxx"

#include "../work.hxx"

static bool eval_commutative(ast &e, Eval flags) {
	/*How many numbers were accumulated. If <= 1, nothing changed*/
	unsigned num_changed = false;

	if (!has(flags, Eval::Commutative))
		return false;

	{
		unsigned numbers = 0, fractions = 0;
		const ast *first = e.firstChild();

		for (const ast *child = first; child != nullptr; child = child->next()) {
			if (child->isNumber())
				numbers++;
			else if (
				e.isOp(Op::Add) && child->isOp(Op::Div) && child->firstChild()->isNumber() &&
				child->firstChild()->next()->isNumber()
			)
				fractions++;
		}

		/*A lone fraction is already normalized, so it is only combined with other numbers*/
		if (first != nullptr && numbers == 0 && fractions <= 1)
			return false;

		/*A lone number that is already first and is not an identity element stays as it is*/
		if (numbers == 1 && fractions == 0 && first->isNumber() && first->next() != nullptr && first->num() != 0 &&
			!(e.op() == Op::Mult && first->isInt(1)))
			return false;
	}

	num *accumulator = num::from(e.op() == Op::Mult ? 1 : 0);

	for (unsigned i = 0; i < e.childCount(); i++) {
		const ast &child = *e.childAt(i);

		if (child.isNumber()) {
			if (e.op() == Op::Mult)
				*accumulator *= child.num();
			else
				*accumulator += child.num();

			ast::dispose(e.removeChildAt(i));
			i--;
			num_changed++;
		} else if (e.isOp(Op::Add) && child.isOp(Op::Div)) {
			const ast &c_num = *child.childAt(0);
			const ast &c_den = *child.childAt(1);

			if (c_num.isNumber() && c_den.isNumber()) {
				*accumulator += c_num.num() / c_den.num();

				ast::dispose(e.removeChildAt(i));

				i--;
				num_changed++;
			}
		}
	}

	/*If all children nodes were numbers or if multiplied by -*/
	if (e.childCount() == 0 || (e.op() == Op::Mult && *accumulator == 0)) {
		e.replace(ast::make(accumulator));
		return true;
	}
	/*Do not append accumulator if multiplying by 1 or adding 0*/
	else if (!((e.op() == Op::Add && *accumulator == 0) || (e.op() == Op::Mult && *accumulator == 1))) {
		e.insertChild(ast::make(accumulator), 0);
	} else
		num::dispose(accumulator);

	return num_changed > 1;
}

static bool eval_div(ast &e, Eval flags);

static bool eval_div_mult(ast &numer, ast &denom, Eval flags) {
	bool changed = false;

	if (numer.isOp(Op::Mult)) {
		for (unsigned i = 0; i < numer.childCount(); i++) {
			const ast &temp_num = *numer.childAt(i);

			if (denom.isOp(Op::Mult)) {
				for (unsigned j = 0; j < denom.childCount(); j++) {
					const ast &temp_den = *denom.childAt(j);

					ast *temp_div = ast::make(Op::Div, temp_num.copy(), temp_den.copy());

					if (eval_div(*temp_div, flags)) {
						ast::dispose(numer.removeChildAt(i));
						ast::dispose(denom.removeChildAt(j));

						numer.appendChild(temp_div);

						changed = true;

						j--;
						break;
					} else {
						ast::dispose(temp_div);
					}
				}
			} else {
				ast *temp_div = ast::make(Op::Div, temp_num.copy(), denom.copy());

				if (eval_div(*temp_div, flags)) {
					ast::dispose(numer.removeChildAt(i));
					denom.replace(ast::make(num::from(1)));

					numer.appendChild(temp_div);

					changed = true;
				} else {
					ast::dispose(temp_div);
				}
			}
		}

	} else if (denom.isOp(Op::Mult)) {
		for (unsigned j = 0; j < denom.childCount(); j++) {
			const ast &temp_den = *denom.childAt(j);

			ast *temp_div = ast::make(Op::Div, numer.copy(), temp_den.copy());

			if (eval_div(*temp_div, flags)) {
				numer.replace(temp_div);
				ast::dispose(denom.removeChildAt(j));

				changed = true;

				j--;
				break;
			} else {
				ast::dispose(temp_div);
			}
		}
	}

	return changed;
}

static bool eval_div(ast &e, Eval flags) {
	bool changed = false;

	ast &numer = *e.childAt(0);
	ast &denom = *e.childAt(1);

	if (has(flags, Eval::BasicIdentities)) {
		if (is_negative_for_sure(numer) && is_negative_for_sure(denom)) {
			absolute_val(numer);
			absolute_val(denom);

			ast *new_div = ast::make(Op::Div, numer.copy(), denom.copy());

			eval_div(*new_div, flags);
			e.replace(new_div);

			return true;
		}

		/*If numerator and denominator are equal, replace node with "1"*/
		if (numer.compare(denom)) {
			e.replace(ast::make(num::from(1)));
			return true;
		}

		if (numer.isNumber() && numer.num() == 0) {
			e.replace(ast::make(num::from(0)));
			return true;
		}

		if (denom.isNumber() && denom.num() == 1) {
			e.replace(e.firstChild());
			return true;
		}

		/*Move the sign of a negative denominator to the numerator*/
		if (denom.isNumber() && denom.num() < 0) {
			mp_rat_neg(&denom.num(), &denom.num());
			numer.replace(ast::make(Op::Mult, ast::make(num::from(-1)), numer.copy()));
			return true;
		}
	}

	if (!has(flags, Eval::Division))
		return false;

	if (numer.isOp(Op::Pow)) {
		const ast &base1 = *numer.childAt(0);
		ast &power1 = *numer.childAt(1);

		if (denom.isOp(Op::Pow)) {
			const ast &base2 = *denom.childAt(0);
			const ast &power2 = *denom.childAt(1);

			if (base1.compare(base2)) {
				/*Subtract powers*/
				power1.replace(
					ast::make(Op::Add, power1.copy(), ast::make(Op::Mult, ast::make(num::from(-1)), power2.copy()))
				);
				denom.replace(ast::make(num::from(1)));
				return true;
			}

		} else if (base1.compare(denom)) {
			/*Subtract powers*/
			power1.replace(ast::make(Op::Add, ast::make(num::from(-1)), power1.copy()));
			denom.replace(ast::make(num::from(1)));

			return true;
		}
	} else if (denom.isOp(Op::Pow) && numer.compare(*denom.childAt(0))) {
		/*Subtract powers*/
		ast &power2 = *denom.childAt(1);

		power2.replace(ast::make(Op::Add, ast::make(num::from(-1)), power2.copy()));
		numer.replace(ast::make(num::from(1)));

		return true;
	}

	if (eval_div_mult(numer, denom, flags))
		return true;

	if (!numer.isNumber() || !denom.isNumber())
		return false;

	/*Simplify fractions using GCD*/
	if (numer.num().isInteger() && denom.num().isInteger()) {
		num divisor;
		mp_int a = MP_NUMER_P(&numer.num()), b = MP_NUMER_P(&denom.num());

		/*TODO: check for division by zero?*/

		mp_int_gcd(a, b, MP_NUMER_P(&divisor));

		if (divisor != 1) {
			mp_int_div(a, MP_NUMER_P(&divisor), a, nullptr);
			mp_int_div(b, MP_NUMER_P(&divisor), b, nullptr);
			changed = true;
		}
	}

	return changed;
}

/*True if a^b is -1 to some power or has at most 64 bits*/
static bool power_in_small_range(ast &a, ast &b) {
	mp_small exponent;

	if (a.num() == -1)
		return true;

	if (!b.num().toInt(exponent) || exponent > 64)
		return false;

	return static_cast<mp_small>(mp_int_count_bits(MP_NUMER_P(&a.num()))) * exponent <= 64;
}

static bool eval_pow(ast &e, Eval flags) {
	/*a^b*/
	ast *a = e.childAt(0);
	ast *b = e.childAt(1);
	bool changed = false;

	if (!a || !b) {
		return false;
	}

	if (has(flags, Eval::BasicIdentities)) {
		/*If a == 0*/
		if (a->isInt(0)) {
			/*If b != 0, e = 0*/
			if (!b->isInt(0)) {
				e.replace(ast::make(num::from(0)));
				return true;
			}
		}

		/*b == 0*/
		else if (b->isInt(0)) {
			/*If a != 0, e = 1*/
			if (!a->isInt(0)) {
				e.replace(ast::make(num::from(1)));
				return true;
			}
		}

		/*a == 1*/
		else if (a->isInt(1)) {
			e.replace(a);
			return true;
		}

		/*b == 1*/
		else if (b->isInt(1)) {
			e.replace(a);
			return true;
		}

		/*(A^B)^C = A^(BC)*/
		if (a->isOp(Op::Pow)) {
			e.replace(ast::make(Op::Pow, a->childAt(0)->copy(), ast::make(Op::Mult, a->childAt(1)->copy(), b->copy())));
			return true;
		}

		/*A^(-B) = 1/(A^B) */
		if (is_negative_for_sure(*b)) {
			absolute_val(*b);
			e.replace(ast::make(Op::Div, ast::make(num::from(1)), e.copy()));
			return true;
		}
	}

	/*Evaluate a^b if a and b are integers*/
	if (a->isNumber() && b->isNumber()) {
		if (a->num().isInteger() && b->num().isInteger() && b->num() > 0) {
			if (has(flags, Eval::PowersFull) || (has(flags, Eval::PowersSmall) && power_in_small_range(*a, *b))) {
				num *result = num::from(1);

				mp_int_expt_full(MP_NUMER_P(&a->num()), MP_NUMER_P(&b->num()), MP_NUMER_P(result));

				e.replace(ast::make(result));

				return true;
			}
		}

	}
	/*Evaluate i^b if b is an integer*/
	else if (
		has(flags, Eval::PowersSmall | Eval::PowersFull) && a->isSymbol() && a->symbol() == Sym::Imag && b->isNumber()
	) {
		if (b->num().isInteger() && b->num() > 0) {
			mp_small remainder;

			mp_int_mod_value(MP_NUMER_P(&b->num()), 4, &remainder);

			if (remainder == 0) {
				e.replace(ast::make(num::from(1)));
			} else if (remainder == 1) {
				e.replace(ast::make(Sym::Imag));
			} else if (remainder == 2) {
				e.replace(ast::make(num::from(-1)));
			} else if (remainder == 3) {
				e.replace(ast::make(Op::Mult, ast::make(num::from(-1)), ast::make(Sym::Imag)));
			}

			return true;
		}
	}

	/*Do roots*/
	if (has(flags, Eval::PowersSmall | Eval::PowersFull) && a->isNumber() && b->isOperator() && b->op() == Op::Div &&
		b->childAt(0)->isInt(1) && b->childAt(1)->isNumber()) {
		/*a root of b*/
		ast &temp = *b;
		b = a;
		a = temp.childAt(1);

		mp_small index;
		if (a->num().toInt(index) && b->num().isInteger() && b->num() > 0) {
			num answer, check;

			mp_int_root(MP_NUMER_P(&b->num()), index, MP_NUMER_P(&answer));
			mp_int_expt(MP_NUMER_P(&answer), index, MP_NUMER_P(&check));

			/*The root is exact*/
			if (check == b->num()) {
				e.replace(ast::make(answer.copy()));
				changed = true;
			}
		}
	}

	return changed;
}

static bool eval_int(ast &e, Eval flags) {
	bool changed = false;

	if (!has(flags, Eval::Int))
		return false;

	const ast &a = *e.childAt(0);

	num remainder;

	if (a.isNumber()) {
		num *res = num::from(1);

		mp_int_div(MP_NUMER_P(&a.num()), MP_DENOM_P(&a.num()), MP_NUMER_P(res), MP_NUMER_P(&remainder));

		if (remainder != 0 && *res < 0)
			mp_int_sub_value(MP_NUMER_P(res), 1, MP_NUMER_P(res));

		e.replace(ast::make(res));

		changed = true;
	} else if (a.isOp(Op::Div)) {
		const ast &numer = *a.childAt(0);
		const ast &denom = *a.childAt(1);

		if (numer.isNumber() && denom.isNumber()) {
			num *res = num::from(1);

			/*That is a lot of num lol*/
			mp_int_div(MP_NUMER_P(&numer.num()), MP_NUMER_P(&denom.num()), MP_NUMER_P(res), MP_NUMER_P(&remainder));

			if (remainder != 0 && *res < 0)
				mp_int_sub_value(MP_NUMER_P(res), 1, MP_NUMER_P(res));

			e.replace(ast::make(res));

			changed = true;
		}
	}

	return changed;
}

static bool eval_abs(ast &e, Eval flags) {
	if (!has(flags, Eval::Abs))
		return false;

	ast &a = *e.childAt(0);
	const bool changed = absolute_val(a) || a.isNumber();

	if (changed)
		e.replace(e.firstChild());

	return changed;
}

static bool eval_log(ast &e, Eval flags) {
	const ast &base = *e.childAt(0);
	const ast &val = *e.childAt(1);

	if (has(flags, Eval::BasicIdentities)) {
		/*log(1) = 0*/
		if (val.isInt(1)) {
			e.replace(ast::make(num::from(0)));
			return true;
		}

		/*This is hardcoded here because we want this to happen before
        powers are evaluated*/
		/*log(A^B)=Blog(A)*/

		if (val.isOp(Op::Pow)) {
			const ast &power_base = *val.childAt(0);
			const ast &power_exponent = *val.childAt(1);

			e.replace(ast::make(Op::Mult, power_exponent.copy(), ast::make(Op::Log, base.copy(), power_base.copy())));
			return true;
		}
	}

	/*Todo evaluate log constants*/

	return false;
}

static bool eval_factorial(ast &e, Eval flags) {
	const ast *a = e.childAt(0);

	if (a->isNumber()) {
		if (has(flags, Eval::BasicIdentities)) {
			/*0! = 1*/
			if (a->num() == 0) {
				e.replace(ast::make(num::from(1)));
				return true;
			}
		}

		if (a->num().isInteger() && a->num() > 0) {
			if (has(flags, Eval::FactorialFull) || (has(flags, Eval::FactorialSmall) && a->num() <= 10)) {
				num *accumulator = num::from(1);
				mp_int i = mp_int_alloc();
				mp_int_init_copy(i, MP_NUMER_P(&a->num()));

				while (mp_int_compare_zero(i) != 0) {
					mp_rat_mul_int(accumulator, i, accumulator);
					mp_int_sub_value(i, 1, i);
				}

				e.replace(ast::make(accumulator));

				mp_int_free(i);

				return true;
			}
		}
	}

	return false;
}

/*Simplifies expressions like 5 + 5 to 10*/
static bool _eval(ast &e, Eval flags) {
	bool changed = false;

	/*Get a head start to evaluate the identity ln(A^B)=Bln(A)
    before child power node is evaluated*/
	if (e.isOp(Op::Log))
		changed |= eval_log(e, flags);

	if (!e.isOperator())
		return false;

	for (ast &current : e.children())
		changed |= eval(current, flags);

	if (is_op_commutative(e.op())) {
		changed |= eval_commutative(e, flags);
	} else {
		/*Simplify pow, root, log, factorial*/
		switch (e.op()) {
			case Op::Div: changed |= eval_div(e, flags); break;
			case Op::Pow: changed |= eval_pow(e, flags); break;
			case Op::Int: changed |= eval_int(e, flags); break;
			case Op::Abs: changed |= eval_abs(e, flags); break;
			case Op::Factorial: changed |= eval_factorial(e, flags); break;
			default: break;
		}
	}

	return changed;
}

bool eval(ast &e, Eval flags) {
	work::enter(e);
	const bool changed = _eval(e, flags);
	work::leave(e);
	return changed;
}

static bool _substitute(ast &e, const ast &from, const ast &to) {
	if (e.compare(from)) {
		e.replace(to.copy());
		return true;
	}

	if (e.isOperator()) {
		bool changed = false;

		for (ast &child : e.children())
			changed |= substitute(child, from, to);

		return changed;
	}

	return false;
}

bool substitute(ast &e, const ast &from, const ast &to) {
	work::enter(e);
	const bool changed = _substitute(e, from, to);
	work::leave(e);
	return changed;
}
