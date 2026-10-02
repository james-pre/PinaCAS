#ifdef COMPILE_PC

#include "tests.h"

#include <string.h>
#include <stdlib.h>

#include "../parser.h"
#include "../ast.h"
#include "../cas/cas.h"

#include "../dbg.h"

/*Trim null terminated string*/
static char *trim(char *str) {
	unsigned i, trimmed_len = 0, trim_index = 0;
	char *trimmed;

	for (i = 0; i < strlen(str) + 1; i++) {
		if (str[i] != ' ' && str[i] != '\t' && str[i] != '\n')
			trimmed_len++;
	}

	trimmed = malloc(trimmed_len * sizeof(char));

	for (i = 0; i < strlen(str) + 1; i++) {
		if (str[i] != ' ' && str[i] != '\t' && str[i] != '\n')
			trimmed[trim_index++] = str[i];
	}

	return trimmed;
}

TestType resolve_type(char *type) {
	if (!strcmp(type, "simplify"))
		return TEST_SIMPLIFY;
	if (!strcmp(type, "gcd"))
		return TEST_GCD;
	if (!strcmp(type, "factor"))
		return TEST_FACTOR;
	if (!strcmp(type, "expand"))
		return TEST_EXPAND;
	if (!strcmp(type, "deriv"))
		return TEST_DERIV;
	if (!strcmp(type, "integ"))
		return TEST_INTEGRAL;
	if (!strcmp(type, "order"))
		return TEST_DE_ORDER;
	if (!strcmp(type, "linear"))
		return TEST_DE_LINEAR;
	if (!strcmp(type, "nonlinear"))
		return TEST_DE_NONLINEAR;
	if (!strcmp(type, "solves"))
		return TEST_DE_SOLVES;
	if (!strcmp(type, "notsolves"))
		return TEST_DE_NOT_SOLVES;

	return TEST_INVALID;
}

test_t *test_Parse(char *line) {
	char *pt, *trimmed;
	unsigned i;
	test_t *t = malloc(sizeof(test_t));

	pt = strtok(line, ";");
	for (i = 0; i < 3; i++) {
		if (pt == NULL) {
			free(t);
			return NULL;
		}

		trimmed = trim(pt);

		if (i == 0)
			t->type = resolve_type(trimmed);
		else if (i == 1)
			strncpy(t->arg1, trimmed, MAX_PAR);
		else if (i == 2)
			strncpy(t->arg2, trimmed, MAX_PAR);

		free(trimmed);

		pt = strtok(NULL, ";");
	}

	if (pt != NULL) {
		trimmed = trim(pt);
		strncpy(t->arg3, trimmed, MAX_PAR);
		free(trimmed);
	} else {
		t->arg3[0] = '\0';
	}

	return t;
}

test_t **test_Load(char *file, unsigned *len) {
	FILE *f;
	unsigned i = 0, cur_line = 1;
	test_t **arr;
	char line[MAX_LINE];

	arr = malloc(sizeof(test_t **) * MAX_TESTS);

	f = fopen(file, "r");

	if (f == NULL) {
		free(arr);
		return NULL;
	}

	while (fgets(line, sizeof(line), f) != NULL) {
		test_t *t;

		if (i >= MAX_TESTS)
			break;

		t = test_Parse(line);

		if (t != NULL && t->type != TEST_INVALID) {
			arr[i] = t;

			t->line = cur_line;

			i++;
		} else if (t != NULL) {
			free(t);
		}

		cur_line++;
	}

	fclose(f);

	*len = i;

	return arr;
}

bool check(test_t *t, pcas_ast_t *actual, pcas_ast_t *expected) {
	char *output_expected, *output_actual;
	unsigned expected_len, actual_len;

	pcas_error_t expted_err, actual_err;

	if (!ast_Compare(expected, actual)) {
		output_expected = (char *)export_to_binary(expected, &expected_len, str_table, &expted_err);
		output_actual = (char *)export_to_binary(actual, &actual_len, str_table, &actual_err);

		if (expted_err != E_SUCCESS || actual_err != E_SUCCESS) {
			printf("Test failed on line %u. Error when exporting ast to text.\n", t->line);
		} else {
			printf(
				"Test failed on line %u. Expected %.*s but got %.*s\n",
				t->line,
				expected_len,
				output_expected,
				actual_len,
				output_actual
			);

			free(output_expected);
			free(output_actual);
		}

		printf("Expected:\n");
		dbg_print_tree(expected, 4);
		printf("Actual:\n");
		dbg_print_tree(actual, 4);

		return false;
	}

	return true;
}

#define MAX_ITEMS (DE_MAX_CONDITIONS + 1)

/*arg1 is an equation with initial conditions, arg2 a solution, and arg3 the independent variable*/
static bool run_verify(test_t *t) {
	pcas_ast_t *items[MAX_ITEMS], *solution, *x;
	pcas_error_t err;
	pcas_de_t de;
	unsigned count, i;
	bool satisfied = false, passed = false;

	count = parse_list((uint8_t *)t->arg1, strlen(t->arg1), str_table, items, MAX_ITEMS, &err);
	solution = parse((uint8_t *)t->arg2, strlen(t->arg2), str_table, &err);
	x = parse((uint8_t *)t->arg3, strlen(t->arg3), str_table, &err);

	if (count == 0 || items[0] == NULL || solution == NULL || x == NULL) {
		printf("Test failed on line %u. Unable to parse arguments.\n", t->line);
	} else if (
		(err = de_LoadList(&de, items, count, x)) != E_SUCCESS ||
		(err = de_Verify(&de, solution, &satisfied)) != E_SUCCESS
	) {
		printf("Test failed on line %u. %s\n", t->line, error_text[err]);
		de_Cleanup(&de);
	} else {
		passed = satisfied == (t->type == TEST_DE_SOLVES);
		if (!passed)
			printf("Test failed on line %u. Expected %s.\n", t->line, satisfied ? "not a solution" : "a solution");
		de_Cleanup(&de);
	}

	for (i = 0; i < count; i++)
		ast_Cleanup(items[i]);
	ast_Cleanup(solution);
	ast_Cleanup(x);

	return passed;
}

bool test_Run(test_t *t) {
	pcas_ast_t *a = NULL, *b = NULL, *c = NULL, *expected, *actual;
	pcas_error_t err;
	bool passed = false;

	if (t->type == TEST_DE_SOLVES || t->type == TEST_DE_NOT_SOLVES)
		return run_verify(t);

	a = parse((uint8_t *)t->arg1, strlen(t->arg1), str_table, &err);
	if (err != E_SUCCESS) {
		printf("Test failed on line %u. Unable to parse first argument %s\n", t->line, t->arg1);
		return false;
	}

	b = parse((uint8_t *)t->arg2, strlen(t->arg2), str_table, &err);
	if (err != E_SUCCESS) {
		ast_Cleanup(a);
		printf("Test failed on line %u. Unable to parse second argument %s\n", t->line, t->arg2);
		return false;
	}

	c = parse((uint8_t *)t->arg3, strlen(t->arg3), str_table, &err);
	if (err != E_SUCCESS) {
		ast_Cleanup(a);
		ast_Cleanup(b);
		printf("Test failed on line %u. Unable to parse third argument %s\n", t->line, t->arg3);
		return false;
	}

	if (a == NULL || b == NULL) {
		ast_Cleanup(c);
		printf("Test failed on line %u. Empty %s argument.\n", t->line, a == NULL ? "first" : "second");
		return false;
	}

	switch (t->type) {
		case TEST_SIMPLIFY:
			expected = b;
			actual = a;

			eval(actual, EVAL_ALL);
			simplify(actual, SIMP_ALL);

			/*We do this to change -1 * 23 to -23 to be able to compare*/
			simplify(expected, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL);

			passed = check(t, actual, expected);
			break;
		case TEST_GCD:
			if (c == NULL) {
				printf("Test failed on line %u. Empty third argument.\n", t->line);
				break;
			}

			expected = c;

			/*We do this to change -1 * 23 to -23 to be able to compare*/
			simplify(expected, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL);

			simplify(a, SIMP_ALL);
			simplify(b, SIMP_ALL);
			actual = gcd(a, b);
			simplify(actual, SIMP_ALL);

			passed = check(t, actual, expected);

			ast_Cleanup(actual);
			break;
		case TEST_FACTOR:
			expected = b;
			actual = a;

			/*We do this to change -1 * 23 to -23 to be able to compare*/
			simplify(expected, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL);

			simplify(actual, SIMP_ALL);
			factor(actual, FAC_ALL);

			passed = check(t, actual, expected);
			break;
		case TEST_EXPAND:
			expected = b;
			actual = a;

			/*We do this to change -1 * 23 to -23 to be able to compare*/
			simplify(expected, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL);

			simplify(actual, SIMP_ALL);
			expand(actual, EXP_ALL);
			simplify(actual, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_LIKE_TERMS | SIMP_EVAL);

			passed = check(t, actual, expected);
			break;
		case TEST_DERIV: {
			if (c == NULL) {
				printf("Test failed on line %u. Empty third argument.\n", t->line);
				break;
			}

			actual = a;
			expected = c;

			derivative(a, b, b);

			/*We do this to change -1 * 23 to -23 to be able to compare*/
			simplify(expected, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL);

			/*Don't simplify derivatives because derivative() should have done that*/
			simplify(actual, SIMP_ALL ^ SIMP_DERIV);

			passed = check(t, actual, expected);

			break;
		}
		case TEST_INTEGRAL:
			if (c == NULL) {
				printf("Test failed on line %u. Empty third argument.\n", t->line);
				break;
			}

			actual = a;
			expected = c;

			simplify(actual, SIMP_ALL);
			integral(actual, b);
			simplify(actual, SIMP_ALL);

			/*We do this to change -1 * 23 to -23 to be able to compare*/
			simplify(expected, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL);

			passed = check(t, actual, expected);
			break;
		case TEST_DE_ORDER:
		case TEST_DE_LINEAR:
		case TEST_DE_NONLINEAR: {
			pcas_de_t de;

			if (t->type != TEST_DE_NONLINEAR && c == NULL) {
				printf("Test failed on line %u. Empty third argument.\n", t->line);
				break;
			}

			err = de_Load(&de, a, b);

			if (err != E_SUCCESS) {
				printf("Test failed on line %u. %s\n", t->line, error_text[err]);
			} else if (t->type == TEST_DE_ORDER) {
				actual = ast_MakeNumber(num_FromInt(de.order));
				passed = check(t, actual, c);
				ast_Cleanup(actual);
			} else if (t->type == TEST_DE_NONLINEAR) {
				passed = !de.linear;
				if (!passed)
					printf("Test failed on line %u. Expected nonlinear.\n", t->line);
			} else if (!de.linear) {
				printf("Test failed on line %u. Expected linear.\n", t->line);
			} else {
				actual = ast_MakeBinary(OP_EQUALS, de_StandardForm(&de), ast_Copy(de.g));
				simplify(actual, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL);
				simplify(c, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL);
				passed = check(t, actual, c);
				ast_Cleanup(actual);
			}

			de_Cleanup(&de);
			break;
		}
		default: break;
	}

	/*This is just here to ensure that the canonical form function terminates on every test case*/
	if (a != NULL) {
		simplify_canonical_form(a, CANONICAL_ALL);
		ast_Cleanup(a);
	}
	if (b != NULL) {
		simplify_canonical_form(b, CANONICAL_ALL);
		ast_Cleanup(b);
	}
	if (c != NULL) {
		simplify_canonical_form(c, CANONICAL_ALL);
		ast_Cleanup(c);
	}

	return passed;
}

void test_Cleanup(test_t *t) {
	free(t);
}

void test_CleanupArr(test_t **arr, unsigned len) {
	unsigned i;
	for (i = 0; i < len; i++) {
		test_Cleanup(arr[i]);
	}
	free(arr);
}

#else
typedef int make_iso_compilers_happy;
#endif
