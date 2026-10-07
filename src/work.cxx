#include "work.hxx"

#include "cas/cas.hxx"

namespace work {

static Record *current = nullptr;
static unsigned depth = 0;
static unsigned paused = 0;

Step::Step(Type type, const char *text, ast *before, ast *after)
	: type(type), text(text), before(before), after(after) {
}

Step::~Step() {
	delete before;
	delete after;
}

Record::~Record() {
	clear();
}

void Record::clear() {
	Step *step = first;

	while (step != nullptr) {
		Step *next = step->next;
		delete step;
		step = next;
	}

	first = last = nullptr;
}

void start(Record &record) {
	record.clear();
	record.extend = false;
	current = &record;
	depth = 0;
	paused = 0;
}

void stop() {
	current = nullptr;
}

bool recording() {
	return current != nullptr && paused == 0;
}

void pause() {
	paused++;
}

void resume() {
	paused--;
}

static ast *display_copy(const ast *e) {
	if (e == nullptr)
		return nullptr;

	ast *copy = e->copy();

	paused++;
	simplify(*copy, Simp::Commutative);
	simplify_canonical_form(*copy, Canonical::All & ~Canonical::Rationalize);
	paused--;

	return copy;
}

static void append(Step *step) {
	if (current->last == nullptr)
		current->first = step;
	else
		current->last->next = step;

	current->last = step;
}

/*True if a and b are drawn the same way*/
static bool same_display(const ast &a, const ast &b) {
	if (a.compare(b))
		return true;

	const ts::Box *box_a = ts::fromAst(a);
	const ts::Box *box_b = ts::fromAst(b);
	const bool same = ts::equal(box_a, box_b);

	delete box_a;
	delete box_b;

	return same;
}

static void record_state(const ast &e) {
	Step *last = current->last;
	ast *snapshot = display_copy(&e);

	if (last != nullptr && last->type == Step::Type::State && same_display(*last->after, *snapshot)) {
		delete snapshot;
		return;
	}

	if (last != nullptr && last->type == Step::Type::State && current->extend) {
		delete last->after;
		last->after = snapshot;
		return;
	}

	current->extend = last != nullptr && last->type == Step::Type::State;
	append(new Step(Step::Type::State, nullptr, nullptr, snapshot));
}

void enter(const ast &e) {
	if (depth++ == 0 && recording())
		record_state(e);
}

void leave(const ast &e) {
	if (--depth == 0 && recording())
		record_state(e);
}

void step(Step::Type type, const char *text, const ast *before, const ast *after) {
	if (!recording())
		return;

	current->extend = false;
	append(new Step(type, text, display_copy(before), display_copy(after)));
}

void text(const char *text) {
	step(Step::Type::Text, text, nullptr, nullptr);
}

ts::Box *layout(const Step *step, bool continued) {
	if (step->type == Step::Type::Text)
		return nullptr;

	ts::Box *row = ts::row();

	switch (step->type) {
		case Step::Type::State:
			if (continued)
				row->append(ts::text("= "));
			break;
		default:
			row->append(ts::fromAst(*step->before));
			row->append(ts::text(" = "));
			break;
	}

	return row->append(ts::fromAst(*step->after));
}

} // namespace work
