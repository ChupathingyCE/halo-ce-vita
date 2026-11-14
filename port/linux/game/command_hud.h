/*
COMMAND_HUD.H

Command mode's HUD drawing (command.c's command_draw: the Halo 3 menus' look -
their panel, strip and scoreboard band pictures from the menus' halo3 folder,
their pc\h3 fonts, their colours) for the other modes' HUDs (forge_modes.inc's
FIREFIGHT: the design, docs/superpowers/specs/2026-11-05-forge-warzone-firefight-design.md,
section 7). The places are the menus' units: the screen 480 high, its width
as wide as the view is (command_hud_frame_begin's).

Threads: the render's (a HUD's draw), as command_draw.
*/

#ifndef __COMMAND_HUD_H
#define __COMMAND_HUD_H

#include "cseries.h"

/* ---------- constants */

/* the pictures (command.c's command_art_files' order) */
enum
{
	COMMAND_HUD_ART_PANEL = 0,
	COMMAND_HUD_ART_ITEM_ON,
	COMMAND_HUD_ART_RULE,
	COMMAND_HUD_ART_STRIP,
	COMMAND_HUD_ART_BAND_RED,
	COMMAND_HUD_ART_BAND_BLUE,
};

/* the fonts */
enum
{
	COMMAND_HUD_FONT_ITEM = 0,
	COMMAND_HUD_FONT_SMALL,
	COMMAND_HUD_FONT_TITLE,
};

/* the menus' colours (the halo3 folder's XML), 0xAARRGGBB */
#define COMMAND_HUD_LIGHT 0xFFF2F6FAUL
#define COMMAND_HUD_ITEM 0xFFC5D0DDUL
#define COMMAND_HUD_DIM 0xFF9FB6CCUL
#define COMMAND_HUD_ORANGE 0xFFF0B070UL
#define COMMAND_HUD_GREY 0xFF66707CUL
#define COMMAND_HUD_RED 0xFFE06058UL
#define COMMAND_HUD_BLUE 0xFF6C8CF0UL

/* ---------- prototypes/COMMAND.C */

/* this frame's view and fonts (the render's camera); FALSE if there is
nothing to draw with. width: the view's width in the menus' units */
boolean command_hud_frame_begin(
	real *width);

/* a picture over a rectangle, alpha 0 to 1 */
void command_hud_draw_art(
	short art,
	real x0,
	real y0,
	real x1,
	real y1,
	real alpha);

/* a flat colour (0xAARRGGBB) */
void command_hud_draw_fill(
	real x0,
	real y0,
	real x1,
	real y1,
	unsigned long color);

/* words from x0 to x1, their line's top at y0: justification 0 left, 1
right, 2 the middle */
void command_hud_draw_text(
	short font,
	real x0,
	real y0,
	real x1,
	short justification,
	unsigned long color,
	char const *text);

/* a text's width as the font draws it */
real command_hud_measure_text(
	short font,
	char const *text);

#endif
