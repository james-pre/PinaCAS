#pragma once

#include "ast.hxx"
#include "typeset.hxx"

namespace work {

struct Step {
	enum class Type : unsigned char {
		/*after is a snapshot of the whole expression being worked on*/
		State,
		/*before is a derivative node, after is its value*/
		Derivative,
		/*before is an integral node, after is its value*/
		Integral,
		/*before = after, such as a substitution*/
		Equation,
		/*Only text*/
		Text
	};

	Type type;
	/*Static string naming the rule or method used. May be nullptr.*/
	const char *text;
	/*Owned by the step*/
	ast *before, *after;
	Step *next = nullptr;

	Step(Type type, const char *text, ast *before, ast *after);
	Step(const Step &) = delete;
	Step &operator=(const Step &) = delete;
	~Step();
};

struct Record {
	Step *first = nullptr, *last = nullptr;
	/*True when the last step is a state that continues a run of states*/
	bool extend = false;

	Record() = default;
	Record(const Record &) = delete;
	Record &operator=(const Record &) = delete;
	~Record();

	void clear();
};

/*Records steps into record until stop() is called.*/
void start(Record &record);
void stop();

bool recording();

/*Suppresses recording during computations that are not part of the shown work.*/
void pause();
void resume();

/*Brackets a CAS operation on e. The outermost operation records the state of e on entry and exit.*/
void enter(const ast &e);
void leave(const ast &e);

/*Records a step. Copies before and after.*/
void step(Step::Type type, const char *text, const ast *before, const ast *after);
void text(const char *text);

/*Lays out the math of a step, or returns nullptr if it has none. A continued state follows another state, so it starts with =.*/
ts::Box *layout(const Step *step, bool continued);

} // namespace work
