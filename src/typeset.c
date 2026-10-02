#include "typeset.h"

#include <string.h>

#include "cas/cas.h"

static ts_box_t *box_new(BoxType type) {
	ts_box_t *b = calloc(1, sizeof(ts_box_t));
	b->type = type;
	return b;
}

ts_box_t *ts_Text(const char *text) {
	ts_box_t *b = box_new(BOX_TEXT);
	b->text = malloc(strlen(text) + 1);
	strcpy(b->text, text);
	return b;
}

ts_box_t *ts_Row(void) {
	return box_new(BOX_ROW);
}

ts_box_t *ts_Append(ts_box_t *parent, ts_box_t *child) {
	ts_box_t **slot = &parent->first;

	while (*slot != NULL)
		slot = &(*slot)->next;

	*slot = child;
	return parent;
}

static ts_box_t *pair(BoxType type, ts_box_t *a, ts_box_t *b) {
	return ts_Append(ts_Append(box_new(type), a), b);
}

static ts_box_t *row2(ts_box_t *a, ts_box_t *b) {
	return pair(BOX_ROW, a, b);
}

static ts_box_t *delimited(ts_box_t *child, char left, char right) {
	ts_box_t *b = ts_Append(box_new(BOX_DELIMITED), child);
	b->left = left;
	b->right = right;
	return b;
}

static ts_box_t *integer_box(mp_int z) {
	mp_size len = mp_int_string_len(z, 10);
	char *text = malloc(len);
	ts_box_t *b;

	mp_int_to_string(z, 10, text, len);
	b = ts_Text(text);
	free(text);

	return b;
}

static ts_box_t *number_box(mp_rat r) {
	if (mp_rat_is_integer(r))
		return integer_box(MP_NUMER_P(r));

	return pair(BOX_FRACTION, integer_box(MP_NUMER_P(r)), integer_box(MP_DENOM_P(r)));
}

static ts_box_t *symbol_box(Symbol symbol) {
	char text[2] = {0};

	switch (symbol) {
		case SYM_PI: text[0] = TS_PI; break;
		case SYM_THETA: text[0] = TS_THETA; break;
		case SYM_EULER: text[0] = 'e'; break;
		case SYM_IMAG: text[0] = 'i'; break;
		default: text[0] = (char)symbol; break;
	}

	return ts_Text(text);
}

/*True if e does not need parentheses as a base or before a postfix operator*/
static bool is_atom(pcas_ast_t *e) {
	switch (e->type) {
		case NODE_NUMBER: return mp_rat_is_integer(e->op.num) && mp_rat_compare_zero(e->op.num) >= 0;
		case NODE_SYMBOL: return true;
		case NODE_OPERATOR: return optype(e) == OP_PRIME || optype(e) == OP_LOG || is_op_function(optype(e));
	}

	return false;
}

/*True if e is drawn starting with a number, so a product needs a dot before it*/
static bool starts_with_number(pcas_ast_t *e) {
	if (e->type == NODE_NUMBER)
		return true;

	if (e->type != NODE_OPERATOR)
		return false;

	switch (optype(e)) {
		case OP_DIV: return true;
		case OP_POW:
		case OP_MULT:
		case OP_FACTORIAL: return starts_with_number(opbase(e));
		default: return false;
	}
}

static ts_box_t *convert(pcas_ast_t *e);

/*Writes a numeric fraction on one line, as in an exponent, or returns NULL if e is not one*/
static ts_box_t *inline_fraction(pcas_ast_t *e) {
	mp_rat value;
	ts_box_t *row;

	if (e->type == NODE_NUMBER && !mp_rat_is_integer(e->op.num)) {
		value = num_Copy(e->op.num);
	} else if (isoptype(e, OP_DIV) && opbase(e)->type == NODE_NUMBER && opbase(e)->next->type == NODE_NUMBER) {
		value = num_Copy(opbase(e)->op.num);
		mp_rat_div(value, opbase(e)->next->op.num, value);
	} else {
		return NULL;
	}

	row = ts_Row();
	if (mp_rat_compare_zero(value) < 0) {
		ts_Append(row, ts_Text("-"));
		mp_rat_abs(value, value);
	}

	ts_Append(row, integer_box(MP_NUMER_P(value)));
	if (!mp_rat_is_integer(value)) {
		ts_Append(row, ts_Text("/"));
		ts_Append(row, integer_box(MP_DENOM_P(value)));
	}

	num_Cleanup(value);
	return row;
}

static ts_box_t *parenthesized(pcas_ast_t *e, bool parens) {
	ts_box_t *b = convert(e);
	return parens ? delimited(b, '(', ')') : b;
}

static ts_box_t *function_box(pcas_ast_t *e) {
	const char *name;
	bool inverse = false;

	switch (optype(e)) {
		case OP_INT: name = "int"; break;
		case OP_SIN_INV: inverse = true; /*FALLTHROUGH*/
		case OP_SIN: name = "sin"; break;
		case OP_COS_INV: inverse = true; /*FALLTHROUGH*/
		case OP_COS: name = "cos"; break;
		case OP_TAN_INV: inverse = true; /*FALLTHROUGH*/
		case OP_TAN: name = "tan"; break;
		case OP_SINH_INV: inverse = true; /*FALLTHROUGH*/
		case OP_SINH: name = "sinh"; break;
		case OP_COSH_INV: inverse = true; /*FALLTHROUGH*/
		case OP_COSH: name = "cosh"; break;
		case OP_TANH_INV: inverse = true; /*FALLTHROUGH*/
		case OP_TANH: name = "tanh"; break;
		default: name = "?"; break;
	}

	return row2(
		inverse ? pair(BOX_SUPERSCRIPT, ts_Text(name), ts_Text("-1")) : ts_Text(name),
		delimited(convert(opbase(e)), '(', ')')
	);
}

static ts_box_t *sum_box(pcas_ast_t *e) {
	ts_box_t *row = ts_Row();
	pcas_ast_t *term;

	for (term = opbase(e); term != NULL; term = term->next) {
		if (term != opbase(e) && is_negative_for_sure(term)) {
			pcas_ast_t *magnitude = ast_Copy(term);
			absolute_val(magnitude);
			ts_Append(row, ts_Text(" - "));
			ts_Append(row, convert(magnitude));
			ast_Cleanup(magnitude);
		} else {
			if (term != opbase(e))
				ts_Append(row, ts_Text(" + "));
			ts_Append(row, convert(term));
		}
	}

	return row;
}

static ts_box_t *product_box(pcas_ast_t *e) {
	ts_box_t *row = ts_Row();
	pcas_ast_t *factor;
	bool empty = true;

	for (factor = opbase(e); factor != NULL; factor = factor->next) {
		if (is_ast_int(factor, 1))
			continue;

		if (!empty && starts_with_number(factor)) {
			char dot[2] = {TS_DOT, '\0'};
			ts_Append(row, ts_Text(dot));
		}

		ts_Append(
			row,
			parenthesized(
				factor,
				isoptype(factor, OP_ADD) || isoptype(factor, OP_EQUALS) || (!empty && is_negative_for_sure(factor))
			)
		);
		empty = false;
	}

	if (empty)
		ts_Append(row, ts_Text("1"));

	return row;
}

static ts_box_t *log_box(pcas_ast_t *e) {
	pcas_ast_t *base = ast_ChildGet(e, 0);
	ts_box_t *name;

	if (base->type == NODE_SYMBOL && base->op.symbol == SYM_EULER)
		name = ts_Text("ln");
	else if (is_ast_int(base, 10))
		name = ts_Text("log");
	else
		name = pair(BOX_SUBSCRIPT, ts_Text("log"), convert(base));

	return row2(name, delimited(convert(ast_ChildGet(e, 1)), '(', ')'));
}

static ts_box_t *operator_box(pcas_ast_t *e) {
	pcas_ast_t *a = opbase(e);
	pcas_ast_t *b = a != NULL ? a->next : NULL;

	switch (optype(e)) {
		case OP_ADD: return sum_box(e);
		case OP_MULT: return product_box(e);
		case OP_DIV: return pair(BOX_FRACTION, convert(a), convert(b));
		case OP_POW: {
			ts_box_t *exponent = inline_fraction(b);
			return pair(BOX_SUPERSCRIPT, parenthesized(a, !is_atom(a)), exponent != NULL ? exponent : convert(b));
		}
		case OP_ROOT:
			if (is_ast_int(a, 2))
				return ts_Append(box_new(BOX_ROOT), convert(b));
			return pair(BOX_SUPERSCRIPT, parenthesized(b, !is_atom(b)), pair(BOX_FRACTION, ts_Text("1"), convert(a)));
		case OP_LOG: return log_box(e);
		case OP_DERIV: {
			ts_box_t *d =
				row2(pair(BOX_FRACTION, ts_Text("d"), row2(ts_Text("d"), convert(b))), delimited(convert(a), '(', ')'));

			if (!ast_Compare(b, b->next)) {
				ts_box_t *at = row2(convert(b), ts_Text("="));
				ts_Append(at, convert(b->next));
				d = pair(BOX_SUBSCRIPT, d, at);
			}

			return d;
		}
		case OP_INTEGRAL: {
			ts_box_t *integrand = row2(parenthesized(a, isoptype(a, OP_ADD)), ts_Text(" d"));
			ts_Append(integrand, convert(b));
			return ts_Append(box_new(BOX_INTEGRAL), integrand);
		}
		case OP_EQUALS: return ts_Append(row2(convert(a), ts_Text(" = ")), convert(b));
		case OP_PRIME: return row2(parenthesized(a, !is_atom(a)), ts_Text("'"));
		case OP_AT: return row2(parenthesized(a, !is_atom(a)), delimited(convert(b), '(', ')'));
		case OP_FACTORIAL: return row2(parenthesized(a, !is_atom(a)), ts_Text("!"));
		case OP_ABS: return delimited(convert(a), '|', '|');
		default: return function_box(e);
	}
}

static ts_box_t *convert(pcas_ast_t *e) {
	if (is_negative_for_sure(e)) {
		pcas_ast_t *magnitude = ast_Copy(e);
		ts_box_t *b;

		absolute_val(magnitude);
		b = row2(ts_Text("-"), convert(magnitude));
		ast_Cleanup(magnitude);

		return b;
	}

	switch (e->type) {
		case NODE_NUMBER: return number_box(e->op.num);
		case NODE_SYMBOL: return symbol_box(e->op.symbol);
		default: return operator_box(e);
	}
}

ts_box_t *ts_FromAst(pcas_ast_t *e) {
	return convert(e);
}

static int max(int a, int b) {
	return a > b ? a : b;
}

static int height(ts_box_t *b) {
	return b->ascent + b->descent;
}

/*Distance from the baseline of a superscript's base up to the baseline of its script*/
static int script_shift(ts_box_t *base, ts_box_t *script, const ts_metrics_t *m) {
	return max(base->ascent - m->script_drop, script->descent + m->script_drop);
}

void ts_Measure(ts_box_t *b, const ts_metrics_t *m) {
	ts_box_t *child, *first = b->first, *second;

	for (child = first; child != NULL; child = child->next)
		ts_Measure(child, m);

	second = first != NULL ? first->next : NULL;

	switch (b->type) {
		case BOX_TEXT:
			b->width = m->text_width(b->text);
			b->ascent = m->ascent;
			b->descent = m->descent;
			break;
		case BOX_ROW:
			b->width = 0;
			b->ascent = m->ascent;
			b->descent = m->descent;
			for (child = first; child != NULL; child = child->next) {
				b->width += child->width;
				b->ascent = max(b->ascent, child->ascent);
				b->descent = max(b->descent, child->descent);
			}
			break;
		case BOX_FRACTION:
			b->width = max(first->width, second->width) + 2 * m->fraction_pad;
			b->ascent = m->axis + m->rule + m->gap + height(first);
			b->descent = max(m->descent, m->gap + height(second) - m->axis);
			break;
		case BOX_SUPERSCRIPT: {
			int shift = script_shift(first, second, m);
			b->width = first->width + second->width;
			b->ascent = max(first->ascent, shift + second->ascent);
			b->descent = max(first->descent, second->descent - shift);
			break;
		}
		case BOX_SUBSCRIPT:
			b->width = first->width + second->width;
			b->ascent = max(first->ascent, second->ascent - m->subscript_drop);
			b->descent = max(first->descent, m->subscript_drop + second->descent);
			break;
		case BOX_ROOT:
			b->ascent = first->ascent + m->gap + m->rule;
			b->descent = first->descent;
			b->width = m->radical_width(height(b)) + first->width;
			break;
		case BOX_DELIMITED: {
			int delimiter = m->delimiter_width(height(first));
			b->width = first->width + (b->left ? delimiter : 0) + (b->right ? delimiter : 0);
			b->ascent = first->ascent;
			b->descent = first->descent;
			break;
		}
		case BOX_INTEGRAL:
			b->ascent = first->ascent + 2 * m->gap;
			b->descent = first->descent + 2 * m->gap;
			b->width = m->integral_width(height(b)) + first->width;
			break;
	}
}

void ts_Draw(ts_box_t *b, int x, int baseline, const ts_metrics_t *m, const ts_renderer_t *r) {
	ts_box_t *child, *first = b->first, *second = first != NULL ? first->next : NULL;
	int top = baseline - b->ascent;

	switch (b->type) {
		case BOX_TEXT: r->text(x, top, b->text); break;
		case BOX_ROW:
			for (child = first; child != NULL; child = child->next) {
				ts_Draw(child, x, baseline, m, r);
				x += child->width;
			}
			break;
		case BOX_FRACTION: {
			int bar = baseline - m->axis - m->rule;
			r->bar(x, bar, b->width);
			ts_Draw(first, x + (b->width - first->width) / 2, bar - m->gap - first->descent, m, r);
			ts_Draw(second, x + (b->width - second->width) / 2, baseline - m->axis + m->gap + second->ascent, m, r);
			break;
		}
		case BOX_SUPERSCRIPT:
			ts_Draw(first, x, baseline, m, r);
			ts_Draw(second, x + first->width, baseline - script_shift(first, second, m), m, r);
			break;
		case BOX_SUBSCRIPT:
			ts_Draw(first, x, baseline, m, r);
			ts_Draw(second, x + first->width, baseline + m->subscript_drop, m, r);
			break;
		case BOX_ROOT: {
			int radical = m->radical_width(height(b));
			r->radical(x, top, height(b));
			r->overline(x + radical, top, first->width);
			ts_Draw(first, x + radical, baseline, m, r);
			break;
		}
		case BOX_DELIMITED: {
			int delimiter = m->delimiter_width(height(b));
			if (b->left) {
				r->delimiter(x, top, height(b), b->left);
				x += delimiter;
			}
			ts_Draw(first, x, baseline, m, r);
			if (b->right)
				r->delimiter(x + first->width, top, height(b), b->right);
			break;
		}
		case BOX_INTEGRAL:
			r->integral(x, top, height(b));
			ts_Draw(first, x + m->integral_width(height(b)), baseline, m, r);
			break;
	}
}

void ts_Cleanup(ts_box_t *b) {
	while (b != NULL) {
		ts_box_t *next = b->next;
		ts_Cleanup(b->first);
		free(b->text);
		free(b);
		b = next;
	}
}

bool ts_Equal(ts_box_t *a, ts_box_t *b) {
	for (; a != NULL && b != NULL; a = a->next, b = b->next) {
		if (a->type != b->type || a->left != b->left || a->right != b->right)
			return false;
		if ((a->text == NULL) != (b->text == NULL) || (a->text != NULL && strcmp(a->text, b->text) != 0))
			return false;
		if (!ts_Equal(a->first, b->first))
			return false;
	}

	return a == NULL && b == NULL;
}
