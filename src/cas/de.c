#include "cas.h"

#include "../work.h"

#define SIMP_BASIC (SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL | SIMP_LIKE_TERMS)

static const char *order_names[DE_MAX_ORDER] = {
    "First order", "Second order", "Third order", "Fourth order",
    "Fifth order", "Sixth order", "Seventh order", "Eighth order"
};

pcas_ast_t *de_Derivative(pcas_ast_t *y, unsigned order) {
    pcas_ast_t *d = ast_Copy(y);

    while(order-- > 0)
        d = ast_MakeUnary(OP_PRIME, d);

    return d;
}

/*Finds the function that primes are applied to and the highest number of primes on it*/
static pcas_error_t find_function(pcas_ast_t *e, pcas_ast_t **y, unsigned *order) {
    pcas_ast_t *child;
    pcas_error_t err;

    if(isoptype(e, OP_PRIME)) {
        unsigned primes = 0;

        while(isoptype(e, OP_PRIME)) {
            e = ast_ChildGet(e, 0);
            primes++;
        }

        if(e->type != NODE_SYMBOL || (*y != NULL && !ast_Compare(*y, e)))
            return E_DE_BAD_FUNCTION;

        *y = e;
        if(primes > *order)
            *order = primes;

        return E_SUCCESS;
    }

    if(e->type == NODE_OPERATOR) {
        for(child = ast_ChildGet(e, 0); child != NULL; child = child->next) {
            if((err = find_function(child, y, order)) != E_SUCCESS)
                return err;
        }
    }

    return E_SUCCESS;
}

/*Replaces y, y', ..., y^(order) in f with distinct symbols that are not x, so the derivatives can be treated as variables*/
static void derivatives_to_symbols(pcas_de_t *de, pcas_ast_t *f, pcas_ast_t **symbols) {
    pcas_ast_t *scope = ast_MakeBinary(OP_ADD, ast_Copy(f), ast_Copy(de->x));
    unsigned k;

    for(k = de->order; k > 0; k--) {
        pcas_ast_t *derivative = de_Derivative(de->y, k);

        symbols[k] = ast_MakeSymbol(fresh_symbol(scope));
        substitute(scope, derivative, symbols[k]);
        substitute(f, derivative, symbols[k]);

        ast_Cleanup(derivative);
    }

    symbols[0] = ast_Copy(de->y);
    ast_Cleanup(scope);
}

/*Fills in the coefficients if f, the left side minus the right side, is linear in the derivatives*/
static bool linear_form(pcas_de_t *de, pcas_ast_t *f) {
    pcas_ast_t *symbols[DE_MAX_ORDER + 1];
    unsigned j, k;
    bool linear = true;

    derivatives_to_symbols(de, f, symbols);
    simplify(f, SIMP_BASIC);

    for(k = 0; k <= de->order; k++) {
        de->a[k] = ast_Copy(f);
        derivative(de->a[k], symbols[k], symbols[k]);
        simplify(de->a[k], SIMP_BASIC);

        for(j = 0; j <= de->order; j++)
            linear &= is_constant(de->a[k], symbols[j]);
    }

    if(linear) {
        de->g = ast_MakeBinary(OP_MULT, ast_MakeNumber(num_FromInt(-1)), ast_Copy(f));

        for(k = 0; k <= de->order; k++) {
            pcas_ast_t *zero = ast_MakeNumber(num_FromInt(0));
            substitute(de->g, symbols[k], zero);
            ast_Cleanup(zero);
        }

        simplify(de->g, SIMP_BASIC);
    } else {
        for(k = 0; k <= de->order; k++) {
            ast_Cleanup(de->a[k]);
            de->a[k] = NULL;
        }
    }

    for(k = 0; k <= de->order; k++)
        ast_Cleanup(symbols[k]);

    return linear;
}

pcas_error_t de_Load(pcas_de_t *de, pcas_ast_t *equation, pcas_ast_t *x) {
    pcas_ast_t *f, *y = NULL;
    pcas_error_t err;
    unsigned k;

    de->x = ast_Copy(x);
    de->y = NULL;
    de->order = 0;
    de->linear = false;
    de->g = NULL;
    de->condition_count = 0;
    for(k = 0; k <= DE_MAX_ORDER; k++)
        de->a[k] = NULL;

    if(isoptype(equation, OP_EQUALS))
        de->equation = ast_Copy(equation);
    else
        de->equation = ast_MakeBinary(OP_EQUALS, ast_Copy(equation), ast_MakeNumber(num_FromInt(0)));

    if((err = find_function(de->equation, &y, &de->order)) != E_SUCCESS)
        return err;
    if(y == NULL)
        return E_DE_NO_DERIVATIVE;
    if(ast_Compare(y, x))
        return E_DE_BAD_FUNCTION;
    if(de->order > DE_MAX_ORDER)
        return E_DE_ORDER;

    de->y = ast_Copy(y);
    canonical_SetFunction(y->op.symbol);

    work_Step(STEP_EQUATION, NULL, ast_ChildGet(de->equation, 0), ast_ChildGet(de->equation, 1));
    work_Text(order_names[de->order - 1]);

    f = ast_MakeBinary(OP_ADD,
            ast_Copy(ast_ChildGet(de->equation, 0)),
            ast_MakeBinary(OP_MULT, ast_MakeNumber(num_FromInt(-1)), ast_Copy(ast_ChildGet(de->equation, 1))));

    work_Pause();
    de->linear = linear_form(de, f);
    work_Resume();

    ast_Cleanup(f);

    if(de->linear) {
        pcas_ast_t *standard = de_StandardForm(de);
        work_Step(STEP_EQUATION, "Linear", standard, de->g);
        ast_Cleanup(standard);
    } else {
        work_Text("Nonlinear");
    }

    return E_SUCCESS;
}

void de_Cleanup(pcas_de_t *de) {
    unsigned k;

    ast_Cleanup(de->equation);
    ast_Cleanup(de->x);
    ast_Cleanup(de->y);
    ast_Cleanup(de->g);

    for(k = 0; k <= DE_MAX_ORDER; k++)
        ast_Cleanup(de->a[k]);

    for(k = 0; k < de->condition_count; k++) {
        ast_Cleanup(de->conditions[k].at);
        ast_Cleanup(de->conditions[k].value);
    }

    canonical_SetFunction(SYM_INVALID);
}

pcas_ast_t *de_StandardForm(pcas_de_t *de) {
    pcas_ast_t *sum = ast_MakeOperator(OP_ADD);
    unsigned k;

    for(k = de->order + 1; k-- > 0;) {
        if(!is_ast_int(de->a[k], 0))
            ast_ChildAppend(sum, ast_MakeBinary(OP_MULT, ast_Copy(de->a[k]), de_Derivative(de->y, k)));
    }

    work_Pause();
    simplify(sum, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_EVAL);
    work_Resume();

    return sum;
}

/*Returns the number of primes if e is a derivative of y, or -1*/
static int derivative_order(pcas_ast_t *e, pcas_ast_t *y) {
    int order = 0;

    while(isoptype(e, OP_PRIME)) {
        e = opbase(e);
        order++;
    }

    return ast_Compare(e, y) ? order : -1;
}

pcas_error_t de_LoadList(pcas_de_t *de, pcas_ast_t **items, unsigned count, pcas_ast_t *x) {
    pcas_error_t err;
    unsigned i;

    err = de_Load(de, items[0], x);

    for(i = 1; i < count && err == E_SUCCESS; i++)
        err = de_AddCondition(de, items[i]);

    return err;
}

pcas_error_t de_AddCondition(pcas_de_t *de, pcas_ast_t *condition) {
    pcas_ast_t *left;
    pcas_condition_t *c;
    int order;

    if(de->condition_count == DE_MAX_CONDITIONS || !isoptype(condition, OP_EQUALS))
        return E_DE_BAD_CONDITION;

    /*Y(0) is parsed as Y*0*/
    left = opbase(condition);
    if(!isoptype(left, OP_MULT) || ast_ChildLength(left) != 2)
        return E_DE_BAD_CONDITION;

    order = derivative_order(opbase(left), de->y);
    if(order < 0)
        return E_DE_BAD_CONDITION;

    c = &de->conditions[de->condition_count++];
    c->order = (unsigned)order;
    c->at = ast_Copy(opbase(left)->next);
    c->value = ast_Copy(left->next);

    work_Pause();
    simplify(c->at, SIMP_BASIC);
    simplify(c->value, SIMP_BASIC);
    work_Resume();

    return E_SUCCESS;
}

/*Writes e as one fraction num/den, without simplifying*/
static void rational_parts(pcas_ast_t *e, pcas_ast_t **num, pcas_ast_t **den) {
    pcas_ast_t *child;

    if(isoptype(e, OP_DIV)) {
        pcas_ast_t *n1, *d1, *n2, *d2;

        rational_parts(opbase(e), &n1, &d1);
        rational_parts(opbase(e)->next, &n2, &d2);

        *num = ast_MakeBinary(OP_MULT, n1, d2);
        *den = ast_MakeBinary(OP_MULT, d1, n2);
    } else if(isoptype(e, OP_MULT)) {
        *num = ast_MakeOperator(OP_MULT);
        *den = ast_MakeOperator(OP_MULT);

        for(child = opbase(e); child != NULL; child = child->next) {
            pcas_ast_t *n, *d;
            rational_parts(child, &n, &d);
            ast_ChildAppend(*num, n);
            ast_ChildAppend(*den, d);
        }
    } else if(isoptype(e, OP_ADD)) {
        pcas_ast_t *nums = ast_MakeOperator(OP_ADD), *dens = ast_MakeOperator(OP_MULT);
        pcas_ast_t *n, *d, *other;

        for(child = opbase(e); child != NULL; child = child->next) {
            rational_parts(child, &n, &d);
            ast_ChildAppend(nums, n);
            ast_ChildAppend(dens, d);
        }

        *num = ast_MakeOperator(OP_ADD);

        for(n = opbase(nums), d = opbase(dens); n != NULL; n = n->next, d = d->next) {
            pcas_ast_t *term = ast_MakeOperator(OP_MULT);

            ast_ChildAppend(term, ast_Copy(n));
            for(other = opbase(dens); other != NULL; other = other->next) {
                if(other != d)
                    ast_ChildAppend(term, ast_Copy(other));
            }

            ast_ChildAppend(*num, term);
        }

        ast_Cleanup(nums);
        *den = dens;
    } else if(isoptype(e, OP_POW) && opbase(e)->next->type == NODE_NUMBER && mp_rat_is_integer(opbase(e)->next->op.num)) {
        pcas_ast_t *n, *d;
        mp_rat exponent = num_Copy(opbase(e)->next->op.num);
        bool negative = mp_rat_compare_zero(exponent) < 0;

        mp_rat_abs(exponent, exponent);
        rational_parts(opbase(e), &n, &d);

        *num = ast_MakeBinary(OP_POW, negative ? d : n, ast_MakeNumber(num_Copy(exponent)));
        *den = ast_MakeBinary(OP_POW, negative ? n : d, ast_MakeNumber(exponent));
    } else {
        *num = ast_Copy(e);
        *den = ast_MakeNumber(num_FromInt(1));
    }
}

/*True if the numerator of e over a common denominator expands to zero after simplifying e with flags*/
static bool numerator_vanishes(pcas_ast_t *e, unsigned short flags) {
    pcas_ast_t *copy = ast_Copy(e), *numerator, *denominator;
    bool zero;

    simplify(copy, flags);
    rational_parts(copy, &numerator, &denominator);

    expand(numerator, EXP_ALL);
    simplify(numerator, SIMP_BASIC);
    zero = is_ast_int(numerator, 0);

    ast_Cleanup(copy);
    ast_Cleanup(numerator);
    ast_Cleanup(denominator);

    return zero;
}

/*True if e simplifies to zero, trying identities only when needed*/
static bool is_zero(pcas_ast_t *e) {
    bool zero;

    work_Pause();
    zero = numerator_vanishes(e, SIMP_BASIC) || numerator_vanishes(e, SIMP_ALL);
    work_Resume();

    return zero;
}

static bool involves_function(pcas_ast_t *e, pcas_ast_t *y) {
    pcas_ast_t *child;

    if(ast_Compare(e, y))
        return true;

    if(e->type == NODE_OPERATOR) {
        for(child = opbase(e); child != NULL; child = child->next) {
            if(involves_function(child, y))
                return true;
        }
    }

    return false;
}

/*Replaces each derivative of y in e with its value in derivatives*/
static void substitute_derivatives(pcas_de_t *de, pcas_ast_t *e, pcas_ast_t **derivatives) {
    unsigned k;

    for(k = de->order + 1; k-- > 0;) {
        pcas_ast_t *d = de_Derivative(de->y, k);
        substitute(e, d, derivatives[k]);
        ast_Cleanup(d);
    }
}

/*Substitutes the derivatives into one side of the equation and simplifies it, recording each form*/
static pcas_ast_t *evaluate_side(pcas_de_t *de, pcas_ast_t *side, pcas_ast_t **derivatives, const char *label) {
    pcas_ast_t *e = ast_Copy(side);

    if(!involves_function(e, de->y))
        return e;

    work_Text(label);
    work_Step(STEP_STATE, NULL, NULL, e);

    work_Pause();
    substitute_derivatives(de, e, derivatives);
    work_Resume();

    work_Step(STEP_STATE, NULL, NULL, e);
    simplify(e, SIMP_BASIC);

    return e;
}

static bool check_condition(pcas_de_t *de, pcas_condition_t *c, pcas_ast_t **derivatives) {
    pcas_ast_t *at, *substituted, *value, *chain;
    bool holds;

    at = ast_MakeBinary(OP_AT, de_Derivative(de->y, c->order), ast_Copy(c->at));
    substituted = ast_Copy(derivatives[c->order]);

    work_Pause();
    substitute(substituted, de->x, c->at);
    value = ast_Copy(substituted);
    simplify(value, SIMP_BASIC);
    if(!ast_Compare(value, c->value))
        simplify(value, SIMP_ALL);
    work_Resume();

    holds = is_zero(chain = ast_MakeBinary(OP_ADD, ast_Copy(value), ast_MakeBinary(OP_MULT, ast_MakeNumber(num_FromInt(-1)), ast_Copy(c->value))));
    ast_Cleanup(chain);

    chain = ast_MakeBinary(OP_EQUALS, substituted, value);
    work_Step(STEP_EQUATION, holds ? "Initial condition holds" : "Initial condition fails", at, chain);

    ast_Cleanup(at);
    ast_Cleanup(chain);

    return holds;
}

pcas_error_t de_Verify(pcas_de_t *de, pcas_ast_t *solution, bool *satisfied) {
    pcas_ast_t *derivatives[DE_MAX_ORDER + 1];
    pcas_ast_t *f = solution, *left, *right, *difference;
    unsigned k;

    if(isoptype(f, OP_EQUALS) && ast_Compare(opbase(f), de->y))
        f = opbase(f)->next;

    if(involves_function(f, de->y))
        return E_DE_IMPLICIT;

    work_Step(STEP_EQUATION, "Solution", de->y, f);

    derivatives[0] = ast_Copy(f);

    for(k = 1; k <= de->order; k++) {
        pcas_ast_t *d = ast_Copy(derivatives[k - 1]);
        pcas_ast_t *prime = de_Derivative(de->y, k);
        pcas_ast_t *chain;

        work_Pause();
        derivative(d, de->x, de->x);
        simplify(d, SIMP_BASIC);
        work_Resume();

        chain = ast_MakeOperator(OP_DERIV);
        ast_ChildAppend(chain, ast_Copy(derivatives[k - 1]));
        ast_ChildAppend(chain, ast_Copy(de->x));
        ast_ChildAppend(chain, ast_Copy(de->x));
        chain = ast_MakeBinary(OP_EQUALS, chain, ast_Copy(d));

        work_Step(STEP_EQUATION, k == 1 ? "Differentiate" : NULL, prime, chain);

        ast_Cleanup(prime);
        ast_Cleanup(chain);

        derivatives[k] = d;
    }

    left = evaluate_side(de, opbase(de->equation), derivatives, "Left side");
    right = evaluate_side(de, opbase(de->equation)->next, derivatives, "Right side");

    difference = ast_MakeBinary(OP_ADD, ast_Copy(left), ast_MakeBinary(OP_MULT, ast_MakeNumber(num_FromInt(-1)), ast_Copy(right)));
    *satisfied = is_zero(difference);
    ast_Cleanup(difference);

    work_Step(STEP_EQUATION, *satisfied ? "Satisfies the equation" : "Does not satisfy the equation", left, right);

    for(k = 0; k < de->condition_count; k++)
        *satisfied &= check_condition(de, &de->conditions[k], derivatives);

    work_Text(*satisfied ? "It is a solution" : "It is not a solution");

    for(k = 0; k <= de->order; k++)
        ast_Cleanup(derivatives[k]);
    ast_Cleanup(left);
    ast_Cleanup(right);

    return E_SUCCESS;
}
