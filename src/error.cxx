#include "error.hxx"
#include "countof.hxx"

static const char *const messages[] = {
	"Success",

	"Generic failure",

	"AST operation not allowed",
	"AST index out of bounds",

	"Invalid token",

	"Bad operator",
	"Unmatched closing parentheses",
	"Bad comma",

	"No mapping for symbol",

	"No derivative in equation",
	"Primes must follow one function",
	"Order too high",
	"Bad initial condition",
	"Solution must be explicit",
	"No method for this DE yet",
	"Unable to integrate",
	"Unable to find the roots",
	"No solution meets the conditions",
	"Given function is not a solution",
	"Not an ordinary point",
};

static_assert(countof(messages) == static_cast<unsigned>(Error::Amount));

const char *error_text(Error error) {
	return messages[static_cast<unsigned>(error)];
}
