/*
    PinaCAS: the multi-purpose CAS specifically for the TI-84+ CE

    Authors:
    James Prevett
    Nathan Farlow (PineappleCAS)
*/

#ifdef COMPILE_PC

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include <string.h>

#include "../ast.h"
#include "../parser.h"

#include "../dbg.h"

#include "../cas/cas.h"
#include "../work.h"

#include "tests.h"
#include "render.h"

void display_help(void) {
	printf("Usage: ./pinacas [--work] [operation] [args]\n");
	printf("\t--work\t\t\t\tPrints the steps taken\n");
	printf("Valid operations include:\n");
	printf("\ttest [file]\t\t\tRuns all tests in file\n");
	printf("\tsimplify [expression]\t\tSimplifies expression. Also evaluates constants.\n");
	printf("\tgcd [expression1] [expression2]\tPrints the GCD of the two expressions\n");
	printf("\tfactor [expression]\t\tFactors expression\n");
	printf("\texpand [expression]\t\tExpands expression\n");
	printf("\tderivative [expression] [respect to] [(optional) eval at]\n");
	printf("\tintegral [expression] [respect to]\n");
	printf("\tde [equation,conditions] [(optional) respect to]\tClassifies and solves a differential equation\n");
	printf(
		"\tseries [equation,conditions] [(optional) respect to] [(optional) terms]\tSolves a differential equation "
		"with a power series\n"
	);
	printf(
		"\tverify [equation,conditions] [solution] [(optional) respect to]\tChecks a solution of a differential "
		"equation\n"
	);
}

/*Trim null terminated string*/
uint8_t *trim(char *input, unsigned *len) {
	unsigned trimmed_len = 0, trim_index = 0;

	for (unsigned i = 0; i < strlen(input) + 1; i++) {
		if (input[i] != ' ' && input[i] != '\t')
			trimmed_len++;
	}

	uint8_t *trimmed = malloc(trimmed_len * sizeof(uint8_t));

	for (unsigned i = 0; i < strlen(input) + 1; i++) {
		if (input[i] != ' ' && input[i] != '\t')
			trimmed[trim_index++] = (uint8_t)input[i];
	}

	*len = trimmed_len - 1; /*Don't include null byte*/

	return trimmed;
}

int run_gcd(int argc, char **argv) {
	if (argc <= 3) {
		display_help();
		return -1;
	}

	unsigned trimmed_a_len;
	uint8_t *trimmed_a = trim(argv[2], &trimmed_a_len);

	printf("Parsing \"%s\"\n", trimmed_a);

	pcas_error_t err;
	pcas_ast_t *a = parse(trimmed_a, trimmed_a_len, str_table, &err);

	printf("%s\n", error_text[err]);

	if (err != E_SUCCESS || a == NULL)
		return -1;

	simplify(a, SIMP_ALL);

	unsigned trimmed_b_len;
	uint8_t *trimmed_b = trim(argv[3], &trimmed_b_len);

	printf("Parsing \"%s\"\n", trimmed_b);

	pcas_ast_t *b = parse(trimmed_b, trimmed_b_len, str_table, &err);

	printf("%s\n", error_text[err]);

	if (err != E_SUCCESS || b == NULL)
		return -1;

	simplify(b, SIMP_ALL);

	printf("Computing gcd...\n\n");

	pcas_ast_t *g = gcd(a, b);

	simplify(g, SIMP_ALL);
	simplify_canonical_form(g, CANONICAL_ALL);

	dbg_print_tree(g, 4);

	printf("\n");

	unsigned output_len;
	uint8_t *output = export_to_binary(g, &output_len, str_table, &err);

	if (err == E_SUCCESS && output != NULL) {
		printf("Output: ");
		printf("%.*s\n", output_len, output);

		free(output);
	}

	free(trimmed_a);
	free(trimmed_b);

	ast_Cleanup(a);
	ast_Cleanup(b);
	ast_Cleanup(g);

	return 0;
}
extern bool simplify_periodic(pcas_ast_t *e);
int run_simplify(int argc, char **argv) {
	if (argc <= 2) {
		display_help();
		return -1;
	}

	unsigned trimmed_len;
	uint8_t *trimmed = trim(argv[2], &trimmed_len);

	printf("Parsing \"%s\"\n", trimmed);

	pcas_error_t err;
	pcas_ast_t *e = parse(trimmed, trimmed_len, str_table, &err);

	printf("%s\n", error_text[err]);

	if (err == E_SUCCESS && e != NULL) {
		printf("\n");
		dbg_print_tree(e, 4);

		printf("\n");

		printf("Simplifying...\n\n");

		simplify(e, SIMP_ALL);
		simplify_canonical_form(e, CANONICAL_ALL);

		dbg_print_tree(e, 4);

		printf("\n");

		unsigned output_len;
		uint8_t *output = export_to_binary(e, &output_len, str_table, &err);

		if (err == E_SUCCESS && output != NULL) {
			printf("Output: ");
			printf("%.*s\n", output_len, output);

			free(output);
		}
	}

	free(trimmed);
	ast_Cleanup(e);

	return 0;
}

int run_factor(int argc, char **argv) {
	if (argc <= 2) {
		display_help();
		return -1;
	}

	unsigned trimmed_len;
	uint8_t *trimmed = trim(argv[2], &trimmed_len);

	printf("Parsing \"%s\"\n", trimmed);

	pcas_error_t err;
	pcas_ast_t *e = parse(trimmed, trimmed_len, str_table, &err);

	printf("%s\n", error_text[err]);

	if (err == E_SUCCESS && e != NULL) {
		printf("\n");
		dbg_print_tree(e, 4);

		printf("\n");

		printf("Simplifying...\n\n");

		simplify(e, SIMP_ALL);

		dbg_print_tree(e, 4);

		printf("\n");

		printf("Factoring...\n\n");

		factor(e, FAC_ALL);

		/*simplify(e, SIMP_ALL);*/
		simplify_canonical_form(e, CANONICAL_ALL);

		dbg_print_tree(e, 4);
		printf("\n");

		unsigned output_len;
		uint8_t *output = export_to_binary(e, &output_len, str_table, &err);

		if (err == E_SUCCESS && output != NULL) {
			printf("Output: ");
			printf("%.*s\n", output_len, output);

			free(output);
		}
	}

	free(trimmed);
	ast_Cleanup(e);

	return 0;
}

int run_expand(int argc, char **argv) {
	if (argc <= 2) {
		display_help();
		return -1;
	}

	unsigned trimmed_len;
	uint8_t *trimmed = trim(argv[2], &trimmed_len);

	printf("Parsing \"%s\"\n", trimmed);

	pcas_error_t err;
	pcas_ast_t *e = parse(trimmed, trimmed_len, str_table, &err);

	printf("%s\n", error_text[err]);

	if (err == E_SUCCESS && e != NULL) {
		printf("\n");
		dbg_print_tree(e, 4);

		printf("\n");

		printf("Simplifying...\n\n");

		simplify(e, SIMP_ALL);

		dbg_print_tree(e, 4);

		printf("\n");

		printf("Expanding...\n\n");

		simplify(e, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL);
		expand(e, EXP_ALL);
		simplify(e, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_LIKE_TERMS | SIMP_EVAL);
		simplify_canonical_form(e, CANONICAL_ALL ^ CANONICAL_COMBINE_POWERS);

		dbg_print_tree(e, 4);
		printf("\n");

		unsigned output_len;
		uint8_t *output = export_to_binary(e, &output_len, str_table, &err);

		if (err == E_SUCCESS && output != NULL) {
			printf("Output: ");
			printf("%.*s\n", output_len, output);

			free(output);
		}
	}

	free(trimmed);
	ast_Cleanup(e);

	return 0;
}

int run_test(int argc, char **argv) {
	if (argc < 3) {
		display_help();
		return -1;
	}

	unsigned len, failed = 0, passed = 0;
	test_t **arr = test_Load(argv[2], &len);

	if (arr == NULL) {
		printf("Could not load test file.\n");
		return -1;
	}

	printf("Running tests...\n");

	clock_t delta = clock();
	for (unsigned i = 0; i < len; i++) {
		test_t *t = arr[i];
		printf("Running test %d/%d on line %d... ", i + 1, len, t->line);
		if (!test_Run(t)) {
			puts("[FAIL]");
			failed++;
		} else {
			puts("[OK]");
			passed++;
		}
	}
	delta = clock() - delta;

	test_CleanupArr(arr, len);

	printf("Finished in %li microseconds.\n", delta / (CLOCKS_PER_SEC / 1000));
	printf("Passed: %u/%u, Failed: %u/%u\n", passed, len, failed, len);

	if (failed == 0) {
		printf("All tests passed!\n");
		return 0;
	}

	return -1;
}

int run_derivative(int argc, char **argv) {
	if (argc <= 3) {
		display_help();
		return -1;
	}

	pcas_ast_t *respect_to = NULL, *at = NULL;

	unsigned trimmed_len;
	uint8_t *trimmed = trim(argv[2], &trimmed_len);
	printf("Parsing \"%s\"\n", trimmed);
	pcas_error_t err;
	pcas_ast_t *e = parse(trimmed, trimmed_len, str_table, &err);
	printf("%s\n", error_text[err]);
	free(trimmed);

	if (err == E_SUCCESS) {
		trimmed = trim(argv[3], &trimmed_len);
		printf("Parsing \"%s\"\n", trimmed);
		respect_to = parse(trimmed, trimmed_len, str_table, &err);
		printf("%s\n", error_text[err]);
		free(trimmed);
	}

	if (err == E_SUCCESS) {
		if (argc >= 5) {
			trimmed = trim(argv[4], &trimmed_len);
			printf("Parsing \"%s\"\n", trimmed);
			at = parse(trimmed, trimmed_len, str_table, &err);
			printf("%s\n", error_text[err]);
			free(trimmed);
		} else {
			at = ast_Copy(respect_to);
		}
	}

	if (err == E_SUCCESS && e != NULL && respect_to != NULL && at != NULL) {
		printf("\n");
		dbg_print_tree(e, 4);

		printf("\n");

		printf("Simplifying...\n\n");

		simplify(e, SIMP_ALL);

		dbg_print_tree(e, 4);

		printf("\n");

		printf("Taking derivative...\n");

		derivative(e, respect_to, at);

		printf("Simplifying...\n\n");

		simplify(e, SIMP_ALL);
		simplify_canonical_form(e, CANONICAL_ALL);

		dbg_print_tree(e, 4);
		printf("\n");

		unsigned output_len;
		uint8_t *output = export_to_binary(e, &output_len, str_table, &err);

		if (err == E_SUCCESS && output != NULL) {
			printf("Output: ");
			printf("%.*s\n", output_len, output);

			free(output);
		}
	}

	ast_Cleanup(e);
	ast_Cleanup(respect_to);
	ast_Cleanup(at);

	return 0;
}

static void print_ast(pcas_ast_t *e) {
	pcas_error_t err;
	unsigned len;
	uint8_t *output = export_to_binary(e, &len, str_table, &err);

	if (err == E_SUCCESS && output != NULL) {
		printf("%.*s", len, output);
		free(output);
	}
}

static void print_work(pcas_work_t *w) {
	const pcas_step_t *previous = NULL;

	printf("Work:\n\n");

	for (pcas_step_t *step = w->first; step != NULL; previous = step, step = step->next) {
		ts_box_t *line = work_Layout(step, previous != NULL && previous->type == STEP_STATE);

		if (step->text != NULL)
			printf("[%s]\n", step->text);

		if (line != NULL) {
			render_Print(line);
			ts_Cleanup(line);
		}

		printf("\n");
	}
}

int run_integral(int argc, char **argv) {
	if (argc <= 3) {
		display_help();
		return -1;
	}

	pcas_ast_t *respect_to = NULL;

	unsigned trimmed_len;
	uint8_t *trimmed = trim(argv[2], &trimmed_len);
	pcas_error_t err;
	pcas_ast_t *e = parse(trimmed, trimmed_len, str_table, &err);
	free(trimmed);

	if (err == E_SUCCESS) {
		trimmed = trim(argv[3], &trimmed_len);
		respect_to = parse(trimmed, trimmed_len, str_table, &err);
		free(trimmed);
	}

	if (err == E_SUCCESS && e != NULL && respect_to != NULL) {
		simplify(e, SIMP_ALL);
		integral_Indefinite(e, respect_to);
		simplify(e, SIMP_ALL);
		simplify_canonical_form(e, CANONICAL_ALL);

		printf("Output: ");
		print_ast(e);
		printf("\n");
	} else {
		printf("%s\n", error_text[err]);
	}

	ast_Cleanup(e);
	ast_Cleanup(respect_to);

	return 0;
}

#define MAX_ITEMS (DE_MAX_CONDITIONS + 1)

int run_de(int argc, char **argv) {
	if (argc <= 2) {
		display_help();
		return -1;
	}

	pcas_ast_t *items[MAX_ITEMS], *x = NULL, *solution = NULL;

	unsigned trimmed_len;
	uint8_t *trimmed = trim(argv[2], &trimmed_len);
	pcas_error_t err;
	const unsigned count = parse_list(trimmed, trimmed_len, str_table, items, MAX_ITEMS, &err);
	free(trimmed);

	if (err == E_SUCCESS) {
		if (argc >= 4) {
			trimmed = trim(argv[3], &trimmed_len);
			x = parse(trimmed, trimmed_len, str_table, &err);
			free(trimmed);
		} else {
			x = ast_MakeSymbol(SYM_X);
		}
	}

	if (err == E_SUCCESS && count > 0 && items[0] != NULL && x != NULL) {
		pcas_de_t de;
		err = de_LoadList(&de, items, count, x);

		if (err == E_SUCCESS) {
			de.series = !strcmp(argv[1], "series");
			if (argc >= 5 && atoi(argv[4]) > 0)
				de.terms = (unsigned)atoi(argv[4]);

			de_Classify(&de);

			printf("Order: %u\n", de.order);
			printf("Linear: %s\n", de.linear ? "yes" : "no");

			if (de.linear) {
				pcas_ast_t *standard = de_StandardForm(&de);
				pcas_ast_t *equation = ast_MakeBinary(OP_EQUALS, standard, ast_Copy(de.g));

				simplify_canonical_form(equation, CANONICAL_ALL);
				printf("Standard form: ");
				print_ast(equation);
				printf("\n");

				ast_Cleanup(equation);
			}

			err = de_Solve(&de, &solution);

			if (err == E_SUCCESS) {
				simplify_canonical_form(solution, CANONICAL_ALL);
				printf("Method: %s\n", de.method);
				printf("Solution: ");
				print_ast(solution);
				printf("\n");
			}
		}

		if (err != E_SUCCESS)
			printf("%s\n", error_text[err]);

		de_Cleanup(&de);
	} else {
		printf("%s\n", error_text[err]);
	}

	for (unsigned i = 0; i < count; i++)
		ast_Cleanup(items[i]);
	ast_Cleanup(x);
	ast_Cleanup(solution);

	return err == E_SUCCESS ? 0 : -1;
}

int run_verify(int argc, char **argv) {
	if (argc <= 3) {
		display_help();
		return -1;
	}

	pcas_ast_t *items[MAX_ITEMS], *solution = NULL, *x = NULL;
	bool satisfied = false;

	unsigned trimmed_len;
	uint8_t *trimmed = trim(argv[2], &trimmed_len);
	pcas_error_t err;
	const unsigned count = parse_list(trimmed, trimmed_len, str_table, items, MAX_ITEMS, &err);
	free(trimmed);

	if (err == E_SUCCESS) {
		trimmed = trim(argv[3], &trimmed_len);
		solution = parse(trimmed, trimmed_len, str_table, &err);
		free(trimmed);
	}

	if (err == E_SUCCESS) {
		if (argc >= 5) {
			trimmed = trim(argv[4], &trimmed_len);
			x = parse(trimmed, trimmed_len, str_table, &err);
			free(trimmed);
		} else {
			x = ast_MakeSymbol(SYM_X);
		}
	}

	if (err == E_SUCCESS && count > 0 && items[0] != NULL && solution != NULL && x != NULL) {
		pcas_de_t de;
		err = de_LoadList(&de, items, count, x);

		if (err == E_SUCCESS)
			err = de_Verify(&de, solution, &satisfied);

		printf("%s\n", err == E_SUCCESS ? (satisfied ? "Solution" : "Not a solution") : error_text[err]);

		de_Cleanup(&de);
	} else {
		printf("%s\n", error_text[err]);
	}

	for (unsigned i = 0; i < count; i++)
		ast_Cleanup(items[i]);
	ast_Cleanup(solution);
	ast_Cleanup(x);

	return err == E_SUCCESS && satisfied ? 0 : -1;
}

int main(int argc, char **argv) {
	pcas_work_t work;
	const bool show_work = argc > 1 && !strcmp(argv[1], "--work");

	if (show_work) {
		argc--;
		argv++;
		work_Start(&work);
	}

	if (argc > 1) {
		int ret;
		if (!strcmp(argv[1], "test"))
			ret = run_test(argc, argv);
		else if (!strcmp(argv[1], "simplify"))
			ret = run_simplify(argc, argv);
		else if (!strcmp(argv[1], "gcd"))
			ret = run_gcd(argc, argv);
		else if (!strcmp(argv[1], "factor"))
			ret = run_factor(argc, argv);
		else if (!strcmp(argv[1], "expand"))
			ret = run_expand(argc, argv);
		else if (!strcmp(argv[1], "derivative"))
			ret = run_derivative(argc, argv);
		else if (!strcmp(argv[1], "integral"))
			ret = run_integral(argc, argv);
		else if (!strcmp(argv[1], "de") || !strcmp(argv[1], "series"))
			ret = run_de(argc, argv);
		else if (!strcmp(argv[1], "verify"))
			ret = run_verify(argc, argv);
		else {
			display_help();
			return -1;
		}
		if (show_work) {
			work_Stop();
			print_work(&work);
			work_Cleanup(&work);
		}
		id_UnloadAll();
		return ret;
	}

	display_help();
	return -1;
}

#else
typedef int make_iso_compilers_happy;
#endif
