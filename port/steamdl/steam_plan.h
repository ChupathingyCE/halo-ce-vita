/*
STEAM_PLAN.H

What the Steam download takes from MCC's depots and where each file goes on
the Vita: the same files, the same places and the same checks as Setup's
Automatic (MCC) install (tools/installer/halo_ce_vita_installer.py:
mcc_plan). 1.1.x's: Halo CE alone (1.2.0's table also has Halo 2).

  Halo CE   ux0:data/haloce-vita/mcc/halo1/maps/...        (the CE root)
            (campaign maps and bitmaps.map from depot 976733, multiplayer
            maps from 976734; ui.map, custom_edition's three resource maps
            and sound/pc's sounds_adpcm.fsb bank and five lists from
            976733)
  Sound banks (STEAM_GAME_HALO_CE_BANKS, the player may leave them out)
            sound/pc's CELT banks from 976733: the dialogue, the music, the
            streams and Anniversary's few effects (1.5 GB of the 2 GB)

A depot's file name is never made into a path: it is only compared (any
case, '/' for '\') with this table's exact depot paths, and the destination
is the table's own.
*/

#ifndef STEAM_PLAN_H
#define STEAM_PLAN_H

#include <stddef.h>
#include <stdint.h>

#define STEAM_APP_MCC 976730u
#define STEAM_DEPOT_HALO1_CAMPAIGN 976733u
#define STEAM_DEPOT_HALO1_MULTIPLAYER 976734u

/* what the player chose */
#define STEAM_GAME_HALO_CE 1u
#define STEAM_GAME_HALO_CE_BANKS 2u
#define STEAM_GAMES_ALL 3u

enum steam_plan_root
{
	/* ux0:data/haloce-vita (Halo CE's files) */
	STEAM_ROOT_CE,
	/* the app's own folder (1.1.x: the same folder; the marker lives there) */
	STEAM_ROOT_APP,
};

/* the header checks Setup makes (_mcc_file_ok) */
enum steam_plan_kind
{
	STEAM_KIND_MCC_MAP, /* "daeh", version 13 */
	STEAM_KIND_BITMAPS, /* halo1 bitmaps.map: 01 00 00 00 */
	STEAM_KIND_RESOURCE, /* custom_edition's bitmaps/sounds/loc.map */
	STEAM_KIND_BANK, /* "FSB5" */
	STEAM_KIND_LIST, /* u32 1 */
};

struct steam_plan_file
{
	uint32_t games;
	/* the depots holding it, the one to take it from first (among those
	the choice downloads) */
	uint32_t depots[2];
	const char *depot_path;
	enum steam_plan_root root;
	const char *destination;
	enum steam_plan_kind kind;
	int required;
};

/* the table, and its length */
const struct steam_plan_file *steam_plan_files(size_t *count);

/* the depots a choice downloads, in order (at most 4): their count */
size_t steam_plan_depots(uint32_t games, uint32_t depots[4]);

/* the planned file a depot's file name is, for this choice and this depot,
or NULL (a name that is not plain never matches) */
const struct steam_plan_file *steam_plan_match(uint32_t games, uint32_t depot, const char *name);

/* the depot a planned file is taken from for this choice, 0 if none */
uint32_t steam_plan_depot_of(uint32_t games, const struct steam_plan_file *file);

/* a depot name is plain: no empty, "." or ".." part, no leading '/', no ':',
no control characters, at most 1024 bytes */
int steam_plan_name_plain(const char *name);

/* the planned file's full path: root (ce_root or app_root) + '/' +
destination: 1, or 0 if it does not fit */
int steam_plan_path(const struct steam_plan_file *file, const char *ce_root, const char *app_root, char *path,
	size_t capacity);

/* the file's header check (Setup's), on its first bytes and its size: 1 if
it passes */
int steam_plan_header_ok(enum steam_plan_kind kind, const char *destination, const uint8_t *head, size_t head_size,
	uint64_t size);

#endif
