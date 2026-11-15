/*
STEAM_PLAN.C

steam_plan.h: the files Setup's Automatic (MCC) install takes, as a table.
Keep it in step with tools/installer/halo_ce_vita_installer.py (mcc_plan and
its constants): run_steam_download_test.sh compares the two.
*/

#include "steam_plan.h"

#include <stdio.h>
#include <string.h>

#define CE STEAM_GAME_HALO_CE
#define BANKS STEAM_GAME_HALO_CE_BANKS
#define D33 STEAM_DEPOT_HALO1_CAMPAIGN
#define D34 STEAM_DEPOT_HALO1_MULTIPLAYER

#define CE_MAP(stem, depot, kind, required) \
	{ CE, { depot, 0 }, "halo1/maps/" stem ".map", STEAM_ROOT_CE, "mcc/halo1/maps/" stem ".map", kind, required }
#define CE_SHARED(path, kind, required) \
	{ CE, { D33, D34 }, "halo1/" path, STEAM_ROOT_CE, "mcc/halo1/" path, kind, required }
#define CE_BANK(name) \
	{ BANKS, { D33, D34 }, "halo1/sound/pc/" name, STEAM_ROOT_CE, "mcc/halo1/sound/pc/" name, STEAM_KIND_BANK, 0 }

static const struct steam_plan_file plan[] = {
	/* Halo CE: MCC_CAMPAIGN_MAPS, bitmaps.map (required) */
	CE_MAP("a10", D33, STEAM_KIND_MCC_MAP, 1), CE_MAP("a30", D33, STEAM_KIND_MCC_MAP, 1),
	CE_MAP("a50", D33, STEAM_KIND_MCC_MAP, 1), CE_MAP("b30", D33, STEAM_KIND_MCC_MAP, 1),
	CE_MAP("b40", D33, STEAM_KIND_MCC_MAP, 1), CE_MAP("c10", D33, STEAM_KIND_MCC_MAP, 1),
	CE_MAP("c20", D33, STEAM_KIND_MCC_MAP, 1), CE_MAP("c40", D33, STEAM_KIND_MCC_MAP, 1),
	CE_MAP("d20", D33, STEAM_KIND_MCC_MAP, 1), CE_MAP("d40", D33, STEAM_KIND_MCC_MAP, 1),
	CE_SHARED("maps/bitmaps.map", STEAM_KIND_BITMAPS, 1),
	/* MCC_MULTIPLAYER_MAPS (each optional in Setup: left out if missing) */
	CE_MAP("beavercreek", D34, STEAM_KIND_MCC_MAP, 0), CE_MAP("sidewinder", D34, STEAM_KIND_MCC_MAP, 0),
	CE_MAP("damnation", D34, STEAM_KIND_MCC_MAP, 0), CE_MAP("ratrace", D34, STEAM_KIND_MCC_MAP, 0),
	CE_MAP("prisoner", D34, STEAM_KIND_MCC_MAP, 0), CE_MAP("hangemhigh", D34, STEAM_KIND_MCC_MAP, 0),
	CE_MAP("chillout", D34, STEAM_KIND_MCC_MAP, 0), CE_MAP("carousel", D34, STEAM_KIND_MCC_MAP, 0),
	CE_MAP("boardingaction", D34, STEAM_KIND_MCC_MAP, 0), CE_MAP("bloodgulch", D34, STEAM_KIND_MCC_MAP, 0),
	CE_MAP("wizard", D34, STEAM_KIND_MCC_MAP, 0), CE_MAP("putput", D34, STEAM_KIND_MCC_MAP, 0),
	CE_MAP("longest", D34, STEAM_KIND_MCC_MAP, 0), CE_MAP("dangercanyon", D34, STEAM_KIND_MCC_MAP, 0),
	CE_MAP("deathisland", D34, STEAM_KIND_MCC_MAP, 0), CE_MAP("gephyrophobia", D34, STEAM_KIND_MCC_MAP, 0),
	CE_MAP("icefields", D34, STEAM_KIND_MCC_MAP, 0), CE_MAP("infinity", D34, STEAM_KIND_MCC_MAP, 0),
	CE_MAP("timberland", D34, STEAM_KIND_MCC_MAP, 0),
	/* ui.map; custom_edition's PC_RESOURCE_MAPS; sound/pc's MCC_SOUND_BANK
	and MCC_SOUND_LISTS (optional) */
	CE_SHARED("maps/ui.map", STEAM_KIND_MCC_MAP, 0),
	CE_SHARED("maps/custom_edition/bitmaps.map", STEAM_KIND_RESOURCE, 0),
	CE_SHARED("maps/custom_edition/sounds.map", STEAM_KIND_RESOURCE, 0),
	CE_SHARED("maps/custom_edition/loc.map", STEAM_KIND_RESOURCE, 0),
	CE_SHARED("sound/pc/sounds_adpcm.fsb", STEAM_KIND_BANK, 0),
	CE_SHARED("sound/pc/lst/sounds_adpcm.lst.bin", STEAM_KIND_LIST, 0),
	CE_SHARED("sound/pc/lst/sounds_dialogs.lst.bin", STEAM_KIND_LIST, 0),
	CE_SHARED("sound/pc/lst/sounds_music.lst.bin", STEAM_KIND_LIST, 0),
	CE_SHARED("sound/pc/lst/sounds_stream.lst.bin", STEAM_KIND_LIST, 0),
	CE_SHARED("sound/pc/lst/sounds_debug.lst.bin", STEAM_KIND_LIST, 0),
	/* MCC_SOUND_CELT_BANKS (the player's choice: the dialogue, the music,
	the streams, Anniversary's effects; mcc_sound.c plays them as Xbox ADPCM) */
	CE_BANK("sounds_dialogs.fsb"), CE_BANK("sounds_music.fsb"), CE_BANK("sounds_stream.fsb"),
	CE_BANK("sounds_debug.fsb"),
};

static const struct
{
	uint32_t depot, games;
} depot_games[] = {
	{ D33, CE | BANKS }, { D34, CE },
};

const struct steam_plan_file *steam_plan_files(size_t *count)
{
	*count = sizeof(plan) / sizeof(plan[0]);
	return plan;
}

static uint32_t games_of_depot(uint32_t depot)
{
	size_t index;

	for (index = 0; index < sizeof(depot_games) / sizeof(depot_games[0]); index++)
		if (depot_games[index].depot == depot)
			return depot_games[index].games;
	return 0;
}

size_t steam_plan_depots(uint32_t games, uint32_t depots[4])
{
	size_t index, count = 0;

	for (index = 0; index < sizeof(depot_games) / sizeof(depot_games[0]); index++)
		if (depot_games[index].games & games)
			depots[count++] = depot_games[index].depot;
	return count;
}

uint32_t steam_plan_depot_of(uint32_t games, const struct steam_plan_file *file)
{
	unsigned int index;

	if (!(file->games & games))
		return 0;
	for (index = 0; index < 2; index++)
		if (file->depots[index] && (games_of_depot(file->depots[index]) & games))
			return file->depots[index];
	return 0;
}

int steam_plan_name_plain(const char *name)
{
	size_t length = strlen(name), index, part = 0;

	if (!length || length > 1024 || name[0] == '/')
		return 0;
	for (index = 0; index <= length; index++)
	{
		unsigned char c = (unsigned char)name[index];

		if (c == '/' || c == 0)
		{
			size_t part_length = index - part;

			if (!part_length || (part_length == 1 && name[part] == '.') ||
				(part_length == 2 && name[part] == '.' && name[part + 1] == '.'))
			{
				return 0;
			}
			part = index + 1;
			continue;
		}
		if (c < 0x20 || c == 0x7f || c == ':' || c == '\\')
			return 0;
	}
	return 1;
}

static int same_name(const char *a, const char *b)
{
	for (;; a++, b++)
	{
		char x = *a, y = *b;

		if (x >= 'A' && x <= 'Z')
			x = (char)(x - 'A' + 'a');
		if (y >= 'A' && y <= 'Z')
			y = (char)(y - 'A' + 'a');
		if (x != y)
			return 0;
		if (!x)
			return 1;
	}
}

const struct steam_plan_file *steam_plan_match(uint32_t games, uint32_t depot, const char *name)
{
	size_t index;

	if (!name || !steam_plan_name_plain(name))
		return NULL;
	for (index = 0; index < sizeof(plan) / sizeof(plan[0]); index++)
		if (same_name(plan[index].depot_path, name) && steam_plan_depot_of(games, &plan[index]) == depot)
			return &plan[index];
	return NULL;
}

int steam_plan_path(const struct steam_plan_file *file, const char *ce_root, const char *app_root, char *path,
	size_t capacity)
{
	int length = snprintf(path, capacity, "%s/%s", file->root == STEAM_ROOT_CE ? ce_root : app_root,
		file->destination);

	return length > 0 && (size_t)length < capacity;
}

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int steam_plan_header_ok(enum steam_plan_kind kind, const char *destination, const uint8_t *head, size_t head_size,
	uint64_t size)
{
	if (size == 0)
		return 0;
	switch (kind)
	{
	case STEAM_KIND_MCC_MAP:
		return size <= (2ull << 30) && head_size >= 8 && !memcmp(head, "daeh", 4) && le32(head + 4) == 13;
	case STEAM_KIND_BITMAPS:
		return size <= (2ull << 30) && head_size >= 4 && le32(head) == 1;
	case STEAM_KIND_BANK:
		return size <= (2ull << 30) && head_size >= 4 && !memcmp(head, "FSB5", 4);
	case STEAM_KIND_LIST:
		return size <= (2ull << 30) && head_size >= 4 && le32(head) == 1;
	case STEAM_KIND_RESOURCE:
	{
		/* (resource_map_ok: the type the name says, the names and index
		within the file) */
		static const struct
		{
			const char *name;
			uint32_t type;
			uint64_t maximum;
		} kinds[] = { { "bitmaps.map", 1, 512u << 20 }, { "sounds.map", 2, 256u << 20 }, { "loc.map", 3, 16u << 20 } };
		const char *base = strrchr(destination, '/');
		size_t index;

		base = base ? base + 1 : destination;
		if (head_size < 16)
			return 0;
		for (index = 0; index < 3; index++)
		{
			uint32_t names_offset = le32(head + 4), index_offset = le32(head + 8);
			int32_t count = (int32_t)le32(head + 12);

			if (strcmp(base, kinds[index].name))
				continue;
			return size <= kinds[index].maximum && le32(head) == kinds[index].type && names_offset >= 16 &&
				index_offset >= names_offset && count >= 0 && index_offset <= size &&
				(uint64_t)count <= (size - index_offset) / 12;
		}
		return 0;
	}
	}
	return 0;
}
