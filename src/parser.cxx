#include "parser.hxx"

#include "stack.hxx"

/*http://tibasicdev.wikidot.com/one-byte-tokens*/
/* clang-format off */
const TokenTable ti_table = {{
    {0, {0}}, {0, {0}},         /*Tok::Number, Tok::Symbol*/

    {1, {0x70}}, {1, {0x71}},   /*Tok::Plus, Tok::Minus*/
    {1, {0x82}}, {1, {0x83}},   /*Tok::Multiply, Tok::Divide*/
    {2, {0xEF, 0x2E}},          /*Tok::Fraction*/
    {2, {0xEF, 0x2F}},          /*Tok::Proper*/
    {1, {0xF0}},                /*Tok::Power*/
    {1, {0x3B}},                /*Tok::Scientific*/
    {1, {0xF1}},                /*Tok::Root*/
    {1, {0x6A}},                /*Tok::Equals*/

    {1, {0xB0}},                            /*Tok::Negate*/
    {1, {0x0C}}, {1, {0x0D}}, {1, {0x0F}},  /*Tok::Reciprocal, Tok::Square, Tok::Cube*/
    {1, {0x2D}},                            /*Tok::Factorial*/
    {1, {0xAE}},                            /*Tok::Prime*/

    {2, {0xEF, 0x34}},          /*Tok::LogBase*/
    {1, {0x25}},                /*Tok::Deriv*/
    {1, {0x24}},                /*Tok::Integral*/

    {1, {0xB1}}, {1, {0xB2}},   /*Tok::Int, Tok::Abs*/
    {1, {0xBC}}, {1, {0xBD}},   /*Tok::Sqrt, Tok::CubedRoot*/
    {1, {0xBE}}, {1, {0xBF}},   /*Tok::Ln, Tok::EToPower*/
    {1, {0xC0}}, {1, {0xC1}},   /*Tok::Log, Tok::TenToPower*/
    {1, {0xC2}}, {1, {0xC3}},   /*Tok::Sin, Tok::Sin_Inv*/
    {1, {0xC4}}, {1, {0xC5}},   /*Tok::Cos, Tok::Cos_Inv*/
    {1, {0xC6}}, {1, {0xC7}},   /*Tok::Tan, Tok::Tan_Inv*/
    {1, {0xC8}}, {1, {0xC9}},   /*Tok::SinH, Tok::SinH_Inv*/
    {1, {0xCA}}, {1, {0xCB}},   /*Tok::CosH, Tok::CosH_Inv*/
    {1, {0xCC}}, {1, {0xCD}},   /*Tok::TanH, Tok::TanH_Inv*/

    {1, {0x10}}, {1, {0x11}},   /*Tok::OpenPar, Tok::ClosePar*/
    {1, {0x2B}}, {1, {0x3A}},   /*Tok::Comma, Tok::Period*/

    {1, {0x2C}},                /*Tok::Imag*/

    {2, {0xBB, 0x31}},          /*TOK_E*/
    {1, {0xAC}}, {1, {0x5B}}    /*Tok::Pi, Tok::Theta*/
}};

const TokenTable str_table = {{
    {0, {0}}, {0, {0}},             /*Tok::Number, Tok::Symbol*/

    {1, "+"}, {1, "_"},             /*Tok::Plus, Tok::Minus*/
    {1, "*"}, {1, "/"},             /*Tok::Multiply, Tok::Divide*/

    {1, "/"},                       /*Tok::Fraction*/
    {1, "+"},                       /*Tok::Proper*/
    {1, "^"},                       /*Tok::Power*/
    {3, "[E]"},                     /*Tok::Scientific*/
    {4, "root"},                    /*Tok::Root*/
    {1, "="},                       /*Tok::Equals*/

    {1, "-"},                               /*Tok::Negate*/
    {5, "^(-1)"}, {2, "^2"}, {2, "^3"},     /*Tok::Reciprocal, Tok::Square, Tok::Cube*/
    {1, "!"},                               /*Tok::Factorial*/
    {1, "'"},                               /*Tok::Prime*/

    {5, "logb("},                   /*Tok::LogBase*/
    {6, "deriv("},                  /*Tok::Deriv*/
    {6, "integ("},                  /*Tok::Integral*/

    {4, "int("}, {4, "abs("},       /*Tok::Int, Tok::Abs*/
    {5, "sqrt("}, {7, "cbrt("},     /*Tok::Sqrt, Tok::CubedRoot*/
    {3, "ln("}, {3, "e^("},         /*Tok::Ln, Tok::EToPower*/
    {4, "log("}, {4, "10^("},       /*Tok::Log, Tok::TenToPower*/
    {4, "sin("}, {5, "asin("},      /*Tok::Sin, Tok::Sin_Inv*/
    {4, "cos("}, {5, "acos("},      /*Tok::Cos, Tok::Cos_Inv*/
    {4, "tan("}, {5, "atan("},      /*Tok::Tan, Tok::Tan_Inv*/
    {5, "sinh("}, {6, "asinh("},    /*Tok::SinH, Tok::SinH_Inv*/
    {5, "cosh("}, {6, "acosh("},    /*Tok::SinH, Tok::SinH_Inv*/
    {5, "tanh("}, {6, "atanh("},    /*Tok::TanH, Tok::TanH_Inv*/

    {1, "("}, {1, ")"},             /*Tok::OpenPar, Tok::ClosePar*/
    {1, ","}, {1, "."},             /*Tok::Comma, Tok::Period*/

    {1, "i"},                       /*Tok::Imag */

    {1, "e"},                       /*TOK_E*/
    {2, "pi"}, {5, "theta"}         /*Tok::Pi, Tok::Theta*/

}};

/* clang-format on */

struct Token {
	Tok type;

	union {
		mp_rat num;
		Sym symbol;
	};
};

struct Tokenizer {
	unsigned amount;
	Token *tokens;
};

/*'0' through '9' and including '.'*/
static bool is_num(uint8_t byte, const TokenTable &lookup) {
	return (byte >= '0' && byte <= '9') || byte == lookup[Tok::Period].bytes[0];
}

mp_rat read_num(
	const uint8_t *equation,
	unsigned index,
	unsigned length,
	const TokenTable &lookup,
	unsigned *consumed
) {
	unsigned size = 0;

	for (unsigned i = index; i < length; i++) {
		if (is_num(equation[i], lookup))
			size++;
		else
			break;
	}

	char *buffer = static_cast<char *>(malloc(size + 1));

	/*Copy digits, but replace Ti's '.' with ascii '.'*/
	for (unsigned i = 0; i < size; i++) {
		const char digit = equation[i + index];

		if (digit == lookup[Tok::Period].bytes[0])
			buffer[i] = '.';
		else
			buffer[i] = digit;
	}

	buffer[size] = '\0';

	mp_rat num = num_FromString(buffer);

	free(buffer);

	*consumed = size;

	return num;
}

Tok read_type(const uint8_t *equation, unsigned index, unsigned length, const TokenTable &lookup, unsigned *consumed) {
	for (unsigned i = static_cast<unsigned>(Tok::Plus); i < static_cast<unsigned>(Tok::Amount); i++) {
		const Tok tok = static_cast<Tok>(i);
		const Identifier &current = lookup[tok];
		bool matches = true;

		/*If there is not enough bytes left in the equation for this multi-byte token*/
		if (length - index < current.length)
			continue;

		/*Check if each byte matches*/
		for (unsigned j = index; j < index + current.length; j++) {
			matches &= equation[j] == current.bytes[j - index];
			if (!matches)
				break;
		}

		if (matches) {
			*consumed = current.length;
			return tok;
		}
	}

	return Tok::Invalid;
}

Sym read_symbol(
	const uint8_t *equation,
	unsigned index,
	unsigned length,
	const TokenTable &lookup,
	unsigned *consumed
) {
	/*Sym letter enum values are mapped to their ascii code (Sym::B == 'B')*/
	if (equation[index] >= 'A' && equation[index] <= 'Z') {
		*consumed = 1;
		return static_cast<Sym>(equation[index]);
	}

	switch (read_type(equation, index, length, lookup, consumed)) {
		case Tok::Imag: return Sym::Imag;
		case Tok::Euler: return Sym::Euler;
		case Tok::Pi: return Sym::Pi;
		case Tok::Theta: return Sym::Theta;
		default: return Sym::Invalid;
	}
}

Token read_token(
	const uint8_t *equation,
	unsigned index,
	unsigned length,
	const TokenTable &lookup,
	unsigned *consumed
) {
	Token tok;

	if (is_num(equation[index], lookup)) {
		tok.type = Tok::Number;
		tok.num = read_num(equation, index, length, lookup, consumed);

	} else if (read_symbol(equation, index, length, lookup, consumed) != Sym::Invalid) {
		tok.type = Tok::Symbol;
		tok.symbol = read_symbol(equation, index, length, lookup, consumed);

	} else {
		tok.type = read_type(equation, index, length, lookup, consumed);

		/*Skip over invalid bytes if choose to not catch error*/
		if (tok.type == Tok::Invalid)
			*consumed = 1;
	}

	return tok;
}

Error _tokenize(
	Token *tokens,
	const uint8_t *equation,
	unsigned length,
	unsigned *tok_amount,
	const TokenTable &lookup
) {
	unsigned token_index = 0;
	unsigned i = 0;

	while (i < length) {
		unsigned consumed;
		const Token tok = read_token(equation, i, length, lookup, &consumed);

		if (tok.type == Tok::Invalid)
			return Error::TokInvalid;

		if (tokens != nullptr) {
			tokens[token_index] = tok;
		} else if (tok.type == Tok::Number) {
			/*Clean up the number if it's not being saved*/
			num_Cleanup(tok.num);
		}

		token_index++;
		i += consumed;
	}

	*tok_amount = token_index;

	return Error::Success;
}

Error tokenize(Tokenizer *t, const uint8_t *equation, unsigned length, const TokenTable &lookup) {
	/*Determine the amount of tokens to malloc()*/
	Error err = _tokenize(nullptr, equation, length, &t->amount, lookup);

	if (err != Error::Success)
		return err;

	/*Malloc and fill tokens*/
	t->tokens = static_cast<Token *>(malloc(t->amount * sizeof(Token)));
	err = _tokenize(t->tokens, equation, length, &t->amount, lookup);

	return err;
}

/*Larger = Higher precedence*/
uint8_t precedence(Tok type) {
	switch (type) {
		case Tok::Equals: return 1;
		case Tok::Plus:
		case Tok::Minus: return 5;
		case Tok::Multiply:
		case Tok::Negate:
		case Tok::Divide:
		case Tok::Fraction: return 10;
		case Tok::Power:
		case Tok::Reciprocal:
		case Tok::Square:
		case Tok::Cube:
		case Tok::Root:
		case Tok::Factorial:
		case Tok::Prime: return 15;
		case Tok::Scientific: return 20;
		default: return 0;
	}
}

/*
Determine if we should insert a multiply operator for a symbol/number with the next token.
For example: 5(2 + 3) and 5x
*/
bool should_multiply_by_next_token(Tokenizer *tokenizer, unsigned index) {
	if (index + 1 < tokenizer->amount) {
		const Token next = tokenizer->tokens[index + 1];

		/*5(2 + x)*/
		return next.type == Tok::OpenPar
			   /*5x or x5. We can infer two number tokens will never follow each other.*/
			   || (next.type == Tok::Number ||
				   next.type == Tok::Symbol
				   /*Crazy, but technically valid TI syntax*/
				   || next.type == Tok::Negate
				   /*5int(2.5)*/
				   || is_tok_function(next.type));
	}

	return false;
}

/*How many parameters for the function or operator*/
uint8_t operand_count(Tok type) {
	if (is_tok_unary_operator(type) || is_tok_unary_function(type))
		return 1;
	if (is_tok_binary_operator(type))
		return 2;

	if (type == Tok::LogBase || type == Tok::Integral)
		return 2;

	if (type == Tok::Deriv)
		return 3;

	return 0;
}

/*Changes the Tok into the Op and fixes operands*/
void translate(ast *e, Tok type) {
	switch (type) {
		case Tok::Plus: e->setOp(Op::Add); break;
		case Tok::Minus: {
			e->setOp(Op::Add);
			e->insertChild(ast::make(Op::Mult, ast::make(num_FromInt(-1)), e->removeChildAt(1)), 0);
			break;
		}
		case Tok::Multiply: e->setOp(Op::Mult); break;
		case Tok::Divide: e->setOp(Op::Div); break;
		case Tok::Fraction: e->setOp(Op::Div); break;
		case Tok::Proper: e->setOp(Op::Add); break;
		case Tok::Power: e->setOp(Op::Pow); break;
		case Tok::Scientific: {
			e->setOp(Op::Mult);

			ast *op2 = ast::make(Op::Pow, ast::make(num_FromInt(10)), e->childAt(1));
			e->removeChildAt(1);
			e->appendChild(op2);

			break;
		}
		case Tok::Root: e->setOp(Op::Root); break;
		case Tok::Equals: e->setOp(Op::Equals); break;
		case Tok::Prime: e->setOp(Op::Prime); break;
		case Tok::Negate:
			e->setOp(Op::Mult);
			e->insertChild(ast::make(num_FromInt(-1)), 0);
			break;
		case Tok::Reciprocal:
			e->setOp(Op::Pow);
			e->appendChild(ast::make(num_FromInt(-1)));
			break;
		case Tok::Square:
			e->setOp(Op::Pow);
			e->appendChild(ast::make(num_FromInt(2)));
			break;
		case Tok::Cube:
			e->setOp(Op::Pow);
			e->appendChild(ast::make(num_FromInt(3)));
			break;
		case Tok::Factorial: e->setOp(Op::Factorial); break;
		case Tok::LogBase:
			e->setOp(Op::Log);
			/*Swap the operands*/
			e->appendChild(e->removeChildAt(0));
			break;
		case Tok::Deriv: e->setOp(Op::Deriv); break;
		case Tok::Integral: e->setOp(Op::Integral); break;
		case Tok::Int: e->setOp(Op::Int); break;
		case Tok::Abs: e->setOp(Op::Abs); break;
		case Tok::Sqrt:
			e->setOp(Op::Root);
			e->insertChild(ast::make(num_FromInt(2)), 0);
			break;
		case Tok::CubedRoot:
			e->setOp(Op::Root);
			e->insertChild(ast::make(num_FromInt(3)), 0);
			break;
		case Tok::Ln:
			e->setOp(Op::Log);
			e->insertChild(ast::make(Sym::Euler), 0);
			break;
		case Tok::EToPower:
			e->setOp(Op::Pow);
			e->insertChild(ast::make(Sym::Euler), 0);
			break;
		case Tok::Log:
			e->setOp(Op::Log);
			e->insertChild(ast::make(num_FromInt(10)), 0);
			break;
		case Tok::TenToPower:
			e->setOp(Op::Pow);
			e->insertChild(ast::make(num_FromInt(10)), 0);
			break;
		case Tok::Sin: e->setOp(Op::Sin); break;
		case Tok::Sin_Inv: e->setOp(Op::Sin_Inv); break;
		case Tok::Cos: e->setOp(Op::Cos); break;
		case Tok::Cos_Inv: e->setOp(Op::Cos_Inv); break;
		case Tok::Tan: e->setOp(Op::Tan); break;
		case Tok::Tan_Inv: e->setOp(Op::Tan_Inv); break;
		case Tok::SinH: e->setOp(Op::SinH); break;
		case Tok::SinH_Inv: e->setOp(Op::SinH_Inv); break;
		case Tok::CosH: e->setOp(Op::CosH); break;
		case Tok::CosH_Inv: e->setOp(Op::CosH_Inv); break;
		case Tok::TanH: e->setOp(Op::TanH); break;
		case Tok::TanH_Inv: e->setOp(Op::TanH_Inv); break;
		default: break;
	}
}

bool collapse_precedence(Stack<const Token *> &operators, Stack<ast *> &expressions, Tok type) {
	while (!operators.empty()) {
		/*Break when we meet the corresponding (*/
		if (type == Tok::ClosePar && operators.peek()->type == Tok::OpenPar)
			break;
		/*Break when we meet the function that the , belongs to*/
		else if (type == Tok::Comma && is_tok_nary_function(operators.peek()->type))
			break;
		/*Break if the precedence is lower than the type*/
		else if (type != Tok::ClosePar && type != Tok::Comma && precedence(operators.peek()->type) < precedence(type))
			break;

		const Token *op = operators.pop();

		/*Occurs when we collapse_all() at the end with not enough closing
        parentheses. This is an acceptable TI format, so we accept it too.*/
		if (op->type == Tok::OpenPar)
			continue;

		/*Store the token type into the operand type. This will
        Be fixed in make_operator*/
		ast *collapsed = ast::make(Op::Amount);
		for (unsigned i = 0; i < operand_count(op->type); i++) {
			ast *operand = expressions.pop();

			if (operand == nullptr) {
				ast::dispose(collapsed);
				return false;
			}

			collapsed->insertChild(operand, 0);
		}

		translate(collapsed, op->type);

		expressions.push(collapsed);
	}

	return true;
}

bool collapse_all(Stack<const Token *> &operators, Stack<ast *> &expressions) {
	return collapse_precedence(operators, expressions, Tok::Number);
}

#define parse_assert(expression, err)                                                                                  \
	if (!(expression)) {                                                                                               \
		while (!expressions.empty())                                                                                   \
			ast::dispose(expressions.pop());                                                                           \
                                                                                                                       \
		free(tokenizer.tokens);                                                                                        \
                                                                                                                       \
		*e = err;                                                                                                      \
		return nullptr;                                                                                                \
	}

unsigned parse_list(
	const uint8_t *equation,
	unsigned length,
	const TokenTable &lookup,
	ast **items,
	unsigned max,
	Error *err
) {
	unsigned i = 0, start = 0, depth = 0, count = 0;

	*err = Error::Success;

	while (true) {
		unsigned consumed = 1;
		bool end = i >= length;

		if (!end) {
			const Token tok = read_token(equation, i, length, lookup, &consumed);

			if (tok.type == Tok::Number)
				num_Cleanup(tok.num);

			if (tok.type == Tok::OpenPar || is_tok_function(tok.type))
				depth++;
			else if (tok.type == Tok::ClosePar && depth > 0)
				depth--;
			else if (tok.type == Tok::Comma && depth == 0)
				end = true;
		}

		if (end) {
			if (count == max) {
				*err = Error::ParseBadComma;
			} else {
				items[count] = parse(equation + start, i - start, lookup, err);
				if (*err == Error::Success)
					count++;
			}

			if (*err != Error::Success) {
				while (count > 0)
					ast::dispose(items[--count]);
				return 0;
			}

			if (i >= length)
				return count;

			start = i + consumed;
		}

		i += consumed;
	}
}

ast *parse(const uint8_t *equation, unsigned length, const TokenTable &lookup, Error *e) {
	Tokenizer tokenizer = {0};

	*e = tokenize(&tokenizer, equation, length, lookup);

	if (*e != Error::Success)
		return nullptr;

	Stack<const Token *> operators;
	Stack<ast *> expressions;

	/*Create instances to push on the stacks as pointers*/
	Token mult = {Tok::Multiply};
	Token open_par = {Tok::OpenPar};

	for (unsigned i = 0; i < tokenizer.amount; i++) {
		Token *tok = &tokenizer.tokens[i];

		if (tok->type == Tok::OpenPar) {
			operators.push(tok);
		} else if (tok->type == Tok::Number || tok->type == Tok::Symbol) {
			if (tok->type == Tok::Number) {
				expressions.push(ast::make(tok->num));
			} else {
				expressions.push(ast::make(tok->symbol));
			}

			if (should_multiply_by_next_token(&tokenizer, i)) {
				parse_assert(collapse_precedence(operators, expressions, Tok::Multiply), Error::ParseBadOperator);
				operators.push(&mult);
			}

		} else if (is_tok_unary_operator(tok->type)) {
			/*Or other left unary operators*/
			if (tok->type != Tok::Negate) {
				parse_assert(collapse_precedence(operators, expressions, tok->type), Error::ParseBadOperator);
				operators.push(tok);

				if (should_multiply_by_next_token(&tokenizer, i)) {
					parse_assert(collapse_precedence(operators, expressions, Tok::Multiply), Error::ParseBadOperator);
					operators.push(&mult);
				}
			} else {
				operators.push(tok);
			}

		} else if (is_tok_binary_operator(tok->type)) {
			parse_assert(collapse_precedence(operators, expressions, tok->type), Error::ParseBadOperator);
			operators.push(tok);
		} else if (is_tok_function(tok->type)) {
			/*Insert a ( to correspond with the other closing ) following the parameters*/
			operators.push(&open_par);
			operators.push(tok);
		} else if (tok->type == Tok::ClosePar) {
			parse_assert(collapse_precedence(operators, expressions, Tok::ClosePar), Error::ParseBadOperator);
			parse_assert(!operators.empty() && operators.peek()->type == Tok::OpenPar, Error::ParseUnmatchedClosePar);

			operators.pop();

			if (should_multiply_by_next_token(&tokenizer, i)) {
				parse_assert(collapse_precedence(operators, expressions, Tok::Multiply), Error::ParseBadOperator);
				operators.push(&mult);
			}
		} else if (tok->type == Tok::Comma) {
			parse_assert(collapse_precedence(operators, expressions, Tok::Comma), Error::ParseBadOperator);
			parse_assert(!operators.empty() && is_tok_function(operators.peek()->type), Error::ParseBadComma);
		}
	}

	parse_assert(collapse_all(operators, expressions), Error::ParseBadOperator);

	ast *root = expressions.pop();

	parse_assert(operators.empty(), Error::Generic);
	parse_assert(expressions.empty(), Error::Generic);

	free(tokenizer.tokens);

	return root;
}
