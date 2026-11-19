/*
LANG.H

The Vita side's text, in English. Halo CE Vita translated its own screens
(the settings panel, the start-up screens) with lang.c; Warthog's screens are
the game's, which the game translates itself, so the few the Vita side still
draws (the renderer's messages, the touch zones' names) stay English.
*/

#ifndef __VITA_LANG_H
#define __VITA_LANG_H

#define T(english) (english)
#define N_(english) english

static inline const char *lang_text(const char *english)
{
	return english;
}

/* characters (not bytes) in UTF-8 text */
static inline int lang_utf8_length(const char *text)
{
	int length = 0;

	for (; *text; text++)
		length += ((unsigned char)*text & 0xC0) != 0x80;
	return length;
}

#endif
