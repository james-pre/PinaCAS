#include "cas.hxx"

#include "../work.hxx"

/*Sum rule, constant rule, product rule, and power rule are hardcoded for speed or because of limitations in identity searching*/
id::Identity id::derivative[] = {
	{"deriv(X,X,T", "1"},
	{"deriv(integ(A,X),X,T", "A"},

	{"deriv(A/B,X,T", "(deriv(A,X,T)B_deriv(B,X,T)A)/B^2"},

	{"deriv(e^A,X,T", "deriv(A,X,T)e^A"},
	{"deriv(abs(A,X,T", "deriv(A,X,T)abs(A)/A"},

	{"deriv(ln(A,X,T", "deriv(A,X,T)/A"},
	{"deriv(logb(A,B),X,T", "deriv(ln(A)/ln(B,X,T"},

	{"deriv(sin(A,X,T", "cos(A)deriv(A,X,T"},
	{"deriv(cos(A,X,T", "-sin(A)deriv(A,X,T"},
	{"deriv(tan(A,X,T", "deriv(A,X,T)/cos(A)^2"},

	{"deriv(asin(A,X,T", "deriv(A,X,T)/sqrt(1_A^2"},
	{"deriv(acos(A,X,T", "-deriv(A,X,T)/sqrt(1_A^2"},
	{"deriv(atan(A,X,T", "deriv(A,X,T)/(1+A^2"},

	{"deriv(sinh(A,X,T", "deriv(A,X,T)cosh(A"},
	{"deriv(cosh(A,X,T", "deriv(A,X,T)sinh(A"},
	{"deriv(tanh(A,X,T", "deriv(A,X,T)/cosh(A)^2"},

	{"deriv(asinh(A,X,T", "deriv(A,X,T)/sqrt(X^2+1"},
	{"deriv(acosh(A,X,T", "deriv(A,X,T)/sqrt(X^2_1"},
	{"deriv(atanh(A,X,T", "deriv(A,X,T)/(1_X^2"},
	{nullptr}
};

/*Identities we have to check manually because we have to check for constants*/
id::Identity id::deriv_power_rule = {"deriv(A^B,X,T", "deriv(A,X,T)BA^(B_1"};
id::Identity id::deriv_exponential_rule = {"deriv(B^A,X,T", "deriv(e^(Aln(B,X,T"};
id::Identity id::deriv_constant_rule = {"deriv(CX,X,T", "C"};
id::Identity id::deriv_product_rule = {"deriv(AB,X,T", "Aderiv(B,X,T)+Bderiv(A,X,T"};

bool is_constant(const ast *e, const ast *respect_to) {
	if (e->compare(*respect_to))
		return false;

	if (e->isOperator()) {
		for (const ast *child : e->children()) {
			if (!is_constant(child, respect_to))
				return false;
		}
	}

	return true;
}

bool eval_derivative_nodes(ast *e) {
	bool changed = false;

	if (!e->isOperator())
		return false;

	for (ast *child : e->children())
		changed |= eval_derivative_nodes(child);

	if (!e->isOp(Op::Deriv))
		return changed;

	const ast *expr = e->childAt(0);
	/*Have to copy these because node might change away from deriv node*/
	ast *respect_to = e->childAt(1)->copy();
	ast *at = e->childAt(2)->copy();

	/*Hardcode constant rule*/
	if (is_constant(expr, respect_to)) {
		e->replace(ast::make(num_FromInt(0)));
		ast::dispose(respect_to);
		ast::dispose(at);
		return true;
	}
	/*Hardcode multiplication rules*/
	else if (expr->isOp(Op::Mult)) {
		ast *copy = e->copy();
		changed |= id::execute(copy, &id::deriv_constant_rule, false);

		/*Multiplication of a constant*/
		if (is_constant(copy, respect_to)) {
			e->replace(copy);
			changed = true;
		}
		/*Apply product rule*/
		else {
			ast::dispose(copy);
			changed |= id::execute(e, &id::deriv_product_rule, false);
		}
	}
	/*Hardcode sum rule*/
	else if (expr->isOp(Op::Add)) {
		ast *n = ast::make(Op::Add);

		for (const ast *child : expr->children()) {
			ast *deriv = ast::make(Op::Deriv);

			deriv->appendChild(child->copy());
			deriv->appendChild(respect_to->copy());
			deriv->appendChild(at->copy());
			n->appendChild(deriv);
		}

		e->replace(n);
		changed = true;
	}
	/*Hardcode power rule because we have to check if the power is a constant*/
	else if (expr->isOp(Op::Pow) && is_constant(expr->childAt(1), respect_to)) {
		changed |= id::execute(e, &id::deriv_power_rule, false);
	} else if (expr->isOp(Op::Pow) && !(expr->firstChild()->isSymbol() && expr->firstChild()->symbol() == Sym::Euler)) {
		changed |= id::execute(e, &id::deriv_exponential_rule, false);
	} else {
		/*While is necessary because of power rules.*/
		while (id::executeTable(e, id::derivative, false))
			changed = true;
	}

	for (ast *child : e->children())
		changed |= eval_derivative_nodes(child);

	if (changed) {
		if (!respect_to->compare(*at))
			substitute(e, respect_to, at);
	}

	ast::dispose(respect_to);
	ast::dispose(at);

	return changed;
}

bool eval_derivatives(ast *e) {
	bool changed = false;

	if (!e->isOperator())
		return false;

	for (ast *child : e->children())
		changed |= eval_derivatives(child);

	if (!e->isOp(Op::Deriv))
		return changed;

	ast *before = e->copy();

	work::pause();
	while (eval_derivative_nodes(e))
		;
	simplify(e, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval | Simp::LikeTerms);
	work::resume();

	work::step(work::Step::Type::Derivative, nullptr, before, e);
	ast::dispose(before);

	return true;
}

void derivative(ast *e, const ast *respect_to, const ast *eval_at) {
	ast *deriv_node = ast::make(Op::Deriv);

	work::enter(e);

	deriv_node->appendChild(e->copy());          /*value to take the derivative of*/
	deriv_node->appendChild(respect_to->copy()); /*variable in respect to*/
	deriv_node->appendChild(eval_at->copy());    /*evaluate at*/

	eval_derivatives(deriv_node);

	e->replace(deriv_node);

	work::leave(e);
}
