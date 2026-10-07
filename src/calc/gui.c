#ifndef COMPILE_PC

#include "gui.h"

#include <tice.h>
#include <fileioc.h>

#include <graphx.h>
#include <keypadc.h>

#include <string.h>
#include <stdcountof.h>

#include "../parser.h"
#include "../cas/cas.h"
#include "../cas/identities.h"
#include "../cas/derivative.h"
#include "../version.h"

#include "calculus.h"
#include "editor.h"
#include "vars.h"
#include "viewer.h"
#include "../work.h"

#define LINE_HEIGHT 10
#define MENU_ROW 16
#define CHECKBOX_SIZE 8
#define CHECKBOX_ROW 12
#define SELECT_ROW 18
#define BOX_HEIGHT 16
#define BUTTON_HEIGHT 20
#define VARIABLE_WIDTH 50
#define GAP 6
#define MARKER_OFFSET 10

typedef struct {
	int x, y, w, h;
} rect_t;

static const rect_t header_area = {40, 10, 240, 40};
static const rect_t menus_area = {14, 70, 96, 136};
static const rect_t options_area = {112, 70, 195, 138};
static const rect_t console_area = {LCD_WIDTH / 6, LCD_HEIGHT / 6, LCD_WIDTH * 2 / 3, LCD_HEIGHT * 2 / 3};

static rect_t inset(rect_t r, int x, int y) {
	return (rect_t){r.x + x, r.y + y, r.w - 2 * x, r.h - 2 * y};
}

static const struct variable {
	const char *name;
	const char *token;
} variables[] = {{"Y1", OS_VAR_Y1},     {"Y2", OS_VAR_Y2},     {"Y3", OS_VAR_Y3},     {"Y4", OS_VAR_Y4},
				 {"Y5", OS_VAR_Y5},     {"Y6", OS_VAR_Y6},     {"Y7", OS_VAR_Y7},     {"Y8", OS_VAR_Y8},
				 {"Y9", OS_VAR_Y9},     {"Y0", OS_VAR_Y0},     {"Str1", OS_VAR_STR1}, {"Str2", OS_VAR_STR2},
				 {"Str3", OS_VAR_STR3}, {"Str4", OS_VAR_STR4}, {"Str5", OS_VAR_STR5}, {"Str6", OS_VAR_STR6},
				 {"Str7", OS_VAR_STR7}, {"Str8", OS_VAR_STR8}, {"Str9", OS_VAR_STR9}, {"Str0", OS_VAR_STR0},
				 {"Ans", OS_VAR_ANS}};

static bool is_ans(unsigned variable) {
	return strcmp(variables[variable].name, "Ans") == 0;
}

typedef enum {
	ELEMENT_END,
	ELEMENT_TEXT,
	ELEMENT_CHECKBOX,
	ELEMENT_VARIABLE,
	ELEMENT_CHARACTER,
	ELEMENT_BUTTON
} element_type;

typedef struct {
	element_type type;
	const char *text;

	union {
		bool *checked;
		/*Index in variables*/
		unsigned *variable;
		struct {
			char *value;
			/*Returns the character for a key, or 0 if the key does not set one*/
			char (*from_key)(uint8_t key);
		} character;
		void (*action)(void);
	};
} element_t;

typedef struct {
	const char *label;
	/*Ends with an ELEMENT_END*/
	const element_t *content;
} menu_t;

static char letter_key(uint8_t key);
static char digit_key(uint8_t key);

#define END {ELEMENT_END, NULL}
#define TEXT(text) {ELEMENT_TEXT, text}
#define CHECKBOX(text, state) {ELEMENT_CHECKBOX, text, .checked = (state)}
#define VARIABLE(text, state) {ELEMENT_VARIABLE, text, .variable = (state)}
#define LETTER(text, state)                                                                                            \
	{                                                                                                                  \
		ELEMENT_CHARACTER, text, .character = {(state), letter_key }                                                   \
	}
#define DIGIT(text, state)                                                                                             \
	{                                                                                                                  \
		ELEMENT_CHARACTER, text, .character = {(state), digit_key }                                                    \
	}
#define BUTTON(text, function) {ELEMENT_BUTTON, text, .action = (function)}
#define CONTENT(...) ((const element_t[]){__VA_ARGS__, END})

typedef struct {
	char respect_to;
	bool show_work;
	bool verify;
	unsigned solution;
	bool series;
	char terms;
} calculus_options_t;

static unsigned input = 0, output = 1;

static struct {
	bool general, trig, hyperbolic, complex, trig_constants, trig_inv_constants;
} simplify_options = {true, true, true, true, true, true};

static struct {
	bool constants, substitute;
	unsigned from, to;
} evaluate_options = {true, false, 10, 11};

static struct {
	bool multiplication, powers;
} expand_options = {true, true};

static calculus_options_t derivative_options = {.respect_to = 'X', .show_work = true};
static calculus_options_t integral_options = {.respect_to = 'X', .show_work = true};
static calculus_options_t de_options =
	{.respect_to = 'X', .show_work = true, .solution = 2, .terms = '0' + DE_DEFAULT_TERMS};

static void execute_simplify(void);
static void execute_evaluate(void);
static void execute_expand(void);
static void execute_derivative(void);
static void execute_integral(void);
static void execute_de(void);
static void close_console(void);

static const element_t header[] = {VARIABLE("Input", &input), VARIABLE("Output", &output), END};

static const menu_t menus[] = {
	{"Simplify",
	 CONTENT(
		 CHECKBOX("Basic identities", &simplify_options.general),
		 CHECKBOX("Trig identities", &simplify_options.trig),
		 CHECKBOX("Hyperbolic identities", &simplify_options.hyperbolic),
		 CHECKBOX("Complex identities", &simplify_options.complex),
		 CHECKBOX("Evaluate trig", &simplify_options.trig_constants),
		 CHECKBOX("Evaluate inverse trig", &simplify_options.trig_inv_constants),
		 BUTTON("Simplify", execute_simplify)
	 )},
	{"Evaluate",
	 CONTENT(
		 CHECKBOX("Evaluate constants", &evaluate_options.constants),
		 CHECKBOX("Substitute expression:", &evaluate_options.substitute),
		 VARIABLE("From:", &evaluate_options.from),
		 VARIABLE("To:", &evaluate_options.to),
		 BUTTON("Evaluate", execute_evaluate)
	 )},
	{"Expand",
	 CONTENT(
		 CHECKBOX("Expand multiplication", &expand_options.multiplication),
		 CHECKBOX("Expand powers", &expand_options.powers),
		 BUTTON("Expand", execute_expand)
	 )},
	{"Derivative",
	 CONTENT(
		 LETTER("Respect to:", &derivative_options.respect_to),
		 CHECKBOX("Show work", &derivative_options.show_work),
		 BUTTON("Differentiate", execute_derivative)
	 )},
	{"Integral",
	 CONTENT(
		 LETTER("Respect to:", &integral_options.respect_to),
		 CHECKBOX("Show work", &integral_options.show_work),
		 BUTTON("Integrate", execute_integral)
	 )},
	{"Solve DE",
	 CONTENT(
		 LETTER("Respect to:", &de_options.respect_to),
		 CHECKBOX("Show work", &de_options.show_work),
		 CHECKBOX("Verify solution", &de_options.verify),
		 VARIABLE("Solution in:", &de_options.solution),
		 CHECKBOX("Power series", &de_options.series),
		 DIGIT("Series terms:", &de_options.terms),
		 BUTTON("Solve", execute_de)
	 )},
	{"About",
	 CONTENT(
		 TEXT("PinaCAS v" PCAS_VERSION " " PCAS_BUILD_DATE),
		 TEXT("github.com/james-pre/PinaCAS"),
		 TEXT(""),
		 TEXT("Credits:"),
		 TEXT("James Prevett"),
		 TEXT("Nathan Farlow (PineappleCAS)"),
		 TEXT("Michael Fromberger (imath)")
	 )}
};

static const element_t console_content[] = {BUTTON("Close", close_console), END};

typedef enum { FOCUS_HEADER, FOCUS_MENUS, FOCUS_OPTIONS } focus_t;

static focus_t focus = FOCUS_MENUS;
static unsigned menu = 0;
/*Index of the focused element in the header or the options*/
static int item = 0;

static bool console_drawn = false;
static bool console_done = false;
static int console_index = 0;

/*the key lookup table for os_GetCSC()*/
static const char alpha_table[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5C, 0x00, 0x57,
								   0x52, 0x4D, 0x48, 0x00, 0x00, 0x00, 0x40, 0x56, 0x51, 0x4C, 0x47, 0x00,
								   0x00, 0x00, 0x5A, 0x55, 0x50, 0x4B, 0x46, 0x43, 0x00, 0x00, 0x59, 0x54,
								   0x4F, 0x4A, 0x45, 0x42, 0x58, 0x00, 0x58, 0x53, 0x4E, 0x49, 0x44, 0x41,
								   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static char letter_key(uint8_t key) {
	return key < countof(alpha_table) ? alpha_table[key] : 0;
}

static char digit_key(uint8_t key) {
	switch (key) {
		case sk_1: return '1';
		case sk_2: return '2';
		case sk_3: return '3';
		case sk_4: return '4';
		case sk_5: return '5';
		case sk_6: return '6';
		case sk_7: return '7';
		case sk_8: return '8';
		case sk_9: return '9';
		default: return 0;
	}
}

static void draw_string_centered(const char *text, int x, int y) {
	gfx_SetTextBGColor(COLOR_TRANSPARENT);
	gfx_SetTextFGColor(COLOR_TEXT);
	gfx_PrintStringXY(text, x - (int)gfx_GetStringWidth(text) / 2, y);
}

static void draw_background(void) {
	gfx_SetMonospaceFont(0);

	gfx_FillScreen(COLOR_BACKGROUND);

	gfx_SetTextTransparentColor(COLOR_TRANSPARENT);
	gfx_SetTextBGColor(COLOR_TRANSPARENT);
	gfx_SetTextFGColor(COLOR_TEXT);

	/*Outer border*/
	gfx_SetColor(COLOR_BLUE);
	gfx_Rectangle(1, 1, LCD_WIDTH - 2, LCD_HEIGHT - 2);
	gfx_HorizLine(1, LCD_HEIGHT - 21, LCD_WIDTH - 2);

	if (gfx_GetStringWidth("PinaCAS v" PCAS_VERSION " by James Prevett") < LCD_WIDTH - 8)
		draw_string_centered("PinaCAS v" PCAS_VERSION " by James Prevett", LCD_WIDTH / 2, LCD_HEIGHT - 14);
	else
		draw_string_centered("PinaCAS v" PCAS_VERSION, LCD_WIDTH / 2, LCD_HEIGHT - 14);

	/*Outer selection border*/
	gfx_Rectangle(10, 50, LCD_WIDTH - 20, 160);

	/*Function rectangle*/
	gfx_Rectangle(10 + 2, 50 + 2, 100, 160 - 4);
	draw_string_centered("Function", menus_area.x + menus_area.w / 2, 50 + 10);
	draw_string_centered("Options", options_area.x + options_area.w / 2, 50 + 10);
}

static void clear(rect_t r) {
	gfx_SetColor(COLOR_BACKGROUND);
	gfx_FillRectangle(r.x, r.y, r.w, r.h);
}

static void draw_marker(int x, int y, uint8_t color) {
	gfx_SetTextFGColor(color);
	gfx_PrintStringXY(">", x - MARKER_OFFSET, y - TEXT_HEIGHT / 2);
	gfx_SetTextFGColor(COLOR_TEXT);
}

static void draw_box(rect_t r, const char *text) {
	gfx_SetColor(COLOR_PURPLE);
	gfx_Rectangle(r.x, r.y, r.w, r.h);
	draw_string_centered(text, r.x + r.w / 2, r.y + r.h / 2 - TEXT_HEIGHT / 2);
}

static int button_width(const element_t *e) {
	return (int)gfx_GetStringWidth(e->text) + 16;
}

static int value_width(const element_t *e) {
	return e->type == ELEMENT_VARIABLE ? VARIABLE_WIDTH : BOX_HEIGHT;
}

/*Draws the value of a variable or character element in r*/
static void draw_value(const element_t *e, rect_t r) {
	if (e->type == ELEMENT_VARIABLE) {
		draw_box(r, variables[*e->variable].name);
	} else {
		const char character[2] = {*e->character.value, '\0'};
		draw_box(r, character);
	}
}

/*Prints text wrapped to width, breaking at the last space that fits or else within a word. Returns the number of lines.*/
static unsigned wrap_text(const char *text, int x, int y, int width) {
	char line[48];
	unsigned lines = 0, length = 0;

	line[0] = '\0';

	for (; *text != '\0'; text++) {
		line[length++] = *text;
		line[length] = '\0';

		if (length > 1 && (length == sizeof(line) - 1 || (int)gfx_GetStringWidth(line) > width)) {
			const char *space = strrchr(line, ' ');
			const unsigned keep = space != NULL ? (unsigned)(space - line) : length - 1;
			const unsigned skip = space != NULL ? keep + 1 : keep;
			const char next = line[keep];

			line[keep] = '\0';
			gfx_PrintStringXY(line, x, y + LINE_HEIGHT * lines++);
			line[keep] = next;

			length -= skip;
			memmove(line, line + skip, length + 1);
		}
	}

	gfx_PrintStringXY(line, x, y + LINE_HEIGHT * lines);

	return lines + 1;
}

/* Lays content out top to bottom in area with buttons in a row along the bottom, marking the element at focused */
static void draw_form(const element_t *content, rect_t area, int focused) {
	int column = 0, buttons = -GAP;

	for (const element_t *e = content; e->type != ELEMENT_END; e++) {
		if (e->type == ELEMENT_BUTTON) {
			buttons += button_width(e) + GAP;
		} else if (e->type == ELEMENT_VARIABLE || e->type == ELEMENT_CHARACTER) {
			const int width = gfx_GetStringWidth(e->text);
			if (width > column)
				column = width;
		}
	}

	column += area.x + GAP;
	int button_x = area.x + (area.w - buttons) / 2, y = area.y;

	gfx_SetTextBGColor(COLOR_TRANSPARENT);
	gfx_SetTextFGColor(COLOR_TEXT);

	for (const element_t *e = content; e->type != ELEMENT_END; e++) {
		rect_t r;
		int marker_x;

		switch (e->type) {
			case ELEMENT_TEXT: y += LINE_HEIGHT * wrap_text(e->text, area.x, y, area.w); continue;

			case ELEMENT_CHECKBOX:
				r = (rect_t){area.x, y + (CHECKBOX_ROW - CHECKBOX_SIZE) / 2, CHECKBOX_SIZE, CHECKBOX_SIZE};
				gfx_SetColor(COLOR_PURPLE);
				gfx_Rectangle(r.x, r.y, r.w, r.h);
				if (*e->checked)
					gfx_FillRectangle(r.x + 2, r.y + 2, r.w - 4, r.h - 4);
				gfx_PrintStringXY(e->text, r.x + r.w + 4, r.y + r.h / 2 - TEXT_HEIGHT / 2);
				marker_x = r.x;
				y += CHECKBOX_ROW;
				break;

			case ELEMENT_VARIABLE:
			case ELEMENT_CHARACTER:
				r = (rect_t){column, y + (SELECT_ROW - BOX_HEIGHT) / 2, value_width(e), BOX_HEIGHT};
				gfx_PrintStringXY(e->text, area.x, r.y + r.h / 2 - TEXT_HEIGHT / 2);
				draw_value(e, r);
				marker_x = area.x;
				y += SELECT_ROW;
				break;

			case ELEMENT_BUTTON:
				r = (rect_t){button_x, area.y + area.h - BUTTON_HEIGHT, button_width(e), BUTTON_HEIGHT};
				draw_box(r, e->text);
				marker_x = r.x;
				button_x += r.w + GAP;
				break;

			default: continue;
		}

		if (e - content == focused)
			draw_marker(marker_x, r.y + r.h / 2, COLOR_PURPLE);
	}
}

/* Lays content out in equal columns across area, each value under its label, marking the element at focused */
static void draw_columns(const element_t *content, rect_t area, int focused) {
	int count = 0;

	for (const element_t *e = content; e->type != ELEMENT_END; e++)
		count++;

	for (const element_t *e = content; e->type != ELEMENT_END; e++) {
		const int center = area.x + area.w * (2 * (e - content) + 1) / (2 * count);
		const rect_t r = {center - value_width(e) / 2, area.y + LINE_HEIGHT + GAP, value_width(e), BUTTON_HEIGHT};

		draw_string_centered(e->text, center, area.y + GAP / 2);
		draw_value(e, r);

		if (e - content == focused)
			draw_marker(r.x, r.y + r.h / 2, COLOR_PURPLE);
	}
}

static void draw_header(void) {
	clear(header_area);
	draw_columns(header, header_area, focus == FOCUS_HEADER ? item : -1);
}

static void draw_menus(void) {
	const rect_t area = inset(menus_area, 12, 10);

	clear(menus_area);
	gfx_SetTextBGColor(COLOR_TRANSPARENT);
	gfx_SetTextFGColor(COLOR_TEXT);

	for (unsigned i = 0; i < countof(menus); i++) {
		const int y = area.y + MENU_ROW * (int)i;

		gfx_PrintStringXY(menus[i].label, area.x, y);

		if (i == menu && focus != FOCUS_HEADER)
			draw_marker(area.x, y + TEXT_HEIGHT / 2, focus == FOCUS_MENUS ? COLOR_PURPLE : COLOR_BLUE);
	}
}

static void draw_options(void) {
	clear(options_area);
	draw_form(menus[menu].content, inset(options_area, 12, 6), focus == FOCUS_OPTIONS ? item : -1);
}

static void draw_screen(void) {
	draw_background();
	draw_header();
	draw_menus();
	draw_options();
}

static void draw_console(void) {
	gfx_SetColor(COLOR_BACKGROUND);
	gfx_FillRectangle(console_area.x, console_area.y, console_area.w, console_area.h);
	gfx_SetColor(COLOR_BLUE);
	gfx_Rectangle(console_area.x, console_area.y, console_area.w, console_area.h);
	gfx_Rectangle(console_area.x + 2, console_area.y + 2, console_area.w - 4, console_area.h - 30);

	draw_form(console_content, inset(console_area, 6, 4), -1);

	console_drawn = true;
}

static void console_write(const char *text) {
	if (!console_drawn)
		draw_console();

	gfx_PrintStringXY(text, console_area.x + 2 + 4, console_area.y + 2 + 4 + console_index * TEXT_HEIGHT);

	console_index++;
}

/* Lets the user close the console */
static void console_finish(void) {
	draw_form(console_content, inset(console_area, 6, 4), 0);
	console_done = true;
}

/* Returns to the main screen */
static void close_console(void) {
	console_drawn = false;
	console_done = false;
	console_index = 0;

	draw_screen();
}

/* Returns the index of the next element of content from index in the direction of step that can be focused, or -1 */
static int next_focusable(const element_t *content, int index, int step) {
	for (index += step; index >= 0 && content[index].type != ELEMENT_END; index += step)
		if (content[index].type != ELEMENT_TEXT)
			return index;

	return -1;
}

/* Toggles a checkbox, steps a variable by step, or runs a button */
static void activate(const element_t *e, int step) {
	switch (e->type) {
		case ELEMENT_CHECKBOX: *e->checked = !*e->checked; break;
		case ELEMENT_VARIABLE: *e->variable = (*e->variable + countof(variables) + step) % countof(variables); break;
		case ELEMENT_BUTTON: e->action(); break;
		default: break;
	}
}

static void focus_header(void) {
	focus = FOCUS_HEADER;
	item = 0;
	draw_header();
	draw_menus();
	draw_options();
}

static void handle_header(uint8_t key) {
	switch (key) {
		case sk_Down:
			focus = FOCUS_MENUS;
			draw_header();
			draw_menus();
			break;
		case sk_Left:
		case sk_Right: {
			const int next = next_focusable(header, item, key == sk_Left ? -1 : 1);
			if (next >= 0) {
				item = next;
				draw_header();
			}
			break;
		}
		case sk_Enter:
		case sk_Up:
			activate(&header[item], key == sk_Enter ? 1 : -1);
			draw_header();
			break;
		default: break;
	}
}

static void handle_menus(uint8_t key) {
	switch (key) {
		case sk_Up:
			if (menu == 0) {
				focus_header();
				break;
			}
			menu--;
			draw_menus();
			draw_options();
			break;
		case sk_Down:
			if (menu + 1 < countof(menus)) {
				menu++;
				draw_menus();
				draw_options();
			}
			break;
		case sk_Right:
		case sk_Enter: {
			const int first = next_focusable(menus[menu].content, -1, 1);
			if (first >= 0) {
				focus = FOCUS_OPTIONS;
				item = first;
				draw_menus();
				draw_options();
			}
			break;
		}
		default: break;
	}
}

static void handle_options(uint8_t key) {
	const element_t *e = &menus[menu].content[item];

	switch (key) {
		case sk_Up:
		case sk_Down: {
			const int next = next_focusable(menus[menu].content, item, key == sk_Up ? -1 : 1);
			if (next >= 0) {
				item = next;
				draw_options();
			} else if (key == sk_Up) {
				focus_header();
			}
			break;
		}
		case sk_Left:
			focus = FOCUS_MENUS;
			draw_menus();
			draw_options();
			break;
		case sk_Enter:
			activate(e, 1);
			if (!console_drawn)
				draw_options();
			break;
		default:
			if (e->type == ELEMENT_CHARACTER) {
				const char value = e->character.from_key(key);
				if (value != 0) {
					*e->character.value = value;
					draw_options();
				}
			}
			break;
	}
}

/*Opens the editor for a variable element other than Ans*/
static void edit(const element_t *e) {
	if (e->type != ELEMENT_VARIABLE || is_ans(*e->variable))
		return;

	editor_Run(variables[*e->variable].name, variables[*e->variable].token);
	draw_screen();
}

static void handle_input(uint8_t key) {
	if (console_drawn) {
		if (console_done && key == sk_Enter)
			activate(&console_content[0], 1);
		return;
	}

	if (key == sk_Yequ && focus != FOCUS_MENUS) {
		edit(focus == FOCUS_HEADER ? &header[item] : &menus[menu].content[item]);
		return;
	}

	switch (focus) {
		case FOCUS_HEADER: handle_header(key); break;
		case FOCUS_MENUS: handle_menus(key); break;
		case FOCUS_OPTIONS: handle_options(key); break;
	}
}

void gui_Run(void) {
	os_ClrHome();
	gfx_Begin();

	draw_screen();

	while (true) {
		const uint8_t key = os_GetCSC();

		if (key == sk_Clear)
			break;

		handle_input(key);
	}

	gfx_End();

	id_UnloadAll();
}

typedef enum {
	CID_GENERAL = 1 << 0,
	CID_TRIG = 1 << 1,
	CID_TRIG_CONSTANTS = 1 << 2,
	CID_TRIG_INV_CONSTANTS = 1 << 3,
	CID_HYPERBOLIC = 1 << 4,
	CID_COMPLEX = 1 << 5,
	CID_DERIVATIVE = 1 << 6,
} compile_ids_mask;

/* Entry i is the table for bit i of compile_ids_mask */
static struct compile_info {
	const char *label;
	pcas_id_t *const table;
	bool compiled;
} compile_info[] = {
	{"basic", id_general, false},
	{"trig", id_trig_identities, false},
	{"constant trig", id_trig_constants, false},
	{"inverse trig", id_trig_inv_constants, false},
	{"hyperbolic", id_hyperbolic, false},
	{"complex", id_complex, false},
	{"derivative", id_derivative, false}
};

#define NUM_COMPILE_INFO countof(compile_info)
#define CID_ALL ((1 << NUM_COMPILE_INFO) - 1)

/* Loads the identity tables in mask that are not loaded yet */
static void compile_ids(compile_ids_mask mask) {
	char buffer[50];

	for (unsigned i = 0; i < NUM_COMPILE_INFO; i++) {
		if (!(mask & (1 << i)) || compile_info[i].compiled)
			continue;

		sprintf(buffer, "Compiling %s ids...", compile_info[i].label);
		console_write(buffer);
		id_LoadTable(compile_info[i].table);
		compile_info[i].compiled = true;
	}
}

static pcas_ast_t *parse_variable(unsigned variable, pcas_error_t *err) {
	return parse_from_tok(variables[variable].token, err);
}

static unsigned parse_variable_list(unsigned variable, pcas_ast_t **items, pcas_error_t *err) {
	return parse_list_from_tok(variables[variable].token, items, MAX_ITEMS, err);
}

static void write_variable(unsigned variable, pcas_ast_t *expression, pcas_error_t *err) {
	write_to_tok(variables[variable].token, expression, err);
}

static void execute_simplify(void) {
	char buffer[50];

	simplify_flags flags = SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL | SIMP_DERIV | SIMP_LIKE_TERMS;
	compile_ids_mask ids = 0;

	if (simplify_options.general) {
		ids |= CID_GENERAL;
		flags |= SIMP_ID_GENERAL;
	}
	if (simplify_options.trig) {
		ids |= CID_TRIG;
		flags |= SIMP_ID_TRIG;
	}
	if (simplify_options.hyperbolic) {
		ids |= CID_HYPERBOLIC;
		flags |= SIMP_ID_HYPERBOLIC;
	}
	if (simplify_options.complex) {
		ids |= CID_COMPLEX;
		flags |= SIMP_ID_COMPLEX;
	}
	if (simplify_options.trig_constants) {
		ids |= CID_TRIG_CONSTANTS;
		flags |= SIMP_ID_TRIG_CONSTANTS;
	}
	if (simplify_options.trig_inv_constants) {
		ids |= CID_TRIG_INV_CONSTANTS;
		flags |= SIMP_ID_TRIG_INV_CONSTANTS;
	}

	compile_ids(ids);

	console_write("Parsing input...");

	pcas_error_t err;
	pcas_ast_t *expression = parse_variable(input, &err);

	if (err == E_SUCCESS) {
		if (expression != NULL) {
			console_write("Simplifying...");

			simplify(expression, flags);
			simplify_canonical_form(expression, CANONICAL_ALL);

			console_write("Exporting...");

			write_variable(output, expression, &err);

			ast_Cleanup(expression);

			if (err == E_SUCCESS) {
				console_write("Success.");
			} else {
				sprintf(buffer, "Failed. %s.", error_text[err]);
				console_write(buffer);
			}

		} else {
			console_write("Failed. Empty input.");
		}

	} else {
		sprintf(buffer, "Failed. %s.", error_text[err]);
		console_write(buffer);
		if (is_ans(input))
			console_write("Make sure Ans is a string.");
	}

	console_finish();
}

static void execute_evaluate(void) {
	char buffer[50];

	const bool should_eval = evaluate_options.constants;
	const bool should_sub = evaluate_options.substitute;

	console_write("Parsing input...");

	pcas_error_t err;
	pcas_ast_t *expression = parse_variable(input, &err);

	if (err == E_SUCCESS) {
		if (expression != NULL) {
			simplify(expression, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL);

			if (should_sub) {
				pcas_error_t err;

				console_write("Parsing sub from...");
				pcas_ast_t *sub_from = parse_variable(evaluate_options.from, &err);
				simplify(sub_from, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL);

				if (err == E_SUCCESS) {
					if (sub_from != NULL) {
						console_write("Parsing sub to...");
						pcas_ast_t *sub_to = parse_variable(evaluate_options.to, &err);
						simplify(sub_to, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL);

						if (err == E_SUCCESS) {
							if (sub_to != NULL) {
								console_write("Substituting...");
								substitute(expression, sub_from, sub_to);

								ast_Cleanup(sub_from);
								ast_Cleanup(sub_to);
							} else {
								console_write("Failed. Empty input.");
							}
						} else {
							sprintf(buffer, "Failed. %s.", error_text[err]);
							console_write(buffer);
						}
					} else {
						console_write("Failed. Empty input.");
					}

				} else {
					sprintf(buffer, "Failed. %s.", error_text[err]);
					console_write(buffer);
					if (is_ans(evaluate_options.from))
						console_write("Make sure Ans is a string.");
				}
			}

			if (should_eval) {
				console_write("Evaluating constants..");
				eval(expression, EVAL_ALL);
			}

			simplify(expression, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL | SIMP_LIKE_TERMS);
			simplify_canonical_form(expression, CANONICAL_ALL);

			console_write("Exporting...");

			write_variable(output, expression, &err);

			ast_Cleanup(expression);

			if (err == E_SUCCESS) {
				console_write("Success.");
			} else {
				sprintf(buffer, "Failed. %s.", error_text[err]);
				console_write(buffer);
			}

		} else {
			console_write("Failed. Empty input.");
		}

	} else {
		sprintf(buffer, "Failed. %s.", error_text[err]);
		console_write(buffer);
		if (is_ans(input))
			console_write("Make sure Ans is a string.");
	}

	console_finish();
}

static void execute_expand(void) {
	char buffer[50];

	expand_flags flags = 0;

	if (expand_options.multiplication) {
		flags |= EXP_DISTRIB_NUMBERS | EXP_DISTRIB_MULTIPLICATION | EXP_DISTRIB_ADDITION;
	}
	if (expand_options.powers) {
		flags |= EXP_EXPAND_POWERS;
	}

	console_write("Parsing input...");

	pcas_error_t err;
	pcas_ast_t *expression = parse_variable(input, &err);

	if (err == E_SUCCESS) {
		if (expression != NULL) {
			console_write("Expanding...");

			simplify(expression, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL);
			expand(expression, flags);
			simplify(
				expression,
				SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL |
					(expand_options.multiplication ? SIMP_LIKE_TERMS : 0) | SIMP_EVAL
			);
			simplify_canonical_form(expression, CANONICAL_ALL ^ CANONICAL_COMBINE_POWERS);

			console_write("Exporting...");

			write_variable(output, expression, &err);

			ast_Cleanup(expression);

			if (err == E_SUCCESS) {
				console_write("Success.");
			} else {
				sprintf(buffer, "Failed. %s.", error_text[err]);
				console_write(buffer);
			}

		} else {
			console_write("Failed. Empty input.");
		}

	} else {
		sprintf(buffer, "Failed. %s.", error_text[err]);
		console_write(buffer);
		if (is_ans(input))
			console_write("Make sure Ans is a string.");
	}

	console_finish();
}

static pcas_ast_t *parse_respect_to(char character, pcas_error_t *err) {
	const char *theta = "theta";

	/*We treat the @ character as theta partially out of laziness*/
	if (character == '@')
		return parse((const uint8_t *)theta, strlen(theta), str_table, err);

	return parse((uint8_t *)&character, 1, str_table, err);
}

/*Runs a calculus function on the input with the options in context, then shows the work or the result*/
static void execute_calculus(Calculus kind, const calculus_options_t *options, const char *title) {
	char buffer[50];

	const bool show_work = options->show_work;
	const bool verify = options->verify;

	compile_ids(CID_DERIVATIVE);

	console_write("Parsing input...");

	pcas_ast_t *items[MAX_ITEMS], *solution = NULL;
	pcas_error_t err;
	unsigned count = parse_variable_list(input, items, &err);

	if (err == E_SUCCESS && verify) {
		solution = parse_variable(options->solution, &err);
		if (err == E_SUCCESS && solution == NULL)
			err = E_GENERIC;
	}

	if (err == E_SUCCESS && (count == 0 || items[0] == NULL)) {
		console_write("Failed. Empty input.");
	} else if (err != E_SUCCESS) {
		sprintf(buffer, "Failed. %s.", error_text[err]);
		console_write(buffer);
		if (is_ans(input))
			console_write("Make sure Ans is a string.");
	} else {
		pcas_ast_t *respect_to = parse_respect_to(options->respect_to, &err);

		pcas_work_t work;
		if (show_work)
			work_Start(&work);

		if (verify) {
			bool satisfied = false;
			console_write("Verifying...");
			err = calculus_Verify(items, count, respect_to, solution, &satisfied);
			if (err == E_SUCCESS)
				console_write(satisfied ? "It is a solution." : "It is not a solution.");
		} else {
			console_write(
				kind == CALCULUS_DERIVATIVE ? "Differentiating..."
				: kind == CALCULUS_INTEGRAL ? "Integrating..."
											: "Solving..."
			);
			const unsigned terms = kind == CALCULUS_DE ? (unsigned)(options->terms - '0') : 0;
			err = calculus_Run(kind, items, count, respect_to, options->series, terms, buffer);

			if (err == E_SUCCESS && kind == CALCULUS_DE)
				console_write(buffer);
		}

		if (show_work)
			work_Stop();

		if (err == E_SUCCESS && !verify) {
			console_write("Exporting...");
			write_variable(output, items[0], &err);
		}

		ast_Cleanup(respect_to);

		if (err == E_SUCCESS && show_work) {
			while (count > 0)
				ast_Cleanup(items[--count]);
			ast_Cleanup(solution);

			viewer_Show(&work, verify ? "Verify solution" : title);
			work_Cleanup(&work);
			close_console();
			return;
		}

		if (show_work)
			work_Cleanup(&work);

		if (err == E_SUCCESS) {
			console_write("Success.");
		} else {
			sprintf(buffer, "Failed. %s.", error_text[err]);
			console_write(buffer);
		}
	}

	while (count > 0)
		ast_Cleanup(items[--count]);
	ast_Cleanup(solution);

	console_finish();
}

static void execute_derivative(void) {
	execute_calculus(CALCULUS_DERIVATIVE, &derivative_options, "Derivative");
}

static void execute_integral(void) {
	execute_calculus(CALCULUS_INTEGRAL, &integral_options, "Integral");
}

static void execute_de(void) {
	execute_calculus(CALCULUS_DE, &de_options, "Differential equation");
}

#else
typedef int make_iso_compilers_happy;
#endif
