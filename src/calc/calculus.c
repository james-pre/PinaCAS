#ifndef COMPILE_PC

#include "calculus.h"

#include <stdio.h>

#include "../cas/cas.h"

pcas_error_t calculus_Run(
	Calculus kind,
	pcas_ast_t **items,
	unsigned count,
	pcas_ast_t *respect_to,
	bool series,
	unsigned terms,
	char *summary
) {
	pcas_ast_t *e = items[0];
	pcas_error_t err = E_SUCCESS;

	if (kind != CALCULUS_DE && count > 1)
		return E_PARSE_BAD_COMMA;

	switch (kind) {
		case CALCULUS_DERIVATIVE:
			simplify(e, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL);
			derivative(e, respect_to, respect_to);
			break;
		case CALCULUS_INTEGRAL:
			simplify(e, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL);
			integral_Indefinite(e, respect_to);
			break;
		case CALCULUS_DE: {
			pcas_de_t de;
			err = de_LoadList(&de, items, count, respect_to);

			if (err == E_SUCCESS) {
				pcas_ast_t *solution;

				de.series = series;
				de.terms = terms;
				de_Classify(&de);
				err = de_Solve(&de, &solution);

				if (err == E_SUCCESS) {
					if (summary != NULL)
						sprintf(summary, "Solved. %s.", de.method);

					if (ast_Compare(opbase(solution), de.y)) {
						replace_node(e, ast_Copy(opbase(solution)->next));
						ast_Cleanup(solution);
					} else {
						replace_node(e, solution);
					}
				} else if (err == E_DE_UNSOLVED || err == E_DE_INTEGRAL) {
					if (summary != NULL)
						sprintf(
							summary, "Order %u, %s. %s.", de.order, de.linear ? "linear" : "nonlinear", error_text[err]
						);

					if (de.linear)
						replace_node(e, ast_MakeBinary(OP_EQUALS, de_StandardForm(&de), ast_Copy(de.g)));

					err = E_SUCCESS;
				}

				simplify_canonical_form(e, CANONICAL_ALL);
			}

			de_Cleanup(&de);
			return err;
		}
	}

	simplify(e, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL | SIMP_LIKE_TERMS);
	simplify_canonical_form(e, CANONICAL_ALL);

	return err;
}

pcas_error_t calculus_Verify(
	pcas_ast_t **items,
	unsigned count,
	pcas_ast_t *respect_to,
	pcas_ast_t *solution,
	bool *satisfied
) {
	pcas_de_t de;
	pcas_error_t err = de_LoadList(&de, items, count, respect_to);

	if (err == E_SUCCESS)
		err = de_Verify(&de, solution, satisfied);

	de_Cleanup(&de);

	return err;
}

#else
typedef int make_iso_compilers_happy;
#endif
