/*
MIXED_COOP_TABLES_TEST.C

Mixed play's built-in tables (port/linux/game/mixed_coop_tables.h): each of
the 23 levels has one; each inflates, reads through the checked readers a
host's streams go through (mixed_coop_dictionary_read, mixed_coop_layout_read)
for its own level, and carries no netgame flag place; Blood Gulch, Battle
Creek and Sidewinder have a layout table whose placements have no place and
whose sources name one placement each; a level past the 23 has none. Built
by run_mixed_coop_tables_test.sh (ASan and UBSan).
*/

#include "../../linux/game/mixed_coop_tables.h"
#include "../../linux/game/mixed_coop_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int ok, char const *what, int level)
{
	if (!ok)
	{
		printf("FAIL level %d: %s\n", level, what);
		failures++;
	}
}

int main(void)
{
	int level;

	for (level = 0; level < MIXED_COOP_LEVELS; level++)
	{
		uint8_t *bytes = NULL;
		uint32_t length = 0, flag;
		struct mixed_coop_dictionary dictionary;
		char why[96];

		check(mixed_coop_table_present(level), "no table", level);
		if (!mixed_coop_table_dictionary(level, &bytes, &length))
		{
			check(0, "the dictionary does not inflate", level);
			continue;
		}
		check(mixed_coop_dictionary_read(bytes, length, &dictionary, why, sizeof(why)), "the dictionary does not read", level);
		check(dictionary.level == level, "the dictionary names another level", level);
		check(dictionary.family == _mixed_coop_family_xbox, "the dictionary is not the Xbox's", level);
		check(dictionary.tag_count > 1000, "too few tags", level);
		for (flag = 0; flag < dictionary.netgame_flag_count; flag++)
		{
			struct mixed_coop_netgame_flag place;

			mixed_coop_dictionary_netgame_flag_get(&dictionary, flag, &place);
			check(!place.position[0] && !place.position[1] && !place.position[2] && !place.facing, "a flag's place in it", level);
		}
		/* (one byte changed anywhere: refused by its check) */
		bytes[length / 2] ^= 0x40;
		check(!mixed_coop_dictionary_read(bytes, length, &dictionary, why, sizeof(why)), "a damaged copy read", level);
		mixed_coop_table_free(bytes);
		bytes = NULL;
		if (mixed_coop_table_layout(level, &bytes, &length))
		{
			struct mixed_coop_layout layout;
			uint32_t skeleton = length >= 8 ? mixed_coop_read_u32(bytes + 4) : 0, placements, index;
			int kind;

			check(length >= 8 && mixed_coop_read_u32(bytes) == MIXED_COOP_TABLE_LAYOUT_MAGIC, "the layout table's magic", level);
			check(skeleton <= length - 8 && mixed_coop_layout_read(bytes + 8, skeleton, &layout, why, sizeof(why)),
				"the layout does not read", level);
			check(layout.level == level, "the layout names another level", level);
			check(layout.blocks[MIXED_COOP_LAYOUT_NETGAME_FLAGS].count == 0, "netgame flags in the layout", level);
			placements = layout.blocks[MIXED_COOP_LAYOUT_SCENERY].count + layout.blocks[MIXED_COOP_LAYOUT_VEHICLES].count;
			check((length - 8 - skeleton) == 4 * placements, "a source for each placement", level);
			for (kind = MIXED_COOP_LAYOUT_SCENERY; kind <= MIXED_COOP_LAYOUT_VEHICLES; kind += 2)
			{
				for (index = 0; index < layout.blocks[kind].count; index++)
				{
					uint8_t const *element = layout.blocks[kind].elements + index * layout.blocks[kind].element_bytes;
					int at, zero = 1;

					for (at = 8; at < 32; at++)
						zero &= element[at] == 0;
					check(zero, "a placement's place in the table", level);
				}
			}
			for (index = 0; index < placements; index++)
				check(bytes[8 + skeleton + 4 * index + 2] <= _mixed_coop_table_source_stand_in, "a source of no kind", level);
			mixed_coop_table_free(bytes);
		}
		else
			check(level != 20 && level != 21 && level != 22, "no layout table", level);
	}
	check(!mixed_coop_table_present(MIXED_COOP_LEVELS) && !mixed_coop_table_present(-1), "a table past the levels", -1);
	printf("%s mixed_coop_tables: %d levels\n", failures ? "FAIL" : "PASS", MIXED_COOP_LEVELS);
	return failures != 0;
}
