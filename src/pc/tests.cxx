#ifdef COMPILE_PC

#include "tests.hxx"

#include <string.h>
#include <stdlib.h>

#include "../parser.hxx"
#include "../ast.hxx"
#include "../cas/cas.hxx"

#include "../dbg.hxx"

/*Trim null terminated string*/
static char *trim(char *str) {
	unsigned trimmed_len = 0, trim_index = 0;

	for (unsigned i = 0; i < strlen(str) + 1; i++) {
		if (str[i] != ' ' && str[i] != '\t' && str[i] != '\n')
			trimmed_len++;
	}

	char *trimmed = static_cast<char *>(malloc(trimmed_len * sizeof(char)));

	for (unsigned i = 0; i < strlen(str) + 1; i++) {
		if (str[i] != ' ' && str[i] != '\t' && str[i] != '\n')
			trimmed[trim_index++] = str[i];
	}

	return trimmed;
}

TestType resolve_type(char *type) {
	if (!strcmp(type, "simplify"))
		return TestType::Simplify;
	if (!strcmp(type, "gcd"))
		return TestType::Gcd;
	if (!strcmp(type, "factor"))
		return TestType::Factor;
	if (!strcmp(type, "expand"))
		return TestType::Expand;
	if (!strcmp(type, "deriv"))
		return TestType::Deriv;
	if (!strcmp(type, "integ"))
		return TestType::Integral;
	if (!strcmp(type, "order"))
		return TestType::DeOrder;
	if (!strcmp(type, "linear"))
		return TestType::DeLinear;
	if (!strcmp(type, "nonlinear"))
		return TestType::DeNonlinear;
	if (!strcmp(type, "solves"))
		return TestType::DeSolves;
	if (!strcmp(type, "notsolves"))
		return TestType::DeNotSolves;
	if (!strcmp(type, "solve"))
		return TestType::DeSolve;
	if (!strcmp(type, "series"))
		return TestType::DeSeries;

	return TestType::Invalid;
}

Test *test_Parse(char *line) {
	Test *t = static_cast<Test *>(malloc(sizeof(Test)));

	char *pt = strtok(line, ";");
	for (unsigned i = 0; i < 3; i++) {
		if (pt == nullptr) {
			free(t);
			return nullptr;
		}

		char *trimmed = trim(pt);

		if (i == 0)
			t->type = resolve_type(trimmed);
		else if (i == 1)
			strncpy(t->arg1, trimmed, MAX_PAR);
		else if (i == 2)
			strncpy(t->arg2, trimmed, MAX_PAR);

		free(trimmed);

		pt = strtok(nullptr, ";");
	}

	if (pt != nullptr) {
		char *trimmed = trim(pt);
		strncpy(t->arg3, trimmed, MAX_PAR);
		free(trimmed);
	} else {
		t->arg3[0] = '\0';
	}

	return t;
}

Test **test_Load(char *file, unsigned *len) {
	Test **arr = static_cast<Test **>(malloc(sizeof(Test **) * MAX_TESTS));

	FILE *f = fopen(file, "r");

	if (f == nullptr) {
		free(arr);
		return nullptr;
	}

	unsigned i = 0, cur_line = 1;
	char line[MAX_LINE];

	while (fgets(line, sizeof(line), f) != nullptr) {
		if (i >= MAX_TESTS)
			break;

		Test *t = test_Parse(line);

		if (t != nullptr && t->type != TestType::Invalid) {
			arr[i] = t;

			t->line = cur_line;

			i++;
		} else if (t != nullptr) {
			free(t);
		}

		cur_line++;
	}

	fclose(f);

	*len = i;

	return arr;
}

bool check(Test *t, ast &actual, ast &expected) {
	if (!expected.compare(actual)) {
		unsigned expected_len, actual_len;
		Error expted_err, actual_err;
		char *output_expected = (char *)export_to_binary(expected, &expected_len, str_table, &expted_err);
		char *output_actual = (char *)export_to_binary(actual, &actual_len, str_table, &actual_err);

		if (expted_err != Error::Success || actual_err != Error::Success) {
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

constexpr unsigned MAX_ITEMS = DiffEq::max_conditions + 1;

/*arg1 is an equation with initial conditions, arg2 a solution, and arg3 the independent variable*/
static bool run_verify(Test *t) {
	ast *items[MAX_ITEMS];
	Error err;
	DiffEq de;
	bool satisfied = false, passed = false;

	const unsigned count = parse_list((uint8_t *)t->arg1, strlen(t->arg1), str_table, items, MAX_ITEMS, &err);
	ast *solution = parse((uint8_t *)t->arg2, strlen(t->arg2), str_table, &err);
	ast *x = parse((uint8_t *)t->arg3, strlen(t->arg3), str_table, &err);

	if (count == 0 || items[0] == nullptr || solution == nullptr || x == nullptr) {
		printf("Test failed on line %u. Unable to parse arguments.\n", t->line);
	} else if (
		(err = de.loadList(items, count, *x)) != Error::Success ||
		(err = de.verify(*solution, &satisfied)) != Error::Success
	) {
		printf("Test failed on line %u. %s\n", t->line, error_text(err));
		de.clear();
	} else {
		passed = satisfied == (t->type == TestType::DeSolves);
		if (!passed)
			printf("Test failed on line %u. Expected %s.\n", t->line, satisfied ? "not a solution" : "a solution");
		de.clear();
	}

	for (unsigned i = 0; i < count; i++)
		ast::dispose(items[i]);
	ast::dispose(solution);
	ast::dispose(x);

	return passed;
}

static void simplify_solution(ast &e) {
	simplify(e, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval | Simp::LikeTerms);
	simplify_canonical_form(e, Canonical::All);
}

static Error solve_test(DiffEq *de, const Test *t, ast **solution) {
	de->series = t->type == TestType::DeSeries;
	return de->solve(solution);
}

/*arg1 is an equation with initial conditions, arg2 the expected solution, and arg3 the independent variable. An explicit solution must also pass verification.*/
static bool run_solve(Test *t) {
	ast *items[MAX_ITEMS], *solution = nullptr;
	Error err;
	DiffEq de;
	bool satisfied = true, passed = false;

	const unsigned count = parse_list((uint8_t *)t->arg1, strlen(t->arg1), str_table, items, MAX_ITEMS, &err);
	ast *expected = parse((uint8_t *)t->arg2, strlen(t->arg2), str_table, &err);
	ast *x = parse((uint8_t *)t->arg3, strlen(t->arg3), str_table, &err);

	if (count == 0 || items[0] == nullptr || expected == nullptr || x == nullptr) {
		printf("Test failed on line %u. Unable to parse arguments.\n", t->line);
	} else if (
		(err = de.loadList(items, count, *x)) != Error::Success ||
		(err = solve_test(&de, t, &solution)) != Error::Success
	) {
		printf("Test failed on line %u. %s\n", t->line, error_text(err));
		de.clear();
	} else {
		simplify_solution(*solution);
		simplify_solution(*expected);
		passed = check(t, *solution, *expected);

		if (passed && strcmp(de.method, "Power series") != 0 && solution->firstChild()->compare(*de.y)) {
			err = de.verify(*solution, &satisfied);
			passed = err == Error::Success && satisfied;
			if (!passed)
				printf("Test failed on line %u. The solution does not verify.\n", t->line);
		}

		de.clear();
	}

	for (unsigned i = 0; i < count; i++)
		ast::dispose(items[i]);
	ast::dispose(expected);
	ast::dispose(x);
	ast::dispose(solution);

	return passed;
}

bool test_Run(Test *t) {
	if (t->type == TestType::DeSolves || t->type == TestType::DeNotSolves)
		return run_verify(t);
	if (t->type == TestType::DeSolve || t->type == TestType::DeSeries)
		return run_solve(t);

	Error err;
	ast *a = parse((uint8_t *)t->arg1, strlen(t->arg1), str_table, &err);
	if (err != Error::Success) {
		printf("Test failed on line %u. Unable to parse first argument %s\n", t->line, t->arg1);
		return false;
	}

	ast *b = parse((uint8_t *)t->arg2, strlen(t->arg2), str_table, &err);
	if (err != Error::Success) {
		ast::dispose(a);
		printf("Test failed on line %u. Unable to parse second argument %s\n", t->line, t->arg2);
		return false;
	}

	ast *c = parse((uint8_t *)t->arg3, strlen(t->arg3), str_table, &err);
	if (err != Error::Success) {
		ast::dispose(a);
		ast::dispose(b);
		printf("Test failed on line %u. Unable to parse third argument %s\n", t->line, t->arg3);
		return false;
	}

	if (a == nullptr || b == nullptr) {
		ast::dispose(c);
		printf("Test failed on line %u. Empty %s argument.\n", t->line, a == nullptr ? "first" : "second");
		return false;
	}

	ast *expected, *actual;
	bool passed = false;

	switch (t->type) {
		case TestType::Simplify:
			expected = b;
			actual = a;

			eval(*actual, Eval::All);
			simplify(*actual, Simp::All);

			/*We do this to change -1 * 23 to -23 to be able to compare*/
			simplify(*expected, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval);

			passed = check(t, *actual, *expected);
			break;
		case TestType::Gcd:
			if (c == nullptr) {
				printf("Test failed on line %u. Empty third argument.\n", t->line);
				break;
			}

			expected = c;

			/*We do this to change -1 * 23 to -23 to be able to compare*/
			simplify(*expected, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval);

			simplify(*a, Simp::All);
			simplify(*b, Simp::All);
			actual = gcd(*a, *b);
			simplify(*actual, Simp::All);

			passed = check(t, *actual, *expected);

			ast::dispose(actual);
			break;
		case TestType::Factor:
			expected = b;
			actual = a;

			/*We do this to change -1 * 23 to -23 to be able to compare*/
			simplify(*expected, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval);

			simplify(*actual, Simp::All);
			factor(*actual, Factor::All);

			passed = check(t, *actual, *expected);
			break;
		case TestType::Expand:
			expected = b;
			actual = a;

			/*We do this to change -1 * 23 to -23 to be able to compare*/
			simplify(*expected, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval);

			simplify(*actual, Simp::All);
			expand(*actual, Expand::All);
			simplify(*actual, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::LikeTerms | Simp::Eval);

			passed = check(t, *actual, *expected);
			break;
		case TestType::Deriv: {
			if (c == nullptr) {
				printf("Test failed on line %u. Empty third argument.\n", t->line);
				break;
			}

			actual = a;
			expected = c;

			derivative(*a, *b, *b);

			/*We do this to change -1 * 23 to -23 to be able to compare*/
			simplify(*expected, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval);

			/*Don't simplify derivatives because derivative() should have done that*/
			simplify(*actual, Simp::All & ~Simp::Deriv);

			passed = check(t, *actual, *expected);

			break;
		}
		case TestType::Integral:
			if (c == nullptr) {
				printf("Test failed on line %u. Empty third argument.\n", t->line);
				break;
			}

			actual = a;
			expected = c;

			simplify(*actual, Simp::All);
			integral(*actual, *b);
			simplify(*actual, Simp::All);

			/*We do this to change -1 * 23 to -23 to be able to compare*/
			simplify(*expected, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval);

			passed = check(t, *actual, *expected);
			break;
		case TestType::DeOrder:
		case TestType::DeLinear:
		case TestType::DeNonlinear: {
			DiffEq de;

			if (t->type != TestType::DeNonlinear && c == nullptr) {
				printf("Test failed on line %u. Empty third argument.\n", t->line);
				break;
			}

			err = de.load(*a, *b);

			if (err != Error::Success) {
				printf("Test failed on line %u. %s\n", t->line, error_text(err));
			} else if (t->type == TestType::DeOrder) {
				actual = ast::make(num::from(de.order));
				passed = check(t, *actual, *c);
				ast::dispose(actual);
			} else if (t->type == TestType::DeNonlinear) {
				passed = !de.linear;
				if (!passed)
					printf("Test failed on line %u. Expected nonlinear.\n", t->line);
			} else if (!de.linear) {
				printf("Test failed on line %u. Expected linear.\n", t->line);
			} else {
				actual = ast::make(Op::Equals, de.standardForm(), de.g->copy());
				simplify(*actual, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval);
				simplify(*c, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval);
				passed = check(t, *actual, *c);
				ast::dispose(actual);
			}

			de.clear();
			break;
		}
		default: break;
	}

	/*This is just here to ensure that the canonical form function terminates on every test case*/
	if (a != nullptr) {
		simplify_canonical_form(*a, Canonical::All);
		ast::dispose(a);
	}
	if (b != nullptr) {
		simplify_canonical_form(*b, Canonical::All);
		ast::dispose(b);
	}
	if (c != nullptr) {
		simplify_canonical_form(*c, Canonical::All);
		ast::dispose(c);
	}

	return passed;
}

void test_Cleanup(Test *t) {
	free(t);
}

void test_CleanupArr(Test **arr, unsigned len) {
	for (unsigned i = 0; i < len; i++) {
		test_Cleanup(arr[i]);
	}
	free(arr);
}

#endif
