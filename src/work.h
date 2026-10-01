#pragma once

#include "ast.h"

typedef enum {
    /*after is a snapshot of the whole expression being worked on*/
    STEP_STATE,
    /*before is a derivative node, after is its value*/
    STEP_DERIVATIVE,
    /*before is an integral node, after is its value*/
    STEP_INTEGRAL,
    /*before = after, such as a substitution*/
    STEP_EQUATION,
    /*Only text*/
    STEP_TEXT
} StepType;

typedef struct _pcas_Step {
    StepType type;
    /*Static string naming the rule or method used. May be NULL.*/
    const char *text;
    pcas_ast_t *before, *after;
    struct _pcas_Step *next;
} pcas_step_t;

typedef struct {
    pcas_step_t *first, *last;
    /*True when the last step is a state that continues a run of states*/
    bool extend;
} pcas_work_t;

/*Records steps into w until work_Stop() is called.*/
void work_Start(pcas_work_t *w);
void work_Stop(void);
void work_Cleanup(pcas_work_t *w);

bool work_Recording(void);

/*Suppresses recording during computations that are not part of the shown work.*/
void work_Pause(void);
void work_Resume(void);

/*Brackets a CAS operation on e. The outermost operation records the state of e on entry and exit.*/
void work_Enter(pcas_ast_t *e);
void work_Leave(pcas_ast_t *e);

/*Records a step. Copies before and after.*/
void work_Step(StepType type, const char *text, pcas_ast_t *before, pcas_ast_t *after);
void work_Text(const char *text);
