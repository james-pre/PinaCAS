#pragma once

#include "ast.hxx"
#include "error.hxx"

/*Ti Tokens, used for reading from yvars*/
enum class Tok : unsigned char {
	Number,
	Symbol, /*numbers, variables, pi, e*/

	Plus,
	Minus,
	Multiply,
	Divide,
	Fraction, /*special n/d for ti pretty print*/
	Proper,   /*special Un/d for ti pretty print*/
	Power,
	Scientific,
	Root,
	Equals,

	/*Unary*/
	Negate,
	Reciprocal,
	Square,
	Cube,
	Factorial,
	Prime,

	LogBase,
	Deriv,
	Integral,

	Int,
	Abs,

	Sqrt,
	CubedRoot,

	Ln,
	EToPower,
	Log,
	TenToPower,

	Sin,
	Sin_Inv,
	Cos,
	Cos_Inv,
	Tan,
	Tan_Inv,
	SinH,
	SinH_Inv,
	CosH,
	CosH_Inv,
	TanH,
	TanH_Inv,

	OpenPar,
	ClosePar,
	Comma,
	Period,

	Imag,

	Euler,
	Pi,
	Theta,

	Amount,
	Invalid
};

constexpr bool is_tok_unary_operator(Tok tok) {
	return tok >= Tok::Negate && tok <= Tok::Prime;
}

constexpr bool is_tok_binary_operator(Tok tok) {
	return tok >= Tok::Plus && tok <= Tok::Equals;
}

constexpr bool is_tok_operator(Tok tok) {
	return is_tok_unary_operator(tok) || is_tok_binary_operator(tok);
}

constexpr bool is_tok_unary_function(Tok tok) {
	return tok >= Tok::Int && tok <= Tok::TanH_Inv;
}

constexpr bool is_tok_nary_function(Tok tok) {
	return tok == Tok::LogBase || tok == Tok::Deriv || tok == Tok::Integral;
}

constexpr bool is_tok_function(Tok tok) {
	return is_tok_unary_function(tok) || is_tok_nary_function(tok);
}

#define MAX_IDENTIFIER_LEN 7

struct Identifier {
	uint8_t length;
	uint8_t bytes[MAX_IDENTIFIER_LEN];
};

/*The identifier of each token*/
struct TokenTable {
	Identifier entries[static_cast<unsigned>(Tok::Amount)];

	const Identifier &operator[](Tok tok) const {
		return entries[static_cast<unsigned>(tok)];
	}
};

extern const TokenTable ti_table;
extern const TokenTable str_table;

ast *parse(const uint8_t *equation, unsigned length, const TokenTable &lookup, Error *e);
/*Parses up to max expressions separated by commas outside of parentheses into items. Returns how many were parsed, or 0 on error.*/
unsigned parse_list(
	const uint8_t *equation,
	unsigned length,
	const TokenTable &lookup,
	ast **items,
	unsigned max,
	Error *err
);
uint8_t *export_to_binary(const ast *e, unsigned *len, const TokenTable &lookup, Error *err);
