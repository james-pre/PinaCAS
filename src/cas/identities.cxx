#include "identities.hxx"

/*For calloc*/
#include <stdlib.h>
/*For strlen*/
#include <string.h>

#include "cas.hxx"
#include "../dbg.hxx"
#include "../work.hxx"
#include "derivative.hxx"

namespace id {

/*
    IDENTITY RULES:
    N is reserved for integers.

    I and J are reserved for real numbers.
*/
Identity general[] = {/*logb(value, base)*/

					  /*This identity is hardcoded in eval.c so that it executes before
    the power node is evaluated with two numerical values*/
					  /*{"logb(X^D,B", "Dlogb(X,B"}*/

					  {"logb(X,B)+logb(Y,B)+C", "logb(XY,B)+C"},
					  {"logb(X,B)_logb(Y,B)+C", "logb(X/Y,B)+C"},

					  {"A^(Clogb(B,A", "B^C"},
					  {"logb(A,A", "1"},

					  {"(ArootB)^A", "B"}, /*Todo ignores negatives*/
					  {"Aroot(B^A)", "B"},

					  {"sin(asin(X", "X"},
					  {"asin(sin(X", "X"},
					  {"cos(acos(X", "X"},
					  {"acos(cos(X", "X"},
					  {"tan(atan(X", "X"},
					  {"atan(tan(X", "X"},

					  {"sinh(asinh(X", "X"},
					  {"asinh(sinh(X", "X"},
					  {"cosh(acosh(X", "X"},
					  {"tanh(atanh(X", "X"},
					  {"atanh(tanh(X", "X"},
					  {nullptr}
};

Identity trig_identities[] = {
	{"sin(pi/2_X+C", "cos(X+C"},
	{"cos(pi/2_X+C", "sin(X+C"},

	{"sin(C+2piN", "sin(C"},
	{"sin(C+2pi", "sin(C"},
	{"cos(C+2piN", "cos(C"},
	{"cos(C+2pi", "cos(C"},
	{"tan(C+piN", "tan(C"},
	{"tan(C+pi", "tan(C"},

	{"sin(-C", "-sin(C"},
	{"cos(-C", "cos(C"},
	{"tan(-C", "-tan(C"},

	/*Double angle identities*/
	{"2Csin(X)cos(X", "Csin(2X"},
	{"cos(X)^2_sin(X)^2+C", "cos(2X)+C"},
	{"2cos(X)^2_1+C", "cos(2X)+C"},
	{"1_2sin(X)^2+C", "cos(2X)+C"},

	{"sin(X)^2+cos(X)^2+C", "1+C"},
	{"tan(X)^2+1+C", "1/cos(X)^2+C"},

	/*tan identities*/
	{"Asin(X)/(Bcos(X", "Atan(X)/B"},
	{"Acos(X)/(Bsin(X", "A/(Btan(X"},
	{"Atan(X)cos(X", "Asin(X"},
	{"Asin(X)/(Btan(X", "Acos(X)/B"},

	{"cos(asin(X", "sqrt(1_X^2"},
	{"cos(atan(X", "1/sqrt(1+X^2"},
	{"sin(acos(X", "sqrt(1_X^2"},
	{"sin(atan(X", "X/sqrt(1+X^2"},
	{"tan(acos(X", "sqrt(1_X^2)/X"},
	{"tan(asin(X", "X/(sqrt(1_X^2"},
	{nullptr}
};

Identity trig_constants[] = {
	{"sin(0", "0"},
	{"sin(pi/6", "1/2"},
	{"sin(pi/4", "sqrt(2)/2"},
	{"sin(pi/3", "sqrt(3)/2"},
	{"sin(pi/2", "1"},
	{"sin(2pi/3", "sqrt(3)/2"},
	{"sin(3pi/4", "sqrt(2)/2"},
	{"sin(5pi/6", "1/2"},
	{"sin(pi", "0"},
	{"sin(7pi/6", "-1/2"},
	{"sin(5pi/4", "-sqrt(2)/2"},
	{"sin(4pi/3", "-sqrt(3)/2"},
	{"sin(3pi/2", "-1"},
	{"sin(5pi/3", "-sqrt(3)/2"},
	{"sin(7pi/4", "-sqrt(2)/2"},
	{"sin(11pi/6", "-1/2"},

	{"cos(0", "1"},
	{"cos(pi/6", "sqrt(3)/2"},
	{"cos(pi/4", "sqrt(2)/2"},
	{"cos(pi/3", "1/2"},
	{"cos(pi/2", "0"},
	{"cos(2pi/3", "-1/2"},
	{"cos(3pi/4", "-sqrt(2)/2"},
	{"cos(5pi/6", "-sqrt(3)/2"},
	{"cos(pi", "-1"},
	{"cos(7pi/6", "-sqrt(3)/2"},
	{"cos(5pi/4", "-sqrt(2)/2"},
	{"cos(4pi/3", "-1/2"},
	{"cos(3pi/2", "0"},
	{"cos(5pi/3", "1/2"},
	{"cos(7pi/4", "sqrt(2)/2"},
	{"cos(11pi/6", "sqrt(3)/2"},

	{"tan(0", "0"},
	{"tan(pi/6", "sqrt(3)/3"},
	{"tan(pi/4", "1"},
	{"tan(pi/3", "sqrt(3)"},
	/*{"tan(pi/2", "inf"},*/
	{"tan(2pi/3", "-sqrt(3"},
	{"tan(3pi/4", "-1"},
	{"tan(5pi/6", "-sqrt(3)/3"},
	{"tan(pi", "0"},
	{nullptr}
};

Identity trig_inv_constants[] = {
	{"asin(-1", "-pi/2"},      {"asin(-sqrt(3)/2", "-pi/3"}, {"asin(-1/2^(1/2", "-pi/4"},  {"asin(-1/2", "-pi/6"},
	{"asin(0", "0"},           {"asin(1/2", "pi/6"},         {"asin(1/2^(1/2)", "pi/4"},   {"asin(sqrt(3)/2", "pi/3"},
	{"asin(1", "pi/2"},

	{"acos(-1", "pi"},         {"acos(-sqrt(3)/2", "5pi/6"}, {"acos(-1/2^(1/2)", "3pi/4"}, {"acos(-1/2", "2pi/3"},
	{"acos(0", "pi/2"},        {"acos(1/2", "pi/3"},         {"acos(1/2^(1/2)", "pi/4"},   {"acos(sqrt(3)/2", "pi/6"},
	{"acos(1", "0"},

	{"atan(-sqrt(3", "-pi/3"}, {"atan(-1", "-pi/4"},         {"atan(-1/sqrt(3", "-pi/6"},  {"atan(0", "0"},
	{"atan(1/sqrt(3", "pi/6"}, {"atan(1", "pi/4"},           {"atan(sqrt(3", "pi/3"},      {nullptr}
};

Identity hyperbolic[] =
	{{"cosh(X)_sinh(X)", "e^(-X"}, {"sinh(X)/cosh(X", "tanh(X"}, {"cosh(X)^2_sinh(X)^2+C", "1+C"}, {nullptr}};

Identity complex[] = {
	{"(-A)^(X/Y)", "e^(ipiX/Y)A^(X/Y"},
	{"1/i", "-i"},
	{"e^(I+Ji", "e^I(cos(J)+isin(J"},
	{"(I+Ji)^X", "e^(Xln(I+Ji"},
	{"X^(I+Ji", "e^((I+Ji)ln(X"},
	{"abs(I+Ji", "sqrt(I^2+J^2"},
	{"atan(X/0", "pi/2"}, /*Lol is this cheating?*/
	{"ln(I+Ji", "ln(abs(I+Ji))+iatan(J/I)"},
	{"logb(X,I+Ji", "ln(X)/ln(I+Ji"},
	{"sin(I+Ji", "sin(I)cosh(J)+icos(I)sinh(J"},
	{"cos(I+Ji", "cos(I)cosh(J)_isin(I)sinh(J)"},
	{"tan(I+Ji", "sin(I+Ji)/cos(I+Ji"},
	{nullptr}
};

using Dictionary = ast **;

/*The entry for a symbol from A to Z*/
static ast *&dict_Get(Dictionary dict, const ast *symbol) {
	return dict[static_cast<unsigned>(symbol->symbol()) - 'A'];
}

static void dict_Copy(Dictionary dest, Dictionary source) {
	for (unsigned i = 0; i < sym_count; i++) {
		if (source[i] != nullptr)
			dest[i] = source[i]->copy();
		else
			dest[i] = nullptr;
	}
}

static void dict_Write(Dictionary dest, Dictionary source) {
	memcpy(dest, source, sym_count * sizeof(ast *));
}

static void dict_Cleanup(Dictionary dict) {
	for (unsigned i = 0; i < sym_count; i++) {
		if (dict[i] != nullptr)
			ast::dispose(dict[i]);
		dict[i] = nullptr;
	}
}

/*Simplifies 2N, 4 to N, 2 to correctly set N*/
static bool divide_numerical_constants(ast &id, ast &e) {
	if (!id.isOp(Op::Mult))
		return false;

	if (e.isInt(0))
		return false;

	for (const ast &child : id.children()) {
		if (child.isNumber()) {
			const bool negates_e = child.isInt(-1) && !is_negative_for_sure(e);

			e.replace(ast::make(Op::Div, e.copy(), child.copy()));
			id.replace(ast::make(Op::Div, id.copy(), child.copy()));

			/*e has no negative coefficient to cancel, so leave the division to fail the match*/
			if (negates_e)
				return true;

			simplify(e, Simp::Commutative | Simp::Eval);
			simplify(id, Simp::Commutative | Simp::Eval);

			return true;
		}
	}

	return false;
}

/*Fills in id->to with discovered values from dictionary */
static void fill(ast *to, Dictionary dict) {
	if (to->isSymbol()) {
		if (dict_Get(dict, to) != nullptr)
			to->replace(dict_Get(dict, to)->copy());
	} else if (to->isOperator()) {
		for (ast &child : to->children()) {
			fill(&child, dict);
		}
	}
}

static bool matches(ast *id, ast &e, Dictionary dict) {
	if (id->isSymbol() && id->symbol() < Sym::Imag) {
		if (id->symbol() == Sym::N) {
			/*Only integers allowed*/
			if (!(e.isNumber() && e.num().isInteger()))
				return false;
		} else if (id->symbol() == Sym::I || id->symbol() == Sym::J) {
			/*We assume something is real if it does not have an imaginary node. This could be wrong.*/
			if (has_imaginary_node(e))
				return false;
		}

		/*Check if dictionary does not yet have value*/
		if (dict_Get(dict, id) == nullptr) {
			/*No value exists in dictionary, this node claims it*/
			dict_Get(dict, id) = e.copy();
			return true;
		} else {
			/*Value already exists, we only match if we are the same*/
			return dict_Get(dict, id)->compare(e);
		}
	} else if (id->isOperator()) {
		/*Make a copy of the dictionary in case the children do not match
        We do not fill the dictionary with bad values*/
		ast *dict_copy[sym_count];

		dict_Copy(dict_copy, dict);

		if (is_op_commutative(id->op())) {
			/*Order does not matter*/
			bool combined = false;
			char combined_character = '\0';

			ast *id_copy = id->copy();
			ast *e_copy = e.copy();

			/*Divide numerical constants from each side.*/
			while (divide_numerical_constants(*id_copy, *e_copy))
				;

			/*Id had numerical coefficients that e did not have.*/
			if (e_copy->isOp(Op::Div)) {
				dict_Cleanup(dict_copy);
				ast::dispose(id_copy);
				ast::dispose(e_copy);
				return false;
			}

			/*Make e_copy an addition or multiplication node with one child so the algorithm below works.*/
			if (!e_copy->isOp(id->op()))
				e_copy->replace(ast::make(id->op(), e_copy->copy()));

			if (!id_copy->isOp(id->op()))
				id_copy->replace(ast::make(id->op(), id_copy->copy()));

			/*Remove the symbol. Do not simplify commutative. id_copy may be a node with one child.*/
			for (unsigned i = 0; i < id_copy->childCount(); i++) {
				const ast &child = *id_copy->childAt(i);

				if (child.isSymbol() && child.symbol() < Sym::Imag && child.symbol() != Sym::N) {
					combined = true;
					combined_character = static_cast<char>(child.symbol());

					ast::dispose(id_copy->removeChildAt(i));

					if (id->isOp(Op::Add))
						dict_copy[combined_character - 'A'] = ast::make(num::from(0));
					else /*Op::Mult*/
						dict_copy[combined_character - 'A'] = ast::make(num::from(1));

					break;
				}
			}

			/*At this point, id_copy and e_copy are both the same either addition
            or multiplication nodes and we can continue normally*/

			/*We know they will not match if id has more elements than e*/
			if (id_copy->childCount() > e_copy->childCount()) {
				dict_Cleanup(dict_copy);
				ast::dispose(id_copy);
				ast::dispose(e_copy);
				return false;
			}

			/*We know we are comparing constants, they need to be same size. */
			if (!combined && id_copy->childCount() != e_copy->childCount()) {
				dict_Cleanup(dict_copy);
				ast::dispose(id_copy);
				ast::dispose(e_copy);
				return false;
			}

			/*Loop through all children and match them and record which children got matched.*/

			bool *matched_e_children = static_cast<bool *>(calloc(e_copy->childCount(), sizeof(bool)));
			bool *matched_id_children = static_cast<bool *>(calloc(id_copy->childCount(), sizeof(bool)));
			bool matched;

			do {
				matched = false;

				for (unsigned i = 0; i < e_copy->childCount(); i++) {
					ast &e_child = *e_copy->childAt(i);

					if (matched_e_children[i])
						continue;

					for (unsigned j = 0; j < id_copy->childCount(); j++) {
						ast *dict_copy_copy[sym_count];
						ast *id_child = id_copy->childAt(j);

						dict_Copy(dict_copy_copy, dict_copy);

						if (!matched_id_children[j]) {
							if (matches(id_child, e_child, dict_copy_copy)) {
								matched_e_children[i] = true;
								matched_id_children[j] = true;
								matched = true;

								dict_Cleanup(dict_copy);
								dict_Write(dict_copy, dict_copy_copy);

								break;
							}
						}

						dict_Cleanup(dict_copy_copy);
					}

					if (matched)
						break;
				}

			} while (matched);

			/*Check if we have matched every id child. We don't have to match
            every e child due to commutative nature.*/
			matched = true;
			for (unsigned i = 0; i < id_copy->childCount(); i++)
				matched &= matched_id_children[i];

			/*Make the grouped variable set equal to the nodes not included matched_e_children*/
			if (matched && combined) {
				ast *c = ast::make(id->op());

				for (unsigned i = 0; i < e_copy->childCount(); i++) {
					const ast &child = *e_copy->childAt(i);

					if (!matched_e_children[i])
						c->appendChild(child.copy());
				}

				if (c->childCount() > 0) {
					/*If child length is 1, fix it*/
					simplify(*c, Simp::Commutative);

					/*If the combined parts need to be real and they are or if they don't need to be real*/
					/*We assume something is real if it does not have an imaginary node. This could be wrong.*/
					if (!((combined_character == 'I' || combined_character == 'J') && has_imaginary_node(*c))) {
						/*Cleanup and overwrite dummy placeholder*/
						ast::dispose(dict_copy[combined_character - 'A']);
						dict_copy[combined_character - 'A'] = c;
					} else {
						matched = false;
						ast::dispose(c);
					}

				} else {
					ast::dispose(c);
				}
			}

			free(matched_e_children);
			free(matched_id_children);

			ast::dispose(e_copy);
			ast::dispose(id_copy);

			if (matched) {
				dict_Cleanup(dict);
				dict_Write(dict, dict_copy);
			} else {
				dict_Cleanup(dict_copy);
			}

			return matched;

		} else {
			/*Order and length do matter*/
			if (!e.isOperator()) {
				dict_Cleanup(dict_copy);
				return false;
			}

			if (e.op() != id->op() || e.childCount() != id->childCount()) {
				dict_Cleanup(dict_copy);
				return false;
			}

			/*Reverse loop to better guess variables for derivative nodes*/
			for (int i = e.childCount() - 1; i >= 0; i--) {
				ast &e_child = *e.childAt(i);
				ast *id_child = id->childAt(i);
				if (!matches(id_child, e_child, dict_copy)) {
					dict_Cleanup(dict_copy);
					return false;
				}
			}

			/*All children are matching. Write the copy to the actual dict*/
			dict_Cleanup(dict);
			dict_Write(dict, dict_copy);
			return true;
		}
	}

	/*Compare numbers or pi, e constants */
	return e.compare(*id);
}

/*
    Requires that constants are already evaluated.
*/
bool execute(ast &e, Identity *id, bool recursive) {
	ast *dict[sym_count] = {0};
	bool changed = false;

	if (id->from == nullptr || id->to == nullptr) {
		if (!load(id)) {
			LOG(("Could not parse identity. from=%s to=%s", id->from_text, id->to_text));
			return false;
		}
	}

	if (matches(id->from, e, dict)) {
		ast *to = id->to->copy();

		fill(to, dict);

		e.replace(to);

		/*LOG(("Matched identity from=%s to=%s", id->from_text, id->to_text));*/

		changed = true;
	}

	if (recursive && e.isOperator()) {
		for (ast &child : e.children())
			changed |= execute(child, id, recursive);
	}

	dict_Cleanup(dict);

	return changed;
}

bool load(Identity *id) {
	Error err;

	id->from = parse((uint8_t *)id->from_text, strlen(id->from_text), str_table, &err);

	if (err != Error::Success)
		return false;

	id->to = parse((uint8_t *)id->to_text, strlen(id->to_text), str_table, &err);

	if (err != Error::Success)
		return false;

	if (id->from != nullptr && id->to != nullptr) {
		/*Assumes that from and to are already simplified. This just puts it into a form we can compare*/
		work::pause();
		simplify(*id->from, Simp::Normalize | Simp::Commutative);
		simplify(*id->to, Simp::Normalize | Simp::Commutative);
		work::resume();
		return true;
	}

	return false;
}

void unload(Identity *id) {
	if (id->from != nullptr)
		ast::dispose(id->from);
	id->from = nullptr;

	if (id->to != nullptr)
		ast::dispose(id->to);
	id->to = nullptr;
}

bool executeTable(ast &e, Identity *table, bool recursive) {
	bool changed = false;

	for (; table->from_text != nullptr; table++) {
		changed |= execute(e, table, recursive);

		/*Break if changed to save time on the calculator becaus chances are good
        that after an identity is applied, we do not need to go through the rest.*/
		simplify(e, Simp::Commutative | Simp::Eval);
		if (changed)
			break;
	}

	return changed;
}

void loadTable(Identity *table) {
	for (; table->from_text != nullptr; table++)
		load(table);
}

void unloadTable(Identity *table) {
	for (; table->from_text != nullptr; table++)
		unload(table);
}

void unloadAll() {
	unloadTable(general);
	unloadTable(trig_identities);
	unloadTable(trig_constants);
	unloadTable(trig_inv_constants);
	unloadTable(hyperbolic);
	unloadTable(complex);

	unloadTable(derivative);

	unload(&deriv_power_rule);
	unload(&deriv_exponential_rule);
	unload(&deriv_constant_rule);
	unload(&deriv_product_rule);
}

} // namespace id
