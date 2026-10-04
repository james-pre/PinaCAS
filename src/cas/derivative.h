#pragma once

#include "identities.h"

#define ID_NUM_DERIV 19
extern pcas_id_t id_derivative[ID_NUM_DERIV];

extern pcas_id_t id_deriv_power_rule;
extern pcas_id_t id_deriv_constant_rule;
extern pcas_id_t id_deriv_product_rule;

bool is_constant(const pcas_ast_t *e, const pcas_ast_t *respect_to);

/*Applies one differentiation rule to each derivative node*/
bool eval_derivative_nodes(pcas_ast_t *e);
/*Evaluates every derivative node completely, innermost first, recording each as a step*/
bool eval_derivatives(pcas_ast_t *e);
/*Replaces the node with a deriv() node and calls eval_derivative nodes*/
void derivative(pcas_ast_t *e, const pcas_ast_t *respect_to, const pcas_ast_t *eval_at);
