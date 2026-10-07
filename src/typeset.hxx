#pragma once

#include "ast.hxx"

namespace ts {

/*Characters in box text that renderers draw as symbols*/
constexpr char Pi = '\x01';
constexpr char Theta = '\x02';
constexpr char Dot = '\x03';
constexpr char Infinity = '\x04';

struct Box {
	enum class Type : unsigned char {
		Text,
		/*Children side by side on a common baseline*/
		Row,
		/*Children are the numerator and denominator*/
		Fraction,
		/*Children are the base and the script*/
		Superscript,
		Subscript,
		Root,
		/*Child surrounded by left and right delimiters*/
		Delimited,
		/*Child preceded by an integral sign*/
		Integral,
		/*Summation sign with the first child below it and the second above it*/
		Summation
	};

	Type type;
	int width = 0, ascent = 0, descent = 0;

	char *text = nullptr;
	char left = 0, right = 0;

	Box *first = nullptr, *next = nullptr;

	explicit Box(Type type);
	Box(const Box &) = delete;
	Box &operator=(const Box &) = delete;
	/*Deletes the children but not the siblings*/
	~Box();

	/*Takes ownership of child and returns this box*/
	Box *append(Box *child);
};

/*Sizes are in the renderer's units, with y increasing downward*/
struct Metrics {
	int (*text_width)(const char *text);
	/*Extent of a line of text above and below its baseline*/
	int ascent, descent;
	/*Distance from the baseline up to the bottom of a fraction bar*/
	int axis;
	/*Thickness of fraction bars and root overlines*/
	int rule;
	/*Space between a fraction bar or overline and what it is next to*/
	int gap;
	/*Extra width on each side of a fraction bar*/
	int fraction_pad;
	/*How far the baseline of an exponent is below the top of its base*/
	int script_drop;
	/*How far the baseline of a subscript is below the baseline of its base*/
	int subscript_drop;
	/*Widths of drawn symbols of a given height, including their spacing*/
	int (*delimiter_width)(int height);
	int (*radical_width)(int height);
	int (*integral_width)(int height);
	/*Width of a summation sign, including its spacing*/
	int summation_width;
};

/*Coordinates are the top left of the area to draw*/
struct Renderer {
	void (*text)(int x, int y, const char *text);
	void (*bar)(int x, int y, int width);
	void (*overline)(int x, int y, int width);
	void (*delimiter)(int x, int y, int height, char kind);
	void (*radical)(int x, int y, int height);
	void (*integral)(int x, int y, int height);
	void (*summation)(int x, int y, int height);
};

/*Copies text*/
Box *text(const char *text);
Box *row();

/*Lays out e in math notation*/
Box *fromAst(const ast *e);

/*Computes the size of b and its descendants*/
void measure(Box *b, const Metrics &m);
/*Draws a measured box with its baseline at y*/
void draw(const Box *b, int x, int baseline, const Metrics &m, const Renderer &r);

/*True if a and b, and their siblings, are laid out the same way*/
bool equal(const Box *a, const Box *b);

} // namespace ts
