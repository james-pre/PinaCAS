#ifndef COMPILE_PC

#include "viewer.hxx"

#include <graphx.h>
#include <keypadc.h>
#include <tice.h>

#include "glyph.hxx"
#include "gui.hxx"

#define TITLE_HEIGHT 16
#define VIEW_LEFT 4
#define VIEW_TOP (TITLE_HEIGHT + 4)
#define VIEW_RIGHT (LCD_WIDTH - 4)
#define VIEW_BOTTOM (LCD_HEIGHT - 2)
#define LABEL_HEIGHT 10
#define ENTRY_SPACING 8
#define SCROLL_Y 16
#define SCROLL_X 48

static const glyph_t *special_glyph(char c) {
	switch (c) {
		case ts::Pi: return &glyph_pi;
		case ts::Theta: return &glyph_theta;
		case ts::Dot: return &glyph_dot;
		case ts::Infinity: return &glyph_infinity;
		default: return nullptr;
	}
}

static int text_width(const char *text) {
	int width = 0;

	for (; *text != '\0'; text++) {
		const glyph_t *g = special_glyph(*text);
		width += g != nullptr ? g->width : (int)gfx_GetCharWidth(*text);
	}

	return width;
}

static int delimiter_width(int height) {
	(void)height;
	return 5;
}

static int radical_width(int height) {
	(void)height;
	return 7;
}

static int integral_width(int height) {
	(void)height;
	return 9;
}

static const ts::Metrics metrics =
	{text_width, 7, 1, 3, 1, 1, 2, 3, 3, delimiter_width, radical_width, integral_width, 10};

static void draw_text(int x, int y, const char *text) {
	for (; *text != '\0'; text++) {
		const glyph_t *g = special_glyph(*text);

		if (g != nullptr) {
			glyph_draw(x, y, g, VIEW_LEFT, VIEW_TOP, VIEW_RIGHT, VIEW_BOTTOM);
			x += g->width;
		} else {
			gfx_SetTextXY(x, y);
			gfx_PrintChar(*text);
			x += gfx_GetCharWidth(*text);
		}
	}
}

static void draw_bar(int x, int y, int width) {
	gfx_HorizLine(x, y, width);
}

static void draw_delimiter(int x, int y, int height, char kind) {
	switch (kind) {
		case '(':
			gfx_VertLine(x + 1, y + 2, height - 4);
			gfx_Line(x + 2, y + 1, x + 3, y);
			gfx_Line(x + 2, y + height - 2, x + 3, y + height - 1);
			break;
		case ')':
			gfx_VertLine(x + 3, y + 2, height - 4);
			gfx_Line(x + 2, y + 1, x + 1, y);
			gfx_Line(x + 2, y + height - 2, x + 1, y + height - 1);
			break;
		default: gfx_VertLine(x + 2, y, height); break;
	}
}

static void draw_radical(int x, int y, int height) {
	gfx_Line(x, y + height - 4, x + 2, y + height - 1);
	gfx_Line(x + 2, y + height - 1, x + 6, y);
}

static void draw_integral(int x, int y, int height) {
	gfx_VertLine(x + 3, y + 2, height - 4);
	gfx_Line(x + 4, y + 1, x + 5, y);
	gfx_Line(x + 2, y + height - 2, x + 1, y + height - 1);
}

static void draw_summation(int x, int y, int height) {
	gfx_HorizLine(x + 1, y, 7);
	gfx_Line(x + 1, y, x + 4, y + height / 2);
	gfx_Line(x + 4, y + height / 2, x + 1, y + height - 1);
	gfx_HorizLine(x + 1, y + height - 1, 7);
}

static const ts::Renderer renderer =
	{draw_text, draw_bar, draw_bar, draw_delimiter, draw_radical, draw_integral, draw_summation};

void viewer::measure(ts::Box *b) {
	ts::measure(b, metrics);
}

void viewer::draw(ts::Box *b, int x, int baseline) {
	ts::draw(b, x, baseline, metrics, renderer);
}

typedef struct {
	const char *label;
	ts::Box *box;
	/*Top of the entry in content coordinates*/
	int y;
	int height;
} entry_t;

static void draw_title(const char *title) {
	gfx_SetClipRegion(0, 0, LCD_WIDTH, LCD_HEIGHT);
	gfx_SetColor(COLOR_BACKGROUND);
	gfx_FillRectangle(0, 0, LCD_WIDTH, TITLE_HEIGHT);

	gfx_SetTextFGColor(COLOR_TEXT);
	gfx_PrintStringXY(title, 6, 4);
	gfx_PrintStringXY("[clear] back", LCD_WIDTH - 6 - gfx_GetStringWidth("[clear] back"), 4);

	gfx_SetColor(COLOR_BLUE);
	gfx_HorizLine(0, TITLE_HEIGHT - 1, LCD_WIDTH);
}

static void draw_entries(entry_t *entries, unsigned count, int scroll_x, int scroll_y) {
	gfx_SetClipRegion(0, 0, LCD_WIDTH, LCD_HEIGHT);
	gfx_SetColor(COLOR_BACKGROUND);
	gfx_FillRectangle(0, TITLE_HEIGHT, LCD_WIDTH, LCD_HEIGHT - TITLE_HEIGHT);
	gfx_SetClipRegion(VIEW_LEFT, VIEW_TOP, VIEW_RIGHT, VIEW_BOTTOM);

	for (unsigned i = 0; i < count; i++) {
		const entry_t *entry = &entries[i];
		int top = VIEW_TOP + entry->y - scroll_y;

		if (top + entry->height < VIEW_TOP || top >= VIEW_BOTTOM)
			continue;

		if (entry->label != nullptr) {
			gfx_SetTextFGColor(COLOR_PURPLE);
			gfx_PrintStringXY(entry->label, VIEW_LEFT - scroll_x, top);
			top += LABEL_HEIGHT;
		}

		if (entry->box != nullptr) {
			gfx_SetTextFGColor(COLOR_TEXT);
			gfx_SetColor(COLOR_TEXT);
			ts::draw(entry->box, VIEW_LEFT - scroll_x, top + entry->box->ascent, metrics, renderer);
		}
	}
}

void viewer::show(work::Record *w, const char *title) {
	unsigned count = 0;

	for (const work::Step *step = w->first; step != nullptr; step = step->next)
		count++;

	entry_t *entries = static_cast<entry_t *>(malloc(sizeof(entry_t) * (count > 0 ? count : 1)));
	entry_t *entry = entries;
	const work::Step *previous = nullptr;
	int content_width = 0, content_height = 0;

	for (work::Step *step = w->first; step != nullptr; previous = step, step = step->next, entry++) {
		entry->label = step->text;
		entry->box = work::layout(step, previous != nullptr && previous->type == work::Step::Type::State);
		entry->y = content_height;
		entry->height = entry->label != nullptr ? LABEL_HEIGHT : 0;

		if (entry->box != nullptr) {
			ts::measure(entry->box, metrics);
			entry->height += entry->box->ascent + entry->box->descent;
			if (entry->box->width > content_width)
				content_width = entry->box->width;
		}

		content_height += entry->height + ENTRY_SPACING;
	}

	int max_x = content_width - (VIEW_RIGHT - VIEW_LEFT);
	int max_y = content_height - (VIEW_BOTTOM - VIEW_TOP);
	if (max_x < 0)
		max_x = 0;
	if (max_y < 0)
		max_y = 0;

	gfx_SetTextConfig(gfx_text_clip);
	gfx_SetTextTransparentColor(COLOR_TRANSPARENT);
	gfx_SetTextBGColor(COLOR_TRANSPARENT);
	gfx_SetDrawBuffer();

	int scroll_x = 0, scroll_y = 0;
	bool redraw = true;

	while (true) {
		if (redraw) {
			draw_entries(entries, count, scroll_x, scroll_y);
			draw_title(title);
			gfx_SwapDraw();
			redraw = false;
		}

		const uint8_t key = os_GetCSC();

		if (key == sk_Clear || key == sk_Enter || key == sk_Del)
			break;

		switch (key) {
			case sk_Down: scroll_y += SCROLL_Y; break;
			case sk_Up: scroll_y -= SCROLL_Y; break;
			case sk_Right: scroll_x += SCROLL_X; break;
			case sk_Left: scroll_x -= SCROLL_X; break;
			default: continue;
		}

		if (scroll_y > max_y)
			scroll_y = max_y;
		if (scroll_y < 0)
			scroll_y = 0;
		if (scroll_x > max_x)
			scroll_x = max_x;
		if (scroll_x < 0)
			scroll_x = 0;

		redraw = true;
	}

	gfx_SetDrawScreen();
	gfx_SetClipRegion(0, 0, LCD_WIDTH, LCD_HEIGHT);
	gfx_SetTextConfig(0);

	for (unsigned i = 0; i < count; i++)
		delete entries[i].box;
	free(entries);
}

#else
typedef int make_iso_compilers_happy;
#endif
