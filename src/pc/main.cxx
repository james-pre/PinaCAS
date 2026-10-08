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

#include "../ast.hxx"
#include "../parser.hxx"

#include "../dbg.hxx"

#include "../cas/cas.hxx"
#include "../work.hxx"

#include "tests.hxx"
#include "render.hxx"

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
char *trim(char *input, unsigned *len) {
	unsigned trimmed_len = 0, trim_index = 0;

	for (unsigned i = 0; i < strlen(input) + 1; i++) {
		if (input[i] != ' ' && input[i] != '\t')
			trimmed_len++;
	}

	char *trimmed = static_cast<char *>(malloc(trimmed_len * sizeof(char)));

	for (unsigned i = 0; i < strlen(input) + 1; i++) {
		if (input[i] != ' ' && input[i] != '\t')
			trimmed[trim_index++] = input[i];
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
	char *trimmed_a = trim(argv[2], &trimmed_a_len);

	printf("Parsing \"%s\"\n", trimmed_a);

	Error err;
	ast *a = parse(trimmed_a, trimmed_a_len, str_table, &err);

	printf("%s\n", error_text(err));

	if (err != Error::Success || a == nullptr)
		return -1;

	simplify(*a, Simp::All);

	unsigned trimmed_b_len;
	char *trimmed_b = trim(argv[3], &trimmed_b_len);

	printf("Parsing \"%s\"\n", trimmed_b);

	ast *b = parse(trimmed_b, trimmed_b_len, str_table, &err);

	printf("%s\n", error_text(err));

	if (err != Error::Success || b == nullptr)
		return -1;

	simplify(*b, Simp::All);

	printf("Computing gcd...\n\n");

	ast *g = gcd(*a, *b);

	simplify(*g, Simp::All);
	simplify_canonical_form(*g, Canonical::All);

	dbg_print_tree(*g, 4);

	printf("\n");

	unsigned output_len;
	uint8_t *output = export_to_binary(*g, &output_len, str_table, &err);

	if (err == Error::Success && output != nullptr) {
		printf("Output: ");
		printf("%.*s\n", output_len, output);

		free(output);
	}

	free(trimmed_a);
	free(trimmed_b);

	ast::dispose(a);
	ast::dispose(b);
	ast::dispose(g);

	return 0;
}

int run_simplify(int argc, char **argv) {
	if (argc <= 2) {
		display_help();
		return -1;
	}

	unsigned trimmed_len;
	char *trimmed = trim(argv[2], &trimmed_len);

	printf("Parsing \"%s\"\n", trimmed);

	Error err;
	ast *e = parse(trimmed, trimmed_len, str_table, &err);

	printf("%s\n", error_text(err));

	if (err == Error::Success && e != nullptr) {
		printf("\n");
		dbg_print_tree(*e, 4);

		printf("\n");

		printf("Simplifying...\n\n");

		simplify(*e, Simp::All);
		simplify_canonical_form(*e, Canonical::All);

		dbg_print_tree(*e, 4);

		printf("\n");

		unsigned output_len;
		uint8_t *output = export_to_binary(*e, &output_len, str_table, &err);

		if (err == Error::Success && output != nullptr) {
			printf("Output: ");
			printf("%.*s\n", output_len, output);

			free(output);
		}
	}

	free(trimmed);
	ast::dispose(e);

	return 0;
}

int run_factor(int argc, char **argv) {
	if (argc <= 2) {
		display_help();
		return -1;
	}

	unsigned trimmed_len;
	char *trimmed = trim(argv[2], &trimmed_len);

	printf("Parsing \"%s\"\n", trimmed);

	Error err;
	ast *e = parse(trimmed, trimmed_len, str_table, &err);

	printf("%s\n", error_text(err));

	if (err == Error::Success && e != nullptr) {
		printf("\n");
		dbg_print_tree(*e, 4);

		printf("\n");

		printf("Simplifying...\n\n");

		simplify(*e, Simp::All);

		dbg_print_tree(*e, 4);

		printf("\n");

		printf("Factoring...\n\n");

		factor(*e, Factor::All);

		/*simplify(e, Simp::All);*/
		simplify_canonical_form(*e, Canonical::All);

		dbg_print_tree(*e, 4);
		printf("\n");

		unsigned output_len;
		uint8_t *output = export_to_binary(*e, &output_len, str_table, &err);

		if (err == Error::Success && output != nullptr) {
			printf("Output: ");
			printf("%.*s\n", output_len, output);

			free(output);
		}
	}

	free(trimmed);
	ast::dispose(e);

	return 0;
}

int run_expand(int argc, char **argv) {
	if (argc <= 2) {
		display_help();
		return -1;
	}

	unsigned trimmed_len;
	char *trimmed = trim(argv[2], &trimmed_len);

	printf("Parsing \"%s\"\n", trimmed);

	Error err;
	ast *e = parse(trimmed, trimmed_len, str_table, &err);

	printf("%s\n", error_text(err));

	if (err == Error::Success && e != nullptr) {
		printf("\n");
		dbg_print_tree(*e, 4);

		printf("\n");

		printf("Simplifying...\n\n");

		simplify(*e, Simp::All);

		dbg_print_tree(*e, 4);

		printf("\n");

		printf("Expanding...\n\n");

		simplify(*e, Simp::Normalize | Simp::Commutative | Simp::Rational);
		expand(*e, Expand::All);
		simplify(*e, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::LikeTerms | Simp::Eval);
		simplify_canonical_form(*e, Canonical::All & ~Canonical::CombinePowers);

		dbg_print_tree(*e, 4);
		printf("\n");

		unsigned output_len;
		uint8_t *output = export_to_binary(*e, &output_len, str_table, &err);

		if (err == Error::Success && output != nullptr) {
			printf("Output: ");
			printf("%.*s\n", output_len, output);

			free(output);
		}
	}

	free(trimmed);
	ast::dispose(e);

	return 0;
}

int run_test(int argc, char **argv) {
	if (argc < 3) {
		display_help();
		return -1;
	}

	unsigned len, failed = 0, passed = 0;
	Test **arr = test_Load(argv[2], &len);

	if (arr == nullptr) {
		printf("Could not load test file.\n");
		return -1;
	}

	printf("Running tests...\n");

	clock_t delta = clock();
	for (unsigned i = 0; i < len; i++) {
		Test *t = arr[i];
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

	ast *respect_to = nullptr, *at = nullptr;

	unsigned trimmed_len;
	char *trimmed = trim(argv[2], &trimmed_len);
	printf("Parsing \"%s\"\n", trimmed);
	Error err;
	ast *e = parse(trimmed, trimmed_len, str_table, &err);
	printf("%s\n", error_text(err));
	free(trimmed);

	if (err == Error::Success) {
		trimmed = trim(argv[3], &trimmed_len);
		printf("Parsing \"%s\"\n", trimmed);
		respect_to = parse(trimmed, trimmed_len, str_table, &err);
		printf("%s\n", error_text(err));
		free(trimmed);
	}

	if (err == Error::Success) {
		if (argc >= 5) {
			trimmed = trim(argv[4], &trimmed_len);
			printf("Parsing \"%s\"\n", trimmed);
			at = parse(trimmed, trimmed_len, str_table, &err);
			printf("%s\n", error_text(err));
			free(trimmed);
		} else {
			at = respect_to != nullptr ? respect_to->copy() : nullptr;
		}
	}

	if (err == Error::Success && e != nullptr && respect_to != nullptr && at != nullptr) {
		printf("\n");
		dbg_print_tree(*e, 4);

		printf("\n");

		printf("Simplifying...\n\n");

		simplify(*e, Simp::All);

		dbg_print_tree(*e, 4);

		printf("\n");

		printf("Taking derivative...\n");

		derivative(*e, *respect_to, *at);

		printf("Simplifying...\n\n");

		simplify(*e, Simp::All);
		simplify_canonical_form(*e, Canonical::All);

		dbg_print_tree(*e, 4);
		printf("\n");

		unsigned output_len;
		uint8_t *output = export_to_binary(*e, &output_len, str_table, &err);

		if (err == Error::Success && output != nullptr) {
			printf("Output: ");
			printf("%.*s\n", output_len, output);

			free(output);
		}
	}

	ast::dispose(e);
	ast::dispose(respect_to);
	ast::dispose(at);

	return 0;
}

static void print_ast(ast &e) {
	Error err;
	unsigned len;
	uint8_t *output = export_to_binary(e, &len, str_table, &err);

	if (err == Error::Success && output != nullptr) {
		printf("%.*s", len, output);
		free(output);
	}
}

static void print_work(work::Record *w) {
	const work::Step *previous = nullptr;

	printf("Work:\n\n");

	for (work::Step *step = w->first; step != nullptr; previous = step, step = step->next) {
		ts::Box *line = work::layout(step, previous != nullptr && previous->type == work::Step::Type::State);

		if (step->text != nullptr)
			printf("[%s]\n", step->text);

		if (line != nullptr) {
			render::print(line);
			delete line;
		}

		printf("\n");
	}
}

int run_integral(int argc, char **argv) {
	if (argc <= 3) {
		display_help();
		return -1;
	}

	ast *respect_to = nullptr;

	unsigned trimmed_len;
	char *trimmed = trim(argv[2], &trimmed_len);
	Error err;
	ast *e = parse(trimmed, trimmed_len, str_table, &err);
	free(trimmed);

	if (err == Error::Success) {
		trimmed = trim(argv[3], &trimmed_len);
		respect_to = parse(trimmed, trimmed_len, str_table, &err);
		free(trimmed);
	}

	if (err == Error::Success && e != nullptr && respect_to != nullptr) {
		simplify(*e, Simp::All);
		integral_Indefinite(*e, *respect_to);
		simplify(*e, Simp::All);
		simplify_canonical_form(*e, Canonical::All);

		printf("Output: ");
		print_ast(*e);
		printf("\n");
	} else {
		printf("%s\n", error_text(err));
	}

	ast::dispose(e);
	ast::dispose(respect_to);

	return 0;
}

constexpr unsigned MAX_ITEMS = DiffEq::max_conditions + 1;

int run_de(int argc, char **argv) {
	if (argc <= 2) {
		display_help();
		return -1;
	}

	ast *items[MAX_ITEMS], *x = nullptr, *solution = nullptr;

	unsigned trimmed_len;
	char *trimmed = trim(argv[2], &trimmed_len);
	Error err;
	const unsigned count = parse_list(trimmed, trimmed_len, str_table, items, MAX_ITEMS, &err);
	free(trimmed);

	if (err == Error::Success) {
		if (argc >= 4) {
			trimmed = trim(argv[3], &trimmed_len);
			x = parse(trimmed, trimmed_len, str_table, &err);
			free(trimmed);
		} else {
			x = ast::make(Sym::X);
		}
	}

	if (err == Error::Success && count > 0 && items[0] != nullptr && x != nullptr) {
		DiffEq de;
		err = de.loadList(items, count, *x);

		if (err == Error::Success) {
			de.series = !strcmp(argv[1], "series");
			if (argc >= 5 && atoi(argv[4]) > 0)
				de.terms = static_cast<unsigned>(atoi(argv[4]));

			de.classify();

			printf("Order: %u\n", de.order);
			printf("Linear: %s\n", de.linear ? "yes" : "no");

			if (de.linear) {
				ast *standard = de.standardForm();
				ast *equation = ast::make(Op::Equals, standard, de.g->copy());

				simplify_canonical_form(*equation, Canonical::All);
				printf("Standard form: ");
				print_ast(*equation);
				printf("\n");

				ast::dispose(equation);
			}

			err = de.solve(&solution);

			if (err == Error::Success) {
				simplify_canonical_form(*solution, Canonical::All);
				printf("Method: %s\n", de.method);
				printf("Solution: ");
				print_ast(*solution);
				printf("\n");
			}
		}

		if (err != Error::Success)
			printf("%s\n", error_text(err));

		de.clear();
	} else {
		printf("%s\n", error_text(err));
	}

	for (unsigned i = 0; i < count; i++)
		ast::dispose(items[i]);
	ast::dispose(x);
	ast::dispose(solution);

	return err == Error::Success ? 0 : -1;
}

int run_verify(int argc, char **argv) {
	if (argc <= 3) {
		display_help();
		return -1;
	}

	ast *items[MAX_ITEMS], *solution = nullptr, *x = nullptr;
	bool satisfied = false;

	unsigned trimmed_len;
	char *trimmed = trim(argv[2], &trimmed_len);
	Error err;
	const unsigned count = parse_list(trimmed, trimmed_len, str_table, items, MAX_ITEMS, &err);
	free(trimmed);

	if (err == Error::Success) {
		trimmed = trim(argv[3], &trimmed_len);
		solution = parse(trimmed, trimmed_len, str_table, &err);
		free(trimmed);
	}

	if (err == Error::Success) {
		if (argc >= 5) {
			trimmed = trim(argv[4], &trimmed_len);
			x = parse(trimmed, trimmed_len, str_table, &err);
			free(trimmed);
		} else {
			x = ast::make(Sym::X);
		}
	}

	if (err == Error::Success && count > 0 && items[0] != nullptr && solution != nullptr && x != nullptr) {
		DiffEq de;
		err = de.loadList(items, count, *x);

		if (err == Error::Success)
			err = de.verify(*solution, &satisfied);

		printf("%s\n", err == Error::Success ? (satisfied ? "Solution" : "Not a solution") : error_text(err));

		de.clear();
	} else {
		printf("%s\n", error_text(err));
	}

	for (unsigned i = 0; i < count; i++)
		ast::dispose(items[i]);
	ast::dispose(solution);
	ast::dispose(x);

	return err == Error::Success && satisfied ? 0 : -1;
}

int main(int argc, char **argv) {
	work::Record work;
	const bool show_work = argc > 1 && !strcmp(argv[1], "--work");

	if (show_work) {
		argc--;
		argv++;
		work::start(work);
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
			work::stop();
			print_work(&work);
			work.clear();
		}
		id::unloadAll();
		return ret;
	}

	display_help();
	return -1;
}

#endif
