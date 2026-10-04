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

	while (step != NULL) {
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

static pcas_ast_t *display_copy(const pcas_ast_t *e) {
	pcas_ast_t *copy;

	if (e == NULL)
		return NULL;

	copy = ast_Copy(e);

	paused++;
	simplify(copy, SIMP_COMMUTATIVE);
	simplify_canonical_form(copy, CANONICAL_ALL & ~CANONICAL_RATIONALIZE);
	paused--;

	return copy;
}

static void append(pcas_step_t *step) {
	step->next = NULL;

	if (current->last == NULL)
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

/*True if a and b are drawn the same way*/
static bool same_display(const pcas_ast_t *a, const pcas_ast_t *b) {
	ts_box_t *box_a, *box_b;
	bool same;

	if (ast_Compare(a, b))
		return true;

	box_a = ts_FromAst(a);
	box_b = ts_FromAst(b);
	same = ts_Equal(box_a, box_b);

	ts_Cleanup(box_a);
	ts_Cleanup(box_b);

	return same;
}

static void record_state(const pcas_ast_t *e) {
	pcas_step_t *last = current->last;
	pcas_ast_t *snapshot = display_copy(e);

	if (last != NULL && last->type == STEP_STATE && same_display(last->after, snapshot)) {
		ast_Cleanup(snapshot);
		return;
	}

	if (last != NULL && last->type == STEP_STATE && current->extend) {
		ast_Cleanup(last->after);
		last->after = snapshot;
		return;
	}

	current->extend = last != NULL && last->type == STEP_STATE;
	append(make_step(STEP_STATE, NULL, NULL, snapshot));
}

void work_Enter(const pcas_ast_t *e) {
	if (depth++ == 0 && work_Recording())
		record_state(e);
}

void work_Leave(const pcas_ast_t *e) {
	if (--depth == 0 && work_Recording())
		record_state(e);
}

void work_Step(StepType type, const char *text, const pcas_ast_t *before, const pcas_ast_t *after) {
	if (!work_Recording())
		return;

	current->extend = false;
	append(make_step(type, text, display_copy(before), display_copy(after)));
}

void work_Text(const char *text) {
	work_Step(STEP_TEXT, text, NULL, NULL);
}

ts_box_t *work_Layout(pcas_step_t *step, bool continued) {
	if (step->type == STEP_TEXT)
		return NULL;

	ts_box_t *row = ts_Row();

	switch (step->type) {
		case STEP_STATE:
			if (continued)
				ts_Append(row, ts_Text("= "));
			break;
		default:
			ts_Append(row, ts_FromAst(step->before));
			ts_Append(row, ts_Text(" = "));
			break;
	}

	return ts_Append(row, ts_FromAst(step->after));
}
