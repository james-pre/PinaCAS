#pragma once

#ifndef COMPILE_PC

#include <stdint.h>

struct Glyph {
	uint8_t width;
	uint8_t rows[8];
};

extern const Glyph glyph_pi, glyph_theta, glyph_dot, glyph_infinity;
extern const Glyph glyph_root, glyph_square, glyph_cube, glyph_inverse, glyph_negative;

/*Draws g with its top left corner at x, y, leaving out pixels outside of left <= x < right and top <= y < bottom*/
void glyph_draw(int x, int y, const Glyph *g, int left, int top, int right, int bottom);

#endif
