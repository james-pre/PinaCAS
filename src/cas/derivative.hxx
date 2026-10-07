#pragma once

#include "identities.hxx"

namespace id {

extern Identity derivative[];

extern Identity deriv_power_rule;
extern Identity deriv_exponential_rule;
extern Identity deriv_constant_rule;
extern Identity deriv_product_rule;

} // namespace id

bool is_constant(const ast *e, const ast *respect_to);

/*Applies one differentiation rule to each derivative node*/
bool eval_derivative_nodes(ast *e);
/*Evaluates every derivative node completely, innermost first, recording each as a step*/
bool eval_derivatives(ast *e);
/*Replaces the node with a deriv() node and calls eval_derivative nodes*/
void derivative(ast *e, const ast *respect_to, const ast *eval_at);
