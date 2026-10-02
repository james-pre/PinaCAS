#pragma once

#include "../ast.h"

/*Evaluates every integral node it can, innermost first, recording each as a step*/
bool eval_integrals(pcas_ast_t *e);
bool contains_integral(pcas_ast_t *e);
/*Replaces e with its antiderivative, leaving an integral node where none is found*/
void integral(pcas_ast_t *e, pcas_ast_t *respect_to);
/*Like integral, but adds an arbitrary constant named C, or another letter if C is taken, when the antiderivative was found*/
void integral_Indefinite(pcas_ast_t *e, pcas_ast_t *respect_to);
