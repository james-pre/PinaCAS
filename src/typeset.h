#pragma once

#include "ast.h"

/*Characters in box text that renderers draw as symbols*/
#define TS_PI '\x01'
#define TS_THETA '\x02'
#define TS_DOT '\x03'
#define TS_INFINITY '\x04'

typedef enum : unsigned char {
	BOX_TEXT,
	/*Children side by side on a common baseline*/
	BOX_ROW,
	/*Children are the numerator and denominator*/
	BOX_FRACTION,
	/*Children are the base and the script*/
	BOX_SUPERSCRIPT,
	BOX_SUBSCRIPT,
	BOX_ROOT,
	/*Child surrounded by left and right delimiters*/
	BOX_DELIMITED,
	/*Child preceded by an integral sign*/
	BOX_INTEGRAL,
	/*Summation sign with the first child below it and the second above it*/
	BOX_SUMMATION
} BoxType;

typedef struct _ts_Box {
	BoxType type;
	int width, ascent, descent;

	char *text;
	char left, right;

	struct _ts_Box *first, *next;
} ts_box_t;

/*Sizes are in the renderer's units, with y increasing downward*/
typedef struct {
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
} ts_metrics_t;

/*Coordinates are the top left of the area to draw*/
typedef struct {
	void (*text)(int x, int y, const char *text);
	void (*bar)(int x, int y, int width);
	void (*overline)(int x, int y, int width);
	void (*delimiter)(int x, int y, int height, char kind);
	void (*radical)(int x, int y, int height);
	void (*integral)(int x, int y, int height);
	void (*summation)(int x, int y, int height);
} ts_renderer_t;

/*Copies text*/
ts_box_t *ts_Text(const char *text);
ts_box_t *ts_Row(void);
/*Takes ownership of child and returns parent*/
ts_box_t *ts_Append(ts_box_t *parent, ts_box_t *child);

/*Lays out e in math notation*/
ts_box_t *ts_FromAst(const pcas_ast_t *e);

/*Computes the size of b and its descendants*/
void ts_Measure(ts_box_t *b, const ts_metrics_t *m);
/*Draws a measured box with its baseline at y*/
void ts_Draw(ts_box_t *b, int x, int baseline, const ts_metrics_t *m, const ts_renderer_t *r);

void ts_Cleanup(ts_box_t *b);

/*True if a and b are laid out the same way*/
bool ts_Equal(ts_box_t *a, ts_box_t *b);
