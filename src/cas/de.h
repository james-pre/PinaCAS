#pragma once

#include "../ast.h"
#include "../error.h"

#define DE_MAX_ORDER 8

typedef struct {
    /*Equals node as entered*/
    pcas_ast_t *equation;
    /*Independent variable and unknown function*/
    pcas_ast_t *x, *y;
    unsigned order;
    bool linear;
    /*When linear, the equation is a[order]y^(order) + ... + a[1]y' + a[0]y = g*/
    pcas_ast_t *a[DE_MAX_ORDER + 1];
    pcas_ast_t *g;
} pcas_de_t;

/*Returns y with order primes*/
pcas_ast_t *de_Derivative(pcas_ast_t *y, unsigned order);

/*Classifies equation, an equals node or an expression equal to zero, and records the classification. Copies its arguments. de_Cleanup must be called even on failure.*/
pcas_error_t de_Load(pcas_de_t *de, pcas_ast_t *equation, pcas_ast_t *x);
void de_Cleanup(pcas_de_t *de);

/*Returns the left side of the linear standard form*/
pcas_ast_t *de_StandardForm(pcas_de_t *de);
