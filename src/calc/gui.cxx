#ifndef COMPILE_PC

#include "gui.hxx"

#include <tice.h>
#include <fileioc.h>

#include <graphx.h>
#include <keypadc.h>

#include <string.h>
#include "../countof.hxx"

#include "../parser.hxx"
#include "../cas/cas.hxx"
#include "../cas/identities.hxx"
#include "../cas/derivative.hxx"
#include "../version.h"

#include "calculus.hxx"
#include "editor.hxx"
#include "vars.hxx"
#include "viewer.hxx"
#include "../work.hxx"

constexpr int LINE_HEIGHT = 10;
constexpr int MENU_ROW = 16;
constexpr int CHECKBOX_SIZE = 8;
constexpr int CHECKBOX_ROW = 12;
constexpr int SELECT_ROW = 18;
constexpr int BOX_HEIGHT = 16;
constexpr int BUTTON_HEIGHT = 20;
constexpr int VARIABLE_WIDTH = 50;
constexpr int GAP = 6;
constexpr int MARKER_OFFSET = 10;

struct Rect {
	int x, y, w, h;
};

static const Rect header_area = {40, 10, 240, 40};
static const Rect menus_area = {14, 70, 96, 136};
static const Rect options_area = {112, 70, 195, 138};
static const Rect console_area = {LCD_WIDTH / 6, LCD_HEIGHT / 6, LCD_WIDTH * 2 / 3, LCD_HEIGHT * 2 / 3};

static Rect inset(Rect r, int x, int y) {
	return (Rect){r.x + x, r.y + y, r.w - 2 * x, r.h - 2 * y};
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

struct Element {
	enum class Type : unsigned char { End, Text, Checkbox, Variable, Character, Button };

	Type type;
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
};

struct Menu {
	const char *label;
	/*Ends with an Element::Type::End*/
	const Element *content;
};

static char letter_key(uint8_t key);
static char digit_key(uint8_t key);

static constexpr Element end{.type = Element::Type::End, .text = nullptr};

static constexpr Element text(const char *label) {
	return {.type = Element::Type::Text, .text = label};
}

static constexpr Element checkbox(const char *label, bool *state) {
	return {.type = Element::Type::Checkbox, .text = label, .checked = state};
}

static constexpr Element variable(const char *label, unsigned *state) {
	return {.type = Element::Type::Variable, .text = label, .variable = state};
}

static constexpr Element letter(const char *label, char *state) {
	return {.type = Element::Type::Character, .text = label, .character = {state, letter_key}};
}

static constexpr Element digit(const char *label, char *state) {
	return {.type = Element::Type::Character, .text = label, .character = {state, digit_key}};
}

static constexpr Element button(const char *label, void (*action)()) {
	return {.type = Element::Type::Button, .text = label, .action = action};
}

struct CalculusOptions {
	char respect_to;
	bool show_work;
	bool verify;
	unsigned solution;
	bool series;
	char terms;
};

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

static CalculusOptions derivative_options = {.respect_to = 'X', .show_work = true};
static CalculusOptions integral_options = {.respect_to = 'X', .show_work = true};
static CalculusOptions de_options =
	{.respect_to = 'X', .show_work = true, .solution = 2, .terms = '0' + DiffEq::default_terms};

static void execute_simplify(void);
static void execute_evaluate(void);
static void execute_expand(void);
static void execute_derivative(void);
static void execute_integral(void);
static void execute_de(void);
static void close_console(void);

static constexpr Element header[] = {variable("Input", &input), variable("Output", &output), end};

static constexpr Element simplify_content[] = {
	checkbox("Basic identities", &simplify_options.general),
	checkbox("Trig identities", &simplify_options.trig),
	checkbox("Hyperbolic identities", &simplify_options.hyperbolic),
	checkbox("Complex identities", &simplify_options.complex),
	checkbox("Evaluate trig", &simplify_options.trig_constants),
	checkbox("Evaluate inverse trig", &simplify_options.trig_inv_constants),
	button("Simplify", execute_simplify),
	end
};

static constexpr Element evaluate_content[] = {
	checkbox("Evaluate constants", &evaluate_options.constants),
	checkbox("Substitute expression:", &evaluate_options.substitute),
	variable("From:", &evaluate_options.from),
	variable("To:", &evaluate_options.to),
	button("Evaluate", execute_evaluate),
	end
};

static constexpr Element expand_content[] = {
	checkbox("Expand multiplication", &expand_options.multiplication),
	checkbox("Expand powers", &expand_options.powers),
	button("Expand", execute_expand),
	end
};

static constexpr Element derivative_content[] = {
	letter("Respect to:", &derivative_options.respect_to),
	checkbox("Show work", &derivative_options.show_work),
	button("Differentiate", execute_derivative),
	end
};

static constexpr Element integral_content[] = {
	letter("Respect to:", &integral_options.respect_to),
	checkbox("Show work", &integral_options.show_work),
	button("Integrate", execute_integral),
	end
};

static constexpr Element de_content[] = {
	letter("Respect to:", &de_options.respect_to),
	checkbox("Show work", &de_options.show_work),
	checkbox("Verify solution", &de_options.verify),
	variable("Solution in:", &de_options.solution),
	checkbox("Power series", &de_options.series),
	digit("Series terms:", &de_options.terms),
	button("Solve", execute_de),
	end
};

static constexpr Element about_content[] = {
	text("PinaCAS v" PCAS_VERSION " " PCAS_BUILD_DATE),
	text("github.com/james-pre/PinaCAS"),
	text(""),
	text("Credits:"),
	text("James Prevett"),
	text("Nathan Farlow (PineappleCAS)"),
	text("Michael Fromberger (imath)"),
	end
};

static const Menu menus[] = {
	{"Simplify", simplify_content},
	{"Evaluate", evaluate_content},
	{"Expand", expand_content},
	{"Derivative", derivative_content},
	{"Integral", integral_content},
	{"Solve DE", de_content},
	{"About", about_content}
};

static constexpr Element console_content[] = {button("Close", close_console), end};

enum class Focus : unsigned char { Header, Menus, Options };

static Focus focus = Focus::Menus;
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
	gfx_PrintStringXY(text, x - static_cast<int>(gfx_GetStringWidth(text)) / 2, y);
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

static void clear(Rect r) {
	gfx_SetColor(COLOR_BACKGROUND);
	gfx_FillRectangle(r.x, r.y, r.w, r.h);
}

static void draw_marker(int x, int y, uint8_t color) {
	gfx_SetTextFGColor(color);
	gfx_PrintStringXY(">", x - MARKER_OFFSET, y - TEXT_HEIGHT / 2);
	gfx_SetTextFGColor(COLOR_TEXT);
}

static void draw_box(Rect r, const char *text) {
	gfx_SetColor(COLOR_PURPLE);
	gfx_Rectangle(r.x, r.y, r.w, r.h);
	draw_string_centered(text, r.x + r.w / 2, r.y + r.h / 2 - TEXT_HEIGHT / 2);
}

static int button_width(const Element *e) {
	return static_cast<int>(gfx_GetStringWidth(e->text) + 16);
}

static int value_width(const Element *e) {
	return e->type == Element::Type::Variable ? VARIABLE_WIDTH : BOX_HEIGHT;
}

/*Draws the value of a variable or character element in r*/
static void draw_value(const Element *e, Rect r) {
	if (e->type == Element::Type::Variable) {
		draw_box(r, variables[*e->variable].name);
	} else {
		const char character[2] = {*e->character.value, '\0'};
		draw_box(r, character);
	}
}

/*Prints text wrapped to width, breaking at the last space that fits or else within a word. Returns the number of lines.*/
static int wrap_text(const char *text, int x, int y, unsigned width) {
	char line[48];
	int lines = 0;
	unsigned length = 0;

	line[0] = '\0';

	for (; *text != '\0'; text++) {
		line[length++] = *text;
		line[length] = '\0';

		if (length > 1 && (length == sizeof(line) - 1 || gfx_GetStringWidth(line) > width)) {
			const char *space = strrchr(line, ' ');
			const unsigned keep = space != nullptr ? static_cast<unsigned>((space - line)) : length - 1;
			const unsigned skip = space != nullptr ? keep + 1 : keep;
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
static void draw_form(const Element *content, Rect area, int focused) {
	int column = 0, buttons = -GAP;

	for (const Element *e = content; e->type != Element::Type::End; e++) {
		if (e->type == Element::Type::Button) {
			buttons += button_width(e) + GAP;
		} else if (e->type == Element::Type::Variable || e->type == Element::Type::Character) {
			const int width = static_cast<int>(gfx_GetStringWidth(e->text));
			if (width > column)
				column = width;
		}
	}

	column += area.x + GAP;
	int button_x = area.x + (area.w - buttons) / 2, y = area.y;

	gfx_SetTextBGColor(COLOR_TRANSPARENT);
	gfx_SetTextFGColor(COLOR_TEXT);

	for (const Element *e = content; e->type != Element::Type::End; e++) {
		Rect r;
		int marker_x;

		switch (e->type) {
			case Element::Type::Text:
				y += LINE_HEIGHT * wrap_text(e->text, area.x, y, static_cast<unsigned>(area.w));
				continue;

			case Element::Type::Checkbox:
				r = (Rect){area.x, y + (CHECKBOX_ROW - CHECKBOX_SIZE) / 2, CHECKBOX_SIZE, CHECKBOX_SIZE};
				gfx_SetColor(COLOR_PURPLE);
				gfx_Rectangle(r.x, r.y, r.w, r.h);
				if (*e->checked)
					gfx_FillRectangle(r.x + 2, r.y + 2, r.w - 4, r.h - 4);
				gfx_PrintStringXY(e->text, r.x + r.w + 4, r.y + r.h / 2 - TEXT_HEIGHT / 2);
				marker_x = r.x;
				y += CHECKBOX_ROW;
				break;

			case Element::Type::Variable:
			case Element::Type::Character:
				r = (Rect){column, y + (SELECT_ROW - BOX_HEIGHT) / 2, value_width(e), BOX_HEIGHT};
				gfx_PrintStringXY(e->text, area.x, r.y + r.h / 2 - TEXT_HEIGHT / 2);
				draw_value(e, r);
				marker_x = area.x;
				y += SELECT_ROW;
				break;

			case Element::Type::Button:
				r = (Rect){button_x, area.y + area.h - BUTTON_HEIGHT, button_width(e), BUTTON_HEIGHT};
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
static void draw_columns(const Element *content, Rect area, int focused) {
	int count = 0;

	for (const Element *e = content; e->type != Element::Type::End; e++)
		count++;

	for (const Element *e = content; e->type != Element::Type::End; e++) {
		const int center = area.x + area.w * (2 * (e - content) + 1) / (2 * count);
		const Rect r = {center - value_width(e) / 2, area.y + LINE_HEIGHT + GAP, value_width(e), BUTTON_HEIGHT};

		draw_string_centered(e->text, center, area.y + GAP / 2);
		draw_value(e, r);

		if (e - content == focused)
			draw_marker(r.x, r.y + r.h / 2, COLOR_PURPLE);
	}
}

static void draw_header(void) {
	clear(header_area);
	draw_columns(header, header_area, focus == Focus::Header ? item : -1);
}

static void draw_menus(void) {
	const Rect area = inset(menus_area, 12, 10);

	clear(menus_area);
	gfx_SetTextBGColor(COLOR_TRANSPARENT);
	gfx_SetTextFGColor(COLOR_TEXT);

	for (unsigned i = 0; i < countof(menus); i++) {
		const int y = area.y + MENU_ROW * static_cast<int>(i);

		gfx_PrintStringXY(menus[i].label, area.x, y);

		if (i == menu && focus != Focus::Header)
			draw_marker(area.x, y + TEXT_HEIGHT / 2, focus == Focus::Menus ? COLOR_PURPLE : COLOR_BLUE);
	}
}

static void draw_options(void) {
	clear(options_area);
	draw_form(menus[menu].content, inset(options_area, 12, 6), focus == Focus::Options ? item : -1);
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
static int next_focusable(const Element *content, int index, int step) {
	for (index += step; index >= 0 && content[index].type != Element::Type::End; index += step)
		if (content[index].type != Element::Type::Text)
			return index;

	return -1;
}

/* Toggles a checkbox, steps a variable by step, or runs a button */
static void activate(const Element *e, int step) {
	switch (e->type) {
		case Element::Type::Checkbox: *e->checked = !*e->checked; break;
		case Element::Type::Variable:
			*e->variable = (*e->variable + countof(variables) + static_cast<unsigned>(step)) % countof(variables);
			break;
		case Element::Type::Button: e->action(); break;
		default: break;
	}
}

static void focus_header(void) {
	focus = Focus::Header;
	item = 0;
	draw_header();
	draw_menus();
	draw_options();
}

static void handle_header(uint8_t key) {
	switch (key) {
		case sk_Down:
			focus = Focus::Menus;
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
				focus = Focus::Options;
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
	const Element *e = &menus[menu].content[item];

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
			focus = Focus::Menus;
			draw_menus();
			draw_options();
			break;
		case sk_Enter:
			activate(e, 1);
			if (!console_drawn)
				draw_options();
			break;
		default:
			if (e->type == Element::Type::Character) {
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
static void edit(const Element *e) {
	if (e->type != Element::Type::Variable || is_ans(*e->variable))
		return;

	editor::run(variables[*e->variable].name, variables[*e->variable].token);
	draw_screen();
}

static void handle_input(uint8_t key) {
	if (console_drawn) {
		if (console_done && key == sk_Enter)
			activate(&console_content[0], 1);
		return;
	}

	if (key == sk_Yequ && focus != Focus::Menus) {
		edit(focus == Focus::Header ? &header[item] : &menus[menu].content[item]);
		return;
	}

	switch (focus) {
		case Focus::Header: handle_header(key); break;
		case Focus::Menus: handle_menus(key); break;
		case Focus::Options: handle_options(key); break;
	}
}

void gui::run() {
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

	id::unloadAll();
}

enum class CompileIds : unsigned char {
	General = 1 << 0,
	Trig = 1 << 1,
	TrigConstants = 1 << 2,
	TrigInvConstants = 1 << 3,
	Hyperbolic = 1 << 4,
	Complex = 1 << 5,
	Derivative = 1 << 6,
};

template <> inline constexpr bool is_flags<CompileIds> = true;

/* Entry i is the table for bit i of CompileIds */
struct CompileInfo {
	const char *label;
	id::Identity *const table;
	bool compiled;
};

static CompileInfo compile_info[] = {
	{"basic", id::general, false},
	{"trig", id::trig_identities, false},
	{"constant trig", id::trig_constants, false},
	{"inverse trig", id::trig_inv_constants, false},
	{"hyperbolic", id::hyperbolic, false},
	{"complex", id::complex, false},
	{"derivative", id::derivative, false}
};

/* Loads the identity tables in mask that are not loaded yet */
static void compile_ids(CompileIds mask) {
	char buffer[50];

	for (unsigned i = 0; i < countof(compile_info); i++) {
		if (!has(mask, static_cast<CompileIds>(1 << i)) || compile_info[i].compiled)
			continue;

		sprintf(buffer, "Compiling %s ids...", compile_info[i].label);
		console_write(buffer);
		id::loadTable(compile_info[i].table);
		compile_info[i].compiled = true;
	}
}

static ast *parse_variable(unsigned variable, Error *err) {
	return parse_from_tok(variables[variable].token, err);
}

static unsigned parse_variable_list(unsigned variable, ast **items, Error *err) {
	return parse_list_from_tok(variables[variable].token, items, MAX_ITEMS, err);
}

static void write_variable(unsigned variable, ast &expression, Error *err) {
	write_to_tok(variables[variable].token, expression, err);
}

static void execute_simplify(void) {
	char buffer[50];

	Simp flags = Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval | Simp::Deriv | Simp::LikeTerms;
	CompileIds ids{};

	if (simplify_options.general) {
		ids |= CompileIds::General;
		flags |= Simp::IdGeneral;
	}
	if (simplify_options.trig) {
		ids |= CompileIds::Trig;
		flags |= Simp::IdTrig;
	}
	if (simplify_options.hyperbolic) {
		ids |= CompileIds::Hyperbolic;
		flags |= Simp::IdHyperbolic;
	}
	if (simplify_options.complex) {
		ids |= CompileIds::Complex;
		flags |= Simp::IdComplex;
	}
	if (simplify_options.trig_constants) {
		ids |= CompileIds::TrigConstants;
		flags |= Simp::IdTrigConstants;
	}
	if (simplify_options.trig_inv_constants) {
		ids |= CompileIds::TrigInvConstants;
		flags |= Simp::IdTrigInvConstants;
	}

	compile_ids(ids);

	console_write("Parsing input...");

	Error err;
	ast *expression = parse_variable(input, &err);

	if (err == Error::Success) {
		if (expression != nullptr) {
			console_write("Simplifying...");

			simplify(*expression, flags);
			simplify_canonical_form(*expression, Canonical::All);

			console_write("Exporting...");

			write_variable(output, *expression, &err);

			ast::dispose(expression);

			if (err == Error::Success) {
				console_write("Success.");
			} else {
				sprintf(buffer, "Failed. %s.", error_text(err));
				console_write(buffer);
			}

		} else {
			console_write("Failed. Empty input.");
		}

	} else {
		sprintf(buffer, "Failed. %s.", error_text(err));
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

	Error err;
	ast *expression = parse_variable(input, &err);

	if (err == Error::Success) {
		if (expression != nullptr) {
			simplify(*expression, Simp::Normalize | Simp::Commutative | Simp::Rational);

			if (should_sub) {
				Error err;

				console_write("Parsing sub from...");
				ast *sub_from = parse_variable(evaluate_options.from, &err);
				simplify(*sub_from, Simp::Normalize | Simp::Commutative | Simp::Rational);

				if (err == Error::Success) {
					if (sub_from != nullptr) {
						console_write("Parsing sub to...");
						ast *sub_to = parse_variable(evaluate_options.to, &err);
						simplify(*sub_to, Simp::Normalize | Simp::Commutative | Simp::Rational);

						if (err == Error::Success) {
							if (sub_to != nullptr) {
								console_write("Substituting...");
								substitute(*expression, *sub_from, *sub_to);

								ast::dispose(sub_from);
								ast::dispose(sub_to);
							} else {
								console_write("Failed. Empty input.");
							}
						} else {
							sprintf(buffer, "Failed. %s.", error_text(err));
							console_write(buffer);
						}
					} else {
						console_write("Failed. Empty input.");
					}

				} else {
					sprintf(buffer, "Failed. %s.", error_text(err));
					console_write(buffer);
					if (is_ans(evaluate_options.from))
						console_write("Make sure Ans is a string.");
				}
			}

			if (should_eval) {
				console_write("Evaluating constants..");
				eval(*expression, Eval::All);
			}

			simplify(*expression, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval | Simp::LikeTerms);
			simplify_canonical_form(*expression, Canonical::All);

			console_write("Exporting...");

			write_variable(output, *expression, &err);

			ast::dispose(expression);

			if (err == Error::Success) {
				console_write("Success.");
			} else {
				sprintf(buffer, "Failed. %s.", error_text(err));
				console_write(buffer);
			}

		} else {
			console_write("Failed. Empty input.");
		}

	} else {
		sprintf(buffer, "Failed. %s.", error_text(err));
		console_write(buffer);
		if (is_ans(input))
			console_write("Make sure Ans is a string.");
	}

	console_finish();
}

static void execute_expand(void) {
	char buffer[50];

	Expand flags{};

	if (expand_options.multiplication) {
		flags |= Expand::DistribNumbers | Expand::DistribMultiplication | Expand::DistribAddition;
	}
	if (expand_options.powers) {
		flags |= Expand::Powers;
	}

	console_write("Parsing input...");

	Error err;
	ast *expression = parse_variable(input, &err);

	if (err == Error::Success) {
		if (expression != nullptr) {
			console_write("Expanding...");

			simplify(*expression, Simp::Normalize | Simp::Commutative | Simp::Rational);
			expand(*expression, flags);
			simplify(
				*expression,
				Simp::Normalize | Simp::Commutative | Simp::Rational |
					(expand_options.multiplication ? Simp::LikeTerms : Simp{}) | Simp::Eval
			);
			simplify_canonical_form(*expression, Canonical::All & ~Canonical::CombinePowers);

			console_write("Exporting...");

			write_variable(output, *expression, &err);

			ast::dispose(expression);

			if (err == Error::Success) {
				console_write("Success.");
			} else {
				sprintf(buffer, "Failed. %s.", error_text(err));
				console_write(buffer);
			}

		} else {
			console_write("Failed. Empty input.");
		}

	} else {
		sprintf(buffer, "Failed. %s.", error_text(err));
		console_write(buffer);
		if (is_ans(input))
			console_write("Make sure Ans is a string.");
	}

	console_finish();
}

static ast *parse_respect_to(char character, Error *err) {
	const char *theta = "theta";

	/*We treat the @ character as theta partially out of laziness*/
	if (character == '@')
		return parse(theta, strlen(theta), str_table, err);

	return parse(&character, 1, str_table, err);
}

/*Runs a calculus function on the input with the options in context, then shows the work or the result*/
static void execute_calculus(calculus::Kind kind, const CalculusOptions *options, const char *title) {
	char buffer[50];

	const bool show_work = options->show_work;
	const bool verify = options->verify;

	compile_ids(CompileIds::Derivative);

	console_write("Parsing input...");

	ast *items[MAX_ITEMS], *solution = nullptr;
	Error err;
	unsigned count = parse_variable_list(input, items, &err);

	if (err == Error::Success && verify) {
		solution = parse_variable(options->solution, &err);
		if (err == Error::Success && solution == nullptr)
			err = Error::Generic;
	}

	if (err == Error::Success && (count == 0 || items[0] == nullptr)) {
		console_write("Failed. Empty input.");
	} else if (err != Error::Success) {
		sprintf(buffer, "Failed. %s.", error_text(err));
		console_write(buffer);
		if (is_ans(input))
			console_write("Make sure Ans is a string.");
	} else {
		ast *respect_to = parse_respect_to(options->respect_to, &err);

		work::Record work;
		if (show_work)
			work::start(work);

		if (verify) {
			bool satisfied = false;
			console_write("Verifying...");
			err = calculus::verify(items, count, *respect_to, *solution, &satisfied);
			if (err == Error::Success)
				console_write(satisfied ? "It is a solution." : "It is not a solution.");
		} else {
			console_write(
				kind == calculus::Kind::Derivative ? "Differentiating..."
				: kind == calculus::Kind::Integral ? "Integrating..."
												   : "Solving..."
			);
			const unsigned terms = kind == calculus::Kind::DiffEq ? static_cast<unsigned>((options->terms - '0')) : 0;
			err = calculus::run(kind, items, count, *respect_to, options->series, terms, buffer);

			if (err == Error::Success && kind == calculus::Kind::DiffEq)
				console_write(buffer);
		}

		if (show_work)
			work::stop();

		if (err == Error::Success && !verify) {
			console_write("Exporting...");
			write_variable(output, *items[0], &err);
		}

		ast::dispose(respect_to);

		if (err == Error::Success && show_work) {
			while (count > 0)
				ast::dispose(items[--count]);
			ast::dispose(solution);

			viewer::show(&work, verify ? "Verify solution" : title);
			work.clear();
			close_console();
			return;
		}

		if (show_work)
			work.clear();

		if (err == Error::Success) {
			console_write("Success.");
		} else {
			sprintf(buffer, "Failed. %s.", error_text(err));
			console_write(buffer);
		}
	}

	while (count > 0)
		ast::dispose(items[--count]);
	ast::dispose(solution);

	console_finish();
}

static void execute_derivative(void) {
	execute_calculus(calculus::Kind::Derivative, &derivative_options, "Derivative");
}

static void execute_integral(void) {
	execute_calculus(calculus::Kind::Integral, &integral_options, "Integral");
}

static void execute_de(void) {
	execute_calculus(calculus::Kind::DiffEq, &de_options, "Differential equation");
}

#endif
