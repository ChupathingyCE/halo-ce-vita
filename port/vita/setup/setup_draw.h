/*
SETUP_DRAW.H

The setup screens' drawing (setup_draw.c): a 960x544 picture in memory,
filled with the Chupathingy Guide's look (port/linux/src/ui_overlay_style.c,
its Halo Blue palette), its words in the Chupathingy menus' faces
(port/assets/fonts) and the PlayStation's buttons in Kenney's Input Prompts
(port/linux/ui/fonts), rasterised with stb_truetype. Colors are 0xRRGGBBAA,
as the overlay's palette writes them.
*/

#ifndef WARTHOG_VITA_SETUP_DRAW_H
#define WARTHOG_VITA_SETUP_DRAW_H

#include "setup.h"

enum
{
	SETUP_FONT_TITLE,
	SETUP_FONT_TEXT,
	SETUP_FONT_PROMPTS,
	SETUP_FONT_COUNT
};

enum
{
	SETUP_ALIGN_LEFT,
	SETUP_ALIGN_CENTER,
	SETUP_ALIGN_RIGHT,
};

/* the Halo Blue palette's colors the screens use */
#define SETUP_COLOR_TEXT 0xFFFFFFFFu
#define SETUP_COLOR_TEXT_DIM 0x9AA9BBFFu
#define SETUP_COLOR_FAINT 0x7F9CBDFFu
#define SETUP_COLOR_LABEL 0x6FB6FFFFu
#define SETUP_COLOR_STATUS 0x8FC4FFFFu
#define SETUP_COLOR_TITLE 0x3D9BFFFFu
#define SETUP_COLOR_TITLE_GLOW 0x3D9BFFC0u
#define SETUP_COLOR_WARNING 0xFFCF6BFFu
#define SETUP_COLOR_GOOD 0x7FE39AFFu
#define SETUP_COLOR_BAD 0xFF8A80FFu
#define SETUP_COLOR_SELECTED_GLOW 0x9FD4FF8Cu
#define SETUP_COLOR_LINE 0x6EB4FF8Cu
#define SETUP_COLOR_SEPARATOR 0x6EB4FF5Au
#define SETUP_COLOR_PANEL_EDGE 0x2F7FD6FFu
#define SETUP_COLOR_GLASS_TOP 0x0C2A58F6u
#define SETUP_COLOR_GLASS_BOTTOM 0x061430FAu
#define SETUP_COLOR_PANE 0x03102AE0u
#define SETUP_COLOR_FIELD 0x02081A80u
#define SETUP_COLOR_PROGRESS_TOP 0x5AAEFFFFu
#define SETUP_COLOR_PROGRESS_BOTTOM 0x2F7FD6FFu

/* the PlayStation's face buttons and Start, drawn in their colors */
enum
{
	SETUP_GLYPH_CROSS,
	SETUP_GLYPH_CIRCLE,
	SETUP_GLYPH_SQUARE,
	SETUP_GLYPH_TRIANGLE,
	SETUP_GLYPH_START,
};

/* the picture started again: the backdrop and the corner's words; 0 if
there is no memory for it */
int setup_draw_begin(const char *version);
const unsigned char *setup_draw_pixels(void);
void setup_draw_rectangle(int x, int y, int width, int height, unsigned int color);
void setup_draw_gradient(int x, int y, int width, int height, unsigned int top, unsigned int bottom);
/* a panel: the Guide's glass with its edge */
void setup_draw_panel(int x, int y, int width, int height);
/* a selected row's glow */
void setup_draw_selection(int x, int y, int width, int height);
/* text on a baseline at x (aligned); its width */
float setup_draw_text(int font, float size, float x, float baseline, int align, unsigned int color, const char *text);
float setup_text_width(int font, float size, const char *text);
/* the title, with its glow */
void setup_draw_title(float x, float baseline, int align, const char *text);
/* a button and what it does, from x on the baseline; its width */
float setup_draw_button(float x, float baseline, int glyph, const char *label);
float setup_button_width(int glyph, const char *label);
/* a bar from 0 to 1 */
void setup_draw_progress(int x, int y, int width, int height, float fraction);
/* text broken into lines of at most width; the baseline after the last */
float setup_draw_wrapped(int font, float size, float x, float baseline, float width, float line_height,
	unsigned int color, const char *text);
/* everything given back */
void setup_draw_release(void);

#endif
