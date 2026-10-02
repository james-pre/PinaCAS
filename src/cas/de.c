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
