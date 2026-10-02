#include "cas.h"

#include "../work.h"

#define SIMP_BASIC (SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL | SIMP_LIKE_TERMS)

/*Limits how deeply integration rules are chained for one integral*/
#define MAX_METHOD_DEPTH 12

static pcas_ast_t *integer(mp_small n) {
    return ast_MakeNumber(num_FromInt(n));
}

static pcas_ast_t *mul(pcas_ast_t *a, pcas_ast_t *b) {
    return ast_MakeBinary(OP_MULT, a, b);
}

static pcas_ast_t *add(pcas_ast_t *a, pcas_ast_t *b) {
    return ast_MakeBinary(OP_ADD, a, b);
}

static pcas_ast_t *quotient(pcas_ast_t *a, pcas_ast_t *b) {
    return ast_MakeBinary(OP_DIV, a, b);
}

static pcas_ast_t *power(pcas_ast_t *a, pcas_ast_t *b) {
    return ast_MakeBinary(OP_POW, a, b);
}

static pcas_ast_t *negate(pcas_ast_t *a) {
    return mul(integer(-1), a);
}

static pcas_ast_t *ln(pcas_ast_t *a) {
    return ast_MakeBinary(OP_LOG, ast_MakeSymbol(SYM_EULER), a);
}

/*Takes ownership of f*/
static pcas_ast_t *integral_node(pcas_ast_t *f, pcas_ast_t *x) {
    return ast_MakeBinary(OP_INTEGRAL, f, ast_Copy(x));
}

/*Returns the value of e if it is a number or a quotient of numbers, otherwise NULL*/
static mp_rat number_value(pcas_ast_t *e) {
    mp_rat value;

    if(e->type == NODE_NUMBER)
        return num_Copy(e->op.num);

    if(!isoptype(e, OP_DIV) || ast_ChildGet(e, 0)->type != NODE_NUMBER || ast_ChildGet(e, 1)->type != NODE_NUMBER)
        return NULL;

    value = num_Copy(ast_ChildGet(e, 0)->op.num);
    mp_rat_div(value, ast_ChildGet(e, 1)->op.num, value);
    return value;
}

static bool is_ast_fraction(pcas_ast_t *e, mp_small num, mp_small den) {
    mp_rat value = number_value(e);
    bool equal;

    if(value == NULL)
        return false;

    equal = mp_rat_compare_value(value, num, den) == 0;
    num_Cleanup(value);
    return equal;
}

static bool is_ast_symbol(pcas_ast_t *e, Symbol symbol) {
    return e->type == NODE_SYMBOL && e->op.symbol == symbol;
}

static bool contains_symbol(pcas_ast_t *e, Symbol symbol) {
    pcas_ast_t *child;

    if(e->type == NODE_SYMBOL)
        return e->op.symbol == symbol;

    if(e->type == NODE_OPERATOR) {
        for(child = ast_ChildGet(e, 0); child != NULL; child = child->next) {
            if(contains_symbol(child, symbol))
                return true;
        }
    }

    return false;
}

static void simplify_quietly(pcas_ast_t *e) {
    work_Pause();
    simplify(e, SIMP_BASIC);
    work_Resume();
}

static pcas_ast_t *derivative_of(pcas_ast_t *e, pcas_ast_t *x) {
    pcas_ast_t *d = ast_Copy(e);

    work_Pause();
    derivative(d, x, x);
    simplify(d, SIMP_BASIC);
    work_Resume();

    return d;
}

/*Returns the slope of e if e is linear in x, otherwise NULL*/
static pcas_ast_t *linear_slope(pcas_ast_t *e, pcas_ast_t *x) {
    pcas_ast_t *d;

    if(is_constant(e, x))
        return NULL;

    d = derivative_of(e, x);

    if(is_constant(d, x) && !is_ast_int(d, 0))
        return d;

    ast_Cleanup(d);
    return NULL;
}

static bool is_polynomial(pcas_ast_t *e, pcas_ast_t *x) {
    pcas_ast_t *child;

    if(is_constant(e, x) || ast_Compare(e, x))
        return true;

    if(isoptype(e, OP_ADD) || isoptype(e, OP_MULT)) {
        for(child = ast_ChildGet(e, 0); child != NULL; child = child->next) {
            if(!is_polynomial(child, x))
                return false;
        }
        return true;
    }

    if(isoptype(e, OP_POW)) {
        pcas_ast_t *exponent = ast_ChildGet(e, 1);
        return exponent->type == NODE_NUMBER
            && mp_rat_is_integer(exponent->op.num)
            && mp_rat_compare_zero(exponent->op.num) >= 0
            && is_polynomial(ast_ChildGet(e, 0), x);
    }

    return false;
}

static pcas_ast_t *reciprocal(pcas_ast_t *e) {
    mp_rat exponent;

    if(isoptype(e, OP_POW) && (exponent = number_value(ast_ChildGet(e, 1))) != NULL) {
        mp_rat_neg(exponent, exponent);
        return power(ast_Copy(ast_ChildGet(e, 0)), ast_MakeNumber(exponent));
    }

    return power(ast_Copy(e), integer(-1));
}

static void collect_factors(pcas_ast_t *e, pcas_ast_t *product, bool inverted) {
    pcas_ast_t *child;

    if(isoptype(e, OP_MULT)) {
        for(child = ast_ChildGet(e, 0); child != NULL; child = child->next)
            collect_factors(child, product, inverted);
    } else if(isoptype(e, OP_DIV)) {
        collect_factors(ast_ChildGet(e, 0), product, inverted);
        collect_factors(ast_ChildGet(e, 1), product, !inverted);
    } else {
        ast_ChildAppend(product, inverted ? reciprocal(e) : ast_Copy(e));
    }
}

/*Returns a multiplication node of the factors of e, with division written as negative powers*/
static pcas_ast_t *factors_of(pcas_ast_t *e) {
    pcas_ast_t *product = ast_MakeOperator(OP_MULT);
    collect_factors(e, product, false);
    return product;
}

/*Takes ownership of both. Returns product * e, or e if product has no factors.*/
static pcas_ast_t *times(pcas_ast_t *product, pcas_ast_t *e) {
    if(ast_ChildLength(product) == 0) {
        ast_Cleanup(product);
        return e;
    }

    ast_ChildAppend(product, e);
    return product;
}

/*Takes ownership of product. Returns its only factor, 1 if it has none, or product itself.*/
static pcas_ast_t *unwrap(pcas_ast_t *product) {
    pcas_ast_t *only;

    switch(ast_ChildLength(product)) {
    case 0:
        ast_Cleanup(product);
        return integer(1);
    case 1:
        only = ast_ChildRemoveIndex(product, 0);
        ast_Cleanup(product);
        return only;
    default:
        return product;
    }
}

/*Returns v if e is c + k*v^2 where k is 1 or -1, otherwise NULL*/
static pcas_ast_t *match_square_sum(pcas_ast_t *e, mp_small c, mp_small k) {
    pcas_ast_t *constant, *square;

    if(!isoptype(e, OP_ADD) || ast_ChildLength(e) != 2)
        return NULL;

    constant = ast_ChildGet(e, 0);
    square = ast_ChildGet(e, 1);

    if(!is_ast_int(constant, c)) {
        pcas_ast_t *swap = constant;
        constant = square;
        square = swap;

        if(!is_ast_int(constant, c))
            return NULL;
    }

    if(k == -1) {
        if(!isoptype(square, OP_MULT) || ast_ChildLength(square) != 2)
            return NULL;

        if(is_ast_int(ast_ChildGet(square, 0), -1))
            square = ast_ChildGet(square, 1);
        else if(is_ast_int(ast_ChildGet(square, 1), -1))
            square = ast_ChildGet(square, 0);
        else
            return NULL;
    }

    if(!isoptype(square, OP_POW) || !is_ast_int(ast_ChildGet(square, 1), 2))
        return NULL;

    return ast_ChildGet(square, 0);
}

/*Takes ownership of F. Divides F by the slope of u, or returns NULL if u is not linear in x.*/
static pcas_ast_t *over_slope(pcas_ast_t *F, pcas_ast_t *u, pcas_ast_t *x) {
    pcas_ast_t *slope = linear_slope(u, x);

    if(slope == NULL) {
        ast_Cleanup(F);
        return NULL;
    }

    return quotient(F, slope);
}

/*Antiderivatives of u^n where u is not linear*/
static pcas_ast_t *table_special_power(pcas_ast_t *u, pcas_ast_t *n, pcas_ast_t *x) {
    pcas_ast_t *v;

    if(is_ast_fraction(n, -2, 1) && u->type == NODE_OPERATOR) {
        v = ast_ChildGet(u, 0);

        switch(optype(u)) {
        case OP_COS:  return over_slope(ast_MakeUnary(OP_TAN, ast_Copy(v)), v, x);
        case OP_SIN:  return over_slope(negate(power(ast_MakeUnary(OP_TAN, ast_Copy(v)), integer(-1))), v, x);
        case OP_COSH: return over_slope(ast_MakeUnary(OP_TANH, ast_Copy(v)), v, x);
        default:      return NULL;
        }
    }

    if(is_ast_fraction(n, -1, 1) && (v = match_square_sum(u, 1, 1)) != NULL)
        return over_slope(ast_MakeUnary(OP_TAN_INV, ast_Copy(v)), v, x);

    if(is_ast_fraction(n, -1, 2)) {
        if((v = match_square_sum(u, 1, -1)) != NULL)
            return over_slope(ast_MakeUnary(OP_SIN_INV, ast_Copy(v)), v, x);
        if((v = match_square_sum(u, 1, 1)) != NULL)
            return over_slope(ast_MakeUnary(OP_SINH_INV, ast_Copy(v)), v, x);
        if((v = match_square_sum(u, -1, 1)) != NULL)
            return over_slope(ast_MakeUnary(OP_COSH_INV, ast_Copy(v)), v, x);
    }

    return NULL;
}

/*Antiderivative of a single non-constant factor h from the table of elementary integrals*/
static pcas_ast_t *table(pcas_ast_t *h, pcas_ast_t *x) {
    pcas_ast_t *u, *n;

    if(ast_Compare(h, x))
        return quotient(power(ast_Copy(x), integer(2)), integer(2));

    if(h->type != NODE_OPERATOR)
        return NULL;

    switch(optype(h)) {
    case OP_POW:
        u = ast_ChildGet(h, 0);
        n = ast_ChildGet(h, 1);

        if(is_constant(n, x)) {
            pcas_ast_t *slope = linear_slope(u, x);

            if(slope == NULL)
                return table_special_power(u, n, x);

            if(is_ast_fraction(n, -1, 1))
                return quotient(ln(ast_MakeUnary(OP_ABS, ast_Copy(u))), slope);

            return quotient(power(ast_Copy(u), add(ast_Copy(n), integer(1))),
                            mul(add(ast_Copy(n), integer(1)), slope));
        }

        if(is_constant(u, x)) {
            if(is_ast_symbol(u, SYM_EULER))
                return over_slope(ast_Copy(h), n, x);
            return over_slope(quotient(ast_Copy(h), ln(ast_Copy(u))), n, x);
        }

        return NULL;
    case OP_LOG: {
        pcas_ast_t *base = ast_ChildGet(h, 0);
        pcas_ast_t *F;

        u = ast_ChildGet(h, 1);

        if(!is_constant(base, x))
            return NULL;

        F = add(mul(ast_Copy(u), ln(ast_Copy(u))), negate(ast_Copy(u)));

        if(!is_ast_symbol(base, SYM_EULER))
            F = quotient(F, ln(ast_Copy(base)));

        return over_slope(F, u, x);
    }
    case OP_SIN:  u = ast_ChildGet(h, 0); return over_slope(negate(ast_MakeUnary(OP_COS, ast_Copy(u))), u, x);
    case OP_COS:  u = ast_ChildGet(h, 0); return over_slope(ast_MakeUnary(OP_SIN, ast_Copy(u)), u, x);
    case OP_TAN:  u = ast_ChildGet(h, 0); return over_slope(negate(ln(ast_MakeUnary(OP_ABS, ast_MakeUnary(OP_COS, ast_Copy(u))))), u, x);
    case OP_SINH: u = ast_ChildGet(h, 0); return over_slope(ast_MakeUnary(OP_COSH, ast_Copy(u)), u, x);
    case OP_COSH: u = ast_ChildGet(h, 0); return over_slope(ast_MakeUnary(OP_SINH, ast_Copy(u)), u, x);
    case OP_TANH: u = ast_ChildGet(h, 0); return over_slope(ln(ast_MakeUnary(OP_COSH, ast_Copy(u))), u, x);
    default:
        return NULL;
    }
}

static void expand_quietly(pcas_ast_t *e) {
    work_Pause();
    expand(e, EXP_ALL);
    simplify(e, SIMP_BASIC);
    work_Resume();
}

/*Antiderivative using linearity, the table, and polynomial expansion, or NULL*/
static pcas_ast_t *elementary(pcas_ast_t *f, pcas_ast_t *x) {
    pcas_ast_t *product, *constants, *variable = NULL, *child, *F = NULL;
    unsigned variables = 0;

    if(is_constant(f, x))
        return mul(ast_Copy(f), ast_Copy(x));

    if(isoptype(f, OP_ADD)) {
        F = ast_MakeOperator(OP_ADD);

        for(child = ast_ChildGet(f, 0); child != NULL; child = child->next) {
            pcas_ast_t *term = elementary(child, x);

            if(term == NULL) {
                ast_Cleanup(F);
                return NULL;
            }

            ast_ChildAppend(F, term);
        }

        return F;
    }

    product = factors_of(f);
    constants = ast_MakeOperator(OP_MULT);

    for(child = ast_ChildGet(product, 0); child != NULL; child = child->next) {
        if(is_constant(child, x)) {
            if(!is_ast_int(child, 1))
                ast_ChildAppend(constants, ast_Copy(child));
        } else {
            variable = child;
            variables++;
        }
    }

    if(variables == 1 && (F = table(variable, x)) != NULL)
        F = times(constants, F);
    else
        ast_Cleanup(constants);

    ast_Cleanup(product);

    if(F == NULL && is_polynomial(f, x)) {
        pcas_ast_t *expanded = ast_Copy(f);
        expand_quietly(expanded);

        if(isoptype(expanded, OP_ADD))
            F = elementary(expanded, x);

        ast_Cleanup(expanded);
    }

    return F;
}

/*Returns the integral of f with its constant factors moved outside*/
static pcas_ast_t *pull_constants(pcas_ast_t *f, pcas_ast_t *x, bool *pulled) {
    pcas_ast_t *product = factors_of(f);
    pcas_ast_t *constants = ast_MakeOperator(OP_MULT);
    pcas_ast_t *rest = ast_MakeOperator(OP_MULT);
    pcas_ast_t *child;

    for(child = ast_ChildGet(product, 0); child != NULL; child = child->next) {
        if(!is_constant(child, x))
            ast_ChildAppend(rest, ast_Copy(child));
        else if(!is_ast_int(child, 1))
            ast_ChildAppend(constants, ast_Copy(child));
    }

    ast_Cleanup(product);

    if(ast_ChildLength(constants) > 0)
        *pulled = true;

    return times(constants, integral_node(unwrap(rest), x));
}

/*Integrates the terms of f that are elementary and pulls out constant factors, leaving integral nodes for the rest*/
static pcas_ast_t *split(pcas_ast_t *f, pcas_ast_t *x, bool *progress) {
    pcas_ast_t *child;

    if(isoptype(f, OP_ADD)) {
        pcas_ast_t *sum = ast_MakeOperator(OP_ADD);

        *progress = true;

        for(child = ast_ChildGet(f, 0); child != NULL; child = child->next) {
            pcas_ast_t *F = elementary(child, x);
            ast_ChildAppend(sum, F != NULL ? F : pull_constants(child, x, progress));
        }

        return sum;
    }

    return pull_constants(f, x, progress);
}

/*Returns a symbol that does not appear in e*/
static Symbol fresh_symbol(pcas_ast_t *e) {
    const char *candidates = "UVWTSRQPNMKJHGFDCBA";
    unsigned i;

    for(i = 0; candidates[i] != '\0'; i++) {
        if(!contains_symbol(e, (Symbol)candidates[i]))
            return (Symbol)candidates[i];
    }

    return SYM_INVALID;
}

/*Returns the argument of h that could be the inner function of a substitution*/
static pcas_ast_t *inner_candidate(pcas_ast_t *h, pcas_ast_t *x) {
    if(h->type != NODE_OPERATOR)
        return NULL;

    switch(optype(h)) {
    case OP_POW:
        if(is_constant(ast_ChildGet(h, 1), x))
            return ast_ChildGet(h, 0);
        if(is_constant(ast_ChildGet(h, 0), x))
            return ast_ChildGet(h, 1);
        return NULL;
    case OP_LOG:
        return ast_ChildGet(h, 1);
    default:
        return is_op_function(optype(h)) ? ast_ChildGet(h, 0) : NULL;
    }
}

/*Integrates g(v(x))v'(x) by substituting u = v(x). Records its steps.*/
static pcas_ast_t *substitution(pcas_ast_t *f, pcas_ast_t *x) {
    pcas_ast_t *product = factors_of(f);
    pcas_ast_t *result = NULL;
    unsigned i, length = ast_ChildLength(product);
    Symbol symbol = fresh_symbol(f);

    if(symbol == SYM_INVALID) {
        ast_Cleanup(product);
        return NULL;
    }

    for(i = 0; i < length && result == NULL; i++) {
        pcas_ast_t *h = ast_ChildGet(product, i);
        pcas_ast_t *v = inner_candidate(h, x);
        pcas_ast_t *rest, *ratio, *u, *g, *G, *slope;

        if(v == NULL || is_constant(v, x))
            continue;

        if((slope = linear_slope(v, x)) != NULL) {
            ast_Cleanup(slope);
            continue;
        }

        rest = ast_Copy(product);
        ast_Cleanup(ast_ChildRemoveIndex(rest, i));

        ratio = quotient(unwrap(rest), derivative_of(v, x));
        simplify_quietly(ratio);

        if(!is_constant(ratio, x)) {
            ast_Cleanup(ratio);
            continue;
        }

        u = ast_MakeSymbol(symbol);
        g = ast_Copy(h);

        work_Pause();
        substitute(g, v, u);
        simplify(g, SIMP_BASIC);
        work_Resume();

        if(is_constant(g, x) && (G = elementary(g, u)) != NULL) {
            pcas_ast_t *before = integral_node(ast_Copy(f), x);
            pcas_ast_t *rewritten = mul(ast_Copy(ratio), integral_node(ast_Copy(g), u));

            simplify_quietly(G);
            simplify_quietly(rewritten);

            work_Step(STEP_EQUATION, "Substitute", u, v);
            work_Step(STEP_INTEGRAL, "Substitution", before, rewritten);

            ast_Cleanup(rewritten);
            rewritten = integral_node(ast_Copy(g), u);
            work_Step(STEP_INTEGRAL, NULL, rewritten, G);
            ast_Cleanup(rewritten);

            result = mul(ast_Copy(ratio), G);

            work_Pause();
            substitute(result, u, v);
            work_Resume();

            simplify_quietly(result);
            work_Step(STEP_INTEGRAL, "Back-substitute", before, result);

            ast_Cleanup(before);
        }

        ast_Cleanup(g);
        ast_Cleanup(u);
        ast_Cleanup(ratio);
    }

    ast_Cleanup(product);
    return result;
}

static unsigned liate_rank(pcas_ast_t *h, pcas_ast_t *x) {
    if(isoptype(h, OP_LOG) && is_constant(ast_ChildGet(h, 0), x))
        return 5;

    if(h->type == NODE_OPERATOR) {
        switch(optype(h)) {
        case OP_SIN_INV: case OP_COS_INV: case OP_TAN_INV:
        case OP_SINH_INV: case OP_COSH_INV: case OP_TANH_INV:
            return 4;
        default:
            break;
        }
    }

    if(is_polynomial(h, x))
        return 3;

    return 0;
}

/*Rewrites the integral of f by parts, choosing u by LIATE. Records the step.*/
static pcas_ast_t *by_parts(pcas_ast_t *f, pcas_ast_t *x) {
    pcas_ast_t *product = factors_of(f);
    pcas_ast_t *u = NULL, *dv, *v, *du, *remaining, *rewritten, *before;
    unsigned i, best = 0, best_index = 0;
    bool pulled = false;

    for(i = 0; i < ast_ChildLength(product); i++) {
        pcas_ast_t *factor = ast_ChildGet(product, i);
        unsigned rank;

        if(is_constant(factor, x))
            continue;

        rank = liate_rank(factor, x);
        if(rank > best) {
            best = rank;
            best_index = i;
        }
    }

    if(best == 0) {
        ast_Cleanup(product);
        return NULL;
    }

    u = ast_ChildRemoveIndex(product, best_index);
    dv = unwrap(product);

    if((v = elementary(dv, x)) == NULL) {
        ast_Cleanup(u);
        ast_Cleanup(dv);
        return NULL;
    }

    simplify_quietly(v);
    du = derivative_of(u, x);

    remaining = mul(ast_Copy(v), du);
    simplify_quietly(remaining);

    rewritten = add(mul(u, v), negate(pull_constants(remaining, x, &pulled)));
    ast_Cleanup(remaining);
    simplify_quietly(rewritten);

    before = integral_node(ast_Copy(f), x);
    work_Step(STEP_INTEGRAL, "By parts", before, rewritten);
    ast_Cleanup(before);

    ast_Cleanup(dv);
    return rewritten;
}

static bool integrate_all(pcas_ast_t *e, unsigned budget);

/*Evaluates the integral node e in place. Returns true if anything was integrated.*/
static bool integrate_node(pcas_ast_t *e, unsigned budget) {
    pcas_ast_t *f, *x, *F, *before;
    bool progress = false;

    if(budget == 0)
        return false;

    f = ast_Copy(ast_ChildGet(e, 0));
    x = ast_Copy(ast_ChildGet(e, 1));
    before = ast_Copy(e);

    simplify_quietly(f);

    if((F = elementary(f, x)) != NULL) {
        simplify_quietly(F);
        work_Step(STEP_INTEGRAL, NULL, before, F);
        replace_node(e, F);
        progress = true;
    } else if(!isoptype(f, OP_ADD) && ((F = substitution(f, x)) != NULL || (F = by_parts(f, x)) != NULL)) {
        replace_node(e, F);
        integrate_all(e, budget - 1);
        simplify_quietly(e);
        progress = true;
    } else {
        F = split(f, x, &progress);

        if(progress) {
            simplify_quietly(F);
            work_Step(STEP_INTEGRAL, NULL, before, F);
            replace_node(e, F);
            integrate_all(e, budget - 1);
        } else {
            ast_Cleanup(F);
        }
    }

    ast_Cleanup(f);
    ast_Cleanup(x);
    ast_Cleanup(before);

    return progress;
}

static bool integrate_all(pcas_ast_t *e, unsigned budget) {
    pcas_ast_t *child;
    bool changed = false;

    if(e->type != NODE_OPERATOR)
        return false;

    for(child = ast_ChildGet(e, 0); child != NULL; child = child->next)
        changed |= integrate_all(child, budget);

    if(isoptype(e, OP_INTEGRAL))
        changed |= integrate_node(e, budget);

    return changed;
}

bool eval_integrals(pcas_ast_t *e) {
    return integrate_all(e, MAX_METHOD_DEPTH);
}

void integral(pcas_ast_t *e, pcas_ast_t *respect_to) {
    pcas_ast_t *node;

    work_Enter(e);

    node = integral_node(ast_Copy(e), respect_to);
    eval_integrals(node);
    replace_node(e, node);

    work_Leave(e);
}
