#include "dbg.hxx"

#ifdef DEBUG
#ifndef COMPILE_PC

void ti_debug(const char *format, ...) {
	va_list args;
	va_start(args, format);

	vsprintf(dbgout, format, args);

	va_end(args);
}

#endif
#endif

const char *operators[static_cast<size_t>(Op::Amount)] = {"+",     "*",    "/",     "^",    "root",  "log",  "deriv",
														  "integ", "=",    "'",     "at",   "sum",   "sub",  "!",
														  "int",   "abs",  "sin",   "asin", "cos",   "acos", "tan",
														  "atan",  "sinh", "asinh", "cosh", "acosh", "tanh", "atanh"};

const char *symbols[] = {"i", "pi", "e", "theta"};

void _print_tree(const ast *e, unsigned indent, unsigned index) {
	if (e == nullptr)
		return;

	for (unsigned i = 0; i < indent * index; i++)
		DBG((" "));

	switch (e->type()) {
		case ast::Type::Number: {
			char *num = e->num().toString(6);
			DBG(("NUMBER: %s\n", num));
			free(num);
			break;
		}
		case ast::Type::Symbol:
			if (e->symbol() >= Sym::Imag && e->symbol() <= Sym::Theta)
				DBG(("SYMBOL: %s\n", symbols[static_cast<unsigned>(e->symbol()) - static_cast<unsigned>(Sym::Imag)]));
			else
				DBG(("SYMBOL: %c\n", static_cast<char>(e->symbol())));
			break;
		case ast::Type::Operator: {
			DBG(("OPERATOR: %s\n", operators[static_cast<unsigned>(e->op())]));
			for (const ast &current : e->children()) {
				_print_tree(&current, indent, index + 1);
			}
			break;
		}
	}
}

void dbg_print_tree(const ast &e, unsigned indent) {
	_print_tree(&e, indent, 0);
}

unsigned dbg_count_nodes(const ast &e) {
	unsigned amount = 1;

	if (e.isOperator()) {
		for (const ast &current : e.children())
			amount += dbg_count_nodes(current);
	}

	return amount;
}
