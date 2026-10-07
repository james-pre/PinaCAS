#pragma once

#include "../ast.hxx"

/*Evaluates every integral node it can, innermost first, recording each as a step*/
bool eval_integrals(ast *e);
bool contains_integral(const ast *e);
/*Replaces e with its antiderivative, leaving an integral node where none is found*/
void integral(ast *e, const ast *respect_to);
/*Like integral, but adds an arbitrary constant named C, or another letter if C is taken, when the antiderivative was found*/
void integral_Indefinite(ast *e, const ast *respect_to);
