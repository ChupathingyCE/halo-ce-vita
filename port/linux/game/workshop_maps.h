/*
WORKSHOP_MAPS.H

(port) Steam Workshop multiplayer maps (1.1.3, a build with configure.py
--steam-workshop): the names they are played under, and what a joiner
makes of a host's map that is one (docs/steam_download_security.md section
8, the security review of the network side).

A Workshop item the game installed (port/steamdl/steam_workshop.c) is the
mods folder's mods/<id>, <id> the item's decimal number. Its maps\*.map
files that are Halo: CE Anniversary caches of multiplayer scenarios are the
multiplayer maps ws<id>_<map> (ws2915693904_coldsnap; the level
levels\test\ws2915693904_coldsnap\ws2915693904_coldsnap), at most 25
characters as every custom map's name (a level name must fit the game
engine's 63). A file name too long for that keeps its first characters and
ends in '-' and four hex digits of its whole name's FNV-1a
(ws2915693904_bigasssn-1f0a): one name for every machine, each of which
finds the file again by making the names of the item's maps.

That name is the whole of what the network carries about a Workshop map:
the host's game settings name the level, and the map's version is the
identity of the host's copy (custom_edition_cache_map_identity: the header's
checksum with the file's length), as for any custom map. No field is added
to any message, so every older build reads the game settings as it always
did: to it the map is a custom map it lacks, which it asks the host for,
and the host refuses (a Workshop map is never shared:
custom_edition_maps_shareable). A joiner of this build that lacks the item
asks its player whether to download it from Steam (never from the host);
one whose copy is not the host's is refused, and says so.

The names are the reserved pattern "ws", 1 to 12 decimal digits without a
leading zero, '_', then letters, digits, '_' and '-': a file of the maps
folder of such a name is never listed (it is not this item's), and the
name is only ever looked for in the mods folder.

This unit is plain C (no game header): the tests and the network fuzzer
use it alone (port/vita/tests/workshop_maps_test.c, net_fuzz_workshop.c).
*/

#ifndef __WORKSHOP_MAPS_H
#define __WORKSHOP_MAPS_H

#include <stddef.h>
#include <stdint.h>

/* ---------- constants */

#define WORKSHOP_MAP_PREFIX "ws"
/* a map's name (custom_edition_maps.c's MAXIMUM_MAP_NAME_LENGTH) and room
for it with its terminator */
#define WORKSHOP_MAP_NAME_MAXIMUM 25
#define WORKSHOP_MAP_NAME_BYTES 32
/* an item's number: decimal, at most this many digits (steam_workshop.h's
STEAM_WORKSHOP_ID_DIGITS, the mods folder's key) */
#define WORKSHOP_MAP_ID_DIGITS 12
/* a map file's name without ".map", lower case (custom_edition_mods.c's
MAP_STEM_BYTES) */
#define WORKSHOP_MAP_STEM_BYTES 24
/* a host's level name read at most this far (struct network_game_map's
name) */
#define WORKSHOP_MAP_LEVEL_NAME_BYTES 0x80
/* the multiplayer level name's form (custom_edition_maps.c) */
#define WORKSHOP_MAP_LEVEL_PREFIX "levels\\test\\"

/* ---------- names */

/* the name of the item `id`'s map whose file is `stem`.map (`stem`:
1 to WORKSHOP_MAP_STEM_BYTES - 1 of [a-z0-9_-]): 1 and the name in `name`
(WORKSHOP_MAP_NAME_BYTES), 0 (`name` empty) if it cannot be named */
int workshop_map_name_make(uint64_t id, const char *stem, char *name);

/* whether `name` (at most `size` bytes read; it must end within them) is a
Workshop map's name, in any case: 1 with the item's number in `*id` (may be
NULL), else 0 */
int workshop_map_name_parse(const char *name, size_t size, uint64_t *id);

/* a host's level name (at most `size` bytes read, WORKSHOP_MAP_LEVEL_NAME_BYTES
from the network): 1 if it is levels\test\<name>\<name> of a Workshop map's
name, with that name (WORKSHOP_MAP_NAME_BYTES, lower case) and the item's
number; else 0 - another name, or one not ended within `size` */
int workshop_map_level_name_parse(const char *level_name, size_t size, char *name, uint64_t *id);

/* the item's number as its mods folder's name: 1, 0 if it does not fit */
int workshop_map_id_text(uint64_t id, char *text, size_t size);
/* a mods folder's name that is an item's number (1 to 12 digits, no leading
zero): 1 and the number */
int workshop_map_id_parse(const char *text, uint64_t *id);

/* ---------- a joiner's verdict on the host's map */

enum workshop_map_join
{
	/* not a Workshop map's name: the other checks decide */
	WORKSHOP_MAP_JOIN_NOT_WORKSHOP,
	/* this machine's copy is the host's */
	WORKSHOP_MAP_JOIN_PLAYABLE,
	/* this machine has no such map: the player may download the item */
	WORKSHOP_MAP_JOIN_MISSING,
	/* this machine's copy is another (the item updated on one side) */
	WORKSHOP_MAP_JOIN_DIFFERENT,
	/* the host did not say which copy it plays (identity 0): refused, a host
	of this build always says */
	WORKSHOP_MAP_JOIN_UNIDENTIFIED,
};

/* The verdict on the host's level `level_name` (`size` bytes at most) of the
identity `host_identity` (its game settings' map version), this machine's
copy being `local_identity` (0: none here). The item's number in `*id` (0
for a name not a Workshop map's). */
enum workshop_map_join workshop_map_join_verdict(const char *level_name, size_t size, unsigned long host_identity,
	unsigned long local_identity, uint64_t *id);

#endif
