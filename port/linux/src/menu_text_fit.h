/*
MENU_TEXT_FIT.H

A text box's text fitted to the room it has. The menus' text boxes break
their text at spaces, and a word wider than the box runs past its edge and
is clipped there: a map list card showed "TRUTH AND RECONCILIATIO" and
"coldsnap_20th_" (the names the port adds: an MCC level's, a Custom
Edition file's). Here such a word is cut where it stops fitting, with "..."
after it, and lines the box has no room for are left out, "..." after the
last, measured in the box's own font (ui_widget.c measures; the names come
from port/linux/game/ui_map_list.c and menu_functions.c).

No game types, so that the build machine can test it.

ChupathingyCE's (147f4155a, GPL-3.0-or-later). In this port: the strings of
the port's own the game's text boxes draw from its string lists (Infection's
description, a Custom Edition map's name: text_group.c
unicode_string_list_string_is_port), fitted as ui_widget.c draws them
(ui_widget_port_text_box_fit); the game's text boxes here do not break a
line themselves, so the line breaks made here are the only ones.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef MENU_TEXT_FIT_H
#define MENU_TEXT_FIT_H

#include <stddef.h>

enum
{
	/* the most characters a text fitted here has, with its end */
	MENU_TEXT_FIT_LENGTH = 256,
};

/* the width of the first `length` characters of `text` drawn in the box's
font (its cursor's end or its ink's right edge, whichever is further) */
typedef long (*menu_text_measure)(void *context, const wchar_t *text, long length);

/* `text` (a buffer of `size` characters) fitted in place to lines less than
`width` wide, as the game's text boxes break them: at spaces, with the
text's own line breaks ("\r\n", or '\r') kept. A word wider than a line is
cut where it stops fitting with "..." after it, and the rest of its
paragraph left out. When `lines` is more than 0, lines past that many are
left out and the last kept ends with "...". Runs of spaces become one, and
a line's leading spaces go. Returns 1 when the text changed. */
int menu_text_fit(wchar_t *text, long size, long width, long lines, menu_text_measure measure, void *context);

#endif
