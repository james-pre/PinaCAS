#pragma once

#include "../ast.h"

/*Evaluates every integral node it can, innermost first, recording each as a step*/
bool eval_integrals(pcas_ast_t *e);
/*Replaces e with its antiderivative, leaving an integral node where none is found*/
void integral(pcas_ast_t *e, pcas_ast_t *respect_to);
