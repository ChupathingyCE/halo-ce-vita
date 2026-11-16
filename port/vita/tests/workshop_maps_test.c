/*
WORKSHOP_MAPS_TEST.C

The desktop test of the Steam Workshop multiplayer maps' names and a joiner's
verdict (port/linux/game/workshop_maps.c), built by run_workshop_maps_test.sh
with AddressSanitizer and UBSan.
*/

#include "../../linux/game/workshop_maps.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok)
	{
		failures++;
		printf("FAIL %s\n", what);
	}
	else
		printf("PASS %s\n", what);
}

int main(void)
{
	char name[WORKSHOP_MAP_NAME_BYTES], level[WORKSHOP_MAP_LEVEL_NAME_BYTES], text[32];
	uint64_t id = 0;

	/* names made */
	check(workshop_map_name_make(2900000002ull, "fakegulch", name) && !strcmp(name, "ws2900000002_fakegulch"),
		"a short file name kept: ws2900000002_fakegulch");
	check(workshop_map_name_make(2900000002ull, "coldsnapextended", name) && !strcmp(name, "ws2900000002_coldsna-8866"),
		"a long file name: its first characters, '-' and four hex digits (ws2900000002_coldsna-8866)");
	check(strlen(name) == WORKSHOP_MAP_NAME_MAXIMUM, "... exactly 25 characters");
	check(workshop_map_name_make(999999999999ull, "abcdefghijklmnop", name) && strlen(name) <= WORKSHOP_MAP_NAME_MAXIMUM,
		"a 12-digit item's long name fits");
	check(!workshop_map_name_make(1000000000000ull, "a", name) && !name[0], "a 13-digit item is refused");
	check(!workshop_map_name_make(0, "a", name), "item 0 is refused");
	check(!workshop_map_name_make(5, "", name), "an empty file name is refused");
	check(!workshop_map_name_make(5, "Upper", name), "an upper-case file name is refused (the mods unit makes it lower)");
	check(!workshop_map_name_make(5, "a.b", name) && !workshop_map_name_make(5, "a\\b", name) &&
		!workshop_map_name_make(5, "../x", name), "a dot, a separator, a climb refused");
	check(!workshop_map_name_make(5, "abcdefghijklmnopqrstuvwx", name), "a file name of 24 characters is refused");
	/* names read */
	check(workshop_map_name_parse("ws2900000002_fakegulch", 32, &id) && id == 2900000002ull, "a name read: its item");
	check(workshop_map_name_parse("WS2900000002_FakeGulch", 32, &id) && id == 2900000002ull, "in any case");
	check(!workshop_map_name_parse("ws02900000002_a", 32, &id) && !id, "a leading zero: not one");
	check(!workshop_map_name_parse("ws1234567890123_a", 32, NULL), "13 digits: not one");
	check(!workshop_map_name_parse("ws123_", 32, NULL) && !workshop_map_name_parse("ws123", 32, NULL) &&
		!workshop_map_name_parse("ws_a", 32, NULL), "no file part, no item: not one");
	check(!workshop_map_name_parse("ws123_a.b", 32, NULL) && !workshop_map_name_parse("ws123_a\\b", 32, NULL),
		"a dot or a separator: not one");
	check(!workshop_map_name_parse("ws123_abcdefghijklmnopqrstuvwxyz", 64, NULL), "longer than 25: not one");
	check(!workshop_map_name_parse("ws123_ab", 8, NULL), "not ended within the size given: not one");
	check(!workshop_map_name_parse("bloodgulch", 32, NULL) && !workshop_map_name_parse("mcc_bloodgulch", 32, NULL),
		"the Xbox's and MCC's names: not ones");
	/* level names */
	check(workshop_map_level_name_parse("levels\\test\\ws2900000002_fakegulch\\ws2900000002_fakegulch", 0x80, name, &id) &&
		!strcmp(name, "ws2900000002_fakegulch") && id == 2900000002ull, "a host's level name read");
	check(workshop_map_level_name_parse("Levels\\Test\\WS7_A\\ws7_a", 0x80, name, &id) && !strcmp(name, "ws7_a") && id == 7,
		"in any case, the name given back in lower case");
	check(!workshop_map_level_name_parse("levels\\test\\ws7_a\\ws7_b", 0x80, name, &id) && !name[0] && !id,
		"two different parts: not one");
	check(!workshop_map_level_name_parse("ws7_a", 0x80, NULL, NULL), "a bare name: not a level name");
	check(!workshop_map_level_name_parse("mods\\ws7_a", 0x80, NULL, NULL) &&
		!workshop_map_level_name_parse("custom_maps\\ws7_a", 0x80, NULL, NULL), "a mods\\ or custom_maps\\ name: not one");
	memset(level, 'x', sizeof(level));
	memcpy(level, "levels\\test\\ws7_a\\ws7_a", 23);
	check(!workshop_map_level_name_parse(level, sizeof(level), NULL, NULL), "a record name not ended: not one");
	/* the verdict */
	check(workshop_map_join_verdict("levels\\test\\bloodgulch\\bloodgulch", 0x80, 5, 5, &id) ==
		WORKSHOP_MAP_JOIN_NOT_WORKSHOP && !id, "an Xbox level: not the Workshop's");
	check(workshop_map_join_verdict("levels\\test\\ws7_a\\ws7_a", 0x80, 5, 5, &id) == WORKSHOP_MAP_JOIN_PLAYABLE && id == 7,
		"the same copy: playable");
	check(workshop_map_join_verdict("levels\\test\\ws7_a\\ws7_a", 0x80, 5, 0, NULL) == WORKSHOP_MAP_JOIN_MISSING,
		"none here: missing (the download offered)");
	check(workshop_map_join_verdict("levels\\test\\ws7_a\\ws7_a", 0x80, 5, 6, NULL) == WORKSHOP_MAP_JOIN_DIFFERENT,
		"another copy: different (refused)");
	check(workshop_map_join_verdict("levels\\test\\ws7_a\\ws7_a", 0x80, 0, 6, NULL) == WORKSHOP_MAP_JOIN_UNIDENTIFIED &&
		workshop_map_join_verdict("levels\\test\\ws7_a\\ws7_a", 0x80, 0, 0, NULL) == WORKSHOP_MAP_JOIN_UNIDENTIFIED,
		"a host that does not say its copy: refused, never a download");
	/* ids */
	check(workshop_map_id_text(2900000002ull, text, sizeof(text)) && !strcmp(text, "2900000002"), "an id as text");
	check(!workshop_map_id_text(2900000002ull, text, 10), "... not in too small a room");
	check(workshop_map_id_parse("2900000002", &id) && id == 2900000002ull && !workshop_map_id_parse("0123", NULL) &&
		!workshop_map_id_parse("12a", NULL) && !workshop_map_id_parse("1234567890123", NULL) &&
		!workshop_map_id_parse("", NULL), "a mods folder's name read as an id, strictly");
	printf("-- %d of %d checks failed\n", failures, checks);
	return failures != 0;
}
