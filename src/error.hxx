#pragma once

enum class Error : unsigned char {
	Success,

	/*Because of bad programming*/
	Generic,

	/*trying to add nodes to a number node for instance*/
	AstNotAllowed,
	AstOutOfBounds,

	/*Tokenizing and parsing*/
	TokInvalid,

	ParseBadOperator,
	ParseUnmatchedClosePar,
	ParseBadComma,

	EvalNoMapping,

	/*Differential equations*/
	DeNoDerivative,
	DeBadFunction,
	DeOrder,
	DeBadCondition,
	DeImplicit,
	DeUnsolved,
	DeIntegral,
	DeRoots,
	DeNoSolution,
	DeNotSolution,
	DeSingular,

	Amount
};

const char *error_text(Error error);
