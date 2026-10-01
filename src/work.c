#include "work.h"

#include "cas/cas.h"

static pcas_work_t *current = NULL;
static unsigned depth = 0;
static unsigned paused = 0;

void work_Start(pcas_work_t *w) {
    w->first = w->last = NULL;
    w->extend = false;
    current = w;
    depth = 0;
    paused = 0;
}

void work_Stop(void) {
    current = NULL;
}

void work_Cleanup(pcas_work_t *w) {
    pcas_step_t *step = w->first;

    while(step != NULL) {
        pcas_step_t *next = step->next;
        ast_Cleanup(step->before);
        ast_Cleanup(step->after);
        free(step);
        step = next;
    }

    w->first = w->last = NULL;
}

bool work_Recording(void) {
    return current != NULL && paused == 0;
}

void work_Pause(void) {
    paused++;
}

void work_Resume(void) {
    paused--;
}

static pcas_ast_t *display_copy(pcas_ast_t *e) {
    pcas_ast_t *copy;

    if(e == NULL)
        return NULL;

    copy = ast_Copy(e);

    paused++;
    simplify_canonical_form(copy, CANONICAL_ALL & ~CANONICAL_RATIONALIZE);
    paused--;

    return copy;
}

static void append(pcas_step_t *step) {
    step->next = NULL;

    if(current->last == NULL)
        current->first = step;
    else
        current->last->next = step;

    current->last = step;
}

static pcas_step_t *make_step(StepType type, const char *text, pcas_ast_t *before, pcas_ast_t *after) {
    pcas_step_t *step = malloc(sizeof(pcas_step_t));
    step->type = type;
    step->text = text;
    step->before = before;
    step->after = after;
    return step;
}

static void record_state(pcas_ast_t *e) {
    pcas_step_t *last = current->last;
    pcas_ast_t *snapshot = display_copy(e);

    if(last != NULL && last->after != NULL && ast_Compare(last->after, snapshot)) {
        ast_Cleanup(snapshot);
        return;
    }

    if(last != NULL && last->type == STEP_STATE && current->extend) {
        ast_Cleanup(last->after);
        last->after = snapshot;
        return;
    }

    current->extend = last != NULL && last->type == STEP_STATE;
    append(make_step(STEP_STATE, NULL, NULL, snapshot));
}

void work_Enter(pcas_ast_t *e) {
    if(depth++ == 0 && work_Recording())
        record_state(e);
}

void work_Leave(pcas_ast_t *e) {
    if(--depth == 0 && work_Recording())
        record_state(e);
}

void work_Step(StepType type, const char *text, pcas_ast_t *before, pcas_ast_t *after) {
    if(!work_Recording())
        return;

    current->extend = false;
    append(make_step(type, text, display_copy(before), display_copy(after)));
}

void work_Text(const char *text) {
    work_Step(STEP_TEXT, text, NULL, NULL);
}
