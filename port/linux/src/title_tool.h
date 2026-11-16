/*
TITLE_TOOL.H

The title tool: Halo CE's heading lettering rebuilt on the player's machine
from the player's own ui.map, and words set in it (title_tool_set): Menu
style Classic+'s headings for the port's own screens
(port/linux/game/menu_titles.c), in place of the port's drawn ones.

The maps' headings are pictures of text set in a commercial typeface, which
the project cannot ship, as a font or as pictures drawn with one. The tool
ships neither: it reads the heading pictures the caller decodes from the
player's ui.map, rebuilds each letter from every place it appears in them
(the same letter at many sub-texel positions, put together), and sets
words with those letters at the headings' own size, over a plate made of
the headings' own. What it makes stays in memory, or in the player's own
data folder (the caller's).

Adapted from ChupathingyCE's title tool (31024b132, GPL-3.0-or-later):
see title_tool.c. No SDL or game dependency.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-FileCopyrightText: 2026 Halo CE for PS Vita contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef TITLE_TOOL_H
#define TITLE_TOOL_H

/* a title the tool reads letters from: its bitmap (its bitmap group tag's
name, its index there, the CRC-32 of its first mip level's pixels: the
pixels its letters were measured on), its text, and where each of its
letters (the text's characters other than spaces) starts, in the bitmap's
texels */
struct title_tool_title
{
	const char *tag;
	int bitmap;
	unsigned long crc;
	const char *text;
	const float *lefts;
};

struct title_tool;

/* DXT3 (15) or DXT5 (16) blocks as 8-bit RGBA, rows top first (malloc'd);
NULL if it cannot be */
unsigned char *title_tool_dxt_decode(const unsigned char *blocks, int width, int height, int format);
/* the tool, from each title's picture (texels[index]: 8-bit RGBA of
widths[index] by heights[index], or NULL for one not had; the tool takes
them all, and sets each NULL); NULL if none can be read */
struct title_tool *title_tool_new(const struct title_tool_title *titles, long count, unsigned char **texels,
	const int *widths, const int *heights);
/* whether title index's picture was read */
int title_tool_has(const struct title_tool *tool, long index);
/* title index's picture as it was given (malloc'd), NULL if not had */
unsigned char *title_tool_copy(const struct title_tool *tool, long index);
/* text (capitals, digits, ':' and spaces) set in the titles' letters, its
first letter starting at left, on the titles' baseline, over the plate of
the title of that size nearest its width: 8-bit RGBA, width by height
(malloc'd); NULL if a letter has no shape, the word does not fit, or no
title is of that size */
unsigned char *title_tool_set(const struct title_tool *tool, const char *text, int width, int height, float left);
/* whether title_tool_set can set text (every letter has a shape, and it
fits width): FALSE is lasting while the pictures are the same, unlike a
NULL from title_tool_set for want of memory */
int title_tool_can_set(const struct title_tool *tool, const char *text, int width, float left);
void title_tool_free(struct title_tool *tool);
/* a picture the tool gave (title_tool_dxt_decode, _copy, _set) let go:
the tool's own allocator's, which the caller's may not be */
void title_tool_picture_free(unsigned char *picture);

#endif
