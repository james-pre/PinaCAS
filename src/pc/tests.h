#pragma once

#include <stdio.h>
#include <stdbool.h>

/*Max length for one parameter*/
#define MAX_PAR 256
/*Max length for a line in the file*/
#define MAX_LINE (MAX_PAR * 3)
/*Max tests we will have*/
#define MAX_TESTS 1024

typedef enum : unsigned char {
	TEST_SIMPLIFY,
	TEST_GCD,
	TEST_FACTOR,
	TEST_EXPAND,
	TEST_DERIV,
	TEST_INTEGRAL,
	TEST_DE_ORDER,
	TEST_DE_LINEAR,
	TEST_DE_NONLINEAR,
	TEST_DE_SOLVES,
	TEST_DE_NOT_SOLVES,
	TEST_DE_SOLVE,
	/*Like TEST_DE_SOLVE with the power series method*/
	TEST_DE_SERIES,

	TEST_INVALID
} TestType;

typedef struct {
	TestType type;

	char arg1[MAX_PAR], arg2[MAX_PAR], arg3[MAX_PAR];

	unsigned line;

} test_t;

/*Returns one test from a line*/
test_t *test_Parse(char *line);
/*Returns an array of tests from a file*/
test_t **test_Load(char *file, unsigned *len);

bool test_Run(test_t *t);
void test_Cleanup(test_t *t);
void test_CleanupArr(test_t **arr, unsigned len);
void test_Print(test_t *t);
