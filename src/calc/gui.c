#ifndef COMPILE_PC

#include "gui.h"

#include <tice.h>
#include <fileioc.h>

#include <graphx.h>
#include <keypadc.h>

#include <string.h>

#include "../parser.h"
#include "../cas/cas.h"
#include "../cas/identities.h"
#include "../cas/derivative.h"
#include "../version.h"

#include "calculus.h"
#include "vars.h"
#include "viewer.h"
#include "../work.h"

void draw_string_centered(char *text, int x, int y) {
	unsigned len;
	len = gfx_GetStringWidth(text);

	gfx_SetTextBGColor(COLOR_TRANSPARENT);
	gfx_SetTextFGColor(COLOR_TEXT);

	gfx_PrintStringXY(text, x - len / 2, y);
}

#define ABOUT_WIDTH 190

static const char *about_lines[] = {
	"github.com/",
	"james-pre/PinaCAS",
	"",
	"Credits:",
	"James Prevett",
	"Nathan Farlow",
	"(PineappleCAS)",
	"Michael Fromberger",
	"(imath)"
};

void draw_background(void) {
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

	draw_string_centered("Input", LCD_WIDTH / 4 + 20, 14);
	draw_string_centered("Output", LCD_WIDTH / 4 * 3 - 20, 14);

	/*Outer selection border*/
	gfx_Rectangle(10, 50, LCD_WIDTH - 20, 160);

	/*Function rectangle*/
	gfx_Rectangle(10 + 2, 50 + 2, 100, 160 - 4);
	draw_string_centered("Function", 10 + 2 + 100 / 2, 50 + 10);

	draw_string_centered("Options", 10 + 2 + 100 + (LCD_WIDTH - 10 - 10 - 2 - 100) / 2, 50 + 10);
}

typedef enum { GUI_LABEL, GUI_CHECKBOX, GUI_CHARSELECT, GUI_DROPDOWN, GUI_BUTTON } GuiType;

/*I miss oop so much*/
typedef struct {
	GuiType type;

	int x, y, w, h;
	bool active;

	char *text;

	/*For checkboxes*/
	bool checked;
	/*For dropdowns*/
	unsigned index;
	/*for char select*/
	char character;

	/*For dropdowns*/
	unsigned selected_index;
} view_t;

#define NUM_DROPDOWN_ENTRIES 21
char *dropdown_entries[NUM_DROPDOWN_ENTRIES] = {"Y1",   "Y2",   "Y3",   "Y4",   "Y5",   "Y6",   "Y7",
												"Y8",   "Y9",   "Y0",   "Str1", "Str2", "Str3", "Str4",
												"Str4", "Str6", "Str7", "Str8", "Str9", "Str0", "Ans"};

#define NUM_IO 2
#define NUM_FUNCTION 7
#define NUM_SIMPLIFY 7
#define NUM_EVALUATE 5
#define NUM_EXPAND 3
#define NUM_DERIVATIVE 3
#define NUM_INTEGRAL 3
#define NUM_DE 5
#define NUM_ABOUT 0

view_t *io_context[NUM_IO];
view_t *function_context[NUM_FUNCTION];
view_t *simplify_context[NUM_SIMPLIFY];
view_t *evaluate_context[NUM_EVALUATE];
view_t *expand_context[NUM_EXPAND];
view_t *derivative_context[NUM_DERIVATIVE];
view_t *integral_context[NUM_INTEGRAL];
view_t *de_context[NUM_DE];
view_t *about_context[1];

view_t *from_drop, *to_drop;

view_t *button_simplify;
view_t *button_evaluate;
view_t *button_expand;
view_t *button_derivative;
view_t *button_integral;
view_t *button_de;

view_t *console_button;

typedef enum {
	CONTEXT_IO,
	CONTEXT_FUNCTION,
	CONTEXT_SIMPLIFY,
	CONTEXT_EVALUATE,
	CONTEXT_EXPAND,
	CONTEXT_DERIVATIVE,
	CONTEXT_INTEGRAL,
	CONTEXT_DE,
	CONTEXT_ABOUT,
	NUM_CONTEXTS
} Context;

unsigned elements_in_context[NUM_CONTEXTS] =
	{NUM_IO, NUM_FUNCTION, NUM_SIMPLIFY, NUM_EVALUATE, NUM_EXPAND, NUM_DERIVATIVE, NUM_INTEGRAL, NUM_DE, NUM_ABOUT};

view_t **context_lookup[NUM_CONTEXTS] = {
	io_context,
	function_context,
	simplify_context,
	evaluate_context,
	expand_context,
	derivative_context,
	integral_context,
	de_context,
	about_context
};

Context current_context = CONTEXT_FUNCTION;
unsigned active_index = 0;
unsigned function_index = 0;

void draw_label(view_t *v) {
	gfx_PrintStringXY(v->text, v->x, v->y + v->h / 2 - TEXT_HEIGHT / 2);
}

void draw_checkbox(view_t *v) {
	gfx_SetColor(COLOR_PURPLE);
	gfx_Rectangle(v->x, v->y, v->w, v->h);

	if (v->checked) {
		gfx_FillRectangle(v->x + 2, v->y + 2, v->w - 4, v->h - 4);
	}

	if (v->text != NULL) {
		gfx_PrintStringXY(v->text, v->x + v->w + 4, v->y + v->h / 2 - TEXT_HEIGHT / 2);
	}
}

void draw_dropdown(view_t *v) {
	gfx_SetColor(COLOR_PURPLE);
	gfx_Rectangle(v->x, v->y, v->w, v->h);
	draw_string_centered(dropdown_entries[v->index], v->x + v->w / 2, v->y + v->h / 2 - TEXT_HEIGHT / 2);
}

void draw_button(view_t *v) {
	gfx_SetColor(COLOR_PURPLE);
	gfx_Rectangle(v->x, v->y, v->w, v->h);
	draw_string_centered(v->text, v->x + v->w / 2, v->y + v->h / 2 - TEXT_HEIGHT / 2);
}

void draw_charselect(view_t *v) {
	char buffer[2] = {0};
	gfx_SetColor(COLOR_PURPLE);
	gfx_Rectangle(v->x, v->y, v->w, v->h);

	buffer[0] = v->character;
	draw_string_centered(buffer, v->x + v->w / 2, v->y + v->h / 2 - TEXT_HEIGHT / 2);
}

void view_draw(view_t *v) {
	gfx_SetTextBGColor(COLOR_TRANSPARENT);
	gfx_SetTextFGColor(COLOR_TEXT);

	switch (v->type) {
		case GUI_LABEL: draw_label(v); break;
		case GUI_CHECKBOX: draw_checkbox(v); break;
		case GUI_DROPDOWN: draw_dropdown(v); break;
		case GUI_BUTTON: draw_button(v); break;
		case GUI_CHARSELECT: draw_charselect(v); break;
		default: return;
	}

	if (v->active) {
		gfx_SetTextFGColor(COLOR_PURPLE);
		gfx_PrintStringXY(">", v->x - 10, v->y + v->h / 2 - TEXT_HEIGHT / 2);
	}
}

view_t *view_create(GuiType type, int x, int y, int w, int h, char *text) {
	view_t *v;
	v = calloc(1, sizeof(view_t));
	v->type = type;
	v->x = x;
	v->y = y;
	v->w = w;
	v->h = h;
	v->text = text;
	return v;
}

view_t *view_create_checkbox(int x, int y, char *text, bool checked) {
	view_t *v;
	v = view_create(GUI_CHECKBOX, x, y, 8, 8, text);
	v->checked = checked;
	return v;
}

view_t *view_create_button(int x, int y, char *text) {
	int width;
	width = gfx_GetStringWidth(text);
	return view_create(GUI_BUTTON, x - width / 2, y - TEXT_HEIGHT / 2, width + 8, 20, text);
}

view_t *view_create_dropdown(int x, int y, unsigned index) {
	view_t *v;
	v = view_create(GUI_DROPDOWN, x, y, 50, 20, NULL);
	v->index = index;
	return v;
}

view_t *view_create_label(int x, int y, char *text) {
	return view_create(GUI_LABEL, x, y, 0, 8, text);
}

view_t *view_create_charselect(int x, int y) {
	view_t *v;
	v = view_create(GUI_CHARSELECT, x, y, 16, 16, NULL);
	v->character = 'X';
	return v;
}

void draw_context(Context c) {
	unsigned i;

	gfx_SetColor(COLOR_BACKGROUND);

	switch (c) {
		case CONTEXT_IO: gfx_FillRectangle(5, 14 + 8, LCD_WIDTH - 10, 26); break;
		case CONTEXT_FUNCTION: gfx_FillRectangle(10 + 4, 70, 100 - 4, 120); break;
		default: gfx_FillRectangle(112, 70, 195, 138); break;
	}

	gfx_SetTextFGColor(COLOR_TEXT);
	if (c == CONTEXT_DE) {
		gfx_PrintStringXY("Solution in:", 136, 136 + 10 - TEXT_HEIGHT / 2);
	}

	if (c == CONTEXT_EVALUATE) {
		gfx_PrintStringXY("From: ", 124 + 25, 96 + 12 + 10 - TEXT_HEIGHT / 2);
		gfx_PrintStringXY("To: ", 124 + 25, 96 + 12 + 24 + 10 - TEXT_HEIGHT / 2);
	} else if (c == CONTEXT_ABOUT) {
		int line = 0;

		if (gfx_GetStringWidth("PinaCAS v" PCAS_VERSION " " PCAS_BUILD_DATE) <= ABOUT_WIDTH) {
			gfx_PrintStringXY("PinaCAS v" PCAS_VERSION " " PCAS_BUILD_DATE, 115, 80);
		} else {
			if (gfx_GetStringWidth("PinaCAS v" PCAS_VERSION) <= ABOUT_WIDTH) {
				gfx_PrintStringXY("PinaCAS v" PCAS_VERSION, 115, 80);
			} else {
				gfx_PrintStringXY("PinaCAS", 115, 80);
				gfx_PrintStringXY("v" PCAS_VERSION, 115, 80 + 10 * ++line);
			}
			gfx_PrintStringXY(PCAS_BUILD_DATE, 115, 80 + 10 * ++line);
		}

		for (i = 0; i < sizeof(about_lines) / sizeof(about_lines[0]); i++)
			gfx_PrintStringXY(about_lines[i], about_lines[i][0] == '(' ? 125 : 115, 80 + 10 * (line + 1 + i));
	} else if (c == CONTEXT_DERIVATIVE || c == CONTEXT_INTEGRAL || c == CONTEXT_DE) {
		gfx_PrintStringXY("Respect to: ", 124, 80);
	}

	for (i = 0; i < elements_in_context[c]; i++) {
		view_draw(context_lookup[c][i]);
	}
}

bool console_drawn = false;
int console_index = 0;

void draw_console(void) {
	gfx_SetColor(COLOR_BACKGROUND);
	gfx_FillRectangle(LCD_WIDTH / 6, LCD_HEIGHT / 6, LCD_WIDTH - LCD_WIDTH / 3, LCD_HEIGHT - LCD_HEIGHT / 3);
	gfx_SetColor(COLOR_BLUE);
	gfx_Rectangle(LCD_WIDTH / 6, LCD_HEIGHT / 6, LCD_WIDTH - LCD_WIDTH / 3, LCD_HEIGHT - LCD_HEIGHT / 3);
	gfx_Rectangle(
		LCD_WIDTH / 6 + 2, LCD_HEIGHT / 6 + 2, LCD_WIDTH - LCD_WIDTH / 3 - 4, LCD_HEIGHT - LCD_HEIGHT / 3 - 30
	);

	view_draw(console_button);

	console_drawn = true;
}

void console_write(char *text) {
	if (!console_drawn)
		draw_console();

	gfx_PrintStringXY(text, LCD_WIDTH / 6 + 2 + 4, LCD_HEIGHT / 6 + 2 + 4 + console_index * TEXT_HEIGHT);

	console_index++;
}

/*Returns to the main screen*/
void close_console(void) {
	draw_background();
	draw_context(CONTEXT_IO);
	draw_context(CONTEXT_FUNCTION);
	draw_context(current_context);

	console_button->active = false;
	console_drawn = false;
	console_index = 0;
}

void execute_simplify(void);
void execute_evaluate(void);
void execute_expand(void);
void execute_derivative(void);
void execute_integral(void);
void execute_de(void);

/*the key lookup tables for os_GetCSC()*/
const char alpha_table[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5C, 0x00, 0x57, 0x52, 0x4D, 0x48,
							0x00, 0x00, 0x00, 0x40, 0x56, 0x51, 0x4C, 0x47, 0x00, 0x00, 0x00, 0x5A, 0x55, 0x50, 0x4B,
							0x46, 0x43, 0x00, 0x00, 0x59, 0x54, 0x4F, 0x4A, 0x45, 0x42, 0x58, 0x00, 0x58, 0x53, 0x4E,
							0x49, 0x44, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void handle_input(uint8_t key) {
	if (console_drawn) {
		if (console_button->active && key == sk_Enter)
			close_console();
		return;
	}

	switch (current_context) {
		case CONTEXT_IO:
			if (key == sk_Down) {
				io_context[active_index]->active = false;

				current_context = CONTEXT_FUNCTION;
				active_index = 0;
				function_context[0]->active = true;

				draw_context(CONTEXT_IO);
				draw_context(CONTEXT_FUNCTION);
				draw_context(CONTEXT_SIMPLIFY);

			} else if (key == sk_Left && active_index == 1) {
				active_index = 0;
				io_context[1]->active = false;
				io_context[0]->active = true;
				draw_context(CONTEXT_IO);
			} else if (key == sk_Right && active_index == 0) {
				active_index = 1;
				io_context[0]->active = false;
				io_context[1]->active = true;
				draw_context(CONTEXT_IO);
			} else if (key == sk_Enter) {
				view_t *v;
				v = context_lookup[current_context][active_index];
				v->index = (v->index + 1) % NUM_DROPDOWN_ENTRIES;
				draw_context(current_context);
			} else if (key == sk_Up) {
				view_t *v;
				v = context_lookup[current_context][active_index];
				if (v->index == 0)
					v->index = NUM_DROPDOWN_ENTRIES - 1;
				else
					v->index--;
				draw_context(current_context);
			}

			break;
		case CONTEXT_FUNCTION:
			if ((key == sk_Right || key == sk_Enter) && elements_in_context[CONTEXT_SIMPLIFY + active_index] > 0) {
				function_index = active_index;

				current_context = (Context)(CONTEXT_SIMPLIFY + active_index);
				function_context[active_index]->active = false;
				active_index = 0;
				context_lookup[current_context][active_index]->active = true;

				draw_context(CONTEXT_FUNCTION);
				draw_context(current_context);

				break;
			}
		default: {
			view_t *v;
			v = context_lookup[current_context][active_index];

			if (key == sk_Up) {
				if (active_index == 0) {
					context_lookup[current_context][active_index]->active = false;
					active_index = 0;
					io_context[active_index]->active = true;
					draw_context(current_context);
					current_context = CONTEXT_IO;
					draw_context(CONTEXT_IO);
					break;
				}

				context_lookup[current_context][active_index]->active = false;
				active_index--;
				context_lookup[current_context][active_index]->active = true;
				draw_context(current_context);

				if (current_context == CONTEXT_FUNCTION)
					draw_context((Context)(CONTEXT_SIMPLIFY + active_index));
			} else if (key == sk_Down) {
				if (active_index == elements_in_context[current_context] - 1)
					break;
				context_lookup[current_context][active_index]->active = false;
				active_index++;
				context_lookup[current_context][active_index]->active = true;
				draw_context(current_context);

				if (current_context == CONTEXT_FUNCTION)
					draw_context((Context)(CONTEXT_SIMPLIFY + active_index));
			} else if (key == sk_Left && current_context != CONTEXT_FUNCTION) {
				context_lookup[current_context][active_index]->active = false;
				draw_context(current_context);
				active_index = function_index;
				current_context = CONTEXT_FUNCTION;
				context_lookup[current_context][active_index]->active = true;
				draw_context(CONTEXT_FUNCTION);
			} else if (key == sk_Enter) {
				switch (v->type) {
					case GUI_CHECKBOX:
						v->checked = !v->checked;
						draw_context(current_context);
						break;
					case GUI_DROPDOWN:
						v->index = (v->index + 1) % NUM_DROPDOWN_ENTRIES;
						draw_context(current_context);
						break;
					case GUI_BUTTON:
						if (v == button_simplify)
							execute_simplify();
						else if (v == button_evaluate)
							execute_evaluate();
						else if (v == button_expand)
							execute_expand();
						else if (v == button_derivative)
							execute_derivative();
						else if (v == button_integral)
							execute_integral();
						else if (v == button_de)
							execute_de();
						break;
					default: break;
				}

			} else {
				if (v->type == GUI_CHARSELECT) {
					char val;
					val = alpha_table[key];

					if (val != 0) {
						v->character = val;
						draw_context(current_context);
					}
				}
			}
		}

		break;
	}
}

void gui_Init(void) {
	io_context[0] = from_drop = view_create_dropdown(LCD_WIDTH / 4 + 20 - 25, 14 + 8 + 4, 0);
	io_context[1] = to_drop = view_create_dropdown(LCD_WIDTH / 4 * 3 - 20 - 25, 14 + 8 + 4, 1);

	function_context[0] = view_create_label(26, 80, "Simplify");
	function_context[1] = view_create_label(26, 96, "Evaluate");
	function_context[2] = view_create_label(26, 112, "Expand");
	function_context[3] = view_create_label(26, 128, "Derivative");
	function_context[4] = view_create_label(26, 144, "Integral");
	function_context[5] = view_create_label(26, 160, "Solve DE");
	function_context[6] = view_create_label(26, 176, "About");

	simplify_context[0] = view_create_checkbox(124, 80 + 12 * 0, "Basic identities", true);
	simplify_context[1] = view_create_checkbox(124, 80 + 12 * 1, "Trig identities", true);
	simplify_context[2] = view_create_checkbox(124, 80 + 12 * 2, "Hyperbolic identities", true);
	simplify_context[3] = view_create_checkbox(124, 80 + 12 * 3, "Complex identities", true);
	simplify_context[4] = view_create_checkbox(124, 80 + 12 * 4, "Evaluate trig", true);
	simplify_context[5] = view_create_checkbox(124, 80 + 12 * 5, "Evaluate inverse trig", true);
	simplify_context[6] = button_simplify =
		view_create_button(10 + 2 + 100 + (LCD_WIDTH - 10 - 10 - 2 - 100) / 2, 184, "Simplify");

	evaluate_context[0] = view_create_checkbox(124, 80, "Evaluate constants", true);
	evaluate_context[1] = view_create_checkbox(124, 80 + 12, "Substitue expression:", false);
	evaluate_context[2] = view_create_dropdown(124 + 80, 96 + 12, 10);
	evaluate_context[3] = view_create_dropdown(124 + 80, 96 + 12 + 24, 11);
	evaluate_context[4] = button_evaluate =
		view_create_button(10 + 2 + 100 + (LCD_WIDTH - 10 - 10 - 2 - 100) / 2, 184, "Evaluate");

	expand_context[0] = view_create_checkbox(124, 80 + 12 * 0, "Expand multiplication", true);
	expand_context[1] = view_create_checkbox(124, 80 + 12 * 1, "Expand powers", true);
	expand_context[2] = button_expand =
		view_create_button(10 + 2 + 100 + (LCD_WIDTH - 10 - 10 - 2 - 100) / 2, 184, "Expand");

	derivative_context[0] = view_create_charselect(124 + 90, 80 - (16 - TEXT_HEIGHT) / 2);
	derivative_context[1] = view_create_checkbox(124, 104, "Show work", true);
	derivative_context[2] = button_derivative =
		view_create_button(10 + 2 + 100 + (LCD_WIDTH - 10 - 10 - 2 - 100) / 2, 184, "Differentiate");

	integral_context[0] = view_create_charselect(124 + 90, 80 - (16 - TEXT_HEIGHT) / 2);
	integral_context[1] = view_create_checkbox(124, 104, "Show work", true);
	integral_context[2] = button_integral =
		view_create_button(10 + 2 + 100 + (LCD_WIDTH - 10 - 10 - 2 - 100) / 2, 184, "Integrate");

	de_context[0] = view_create_charselect(124 + 90, 80 - (16 - TEXT_HEIGHT) / 2);
	de_context[1] = view_create_checkbox(124, 104, "Show work", true);
	de_context[2] = view_create_checkbox(124, 124, "Verify solution", false);
	de_context[3] = view_create_dropdown(124 + 90, 136, 2);
	de_context[4] = button_de = view_create_button(10 + 2 + 100 + (LCD_WIDTH - 10 - 10 - 2 - 100) / 2, 184, "Solve");

	console_button = view_create_button(LCD_WIDTH / 2, LCD_HEIGHT - LCD_HEIGHT / 6 - 20, "Close");

	current_context = CONTEXT_FUNCTION;
	active_index = 0;
	function_context[0]->active = true;
}

void gui_Cleanup(void) {
	unsigned i, j;

	for (i = 0; i < NUM_CONTEXTS; i++) {
		for (j = 0; j < elements_in_context[i]; j++) {
			free(context_lookup[i][j]);
		}
	}

	free(console_button);

	id_UnloadAll();
}

void gui_Run(void) {
	gui_Init();

	os_ClrHome();
	gfx_Begin();

	draw_background();
	draw_context(CONTEXT_IO);
	draw_context(CONTEXT_FUNCTION);
	draw_context(CONTEXT_SIMPLIFY);

	while (true) {
		uint8_t key;
		key = os_GetCSC();

		if (key == sk_Clear)
			break;

		handle_input(key);
	}

	gfx_End();

	gui_Cleanup();
}

void compile_general(void) {
	static bool compiled = false;

	if (!compiled) {
		console_write("Compiling basic ids...");
		id_LoadTable(id_general);
		compiled = true;
	}
}

void compile_trig(void) {
	static bool compiled = false;

	if (!compiled) {
		console_write("Compiling trig ids...");
		id_LoadTable(id_trig_identities);
		compiled = true;
	}
}

void compile_trig_constants(void) {
	static bool compiled = false;

	if (!compiled) {
		console_write("Compiling trig const ids...");
		id_LoadTable(id_trig_constants);
		compiled = true;
	}
}

void compile_trig_inv_constants(void) {
	static bool compiled = false;

	if (!compiled) {
		console_write("Compiling inv trig const ids...");
		id_LoadTable(id_trig_inv_constants);
		compiled = true;
	}
}

void compile_hyperbolic(void) {
	static bool compiled = false;

	if (!compiled) {
		console_write("Compiling hyperbolic ids...");
		id_LoadTable(id_hyperbolic);
		compiled = true;
	}
}

void compile_complex(void) {
	static bool compiled = false;

	if (!compiled) {
		console_write("Compiling complex ids...");
		id_LoadTable(id_complex);
		compiled = true;
	}
}

void compile_derivative(void) {
	static bool compiled = false;

	if (!compiled) {
		console_write("Compiling derivative ids...");
		id_LoadTable(id_derivative);
		compiled = true;
	}
}

void compile_all(void) {
	compile_general();
	compile_trig();
	compile_trig_constants();
	compile_hyperbolic();
	compile_complex();
}

static const char *const token_table[21] = {OS_VAR_Y1,   OS_VAR_Y2,   OS_VAR_Y3,   OS_VAR_Y4,   OS_VAR_Y5,
											OS_VAR_Y6,   OS_VAR_Y7,   OS_VAR_Y8,   OS_VAR_Y9,   OS_VAR_Y0,
											OS_VAR_STR1, OS_VAR_STR2, OS_VAR_STR3, OS_VAR_STR4, OS_VAR_STR5,
											OS_VAR_STR6, OS_VAR_STR7, OS_VAR_STR8, OS_VAR_STR9, OS_VAR_STR0,
											OS_VAR_ANS};

pcas_ast_t *parse_from_dropdown_index(unsigned index, pcas_error_t *err) {
	return parse_from_tok(token_table[index], err);
}

void write_to_dropdown_index(unsigned index, pcas_ast_t *expression, pcas_error_t *err) {
	write_to_tok(token_table[index], expression, err);
}

void execute_simplify(void) {
	char buffer[50];

	pcas_ast_t *expression;
	pcas_error_t err;

	unsigned short flags = SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | SIMP_EVAL | SIMP_DERIV | SIMP_LIKE_TERMS;

	if (simplify_context[0]->checked) {
		compile_general();
		flags |= SIMP_ID_GENERAL;
	}
	if (simplify_context[1]->checked) {
		compile_trig();
		flags |= SIMP_ID_TRIG;
	}
	if (simplify_context[2]->checked) {
		compile_hyperbolic();
		flags |= SIMP_ID_HYPERBOLIC;
	}
	if (simplify_context[3]->checked) {
		compile_complex();
		flags |= SIMP_ID_COMPLEX;
	}
	if (simplify_context[4]->checked) {
		compile_trig_constants();
		flags |= SIMP_ID_TRIG_CONSTANTS;
	}
	if (simplify_context[5]->checked) {
		compile_trig_inv_constants();
		flags |= SIMP_ID_TRIG_INV_CONSTANTS;
	}

	console_write("Parsing input...");

	expression = parse_from_dropdown_index(from_drop->index, &err);

	if (err == E_SUCCESS) {
		if (expression != NULL) {
			console_write("Simplifying...");

			simplify(expression, flags);
			simplify_canonical_form(expression, CANONICAL_ALL);

			console_write("Exporting...");

			write_to_dropdown_index(to_drop->index, expression, &err);

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
		if (from_drop->index == 20)
			console_write("Make sure Ans is a string.");
	}

	console_button->active = true;
	view_draw(console_button);
}

void execute_evaluate(void) {
	char buffer[50];

	bool should_sub, should_eval;
	pcas_ast_t *expression;
	pcas_error_t err;

	should_eval = evaluate_context[0]->checked;
	should_sub = evaluate_context[1]->checked;

	console_write("Parsing input...");

	expression = parse_from_dropdown_index(from_drop->index, &err);

	if (err == E_SUCCESS) {
		if (expression != NULL) {
			simplify(expression, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL);

			if (should_sub) {
				pcas_ast_t *sub_from, *sub_to;
				pcas_error_t err;

				console_write("Parsing sub from...");
				sub_from = parse_from_dropdown_index(evaluate_context[2]->index, &err);
				simplify(sub_from, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL);

				if (err == E_SUCCESS) {
					if (sub_from != NULL) {
						console_write("Parsing sub to...");
						sub_to = parse_from_dropdown_index(evaluate_context[3]->index, &err);
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
					if (evaluate_context[3]->index == 20)
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

			write_to_dropdown_index(to_drop->index, expression, &err);

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
		if (from_drop->index == 20)
			console_write("Make sure Ans is a string.");
	}

	console_button->active = true;
	view_draw(console_button);
}

void execute_expand(void) {
	char buffer[50];

	pcas_ast_t *expression;
	pcas_error_t err;

	unsigned short flags = 0;

	if (expand_context[0]->checked) {
		flags |= EXP_DISTRIB_NUMBERS | EXP_DISTRIB_MULTIPLICATION | EXP_DISTRIB_ADDITION;
	}
	if (expand_context[1]->checked) {
		flags |= EXP_EXPAND_POWERS;
	}

	console_write("Parsing input...");

	expression = parse_from_dropdown_index(from_drop->index, &err);

	if (err == E_SUCCESS) {
		if (expression != NULL) {
			console_write("Expanding...");

			simplify(expression, SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL);
			expand(expression, flags);
			simplify(
				expression,
				SIMP_NORMALIZE | SIMP_COMMUTATIVE | SIMP_RATIONAL | (expand_context[0]->checked ? SIMP_LIKE_TERMS : 0) |
					SIMP_EVAL
			);
			simplify_canonical_form(expression, CANONICAL_ALL ^ CANONICAL_COMBINE_POWERS);

			console_write("Exporting...");

			write_to_dropdown_index(to_drop->index, expression, &err);

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
		if (from_drop->index == 20)
			console_write("Make sure Ans is a string.");
	}

	console_button->active = true;
	view_draw(console_button);
}

pcas_ast_t *parse_respect_to(view_t *charselect, pcas_error_t *err) {
	char *theta = "theta";

	/*We treat the @ character as theta partially out of laziness*/
	if (charselect->character == '@')
		return parse((uint8_t *)theta, strlen(theta), str_table, err);

	return parse((uint8_t *)&charselect->character, 1, str_table, err);
}

unsigned parse_list_from_dropdown_index(unsigned index, pcas_ast_t **items, pcas_error_t *err) {
	return parse_list_from_tok(token_table[index], items, MAX_ITEMS, err);
}

/*Runs a calculus function on the input with the options in context, then shows the work or the result*/
void execute_calculus(Calculus kind, view_t **context, const char *title) {
	char buffer[50];

	pcas_ast_t *items[MAX_ITEMS], *respect_to, *solution = NULL;
	pcas_error_t err;
	pcas_work_t work;
	unsigned count;
	bool show_work = context[1]->checked;
	bool verify = kind == CALCULUS_DE && context[2]->checked;
	bool satisfied = false;

	compile_derivative();

	console_write("Parsing input...");

	count = parse_list_from_dropdown_index(from_drop->index, items, &err);

	if (err == E_SUCCESS && verify) {
		solution = parse_from_dropdown_index(context[3]->index, &err);
		if (err == E_SUCCESS && solution == NULL)
			err = E_GENERIC;
	}

	if (err == E_SUCCESS && (count == 0 || items[0] == NULL)) {
		console_write("Failed. Empty input.");
	} else if (err != E_SUCCESS) {
		sprintf(buffer, "Failed. %s.", error_text[err]);
		console_write(buffer);
		if (from_drop->index == 20)
			console_write("Make sure Ans is a string.");
	} else {
		respect_to = parse_respect_to(context[0], &err);

		if (show_work)
			work_Start(&work);

		if (verify) {
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
			err = calculus_Run(kind, items, count, respect_to, buffer);

			if (err == E_SUCCESS && kind == CALCULUS_DE)
				console_write(buffer);
		}

		if (show_work)
			work_Stop();

		if (err == E_SUCCESS && !verify) {
			console_write("Exporting...");
			write_to_dropdown_index(to_drop->index, items[0], &err);
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

	console_button->active = true;
	view_draw(console_button);
}

void execute_derivative(void) {
	execute_calculus(CALCULUS_DERIVATIVE, derivative_context, "Derivative");
}

void execute_integral(void) {
	execute_calculus(CALCULUS_INTEGRAL, integral_context, "Integral");
}

void execute_de(void) {
	execute_calculus(CALCULUS_DE, de_context, "Differential equation");
}

#else
typedef int make_iso_compilers_happy;
#endif
