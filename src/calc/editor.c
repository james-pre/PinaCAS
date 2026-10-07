#ifndef COMPILE_PC

#include "editor.h"

#include <fileioc.h>
#include <graphx.h>
#include <tice.h>

#include <stdcountof.h>
#include <string.h>

#include "../cas/cas.h"
#include "../parser.h"
#include "../typeset.h"

#include "calculus.h"
#include "glyph.h"
#include "gui.h"
#include "vars.h"
#include "viewer.h"

#define MAX_LENGTH 512
#define BAR_HEIGHT 16
#define MARGIN 6
#define LINE_HEIGHT 12
#define VISIBLE_LINES 6
#define TOKENS_TOP (BAR_HEIGHT + 4)
#define PREVIEW_TOP (TOKENS_TOP + LINE_HEIGHT * VISIBLE_LINES + 4)
#define PREVIEW_BOTTOM (LCD_HEIGHT - BAR_HEIGHT - 2)
#define PREVIEW_SPACING 6
#define MENU_ROWS 8
#define MENU_WIDTH 120
/*Deeper expressions are not previewed, since simplifying and laying them out can overflow the stack*/
#define MAX_PREVIEW_DEPTH 24

typedef enum : unsigned char { MODE_NORMAL, MODE_SECOND, MODE_ALPHA, MODE_ALPHA_LOCK } key_mode_t;

/*Tokens are big endian, with 0 for none*/
static const struct key_tokens {
	uint8_t key;
	uint16_t normal, second, alpha;
} key_tokens[] = {
	{sk_0, '0', 0, 0},           {sk_1, '1', 0, 'Y'},          {sk_2, '2', 0, 'Z'},        {sk_3, '3', 0, 0x5B},
	{sk_4, '4', 0, 'T'},         {sk_5, '5', 0, 'U'},          {sk_6, '6', 0, 'V'},        {sk_7, '7', 0, 'O'},
	{sk_8, '8', 0, 'P'},         {sk_9, '9', 0, 'Q'},          {sk_DecPnt, 0x3A, 0x2C, 0}, {sk_Chs, 0xB0, 0, 0},
	{sk_Add, 0x70, 0, 0},        {sk_Sub, 0x71, 0, 'W'},       {sk_Mul, 0x82, 0, 'R'},     {sk_Div, 0x83, 0xBB31, 'M'},
	{sk_Power, 0xF0, 0xAC, 'H'}, {sk_Square, 0x0D, 0xBC, 'I'}, {sk_Recip, 0x0C, 0, 'D'},   {sk_LParen, 0x10, 0, 'K'},
	{sk_RParen, 0x11, 0, 'L'},   {sk_Comma, 0x2B, 0x3B, 'J'},  {sk_Sin, 0xC2, 0xC3, 'E'},  {sk_Cos, 0xC4, 0xC5, 'F'},
	{sk_Tan, 0xC6, 0xC7, 'G'},   {sk_Ln, 0xBE, 0xBF, 'S'},     {sk_Log, 0xC0, 0xC1, 'N'},  {sk_GraphVar, 'X', 0, 0},
	{sk_Store, 0, 0, 'X'},       {sk_Math, 0, 0x6A, 'A'},      {sk_Apps, 0, 0xAE, 'B'},    {sk_Prgm, 0, 0, 'C'}
};

/*Tokens without a key of their own, chosen from the menu*/
static const uint16_t menu_tokens[] =
	{0x6A, 0xAE, 0xB2, 0x2D, 0xB1, 0x25, 0x24, 0xEF34, 0x0F, 0xBD, 0xF1, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0x5B};

static uint8_t data[MAX_LENGTH];
static unsigned length, cursor;
static key_mode_t mode;

static ts_box_t *previews[MAX_ITEMS];
static unsigned preview_count;
/*Why there is no preview, or NULL*/
static const char *preview_message;

/*Returns the display text of the token at token, and sets size to its length in bytes*/
static const char *token_text(const uint8_t *token, unsigned *size) {
	void *read = (void *)token;
	uint8_t token_length;
	const char *text = ti_GetTokenString(&read, &token_length, NULL);

	*size = token_length;
	return text;
}

static const glyph_t *os_glyph(uint8_t c) {
	switch (c) {
		case 0x0E: return &glyph_cube;
		case 0x10: return &glyph_root;
		case 0x11: return &glyph_inverse;
		case 0x12: return &glyph_square;
		case 0x1A: return &glyph_negative;
		case 0x5B: return &glyph_theta;
		case 0xC4: return &glyph_pi;
		default: return NULL;
	}
}

/*Returns how to print a character of the OS character set that has no glyph*/
static const char *os_char(uint8_t c) {
	static char ascii[2];

	switch (c) {
		case 0x1B: return "E";
		case 0x1D: return "10";
		case 0xD7: return "i";
		case 0xDB: return "e";
		default: ascii[0] = c >= 0x20 && c < 0x7F ? (char)c : '?'; return ascii;
	}
}

/*Prints text in the OS character set at x, y if draw is true. Returns its width.*/
static int print_os_text(const char *text, int x, int y, bool draw) {
	int start = x;

	if (draw)
		gfx_SetColor(COLOR_TEXT);

	for (; *text != '\0'; text++) {
		const glyph_t *g = os_glyph(*text);

		if (g != NULL) {
			if (draw)
				glyph_draw(x, y, g, 0, 0, LCD_WIDTH, LCD_HEIGHT);
			x += g->width;
		} else {
			const char *ascii = os_char(*text);

			if (draw)
				gfx_PrintStringXY(ascii, x, y);
			x += gfx_GetStringWidth(ascii);
		}
	}

	return x - start;
}

static unsigned token_size(unsigned offset) {
	unsigned size;
	token_text(data + offset, &size);
	return size;
}

static unsigned previous_token(unsigned offset) {
	unsigned current = 0, previous = 0;

	while (current < offset) {
		previous = current;
		current += token_size(current);
	}

	return previous;
}

static void insert(uint16_t token) {
	unsigned size = token > 0xFF ? 2 : 1;

	if (token == 0 || length + size > MAX_LENGTH)
		return;

	memmove(data + cursor + size, data + cursor, length - cursor);
	if (size == 2)
		data[cursor++] = token >> 8;
	data[cursor++] = token & 0xFF;
	length += size;
}

static void delete(unsigned offset) {
	unsigned size = token_size(offset);

	memmove(data + offset, data + offset + size, length - offset - size);
	length -= size;
}

static bool is_function(const pcas_ast_t *e) {
	while (isoptype(e, OP_PRIME))
		e = opbase(e);

	return e->type == NODE_SYMBOL;
}

/*Shows an initial condition such as Y(0)=1, which is parsed as Y*0=1, as the function at a point*/
static void show_condition(pcas_ast_t *e) {
	if (!isoptype(e, OP_EQUALS))
		return;

	pcas_ast_t *left = opbase(e);
	if (isoptype(left, OP_MULT) && ast_ChildLength(left) == 2 && is_function(opbase(left)))
		optype(left) = OP_AT;
}

static bool too_deep(const pcas_ast_t *e, unsigned depth) {
	if (depth > MAX_PREVIEW_DEPTH)
		return true;

	if (e->type == NODE_OPERATOR)
		for (const pcas_ast_t *child = opbase(e); child != NULL; child = child->next)
			if (too_deep(child, depth + 1))
				return true;

	return false;
}

static void clear_preview(void) {
	while (preview_count > 0)
		ts_Cleanup(previews[--preview_count]);
}

static void update_preview(void) {
	clear_preview();
	preview_message = NULL;

	if (length == 0)
		return;

	pcas_ast_t *items[MAX_ITEMS];
	pcas_error_t err;
	const unsigned count = parse_list(data, length, ti_table, items, MAX_ITEMS, &err);

	if (count == 0 && err != E_SUCCESS)
		preview_message = error_text[err];

	for (unsigned i = 0; i < count; i++)
		if (items[i] != NULL && too_deep(items[i], 0))
			preview_message = "Too deeply nested to preview";

	for (unsigned i = 0; i < count; i++) {
		if (items[i] == NULL)
			continue;

		if (preview_message != NULL) {
			ast_Cleanup(items[i]);
			continue;
		}

		if (i > 0)
			show_condition(items[i]);

		simplify(items[i], SIMP_COMMUTATIVE);
		simplify_canonical_form(items[i], CANONICAL_ALL);

		previews[preview_count] = ts_FromAst(items[i]);
		viewer_Measure(previews[preview_count++]);

		ast_Cleanup(items[i]);
	}
}

/*Lays the tokens out in lines, drawing the lines from first_line on if draw is true. Returns the line of the cursor.*/
static int layout_tokens(int first_line, bool draw) {
	unsigned offset = 0;
	int x = MARGIN, line = 0, cursor_line = 0;

	while (true) {
		const char *text = NULL;
		unsigned size = 0;
		int width = 0;

		if (offset < length) {
			text = token_text(data + offset, &size);
			width = print_os_text(text, 0, 0, false);

			if (x + width > LCD_WIDTH - MARGIN && x > MARGIN) {
				x = MARGIN;
				line++;
			}
		}

		const int y = TOKENS_TOP + (line - first_line) * LINE_HEIGHT;
		const bool visible = draw && line >= first_line && line < first_line + VISIBLE_LINES;

		if (offset == cursor) {
			cursor_line = line;

			if (visible) {
				gfx_SetColor(mode == MODE_SECOND ? COLOR_BLUE : COLOR_PURPLE);
				gfx_FillRectangle(x - 1, y, 2, LINE_HEIGHT - 2);
			}
		}

		if (offset >= length)
			return cursor_line;

		if (visible)
			print_os_text(text, x, y + 2, true);

		x += width;
		offset += size;
	}
}

static void draw_rule(int y) {
	gfx_SetColor(COLOR_BLUE);
	gfx_HorizLine(0, y, LCD_WIDTH);
}

static void print_right(const char *text, int y) {
	gfx_PrintStringXY(text, LCD_WIDTH - MARGIN - gfx_GetStringWidth(text), y);
}

static void draw(const char *name) {
	static const char *const mode_text[] = {"", "2nd", "alpha", "ALPHA"};
	gfx_SetClipRegion(0, 0, LCD_WIDTH, LCD_HEIGHT);
	gfx_FillScreen(COLOR_BACKGROUND);
	gfx_SetTextFGColor(COLOR_TEXT);

	gfx_PrintStringXY("Edit ", MARGIN, 4);
	gfx_PrintString(name);
	print_right("[enter] save", 4);
	draw_rule(BAR_HEIGHT - 1);

	const int first_line = layout_tokens(0, false) - (VISIBLE_LINES - 1);
	layout_tokens(first_line > 0 ? first_line : 0, true);

	draw_rule(PREVIEW_TOP - 3);

	int y = PREVIEW_TOP;

	if (preview_message != NULL) {
		gfx_SetTextFGColor(COLOR_PURPLE);
		gfx_PrintStringXY(preview_message, MARGIN, y + 2);
	}

	gfx_SetClipRegion(MARGIN, PREVIEW_TOP, LCD_WIDTH - MARGIN, PREVIEW_BOTTOM);
	gfx_SetTextFGColor(COLOR_TEXT);
	gfx_SetColor(COLOR_TEXT);

	for (unsigned i = 0; i < preview_count; i++) {
		viewer_Draw(previews[i], MARGIN, y + 2 + previews[i]->ascent);
		y += previews[i]->ascent + previews[i]->descent + PREVIEW_SPACING;
	}

	gfx_SetClipRegion(0, 0, LCD_WIDTH, LCD_HEIGHT);
	draw_rule(LCD_HEIGHT - BAR_HEIGHT);
	gfx_SetTextFGColor(COLOR_PURPLE);
	gfx_PrintStringXY(mode_text[mode], MARGIN, LCD_HEIGHT - BAR_HEIGHT + 5);
	gfx_SetTextFGColor(COLOR_TEXT);
	print_right("[math] more  [mode] cancel", LCD_HEIGHT - BAR_HEIGHT + 5);
}

static uint8_t wait_key(void) {
	uint8_t key;

	while (!(key = os_GetCSC()))
		;

	return key;
}

/*Lets the user choose one of menu_tokens over the editor. Returns it, or 0 if they cancel.*/
static uint16_t choose_menu_token(const char *name) {
	const int height = MENU_ROWS * LINE_HEIGHT + 8;
	const int left = (LCD_WIDTH - MENU_WIDTH) / 2, top = (LCD_HEIGHT - height) / 2;
	unsigned selected = 0, first = 0;

	while (true) {
		draw(name);

		gfx_SetColor(COLOR_BACKGROUND);
		gfx_FillRectangle(left, top, MENU_WIDTH, height);
		gfx_SetColor(COLOR_BLUE);
		gfx_Rectangle(left, top, MENU_WIDTH, height);

		for (unsigned i = first; i < first + MENU_ROWS && i < countof(menu_tokens); i++) {
			const uint8_t token[2] = {menu_tokens[i] >> 8, menu_tokens[i] & 0xFF};
			unsigned size;
			const int y = top + 4 + (int)(i - first) * LINE_HEIGHT + 2;

			gfx_SetTextFGColor(COLOR_PURPLE);
			if (i == selected)
				gfx_PrintStringXY(">", left + 6, y);
			gfx_SetTextFGColor(COLOR_TEXT);
			print_os_text(token_text(token[0] != 0 ? token : token + 1, &size), left + 18, y, true);
		}

		gfx_SwapDraw();

		switch (wait_key()) {
			case sk_Up: selected = selected > 0 ? selected - 1 : countof(menu_tokens) - 1; break;
			case sk_Down: selected = (selected + 1) % countof(menu_tokens); break;
			case sk_Enter: return menu_tokens[selected];
			case sk_Clear:
			case sk_Math:
			case sk_Mode: return 0;
			default: break;
		}

		if (selected < first)
			first = selected;
		else if (selected >= first + MENU_ROWS)
			first = selected - MENU_ROWS + 1;
	}
}

static uint16_t key_token(uint8_t key, bool second, bool alpha) {
	for (unsigned i = 0; i < countof(key_tokens); i++)
		if (key_tokens[i].key == key)
			return alpha ? key_tokens[i].alpha : second ? key_tokens[i].second : key_tokens[i].normal;

	return 0;
}

bool editor_Run(const char *name, const char *tok) {
	bool saved = false, done = false, changed = true;

	if (!read_tokens_from_tok(tok, data, sizeof(data), &length))
		return false;

	cursor = length;
	mode = MODE_NORMAL;

	gfx_SetTextConfig(gfx_text_clip);
	gfx_SetTextTransparentColor(COLOR_TRANSPARENT);
	gfx_SetTextBGColor(COLOR_TRANSPARENT);
	gfx_SetDrawBuffer();

	while (!done) {
		if (changed) {
			update_preview();
			changed = false;
		}

		draw(name);
		gfx_SwapDraw();

		const uint8_t key = wait_key();

		if (key == sk_2nd) {
			mode = mode == MODE_SECOND ? MODE_NORMAL : MODE_SECOND;
			continue;
		}

		if (key == sk_Alpha) {
			mode = mode == MODE_SECOND ? MODE_ALPHA_LOCK : mode == MODE_NORMAL ? MODE_ALPHA : MODE_NORMAL;
			continue;
		}

		const bool second = mode == MODE_SECOND;
		const bool alpha = mode == MODE_ALPHA || mode == MODE_ALPHA_LOCK;
		if (mode != MODE_ALPHA_LOCK)
			mode = MODE_NORMAL;

		switch (key) {
			case sk_Enter: {
				pcas_error_t err;
				write_tokens_to_tok(tok, data, length, &err);
				saved = err == E_SUCCESS;
				done = true;
				break;
			}
			case sk_Mode: done = true; break;
			case sk_Clear:
				done = length == 0;
				length = cursor = 0;
				changed = true;
				break;
			case sk_Left: cursor = second ? 0 : previous_token(cursor); break;
			case sk_Right:
				if (second)
					cursor = length;
				else if (cursor < length)
					cursor += token_size(cursor);
				break;
			case sk_Del:
				if (cursor == length && length > 0)
					cursor = previous_token(cursor);
				if (cursor < length) {
					delete(cursor);
					changed = true;
				}
				break;
			default:
				if (key == sk_Math && !second && !alpha)
					insert(choose_menu_token(name));
				else
					insert(key_token(key, second, alpha));
				changed = true;
				break;
		}
	}

	clear_preview();

	gfx_SetDrawScreen();
	gfx_SetClipRegion(0, 0, LCD_WIDTH, LCD_HEIGHT);
	gfx_SetTextConfig(gfx_text_noclip);

	return saved;
}

#else
typedef int make_iso_compilers_happy;
#endif
