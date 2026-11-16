/*
MENU_TITLES.C

Menu style Classic+'s headings for the port's own screens (menus/classic:
LOBBY OPTIONS, FILE SHARE, FRIENDS, MEDIA, ...: classic/{lang}/header_*.png)
in Halo CE's own heading lettering, made on the player's machine from the
player's ui.map as the menus load, never shipped: the game's screen
headings (CHOOSE DIFFICULTY, SELECT GAMETYPE, ...) are read out of it, their
letters rebuilt and the heading's words set in them at the game's own size,
place and colour, over a plate like the game's (port/linux/src/title_tool.c,
adapted from ChupathingyCE's title tool, GPL-3.0-or-later). A heading the
game has itself (SELECT MAP) is its own picture.

The headings' pictures the letters are read from, their words and where
each letter starts are ChupathingyCE's port/assets/titles/titles.json's
"from_files" titles (measured by OpenCE's tools/title_assets.py, CC0, and
ChupathingyCE's); only a picture whose pixels are the ones measured (its
CRC-32) is read - an Xbox ui.map has the 26, MCC's (Halo PC's, from
bitmaps.map) 18.

A heading that cannot be made - a letter none of the headings has (B, J,
Q, W, Z; K on MCC; an accent), a word too wide, a ui.map without them (a
Custom Edition one), HALO_MENU_TITLES=0 - is the port's drawn picture
(tools/classic_menu_art.py), as before. Each made heading is kept in the
game's data folder (t:\menu_titles_<lang>_<name>.rgba, with the pictures'
checksum): made again only when the map's pictures change. The tool's
memory (some MB as it builds) is let go once the menus' pictures are read
(menu_titles_release). HALO_MENU_TITLES_DUMP=1: each heading made, as PAM,
in the data folder (t:\menu_titles_<lang>_<name>.pam).
*/

#include "cseries.h"
#include "cseries_windows.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmap_group.h"
#include "cache/cache_files.h"
#include "tag_files/tag_groups.h"
#include "memory/zlib/zlib.h"

#include "../src/lang.h"
#include "../src/title_tool.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
unsigned long long vita_host_time_us(void);
long menu_tags_generation(void);

/* ---------- constants */

#define BITMAP_FORMAT_DXT3 15
#define BITMAP_FORMAT_DXT5 16
#define BITMAP_CACHED_BIT 7
#define HEADING_WIDTH 512
#define HEADING_HEIGHT 64
#define HEADING_BYTES (HEADING_WIDTH * HEADING_HEIGHT * 4)
/* where a heading's first letter starts in its picture: the game's own
headings' (29.75 of a 512 x 64 picture drawn at 35, 11) */
#define HEADING_LEFT 29.75f
#define TITLES_FILE_SIGNATURE 0x3254544DUL /* 'MTT2': made again when the way of making them changes */

/* ---------- the game's headings the letters are read from (ChupathingyCE's titles.json) */

static float const lefts_0[] = { 29.875f, 50.5f, 71.0f, 94.125f, 117.75f, 140.375f, 173.625f, 196.625f, 205.25f, 225.875f, 246.625f, 254.375f, 275.375f, 298.0f, 313.0f, 330.125f };
static float const lefts_1[] = { 29.75f, 52.375f, 73.5f, 89.375f, 109.875f, 128.5f, 159.25f, 180.0f, 204.625f, 232.375f, 251.5f, 268.625f, 291.125f, 311.875f };
static float const lefts_2[] = { 29.75f, 52.375f, 73.5f, 89.375f, 109.875f, 128.5f, 160.625f, 183.125f, 204.0f, 228.25f, 249.125f, 257.5f, 273.875f };
static float const lefts_3[] = { 29.75f, 52.375f, 73.5f, 89.375f, 109.875f, 128.5f, 158.375f, 178.375f, 196.625f, 221.125f, 248.75f };
static float const lefts_4[] = { 29.75f, 51.125f, 73.875f, 91.5f, 99.0f, 120.75f, 140.75f, 161.75f, 196.875f, 218.5f, 232.25f, 253.75f, 274.75f, 296.5f, 317.5f };
static float const lefts_5[] = { 29.75f, 50.125f, 70.75f, 93.0f, 112.375f, 133.625f, 173.5f, 190.625f, 200.0f, 223.5f, 253.75f, 274.5f, 299.125f, 326.375f, 346.75f };
static float const lefts_6[] = { 29.375f, 52.25f, 73.5f, 89.25f, 109.625f, 128.25f, 160.5f, 182.875f, 203.5f, 228.25f, 249.0f, 257.5f, 273.75f, 294.125f, 312.875f, 335.0f, 348.75f, 370.25f, 391.25f, 412.375f, 446.25f };
static float const lefts_7[] = { 29.375f, 52.25f, 73.5f, 89.25f, 109.625f, 128.25f, 160.5f, 182.875f, 203.5f, 228.25f, 249.0f, 257.5f, 273.75f, 294.125f, 312.875f, 335.0f, 348.75f, 370.25f, 391.25f, 412.375f, 445.375f };
static float const lefts_8[] = { 30.75f, 59.0f, 81.875f, 97.25f, 118.0f, 126.875f, 148.875f, 162.75f, 184.375f, 205.25f, 226.375f };
static float const lefts_9[] = { 29.75f, 52.375f, 73.5f, 89.375f, 109.875f, 128.5f, 160.625f, 186.5f, 211.125f };
static float const lefts_10[] = { 29.75f, 52.375f, 73.5f, 89.375f, 109.875f, 128.5f, 160.625f, 183.125f, 204.0f, 228.25f, 249.125f, 257.5f, 273.875f };
static float const lefts_11[] = { 31.125f, 40.0f, 63.625f, 86.625f, 94.875f, 112.625f, 135.5f, 154.5f, 179.125f, 211.5f, 236.625f, 256.5f, 277.125f, 285.0f, 309.5f, 331.75f };
static float const lefts_12[] = { 31.125f, 37.875f, 57.875f, 79.125f, 118.0f, 142.625f, 162.875f, 183.625f, 191.0f, 216.0f, 237.75f };
static float const lefts_13[] = { 29.75f, 50.5f, 75.125f, 102.375f, 121.875f, 139.125f, 161.125f, 182.375f, 215.0f, 235.5f, 260.125f, 287.875f };
static float const lefts_14[] = { 31.0f, 52.875f, 66.75f, 87.75f, 108.75f, 130.375f, 162.875f, 187.875f, 207.75f, 228.5f, 236.375f, 260.75f, 283.0f };
static float const lefts_15[] = { 29.75f, 51.5f, 70.875f, 92.625f, 125.875f, 133.625f, 151.25f, 173.875f, 194.5f, 210.875f, 230.75f, 249.875f, 280.75f, 301.0f, 326.125f, 353.375f };
static float const lefts_16[] = { 29.75f, 51.375f, 74.5f, 81.25f, 113.875f, 141.875f, 164.875f, 180.25f, 201.0f, 209.875f, 232.0f, 245.75f, 266.75f, 288.25f, 309.375f, 341.75f, 363.0f, 387.375f, 415.25f, 434.25f, 451.25f, 473.5f, 494.75f };
static float const lefts_17[] = { 29.75f, 51.5f, 70.875f, 92.625f, 125.125f, 146.125f, 163.75f, 186.375f, 205.5f, 236.75f, 257.0f, 282.125f, 309.375f, 342.625f, 364.875f, 387.5f, 403.375f, 423.75f };
static float const lefts_18[] = { 29.75f, 52.375f, 73.5f, 89.375f, 109.875f, 128.5f, 159.25f, 180.0f, 204.625f, 232.375f, 251.5f, 268.625f, 291.125f, 311.875f, 342.875f, 362.0f, 397.375f, 418.625f, 441.625f, 449.0f };
static float const lefts_19[] = { 29.75f, 52.375f, 73.5f, 89.375f, 109.875f, 128.5f, 160.625f, 183.125f, 204.0f, 228.25f, 249.125f, 257.5f, 273.875f, 305.0f, 324.0f, 358.875f, 380.625f, 403.625f, 410.875f };
static float const lefts_20[] = { 28.0f, 52.625f, 72.875f, 94.125f, 118.625f, 140.75f, 160.375f, 181.625f, 215.375f, 234.5f, 259.625f, 280.5f, 301.125f, 322.0f, 346.5f, 362.25f };
static float const lefts_21[] = { 30.75f, 59.0f, 81.875f, 97.25f, 118.0f, 126.875f, 148.875f, 162.75f, 184.375f, 205.25f, 226.375f, 259.75f, 278.875f, 303.0f, 318.875f, 343.375f };
static float const lefts_22[] = { 29.875f, 49.0f, 74.0f, 94.875f, 115.625f, 136.5f, 161.0f, 177.5f, 193.875f, 215.125f, 247.75f, 270.875f, 289.875f, 310.375f, 333.625f };
static float const lefts_23[] = { 29.75f, 51.625f, 74.625f, 81.5f, 114.0f, 136.125f, 157.0f, 181.875f, 202.125f, 211.0f, 226.875f, 258.75f, 281.375f, 301.0f, 319.5f, 340.125f, 349.0f, 371.25f, 393.75f };
static float const lefts_24[] = { 31.125f, 53.625f, 74.5f, 98.75f, 119.625f, 128.0f, 143.875f, 177.0f, 197.625f, 222.125f, 249.875f };
static float const lefts_25[] = { 30.5f, 46.0f, 68.0f, 92.625f, 127.0f, 142.875f, 160.875f, 184.375f, 205.0f };

static struct title_tool_title const titles[] =
{
	{ "ui\\shell\\main_menu\\difficulty_select\\header_choose_difficulty", 0, 0x0E40DE64UL, "CHOOSE DIFFICULTY", lefts_0 },
	{ "ui\\shell\\main_menu\\gametype_select\\header_select_gametype", 0, 0xCFAD73CFUL, "SELECT GAMETYPE", lefts_1 },
	{ "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\4way_profile_select\\header_select_profile", 0, 0xE31755CCUL, "SELECT PROFILE", lefts_2 },
	{ "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\header_enlisted_players", 0, 0x0BF7FFFBUL, "SELECT TEAMS", lefts_3 },
	{ "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\header_enlisted_players", 1, 0xC2FD9158UL, "ENLISTED PLAYERS", lefts_4 },
	{ "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\server_list\\header_found_games", 0, 0xA6464785UL, "SYSTEM LINK GAMES", lefts_5 },
	{ "ui\\shell\\main_menu\\multiplayer_type_select\\coop\\header_select_profile_player_1", 0, 0xDA1FFA76UL, "SELECT PROFILE: PLAYER 1", lefts_6 },
	{ "ui\\shell\\main_menu\\multiplayer_type_select\\coop\\header_select_profile_player_2", 0, 0xB1A8B30CUL, "SELECT PROFILE: PLAYER 2", lefts_7 },
	{ "ui\\shell\\main_menu\\multiplayer_type_select\\header_multiplayer", 0, 0x508429EBUL, "MULTIPLAYER", lefts_8 },
	{ "ui\\shell\\main_menu\\multiplayer_type_select\\mp_map_select\\header_select_map", 0, 0x4FF21EF9UL, "SELECT MAP", lefts_9 },
	{ "ui\\shell\\main_menu\\player_profiles_select\\header_select_profile", 0, 0xE31755CCUL, "SELECT PROFILE", lefts_10 },
	{ "ui\\shell\\main_menu\\settings_select\\multiplayer_setup\\indicator_options_edit\\header_indicator_options", 0, 0x447CFFB8UL, "INDICATOR OPTIONS", lefts_11 },
	{ "ui\\shell\\main_menu\\settings_select\\multiplayer_setup\\item_options_edit\\header_item_options", 0, 0x13313AD1UL, "ITEM OPTIONS", lefts_12 },
	{ "ui\\shell\\main_menu\\settings_select\\multiplayer_setup\\name_edit\\header_gametype_name", 0, 0x6AD0142EUL, "GAMETYPE NAME", lefts_13 },
	{ "ui\\shell\\main_menu\\settings_select\\multiplayer_setup\\player_options_edit\\header_player_options", 0, 0xE90D9D37UL, "PLAYER OPTIONS", lefts_14 },
	{ "ui\\shell\\main_menu\\settings_select\\multiplayer_setup\\playlist_edit\\game_type_select\\header_rules", 0, 0xD1631B79UL, "STEP 1: SELECT GAME", lefts_15 },
	{ "ui\\shell\\main_menu\\settings_select\\multiplayer_setup\\playlist_edit\\header_edit_multiplayer_gametype", 0, 0x7F5267BBUL, "EDIT MULTIPLAYER GAMETYPE", lefts_16 },
	{ "ui\\shell\\main_menu\\settings_select\\multiplayer_setup\\playlist_edit\\header_set_options", 0, 0x3CC9D9E8UL, "STEP 2: SET GAME RULES", lefts_17 },
	{ "ui\\shell\\main_menu\\settings_select\\multiplayer_setup\\playlist_select\\header_select_gametype_to_edit", 0, 0xD71DECB1UL, "SELECT GAMETYPE TO EDIT", lefts_18 },
	{ "ui\\shell\\main_menu\\settings_select\\player_setup\\header_select_profile_to_edit", 0, 0xE5CC2C81UL, "SELECT PROFILE TO EDIT", lefts_19 },
	{ "ui\\shell\\main_menu\\settings_select\\player_setup\\player_profile_edit\\advanced_controls\\header_advanced_controls", 0, 0x066B9526UL, "ADVANCED CONTROLS", lefts_20 },
	{ "ui\\shell\\main_menu\\settings_select\\player_setup\\player_profile_edit\\color_edit\\header_profile_color", 0, 0xD122AEDEUL, "MULTIPLAYER COLOR", lefts_21 },
	{ "ui\\shell\\main_menu\\settings_select\\player_setup\\player_profile_edit\\controller_edit\\header_profile_controller_setting", 0, 0x447A095DUL, "CONTROLLER SETUP", lefts_22 },
	{ "ui\\shell\\main_menu\\settings_select\\player_setup\\player_profile_edit\\header_edit_profile_settings", 0, 0x3CB0E512UL, "EDIT PROFILE SETTINGS", lefts_23 },
	{ "ui\\shell\\main_menu\\settings_select\\player_setup\\player_profile_edit\\name_edit\\header_profile_name", 0, 0xF9C1952AUL, "PROFILE NAME", lefts_24 },
	{ "ui\\shell\\main_menu\\solo_level_select\\header_load_level", 0, 0xC5230DD9UL, "LOAD LEVEL", lefts_25 },
};
#define TITLE_COUNT ((long)NUMBEROF(titles))

/* ---------- the port's headings (tools/classic_menu_art.py HEADINGS, XBOX_HEADINGS) */

/* each one's picture's name (classic/{lang}/<name>.png), its words in
English (the language file's key: lang_text) and where its first letter
starts: the game's own headings' place, but for the stand-ins for two Xbox
headings MCC's ui.map has not, whose widgets are at 29, not 35
(classic/xbox_screens.xml) */
static struct
{
	char const *name;
	char const *english;
	float left;
} const headings[] =
{
	{ "header_lobby_options", "LOBBY OPTIONS", HEADING_LEFT },
	{ "header_forge", "FORGE", HEADING_LEFT },
	{ "header_extra_objects", "EXTRA OBJECTS", HEADING_LEFT },
	{ "header_customization", "CUSTOMIZATION", HEADING_LEFT },
	{ "header_file_share", "FILE SHARE", HEADING_LEFT },
	{ "header_mod_manager", "MOD MANAGER", HEADING_LEFT },
	{ "header_game_manager", "GAME MANAGER", HEADING_LEFT },
	{ "header_friends", "FRIENDS", HEADING_LEFT },
	{ "header_party", "PARTY", HEADING_LEFT },
	{ "header_add_friend", "ADD FRIEND", HEADING_LEFT },
	{ "header_invite_friends", "INVITE FRIENDS", HEADING_LEFT },
	{ "header_quick_chat", "QUICK CHAT", HEADING_LEFT },
	{ "header_select_map", "SELECT MAP", HEADING_LEFT },
	{ "header_select_network", "SELECT NETWORK", HEADING_LEFT },
	{ "header_players", "PLAYERS", HEADING_LEFT },
	{ "header_game_types", "GAME TYPES", HEADING_LEFT },
	{ "header_choose_a_base", "CHOOSE A BASE", HEADING_LEFT },
	{ "header_edit_game_type", "EDIT GAME TYPE", HEADING_LEFT },
	{ "header_sharing", "SHARING", HEADING_LEFT },
	{ "header_send_to_a_friend", "SEND TO A FRIEND", HEADING_LEFT },
	{ "header_media", "MEDIA", HEADING_LEFT },
	{ "header_modes", "EXTRAS", HEADING_LEFT },
	{ "header_command", "COMMAND", HEADING_LEFT },
	{ "header_select_profile_to_edit", "SELECT PROFILE TO EDIT", HEADING_LEFT - 6 },
	{ "header_advanced_controls", "ADVANCED CONTROLS", HEADING_LEFT - 6 },
};

/* ---------- globals */

static struct
{
	/* tried (1 read, -1 not to be had), as ui.map last loaded */
	int state;
	long generation;
	/* the headings' pictures' checksum (the CRCs of those read) */
	unsigned long checksum;
	long read;
	/* each picture as it was read: its DXT blocks (freed as the tool takes them) */
	unsigned char *blocks[TITLE_COUNT];
	int widths[TITLE_COUNT], heights[TITLE_COUNT], formats[TITLE_COUNT];
	/* made as a heading first needs it, let go by menu_titles_release */
	struct title_tool *tool;
	boolean tool_tried;
} menu_titles;

/* ---------- private code */

static boolean menu_titles_enabled(void)
{
	char const *value = getenv("HALO_MENU_TITLES");

	return !value || strcmp(value, "0") != 0;
}

/* title index's first level's DXT blocks out of the map, if it is there and
its pixels are those its letters were measured on */
static boolean title_read(long index)
{
	long tag_index = tag_loaded('bitm', titles[index].tag);
	struct bitmap_group *group;
	struct bitmap_data bitmap;
	unsigned long long started;
	boolean done = FALSE;
	unsigned char *data;
	long size, offset;

	if (tag_index == NONE || !(group = tag_get('bitm', tag_index)) || titles[index].bitmap >= group->bitmaps.count)
		return FALSE;
	bitmap = ((struct bitmap_data *)group->bitmaps.address)[titles[index].bitmap];
	if ((bitmap.format != BITMAP_FORMAT_DXT3 && bitmap.format != BITMAP_FORMAT_DXT5) || bitmap.depth != 1 ||
		bitmap.width < 4 || bitmap.height < 4 || bitmap.width > 1024 || bitmap.height > 256 ||
		(bitmap.width & 3) || (bitmap.height & 3))
	{
		return FALSE;
	}
	/* (DXT3 and DXT5: a byte a texel; its offset in the map as menu_words.c
	reads the main menu's words) */
	size = (long)bitmap.width * bitmap.height;
	offset = bitmap.pixels_offset + (TEST_FLAG(bitmap.flags, BITMAP_CACHED_BIT) ? 0 : group->pixel_data.file_offset);
	data = malloc(size);
	if (!data)
		return FALSE;
	started = vita_host_time_us();
	if (cache_file_read(tag_index, offset, size, data, &done, TRUE) != NONE)
	{
		while (!__atomic_load_n(&done, __ATOMIC_ACQUIRE) && vita_host_time_us() - started < 2000000ULL)
			SwitchToThread();
	}
	if (__atomic_load_n(&done, __ATOMIC_ACQUIRE) != TRUE ||
		crc32(0L, data, (uInt)size) != (uLong)titles[index].crc)
	{
		free(data);
		return FALSE;
	}
	menu_titles.blocks[index] = data;
	menu_titles.widths[index] = bitmap.width;
	menu_titles.heights[index] = bitmap.height;
	menu_titles.formats[index] = bitmap.format;
	return TRUE;
}

static void blocks_free(void)
{
	long index;

	for (index = 0; index < TITLE_COUNT; index++)
	{
		if (menu_titles.blocks[index])
			free(menu_titles.blocks[index]);
		menu_titles.blocks[index] = NULL;
	}
}

/* the headings' pictures read, once a ui.map load (the letters made from
them only as a heading first needs it: menu_titles_tool) */
static boolean menu_titles_ready(void)
{
	long generation = menu_tags_generation();
	long index;

	if (menu_titles.state && menu_titles.generation == generation)
		return menu_titles.state > 0;
	if (menu_titles.tool)
		title_tool_free(menu_titles.tool);
	blocks_free();
	memset(&menu_titles, 0, sizeof(menu_titles));
	menu_titles.generation = generation;
	menu_titles.state = -1;
	for (index = 0; index < TITLE_COUNT; index++)
	{
		if (!title_read(index))
			continue;
		menu_titles.checksum = crc32(menu_titles.checksum, (Bytef const *)&titles[index].crc, sizeof(titles[index].crc));
		menu_titles.read++;
	}
	if (!menu_titles.read)
	{
		platform_log("menu titles: ui.map has none of the game's headings: the port's pictures");
		return FALSE;
	}
	menu_titles.state = 1;
	platform_log("menu titles: %ld of the game's %ld headings read", menu_titles.read, TITLE_COUNT);
	return TRUE;
}

/* the letters, made of the pictures read */
static struct title_tool *menu_titles_tool(void)
{
	unsigned char *texels[TITLE_COUNT];
	unsigned long long started;
	long index;

	if (menu_titles.tool || menu_titles.tool_tried)
		return menu_titles.tool;
	menu_titles.tool_tried = TRUE;
	started = vita_host_time_us();
	for (index = 0; index < TITLE_COUNT; index++)
	{
		texels[index] = menu_titles.blocks[index] ? title_tool_dxt_decode(menu_titles.blocks[index],
			menu_titles.widths[index], menu_titles.heights[index], menu_titles.formats[index]) : NULL;
	}
	menu_titles.tool = title_tool_new(titles, TITLE_COUNT, texels, menu_titles.widths, menu_titles.heights);
	platform_log("menu titles: the game's heading letters %s in %lu ms", menu_titles.tool ? "made" : "could not be made",
		(unsigned long)((vita_host_time_us() - started) / 1000));
	return menu_titles.tool;
}

/* the heading's words in the language chosen, in capitals; FALSE when
they have a character beyond ASCII (no heading has its letter) */
static boolean heading_words(char const *english, char *words, int size)
{
	char const *text = lang_text(english);
	int index;

	for (index = 0; text[index] && index < size - 1; index++)
	{
		if ((unsigned char)text[index] >= 0x80)
			return FALSE;
		words[index] = (char)toupper((unsigned char)text[index]);
	}
	words[index] = 0;
	return text[index] == 0;
}

/* the heading kept in the data folder, if it is of these pictures and words */
static boolean cache_read(char const *path, unsigned long checksum, unsigned char *pixels)
{
	unsigned long header[3];
	FILE *file;
	boolean read = FALSE;

	if ((file = fopen(path, "rb")) == NULL)
		return FALSE;
	if (fread(header, sizeof(header), 1, file) == 1 && header[0] == TITLES_FILE_SIGNATURE && header[1] == checksum &&
		header[2] == HEADING_BYTES)
	{
		read = fread(pixels, HEADING_BYTES, 1, file) == 1;
	}
	fclose(file);
	return read;
}

static void cache_write(char const *path, unsigned long checksum, unsigned char const *pixels)
{
	unsigned long header[3];
	FILE *file;

	header[0] = TITLES_FILE_SIGNATURE;
	header[1] = checksum;
	header[2] = HEADING_BYTES;
	if ((file = fopen(path, "wb")) == NULL)
		return;
	if (fwrite(header, sizeof(header), 1, file) != 1 || fwrite(pixels, HEADING_BYTES, 1, file) != 1)
	{
		fclose(file);
		remove(path);
		return;
	}
	fclose(file);
}

/* (debug) HALO_MENU_TITLES_DUMP=1: the heading as PAM (BGRA in) */
static void heading_dump(char const *code, char const *name, unsigned char const *pixels)
{
	char path[96];
	FILE *file;
	long index;

	if (!getenv("HALO_MENU_TITLES_DUMP") || !atoi(getenv("HALO_MENU_TITLES_DUMP")))
		return;
	snprintf(path, sizeof(path), "t:\\menu_titles_%s_%s.pam", code, name);
	if ((file = fopen(path, "wb")) == NULL)
		return;
	fprintf(file, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", HEADING_WIDTH,
		HEADING_HEIGHT);
	for (index = 0; index < HEADING_WIDTH * HEADING_HEIGHT; index++)
	{
		unsigned char rgba[4];

		rgba[0] = pixels[index * 4 + 2];
		rgba[1] = pixels[index * 4 + 1];
		rgba[2] = pixels[index * 4];
		rgba[3] = pixels[index * 4 + 3];
		fwrite(rgba, 1, 4, file);
	}
	fclose(file);
}

/* ---------- public code */

/* (menu_tags.c, a frame of a PNG of the menus folder about to be read) the
heading made of the game's letters, 8-bit BGRA, malloc's (the caller frees
it); NULL: the PNG as ever. png: the frame's path as written
("classic/{lang}/header_forge.png"); width, height: the frame's */
unsigned char *menu_titles_frame(
	char const *png,
	long width,
	long height)
{
	char name[48], words[64], path[96];
	char const *code = lang_current();
	unsigned long checksum;
	unsigned char *pixels, *made;
	long index, title;
	int length;

	if (!png || strncmp(png, "classic/{lang}/header_", 22) || width != HEADING_WIDTH || height != HEADING_HEIGHT ||
		!menu_titles_enabled())
	{
		return NULL;
	}
	length = (int)strlen(png + 15);
	if (length < 5 || length - 4 >= (int)sizeof(name) || strcmp(png + 15 + length - 4, ".png"))
		return NULL;
	memcpy(name, png + 15, (size_t)(length - 4));
	name[length - 4] = 0;
	for (index = 0; index < (long)NUMBEROF(headings) && strcmp(headings[index].name, name); index++)
		;
	if (index == (long)NUMBEROF(headings) || !heading_words(headings[index].english, words, (int)sizeof(words)) ||
		!menu_titles_ready())
	{
		return NULL;
	}
	pixels = malloc(HEADING_BYTES);
	if (!pixels)
		return NULL;
	checksum = crc32(menu_titles.checksum, (Bytef const *)words, (uInt)strlen(words));
	checksum = crc32(checksum, (Bytef const *)&headings[index].left, sizeof(headings[index].left));
	snprintf(path, sizeof(path), "t:\\menu_titles_%s_%s.rgba", code, name);
	if (cache_read(path, checksum, pixels))
	{
		heading_dump(code, name, pixels);
		return pixels;
	}
	free(pixels);
	if (!menu_titles_tool())
		return NULL;
	/* (a heading the game has, at the game's place: its own picture) */
	made = NULL;
	for (title = 0; title < TITLE_COUNT && !made; title++)
	{
		if (headings[index].left == HEADING_LEFT && !strcmp(titles[title].text, words) &&
			menu_titles.widths[title] == HEADING_WIDTH && menu_titles.heights[title] == HEADING_HEIGHT)
		{
			made = title_tool_copy(menu_titles.tool, title);
		}
	}
	if (!made)
		made = title_tool_set(menu_titles.tool, words, HEADING_WIDTH, HEADING_HEIGHT, headings[index].left);
	pixels = made ? malloc(HEADING_BYTES) : NULL;
	if (!pixels)
	{
		if (made)
			title_tool_picture_free(made);
		platform_log("menu titles: %s cannot be set in the game's letters: the port's picture", words);
		return NULL;
	}
	/* (RGBA to BGRA, as menu_tags.c's PNGs; in this allocator's memory,
	which menu_tags.c frees) */
	for (title = 0; title < HEADING_WIDTH * HEADING_HEIGHT; title++)
	{
		pixels[title * 4] = made[title * 4 + 2];
		pixels[title * 4 + 1] = made[title * 4 + 1];
		pixels[title * 4 + 2] = made[title * 4];
		pixels[title * 4 + 3] = made[title * 4 + 3];
	}
	title_tool_picture_free(made);
	cache_write(path, checksum, pixels);
	heading_dump(code, name, pixels);
	platform_log("menu titles: %s made of the game's letters", words);
	return pixels;
}

/* (menu_tags.c, the menus' pictures read) the letters and the pictures
they were made of let go: what the next heading needs is read again */
void menu_titles_release(
	void)
{
	if (menu_titles.tool)
		title_tool_free(menu_titles.tool);
	menu_titles.tool = NULL;
	menu_titles.tool_tried = FALSE;
	blocks_free();
	menu_titles.state = 0;
}
