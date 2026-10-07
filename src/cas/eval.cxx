#include "cas.hxx"

#include "../work.hxx"

static bool eval_commutative(ast *e, Eval flags) {
	/*How many numbers were accumulated. If <= 1, nothing changed*/
	unsigned num_changed = false;

	if (!has(flags, Eval::Commutative))
		return false;

	{
		unsigned numbers = 0, fractions = 0;
		const ast *first = e->firstChild();

		for (const ast *child = first; child != nullptr; child = child->next()) {
			if (child->isNumber())
				numbers++;
			else if (
				e->isOp(Op::Add) && child->isOp(Op::Div) && child->firstChild()->isNumber() &&
				child->firstChild()->next()->isNumber()
			)
				fractions++;
		}

		/*A lone fraction is already normalized, so it is only combined with other numbers*/
		if (first != nullptr && numbers == 0 && fractions <= 1)
			return false;

		/*A lone number that is already first and is not an identity element stays as it is*/
		if (numbers == 1 && fractions == 0 && first->isNumber() && first->next() != nullptr &&
			mp_rat_compare_zero(first->num()) != 0 && !(e->op() == Op::Mult && first->isInt(1)))
			return false;
	}

	mp_rat accumulator = num_FromInt(e->op() == Op::Mult ? 1 : 0);

	for (unsigned i = 0; i < e->childCount(); i++) {
		const ast *child = e->childAt(i);

		if (child->isNumber()) {
			if (e->op() == Op::Mult)
				mp_rat_mul(accumulator, child->num(), accumulator);
			else
				mp_rat_add(accumulator, child->num(), accumulator);

			ast::dispose(e->removeChildAt(i));
			i--;
			num_changed++;
		} else if (e->isOp(Op::Add) && child->isOp(Op::Div)) {
			const ast *c_num = child->childAt(0);
			const ast *c_den = child->childAt(1);

			if (c_num->isNumber() && c_den->isNumber()) {
				/*a/b + c/d = (ad + bc)/(bd)*/

				mp_int a = &accumulator->num;
				mp_int b = &accumulator->den;
				const mpz_t *c = MP_NUMER_P(c_num->num());
				const mpz_t *d = MP_NUMER_P(c_den->num());

				mp_int first = mp_int_alloc();
				mp_int_init(first);

				mp_int_mul(a, d, first);

				mp_int second = mp_int_alloc();
				mp_int_init(second);

				mp_int_mul(b, c, second);

				mp_int num = mp_int_alloc();
				mp_int_add(first, second, num);

				mp_int den = mp_int_alloc();
				mp_int_init(den);

				mp_int_mul(b, d, den);

				mp_int_copy(num, a);
				mp_int_copy(den, b);

				mp_int_free(first);
				mp_int_free(second);
				mp_int_free(num);
				mp_int_free(den);

				ast::dispose(e->removeChildAt(i));

				i--;
				num_changed++;
			}
		}
	}

	/*If all children nodes were numbers or if multiplied by -*/
	if (e->childCount() == 0 || (e->op() == Op::Mult && mp_rat_compare_zero(accumulator) == 0)) {
		e->replace(ast::make(accumulator));
		return true;
	}
	/*Do not append accumulator if multiplying by 1 or adding 0*/
	else if (!((e->op() == Op::Add && mp_rat_compare_zero(accumulator) == 0) ||
			   (e->op() == Op::Mult && mp_rat_compare_value(accumulator, 1, 1) == 0))) {
		e->insertChild(ast::make(accumulator), 0);
	} else
		num_Cleanup(accumulator);

	return num_changed > 1;
}

static bool eval_div(ast *e, Eval flags);

static bool eval_div_mult(ast *num, ast *den, Eval flags) {
	bool changed = false;

	if (num->isOp(Op::Mult)) {
		for (unsigned i = 0; i < num->childCount(); i++) {
			const ast *temp_num = num->childAt(i);

			if (den->isOp(Op::Mult)) {
				for (unsigned j = 0; j < den->childCount(); j++) {
					const ast *temp_den = den->childAt(j);

					ast *temp_div = ast::make(Op::Div, temp_num->copy(), temp_den->copy());

					if (eval_div(temp_div, flags)) {
						ast::dispose(num->removeChildAt(i));
						ast::dispose(den->removeChildAt(j));

						num->appendChild(temp_div);

						changed = true;

						j--;
						break;
					} else {
						ast::dispose(temp_div);
					}
				}
			} else {
				ast *temp_div = ast::make(Op::Div, temp_num->copy(), den->copy());

				if (eval_div(temp_div, flags)) {
					ast::dispose(num->removeChildAt(i));
					den->replace(ast::make(num_FromInt(1)));

					num->appendChild(temp_div);

					changed = true;
				} else {
					ast::dispose(temp_div);
				}
			}
		}

	} else if (den->isOp(Op::Mult)) {
		for (unsigned j = 0; j < den->childCount(); j++) {
			const ast *temp_den = den->childAt(j);

			ast *temp_div = ast::make(Op::Div, num->copy(), temp_den->copy());

			if (eval_div(temp_div, flags)) {
				num->replace(temp_div);
				ast::dispose(den->removeChildAt(j));

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

static bool eval_div(ast *e, Eval flags) {
	bool changed = false;

	ast *num = e->childAt(0);
	ast *den = e->childAt(1);

	if (has(flags, Eval::BasicIdentities)) {
		if (is_negative_for_sure(num) && is_negative_for_sure(den)) {
			absolute_val(num);
			absolute_val(den);

			ast *new_div = ast::make(Op::Div, num->copy(), den->copy());

			eval_div(new_div, flags);
			e->replace(new_div);

			return true;
		}

		/*If numerator and denominator are equal, replace node with "1"*/
		if (num->compare(*den)) {
			e->replace(ast::make(num_FromInt(1)));
			return true;
		}

		if (num->isNumber() && mp_rat_compare_zero(num->num()) == 0) {
			e->replace(ast::make(num_FromInt(0)));
			return true;
		}

		if (den->isNumber() && mp_rat_compare_value(den->num(), 1, 1) == 0) {
			e->replace(num);
			return true;
		}

		/*Move the sign of a negative denominator to the numerator*/
		if (den->isNumber() && mp_rat_compare_zero(den->num()) < 0) {
			mp_rat_neg(den->num(), den->num());
			num->replace(ast::make(Op::Mult, ast::make(num_FromInt(-1)), num->copy()));
			return true;
		}
	}

	if (!has(flags, Eval::Division))
		return false;

	if (num->isOp(Op::Pow)) {
		const ast *base1 = num->childAt(0);
		ast *power1 = num->childAt(1);

		if (den->isOp(Op::Pow)) {
			const ast *base2 = den->childAt(0);
			const ast *power2 = den->childAt(1);

			if (base1->compare(*base2)) {
				/*Subtract powers*/
				power1->replace(
					ast::make(Op::Add, power1->copy(), ast::make(Op::Mult, ast::make(num_FromInt(-1)), power2->copy()))
				);
				den->replace(ast::make(num_FromInt(1)));
				return true;
			}

		} else if (base1->compare(*den)) {
			/*Subtract powers*/
			power1->replace(ast::make(Op::Add, ast::make(num_FromInt(-1)), power1->copy()));
			den->replace(ast::make(num_FromInt(1)));

			return true;
		}
	} else if (den->isOp(Op::Pow) && num->compare(*den->childAt(0))) {
		/*Subtract powers*/
		ast *power2 = den->childAt(1);

		power2->replace(ast::make(Op::Add, ast::make(num_FromInt(-1)), power2->copy()));
		num->replace(ast::make(num_FromInt(1)));

		return true;
	}

	if (eval_div_mult(num, den, flags))
		return true;

	if (!num->isNumber() || !den->isNumber())
		return false;

	/*Simplify fractions using GCD*/
	if (mp_rat_is_integer(num->num()) && mp_rat_is_integer(den->num())) {
		mpz_t gcd;
		mp_int a = &num->num()->num, b = &den->num()->num;

		/*TODO: check for division by zero?*/

		mp_int_init(&gcd);
		mp_int_gcd(a, b, &gcd);

		if (mp_int_compare_value(&gcd, 1) != 0) {
			mp_int_div(a, &gcd, a, nullptr);
			mp_int_div(b, &gcd, b, nullptr);
			changed = true;
		}

		mp_int_clear(&gcd);
	}

	return changed;
}

/*True if a^b is -1 to some power or has at most 64 bits*/
static bool power_in_small_range(ast *a, ast *b) {
	mp_small exponent;

	if (mp_rat_compare_value(a->num(), -1, 1) == 0)
		return true;

	if (mp_int_to_int(&b->num()->num, &exponent) != MP_OK || exponent > 64)
		return false;

	return (mp_small)mp_int_count_bits(&a->num()->num) * exponent <= 64;
}

static bool eval_pow(ast *e, Eval flags) {
	/*a^b*/
	ast *a = e->childAt(0);
	ast *b = e->childAt(1);
	bool changed = false;

	if (!a || !b) {
		return false;
	}

	if (has(flags, Eval::BasicIdentities)) {
		/*If a == 0*/
		if (a->isInt(0)) {
			/*If b != 0, e = 0*/
			if (!b->isInt(0)) {
				e->replace(ast::make(num_FromInt(0)));
				return true;
			}
		}

		/*b == 0*/
		else if (b->isInt(0)) {
			/*If a != 0, e = 1*/
			if (!a->isInt(0)) {
				e->replace(ast::make(num_FromInt(1)));
				return true;
			}
		}

		/*a == 1*/
		else if (a->isInt(1)) {
			e->replace(a);
			return true;
		}

		/*b == 1*/
		else if (b->isInt(1)) {
			e->replace(a);
			return true;
		}

		/*(A^B)^C = A^(BC)*/
		if (a->isOp(Op::Pow)) {
			e->replace(
				ast::make(Op::Pow, a->childAt(0)->copy(), ast::make(Op::Mult, a->childAt(1)->copy(), b->copy()))
			);
			return true;
		}

		/*A^(-B) = 1/(A^B) */
		if (is_negative_for_sure(b)) {
			absolute_val(b);
			e->replace(ast::make(Op::Div, ast::make(num_FromInt(1)), e->copy()));
			return true;
		}
	}

	/*Evaluate a^b if a and b are integers*/
	if (a->isNumber() && b->isNumber()) {
		if (mp_rat_is_integer(a->num()) && mp_rat_is_integer(b->num()) && mp_rat_compare_zero(b->num()) > 0) {
			if (has(flags, Eval::PowersFull) || (has(flags, Eval::PowersSmall) && power_in_small_range(a, b))) {
				mp_rat result = num_FromInt(1);

				mp_int_expt_full(&a->num()->num, &b->num()->num, &result->num);

				e->replace(ast::make(result));

				return true;
			}
		}

	}
	/*Evaluate i^b if b is an integer*/
	else if (
		has(flags, Eval::PowersSmall | Eval::PowersFull) && a->isSymbol() && a->symbol() == Sym::Imag && b->isNumber()
	) {
		if (mp_rat_is_integer(b->num()) && mp_rat_compare_zero(b->num()) > 0) {
			mp_small remainder;

			mp_int_mod_value(&b->num()->num, 4, &remainder);

			if (remainder == 0) {
				e->replace(ast::make(num_FromInt(1)));
			} else if (remainder == 1) {
				e->replace(ast::make(Sym::Imag));
			} else if (remainder == 2) {
				e->replace(ast::make(num_FromInt(-1)));
			} else if (remainder == 3) {
				e->replace(ast::make(Op::Mult, ast::make(num_FromInt(-1)), ast::make(Sym::Imag)));
			}

			return true;
		}
	}

	/*Do roots*/
	if (has(flags, Eval::PowersSmall | Eval::PowersFull) && a->isNumber() && b->isOperator() && b->op() == Op::Div &&
		b->childAt(0)->isInt(1) && b->childAt(1)->isNumber()) {
		/*a root of b*/
		ast *temp = b;
		b = a;
		a = temp->childAt(1);

		if (mp_rat_is_integer(a->num()) && mp_rat_is_integer(b->num()) && mp_rat_compare_zero(b->num()) > 0) {
			/*Set answer = small root of a*/
			mp_int answer = mp_int_alloc();
			mp_int_init(answer);

			mp_small small;
			mp_int_to_int(&a->num()->num, &small);

			mp_int_root(&b->num()->num, small, answer);

			/*Check if answer ^ small == b*/
			mp_int check = mp_int_alloc();
			mp_int_init(check);

			mp_int_expt(answer, small, check);

			if (mp_int_compare(check, &b->num()->num) == 0) {
				/*Then this was successful*/
				mp_rat ret = num_FromInt(1);
				mp_int_copy(answer, &ret->num);

				e->replace(ast::make(ret));

				changed = true;
			}

			mp_int_free(check);
			mp_int_free(answer);
		}
	}

	return changed;
}

static bool eval_int(ast *e, Eval flags) {
	bool changed = false;

	if (!has(flags, Eval::Int))
		return false;

	const ast *a = e->childAt(0);

	mp_int remainder = mp_int_alloc();
	mp_int_init(remainder);

	if (a->isNumber()) {
		mp_rat res = num_FromInt(1);

		mp_int_div(&a->num()->num, &a->num()->den, &res->num, remainder);

		if (mp_int_compare_zero(remainder) != 0 && mp_int_compare_zero(&res->num) < 0)
			mp_int_sub_value(&res->num, 1, &res->num);

		e->replace(ast::make(res));

		changed = true;
	} else if (a->isOp(Op::Div)) {
		const ast *num = a->childAt(0);
		const ast *den = a->childAt(1);

		if (num->isNumber() && den->isNumber()) {
			mp_rat res = num_FromInt(1);

			/*That is a lot of num lol*/
			mp_int_div(&num->num()->num, &den->num()->num, &res->num, remainder);

			if (mp_int_compare_zero(remainder) != 0 && mp_int_compare_zero(&res->num) < 0)
				mp_int_sub_value(&res->num, 1, &res->num);

			e->replace(ast::make(res));

			changed = true;
		}
	}

	mp_int_free(remainder);

	return changed;
}

static bool eval_abs(ast *e, Eval flags) {
	if (!has(flags, Eval::Abs))
		return false;

	ast *a = e->childAt(0);
	const bool changed = absolute_val(a) || a->isNumber();

	if (changed)
		e->replace(a);

	return changed;
}

static bool eval_log(ast *e, Eval flags) {
	const ast *base = e->childAt(0);
	const ast *val = e->childAt(1);

	if (has(flags, Eval::BasicIdentities)) {
		/*log(1) = 0*/
		if (val->isInt(1)) {
			e->replace(ast::make(num_FromInt(0)));
			return true;
		}

		/*This is hardcoded here because we want this to happen before
        powers are evaluated*/
		/*log(A^B)=Blog(A)*/

		if (val->isOp(Op::Pow)) {
			const ast *power_base = val->childAt(0);
			const ast *power_exponent = val->childAt(1);

			e->replace(
				ast::make(Op::Mult, power_exponent->copy(), ast::make(Op::Log, base->copy(), power_base->copy()))
			);
			return true;
		}
	}

	/*Todo evaluate log constants*/

	return false;
}

#define factorial_in_small_range(a) (mp_rat_compare_value((a)->num(), 10, 1) <= 0)

static bool eval_factorial(ast *e, Eval flags) {
	const ast *a = e->childAt(0);

	if (a->isNumber()) {
		if (has(flags, Eval::BasicIdentities)) {
			/*0! = 1*/
			if (mp_rat_compare_zero(a->num()) == 0) {
				e->replace(ast::make(num_FromInt(1)));
				return true;
			}
		}

		if (mp_rat_is_integer(a->num()) && mp_rat_compare_zero(a->num()) > 0) {
			if (has(flags, Eval::FactorialFull) || (has(flags, Eval::FactorialSmall) && factorial_in_small_range(a))) {
				mp_rat accumulator = num_FromInt(1);
				mp_int i = mp_int_alloc();
				mp_int_init_copy(i, &a->num()->num);

				while (mp_int_compare_zero(i) != 0) {
					mp_rat_mul_int(accumulator, i, accumulator);
					mp_int_sub_value(i, 1, i);
				}

				e->replace(ast::make(accumulator));

				mp_int_free(i);

				return true;
			}
		}
	}

	return false;
}

/*Simplifies expressions like 5 + 5 to 10*/
static bool _eval(ast *e, Eval flags) {
	bool changed = false;

	/*Get a head start to evaluate the identity ln(A^B)=Bln(A)
    before child power node is evaluated*/
	if (e->isOp(Op::Log))
		changed |= eval_log(e, flags);

	if (!e->isOperator())
		return false;

	for (ast *current : e->children())
		changed |= eval(current, flags);

	if (is_op_commutative(e->op())) {
		changed |= eval_commutative(e, flags);
	} else {
		/*Simplify pow, root, log, factorial*/
		switch (e->op()) {
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

bool eval(ast *e, Eval flags) {
	work::enter(e);
	const bool changed = _eval(e, flags);
	work::leave(e);
	return changed;
}

static bool _substitute(ast *e, const ast *from, const ast *to) {
	if (e->compare(*from)) {
		e->replace(to->copy());
		return true;
	}

	if (e->isOperator()) {
		bool changed = false;

		for (ast *child : e->children())
			changed |= substitute(child, from, to);

		return changed;
	}

	return false;
}

bool substitute(ast *e, const ast *from, const ast *to) {
	work::enter(e);
	const bool changed = _substitute(e, from, to);
	work::leave(e);
	return changed;
}
