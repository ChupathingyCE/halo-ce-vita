/*
MENU_TEXT_FIT_TEST.C

Tests of the menus' text fit (port/linux/src/menu_text_fit.c): a map list
card's name cut with "..." where a word is wider than the card, never
clipped mid-word. Built and run by run_menu_text_fit_test.sh, with the C
library's wchar_t and with the game's 16-bit one (-fshort-wchar: the text's
own loops, no wcs* call); exits nonzero on a failure.

ChupathingyCE's (147f4155a, GPL-3.0-or-later); this port: its wide strings
compared, copied and counted by its own loops, so it runs with either size.
*/

#include "../../linux/src/menu_text_fit.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* a font of 10 units a letter, capitals 16 wide (as the menus' are wider),
spaces and dots 5 */
static long measure(void *context, const wchar_t *text, long length)
{
	long width = 0, index;

	(void)context;
	for (index = 0; index < length; index++)
	{
		wchar_t c = text[index];

		width += c == L' ' || c == L'.' ? 5 : c >= L'A' && c <= L'Z' ? 16 : 10;
	}
	return width;
}

/* (this port) the wide strings' own loops, for either size of wchar_t */
static long text_length(const wchar_t *text)
{
	long length = 0;

	while (text[length])
		length++;
	return length;
}

static int text_compare(const wchar_t *a, const wchar_t *b)
{
	long at;

	for (at = 0; a[at] || b[at]; at++)
	{
		if (a[at] != b[at])
			return a[at] < b[at] ? -1 : 1;
	}
	return 0;
}

static void text_copy(wchar_t *to, const wchar_t *from, long count)
{
	long at;

	for (at = 0; at < count && from[at]; at++)
		to[at] = from[at];
	for (; at < count; at++)
		to[at] = 0;
}

static void text_print(const char *label, const wchar_t *text)
{
	long at;

	printf("  %s \"", label);
	for (at = 0; text[at]; at++)
		putchar(text[at] < 0x80 ? (int)text[at] : '?');
	printf("\"\n");
}

static int same(const wchar_t *text, const wchar_t *expected)
{
	if (!text_compare(text, expected))
		return 1;
	text_print("got", text);
	text_print("not", expected);
	return 0;
}

static wchar_t text[MENU_TEXT_FIT_LENGTH];

static int fit(const wchar_t *in, long width, long lines)
{
	text_copy(text, in, MENU_TEXT_FIT_LENGTH - 1);
	text[MENU_TEXT_FIT_LENGTH - 1] = 0;
	return menu_text_fit(text, MENU_TEXT_FIT_LENGTH, width, lines, measure, NULL);
}

int main(void)
{
	/* a name that fits is left alone */
	CHECK(!fit(L"Blood Gulch", 200, 2) && same(text, L"Blood Gulch"));
	CHECK(!fit(L"Boarding\r\nAction", 200, 2) && same(text, L"Boarding\r\nAction"));
	/* the card's own wrap at spaces, where the box breaks lines too */
	CHECK(fit(L"PILLAR OF AUTUMN [MCC]", 170, 2) && same(text, L"PILLAR OF\r\nAUTUMN [MCC]"));
	/* a word wider than the card is cut with "...", the rest of its line
	left out, as the smoke test's "TRUTH AND RECONCILIATIO" (224 wide) was
	clipped at a card 190 wide */
	CHECK(fit(L"TRUTH AND\r\nRECONCILIATION", 190, 2) && same(text, L"TRUTH AND\r\nRECONCILIA..."));
	CHECK(fit(L"TRUTH AND RECONCILIATION [MCC]", 190, 2) && same(text, L"TRUTH AND\r\nRECONCILIA..."));
	/* a Custom Edition file's name over its mark */
	CHECK(fit(L"coldsnap_20th_xl_rc5\r\n[CE]", 150, 2) && same(text, L"coldsnap_20th...\r\n[CE]"));
	/* lines past the box's room go, "..." after the last; a break with
	nothing after it goes quietly */
	CHECK(fit(L"PILLAR OF AUTUMN [MCC]", 170, 1) && same(text, L"PILLAR OF..."));
	CHECK(fit(L"Boarding\r\nAction\r\n", 200, 2) && same(text, L"Boarding\r\nAction"));
	CHECK(fit(L"one\r\ntwo\r\nthree", 200, 2) && same(text, L"one\r\ntwo..."));
	/* no limit on lines: a wide word cut, the rest of its paragraph left
	out, the next paragraph kept */
	CHECK(fit(L"a bb ccccccccccc dd", 60, 0) && same(text, L"a bb\r\ncccc..."));
	CHECK(fit(L"a bb ccccccccccc dd\r\nee", 60, 0) && same(text, L"a bb\r\ncccc...\r\nee"));
	/* runs of spaces and a line's leading spaces go */
	CHECK(fit(L"  Blood   Gulch ", 200, 2) && same(text, L"Blood Gulch"));
	/* a lone '\r' break is kept, "\r\n" kept whole */
	CHECK(!fit(L"Blood\rGulch", 200, 2) && same(text, L"Blood\rGulch"));
	/* nothing fits a width of 0 or a missing measure: left alone */
	CHECK(!fit(L"Blood Gulch", 0, 2) && same(text, L"Blood Gulch"));
	{
		wchar_t copy[8] = L"abcdefg";

		CHECK(!menu_text_fit(copy, 8, 200, 2, NULL, NULL) && !text_compare(copy, L"abcdefg"));
		/* a buffer with no room for "...": the word kept as far as it fits */
		CHECK(menu_text_fit(copy, 8, 60, 1, measure, NULL) && text_length(copy) < 8);
	}
	/* a long word with nothing of it fitting beside "...": "..." alone */
	CHECK(fit(L"WWWWWWWW", 20, 1) && same(text, L"..."));
	if (failures)
	{
		printf("%d failures\n", failures);
		return 1;
	}
	printf("menu_text_fit: every check passed\n");
	return 0;
}
