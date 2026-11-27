/*
DRAW_STRING_PARSE_TEST.C

(1.1.3 8v audit) The game's own text parser for wide strings
(source/text/draw_string.c parse_unicode_string, every text box's) on
strings that end in '|', the text's formatting mark: built with AddressSanitizer
by run_draw_string_parse_test.sh, each string on the heap of exactly its size,
so a read past its terminator is a failure. The file is the game's own,
included whole; nothing else of it runs.

"x|" before the audit's fix read past the end ("Snowtrap |", a Workshop map's
title, and a host's map or game name); "|n" is still a line break, "|x" and
"||" what they were.
*/

#include "../../../source/text/draw_string.c"

#include <stdio.h>
#include <stdlib.h>

/* (the C library's own, not the game's stand-ins for them, which are not linked: cseries.h, the port's stdio.h) */
#undef printf
#undef malloc
#undef free
#undef memset
#undef strcmp
#undef strlen
#undef memcpy

static int failures, checks;

static void check(int ok, char const *what)
{
	checks++;
	if (!ok)
		failures++;
	printf("%s %s\n", ok ? "PASS" : "FAIL", what);
}

/* parses `text` (ASCII, made wide on the heap, exactly its size) as the game does, until the parser says
the end: the results in order, a letter each ('c' a character, 'n' a line's end, 't' a column's, 'e' the
end, '?' another); a read past the terminator is AddressSanitizer's heap-buffer-overflow. TRUE if it ended
at the terminator */
static int parse(char const *text, char *results, int results_size)
{
	long length = (long)strlen(text), index;
	wchar_t *wide = malloc((size_t)(length + 1) * sizeof(wchar_t));
	struct parse_string_state state;
	int count = 0;

	for (index = 0; index <= length; index++)
		wide[index] = (wchar_t)(unsigned char)text[index];
	memset(&state, 0, sizeof(state));
	state.string = (byte *)wide;
	state.result = _parsed_character;
	while (count < results_size - 1)
	{
		short result;

		result = (short)parse_unicode_string(&state);
		results[count++] = result == _parsed_character ? 'c' : result == _parsed_end_of_line ? 'n' :
			result == _parsed_end_of_column ? 't' : result == _parsed_end_of_string ? 'e' : '?';
		if (result == _parsed_end_of_string)
			break;
	}
	results[count] = 0;
	free(wide);
	return count > 0 && results[count - 1] == 'e' && state.string_index == length + 1;
}

int main(void)
{
	char results[64];

	check(parse("ab", results, sizeof(results)) && !strcmp(results, "cce"), "\"ab\": two characters, the end");
	check(parse("Snowtrap |", results, sizeof(results)) && !strcmp(results, "cccccccccce"),
		"\"Snowtrap |\": the trailing '|' takes nothing past the end");
	printf("  (%s)\n", results);
	check(parse("|", results, sizeof(results)), "\"|\" alone: nothing past the end");
	check(parse("a|nb", results, sizeof(results)) && !strcmp(results, "cnce"),
		"\"a|nb\": '|n' still ends the line");
	printf("  (%s)\n", results);
	check(parse("||", results, sizeof(results)) && !strcmp(results, "ce"), "\"||\": the mark and its letter, the end");
	printf("  (%s)\n", results);
	check(parse("x||", results, sizeof(results)), "\"x||\": nothing past the end");
	check(parse("a|xb", results, sizeof(results)) && !strcmp(results, "ccce"), "\"a|xb\": '|x' as before (its letter taken)");
	printf("  (%s)\n", results);
	printf("-- %d of %d checks failed\n", failures, checks);
	return failures != 0;
}
