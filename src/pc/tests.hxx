#pragma once

#include <stdio.h>
#include <stdbool.h>

/*Max length for one parameter*/
constexpr int MAX_PAR = 256;
/*Max length for a line in the file*/
constexpr int MAX_LINE = MAX_PAR * 3;
/*Max tests we will have*/
constexpr int MAX_TESTS = 1024;

enum class TestType : unsigned char {
	Simplify,
	Gcd,
	Factor,
	Expand,
	Deriv,
	Integral,
	DeOrder,
	DeLinear,
	DeNonlinear,
	DeSolves,
	DeNotSolves,
	DeSolve,
	/*Like DeSolve with the power series method*/
	DeSeries,

	Invalid
};

struct Test {
	TestType type;

	char arg1[MAX_PAR], arg2[MAX_PAR], arg3[MAX_PAR];

	unsigned line;
};

/*Returns one test from a line*/
Test *test_Parse(char *line);
/*Returns an array of tests from a file*/
Test **test_Load(char *file, unsigned *len);

bool test_Run(Test *t);
void test_Cleanup(Test *t);
void test_CleanupArr(Test **arr, unsigned len);
void test_Print(Test *t);
