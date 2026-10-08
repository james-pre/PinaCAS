#ifdef COMPILE_PC

#include "render.hxx"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t *cells;
static int canvas_width, canvas_height;

static void put(int x, int y, uint32_t c) {
	if (x >= 0 && x < canvas_width && y >= 0 && y < canvas_height)
		cells[y * canvas_width + x] = c;
}

static uint32_t symbol(char c) {
	switch (c) {
		case ts::Pi: return 0x03C0;
		case ts::Theta: return 0x03B8;
		case ts::Dot: return 0x00B7;
		case ts::Infinity: return 0x221E;
		default: return static_cast<unsigned char>(c);
	}
}

static unsigned text_width(const char *text) {
	return strlen(text);
}

static int one(int height) {
	(void)height;
	return 1;
}

static int integral_width(int height) {
	(void)height;
	return 2;
}

static const ts::Metrics metrics = {text_width, 1, 0, 0, 1, 0, 0, 0, 1, one, one, integral_width, 2};

static void draw_text(int x, int y, const char *text) {
	for (; *text != '\0'; text++)
		put(x++, y, symbol(*text));
}

static void draw_bar(int x, int y, int width) {
	while (width-- > 0)
		put(x++, y, 0x2500);
}

static void draw_overline(int x, int y, int width) {
	while (width-- > 0)
		put(x++, y, '_');
}

/*Draws a column of a tall symbol, or single when it is one row*/
static void column(int x, int y, int height, uint32_t single, uint32_t top, uint32_t middle, uint32_t bottom) {
	if (height == 1) {
		put(x, y, single);
		return;
	}

	put(x, y, top);
	for (int i = 1; i < height - 1; i++)
		put(x, y + i, middle);
	put(x, y + height - 1, bottom);
}

static void draw_delimiter(int x, int y, int height, char kind) {
	switch (kind) {
		case '(': column(x, y, height, '(', 0x239B, 0x239C, 0x239D); break;
		case ')': column(x, y, height, ')', 0x239E, 0x239F, 0x23A0); break;
		default: column(x, y, height, '|', 0x2502, 0x2502, 0x2502); break;
	}
}

static void draw_radical(int x, int y, int height) {
	column(x, y, height, 0x221A, ' ', 0x2502, 0x221A);
}

static void draw_integral(int x, int y, int height) {
	column(x, y, height, 0x222B, 0x2320, 0x23AE, 0x2321);
}

static void draw_summation(int x, int y, int height) {
	(void)height;
	put(x, y, 0x03A3);
}

static const ts::Renderer renderer =
	{draw_text, draw_bar, draw_overline, draw_delimiter, draw_radical, draw_integral, draw_summation};

static void print_utf8(uint32_t c) {
	if (c < 0x80) {
		putchar(static_cast<int>(c));
	} else if (c < 0x800) {
		putchar(static_cast<int>((0xC0 | (c >> 6))));
		putchar(static_cast<int>((0x80 | (c & 0x3F))));
	} else {
		putchar(static_cast<int>((0xE0 | (c >> 12))));
		putchar(static_cast<int>((0x80 | ((c >> 6) & 0x3F))));
		putchar(static_cast<int>((0x80 | (c & 0x3F))));
	}
}

void render::print(ts::Box *b) {
	ts::measure(b, metrics);

	canvas_width = b->width;
	canvas_height = b->ascent + b->descent;
	cells = static_cast<uint32_t *>(malloc(sizeof(uint32_t) * static_cast<size_t>(canvas_width * canvas_height)));

	for (int i = 0; i < canvas_width * canvas_height; i++)
		cells[i] = ' ';

	ts::draw(b, 0, b->ascent, metrics, renderer);

	for (int y = 0; y < canvas_height; y++) {
		int end = canvas_width;

		while (end > 0 && cells[y * canvas_width + end - 1] == ' ')
			end--;

		for (int x = 0; x < end; x++)
			print_utf8(cells[y * canvas_width + x]);

		putchar('\n');
	}

	free(cells);
}

#endif
