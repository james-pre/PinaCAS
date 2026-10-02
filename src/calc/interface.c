#ifndef COMPILE_PC

#include "interface.h"

#include <stdlib.h>
#include <string.h>

#include "../ast.h"
#include "../parser.h"
#include "../cas/cas.h"

#define TI_SPACE 0x29
#define TI_COMMA 0x2B

/*Returns a new string with TI spaces removed*/
uint8_t *trim(uint8_t *input, unsigned input_len, unsigned *trimmed_len) {
    unsigned i, trim_index = 0;
    uint8_t *trimmed;

    *trimmed_len = 0;

    for(i = 0; i < input_len; i++) {
        if(input[i] != TI_SPACE)
            (*trimmed_len)++;
    }

    trimmed = malloc((*trimmed_len) * sizeof(char));

    for(i = 0; i < input_len; i++) {
        if(input[i] != TI_SPACE)
            trimmed[trim_index++] = input[i];
    }

    return trimmed;
}

bool is_var_string_type(ti_var_t var) {
    /*Check if 4 low bits from first byte of vat data is 0x4*/
    return (*((uint8_t*)ti_GetVATPtr(var)) & 0xFu) == 0x4u;
}

/*Read trimmed string from Ans variable*/
uint8_t *read_ans(unsigned *ans_len) {
    ti_var_t v;
    const uint8_t *data;
    uint8_t *str, *trimmed = NULL;

    uint16_t size;

    ti_CloseAll();

    v = ti_OpenVar(ti_Ans, "r", TI_STRING_TYPE);

    /*If Ans is a string*/
    if(is_var_string_type(v)) {
        data = ti_GetDataPtr(v);
        size = ti_GetSize(v);

        str = malloc(sizeof(char) * size);
        memcpy(str, data, size);

        trimmed = trim(str, (unsigned)size, ans_len);
        free(str);
    } else {
        *ans_len = 0;
    }

    ti_Close(v);

    return trimmed;
}

typedef struct {
    unsigned amount;
    uint8_t **args;
    /*Array for the length of each argv. Have to include because argument may have null chars so we can't use strlen()*/
    unsigned *arg_len;
} arg_list;

uint8_t *parse_one(uint8_t *input, unsigned input_len, unsigned start, unsigned *arg_len) {
    *arg_len = 0;

    for(unsigned i = start; i < input_len; i++) {
        if(input[i] == TI_COMMA)
            break;
        (*arg_len)++;
    }

    uint8_t *ret = NULL;

    if(*arg_len > 0)
        ret = malloc(sizeof(char) * (*arg_len));

    for(unsigned i = 0; i < *arg_len; i++) {
        ret[i] = input[start + i];
    }

    return ret;
}

bool parse_args(uint8_t *input, unsigned input_len, arg_list *args) {
    if(input_len == 0)
        return false;

    /*Calculate amount of arguments*/
    args->amount = 1;
    for(unsigned i = 0; i < input_len; i++) {
        if(input[i] == TI_COMMA)
            args->amount++;
    }

    args->args = malloc(sizeof(char*) * args->amount);
    args->arg_len = malloc(sizeof(unsigned) * args->amount);

	unsigned arg_index = 0;

    /*Parse arguments one by one*/
    for(unsigned i = 0; i < args->amount; i++) {
        args->args[i] = parse_one(input, input_len, arg_index, &args->arg_len[i]);
        arg_index += args->arg_len[i] + 1; /*+ 1 to eliminate the comma*/
    }

    return true;
}

void cleanup_args(arg_list *args) {

    for(unsigned i = 0; i < args->amount; i++) {
        free(args->args[i]);
    }

    free(args->args);
    free(args->arg_len);
}

void write_ans(int val) {
    real_t real;
    real = os_Int24ToReal(val);
    ti_SetVar(TI_REAL_TYPE, ti_Ans, &real);
}

/*Shows message, if it is not NULL, and returns an error code*/
int fail(char *message) {
    char buffer[150] = {0};

    if(message != NULL) {
        os_ClrHome();

        os_SetCursorPos(0, 0);

        if(strlen(message) < 50) {
            sprintf(buffer, "Wrong command syntax. %s. See github.com/james-pre/PinaCAS for usage.", message);
            os_PutStrFull(buffer);   
        }

        while (!os_GetCSC());
    }

    return 1;
}

#define interface_assert(condition, message) if(!(condition)) return fail(message);

/*Valid symbols are Y1 through Y0, Str1 through Str0, and Ans*/
bool tok_valid(uint8_t *symbol, unsigned symbol_len) {
    if(symbol_len != 4)
        /*Check if symbol is Ans*/
        return symbol_len == 3 && symbol[0] == 0x72u && symbol[1] == 0u && symbol[2] == 0u;

    /*Check if symbol is Y1 through Y0*/
    if(symbol[0] == 0x5Eu) {
        return symbol[1] >= 0x10u && symbol[1] <= 0x19u && symbol[2] == 0u && symbol[3] == 0u;
    }
    /*Check if symbol is Str1 through Str0*/
    else if(symbol[0] == 0xAAu) {
        return symbol[1] >= 0x0u && symbol[1] <= 0x9u && symbol[2] == 0u && symbol[3] == 0u;
    }

    return false;
}
/*Add two null terminators to symbol*/
void tok_fix(uint8_t **symbol, unsigned *symbol_len) {
    *symbol = realloc(*symbol, sizeof(char) * (*symbol_len + 2));
    (*symbol)[*symbol_len] = 0;
    (*symbol)[*symbol_len + 1] = 0;
    (*symbol_len) += 2;
}

unsigned parse_list_from_tok(uint8_t *tok, pcas_ast_t **items, unsigned max, pcas_error_t *err) {
    ti_var_t var;
    unsigned count;

    ti_CloseAll();

    var = ti_OpenVar((char*)tok, "r", tok[0] == 0x5Eu ? TI_EQU_TYPE : TI_STRING_TYPE);

    /*If we're opening Ans or a string and the type is not a string type*/
    if(var == 0 || (tok[0] != 0x5Eu && !is_var_string_type(var))) {
        *err = E_GENERIC;
        return 0;
    }

    count = parse_list(ti_GetDataPtr(var), ti_GetSize(var), ti_table, items, max, err);

    ti_Close(var);

    return count;
}

pcas_ast_t *parse_from_tok(uint8_t *tok, pcas_error_t *err) {
    pcas_ast_t *result;
    return parse_list_from_tok(tok, &result, 1, err) == 1 ? result : NULL;
}

void write_to_tok(uint8_t *tok, pcas_ast_t *expression, pcas_error_t *err) {
    unsigned bin_len;
    uint8_t *bin;
    ti_var_t var;

    ti_CloseAll();

    var = ti_OpenVar((char*)tok, "w", tok[0] == 0x5Eu ? TI_EQU_TYPE : TI_STRING_TYPE);

    if(var != 0) {

        /*Write to var*/
        bin = export_to_binary(expression, &bin_len, ti_table, err);
        ti_Write(bin, bin_len, 1, var);

        /*If var is a yvar, enable it*/
        if(var <= 9) {
            /*Thanks Mateo: https://www.cemetech.net/forum/viewtopic.php?t=15947*/
            uint8_t *status;
            status = ti_GetVATPtr(var);
            status--;
            *status |= 1;
        }
        
        ti_Close(var);
    } else {
        *err = E_GENERIC;
        return;
    }

    *err = E_SUCCESS;
}

/*
    Syntax: SIMP,Y1,Y2 or SIMP,Y1,Y2,010101

    None set by default.

    Boolean 1 = Basic identities
    Boolean 2 = Trig identities
    Boolean 3 = Hyperbolic identities
    Boolean 4 = Complex identities
    Boolean 5 = Evaluate trig constants
    Boolean 6 = Evaluate inverse trig constants
*/
int interface_Simplify(int argc, const char *args[], unsigned flags) {
    unsigned short simplify_flags = SIMP_ALL;

    if(argc >= 4) {
        simplify_flags ^= SIMP_ID_ALL;

        const char *options = args[3];

        interface_assert(strlen(options) == 6, "Wrong number of boolean options");

        for(unsigned i = 0; i < 6; i++)
            interface_assert(options[i] == '0' || options[i] == '1', "Boolean option must be 0 or 1");

        if(options[0] == '1') simplify_flags |= SIMP_ID_GENERAL;
        if(options[1] == '1') simplify_flags |= SIMP_ID_TRIG;
        if(options[2] == '1') simplify_flags |= SIMP_ID_HYPERBOLIC;
        if(options[3] == '1') simplify_flags |= SIMP_ID_COMPLEX;
        if(options[4] == '1') simplify_flags |= SIMP_ID_TRIG_CONSTANTS;
        if(options[5] == '1') simplify_flags |= SIMP_ID_TRIG_INV_CONSTANTS;
    }

	pcas_error_t err;
    pcas_ast_t *expression = parse_from_tok((uint8_t*)args[1], &err);
    /*Fail silently because syntax is correct, but input might be bad. Let basic program handle error*/
    interface_assert(err == E_SUCCESS && expression != NULL, NULL);

    simplify(expression, simplify_flags);
    simplify_canonical_form(expression, CANONICAL_ALL);

    write_to_tok((uint8_t*)args[2], expression, &err);

    ast_Cleanup(expression);

    interface_assert(err == E_SUCCESS, NULL);

    return 0;
}

/*
    Syntax: EVAL,Y1,Y2
*/
int interface_Eval(int argc, const char *args[], unsigned flags) {
    pcas_error_t err;
    pcas_ast_t *expression = parse_from_tok((uint8_t*)args[1], &err);
    /*Fail silently because syntax is correct, but input might be bad. Let basic program handle error*/
    interface_assert(err == E_SUCCESS && expression != NULL, NULL);

    simplify(expression, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL);
    eval(expression, EVAL_ALL);
    simplify(expression, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL | SIMP_LIKE_TERMS);
    simplify_canonical_form(expression, CANONICAL_ALL);

    write_to_tok((uint8_t*)args[2], expression, &err);

    ast_Cleanup(expression);

    interface_assert(err == E_SUCCESS, NULL);

    return 0;
}

/*
    Syntax: SUB,Y1,Y2,Str1,Str2
*/
int interface_Substitute(int argc, const char *args[], unsigned flags) {
    pcas_ast_t *expression, *sub_from_expr, *sub_to_expr;
    pcas_error_t err;

    expression = parse_from_tok((uint8_t*)args[1], &err);
    /*Fail silently because syntax is correct, but input might be bad. Let basic program handle error*/
    interface_assert(err == E_SUCCESS && expression != NULL, NULL);
    sub_from_expr = parse_from_tok((uint8_t*)args[3], &err);
    interface_assert(err == E_SUCCESS && sub_from_expr != NULL, NULL);
    sub_to_expr = parse_from_tok((uint8_t*)args[4], &err);
    interface_assert(err == E_SUCCESS && sub_to_expr != NULL, NULL);

    simplify(expression, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL);
    substitute(expression, sub_from_expr, sub_to_expr);
    simplify(expression, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL | SIMP_LIKE_TERMS);
    simplify_canonical_form(expression, CANONICAL_ALL);

    write_to_tok((uint8_t*)args[2], expression, &err);

    ast_Cleanup(expression);
    ast_Cleanup(sub_from_expr);
    ast_Cleanup(sub_to_expr);

    interface_assert(err == E_SUCCESS, NULL);

    return 0;
}

/*
    Syntax: EXP,Y1,Y2 or EXP,Y1,Y2,00

    All set by default.

    Boolean 1 = expand multiplication (A+X)(B+2)
    Boolean 2 = expand powers (1+A)^6
*/
int interface_Expand(int argc, const char *args[], unsigned flags) {
    pcas_ast_t *expression;
    pcas_error_t err;

    unsigned short expand_flags = 0;

    if(argc >= 4) {
        const char *options = args[3];

        interface_assert(strlen(options) == 2, "Wrong number of boolean options");

        for(unsigned i = 0; i < 2; i++)
            interface_assert(options[i] == '0' || options[i] == '1', "Boolean option must be 0 or 1");

        if(options[0] == '1') expand_flags |= EXP_DISTRIB_NUMBERS | EXP_DISTRIB_MULTIPLICATION | EXP_DISTRIB_ADDITION;
        if(options[1] == '1') expand_flags |= EXP_EXPAND_POWERS;
    } else {
        expand_flags = EXP_DISTRIB_NUMBERS | EXP_DISTRIB_MULTIPLICATION | EXP_DISTRIB_ADDITION | EXP_EXPAND_POWERS;
    }

    expression = parse_from_tok((uint8_t*)args[1], &err);
    /*Fail silently because syntax is correct, but input might be bad. Let basic program handle error*/
    interface_assert(err == E_SUCCESS && expression != NULL, NULL);

    simplify(expression, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL);
    expand(expression, expand_flags);
    simplify(expression, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | ((expand_flags & EXP_DISTRIB_MULTIPLICATION) ? SIMP_LIKE_TERMS : 0) | SIMP_EVAL);
    simplify_canonical_form(expression, CANONICAL_ALL ^ CANONICAL_COMBINE_POWERS);

    write_to_tok((uint8_t*)args[2], expression, &err);

    ast_Cleanup(expression);

    interface_assert(err == E_SUCCESS, NULL);

    return 0;
}

/*Valid letters are A through Z and theta*/
bool letter_valid(const char *symbol) {
    return strlen(symbol) == 1 && symbol[0] >= 'A' && symbol[0] <= ('Z' + 1); /*Z + 1 is theta*/
}

pcas_error_t calculus_Run(Calculus kind, pcas_ast_t **items, unsigned count, pcas_ast_t *respect_to, char *summary) {
    pcas_ast_t *e = items[0];
    pcas_error_t err = E_SUCCESS;
    pcas_de_t de;

    if(kind != CALCULUS_DE && count > 1)
        return E_PARSE_BAD_COMMA;

    switch(kind) {
    case CALCULUS_DERIVATIVE:
        simplify(e, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL);
        derivative(e, respect_to, respect_to);
        break;
    case CALCULUS_INTEGRAL:
        simplify(e, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL);
        integral_Indefinite(e, respect_to);
        break;
    case CALCULUS_DE:
        err = de_LoadList(&de, items, count, respect_to);

        if(err == E_SUCCESS) {
            de_Classify(&de);

            if(summary != NULL)
                sprintf(summary, "Order %u, %s.", de.order, de.linear ? "linear" : "nonlinear");

            if(de.linear)
                replace_node(e, ast_MakeBinary(OP_EQUALS, de_StandardForm(&de), ast_Copy(de.g)));

            simplify_canonical_form(e, CANONICAL_ALL);
        }

        de_Cleanup(&de);
        return err;
    }

    simplify(e, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL | SIMP_LIKE_TERMS);
    simplify_canonical_form(e, CANONICAL_ALL);

    return err;
}

pcas_error_t calculus_Verify(pcas_ast_t **items, unsigned count, pcas_ast_t *respect_to, pcas_ast_t *solution, bool *satisfied) {
    pcas_de_t de;
    pcas_error_t err;

    err = de_LoadList(&de, items, count, respect_to);

    if(err == E_SUCCESS)
        err = de_Verify(&de, solution, satisfied);

    de_Cleanup(&de);

    return err;
}

/*Parses the "respect to" argument, which is a letter or theta*/
static pcas_ast_t *parse_respect_to(const char *respect_to, pcas_error_t *err) {
    const char *text = respect_to[0] == 'Z' + 1 ? "theta" : respect_to;
    return parse((const uint8_t*)text, strlen(text), str_table, err);
}

static void cleanup_items(pcas_ast_t **items, unsigned count) {
    while(count > 0)
        ast_Cleanup(items[--count]);
}

/*
    Syntax: DERIV,Y1,Y2,X or INTEG,Y1,Y2,X or DE,Y1,Y2,X

    Takes the derivative or integral of, or classifies the differential equation in, the input with respect to the 4th argument
*/
int interface_Calculus(int argc, const char *args[], unsigned flags) {
    pcas_ast_t *items[MAX_ITEMS], *respect_to;
    pcas_error_t err;

    unsigned count = parse_list_from_tok((uint8_t*)args[1], items, MAX_ITEMS, &err);
    /*Fail silently because syntax is correct, but input might be bad. Let basic program handle error*/
    interface_assert(err == E_SUCCESS && count > 0 && items[0] != NULL, NULL);

    respect_to = parse_respect_to(args[3], &err);
    interface_assert(err == E_SUCCESS && respect_to != NULL, NULL);

    err = calculus_Run((Calculus)flags, items, count, respect_to, NULL);

    if(err == E_SUCCESS)
        write_to_tok((uint8_t*)args[2], items[0], &err);

    cleanup_items(items, count);
    ast_Cleanup(respect_to);

    interface_assert(err == E_SUCCESS, NULL);

    return 0;
}

/*
    Syntax: VERIFY,Y1,Y2,X

    Checks whether the solution in the 3rd argument satisfies the differential equation and initial conditions in the 2nd. Writes 1 to Ans if it does and 0 if not.
*/
int interface_Verify(int argc, const char *args[], unsigned flags) {
    pcas_ast_t *items[MAX_ITEMS], *solution, *respect_to;
    pcas_error_t err;
    bool satisfied = false;

    unsigned count = parse_list_from_tok((uint8_t*)args[1], items, MAX_ITEMS, &err);
    interface_assert(err == E_SUCCESS && count > 0 && items[0] != NULL, NULL);

    solution = parse_from_tok((uint8_t*)args[2], &err);
    respect_to = parse_respect_to(args[3], &err);

    if(solution != NULL && respect_to != NULL)
        err = calculus_Verify(items, count, respect_to, solution, &satisfied);

    cleanup_items(items, count);
    ast_Cleanup(solution);
    ast_Cleanup(respect_to);

    return err == E_SUCCESS && satisfied ? 0 : 1;
}

#define MAX_PARAMS 4

typedef enum {
    /* This parameter is a variable, like Y1 through Y0, Str1 through Str0, or Ans */
    P_VAR,
    /* This parameter is a variable, like A through Z or theta, like the variable to differentiate with respect to */
    P_LETTER,
} ParamFlags;

typedef struct {
    const char *label;
    ParamFlags flags;
} interface_param;

typedef struct interface_op {
	const char* name;
	/*Called with the arguments after they are checked, starting with the name, and the flags of the operation. Returns 0 on success.*/
	int (*func)(int argc, const char *args[], unsigned flags);
	/*The required parameters*/
	interface_param params[MAX_PARAMS];
	unsigned flags;
} interface_op;

static const struct interface_op ops[] = {
	{"SIMP", interface_Simplify, {{"input"}, {"output"}}},
	{"EVAL", interface_Eval, {{"input"}, {"output"}}},
	{"SUB", interface_Substitute, {{"input"}, {"output"}, {"substitute from"}, {"substitute to"}}},
	{"EXP", interface_Expand, {{"input"}, {"output"}}},
	{"DERIV", interface_Calculus, {{"input"}, {"output"}, {"respect to", P_LETTER}}, CALCULUS_DERIVATIVE},
	{"INTEG", interface_Calculus, {{"input"}, {"output"}, {"respect to", P_LETTER}}, CALCULUS_INTEGRAL},
	{"DE", interface_Calculus, {{"input"}, {"output"}, {"respect to", P_LETTER}}, CALCULUS_DE},
	{"VERIFY", interface_Verify, {{"equation"}, {"solution"}, {"respect to", P_LETTER}}}
};

/*Returns the operation named by the first argument, or NULL if there is none*/
static const interface_op *find_op(arg_list *args) {
    for(unsigned i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        if(strlen(ops[i].name) == args->arg_len[0] && !strncmp(ops[i].name, (char*)args->args[0], args->arg_len[0]))
            return &ops[i];
    }

    return NULL;
}

/*Checks the arguments for op and runs it. Returns 0 on success.*/
static int run_op(const interface_op *op, arg_list *args) {
    char buffer[50];
    unsigned required = 0;

    while(required < MAX_PARAMS && op->params[required].label != NULL)
        required++;

    interface_assert(args->amount > required, "Not enough arguments");

    for(unsigned i = 0; i < args->amount; i++)
        tok_fix(&args->args[i], &args->arg_len[i]);

    for(unsigned i = 0; i < required; i++) {
        const interface_param *param = &op->params[i];
        uint8_t *arg = args->args[i + 1];
        bool valid = false;

        switch(param->flags) {
        case P_VAR: valid = tok_valid(arg, args->arg_len[i + 1]); break;
        case P_LETTER:   valid = letter_valid((char*)arg);              break;
        }

        sprintf(buffer, "Not a valid %s %s", param->label, param->flags == P_LETTER ? "letter" : "variable");
        interface_assert(valid, buffer);
    }

    return op->func(args->amount, (const char**)args->args, op->flags);
}

void interface_Run(void) {
    uint8_t *ans;
    unsigned ans_len;

    arg_list args;

    ans = read_ans(&ans_len);

    if(parse_args(ans, ans_len, &args)) {

        const interface_op *op = find_op(&args);

        if(op != NULL)
            write_ans(run_op(op, &args) == 0);

        id_UnloadAll();

        cleanup_args(&args);
    }

    free(ans);
}


/*Checks if Ans is trying to call a function of PCAS*/
bool interface_Valid(void) {
    uint8_t *ans;
    unsigned ans_len;

    arg_list args;

    bool valid = false;

    ans = read_ans(&ans_len);

    if(parse_args(ans, ans_len, &args)) {
        valid = find_op(&args) != NULL;

        cleanup_args(&args);
    }

    free(ans);

    return valid;
}

#else
typedef int make_iso_compilers_happy;
#endif