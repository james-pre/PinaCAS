#pragma once

#include "imath/imrat.h"
#include "error.h"

#include <stdlib.h>
#include <stdbool.h>

typedef enum { NODE_NUMBER, NODE_SYMBOL, NODE_OPERATOR } NodeType;

#define is_op_commutative(op) ((op) == OP_ADD || (op) == OP_MULT)

#define is_op_operator(op) ((op) >= OP_ADD && (op) <= OP_LOG)
#define is_op_function(op) ((op) >= OP_INT && (op) <= OP_TANH_INV)

#define is_op_nary(op) ((op) >= OP_ADD && (op) <= OP_LOG)
#define is_op_unary(op) ((op) >= OP_FACTORIAL && (op) <= OP_TANH_INV)

typedef enum {
	/*nary*/
	OP_ADD,
	OP_MULT,
	OP_DIV,

	OP_POW,
	OP_ROOT,
	OP_LOG,

	/*1st child = f(var), 2nd child = var, 3rd child = value for var*/
	OP_DERIV,
	/*1st child = f(var), 2nd child = var*/
	OP_INTEGRAL,

	/*1st child = left side, 2nd child = right side*/
	OP_EQUALS,
	/*Derivative of the unknown function that is its child*/
	OP_PRIME,
	/*1st child = function, 2nd child = point it is evaluated at*/
	OP_AT,
	/*Shown in work only: 1st child = term, 2nd child = index, 3rd child = lower limit of a sum to infinity*/
	OP_SUM,
	/*Shown in work only: 1st child = name, 2nd child = subscript*/
	OP_SUBSCRIPT,

	/*Unary*/
	OP_FACTORIAL,

	OP_INT,
	OP_ABS,

	OP_SIN,
	OP_SIN_INV,
	OP_COS,
	OP_COS_INV,
	OP_TAN,
	OP_TAN_INV,

	OP_SINH,
	OP_SINH_INV,
	OP_COSH,
	OP_COSH_INV,
	OP_TANH,
	OP_TANH_INV,

	AMOUNT_OPS
} OperatorType;

#define AMOUNT_SYMBOLS 30

typedef enum {
	SYM_A = 'A',
	SYM_B,
	SYM_C,
	SYM_D,
	SYM_E,
	SYM_F,
	SYM_G,
	SYM_H,
	SYM_I,
	SYM_J,
	SYM_K,
	SYM_L,
	SYM_M,
	SYM_N,
	SYM_O,
	SYM_P,
	SYM_Q,
	SYM_R,
	SYM_S,
	SYM_T,
	SYM_U,
	SYM_V,
	SYM_W,
	SYM_X,
	SYM_Y,
	SYM_Z,

	SYM_IMAG,

	SYM_PI,
	SYM_EULER,
	SYM_THETA,

	SYM_INVALID
} Symbol;

/*Shortcuts for NODE_OPERATOR*/
#define optype(e) (e)->op.operator.type
#define isoptype(e, op) ((e)->type == NODE_OPERATOR && optype(e) == (op))
#define opbase(e) (e)->op.operator.base

#define is_ast_int(e, val) ((e)->type == NODE_NUMBER && num_IsInt((e)->op.num, val))

typedef struct _pcas_Node {
	NodeType type;
	/*For the linked list implementation*/
	struct _pcas_Node *next;

	union {
		/*NODE_NUMBER*/
		mp_rat num;

		/*NODE_SYMBOL*/
		Symbol symbol;

		/*NODE_OPERATOR*/
		struct {
			OperatorType type;

			/*The base node for the linked list*/
			struct _pcas_Node *base;

		} operator;
	} op;

} pcas_ast_t;

/*Wrapper functions for shorthand calling in functions*/
/*Expects null terminated string*/
mp_rat num_FromString(const char *str);
mp_rat num_FromInt(mp_small num);
mp_rat num_FromFraction(mp_small num, mp_small den);
mp_rat num_Copy(mp_rat other);
/*Expects num to be reduced*/
bool num_IsInt(mp_rat num, mp_small value);
char *num_ToString(mp_rat num, mp_size precision);
void num_Cleanup(mp_rat num);

pcas_ast_t *ast_MakeNumber(mp_rat num);
pcas_ast_t *ast_MakeSymbol(char symbol);

pcas_ast_t *ast_MakeOperator(OperatorType type);
pcas_ast_t *ast_MakeUnary(OperatorType type, pcas_ast_t *operand);
pcas_ast_t *ast_MakeBinary(OperatorType type, pcas_ast_t *left, pcas_ast_t *right);

pcas_ast_t *ast_Copy(const pcas_ast_t *e);
bool ast_Compare(const pcas_ast_t *a, const pcas_ast_t *b);

void ast_Cleanup(pcas_ast_t *e);

/*Functions dealing with the children of operator asts*/
pcas_error_t ast_ChildAppend(pcas_ast_t *parent, pcas_ast_t *child);
pcas_error_t ast_ChildInsert(pcas_ast_t *parent, pcas_ast_t *child, size_t index);

pcas_ast_t *ast_ChildGet(const pcas_ast_t *parent, size_t index);
pcas_ast_t *ast_ChildGetLast(const pcas_ast_t *parent);

/*returns -1 (unsigned) if not found*/
size_t ast_ChildIndexOf(const pcas_ast_t *parent, const pcas_ast_t *child);

/*
Returns the removed node, null if none
Currently, removeIndex() is much faster, so please
use that if possible.
*/
pcas_ast_t *ast_ChildRemove(pcas_ast_t *parent, pcas_ast_t *child);
pcas_ast_t *ast_ChildRemoveIndex(pcas_ast_t *parent, size_t index);

size_t ast_ChildLength(const pcas_ast_t *parent);
