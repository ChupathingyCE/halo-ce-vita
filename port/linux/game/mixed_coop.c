/*
MIXED_COOP.C

Mixed co-op: Xbox and MCC files in one network co-op campaign game
(mixed_coop.h). The parts:

- the level's files: which family this machine plays a stock campaign level
  from, and the cache loader's alias of the Xbox's level name to MCC's file
  in a network game (mixed_coop_mcc_alias);
- the map's load (mixed_coop_tags_loaded): an MCC level of network co-op
  made the Xbox's - its MCC-only placements (the scavenger hunt's Moa
  scenery) taken out of the scenario before anything is placed, its MCC-only
  scripts made static (never started), the Xbox rules profile - then every
  tag's digest by the tag schemas (tag_schema.h), the layout's and the
  dictionary's table;
- the lobby's hello and answer (as infection_net.c's);
- after the load: the client's request, the host's dictionary in fragments,
  the client's check of it and its translation tables; a structure BSP's
  collision digest as each machine loads it;
- the translation of a host's messages on a client (mixed_coop_translate_
  incoming: each module's own entries, network_objects.c ...).

The digests (mixed_coop_tag_digest): a tag walked through its group's schema,
every field the schema lists taken as what it means rather than its bytes -
a tag reference or tag index as its tag's group and path (so the same tag
in two maps hashes alike whatever its index), a block as its count and
elements, data as its bytes (a file's data as its size), a runtime value not
at all - and every byte the schema does not list (the plain numbers and
flags the game reads) as it is. A Custom Edition map's tags are this build's
by then (cache_file_formats.c), so an Xbox tag and its MCC copy hash alike
when what the game reads in them is alike.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "cache/cache_files.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "tag_files/files.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "hs/hs_scenario_definitions.h"
#include "ai/ai_scenario_definitions.h"
#include "items/projectile_definitions.h"
#include "items/weapon_definitions.h"
#include "items/item_definitions.h"
#include "units/biped_definitions.h"
#include "main/main.h"
#include "models/model_animation_definitions.h"
#include "bungie_net/network/transport.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_messages.h"
#include "networking/network_client_manager.h"
#include "networking/network_server_manager.h"
#include "networking/network_server_manager_internal.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "physics/collision_bsp_definitions.h"
#include "physics/physics_definitions.h"
#include "physics/collision_model_definitions.h"
#include "physics/collision_bsp.h"
#include "physics/collisions.h"
#include "objects/damage_resistances.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"

#include "mcc_content.h"
#include "custom_edition_maps.h"

/* (collision_bsp.c) the collision BSP whose invisible surfaces collide with nothing, or NULL */
extern struct collision_bsp const *collision_bsp_invisible_masked;
#include "mixed_coop.h"
#include "mixed_collision_protocol.h"
#include "mixed_coop_delta.h"
#include "mixed_coop_tables.h"
#include "network_distributed.h"
#include "network_wire.h"
#include "tag_schema.h"
#include "../src/lang.h"
#include "../../third_party/monocypher/monocypher.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* the platform's and the game's */
void platform_log(char const *format, ...);
void platform_show_message(char const *title, char const *message);
const char *config_string(char const *name);
boolean network_game_client_send_to_server(struct network_game_client *client, void *message);
int halo_content_library_found(void);
extern long halo_loaded_tag_count;
/* (cache_files.c, 1.1.3) a runtime tag beside the map's (mixed play's flag stand collision), or NULL */
struct cache_file_tag_instance_view *cache_runtime_tag_instance(long tag_index);

/* (network_messages.c's packet table: the classes of the lobby's) */
#define MIXED_COOP_CLASS_CLIENT_PREGAME 3
#define MIXED_COOP_CLASS_SERVER_PREGAME 2

/* how long a client waits for the host's dictionary before it leaves */
#define MIXED_COOP_DICTIONARY_WAIT_MILLISECONDS 20000
/* (1.1.3) how long a host holds a machine's dictionary request for the collision it offered */
#define MIXED_COLLISION_WAIT_MILLISECONDS 12000
#define MIXED_COOP_HELLO_RESEND_MILLISECONDS 1000
/* the deepest a tag's walk goes, and the most elements it takes in all */
#define MIXED_COOP_WALK_DEPTH 12
#define MIXED_COOP_WALK_BUDGET 4000000L
#define MIXED_COOP_MASK_BYTES 8192

/* the MCC-only placements of the campaign's scenarios (the research's
scenario_campaign_order.json: appended to the scenery of a30 ... d20, and
a10's), by their palette entry's tag */
static char const *const mixed_coop_mcc_extra_objects[] =
{
	"scenery\\reach_moa\\reach_moa",
};
/* the equipment spawns Halo PC added to the multiplayer maps (MCC's files
have them; the Xbox's none): the flamethrower's (every map: the game makes
them rocket launchers, more than the Xbox's map has) and the plasma cannon's
(Boarding Action: a weapon the Xbox's files have not, which an Xbox client
could not make). Not spawned on a machine of MCC's files in mixed play */
static char const *const mixed_coop_mcc_extra_item_collections[] =
{
	"item collections\\single weapons\\flame thrower",
	"item collections\\single weapons\\plasma_cannon",
};
/* the object names those placements have (sh_<level>_...): left out of the
layout's digest */
#define MIXED_COOP_MCC_EXTRA_NAME_PREFIX "sh_"
/* the scripts MCC alone has, by name (the research's m_only): breadcrumb and
revised nav points, a few renamed test scripts. Made static: never started,
only run if another script calls one (none of the Xbox's does) */
static char const *const mixed_coop_mcc_only_script_prefixes[] =
{
	"breadcrumbs_", "bcs_", "improved_nav_points_", "revised_nav_points_", "crazy_marine_nav_point",
};

/* the levels by index: the campaign's, as main.c's campaign lists them;
then the multiplayer maps whose collision the research (Nov 13 2026,
mixmaps-runs/REPORT.md) found the same in MCC's files and the Xbox's - Chill
Out, Longest, Prisoner, Chiron TL-34, Rat Race, Wizard, and Boarding Action
(two ladders up to 10 cm apart: the owner accepted them, Nov 14); and Damnation (r3: Halo PC added only
invisible two-sided surfaces to its collision - the research's PRIOR-ART.md -, which an MCC machine in an
Xbox host's game makes collide with nothing: the Xbox's collision exactly) */
static char const *const mixed_coop_level_stems[MIXED_COOP_LEVELS] =
{
	"a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
	"chillout", "longest", "prisoner", "putput", "ratrace", "wizard", "boardingaction", "damnation",
	/* (r3, two of the maps Halo PC rebuilt whose placements, flags and starts are the Xbox's: an Xbox-files
	host's game alone, its collision deciding - mixed_coop_rebuilt_maps) */
	"carousel", "hangemhigh",
	/* (round 4, Nov 23 2026: the three Halo PC rebuilt and placed other scenery and vehicles on - an Xbox-files
	host's game alone, its placements sent to an MCC machine before it loads the map and its collision deciding:
	mixed_coop_rebuilt_maps' host_layout) */
	"bloodgulch", "beavercreek", "sidewinder",
};

/* the multiplayer maps Halo PC rebuilt (the research: walls, ceilings,
invisible walls, spawns, vehicles - MCC has Halo PC's). Derelict and Hang 'Em
High (r3): MCC's placements, flags and starts are the Xbox's, only the
collision differs - mixed in an Xbox-files host's game, the host's collision
deciding (the Xbox-host-decides mode: mixed_coop_rebuilt_level), an MCC host's
refused to an Xbox machine. Blood Gulch, Battle Creek and Sidewinder place
other scenery or vehicles too (r4): the same, with the host's layout taken by
an MCC machine before it loads the map (host_layout). An Xbox machine refused
an MCC host's game is told so (mixed_coop_multiplayer_refusal) */
static struct
{
	char const *stem;
	char const *title;
	char const *why;
	/* (one of mixed_coop_level_stems: an Xbox-files host's game mixed) */
	boolean xbox_host_mixes;
	/* (r4) the host's placements (scenery, vehicles, their palettes and the netgame flags) taken by an MCC machine
	before it loads the map (mixed_coop_layout_*): MCC's files place Halo PC's */
	boolean host_layout;
} const mixed_coop_rebuilt_maps[] =
{
	{ "bloodgulch", "Blood Gulch", "rebuilt walls, a lower ceiling, invisible walls and other vehicles", TRUE, TRUE },
	{ "beavercreek", "Battle Creek", "rebuilt collision and other scenery", TRUE, TRUE },
	{ "carousel", "Derelict", "rebuilt collision", TRUE, FALSE },
	{ "hangemhigh", "Hang 'Em High", "rebuilt collision", TRUE, FALSE },
	{ "sidewinder", "Sidewinder", "rebuilt collision, other scenery and vehicles", TRUE, TRUE },
};

/* a tag's place in the digests */
enum
{
	/* not compared */
	_mixed_tag_other = 0,
	/* gameplay: a difference refuses the game */
	_mixed_tag_gameplay,
	/* the host's alone (AI) or what only shows (effects, animations): told */
	_mixed_tag_told,
};

struct mixed_coop_local_tag
{
	uint32_t path_hash;
	uint32_t digest;
	uint8_t kind;
	uint8_t ambiguous;
};

static struct
{
	/* --- the map loaded */
	boolean loaded;
	int level;
	int family;
	/* (a network co-op game's map, made the Xbox's on an MCC machine) */
	boolean network_coop;
	long scenario_index;
	long tag_count;
	struct mixed_coop_local_tag *tags;
	struct mixed_coop_digests digests;
	uint32_t bsp_digests[MIXED_COOP_MAXIMUM_BSPS][2];
	short bsp_count;
	long extras_removed;
	long scripts_disabled;
	long rules_applied;

	/* --- the lobby: the host's */
	boolean hosting;
	uint32_t session;
	struct
	{
		boolean known;
		boolean answered;
		long machine_index;
		int family;
		unsigned long hello_time;
		/* (after the load) asked for the dictionary, sent it */
		boolean requested;
		boolean mixed;
		short hellos;
		/* (r4) asked for the host's layout: 1 + the next fragment to send it (0: not asked) */
		short layout_next;
		/* (1.1.3, an MCC host's: mixed_collision_protocol.h) asked for the collision it lacks (how often, when
		last), the machine's offer heard (what it will send), its stream as it comes, done (taken or not), and
		its dictionary request held until then */
		short collision_asks;
		unsigned long collision_ask_time;
		boolean collision_offer_heard;
		uint16_t collision_offered;
		struct mixed_collision_reassembly *collision_reassembly;
		boolean collision_done;
		boolean request_held;
		unsigned long request_held_since;
		struct mixed_coop_request held_request;
	} machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	/* the host's dictionary stream (built once a map) */
	uint8_t *stream;
	uint32_t stream_length;

	/* --- the lobby: a client's */
	/* (1.1.3) the collision models an MCC host asked this Xbox machine for, in that hosting */
	uint16_t collision_asked;
	uint32_t collision_asked_session;
	boolean host_said;
	int host_family;
	int host_level;
	uint32_t host_session;

	/* --- after the load: a client's */
	boolean needs_dictionary;
	boolean requested;
	unsigned long requested_time;
	boolean matched;
	boolean leaving;
	char leave_text[320];
	struct mixed_coop_reassembly *reassembly;
	uint32_t host_bsp_digests[MIXED_COOP_MAXIMUM_BSPS][2];
	/* host tag index -> this machine's tag index (datum), and back */
	long host_tag_count;
	long *host_to_local;
	long *local_to_host;
	/* the host's tags' datums, by the host's absolute index */
	long *host_datums;
	/* animations: per this machine's graph (absolute tag index), the host's
	animation index -> this machine's, in a pool */
	long *graph_offsets;
	short *graph_counts;
	short *animation_pool;
	long animation_pool_used;
	long unmapped_tags;
	long unmapped_animations;

	/* --- (r4) the host's layout (Blood Gulch, Battle Creek, Sidewinder: mixed_coop_rebuilt_maps' host_layout) */
	/* the level the host's hellos name */
	int hello_level;
	/* the host's stream (built as its map loads) */
	uint8_t *layout_stream;
	uint32_t layout_length;
	/* a client's: the stream as it comes, whole once complete; the game's start held meanwhile */
	struct mixed_coop_layout_reassembly *layout_reassembly;
	boolean layout_complete;
	struct network_game_client *start_pending;
	unsigned long start_pending_since;
	unsigned long layout_asked_time;
	/* (1.1.3, the owner's 8l) the game's start held for the host's hello: a host that never says it plays mixed
	(an older build) refused before the map loads; and the lobby's wait for it (0: not waiting) */
	boolean start_waits_hello;
	unsigned long lobby_hello_since;
	/* 1 + the level a refusal before the load names (its title "Halo: map" or "Halo: co-op"); 0: the loaded one */
	int leave_level;
	/* the scenario blocks made from it (freed at the next map's load) */
	void *layout_blocks[8];
	/* the Xbox tags this machine plays with its stand-ins (mixed_coop_stand_ins) */
	long stand_ins;
	/* this machine's map placed as the host's layout */
	boolean layout_taken;
	/* (1.1.3) the host never said it plays mixed (an older build, an OpenCE or ChupathingyCE host): this machine
	plays by its built-in table of the Xbox's level (mixed_coop_tables.h), as if the host had sent it */
	boolean client_only;
	/* (1.1.3) a map Halo PC rebuilt played so: the player asked (the level), its answer awaited; the game's start
	held for it */
	int differs_asked_level;
	boolean start_waits_answer;
} mixed_coop;

/* the game's free asserts on NULL (debug_memory.c) */
static void mixed_coop_free(
	void *pointer)
{
	if (pointer)
		free(pointer);
}

/* (the crossplay wire: a PC host's game) whether the host is a ChupathingyCE host that said it plays mixed, over
Delta Peer (mixed_coop_delta.h) - none else plays mixed on that wire */
static boolean mixed_coop_crossplay_host(
	void)
{
	return network_wire_crossplay() && mixed_coop_delta_agreed() && mixed_coop.host_said;
}

/* (the crossplay wire) the pieces of the host's dictionary and layout as they come over Delta Peer, into the
reassemblies' own bytes */
static struct mixed_coop_delta_pieces mixed_coop_delta_dictionary;
static struct mixed_coop_delta_pieces mixed_coop_delta_layout;
/* how often a client says again what it still wants of them (Delta Peer's datagrams may be lost) */
#define MIXED_COOP_DELTA_WANT_MILLISECONDS 500

/* ---------- the level and its files */

static boolean mixed_coop_has_mcc_prefix(
	char const *name)
{
	char const *prefix = MCC_CONTENT_NAME_PREFIX;

	for (; *prefix; prefix++, name++)
	{
		char character = *name >= 'A' && *name <= 'Z' ? (char)(*name + ('a' - 'A')) : *name;

		if (character != *prefix)
			return FALSE;
	}
	return TRUE;
}

/* whether mixed multiplayer is on (HALO_MIXED_MULTIPLAYER=0, tests: as a
build of before it; the multiplayer maps are then never mixed_coop's) */
int mixed_coop_multiplayer_enabled(
	void)
{
	char const *setting = getenv("HALO_MIXED_MULTIPLAYER");

	return !(setting && setting[0] == '0') && !(getenv("HALO_MIXED_COOP") && getenv("HALO_MIXED_COOP")[0] == '0');
}

/* a level name's stem as a stock multiplayer map's: chillout,
levels\test\chillout\chillout, and MCC's names of it (mcc_chillout,
levels\test\mcc_chillout\mcc_chillout: mcc_content.h) */
static boolean mixed_coop_multiplayer_name_is(
	char const *level_name,
	char const *stem)
{
	char const *name = tag_name_strip_path(level_name);
	char full[64];

	if (mixed_coop_has_mcc_prefix(name))
	{
		name += sizeof(MCC_CONTENT_NAME_PREFIX) - 1;
		if (csstrcasecmp(name, stem))
			return FALSE;
		if (name - (sizeof(MCC_CONTENT_NAME_PREFIX) - 1) == level_name)
			return TRUE;
		snprintf(full, sizeof(full), "levels\\test\\%s%s\\%s%s", MCC_CONTENT_NAME_PREFIX, stem, MCC_CONTENT_NAME_PREFIX, stem);
		return !csstrcasecmp(level_name, full);
	}
	if (csstrcasecmp(name, stem))
		return FALSE;
	if (name == level_name)
		return TRUE;
	snprintf(full, sizeof(full), "levels\\test\\%s\\%s", stem, stem);
	return !csstrcasecmp(level_name, full);
}

int mixed_coop_level_index(
	char const *level_name)
{
	char const *name;
	int index;

	if (!level_name || !level_name[0])
		return -1;
	name = tag_name_strip_path(level_name);
	for (index = 0; index < MIXED_COOP_CAMPAIGN_LEVELS; index++)
	{
		char full[32];

		if (csstrcasecmp(name, mixed_coop_level_stems[index]))
			continue;
		/* (a bare stem, or levels\<stem>\<stem>) */
		if (name == level_name)
			return index;
		snprintf(full, sizeof(full), "levels\\%s\\%s", mixed_coop_level_stems[index], mixed_coop_level_stems[index]);
		if (!csstrcasecmp(level_name, full))
			return index;
	}
	if (!mixed_coop_multiplayer_enabled())
		return -1;
	/* (the Xbox's names alone: MCC's, mcc_chillout, are MCC's own game,
	mixed_coop_host_map_name) */
	if (mixed_coop_has_mcc_prefix(name))
		return -1;
	for (; index < MIXED_COOP_LEVELS; index++)
	{
		if (mixed_coop_multiplayer_name_is(level_name, mixed_coop_level_stems[index]))
			return index;
	}
	return -1;
}

/* a mixed multiplayer map's index from MCC's name of it (mcc_chillout), or -1 */
static int mixed_coop_level_index_mcc_name(
	char const *level_name)
{
	int index;

	if (!level_name || !mixed_coop_has_mcc_prefix(tag_name_strip_path(level_name)) || !mixed_coop_multiplayer_enabled())
		return -1;
	for (index = MIXED_COOP_CAMPAIGN_LEVELS; index < MIXED_COOP_LEVELS; index++)
	{
		if (mixed_coop_multiplayer_name_is(level_name, mixed_coop_level_stems[index]))
			return index;
	}
	return -1;
}

int mixed_coop_level_is_multiplayer(
	int level)
{
	return level >= MIXED_COOP_CAMPAIGN_LEVELS && level < MIXED_COOP_LEVELS;
}

/* whether the level is one Halo PC rebuilt (mixed_coop_rebuilt_maps): mixed with an Xbox-files host alone */
static boolean mixed_coop_rebuilt_level(
	int level)
{
	short index;

	for (index = 0; level >= MIXED_COOP_CAMPAIGN_LEVELS && level < MIXED_COOP_LEVELS &&
		index < NUMBEROF(mixed_coop_rebuilt_maps); index++)
	{
		if (!csstrcasecmp(mixed_coop_level_stems[level], mixed_coop_rebuilt_maps[index].stem))
			return TRUE;
	}
	return FALSE;
}

/* (r4) whether the level is one whose placements an MCC machine takes from an Xbox-files host
(mixed_coop_rebuilt_maps' host_layout) */
static boolean mixed_coop_host_layout_level(
	int level)
{
	short index;

	for (index = 0; level >= MIXED_COOP_CAMPAIGN_LEVELS && level < MIXED_COOP_LEVELS &&
		index < NUMBEROF(mixed_coop_rebuilt_maps); index++)
	{
		if (!csstrcasecmp(mixed_coop_level_stems[level], mixed_coop_rebuilt_maps[index].stem))
			return mixed_coop_rebuilt_maps[index].host_layout;
	}
	return FALSE;
}

char const *mixed_coop_multiplayer_refusal(
	char const *level_name)
{
	static char text[256];
	short index;

	if (!level_name || !level_name[0])
		return NULL;
	for (index = 0; index < NUMBEROF(mixed_coop_rebuilt_maps); index++)
	{
		if (!mixed_coop_multiplayer_name_is(level_name, mixed_coop_rebuilt_maps[index].stem))
			continue;
		if (mixed_coop_rebuilt_maps[index].xbox_host_mixes)
			snprintf(text, sizeof(text), T("Halo PC rebuilt %s (%s), and MCC's files have Halo PC's. With Xbox and MCC "
				"files together %s needs an Xbox-files host: ask a player with the Xbox's maps to host."),
				mixed_coop_rebuilt_maps[index].title, mixed_coop_rebuilt_maps[index].why, mixed_coop_rebuilt_maps[index].title);
		else
			snprintf(text, sizeof(text), T("Halo PC rebuilt %s (%s), and MCC's files have Halo PC's. Mixed games on %s "
				"will need an Xbox-files host and aren't ready yet: everyone needs the same files for now."),
				mixed_coop_rebuilt_maps[index].title, mixed_coop_rebuilt_maps[index].why, mixed_coop_rebuilt_maps[index].title);
		return text;
	}
	return NULL;
}

static boolean mixed_coop_path_exists(
	char const *path)
{
	struct file_reference reference;

	return file_exists(file_reference_create_from_path(&reference, path, FALSE));
}

static boolean mixed_coop_xbox_file(
	int level)
{
	char path[256];

	snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), mixed_coop_level_stems[level]);
	return mixed_coop_path_exists(path);
}

static boolean mixed_coop_mcc_file(
	int level)
{
	char path[256];

	return halo_content_library_found() && mcc_content_map_path(mixed_coop_level_stems[level], path, sizeof(path)) &&
		mixed_coop_path_exists(path);
}

int mixed_coop_local_family(
	int level)
{
	if (level < 0 || level >= MIXED_COOP_LEVELS)
		return _mixed_coop_family_none;
	if (mixed_coop_xbox_file(level))
		return _mixed_coop_family_xbox;
	if (mixed_coop_mcc_file(level))
		return _mixed_coop_family_mcc;
	return _mixed_coop_family_none;
}

/* whether mixed co-op is off (HALO_MIXED_COOP=0, tests: as an older build) */
static boolean mixed_coop_capable(
	void)
{
	char const *setting = getenv("HALO_MIXED_COOP");

	return !(setting && setting[0] == '0');
}

/* whether this machine hosts a network game, or is joined to one (its lobby
too: the map is precached there, before the game's connection says so) -
not the main menu's search for games */
static boolean mixed_coop_in_network_game(
	void)
{
	struct network_game_client *client;

	if (game_connection() == _game_connection_network_client || game_connection() == _game_connection_network_server ||
		global_network_game_server_get())
	{
		return TRUE;
	}
	client = global_network_game_client_get();
	return client && network_game_client_get_state(client, NULL) >= 1 /* joining, pregame, in game, postgame */;
}

int mixed_coop_mcc_alias(
	char const *map_name,
	char *stem)
{
	int level = mixed_coop_level_index(map_name);

	if (stem)
		stem[0] = 0;
	/* (a network game's alone: alone, the campaign's MCC levels are the mods
	folder's mods\mcc_a10, saved apart) */
	if (level < 0 || !mixed_coop_capable() || !mixed_coop_in_network_game())
		return FALSE;
	if (mixed_coop_xbox_file(level) || !mixed_coop_mcc_file(level))
		return FALSE;
	if (stem)
		csstrncpy(stem, mixed_coop_level_stems[level], MCC_CONTENT_STEM_BYTES - 1);
	return TRUE;
}

int mixed_coop_host_campaign_name(
	char const *map_name,
	char *xbox_name,
	long size)
{
	char stem[MCC_CONTENT_STEM_BYTES];
	int level;

	if (!map_name || !xbox_name || size <= 0 || !mixed_coop_capable() || network_wire_crossplay() ||
		mcc_content_name_stem(tag_name_strip_path(map_name), stem) != _mcc_content_campaign)
	{
		return FALSE;
	}
	level = mixed_coop_level_index(stem);
	if (level < 0 || mixed_coop_level_is_multiplayer(level) || mixed_coop_xbox_file(level) || !mixed_coop_mcc_file(level))
		return FALSE;
	snprintf(xbox_name, (size_t)size, "levels\\%s\\%s", stem, stem);
	platform_log("mixed co-op: hosting MCC's %s under the Xbox's name (%s): Xbox and MCC machines can join", map_name,
		xbox_name);
	return TRUE;
}

static boolean mixed_coop_host_rename(
	char *map_name,
	long size,
	boolean said)
{
	int level = mixed_coop_level_index_mcc_name(map_name);
	char const *stem;

	/* (a map Halo PC rebuilt: an MCC host's game is MCC's own, never the Xbox's - mixed_coop_rebuilt_maps) */
	if (!mixed_coop_level_is_multiplayer(level) || mixed_coop_rebuilt_level(level) || !mixed_coop_capable() ||
		network_wire_crossplay())
	{
		return FALSE;
	}
	stem = mixed_coop_level_stems[level];
	/* (MCC's name, on a machine with no Xbox file of the map: else the
	host chose MCC's copy beside the Xbox's, MCC's own game) */
	if (!mixed_coop_has_mcc_prefix(tag_name_strip_path(map_name)) || mixed_coop_xbox_file(level) || !mixed_coop_mcc_file(level))
		return FALSE;
	if (said)
		platform_log("mixed play: hosting %s from MCC's files under the Xbox's name (levels\\test\\%s\\%s): Xbox machines "
			"can join", map_name, stem, stem);
	snprintf(map_name, (size_t)size, "levels\\test\\%s\\%s", stem, stem);
	return TRUE;
}

int mixed_coop_host_map_name(
	char *map_name,
	long size)
{
	return mixed_coop_host_rename(map_name, size, TRUE);
}

int mixed_coop_host_map_name_quiet(
	char *map_name,
	long size)
{
	return mixed_coop_host_rename(map_name, size, FALSE);
}

/* ---------- tags */

static struct cache_file_tag_instance_view
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
} *mixed_coop_instances(void)
{
	extern struct cache_file_tag_instance *global_tag_instances;

	return (struct cache_file_tag_instance_view *)global_tag_instances;
}

static struct cache_file_tag_instance_view *mixed_coop_instance(
	long tag_index)
{
	short absolute_index = (short)tag_index;

	if (tag_index == NONE || absolute_index < 0 || absolute_index >= halo_loaded_tag_count || !mixed_coop_instances())
		return NULL;
	return &mixed_coop_instances()[absolute_index];
}

static uint32_t mixed_coop_tag_path_hash(
	long tag_index)
{
	struct cache_file_tag_instance_view *instance = mixed_coop_instance(tag_index);

	/* (1.1.3: the flag stand's collision an Xbox machine sent this MCC host, named as the Xbox's) */
	if (!instance)
		instance = cache_runtime_tag_instance(tag_index);
	if (!instance || !instance->name)
		return 0xFFFFFFFFu;
	return mixed_coop_path_hash((uint32_t)instance->group_tag, instance->name);
}

static boolean mixed_coop_tag_is(
	long tag_index,
	char const *path)
{
	struct cache_file_tag_instance_view *instance = mixed_coop_instance(tag_index);

	return instance && instance->name && !csstrcasecmp(instance->name, path);
}

/* ---------- the digests' walk */

static struct tag_schema_group const *mixed_coop_schema_group(
	unsigned long group_tag,
	boolean custom_edition)
{
	struct tag_schema_group const *const *list;
	struct tag_schema_group const *group;

	if (custom_edition)
	{
		for (group = tag_schema_custom_edition_groups; group->group_tag; group++)
		{
			if (group->group_tag == group_tag)
				return group;
		}
	}
	for (list = tag_schema_group_lists; *list; list++)
	{
		for (group = *list; group->group_tag; group++)
		{
			if (group->group_tag == group_tag)
				return group;
		}
	}
	return NULL;
}

/* what the digests leave out or take loosely, each looked at in the Xbox and
MCC tags of the ten levels (Nov 22 2026, mixcoop-out/NOTES.md) and in what
the game reads: by the schema definition's name and a byte range in it */
enum
{
	/* not hashed: not read by the game, or read only to draw */
	_mixed_rule_skip = 'm',
	/* only these bits of a long hashed */
	_mixed_rule_bits = 'b',
	/* floats a tool worked out: their last bits differ (the same values one
	or two float steps apart): rounded to 12 bits of mantissa, near 0 as 0 */
	_mixed_rule_floats = 'f',
	/* an inertia tensor and its inverse: as floats, but what is near 0 next to
	its diagonal (below 0.01: the tools' leftovers off the diagonal, and the
	inverse, worked out from the tensor) as 0 */
	_mixed_rule_inertia = 'i',
};

struct mixed_coop_rule
{
	char const *definition;
	long begin;
	long end;
	char kind;
	uint32_t keep_bits;
};

static struct mixed_coop_rule const mixed_coop_rules_table[] =
{
	/* (unit: blip_type, the motion tracker's blip size, drawn only; padxxx;
	unused[3], where MCC keeps its metagame type and class) */
	{ "unit", 0x11c, 0x12c, _mixed_rule_skip, 0 },
	/* (a trigger's flags past _weapon_trigger_projectiles_have_identical_
	error_bit: client_side_only, which nothing reads, and Halo PC's
	use_original_unit_adjust_projectile_ray past the enum's end) */
	{ "weapon_trigger", 0x0, 0x4, _mixed_rule_bits, 0x1FFFu },
	/* (a device's runtime_* values: worked out from its times) */
	{ "device", 0x100, 0x114, _mixed_rule_skip, 0 },
	/* (the globals' multiplayer playlist block, its first person interface
	and HUD, the rasterizer's and interface's bitmaps, multiplayer's: drawn,
	or multiplayer's) */
	{ "game_globals", 0x1a0, 0x1ac, _mixed_rule_skip, 0 },
	{ "game_globals_first_person_interface", 0, 0x7FFFFFFF, _mixed_rule_skip, 0 },
	{ "game_globals_rasterizer_data", 0, 0x7FFFFFFF, _mixed_rule_skip, 0 },
	{ "game_globals_interface_tag_references", 0, 0x7FFFFFFF, _mixed_rule_skip, 0 },
	{ "game_globals_multiplayer_information", 0, 0x7FFFFFFF, _mixed_rule_skip, 0 },
	/* (a grenade's mp_spawn_default: multiplayer's) */
	{ "game_globals_grenade", 0x2, 0x4, _mixed_rule_skip, 0 },
	/* (worked out by the tools: falling damage's velocities, a physics'
	moments, centre of mass and inertia, a collision model's points and
	planes) */
	{ "game_globals_falling_damage", 0, 0x7FFFFFFF, _mixed_rule_floats, 0 },
	{ "physics", 0, 0x7FFFFFFF, _mixed_rule_floats, 0 },
	{ "inertial_matrix", 0, 0x7FFFFFFF, _mixed_rule_inertia, 0 },
	{ "collision_vertex", 0, 0x7FFFFFFF, _mixed_rule_floats, 0 },
	{ "real_plane3d", 0, 0x7FFFFFFF, _mixed_rule_floats, 0 },
	/* (the AI's pathfinding spheres: the host's AI alone) */
	{ "pathfinding_sphere", 0, 0x7FFFFFFF, _mixed_rule_skip, 0 },
	/* (a multiplayer map's netgame flags and equipment - mixed
	multiplayer's layout: their places and facings as the tools' floats;
	their unused longs and the equipment's runtime item index not at all) */
	{ "netgame_flag", 0x0, 0x10, _mixed_rule_floats, 0 },
	{ "netgame_flag", 0x14, 0x94, _mixed_rule_skip, 0 },
	{ "netgame_equipment", 0x10, 0x40, _mixed_rule_skip, 0 },
	{ "netgame_equipment", 0x40, 0x50, _mixed_rule_floats, 0 },
	{ "netgame_equipment", 0x54, 0x84, _mixed_rule_skip, 0 },
	/* (a control's on, off and denied effects, which no schema lists: their
	name pointers, which a loader points at its own empty string - an
	address in the executable, another in every build - not hashed; their
	groups and indices are) */
	{ "control_definition", 0x2ec, 0x2f0, _mixed_rule_skip, 0 },
	{ "control_definition", 0x2fc, 0x300, _mixed_rule_skip, 0 },
	{ "control_definition", 0x30c, 0x310, _mixed_rule_skip, 0 },
};

/* ---------- the owner's decisions (Nov 22 2026, 09:27 "All I want is the mcc maps to play with Xbox
maps": mixed play by default. Each one line below: its TRUE made FALSE refuses that difference again, a
mixed game naming the tag. Tests turn a FALSE one on with HALO_MIXED_DECISIONS=<name>,<name>..., and
HALO_MIXED_DECISIONS_OFF=<name>,... turns a TRUE one off) */

enum
{
	/* scenery\c_field_generator's collision model: MCC recompiled it, up
	to 0.19 wu off the Xbox's (a10 a50 b30 b40 c10 d20 d40, c40 in play;
	Rat Race). TRUE: played anyway - the host's collision decides (its
	simulation is the game's; a client's prediction near one is corrected
	to it); an MCC host's is MCC's, which it says in its log (default, r3) */
	_mixed_decision_field_generator,
	/* the flood carrier's autoaim width: Xbox 0, MCC 0.001 (c20 c40 d20
	d40). TRUE: a 4th Xbox value, 0 on MCC machines (mixed_coop_rules;
	default, r3) */
	_mixed_decision_flood_carrier_autoaim,
	/* a50's medium granite boulder's collision material type (Xbox 0 dirt,
	MCC 2 stone). TRUE: a 5th Xbox value, 0 on MCC machines
	(mixed_coop_rules), its collision's float noise played anyway (default,
	r3) */
	_mixed_decision_granite_boulder,
	/* multiplayer: the flag stand and teleporter base have no collision in
	MCC's files (the collision tag is nowhere in MCC). TRUE: dropped for
	everyone in mixed multiplayer (the Xbox machines' too: a cut) */
	_mixed_decision_flag_stand,
	/* multiplayer (Chill Out, Wizard): MCC's needler item collection spawns
	Halo PC's mp_needler. TRUE: the Xbox's needler on MCC machines, in every
	network game on these maps (default, the owner Nov 24 2026: an MCC host
	takes Xbox machines on Chill Out and Wizard; an older MCC build, which
	spawns mp_needler, then differs in the collection's digest and is refused) */
	_mixed_decision_needler,
	NUMBER_OF_MIXED_DECISIONS
};

static struct
{
	char const *name;
	boolean decided;
} const mixed_coop_decisions[NUMBER_OF_MIXED_DECISIONS] =
{
	{ "field_generator", TRUE },
	{ "flood_carrier_autoaim", TRUE },
	{ "granite_boulder", TRUE },
	{ "flag_stand", FALSE },
	{ "needler", TRUE },
};

static boolean mixed_coop_decided(
	int decision)
{
	char const *setting = getenv("HALO_MIXED_DECISIONS");
	size_t length = strlen(mixed_coop_decisions[decision].name);

	char const *off = getenv("HALO_MIXED_DECISIONS_OFF");

	/* (HALO_MIXED_DECISIONS_OFF: a default decision undone, for the tests and for anyone checking one) */
	while (off && *off)
	{
		char const *end = strchr(off, ',');
		size_t part = end ? (size_t)(end - off) : strlen(off);

		if (part == length && !strncmp(off, mixed_coop_decisions[decision].name, length))
			return FALSE;
		off = end ? end + 1 : NULL;
	}
	if (mixed_coop_decisions[decision].decided)
		return TRUE;
	while (setting && *setting)
	{
		char const *end = strchr(setting, ',');
		size_t part = end ? (size_t)(end - setting) : strlen(setting);

		if (part == length && !strncmp(setting, mixed_coop_decisions[decision].name, length))
			return TRUE;
		setting = end ? end + 1 : NULL;
	}
	return FALSE;
}

/* the tags a decision taken lets differ (played anyway, each machine its own) */
static struct
{
	int decision;
	char const *path;
} const mixed_coop_decision_tags[] =
{
	{ _mixed_decision_field_generator, "scenery\\c_field_generator\\c_field_generator" },
	{ _mixed_decision_granite_boulder, "scenery\\rocks\\boulder_granite_medium\\boulder_granite_medium" },
};

/* (mixed play r3, the Xbox-host-decides mode) what differs that the host alone uses or decides: with an
Xbox-files host it is the Xbox's game - the host's collision of the flag stand and teleporter base (MCC's
files have none; an MCC client near one is corrected to the host's), the host's needler item collection
(the host spawns every item); with an MCC host they would be Halo PC's, so a mixed game there needs an
Xbox-files host (refused, said so). The owner's call on the flag stand stays open (_mixed_decision_flag_stand) */
static char const *const mixed_coop_xbox_host_decides[] =
{
	"scenery\\flag_base\\flag_base", "scenery\\teleporter_base\\teleporter_base",
	"item collections\\single weapons\\needler",
};

static boolean mixed_coop_xbox_host_decides_tag(
	char const *name)
{
	short index;

	for (index = 0; name && index < NUMBEROF(mixed_coop_xbox_host_decides); index++)
	{
		if (!csstrcasecmp(name, mixed_coop_xbox_host_decides[index]))
			return TRUE;
	}
	return FALSE;
}

/* ---------- (1.1.3) whether what an MCC host's game lacks of the Xbox's can come into play */

/* (game_engine.c's netgame equipment types past the game engines: every game, every game but CTF, every game but
CTF and Race) */
enum
{
	_mixed_coop_game_type_all = 12,
	_mixed_coop_game_type_all_non_team,
	_mixed_coop_game_type_all_normal,
};

/* game_engine.c's match_game_type with a game engine: whether an entry of netgame or starting equipment of `types`
spawns in a game of `game_type` */
static boolean mixed_coop_game_type_matches(
	short const *types,
	long count,
	short game_type)
{
	long index;

	for (index = 0; index < count; index++)
	{
		if (types[index] == game_type || types[index] == _mixed_coop_game_type_all ||
			(types[index] == _mixed_coop_game_type_all_non_team && game_type != game_engine_ctf) ||
			(types[index] == _mixed_coop_game_type_all_normal && game_type != game_engine_ctf && game_type != game_engine_race))
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* the scenery placements of the scenario that place `name` (the engine places every one in every game: no game type
keeps one out, objects.c's object_new_from_scenario) */
static long mixed_coop_scenery_placed(
	struct scenario const *scenario,
	char const *name)
{
	/* (a scenery placement: the object's, then its permutation - scenery.c) */
	long const size = (long)(sizeof(struct scenario_object_datum) + sizeof(struct scenario_object_permutation));
	long index, count = 0;

	if (!scenario || !scenario->scenery.address || !scenario->scenery_palette.address)
		return 0;
	for (index = 0; index < scenario->scenery.count; index++)
	{
		struct scenario_object_datum const *datum =
			(struct scenario_object_datum const *)((byte const *)scenario->scenery.address + index * size);

		if (datum->palette_entry_index >= 0 && datum->palette_entry_index < scenario->scenery_palette.count &&
			mixed_coop_tag_is(((struct scenario_object_palette_entry const *)scenario->scenery_palette.address)
				[datum->palette_entry_index].reference.index, name))
		{
			count++;
		}
	}
	return count;
}

/* the game types (bit game_engine_ctf ... game_engine_race) in which the scenario's netgame or starting equipment
spawns the item collection `name` (game_engine.c's item spawns and custom starting equipment) */
static unsigned mixed_coop_collection_game_types(
	struct scenario const *scenario,
	char const *name)
{
	unsigned types = 0;
	short game_type;
	long index, slot;

	if (!scenario)
		return 0;
	for (game_type = game_engine_ctf; game_type <= game_engine_race; game_type++)
	{
		for (index = 0; scenario->netgame_equipment.address && index < scenario->netgame_equipment.count; index++)
		{
			struct scenario_netgame_equipment const *equipment =
				(struct scenario_netgame_equipment const *)scenario->netgame_equipment.address + index;

			if (mixed_coop_tag_is(equipment->item_collection.index, name) &&
				mixed_coop_game_type_matches(equipment->game_type, NUMBEROF(equipment->game_type), game_type))
			{
				types |= 1u << game_type;
			}
		}
		for (index = 0; scenario->scenario_starting_equipment.address && index < scenario->scenario_starting_equipment.count; index++)
		{
			struct scenario_starting_equipment const *equipment =
				(struct scenario_starting_equipment const *)scenario->scenario_starting_equipment.address + index;

			for (slot = 0; slot < NUMBEROF(equipment->item_collection); slot++)
			{
				if (mixed_coop_tag_is(equipment->item_collection[slot].index, name) &&
					mixed_coop_game_type_matches(equipment->game_type, NUMBEROF(equipment->game_type), game_type))
				{
					types |= 1u << game_type;
				}
			}
		}
	}
	return types;
}

/* whether the tag of mixed_coop_xbox_host_decides `name` (the Xbox's flag stand and teleporter base, which MCC's files
have no collision for; the needler item collection, which spawns Halo PC's mp_needler from MCC's) can come into play in
a game of `game_type` on the loaded level - an MCC host's game, an Xbox machine in it, refused only when one can (the
owner, Nov 24: no cut, refuse only where it matters); `why` (may be NULL) says where it is */
static boolean mixed_coop_xbox_host_tag_in_play(
	char const *name,
	short game_type,
	char *why,
	size_t why_size)
{
	struct cache_file_tag_instance_view *scenario_instance = mixed_coop_instance(mixed_coop.scenario_index);
	struct scenario const *scenario = scenario_instance ? scenario_instance->base_address : NULL;
	char const *dot;

	if (why && why_size)
		why[0] = 0;
	if (!name || !scenario)
		return TRUE;
	dot = strrchr(name, '\\');
	if (!strncasecmp(name, "item collections\\", 17))
	{
		unsigned types = mixed_coop_collection_game_types(scenario, name);

		if (why && why_size)
			snprintf(why, why_size, "%s spawns in %s game types (mask 0x%02x)", dot ? dot + 1 : name,
				types ? "some" : "no", types);
		/* (an unknown game type: as if it could) */
		return game_type < game_engine_ctf || game_type > game_engine_race || (types & (1u << game_type)) != 0;
	}
	{
		long placed = mixed_coop_scenery_placed(scenario, name);

		if (why && why_size)
			snprintf(why, why_size, "%s placed %ld times (every game type)", dot ? dot + 1 : name, placed);
		return placed != 0;
	}
}

/* a multiplayer map's level is loaded (its rules: mixed_coop_tags_loaded) */
static boolean mixed_coop_multiplayer_level;
/* its scenario has race vehicle flags (the globals' multiplayer vehicles can
come into play: game_engine_race.c) */
static boolean mixed_coop_race_vehicle_flags;

/* the Xbox's weapon list's length (game_engine.c's _weapon_list_*: the game
reads no entry past it - MCC's Halo PC globals append the plasma cannon and
mp_needler) and its flamethrower's entry (game_engine_remap_weapon makes
every multiplayer flamethrower a rocket launcher): what the globals' list
brings into play is what the game engine makes from it */
#define MIXED_COOP_XBOX_WEAPON_LIST_COUNT 14
#define MIXED_COOP_WEAPON_LIST_FLAMETHROWER 1
/* (and its gravity rifle's: no weapon set, custom loadout or remap makes
one - only a map's own item collection, reached as such) */
#define MIXED_COOP_WEAPON_LIST_GRAVITY_RIFLE 2

static struct mixed_coop_rule const *mixed_coop_rule_find(
	char const *definition,
	long offset)
{
	short index;

	for (index = 0; index < NUMBEROF(mixed_coop_rules_table); index++)
	{
		if (offset >= mixed_coop_rules_table[index].begin && offset < mixed_coop_rules_table[index].end &&
			!strcmp(definition, mixed_coop_rules_table[index].definition))
		{
			return &mixed_coop_rules_table[index];
		}
	}
	return NULL;
}

static uint32_t mixed_coop_round_float_near(
	uint32_t bits,
	real zero)
{
	real value;

	csmemcpy(&value, &bits, sizeof(value));
	if (!(value == value) || (value > -zero && value < zero))
		return 0;
	return (bits + (1u << 10)) & ~((1u << 11) - 1u);
}

static uint32_t mixed_coop_round_float(
	uint32_t bits)
{
	return mixed_coop_round_float_near(bits, 1e-5f);
}

static long mixed_coop_walk_budget;
static boolean mixed_coop_walk_custom_edition;

/* (HALO_MIXED_COOP_TRACE=<group>:<path part>) a tag's walk logged field by field */
static char const *mixed_coop_trace_filter;
static boolean mixed_coop_tracing;

static void mixed_coop_walk(uint32_t *hash, byte const *base, struct tag_schema_definition const *definition,
	int depth, char const *where);

static void mixed_coop_walk_field(
	uint32_t *hash,
	byte const *base,
	struct tag_schema_field const *field,
	byte *mask,
	long mask_size,
	int depth)
{
	short element;

	for (element = 0; element < field->count; element++)
	{
		long offset = field->offset + (long)element * field->size;
		byte const *at = base + offset;
		uint32_t before = *hash;

		/* (the field's bytes taken as what they mean: not again as bytes) */
		if (field->type != _tag_schema_block_index && field->type != _tag_schema_enum && field->type != _tag_schema_check &&
			offset >= 0 && offset + field->size <= mask_size)
		{
			memset(mask + offset, 1, (size_t)field->size);
		}
		/* (what the game preloads for a tag - its predicted resources - is
		no rule of play: MCC's name its own bitmaps and sounds) */
		if (field->type == _tag_schema_block && field->name && !strcmp(field->name, "predicted_resources"))
			continue;
		switch (field->type)
		{
		case _tag_schema_block:
		{
			struct tag_block const *block = (struct tag_block const *)at;
			struct tag_schema_definition const *element_definition = field->definition;
			long count = block->count, index;

			/* (multiplayer: the globals' weapon list as far as the game reads
			it) */
			if (mixed_coop_multiplayer_level && field->name && !strcmp(field->name, "weapon_list"))
				count = MIN(count, MIXED_COOP_XBOX_WEAPON_LIST_COUNT);
			*hash = mixed_coop_fnv(*hash, &count, sizeof(count));
			if (mixed_coop_tracing)
				platform_log("mixed co-op trace: %*s@block %s %ld", depth * 2, "", field->name ? field->name : "?", count);
			if (!element_definition || count <= 0 || !block->address || depth >= MIXED_COOP_WALK_DEPTH)
				break;
			for (index = 0; index < count && mixed_coop_walk_budget > 0; index++)
			{
				mixed_coop_walk_budget--;
				mixed_coop_walk(hash, (byte const *)block->address + index * element_definition->size, element_definition,
					depth + 1, field->name);
			}
			break;
		}
		case _tag_schema_data:
		{
			struct tag_data const *data = (struct tag_data const *)at;
			long size = data->size;

			*hash = mixed_coop_fnv(*hash, &size, sizeof(size));
			if (size > 0 && data->address && size <= 0x400000)
				*hash = mixed_coop_fnv(*hash, data->address, (uint32_t)size);
			if (mixed_coop_tracing)
				platform_log("mixed co-op trace: %*s@data %s %ld %08x", depth * 2, "", field->name ? field->name : "?", size,
					(unsigned)(size > 0 && data->address && size <= 0x400000 ?
						mixed_coop_fnv(MIXED_COOP_FNV_BASIS, data->address, (uint32_t)size) : 0));
			break;
		}
		case _tag_schema_file_data:
		{
			long size = ((struct tag_data const *)at)->size;

			*hash = mixed_coop_fnv(*hash, &size, sizeof(size));
			break;
		}
		case _tag_schema_reference:
		{
			long referenced = ((struct tag_reference const *)at)->index;
			uint32_t path = mixed_coop_tag_path_hash(referenced);

			/* (a projectile's material response's potential effect: drawn
			only - MCC's fuel rod has a splash on water the Xbox's has not) */
			if (field->name && !strcmp(field->name, "potential_effect"))
				break;
			*hash = mixed_coop_fnv(*hash, &path, sizeof(path));
			if (mixed_coop_tracing)
			{
				struct cache_file_tag_instance_view *instance = mixed_coop_instance(referenced);

				platform_log("mixed co-op trace: %*s@ref %s %s", depth * 2, "", field->name ? field->name : "?",
					instance && instance->name ? instance->name : "NONE");
			}
			break;
		}
		case _tag_schema_tag_index:
		{
			long index;
			uint32_t path;

			csmemcpy(&index, at, sizeof(index));
			path = mixed_coop_tag_path_hash(index);
			*hash = mixed_coop_fnv(*hash, &path, sizeof(path));
			if (mixed_coop_tracing)
			{
				struct cache_file_tag_instance_view *instance = mixed_coop_instance(index);

				platform_log("mixed co-op trace: %*s@ref %s %s", depth * 2, "", field->name ? field->name : "?",
					instance && instance->name ? instance->name : "NONE");
			}
			break;
		}
		case _tag_schema_string:
		{
			long length = 0;

			while (length < field->size && at[length])
				length++;
			*hash = mixed_coop_fnv(*hash, at, (uint32_t)length);
			break;
		}
		case _tag_schema_struct:
			if (field->definition && depth < MIXED_COOP_WALK_DEPTH)
				mixed_coop_walk(hash, at, field->definition, depth + 1, field->name);
			break;
		default:
			/* (block indices and enums: their bytes, as the plain numbers;
			runtime values: nothing) */
			break;
		}
		if (mixed_coop_tracing && *hash != before)
			platform_log("mixed co-op trace: %*s%s[%d] -> %08x", depth * 2, "", field->name ? field->name : "?", element,
				(unsigned)*hash);
	}
}

static void mixed_coop_walk(
	uint32_t *hash,
	byte const *base,
	struct tag_schema_definition const *definition,
	int depth,
	char const *where)
{
	byte mask[MIXED_COOP_MASK_BYTES];
	long size = definition->size;
	struct tag_schema_field const *field;
	long offset, run;

	(void)where;
	if (size <= 0)
		return;
	{
		struct mixed_coop_rule const *rule = mixed_coop_rule_find(definition->name, 0);

		if (rule && rule->kind == _mixed_rule_skip && rule->begin == 0 && rule->end >= definition->size)
			return;
	}
	if (size > MIXED_COOP_MASK_BYTES)
		size = MIXED_COOP_MASK_BYTES;
	memset(mask, 0, (size_t)size);
	for (field = definition->fields; field && field->type != _tag_schema_terminator; field++)
	{
		struct mixed_coop_rule const *rule = mixed_coop_rule_find(definition->name, field->offset);

		/* (a field the rules leave out: its bytes neither hashed nor walked) */
		if (rule && rule->kind == _mixed_rule_skip)
		{
			if (field->offset >= 0 && field->offset + (long)field->size * field->count <= size)
				memset(mask + field->offset, 1, (size_t)field->size * (size_t)field->count);
			continue;
		}
		mixed_coop_walk_field(hash, base, field, mask, size, depth);
	}
	/* (the plain numbers and flags: every byte not taken above, in runs) */
	for (offset = 0; offset < size; offset = run)
	{
		uint32_t before = *hash;

		run = offset;
		while (run < size && !mask[run])
			run++;
		if (run > offset)
		{
			long at;

			/* (the rules: a word at a time where one applies) */
			for (at = offset; at < run; )
			{
				struct mixed_coop_rule const *rule = mixed_coop_rule_find(definition->name, at);

				if (rule && at + 4 <= run && (at & 3) == 0)
				{
					uint32_t word;

					csmemcpy(&word, base + at, 4);
					if (rule->kind == _mixed_rule_bits)
						word &= rule->keep_bits;
					else if (rule->kind == _mixed_rule_floats)
						word = mixed_coop_round_float(word);
					else if (rule->kind == _mixed_rule_inertia)
						word = mixed_coop_round_float_near(word, 0.01f);
					if (rule->kind != _mixed_rule_skip)
						*hash = mixed_coop_fnv(*hash, &word, 4);
					at += 4;
				}
				else if (rule && rule->kind == _mixed_rule_skip)
				{
					at++;
				}
				else
				{
					*hash = mixed_coop_fnv(*hash, base + at, 1);
					at++;
				}
			}
			if (mixed_coop_tracing)
			{
				char text[2 * 600 + 1];
				long index;

				text[0] = 0;
				for (index = offset; index < run && index < offset + 600; index++)
					snprintf(text + 2 * (index - offset), 3, "%02x", base[index]);
				platform_log("mixed co-op trace: %*s%s +0x%lx..0x%lx: %s-> %08x (was %08x)", depth * 2, "",
					definition->name, offset, run, text, (unsigned)*hash, (unsigned)before);
			}
		}
		while (run < size && mask[run])
			run++;
	}
}

/* ---------- what can come into play: the tags reachable from the scenario's
placed objects' palette entries, its AI, its starting profiles and equipment,
its scripts' references and the globals, every reference followed (a tag in
the map that nothing placed or run names - c20's field generator, the
flamethrower of c20 and d20 - is not compared) */

static char const *const mixed_coop_placement_blocks[9][2];
static uint8_t *mixed_coop_reach;
static long *mixed_coop_reach_queue;
static long *mixed_coop_reach_parent;
static long mixed_coop_reach_current = NONE;
static long mixed_coop_reach_queued;

static void mixed_coop_reach_tag(
	long tag_index)
{
	short absolute_index = (short)tag_index;

	if (tag_index == NONE || absolute_index < 0 || absolute_index >= halo_loaded_tag_count || !mixed_coop_reach ||
		mixed_coop_reach[absolute_index])
	{
		return;
	}
	mixed_coop_reach[absolute_index] = 1;
	if (mixed_coop_reach_parent)
		mixed_coop_reach_parent[absolute_index] = mixed_coop_reach_current;
	mixed_coop_reach_queue[mixed_coop_reach_queued++] = absolute_index;
}

static void mixed_coop_reach_walk(
	byte const *base,
	struct tag_schema_definition const *definition,
	int depth)
{
	struct tag_schema_field const *field;

	for (field = definition->fields; field && field->type != _tag_schema_terminator; field++)
	{
		short element;

		for (element = 0; element < field->count; element++)
		{
			byte const *at = base + field->offset + (long)element * field->size;

			switch (field->type)
			{
			case _tag_schema_reference:
				mixed_coop_reach_tag(((struct tag_reference const *)at)->index);
				break;
			case _tag_schema_tag_index:
			{
				long index;

				csmemcpy(&index, at, sizeof(index));
				mixed_coop_reach_tag(index);
				break;
			}
			case _tag_schema_block:
			{
				struct tag_block const *block = (struct tag_block const *)at;
				struct tag_schema_definition const *element_definition = field->definition;
				long index;

				long count = block->count;
				boolean weapon_list = FALSE;

				if (!element_definition || block->count <= 0 || !block->address || depth >= MIXED_COOP_WALK_DEPTH)
					break;
				/* (multiplayer: the globals' weapons the game can make - not the
				flamethrower, nor past the Xbox's list -, and their multiplayer
				vehicles only where a race can make one) */
				if (mixed_coop_multiplayer_level && field->name)
				{
					if (!strcmp(field->name, "weapon_list") && !strcmp(definition->name, "game_globals"))
					{
						weapon_list = TRUE;
						count = MIN(count, MIXED_COOP_XBOX_WEAPON_LIST_COUNT);
					}
					else if (!strcmp(field->name, "vehicles") && !strcmp(definition->name, "game_globals_multiplayer_information") &&
						!mixed_coop_race_vehicle_flags)
					{
						break;
					}
				}
				for (index = 0; index < count && mixed_coop_walk_budget > 0; index++, mixed_coop_walk_budget--)
				{
					if (weapon_list && (index == MIXED_COOP_WEAPON_LIST_FLAMETHROWER || index == MIXED_COOP_WEAPON_LIST_GRAVITY_RIFLE))
						continue;
					mixed_coop_reach_walk((byte const *)block->address + index * element_definition->size, element_definition,
						depth + 1);
				}
				break;
			}
			case _tag_schema_struct:
				if (field->definition && depth < MIXED_COOP_WALK_DEPTH)
					mixed_coop_reach_walk(at, field->definition, depth + 1);
				break;
			default:
				break;
			}
		}
	}
}

static void mixed_coop_reach_scenario_block(
	struct scenario const *scenario,
	struct tag_schema_definition const *scenario_schema,
	char const *name)
{
	struct tag_schema_field const *field;

	for (field = scenario_schema->fields; field && field->type != _tag_schema_terminator; field++)
	{
		if (field->type == _tag_schema_block && field->name && !strcmp(field->name, name))
		{
			mixed_coop_walk_budget = MIXED_COOP_WALK_BUDGET;
			mixed_coop_reach_walk((byte const *)scenario, &(struct tag_schema_definition){ "root", 0,
				(struct tag_schema_field const []){ *field, TAG_SCHEMA_END } }, 0);
		}
	}
}

static long mixed_coop_compute_reach(
	struct scenario const *scenario,
	struct tag_schema_definition const *scenario_schema)
{
	static char const *const roots[] =
	{
		"starting_profiles", "scenario_starting_equipment", "hs_references", "ai_animation_references",
		"device_groups",
	};
	short index;
	long at, reached = 0;

	mixed_coop_free(mixed_coop_reach);
	mixed_coop_free(mixed_coop_reach_queue);
	mixed_coop_free(mixed_coop_reach_parent);
	mixed_coop_reach_parent = malloc(sizeof(long) * (size_t)MAX(1, halo_loaded_tag_count));
	mixed_coop_reach_current = NONE;
	/* (the game's malloc, as mixed_coop_free frees: libc's calloc is not
	the game's debug allocator, and its block freed here at the next level
	corrupted the heap) */
	mixed_coop_reach = malloc((size_t)MAX(1, halo_loaded_tag_count));
	if (mixed_coop_reach)
		csmemset(mixed_coop_reach, 0, (size_t)MAX(1, halo_loaded_tag_count));
	mixed_coop_reach_queue = malloc(sizeof(long) * (size_t)MAX(1, halo_loaded_tag_count));
	mixed_coop_reach_queued = 0;
	if (!mixed_coop_reach || !mixed_coop_reach_queue)
		return 0;
	/* (each placed object's palette entry) */
	for (index = 0; index < NUMBEROF(mixed_coop_placement_blocks); index++)
	{
		struct tag_schema_field const *field;
		struct tag_block const *placements = NULL, *palette = NULL;
		long element_size = 0, element;

		for (field = scenario_schema->fields; field && field->type != _tag_schema_terminator; field++)
		{
			if (field->type != _tag_schema_block || !field->name)
				continue;
			if (!strcmp(field->name, mixed_coop_placement_blocks[index][0]))
			{
				placements = (struct tag_block const *)((byte const *)scenario + field->offset);
				element_size = field->definition ? ((struct tag_schema_definition const *)field->definition)->size : 0;
			}
			else if (!strcmp(field->name, mixed_coop_placement_blocks[index][1]))
			{
				palette = (struct tag_block const *)((byte const *)scenario + field->offset);
			}
		}
		if (!placements || !palette || !placements->address || !palette->address || element_size <= 0)
			continue;
		for (element = 0; element < placements->count; element++)
		{
			short palette_index = ((struct scenario_object_datum const *)((byte const *)placements->address +
				element * element_size))->palette_entry_index;

			if (palette_index >= 0 && palette_index < palette->count)
				mixed_coop_reach_tag(((struct scenario_object_palette_entry const *)palette->address)[palette_index].reference.index);
		}
	}
	for (index = 0; index < NUMBEROF(roots); index++)
		mixed_coop_reach_scenario_block(scenario, scenario_schema, roots[index]);
	/* (the AI's actor variants that a squad places: a palette entry no squad
	names - c20's and d20's flood elite with a flamethrower - comes into play
	only by a script's name, hs_references) */
	{
		long encounter;

		for (encounter = 0; encounter < scenario->ai_encounters.count && scenario->ai_encounters.address; encounter++)
		{
			struct encounter_definition const *definition =
				(struct encounter_definition const *)scenario->ai_encounters.address + encounter;
			long squad;

			for (squad = 0; squad < definition->squads.count && definition->squads.address; squad++)
			{
				struct squad_definition const *squad_definition = (struct squad_definition const *)definition->squads.address + squad;
				short palette_index = squad_definition->actor_palette_index;
				long location;

				if (palette_index >= 0 && palette_index < scenario->ai_actor_palette.count && scenario->ai_actor_palette.address)
					mixed_coop_reach_tag(((struct tag_reference const *)scenario->ai_actor_palette.address)[palette_index].index);
				/* (a starting location's own actor variant) */
				for (location = 0; location < squad_definition->starting_locations.count && squad_definition->starting_locations.address;
					location++)
				{
					palette_index = ((struct actor_starting_location const *)squad_definition->starting_locations.address)[location].actor_variant_index;
					if (palette_index >= 0 && palette_index < scenario->ai_actor_palette.count && scenario->ai_actor_palette.address)
						mixed_coop_reach_tag(((struct tag_reference const *)scenario->ai_actor_palette.address)[palette_index].index);
				}
			}
		}
	}
	/* (the globals) */
	for (at = 0; at < halo_loaded_tag_count; at++)
	{
		if (mixed_coop_instances()[at].group_tag == 'matg')
			mixed_coop_reach_tag(mixed_coop_instances()[at].tag_index);
	}
	/* (every reference of every tag reached, until none is new) */
	for (at = 0; at < mixed_coop_reach_queued; at++)
	{
		struct cache_file_tag_instance_view *instance = &mixed_coop_instances()[mixed_coop_reach_queue[at]];
		struct tag_schema_group const *group = mixed_coop_schema_group((unsigned long)instance->group_tag,
			mixed_coop_walk_custom_edition);

		if (!instance->base_address || !group || !group->definition)
			continue;
		mixed_coop_walk_budget = MIXED_COOP_WALK_BUDGET;
		mixed_coop_reach_current = mixed_coop_reach_queue[at];
		mixed_coop_reach_walk((byte const *)instance->base_address, group->definition, 0);
	}
	mixed_coop_reach_current = NONE;
	/* (HALO_MIXED_COOP_REACH=<path part>: how a tag came into play) */
	if (getenv("HALO_MIXED_COOP_REACH") && mixed_coop_reach_parent)
	{
		for (at = 0; at < halo_loaded_tag_count; at++)
		{
			struct cache_file_tag_instance_view *instance = &mixed_coop_instances()[at];
			long step = at;
			short guard = 0;

			if (!mixed_coop_reach[at] || !instance->name || !strstr(instance->name, getenv("HALO_MIXED_COOP_REACH")))
				continue;
			while (step != NONE && guard++ < 16)
			{
				platform_log("mixed reach: %s <- %s", mixed_coop_instances()[step].name ? mixed_coop_instances()[step].name : "?",
					mixed_coop_reach_parent[step] != NONE && mixed_coop_instances()[mixed_coop_reach_parent[step]].name ?
						mixed_coop_instances()[mixed_coop_reach_parent[step]].name : "(a root)");
				step = mixed_coop_reach_parent[step];
			}
		}
	}
	for (at = 0; at < halo_loaded_tag_count; at++)
		reached += mixed_coop_reach[at] != 0;
	return reached;
}

static int mixed_coop_tag_kind(
	unsigned long group_tag,
	unsigned long parent_group_tag)
{
	switch (group_tag)
	{
	case 'jpt!': case 'phys': case 'coll': case 'matg': case 'itmc':
		return _mixed_tag_gameplay;
	/* (point physics: particles, contrails and flames only - the tools'
	derived constants a float step apart in MCC's - and what only shows or
	the host alone runs) */
	case 'pphy': case 'effe': case 'antr': case 'actr': case 'actv':
		return _mixed_tag_told;
	/* (models: drawn; a gbxmodel's runtime parts differ between two loads of
	one file) */
	case 'mod2': case 'mode':
		return _mixed_tag_other;
	case 'ssce': case 'lifi':
		return _mixed_tag_other;
	}
	if (parent_group_tag == 'obje' || group_tag == 'obje' || parent_group_tag == 'unit' || parent_group_tag == 'item' ||
		parent_group_tag == 'devi')
	{
		return _mixed_tag_gameplay;
	}
	return _mixed_tag_other;
}

static uint32_t mixed_coop_tag_digest(
	long absolute_index)
{
	struct cache_file_tag_instance_view *instance = &mixed_coop_instances()[absolute_index];
	struct tag_schema_group const *group;
	uint32_t hash = MIXED_COOP_FNV_BASIS;

	if (!instance->base_address)
		return 0;
	group = mixed_coop_schema_group((unsigned long)instance->group_tag, mixed_coop_walk_custom_edition);
	if (!group || !group->definition)
		return 0;
	mixed_coop_tracing = FALSE;
	if (mixed_coop_trace_filter && instance->name)
	{
		char text[300];

		snprintf(text, sizeof(text), "%c%c%c%c:%s", (char)(instance->group_tag >> 24), (char)(instance->group_tag >> 16),
			(char)(instance->group_tag >> 8), (char)instance->group_tag, instance->name);
		{
			static char filters[65536];
			char *filter, *state = NULL;

			snprintf(filters, sizeof(filters), "%s", mixed_coop_trace_filter);
			for (filter = strtok_r(filters, "|", &state); filter && !mixed_coop_tracing; filter = strtok_r(NULL, "|", &state))
				mixed_coop_tracing = !strcmp(text, filter);
		}
		if (mixed_coop_tracing)
			platform_log("mixed co-op trace: %s", text);
	}
	mixed_coop_walk_budget = MIXED_COOP_WALK_BUDGET;
	mixed_coop_walk(&hash, (byte const *)instance->base_address, group->definition, 0, "root");
	mixed_coop_tracing = FALSE;
	/* (never 0: 0 is a tag not compared) */
	return hash ? hash : 1;
}

/* ---------- the scenario: MCC's made the Xbox's */

static boolean mixed_coop_palette_entry_is_extra(
	struct tag_block const *palette,
	short palette_index)
{
	struct scenario_object_palette_entry const *entry;
	short index;

	if (palette_index < 0 || palette_index >= palette->count || !palette->address)
		return FALSE;
	entry = (struct scenario_object_palette_entry const *)palette->address + palette_index;
	for (index = 0; index < NUMBEROF(mixed_coop_mcc_extra_objects); index++)
	{
		if (mixed_coop_tag_is(entry->reference.index, mixed_coop_mcc_extra_objects[index]))
			return TRUE;
	}
	return FALSE;
}

/* a placement block's MCC-only entries taken out (the rest moved up, in
their order), and the palette's MCC-only entries past every other cut */
static long mixed_coop_remove_extras(
	struct tag_block *placements,
	long element_size,
	struct tag_block *palette)
{
	long read, write = 0, removed = 0;

	if (!placements->address || placements->count <= 0)
		return 0;
	for (read = 0; read < placements->count; read++)
	{
		byte *element = (byte *)placements->address + read * element_size;
		struct scenario_object_datum const *datum = (struct scenario_object_datum const *)element;

		if (mixed_coop_palette_entry_is_extra(palette, datum->palette_entry_index))
		{
			removed++;
			continue;
		}
		if (write != read)
			memmove((byte *)placements->address + write * element_size, element, (size_t)element_size);
		write++;
	}
	placements->count = write;
	/* (the palette: its last entries, while they are MCC's and no placement
	names them) */
	while (palette->count > 0 && mixed_coop_palette_entry_is_extra(palette, (short)(palette->count - 1)))
	{
		short last = (short)(palette->count - 1);
		boolean used = FALSE;

		for (read = 0; read < placements->count && !used; read++)
			used = ((struct scenario_object_datum const *)((byte *)placements->address + read * element_size))->palette_entry_index == last;
		if (used)
			break;
		palette->count--;
	}
	return removed;
}

static boolean mixed_coop_script_is_mcc_only(
	char const *name)
{
	short index;

	for (index = 0; index < NUMBEROF(mixed_coop_mcc_only_script_prefixes); index++)
	{
		if (!strncmp(name, mixed_coop_mcc_only_script_prefixes[index], strlen(mixed_coop_mcc_only_script_prefixes[index])))
			return TRUE;
	}
	return FALSE;
}

/* the scenario's element sizes, by the schema's definition of each block */
static long mixed_coop_scenario_element_size(
	struct tag_schema_definition const *scenario_schema,
	char const *name)
{
	struct tag_schema_field const *field;

	for (field = scenario_schema->fields; field && field->type != _tag_schema_terminator; field++)
	{
		if (field->type == _tag_schema_block && field->name && !strcmp(field->name, name) && field->definition)
			return ((struct tag_schema_definition const *)field->definition)->size;
	}
	return 0;
}

static void mixed_coop_make_xbox_scenario(
	struct scenario *scenario,
	struct tag_schema_definition const *scenario_schema)
{
	static struct
	{
		char const *placements;
		size_t placements_offset;
		size_t palette_offset;
	} const blocks[] =
	{
		{ "scenery", offsetof(struct scenario, scenery), offsetof(struct scenario, scenery_palette) },
		{ "bipeds", offsetof(struct scenario, bipeds), offsetof(struct scenario, biped_palette) },
		{ "vehicles", offsetof(struct scenario, vehicles), offsetof(struct scenario, vehicle_palette) },
		{ "equipment", offsetof(struct scenario, equipment), offsetof(struct scenario, equipment_palette) },
		{ "weapons", offsetof(struct scenario, weapons), offsetof(struct scenario, weapon_palette) },
		{ "machines", offsetof(struct scenario, machines), offsetof(struct scenario, machine_palette) },
		{ "controls", offsetof(struct scenario, controls), offsetof(struct scenario, control_palette) },
		{ "light_fixtures", offsetof(struct scenario, light_fixtures), offsetof(struct scenario, light_fixtures_palette) },
		{ "sound_scenery", offsetof(struct scenario, sound_scenery), offsetof(struct scenario, sound_scenery_palette) },
	};
	short index;
	long script_index;

	for (index = 0; index < NUMBEROF(blocks); index++)
	{
		long element_size = mixed_coop_scenario_element_size(scenario_schema, blocks[index].placements);
		long removed;

		if (element_size < (long)sizeof(struct scenario_object_datum))
			continue;
		removed = mixed_coop_remove_extras((struct tag_block *)((byte *)scenario + blocks[index].placements_offset),
			element_size, (struct tag_block *)((byte *)scenario + blocks[index].palette_offset));
		if (removed)
			platform_log("mixed co-op: %ld MCC-only %s not placed (the Xbox's placements, at the Xbox's object indices)",
				removed, blocks[index].placements);
		mixed_coop.extras_removed += removed;
	}
	/* (multiplayer: Halo PC's added equipment spawns) */
	if (mixed_coop_multiplayer_level && scenario->netgame_equipment.address)
	{
		long read, write = 0, removed = 0;

		for (read = 0; read < scenario->netgame_equipment.count; read++)
		{
			struct scenario_netgame_equipment *entry = (struct scenario_netgame_equipment *)scenario->netgame_equipment.address + read;
			boolean extra = FALSE;

			for (index = 0; index < NUMBEROF(mixed_coop_mcc_extra_item_collections); index++)
				extra |= mixed_coop_tag_is(entry->item_collection.index, mixed_coop_mcc_extra_item_collections[index]);
			if (extra)
			{
				removed++;
				continue;
			}
			if (write != read)
				memmove((struct scenario_netgame_equipment *)scenario->netgame_equipment.address + write, entry, sizeof(*entry));
			write++;
		}
		scenario->netgame_equipment.count = write;
		if (removed)
			platform_log("mixed play: %ld equipment spawns Halo PC added not spawned (the flamethrower's, the plasma cannon's)",
				removed);
		mixed_coop.extras_removed += removed;
	}
	for (script_index = 0; script_index < scenario->hs_scripts.count && scenario->hs_scripts.address; script_index++)
	{
		struct hs_script *script = (struct hs_script *)scenario->hs_scripts.address + script_index;
		char name[sizeof(script->name) + 1];

		csmemcpy(name, script->name, sizeof(script->name));
		name[sizeof(script->name)] = 0;
		if (mixed_coop_script_is_mcc_only(name) &&
			(script->script_type == _hs_script_startup || script->script_type == _hs_script_dormant ||
				script->script_type == _hs_script_continuous))
		{
			script->script_type = _hs_script_static;
			mixed_coop.scripts_disabled++;
			platform_log("mixed co-op: MCC's script '%s' is not started (the Xbox has none)", name);
		}
	}
}

/* ---------- the Xbox rules profile (the owner, Nov 14 2026: these three of
the Xbox's values in code on a machine playing MCC's files in a mixed game;
any other gameplay field the digests find different is asked before it is
added here) */

static long mixed_coop_find_tag(
	long group_tag,
	char const *path)
{
	long index;

	for (index = 0; index < halo_loaded_tag_count; index++)
	{
		struct cache_file_tag_instance_view *instance = &mixed_coop_instances()[index];

		if (instance->group_tag == group_tag && instance->name && !csstrcasecmp(instance->name, path))
			return index;
	}
	return NONE;
}

/* the owner's decisions taken that change a machine's tags (mixed_coop_decisions):
the flag stand and teleporter base without collision (both families: MCC's
have none), the Xbox's needler in MCC's needler item collection */
static void mixed_coop_decisions_apply(
	void)
{
	static char const *const collisionless[] =
	{
		"scenery\\flag_base\\flag_base", "scenery\\teleporter_base\\teleporter_base",
	};
	short index;

	if (!mixed_coop_multiplayer_level)
		return;
	if (mixed_coop_decided(_mixed_decision_flag_stand))
	{
		for (index = 0; index < NUMBEROF(collisionless); index++)
		{
			long tag = mixed_coop_find_tag('scen', collisionless[index]);
			struct object_definition *definition = tag != NONE ? mixed_coop_instances()[tag].base_address : NULL;

			if (definition && definition->object.collision_model.index != NONE)
			{
				platform_log("mixed play: the owner's decision: %s without collision (as MCC's)", collisionless[index]);
				definition->object.collision_model.index = NONE;
				mixed_coop.rules_applied++;
			}
		}
	}
	if (mixed_coop.family == _mixed_coop_family_mcc && mixed_coop_decided(_mixed_decision_needler))
	{
		long collection = mixed_coop_find_tag('itmc', "item collections\\single weapons\\needler");
		long xbox_needler = mixed_coop_find_tag('weap', "weapons\\needler\\needler");
		struct item_collection_definition *definition = collection != NONE ? mixed_coop_instances()[collection].base_address : NULL;
		long permutation;

		for (permutation = 0; definition && xbox_needler != NONE && permutation < definition->permutations.count &&
			definition->permutations.address; permutation++)
		{
			struct item_permutation_definition *entry = (struct item_permutation_definition *)definition->permutations.address +
				permutation;

			if (mixed_coop_tag_is(entry->item.index, "weapons\\needler\\mp_needler"))
			{
				platform_log("mixed play: the owner's decision: MCC's needler item collection spawns the Xbox's needler, "
					"not mp_needler");
				entry->item.index = mixed_coop_instances()[xbox_needler].tag_index;
				mixed_coop.rules_applied++;
			}
		}
	}
}

static void mixed_coop_rules(
	void)
{
	long index;

	for (index = 0; index < halo_loaded_tag_count; index++)
	{
		struct cache_file_tag_instance_view *instance = &mixed_coop_instances()[index];

		if (!instance->base_address || !instance->name)
			continue;
		/* the rocket launcher: slower when zoomed or reloading (MCC: never) */
		if (instance->group_tag == 'weap' && !csstrcasecmp(instance->name, "weapons\\rocket launcher\\rocket launcher"))
		{
			struct weapon_definition *weapon = instance->base_address;

			platform_log("mixed co-op: Xbox rules: the rocket launcher's movement penalty %d %.3f %.3f -> 2 0.250 0.250",
				weapon->weapon.movement_penalty_mode, weapon->weapon.forward_movement_penalty,
				weapon->weapon.sideways_movement_penalty);
			weapon->weapon.movement_penalty_mode = 2;
			weapon->weapon.forward_movement_penalty = 0.25f;
			weapon->weapon.sideways_movement_penalty = 0.25f;
			mixed_coop.rules_applied++;
		}
		/* the Warthog's ground friction */
		else if (instance->group_tag == 'phys' && !csstrcasecmp(instance->name, "vehicles\\warthog\\warthog"))
		{
			struct physics_definition *physics = instance->base_address;

			platform_log("mixed co-op: Xbox rules: the Warthog's ground friction %.3f -> 0.200", physics->ground_friction);
			physics->ground_friction = 0.2f;
			mixed_coop.rules_applied++;
		}
		/* the Banshee's fuel rod: its damage's range in the air */
		else if (instance->group_tag == 'proj' && !csstrcasecmp(instance->name, "vehicles\\banshee\\banshee fuel rod"))
		{
			struct projectile_definition *projectile = instance->base_address;

			platform_log("mixed co-op: Xbox rules: the Banshee fuel rod's air damage range %.3f..%.3f -> %.3f..30.000",
				projectile->projectile.air_damage_range_lower_bound, projectile->projectile.air_damage_range_upper_bound,
				projectile->projectile.air_damage_range_lower_bound);
			projectile->projectile.air_damage_range_upper_bound = 30.0f;
			mixed_coop.rules_applied++;
		}
		/* (pending: the owner's decision, _mixed_decision_flood_carrier_autoaim)
		the flood carrier's autoaim width */
		else if (instance->group_tag == 'bipd' && !csstrcasecmp(instance->name, "characters\\floodcarrier\\floodcarrier") &&
			mixed_coop_decided(_mixed_decision_flood_carrier_autoaim))
		{
			struct biped_definition *biped = instance->base_address;

			platform_log("mixed co-op: Xbox rules: the flood carrier's autoaim width %.4f -> 0", biped->biped.autoaim_width);
			biped->biped.autoaim_width = 0.0f;
			mixed_coop.rules_applied++;
		}
		/* (the owner's decision, _mixed_decision_field_generator) the field generator's collision:
		MCC's own, said - the host's decides for every machine */
		else if (instance->group_tag == 'coll' &&
			!csstrcasecmp(instance->name, "scenery\\c_field_generator\\c_field_generator") &&
			mixed_coop_decided(_mixed_decision_field_generator))
		{
			platform_log("mixed play: the field generator's collision is MCC's on this machine (up to 0.19 wu off the "
				"Xbox's): as host, its collision decides for every machine; joining an Xbox host, the host's decides "
				"and a player near one is corrected to it");
		}
		/* (the owner's decision, _mixed_decision_granite_boulder) a50's medium granite boulder's
		collision material type: the Xbox's (0, dirt; MCC's 2, stone - what a shot or grenade
		against it does, its material's responses) */
		else if (instance->group_tag == 'coll' &&
			!csstrcasecmp(instance->name, "scenery\\rocks\\boulder_granite_medium\\boulder_granite_medium") &&
			mixed_coop_decided(_mixed_decision_granite_boulder))
		{
			struct collision_model *collision = instance->base_address;
			struct damage_resistance_material *materials = collision->resistance.materials.address;
			long material;

			for (material = 0; materials && material < collision->resistance.materials.count; material++)
			{
				if (materials[material].material_type == 2)
				{
					platform_log("mixed co-op: Xbox rules: the medium granite boulder's material %ld type 2 -> 0", material);
					materials[material].material_type = 0;
					mixed_coop.rules_applied++;
				}
			}
		}
	}
}

/* ---------- (r4) the host's layout: Blood Gulch, Battle Creek, Sidewinder

Halo PC placed other scenery and vehicles on these three maps it rebuilt, and MCC's files have Halo PC's. In an
Xbox-files host's game an MCC machine places the host's: the host's scenery, vehicles, their palettes and its
netgame flags come to it before it loads the map (mixed_coop_client_start_waits ... mixed_coop_layout_apply), and
its collision is the host's deciding (the Xbox-host-decides mode, as on Derelict and Hang 'Em High). A tag the
host places that MCC's map file has not is played with its stand-in, below, or refused. */

/* the Xbox tags these maps place that MCC's files have only under Halo PC's names: on an MCC machine in an Xbox
host's game on one of them, the MCC tag takes the Xbox's name (so the host's placements, objects and messages name
it). The Warthog and the Ghost's gun are made the Xbox's in what the game reads (mixed_coop_stand_in_rules: their
digests then the Xbox's); the trees are Halo PC's models and collision (visual, and their collision the host's
deciding as the level's is). A line made a comment refuses that map's mixed game again, naming the tag. */
static struct
{
	long group_tag;
	char const *xbox_path;
	/* (MCC's: the end of its path) */
	char const *mcc_ending;
	/* (its gameplay differs, the host's deciding: told, not refused) */
	boolean host_decides;
} const mixed_coop_stand_ins[] =
{
	{ 'vehi', "vehicles\\warthog\\warthog", "\\warthog\\mp_warthog", FALSE },
	{ 'weap', "vehicles\\ghost\\ghost gun", "\\ghost\\mp_ghost gun", FALSE },
	{ 'scen', "scenery\\trees\\tree_pine\\tree_pine", "\\mp_tree_pine_small\\mp_tree_pine_small", TRUE },
	{ 'scen', "scenery\\trees\\tree_pine_snow\\tree_pine_snow", "\\mp_tree_pine_snow_tall\\mp_tree_pine_snow_tall", TRUE },
	{ 'scen', "scenery\\trees\\tree_pine_snowsmall\\tree_pine_snowsmall", "\\mp_tree_pine_snow_small\\mp_tree_pine_snow_small",
		TRUE },
};

static boolean mixed_coop_name_ends_with(
	char const *name,
	char const *ending)
{
	size_t length = name ? strlen(name) : 0, ending_length = strlen(ending);

	return length >= ending_length && !csstrcasecmp(name + length - ending_length, ending);
}

/* whether a tag is a stand-in whose gameplay the host decides (accepted) */
static boolean mixed_coop_stand_in_host_decides(
	char const *name)
{
	short index;

	for (index = 0; name && index < NUMBEROF(mixed_coop_stand_ins); index++)
	{
		if (mixed_coop_stand_ins[index].host_decides && !csstrcasecmp(name, mixed_coop_stand_ins[index].xbox_path))
			return mixed_coop.stand_ins != 0;
	}
	return FALSE;
}

/* the Xbox's values of the Warthog's and the Ghost gun's (the research's compare of the two families' tags, Nov 23
2026: everything else the game reads in them is the same) */
static void mixed_coop_stand_in_rules(
	long absolute_index,
	short stand_in)
{
	struct cache_file_tag_instance_view *instance = &mixed_coop_instances()[absolute_index];

	if (mixed_coop_stand_ins[stand_in].group_tag == 'vehi')
	{
		/* the Warthog's speeds: Halo PC's mp_warthog is faster (0.275 0.12 0.0028) */
		static uint32_t const xbox_speeds[3] = { 0x3E828F5Cu, 0x3DCCCCCDu, 0x3B343958u };

		csmemcpy((byte *)instance->base_address + 0x2F8, xbox_speeds, sizeof(xbox_speeds));
		platform_log("mixed play: Xbox rules: the Warthog's speeds the Xbox's (0.255, 0.100, 0.00275)");
		mixed_coop.rules_applied++;
	}
	else if (mixed_coop_stand_ins[stand_in].group_tag == 'weap')
	{
		/* the Ghost's gun: the Xbox's has a magazine (300 rounds, 50 loaded, 2 s reload, one a shot), Halo PC's none */
		static struct weapon_magazine_definition magazine;
		struct weapon_definition *weapon = instance->base_address;
		struct weapon_trigger_definition *triggers = weapon->weapon.triggers.address;
		long trigger;

		if (weapon->weapon.magazines.count != 0 || !triggers)
			return;
		csmemset(&magazine, 0, sizeof(magazine));
		magazine.rounds_total_initial = 300;
		magazine.rounds_total_maximum = 300;
		magazine.rounds_loaded_maximum = 50;
		magazine.reload_time = 2.0f;
		magazine.rounds_reloaded = 50;
		magazine.chamber_time = 0.3f;
		magazine.reloading_effect.group_tag = 'effe';
		magazine.reloading_effect.name = "";
		magazine.reloading_effect.index = NONE;
		magazine.chambering_effect.group_tag = 'effe';
		magazine.chambering_effect.name = "";
		magazine.chambering_effect.index = NONE;
		weapon->weapon.magazines.address = &magazine;
		weapon->weapon.magazines.count = 1;
		for (trigger = 0; trigger < weapon->weapon.triggers.count; trigger++)
		{
			triggers[trigger].magazine_index = 0;
			triggers[trigger].rounds_per_shot = 1;
		}
		platform_log("mixed play: Xbox rules: the Ghost's gun has the Xbox's magazine (300 rounds, 50 loaded, 2 s reload)");
		mixed_coop.rules_applied++;
	}
}

/* (an MCC machine in an Xbox host's game on a host_layout map) the stand-ins named the Xbox's */
static void mixed_coop_stand_ins_apply(
	void)
{
	static char names[NUMBEROF(mixed_coop_stand_ins)][128];
	short stand_in;

	mixed_coop.stand_ins = 0;
	for (stand_in = 0; stand_in < NUMBEROF(mixed_coop_stand_ins); stand_in++)
	{
		long index;

		if (mixed_coop_find_tag(mixed_coop_stand_ins[stand_in].group_tag, mixed_coop_stand_ins[stand_in].xbox_path) != NONE)
			continue;
		for (index = 0; index < halo_loaded_tag_count; index++)
		{
			struct cache_file_tag_instance_view *instance = &mixed_coop_instances()[index];

			if (instance->group_tag != mixed_coop_stand_ins[stand_in].group_tag || !instance->base_address ||
				!mixed_coop_name_ends_with(instance->name, mixed_coop_stand_ins[stand_in].mcc_ending))
			{
				continue;
			}
			platform_log("mixed play: %s stands in for the Xbox's %s on this machine", instance->name,
				mixed_coop_stand_ins[stand_in].xbox_path);
			snprintf(names[stand_in], sizeof(names[stand_in]), "%s", mixed_coop_stand_ins[stand_in].xbox_path);
			instance->name = names[stand_in];
			mixed_coop_stand_in_rules(index, stand_in);
			mixed_coop.stand_ins++;
			break;
		}
	}
}

/* the layout's blocks: its palette's and placements' kinds, the scenario's blocks */
static struct
{
	int palette_kind;
	int placements_kind;
	size_t palette_offset;
	size_t placements_offset;
	char const *placements;
	char const *what;
} const mixed_coop_layout_blocks[] =
{
	{ MIXED_COOP_LAYOUT_SCENERY_PALETTE, MIXED_COOP_LAYOUT_SCENERY, offsetof(struct scenario, scenery_palette),
		offsetof(struct scenario, scenery), "scenery", "scenery" },
	{ MIXED_COOP_LAYOUT_VEHICLE_PALETTE, MIXED_COOP_LAYOUT_VEHICLES, offsetof(struct scenario, vehicle_palette),
		offsetof(struct scenario, vehicles), "vehicles", "vehicle" },
};


static boolean mixed_coop_table_writing;
static void mixed_coop_table_hex(char const *what, char const *family, uint8_t const *bytes, uint32_t length);
static void mixed_coop_table_dump_layout(char const *family, struct scenario const *scenario);
static boolean mixed_coop_client_only_layout(struct scenario const *scenario, struct tag_schema_definition const *scenario_schema);

/* (the host's) its layout's stream, as its map loads */
static void mixed_coop_layout_build(
	struct scenario const *scenario,
	struct tag_schema_definition const *scenario_schema)
{
	struct mixed_coop_dictionary_writer writer;
	short block;
	long index;

	mixed_coop_free(mixed_coop.layout_stream);
	mixed_coop.layout_stream = malloc(MIXED_COOP_LAYOUT_MAXIMUM_BYTES);
	mixed_coop.layout_length = 0;
	if (!mixed_coop.layout_stream)
		return;
	mixed_coop_writer_begin(&writer, mixed_coop.layout_stream, MIXED_COOP_LAYOUT_MAXIMUM_BYTES);
	mixed_coop_writer_u32(&writer, MIXED_COOP_LAYOUT_MAGIC);
	mixed_coop_writer_u16(&writer, MIXED_COOP_LAYOUT_VERSION);
	mixed_coop_writer_u8(&writer, (uint8_t)mixed_coop.level);
	mixed_coop_writer_u8(&writer, (uint8_t)(2 * NUMBEROF(mixed_coop_layout_blocks) + 1));
	for (block = 0; block < NUMBEROF(mixed_coop_layout_blocks); block++)
	{
		struct tag_block const *palette = (struct tag_block const *)((byte const *)scenario +
			mixed_coop_layout_blocks[block].palette_offset);
		struct tag_block const *placements = (struct tag_block const *)((byte const *)scenario +
			mixed_coop_layout_blocks[block].placements_offset);
		long element_size = mixed_coop_scenario_element_size(scenario_schema, mixed_coop_layout_blocks[block].placements);
		long palette_count = palette->address ? palette->count : 0, count = placements->address ? placements->count : 0;

		mixed_coop_writer_u8(&writer, (uint8_t)mixed_coop_layout_blocks[block].palette_kind);
		mixed_coop_writer_u8(&writer, 0);
		mixed_coop_writer_u16(&writer, MIXED_COOP_LAYOUT_PALETTE_BYTES);
		mixed_coop_writer_u32(&writer, (uint32_t)palette_count);
		for (index = 0; index < palette_count; index++)
		{
			struct scenario_object_palette_entry const *entry = (struct scenario_object_palette_entry const *)palette->address + index;
			struct cache_file_tag_instance_view *instance = mixed_coop_instance(entry->reference.index);
			char path[MIXED_COOP_LAYOUT_PATH_BYTES];
			short at;

			/* (its last byte NUL: a longer path is cut, for the player alone) */
			csmemset(path, 0, sizeof(path));
			if (instance && instance->name)
				snprintf(path, sizeof(path), "%s", instance->name);
			mixed_coop_writer_u32(&writer, instance ? (uint32_t)instance->group_tag : 0);
			mixed_coop_writer_u32(&writer, instance && instance->name ? mixed_coop_tag_path_hash(entry->reference.index) : 0);
			for (at = 0; at < MIXED_COOP_LAYOUT_PATH_BYTES; at++)
				mixed_coop_writer_u8(&writer, (uint8_t)path[at]);
		}
		mixed_coop_writer_u8(&writer, (uint8_t)mixed_coop_layout_blocks[block].placements_kind);
		mixed_coop_writer_u8(&writer, 0);
		mixed_coop_writer_u16(&writer, (uint16_t)element_size);
		mixed_coop_writer_u32(&writer, (uint32_t)count);
		for (index = 0; index < count; index++)
		{
			long at;

			for (at = 0; at < element_size; at++)
				mixed_coop_writer_u8(&writer, ((byte const *)placements->address)[index * element_size + at]);
		}
	}
	mixed_coop_writer_u8(&writer, MIXED_COOP_LAYOUT_NETGAME_FLAGS);
	mixed_coop_writer_u8(&writer, 0);
	mixed_coop_writer_u16(&writer, MIXED_COOP_NETGAME_FLAG_BYTES);
	/* (1.1.3, a built-in table's: no netgame flags - a client by a table keeps its own) */
	mixed_coop_writer_u32(&writer, (uint32_t)(scenario->netgame_flags.address && !mixed_coop_table_writing ?
		scenario->netgame_flags.count : 0));
	for (index = 0; scenario->netgame_flags.address && !mixed_coop_table_writing && index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag const *flag = (struct scenario_netgame_flag const *)scenario->netgame_flags.address + index;
		uint32_t words[4];

		csmemcpy(&words[0], &flag->position.x, 4);
		csmemcpy(&words[1], &flag->position.y, 4);
		csmemcpy(&words[2], &flag->position.z, 4);
		csmemcpy(&words[3], &flag->facing, 4);
		mixed_coop_writer_u32(&writer, words[0]);
		mixed_coop_writer_u32(&writer, words[1]);
		mixed_coop_writer_u32(&writer, words[2]);
		mixed_coop_writer_u32(&writer, words[3]);
		mixed_coop_writer_u16(&writer, (uint16_t)flag->type);
		mixed_coop_writer_u16(&writer, (uint16_t)flag->team_index);
	}
	mixed_coop.layout_length = mixed_coop_writer_end(&writer);
	platform_log("mixed play: %s's layout for MCC machines: %lu bytes (scenery %ld, vehicles %ld, netgame flags %ld)",
		mixed_coop_level_stems[mixed_coop.level], (unsigned long)mixed_coop.layout_length,
		scenario->scenery.count, scenario->vehicles.count, scenario->netgame_flags.count);
	if (!mixed_coop.layout_length)
	{
		mixed_coop_free(mixed_coop.layout_stream);
		mixed_coop.layout_stream = NULL;
	}
}

/* (an Xbox host's) the scenery and vehicle palettes' entries no placement names: no tag (their placements' digests
then the MCC machine's, which has no such tag) */
static void mixed_coop_layout_unplaced_entries(
	struct scenario *scenario,
	struct tag_schema_definition const *scenario_schema)
{
	short block;

	for (block = 0; block < NUMBEROF(mixed_coop_layout_blocks); block++)
	{
		struct tag_block *palette = (struct tag_block *)((byte *)scenario + mixed_coop_layout_blocks[block].palette_offset);
		struct tag_block const *placements = (struct tag_block const *)((byte const *)scenario +
			mixed_coop_layout_blocks[block].placements_offset);
		long element_size = mixed_coop_scenario_element_size(scenario_schema, mixed_coop_layout_blocks[block].placements);
		long entry, index;

		for (entry = 0; palette->address && entry < palette->count; entry++)
		{
			struct scenario_object_palette_entry *palette_entry = (struct scenario_object_palette_entry *)palette->address + entry;
			boolean used = FALSE;

			for (index = 0; placements->address && index < placements->count && !used; index++)
				used = ((struct scenario_object_datum const *)((byte const *)placements->address + index * element_size))->
					palette_entry_index == entry;
			if (!used && palette_entry->reference.index != NONE)
			{
				struct cache_file_tag_instance_view *instance = mixed_coop_instance(palette_entry->reference.index);

				platform_log("mixed play: the %s palette's %s places nothing: no tag in it (as on MCC machines)",
					mixed_coop_layout_blocks[block].what, instance && instance->name ? instance->name : "?");
				palette_entry->reference.index = NONE;
			}
		}
	}
}

static void mixed_coop_leave(char const *format, ...);

/* a path from another machine, as text for the player: printable, ends within its bytes */
static void mixed_coop_layout_path_text(
	uint8_t const *bytes,
	char *text,
	size_t size)
{
	size_t at;

	for (at = 0; at + 1 < size && at < MIXED_COOP_LAYOUT_PATH_BYTES && bytes[at]; at++)
		text[at] = bytes[at] >= 0x20 && bytes[at] < 0x7F ? (char)bytes[at] : '?';
	text[at] = 0;
}

/* (an MCC client of an Xbox host, as its map loads) the host's layout in place of MCC's */
static boolean mixed_coop_layout_apply(
	struct scenario *scenario,
	struct tag_schema_definition const *scenario_schema)
{
	struct mixed_coop_layout layout;
	char why[96];
	short block;
	long index, allocation = 0;

	if (!mixed_coop.layout_complete || !mixed_coop.layout_reassembly ||
		!mixed_coop_layout_read(mixed_coop.layout_reassembly->bytes, mixed_coop.layout_reassembly->total_bytes, &layout, why,
			sizeof(why)) || layout.level != (uint8_t)mixed_coop.level)
	{
		mixed_coop_leave(T("The host's placements of %s didn't come or couldn't be read: Xbox and MCC files play it "
			"together only with them."), mixed_coop_level_stems[mixed_coop.level]);
		return FALSE;
	}
	for (block = 0; block < NUMBEROF(mixed_coop_layout_blocks); block++)
	{
		long element_size = mixed_coop_scenario_element_size(scenario_schema, mixed_coop_layout_blocks[block].placements);
		uint32_t palette_count = layout.blocks[mixed_coop_layout_blocks[block].palette_kind].count;
		uint32_t count = layout.blocks[mixed_coop_layout_blocks[block].placements_kind].count;
		uint8_t const *palette_bytes = layout.blocks[mixed_coop_layout_blocks[block].palette_kind].elements;
		uint8_t const *placement_bytes = layout.blocks[mixed_coop_layout_blocks[block].placements_kind].elements;
		struct scenario_object_palette_entry *palette;
		byte *placements;

		if (!palette_bytes || !placement_bytes ||
			layout.blocks[mixed_coop_layout_blocks[block].palette_kind].element_bytes !=
				MIXED_COOP_LAYOUT_PALETTE_BYTES ||
			layout.blocks[mixed_coop_layout_blocks[block].placements_kind].element_bytes != (uint16_t)element_size ||
			allocation + 2 > (long)NUMBEROF(mixed_coop.layout_blocks))
		{
			mixed_coop_leave(T("The host's placements of %s are another version's: Xbox and MCC files can't play it "
				"together here."), mixed_coop_level_stems[mixed_coop.level]);
			return FALSE;
		}
		palette = malloc(sizeof(*palette) * MAX(1, palette_count));
		placements = malloc((size_t)element_size * MAX(1, count));
		mixed_coop.layout_blocks[allocation++] = palette;
		mixed_coop.layout_blocks[allocation++] = placements;
		if (!palette || !placements)
		{
			mixed_coop_leave(T("Not enough memory to check the host's level."));
			return FALSE;
		}
		csmemset(palette, 0, sizeof(*palette) * MAX(1, palette_count));
		for (index = 0; index < (long)palette_count; index++)
		{
			uint8_t const *entry = palette_bytes + index * MIXED_COOP_LAYOUT_PALETTE_BYTES;
			uint32_t group = mixed_coop_read_u32(entry), path_hash = mixed_coop_read_u32(entry + 4);
			long local, found = NONE;

			for (local = 0; local < mixed_coop.tag_count && found == NONE; local++)
			{
				struct cache_file_tag_instance_view *instance = &mixed_coop_instances()[local];

				if ((uint32_t)instance->group_tag == group && instance->name && instance->base_address &&
					mixed_coop_path_hash(group, instance->name) == path_hash)
				{
					found = local;
				}
			}
			palette[index].reference.group_tag = (unsigned long)group;
			/* (a tag this machine has not: refused below if a placement names it - a palette may list one
			nothing places, as Sidewinder's landing beacon) */
			if (found == NONE)
			{
				palette[index].reference.name = "";
				palette[index].reference.name_length = 0;
				palette[index].reference.index = NONE;
				continue;
			}
			palette[index].reference.name = mixed_coop_instances()[found].name;
			palette[index].reference.name_length = (long)strlen(mixed_coop_instances()[found].name);
			palette[index].reference.index = mixed_coop_instances()[found].tag_index;
		}
		for (index = 0; index < (long)count; index++)
		{
			byte *element = placements + index * element_size;
			struct scenario_object_datum const *datum = (struct scenario_object_datum const *)element;

			csmemcpy(element, placement_bytes + index * element_size, (size_t)element_size);
			if (datum->palette_entry_index < NONE || datum->palette_entry_index >= (long)palette_count ||
				datum->name_index < NONE || datum->name_index >= scenario->object_names.count)
			{
				mixed_coop_leave(T("The host's placements of %s couldn't be read (an object past its lists)."),
					mixed_coop_level_stems[mixed_coop.level]);
				return FALSE;
			}
			if (datum->palette_entry_index != NONE && palette[datum->palette_entry_index].reference.index == NONE)
			{
				char path[MIXED_COOP_LAYOUT_PATH_BYTES + 1];

				mixed_coop_layout_path_text(palette_bytes + datum->palette_entry_index * MIXED_COOP_LAYOUT_PALETTE_BYTES + 8, path,
					sizeof(path));
				mixed_coop_leave(T("The host's %s places a %s your MCC files don't have (%s): Xbox and MCC files can't play "
					"it together."), mixed_coop_level_stems[mixed_coop.level], mixed_coop_layout_blocks[block].what, path);
				return FALSE;
			}
		}
		{
			struct tag_block *palette_block = (struct tag_block *)((byte *)scenario + mixed_coop_layout_blocks[block].palette_offset);
			struct tag_block *placements_block =
				(struct tag_block *)((byte *)scenario + mixed_coop_layout_blocks[block].placements_offset);

			platform_log("mixed play: the host's %s: %lu placements of %lu tags (this file's %ld of %ld)",
				mixed_coop_layout_blocks[block].placements, (unsigned long)count, (unsigned long)palette_count,
				placements_block->count, palette_block->count);
			palette_block->address = palette;
			palette_block->count = (long)palette_count;
			placements_block->address = placements;
			placements_block->count = (long)count;
		}
	}
	/* (the netgame flags: the host's, its teleporters, CTF flags, hills and checkpoints; by a table, this file's own:
	the host's places unknown) */
	if (!mixed_coop.client_only)
	{
		uint32_t count = layout.blocks[MIXED_COOP_LAYOUT_NETGAME_FLAGS].count;
		uint8_t const *bytes = layout.blocks[MIXED_COOP_LAYOUT_NETGAME_FLAGS].elements;
		struct scenario_netgame_flag *flags;

		if (!bytes || allocation + 1 > (long)NUMBEROF(mixed_coop.layout_blocks))
		{
			mixed_coop_leave(T("The host's placements of %s are another version's: Xbox and MCC files can't play it "
				"together here."), mixed_coop_level_stems[mixed_coop.level]);
			return FALSE;
		}
		flags = malloc(sizeof(*flags) * MAX(1, count));
		mixed_coop.layout_blocks[allocation++] = flags;
		if (!flags)
		{
			mixed_coop_leave(T("Not enough memory to check the host's level."));
			return FALSE;
		}
		csmemset(flags, 0, sizeof(*flags) * MAX(1, count));
		for (index = 0; index < (long)count; index++)
		{
			uint8_t const *at = bytes + index * MIXED_COOP_NETGAME_FLAG_BYTES;
			uint32_t words[4];
			short type = (short)(at[16] | at[17] << 8), team = (short)(at[18] | at[19] << 8);
			short value;

			for (value = 0; value < 4; value++)
				words[value] = mixed_coop_read_u32(at + 4 * value);
			if (type < 0 || type >= NUMBER_OF_NETGAME_FLAG_TYPES || team < NONE || team >= 32)
			{
				mixed_coop_leave(T("The host's placements of %s couldn't be read (a netgame flag of no kind known)."),
					mixed_coop_level_stems[mixed_coop.level]);
				return FALSE;
			}
			csmemcpy(&flags[index].position.x, &words[0], 4);
			csmemcpy(&flags[index].position.y, &words[1], 4);
			csmemcpy(&flags[index].position.z, &words[2], 4);
			csmemcpy(&flags[index].facing, &words[3], 4);
			flags[index].type = type;
			flags[index].team_index = team;
		}
		platform_log("mixed play: the host's netgame flags: %lu (this file's %ld)", (unsigned long)count,
			scenario->netgame_flags.count);
		scenario->netgame_flags.address = flags;
		scenario->netgame_flags.count = (long)count;
	}
	mixed_coop.layout_complete = FALSE;
	mixed_coop.layout_taken = TRUE;
	platform_log("mixed play: %s placed as the host's (Xbox) layout", mixed_coop_level_stems[mixed_coop.level]);
	return TRUE;
}

static void mixed_coop_layout_free(
	void)
{
	short index;

	for (index = 0; index < NUMBEROF(mixed_coop.layout_blocks); index++)
	{
		mixed_coop_free(mixed_coop.layout_blocks[index]);
		mixed_coop.layout_blocks[index] = NULL;
	}
	mixed_coop_free(mixed_coop.layout_stream);
	mixed_coop.layout_stream = NULL;
	mixed_coop.layout_length = 0;
}

/* ---------- the digests of the level */

static boolean mixed_coop_layout_block(
	char const *name)
{
	static char const *const names[] =
	{
		"object_names", "scenery", "scenery_palette", "bipeds", "biped_palette", "vehicles", "vehicle_palette",
		"equipment", "equipment_palette", "weapons", "weapon_palette", "device_groups", "machines", "machine_palette",
		"controls", "control_palette", "light_fixtures", "light_fixtures_palette", "sound_scenery",
		"sound_scenery_palette", "starting_profiles", "players", "scenario_starting_equipment",
		"bsp_switch_trigger_volumes", "structure_bsp_references",
	};
	short index;

	/* (multiplayer: its player starts and equipment spawns are the host's,
	which spawns every player and item - MCC's files have Halo PC's few more
	and moved; its flags the host's dictionary gives a client,
	mixed_coop_dictionary_take) */
	if (mixed_coop_multiplayer_level && (!strcmp(name, "players") || !strcmp(name, "netgame_equipment")))
		return FALSE;
	for (index = 0; index < NUMBEROF(names); index++)
	{
		if (!strcmp(names[index], name))
			return TRUE;
	}
	return FALSE;
}

/* the placements' blocks, and their palettes */
static char const *const mixed_coop_placement_blocks[9][2] =
{
	{ "scenery", "scenery_palette" }, { "bipeds", "biped_palette" }, { "vehicles", "vehicle_palette" },
	{ "equipment", "equipment_palette" }, { "weapons", "weapon_palette" }, { "machines", "machine_palette" },
	{ "controls", "control_palette" }, { "light_fixtures", "light_fixtures_palette" },
	{ "sound_scenery", "sound_scenery_palette" },
};

static boolean mixed_coop_placement_block(
	char const *name)
{
	short index;

	for (index = 0; index < NUMBEROF(mixed_coop_placement_blocks); index++)
	{
		if (!strcmp(mixed_coop_placement_blocks[index][0], name))
			return TRUE;
	}
	return FALSE;
}

/* a placement block: each element walked as the schema has it, but where
its object has no collision model (a lens flare, a light, a sound: nothing
touches it, nothing it does depends on where it stands to a hundredth of a
unit - a10's yellow lens flare stands 0.02 units apart in MCC's) without its
position and rotation */
static uint32_t mixed_coop_placements_digest(
	struct scenario const *scenario,
	struct tag_schema_definition const *scenario_schema,
	struct tag_schema_field const *field,
	real offset)
{
	struct tag_block const *block = (struct tag_block const *)((byte const *)scenario + field->offset);
	struct tag_schema_definition const *definition = field->definition;
	struct tag_block const *palette = NULL;
	struct tag_schema_field const *other;
	uint32_t hash = MIXED_COOP_FNV_BASIS;
	byte element[1024];
	byte mask[1];
	long index;
	short at;

	(void)mask;
	for (at = 0; at < NUMBEROF(mixed_coop_placement_blocks); at++)
	{
		if (strcmp(mixed_coop_placement_blocks[at][0], field->name))
			continue;
		for (other = scenario_schema->fields; other && other->type != _tag_schema_terminator; other++)
		{
			if (other->type == _tag_schema_block && other->name && !strcmp(other->name, mixed_coop_placement_blocks[at][1]))
				palette = (struct tag_block const *)((byte const *)scenario + other->offset);
		}
	}
	hash = mixed_coop_fnv(hash, &block->count, sizeof(block->count));
	if (!definition || definition->size > (long)sizeof(element) || !block->address)
		return hash;
	for (index = 0; index < block->count; index++)
	{
		struct scenario_object_datum *datum = (struct scenario_object_datum *)element;
		boolean collides = TRUE;

		csmemcpy(element, (byte const *)block->address + index * definition->size, (size_t)definition->size);
		if (palette && palette->address && datum->palette_entry_index >= 0 && datum->palette_entry_index < palette->count)
		{
			long object_tag = ((struct scenario_object_palette_entry const *)palette->address)[datum->palette_entry_index].reference.index;
			struct cache_file_tag_instance_view *instance = mixed_coop_instance(object_tag);

			collides = !instance || !instance->base_address ||
				((struct object_definition const *)instance->base_address)->object.collision_model.index != NONE;
			/* (the flag stand and teleporter base: collision on Xbox machines alone, the host's deciding -
			their places as a non-colliding object's on every machine, mixed_coop_xbox_host_decides) */
			if (instance && mixed_coop_xbox_host_decides_tag(instance->name))
				collides = FALSE;
		}
		if (!collides)
		{
			csmemset(&datum->position, 0, sizeof(datum->position));
			csmemset(&datum->rotation, 0, sizeof(datum->rotation));
		}
		/* (the placement flags the game reads, not automatic and the three
		difficulties: MCC sets another, "use player appearance", on a few
		cinematic props; and its place and rotation a float step or two off
		on a few: rounded as the tools' floats) */
		datum->placement_flags &= 0xF;
		/* (which BSPs it stands in: the game's own, worked out as each BSP
		connects, object_types.c; MCC's tools left theirs in the tag) */
		datum->on_bsp_flags = 0;
		/* (and its unused long, where MCC keeps something on a cinematic
		prop or two) */
		datum->unused = 0;
		/* (its place on a grid of 1/64 of a unit, its rotation of 1/1024 of a
		radian, the grid moved by offset steps: b40's small snow boulder
		stands 0.001 units lower in MCC's) */
		{
			real *values = &datum->position.x;
			short value;

			for (value = 0; value < 6; value++)
			{
				long step = (long)floorf(values[value] * (value < 3 ? 64.0f : 1024.0f) + offset);

				csmemcpy(&values[value], &step, sizeof(step));
			}
		}
		mixed_coop_walk_budget = MIXED_COOP_WALK_BUDGET;
		mixed_coop_walk(&hash, element, definition, 1, field->name);
		if (mixed_coop_trace_filter && strstr(mixed_coop_trace_filter, "placements"))
		{
			uint32_t alone = MIXED_COOP_FNV_BASIS;
			struct cache_file_tag_instance_view *instance = palette && palette->address && datum->palette_entry_index >= 0 &&
				datum->palette_entry_index < palette->count ? mixed_coop_instance(((struct scenario_object_palette_entry const *)
					palette->address)[datum->palette_entry_index].reference.index) : NULL;

			mixed_coop_walk(&alone, element, definition, 1, field->name);
			char text[2 * 160 + 1];
			long at_byte;

			text[0] = 0;
			for (at_byte = 0; at_byte < definition->size && at_byte < 160; at_byte++)
				snprintf(text + 2 * at_byte, 3, "%02x", element[at_byte]);
			platform_log("mixed placement: %s %ld %08x %s %.3f %.3f %.3f %d %s", field->name, index, (unsigned)alone,
				instance && instance->name ? instance->name : "?", datum->position.x, datum->position.y, datum->position.z,
				collides, text);
		}
	}
	return hash;
}

static boolean mixed_coop_host_only_block(
	char const *name)
{
	return !strncmp(name, "ai_", 3) || !strncmp(name, "hs_", 3) || !strcmp(name, "recorded_animations") ||
		(mixed_coop_multiplayer_level && (!strcmp(name, "players") || !strcmp(name, "netgame_equipment"))) ||
		/* (tested by the scripts alone, which the host alone runs) */
		!strcmp(name, "trigger_volumes");
}

static void mixed_coop_scenario_digests(
	struct scenario const *scenario,
	struct tag_schema_definition const *scenario_schema)
{
	struct tag_schema_field const *field;
	uint32_t layout = MIXED_COOP_FNV_BASIS, host_only = MIXED_COOP_FNV_BASIS, layout_offset = MIXED_COOP_FNV_BASIS;
	byte mask[1];

	for (field = scenario_schema->fields; field && field->type != _tag_schema_terminator; field++)
	{
		if ((field->type != _tag_schema_block && field->type != _tag_schema_data) || !field->name)
			continue;
		/* (the names: but the MCC extras', sh_...) */
		if (!strcmp(field->name, "object_names"))
		{
			long index;

			for (index = 0; index < scenario->object_names.count && scenario->object_names.address; index++)
			{
				struct scenario_object_name const *object_name =
					(struct scenario_object_name const *)scenario->object_names.address + index;
				long length = 0;

				if (!strncmp(object_name->name, MIXED_COOP_MCC_EXTRA_NAME_PREFIX, strlen(MIXED_COOP_MCC_EXTRA_NAME_PREFIX)))
					continue;
				while (length < TAG_STRING_LENGTH && object_name->name[length])
					length++;
				layout = mixed_coop_fnv(layout, object_name->name, (uint32_t)length);
				layout_offset = mixed_coop_fnv(layout_offset, object_name->name, (uint32_t)length);
			}
			continue;
		}
		/* (the structure BSPs' references: their tags' paths alone, their file
		places differ by file) */
		if (!strcmp(field->name, "structure_bsp_references"))
		{
			long index;
			struct tag_block const *block = (struct tag_block const *)((byte const *)scenario + field->offset);

			layout = mixed_coop_fnv(layout, &block->count, sizeof(block->count));
			layout_offset = mixed_coop_fnv(layout_offset, &block->count, sizeof(block->count));
			for (index = 0; index < block->count && block->address; index++)
			{
				uint32_t path = mixed_coop_tag_path_hash(
					((struct scenario_structure_bsp_reference const *)block->address)[index].structure_bsp.index);

				layout = mixed_coop_fnv(layout, &path, sizeof(path));
				layout_offset = mixed_coop_fnv(layout_offset, &path, sizeof(path));
			}
			continue;
		}
		mixed_coop_walk_budget = MIXED_COOP_WALK_BUDGET;
		if (mixed_coop_placement_block(field->name))
		{
			uint32_t alone = mixed_coop_placements_digest(scenario, scenario_schema, field, 0.5f);
			uint32_t alone_offset = mixed_coop_placements_digest(scenario, scenario_schema, field, 0.0f);

			if (getenv("HALO_MIXED_COOP_DIGEST_DUMP"))
				platform_log("mixed co-op: layout block %s (%ld) %08x %08x", field->name,
					((struct tag_block const *)((byte const *)scenario + field->offset))->count, (unsigned)alone,
					(unsigned)alone_offset);
			layout = mixed_coop_fnv(layout, &alone, sizeof(alone));
			layout_offset = mixed_coop_fnv(layout_offset, &alone_offset, sizeof(alone_offset));
		}
		else if (mixed_coop_layout_block(field->name))
		{
			if (getenv("HALO_MIXED_COOP_DIGEST_DUMP"))
			{
				uint32_t alone = MIXED_COOP_FNV_BASIS;
				long count = field->type == _tag_schema_block ?
					((struct tag_block const *)((byte const *)scenario + field->offset))->count : 0;

				mixed_coop_walk_field(&alone, (byte const *)scenario, field, mask, 0, 0);
				platform_log("mixed co-op: layout block %s (%ld) %08x", field->name, count, (unsigned)alone);
				mixed_coop_walk_budget = MIXED_COOP_WALK_BUDGET;
			}
			{
				uint32_t alone = MIXED_COOP_FNV_BASIS;

				mixed_coop_walk_field(&alone, (byte const *)scenario, field, mask, 0, 0);
				layout = mixed_coop_fnv(layout, &alone, sizeof(alone));
				layout_offset = mixed_coop_fnv(layout_offset, &alone, sizeof(alone));
			}
		}
		else if (mixed_coop_host_only_block(field->name))
		{
			mixed_coop_walk_field(&host_only, (byte const *)scenario, field, mask, 0, 0);
		}
	}
	mixed_coop.digests.layout = layout;
	mixed_coop.digests.layout_offset = layout_offset;
	mixed_coop.digests.host_only = host_only;
	/* (tests: a multiplayer map's flags and equipment, one line each) */
	if (getenv("HALO_MIXED_COOP_DIGEST_DUMP") && mixed_coop_multiplayer_level)
	{
		long index;

		for (index = 0; index < scenario->netgame_flags.count && scenario->netgame_flags.address; index++)
		{
			struct scenario_netgame_flag const *flag = (struct scenario_netgame_flag const *)scenario->netgame_flags.address + index;

			platform_log("mixed netgame: flag %ld type %d team %d at %.4f %.4f %.4f facing %.4f", index, (int)flag->type,
				(int)flag->team_index, flag->position.x, flag->position.y, flag->position.z, flag->facing);
		}
		for (index = 0; index < scenario->netgame_equipment.count && scenario->netgame_equipment.address; index++)
		{
			struct scenario_netgame_equipment const *equipment =
				(struct scenario_netgame_equipment const *)scenario->netgame_equipment.address + index;
			struct cache_file_tag_instance_view *instance = mixed_coop_instance(equipment->item_collection.index);

			platform_log("mixed netgame: equipment %ld flags %lx types %d %d %d %d team %d spawn %d at %.4f %.4f %.4f facing %.4f %s",
				index, (unsigned long)equipment->flags, equipment->game_type[0], equipment->game_type[1], equipment->game_type[2],
				equipment->game_type[3], (int)equipment->team_index, (int)equipment->spawn_time, equipment->position.x,
				equipment->position.y, equipment->position.z, equipment->facing, instance && instance->name ? instance->name : "NONE");
		}
	}
}

static int mixed_coop_compare_tags(
	void const *first,
	void const *second)
{
	uint32_t a = ((struct mixed_coop_local_tag const *)first)->path_hash;
	uint32_t b = ((struct mixed_coop_local_tag const *)second)->path_hash;

	return a < b ? -1 : a > b ? 1 : 0;
}

static void mixed_coop_free_tables(
	void)
{
	mixed_coop_free(mixed_coop.tags);
	mixed_coop.tags = NULL;
	mixed_coop_free(mixed_coop.stream);
	mixed_coop.stream = NULL;
	mixed_coop.stream_length = 0;
	mixed_coop_free(mixed_coop.host_to_local);
	mixed_coop.host_to_local = NULL;
	mixed_coop_free(mixed_coop.local_to_host);
	mixed_coop.local_to_host = NULL;
	mixed_coop_free(mixed_coop.host_datums);
	mixed_coop.host_datums = NULL;
	mixed_coop_free(mixed_coop.graph_offsets);
	mixed_coop.graph_offsets = NULL;
	mixed_coop_free(mixed_coop.graph_counts);
	mixed_coop.graph_counts = NULL;
	mixed_coop_free(mixed_coop.animation_pool);
	mixed_coop.animation_pool = NULL;
	mixed_coop.animation_pool_used = 0;
	mixed_coop.host_tag_count = 0;
}

static void mixed_coop_dump(
	void)
{
	char const *path = getenv("HALO_MIXED_COOP_DIGEST_DUMP");
	long index;

	if (!path || !path[0])
		return;
	platform_log("mixed digest: level %d family %s tags %ld layout %08x/%08x gameplay %08x host_only %08x extras %ld scripts %ld rules %ld",
		mixed_coop.level, mixed_coop_family_name(mixed_coop.family), mixed_coop.tag_count,
		(unsigned)mixed_coop.digests.layout, (unsigned)mixed_coop.digests.layout_offset, (unsigned)mixed_coop.digests.gameplay, (unsigned)mixed_coop.digests.host_only,
		mixed_coop.extras_removed, mixed_coop.scripts_disabled, mixed_coop.rules_applied);
	for (index = 0; index < mixed_coop.tag_count; index++)
	{
		struct cache_file_tag_instance_view *instance = &mixed_coop_instances()[index];
		long group = instance->group_tag == 'mod2' ? 'mode' : instance->group_tag;

		platform_log("mixed digest: %c%c%c%c %d %08x %08x %s", (char)(group >> 24), (char)(group >> 16), (char)(group >> 8),
			(char)group, (int)mixed_coop.tags[index].kind, (unsigned)mixed_coop.tags[index].path_hash,
			(unsigned)mixed_coop.tags[index].digest, instance->name ? instance->name : "?");
	}
}

/* ---------- (1.1.3, the owner's flag stand decision, Nov 24 2026) the flag stand and teleporter base collision an
Xbox-files machine sends an MCC-files host (mixed_collision_protocol.h) */

/* (the game's layout, as mixed_collision_protocol.c lays a model out) */
typedef char mixed_collision_root_size_assert[sizeof(struct collision_model) == MIXED_COLLISION_ROOT_BYTES ? 1 : -1];
typedef char mixed_collision_root_materials_assert[
	offsetof(struct collision_model, resistance.materials) == MIXED_COLLISION_ROOT_MATERIALS ? 1 : -1];
typedef char mixed_collision_root_regions_assert[
	offsetof(struct collision_model, resistance.regions) == MIXED_COLLISION_ROOT_REGIONS ? 1 : -1];
typedef char mixed_collision_root_modifiers_assert[
	offsetof(struct collision_model, resistance.modifiers) == MIXED_COLLISION_ROOT_MODIFIERS ? 1 : -1];
typedef char mixed_collision_root_spheres_assert[
	offsetof(struct collision_model, pathfinding_spheres) == MIXED_COLLISION_ROOT_SPHERES ? 1 : -1];
typedef char mixed_collision_root_nodes_assert[offsetof(struct collision_model, nodes) == MIXED_COLLISION_ROOT_NODES ? 1 : -1];
typedef char mixed_collision_root_effects_assert[
	offsetof(struct collision_model, resistance.localized_damage_effect) == 0x70 &&
	offsetof(struct collision_model, resistance.area_damage_effect) == 0x84 &&
	offsetof(struct collision_model, resistance.body_damaged_effect) == 0x98 &&
	offsetof(struct collision_model, resistance.body_depleted_effect) == 0xA8 &&
	offsetof(struct collision_model, resistance.body_destroyed_effect) == 0xBC &&
	offsetof(struct collision_model, resistance.maximum_shield_vitality) == 0xCC &&
	offsetof(struct collision_model, resistance.shield_material_type) == 0xD2 &&
	offsetof(struct collision_model, resistance.shield_damaged_effect) == 0x188 &&
	offsetof(struct collision_model, resistance.shield_depleted_effect) == 0x198 &&
	offsetof(struct collision_model, resistance.shield_recharging_effect) == 0x1A8 &&
	offsetof(struct collision_model, resistance.runtime_shield_recharge_velocity) == 0x1C0 &&
	offsetof(struct collision_model, pathfinding_box) == 0x268 ? 1 : -1];
typedef char mixed_collision_material_size_assert[
	sizeof(struct damage_resistance_material) == MIXED_COLLISION_MATERIAL_BYTES ? 1 : -1];
typedef char mixed_collision_sphere_size_assert[sizeof(struct pathfinding_sphere) == MIXED_COLLISION_SPHERE_BYTES ? 1 : -1];
typedef char mixed_collision_node_size_assert[sizeof(struct collision_node) == MIXED_COLLISION_NODE_BYTES &&
	offsetof(struct collision_node, bsps) == MIXED_COLLISION_NODE_BSPS ? 1 : -1];
typedef char mixed_collision_bsp_size_assert[sizeof(struct collision_bsp) == MIXED_COLLISION_BSP_BYTES &&
	offsetof(struct collision_bsp, bsp3d.nodes) == MIXED_COLLISION_BSP_BSP3D_NODES &&
	offsetof(struct collision_bsp, bsp3d.planes) == MIXED_COLLISION_BSP_PLANES &&
	offsetof(struct collision_bsp, leaves) == MIXED_COLLISION_BSP_LEAVES &&
	offsetof(struct collision_bsp, bsp2d_references) == MIXED_COLLISION_BSP_BSP2D_REFERENCES &&
	offsetof(struct collision_bsp, bsp2d.nodes) == MIXED_COLLISION_BSP_BSP2D_NODES &&
	offsetof(struct collision_bsp, surfaces) == MIXED_COLLISION_BSP_SURFACES &&
	offsetof(struct collision_bsp, edges) == MIXED_COLLISION_BSP_EDGES &&
	offsetof(struct collision_bsp, vertices) == MIXED_COLLISION_BSP_VERTICES ? 1 : -1];
typedef char mixed_collision_bsp_elements_assert[sizeof(struct bsp3d_node) == MIXED_COLLISION_BSP3D_NODE_BYTES &&
	sizeof(struct bsp2d_node) == MIXED_COLLISION_BSP2D_NODE_BYTES && sizeof(struct collision_surface) == MIXED_COLLISION_SURFACE_BYTES &&
	sizeof(struct collision_edge) == MIXED_COLLISION_EDGE_BYTES && sizeof(struct collision_vertex) == MIXED_COLLISION_VERTEX_BYTES ? 1 : -1];

/* (cache_files.c) a runtime tag put beside the map's */
long cache_runtime_tag_add(long group_tag, char *name, void *base_address);

/* the Xbox's files' models, by their digest (BLAKE2b-256 of the stream's model bytes, the same in every stock
multiplayer map of the Xbox's that has the model: measured Nov 24 2026 from the 13 maps). A model of any other
digest is not taken */
static struct
{
	uint8_t model;
	uint8_t digest[MIXED_COLLISION_DIGEST_BYTES];
} const mixed_collision_known[] =
{
	/* (scenery\flag_base\flag_base: 1796 bytes, all 13 maps) */
	{ MIXED_COLLISION_MODEL_FLAG_STAND, { 0x40, 0x7d, 0x8a, 0x48, 0x90, 0xc8, 0x3d, 0x9e, 0x2e, 0x0c, 0x29, 0x2d, 0x56, 0x03, 0x84, 0x95,
		0x15, 0xd6, 0x9b, 0x69, 0x30, 0xbd, 0x8a, 0x87, 0x74, 0xbb, 0xd0, 0xe1, 0x44, 0xa0, 0x10, 0x76 } },
	/* (scenery\teleporter_base\teleporter_base: 1636 bytes, the 3 maps that have it) */
	{ MIXED_COLLISION_MODEL_TELEPORTER_BASE, { 0xb7, 0x31, 0x17, 0xcb, 0xb2, 0x50, 0x1f, 0xac, 0xc9, 0x07, 0xbc, 0xf9, 0x1d, 0x15, 0x31, 0xc7,
		0xbf, 0x3e, 0x4c, 0x1b, 0x6e, 0xe5, 0xb2, 0x0b, 0x12, 0x85, 0xb9, 0x2a, 0xeb, 0x2b, 0x04, 0x60 } },
};

/* (the host's) the models this hosting holds, in memory alone - from the first machine that sent each, checked:
taken again for each map of the hosting, never written anywhere nor sent on */
static struct
{
	uint8_t *region;
	uint32_t region_bytes;
	void *root;
	char name[64];
	/* (this map's) the runtime tag, the scenery it was given to and that scenery's digest before */
	long tag_index;
	long scenery_absolute;
	uint32_t digest_before;
	boolean grafted;
} mixed_collision_held[NUMBER_OF_MIXED_COLLISION_MODELS];

static boolean mixed_collision_tests_any_digest(
	void)
{
#ifndef HALO_RELEASE
	/* (debug builds alone: the digests measured) */
	return getenv("HALO_MIXED_COLLISION_ANY_DIGEST") != NULL;
#else
	return FALSE;
#endif
}

static void mixed_collision_digest_text(
	uint8_t const *digest,
	char *text)
{
	short index;

	for (index = 0; index < MIXED_COLLISION_DIGEST_BYTES; index++)
		sprintf(text + index * 2, "%02x", digest[index]);
}

static boolean mixed_collision_digest_known(
	int model,
	uint8_t const *digest)
{
	short index;

	for (index = 0; index < (short)NUMBEROF(mixed_collision_known); index++)
	{
		if (mixed_collision_known[index].model == model &&
			!memcmp(mixed_collision_known[index].digest, digest, MIXED_COLLISION_DIGEST_BYTES))
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* the held models let go (this machine hosts no more) */
static void mixed_collision_free_held(
	void)
{
	short model;

	for (model = 0; model < NUMBER_OF_MIXED_COLLISION_MODELS; model++)
	{
		if (mixed_collision_held[model].region)
			platform_log("mixed play: the %s collision this hosting held let go", mixed_collision_model_path(model));
		mixed_coop_free(mixed_collision_held[model].region);
		csmemset(&mixed_collision_held[model], 0, sizeof(mixed_collision_held[model]));
	}
}

/* whether this machine (the host) plays the mixed multiplayer map `level` from MCC's files, which lack the models */
static boolean mixed_collision_host_lacks(
	int level)
{
	/* (the maps whose MCC host spawns other weapons than an Xbox machine would - Boarding Action's netgame
	equipment spawns Halo PC's needler on MCC's files where the Xbox's spawns its sniper rifles and assault rifles,
	which the digests do not see: tested Nov 24 2026, the two machines' objects parted from the first tick. An MCC
	host's game there stays refused to an Xbox machine, as before 1.1.3's flag stand collision) */
	static char const *const unoffered[] = { "boardingaction" };
	short index;

	for (index = 0; level >= 0 && level < MIXED_COOP_LEVELS && index < (short)NUMBEROF(unoffered); index++)
	{
		if (!strcmp(mixed_coop_level_stems[level], unoffered[index]))
			return FALSE;
	}
	return level >= 0 && mixed_coop_level_is_multiplayer(level) && !mixed_coop_rebuilt_level(level) &&
		mixed_coop_local_family(level) == _mixed_coop_family_mcc && global_network_game_server_get() != NULL;
}

/* (the host's) the models it asks an Xbox machine for: those it lacks and holds none of */
static uint16_t mixed_collision_missing(
	int level)
{
	uint16_t bits = 0;
	short model;

	if (!mixed_collision_host_lacks(level))
		return 0;
	for (model = 0; model < NUMBER_OF_MIXED_COLLISION_MODELS; model++)
	{
		if (!mixed_collision_held[model].region)
			bits |= (uint16_t)(1u << model);
	}
	return bits;
}

static uint32_t mixed_coop_gameplay_digest(void);

/* (the host's) a held model given to its scenery on the loaded map: its runtime tag made, the scenery's collision
model, its placed objects' (objects_placed: the game's running) made colliding, its digest worked out again (the one
before kept: an MCC machine is sent that one, mixed_coop_build_stream) */
static boolean mixed_collision_graft(
	int model,
	boolean objects_placed)
{
	long scenery = mixed_coop_find_tag('scen', mixed_collision_model_path(model));
	struct object_definition *definition = scenery != NONE ? mixed_coop_instances()[scenery].base_address : NULL;
	long tag_index, objects = 0;

	if (!mixed_collision_held[model].region || mixed_collision_held[model].grafted || !definition || !mixed_coop.tags ||
		scenery >= mixed_coop.tag_count)
	{
		return FALSE;
	}
	if (definition->object.collision_model.index != NONE)
	{
		platform_log("mixed play: %s has a collision model already (this machine's files): kept",
			mixed_collision_model_path(model));
		return FALSE;
	}
	tag_index = cache_runtime_tag_add('coll', mixed_collision_held[model].name, mixed_collision_held[model].root);
	if (tag_index == NONE)
	{
		platform_log("mixed play: no room for %s's collision beside the map's tags", mixed_collision_model_path(model));
		return FALSE;
	}
	definition->object.collision_model.group_tag = 'coll';
	definition->object.collision_model.index = tag_index;
	mixed_collision_held[model].tag_index = tag_index;
	mixed_collision_held[model].scenery_absolute = scenery;
	mixed_collision_held[model].grafted = TRUE;
	if (objects_placed)
	{
		struct object_iterator iterator;

		object_iterator_new(&iterator, _object_mask_scenery, 0);
		while (object_iterator_next(&iterator))
		{
			struct object_datum *object = object_get(iterator.index);

			if ((short)object->definition_index != (short)scenery ||
				TEST_FLAG(object->object.flags, _object_has_collision_model_bit))
			{
				continue;
			}
			if (TEST_FLAG(object->object.flags, _object_connected_to_map_bit))
			{
				struct location location = object->object.location;

				object_disconnect_from_map(iterator.index);
				SET_FLAG(object->object.flags, _object_has_collision_model_bit, TRUE);
				object_reconnect_to_map(iterator.index, &location);
			}
			else
				SET_FLAG(object->object.flags, _object_has_collision_model_bit, TRUE);
			objects++;
		}
	}
	mixed_collision_held[model].digest_before = mixed_coop.tags[scenery].digest;
	if (mixed_coop.tags[scenery].kind != _mixed_tag_other)
		mixed_coop.tags[scenery].digest = mixed_coop_tag_digest(scenery);
	if (objects_placed)
	{
		mixed_coop.digests.gameplay = mixed_coop_gameplay_digest();
		mixed_coop_free(mixed_coop.stream);
		mixed_coop.stream = NULL;
		mixed_coop.stream_length = 0;
	}
	platform_log("mixed play: %s plays with the Xbox's collision now (held in memory, from a joining Xbox machine's own "
		"files): tag %08lx, %ld placed objects made colliding, its digest %08x (MCC's %08x)", mixed_collision_model_path(model),
		(unsigned long)tag_index, objects, (unsigned)mixed_coop.tags[scenery].digest,
		(unsigned)mixed_collision_held[model].digest_before);
	return TRUE;
}

/* (the host's) a model's bytes from machine_index: checked (mixed_collision_model_rebuild's checks, the game's own
tag check as a scene pack's, its digest one of the Xbox's) and held. TRUE if held */
static boolean mixed_collision_take_model(
	long machine_index,
	int model,
	uint8_t const *bytes,
	uint32_t length)
{
	/* (the region's first bytes: a tag header and its one instance, as tag_validate_pack_tags takes a pack's, and
	the tag's name) */
	enum
	{
		HEADER_BYTES = 0x24,
		INSTANCE_BYTES = 0x20,
		NAME_BYTES = 64,
		RESERVE = HEADER_BYTES + INSTANCE_BYTES + NAME_BYTES,
	};
	struct pack_header
	{
		void *instances;
		long scenario_tag_index;
		unsigned long checksum;
		long tag_count;
		long vertex_buffer_count;
		void *vertex_buffers;
		long index_buffer_count;
		void *index_buffers;
		unsigned long signature;
	} *header;
	struct cache_file_tag_instance_view *instance;
	uint8_t digest[MIXED_COLLISION_DIGEST_BYTES];
	char digest_text[MIXED_COLLISION_DIGEST_BYTES * 2 + 1];
	uint8_t *region;
	uint32_t region_bytes, root_offset;
	char why[160];
	long corrections = 0;

	typedef char pack_header_size_assert[sizeof(struct pack_header) == HEADER_BYTES ? 1 : -1];
	typedef char pack_instance_size_assert[sizeof(struct cache_file_tag_instance_view) == INSTANCE_BYTES ? 1 : -1];

	why[0] = 0;
	crypto_blake2b(digest, sizeof(digest), bytes, length);
	mixed_collision_digest_text(digest, digest_text);
	/* (first its digest: only the Xbox's files' own bytes go further - then every check of them all the same) */
	if (!mixed_collision_digest_known(model, digest) && !mixed_collision_tests_any_digest())
	{
		platform_log("mixed play: machine %ld's %s collision not taken: its digest %s is none of the Xbox's files'",
			machine_index, mixed_collision_model_path(model), digest_text);
		return FALSE;
	}
	if (!mixed_collision_model_rebuild(bytes, length, RESERVE, &region, &region_bytes, &root_offset, why, sizeof(why)))
	{
		platform_log("mixed play: machine %ld's %s collision not taken: %s", machine_index, mixed_collision_model_path(model),
			why);
		return FALSE;
	}
	header = (struct pack_header *)region;
	instance = (struct cache_file_tag_instance_view *)(region + HEADER_BYTES);
	header->instances = instance;
	header->scenario_tag_index = NONE;
	header->tag_count = 1;
	header->signature = 'tags';
	instance->group_tag = 'coll';
	instance->parent_group_tags[0] = NONE;
	instance->parent_group_tags[1] = NONE;
	instance->tag_index = (long)0xE7A10000UL;
	instance->name = (char *)(region + HEADER_BYTES + INSTANCE_BYTES);
	snprintf(instance->name, NAME_BYTES, "%s", mixed_collision_model_path(model));
	instance->base_address = region + root_offset;
	if (!tag_validate_pack_tags(region, (long)region_bytes, 0, 0, "mixed play collision", &corrections) || corrections)
	{
		platform_log("mixed play: machine %ld's %s collision not taken: the game's tag check refused it (%ld corrections)",
			machine_index, mixed_collision_model_path(model), corrections);
		mixed_coop_free(region);
		return FALSE;
	}
	mixed_coop_free(mixed_collision_held[model].region);
	csmemset(&mixed_collision_held[model], 0, sizeof(mixed_collision_held[model]));
	mixed_collision_held[model].region = region;
	mixed_collision_held[model].region_bytes = region_bytes;
	mixed_collision_held[model].root = region + root_offset;
	snprintf(mixed_collision_held[model].name, sizeof(mixed_collision_held[model].name), "%s", mixed_collision_model_path(model));
	mixed_collision_held[model].tag_index = NONE;
	platform_log("mixed play: machine %ld's %s collision taken (%lu bytes, digest %s%s), held in memory for this hosting",
		machine_index, mixed_collision_model_path(model), (unsigned long)length, digest_text,
		mixed_collision_digest_known(model, digest) ? ", the Xbox's" : ", ANY DIGEST (test)");
	return TRUE;
}

/* (HALO_MIXED_COOP_DIGEST_DUMP) the flag stand's and teleporter base's collision models of this map as an Xbox machine
would send them: their sizes and digests (the digests mixed_collision_known lists were measured so) */
static void mixed_collision_dump_models(
	void)
{
	static uint8_t bytes[MIXED_COLLISION_MAXIMUM_MODEL_BYTES];
	short model;

	for (model = 0; model < NUMBER_OF_MIXED_COLLISION_MODELS; model++)
	{
		long scenery = mixed_coop_find_tag('scen', mixed_collision_model_path(model));
		struct object_definition const *definition = scenery != NONE ? mixed_coop_instances()[scenery].base_address : NULL;
		struct cache_file_tag_instance_view *collision = definition ? mixed_coop_instance(definition->object.collision_model.index) : NULL;
		uint8_t digest[MIXED_COLLISION_DIGEST_BYTES];
		char text[MIXED_COLLISION_DIGEST_BYTES * 2 + 1], why[160];
		uint32_t length;

		if (!collision || collision->group_tag != 'coll' || !collision->base_address)
		{
			platform_log("mixed collision digest: %d none (%s)", (int)model, definition ? "no collision model" : "no scenery");
			continue;
		}
		why[0] = 0;
		length = mixed_collision_model_write(collision->base_address, bytes, sizeof(bytes), why, sizeof(why));
		if (!length)
		{
			platform_log("mixed collision digest: %d not written: %s", (int)model, why);
			continue;
		}
		crypto_blake2b(digest, sizeof(digest), bytes, length);
		mixed_collision_digest_text(digest, text);
		platform_log("mixed collision digest: %d %lu %s %s", (int)model, (unsigned long)length, text,
			mixed_collision_digest_known(model, digest) ? "known" : "unknown");
		/* (HALO_MIXED_COLLISION_HEX: the bytes too, 64 a line - the fuzz target's seeds) */
		if (getenv("HALO_MIXED_COLLISION_HEX"))
		{
			uint32_t at;

			for (at = 0; at < length; at += 64)
			{
				char line[129];
				uint32_t index;

				for (index = 0; index < 64 && at + index < length; index++)
					sprintf(line + index * 2, "%02x", bytes[at + index]);
				platform_log("mixed collision hex: %d %s", (int)model, line);
			}
		}
	}
}

/* the gameplay digest: the gameplay tags' digests by path, in path order */
static uint32_t mixed_coop_gameplay_digest(
	void)
{
	uint32_t gameplay = MIXED_COOP_FNV_BASIS;
	struct mixed_coop_local_tag *sorted;
	long index;

	sorted = malloc(sizeof(*sorted) * (size_t)MAX(1, mixed_coop.tag_count));
	if (!sorted)
		return gameplay;
	csmemcpy(sorted, mixed_coop.tags, sizeof(*sorted) * (size_t)mixed_coop.tag_count);
	qsort(sorted, (size_t)mixed_coop.tag_count, sizeof(*sorted), mixed_coop_compare_tags);
	for (index = 0; index < mixed_coop.tag_count; index++)
	{
		if (index + 1 < mixed_coop.tag_count && sorted[index].path_hash == sorted[index + 1].path_hash)
			platform_log("mixed co-op: two tags of one path hash %08x (left out of the translation)",
				(unsigned)sorted[index].path_hash);
		if (sorted[index].kind == _mixed_tag_gameplay)
		{
			gameplay = mixed_coop_fnv(gameplay, &sorted[index].path_hash, 4);
			gameplay = mixed_coop_fnv(gameplay, &sorted[index].digest, 4);
		}
	}
	mixed_coop_free(sorted);
	return gameplay;
}

void mixed_coop_tags_loaded(
	long scenario_index,
	int custom_edition)
{
	struct cache_file_tag_instance_view *scenario_instance;
	struct tag_schema_group const *scenario_group;
	struct network_game *game;
	long index;
	boolean dump = getenv("HALO_MIXED_COOP_DIGEST_DUMP") != NULL;
	boolean force = getenv("HALO_MIXED_COOP_FORCE") != NULL;
	uint32_t gameplay = MIXED_COOP_FNV_BASIS;
	struct mixed_coop_local_tag *sorted;

	mixed_coop_free_tables();
	mixed_coop_layout_free();
	/* (1.1.3: the held collision's runtime tags went with the last map; let go once this machine hosts no more) */
	{
		short model;

		for (model = 0; model < NUMBER_OF_MIXED_COLLISION_MODELS; model++)
		{
			mixed_collision_held[model].grafted = FALSE;
			mixed_collision_held[model].tag_index = NONE;
		}
		if (game_connection() != _game_connection_network_server)
			mixed_collision_free_held();
	}
	mixed_coop.stand_ins = 0;
	mixed_coop.layout_taken = FALSE;
	collision_bsp_invisible_masked = NULL;
	csmemset(mixed_coop.bsp_digests, 0, sizeof(mixed_coop.bsp_digests));
	csmemset(mixed_coop.host_bsp_digests, 0, sizeof(mixed_coop.host_bsp_digests));
	mixed_coop.loaded = FALSE;
	mixed_coop.network_coop = FALSE;
	mixed_coop.extras_removed = mixed_coop.scripts_disabled = mixed_coop.rules_applied = 0;
	mixed_coop.matched = FALSE;
	mixed_coop.requested = FALSE;
	mixed_coop.needs_dictionary = FALSE;
	/* (a host's: the machines that asked for the last map's dictionary ask again for this one's - its BSP digests
	were sent to them as this map loaded, before the game's time began: a dedicated server's map cycle with an MCC
	machine in it halted, game_time_get's assertion, as the next map loaded) */
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_MACHINES; index++)
		mixed_coop.machines[index].requested = FALSE;
	mixed_coop.unmapped_tags = mixed_coop.unmapped_animations = 0;
	scenario_instance = mixed_coop_instance(scenario_index);
	if (getenv("HALO_MIXED_COOP_DIGEST_DUMP"))
		platform_log("mixed co-op: map loaded: scenario %08lx (%s), %ld tags, connection %d", (unsigned long)scenario_index,
			scenario_instance && scenario_instance->name ? scenario_instance->name : "?", halo_loaded_tag_count,
			(int)game_connection());
	if (!scenario_instance || !scenario_instance->name || !mixed_coop_capable())
		return;
	mixed_coop.level = mixed_coop_level_index(scenario_instance->name);
	if (mixed_coop.level < 0)
		return;
	/* (a network game on a campaign level, with no game engine: co-op; or on
	a mixed multiplayer map, whatever its game) */
	game = network_game_get_game();
	mixed_coop.network_coop = (game_connection() == _game_connection_network_client ||
		game_connection() == _game_connection_network_server) && game &&
		/* (a PC host's game, the crossplay wire: only a ChupathingyCE host that said it plays mixed, over Delta
		Peer - mixed_coop_delta.h) */
		(!network_wire_crossplay() || mixed_coop_crossplay_host() || mixed_coop.client_only) &&
		(mixed_coop_level_is_multiplayer(mixed_coop.level) || game->variant.game_engine_index == 0);
	/* (a multiplayer map's scenario has one name in both families' files:
	the game's name says which game - MCC's own, mcc_chillout, is not mixed) */
	if (mixed_coop.network_coop && mixed_coop_level_is_multiplayer(mixed_coop.level) &&
		mixed_coop_level_index(game->map.name) != mixed_coop.level)
	{
		mixed_coop.network_coop = FALSE;
	}
	if (!mixed_coop.network_coop && !dump && !force)
		return;
	mixed_coop.family = custom_edition ? _mixed_coop_family_mcc : _mixed_coop_family_xbox;
	mixed_coop_multiplayer_level = mixed_coop_level_is_multiplayer(mixed_coop.level);
	mixed_coop_race_vehicle_flags = FALSE;
	if (mixed_coop_multiplayer_level)
	{
		struct scenario const *scenario = scenario_instance->base_address;
		long flag;

		for (flag = 0; flag < scenario->netgame_flags.count && scenario->netgame_flags.address; flag++)
			mixed_coop_race_vehicle_flags |= ((struct scenario_netgame_flag const *)scenario->netgame_flags.address)[flag].type ==
				_netgame_flag_race_vehicle;
#ifdef HALO_DEDICATED_SERVER
		/* (a dedicated server's race gives each player a vehicle where it
		spawns: race_vehicles.c) */
		mixed_coop_race_vehicle_flags = TRUE;
#endif
		/* (1.1.3: what of an MCC host's game an Xbox machine is refused for can come into play here,
		mixed_coop_xbox_host_tag_in_play) */
		platform_log("mixed play: %s (%s files): flag stands placed %ld, teleporter bases placed %ld, the needler item "
			"collection spawns in game types 0x%02x (bits: 1 CTF, 2 Slayer, 3 Oddball, 4 King, 5 Race)",
			mixed_coop_level_stems[mixed_coop.level], custom_edition ? "MCC" : "Xbox",
			mixed_coop_scenery_placed(scenario, mixed_coop_xbox_host_decides[0]),
			mixed_coop_scenery_placed(scenario, mixed_coop_xbox_host_decides[1]),
			mixed_coop_collection_game_types(scenario, mixed_coop_xbox_host_decides[2]));
	}
	mixed_coop.scenario_index = scenario_index;
	mixed_coop.tag_count = halo_loaded_tag_count;
	mixed_coop.bsp_count = 0;
	mixed_coop_walk_custom_edition = custom_edition != 0;
	mixed_coop_trace_filter = getenv("HALO_MIXED_COOP_TRACE");
	scenario_group = mixed_coop_schema_group('scnr', FALSE);
	if (!scenario_group || !scenario_group->definition)
		return;
	/* (r4) an MCC machine in an Xbox-files host's game on Blood Gulch, Battle Creek or Sidewinder: the Xbox tags'
	stand-ins named the Xbox's, then the host's placements in place of MCC's (mixed_coop_layout_apply) */
	if (mixed_coop.family == _mixed_coop_family_mcc && mixed_coop_host_layout_level(mixed_coop.level))
	{
		boolean xbox_host = mixed_coop.network_coop && game_connection() == _game_connection_network_client &&
			mixed_coop.host_said && mixed_coop.host_family == _mixed_coop_family_xbox;

		if (xbox_host || force)
			mixed_coop_stand_ins_apply();
		/* (1.1.3, tools/mixed_tables.py: the MCC file's placements, as a client by a table finds them) */
		if (force && !mixed_coop.network_coop)
			mixed_coop_table_dump_layout("mcc", (struct scenario const *)scenario_instance->base_address);
		/* (1.1.3, a host that never said it plays mixed: the layout this build's table gives) */
		if (xbox_host && mixed_coop.client_only)
			mixed_coop_client_only_layout((struct scenario const *)scenario_instance->base_address, scenario_group->definition);
		if (xbox_host && !mixed_coop.leaving)
			mixed_coop_layout_apply((struct scenario *)scenario_instance->base_address, scenario_group->definition);
	}
	/* (an MCC level played in network co-op: the Xbox's) */
	if (mixed_coop.family == _mixed_coop_family_mcc && (mixed_coop.network_coop || force))
	{
		mixed_coop_make_xbox_scenario((struct scenario *)scenario_instance->base_address, scenario_group->definition);
		mixed_coop_rules();
	}
	if (mixed_coop.network_coop || force)
		mixed_coop_decisions_apply();
	/* (r4, an Xbox-files host of Blood Gulch, Battle Creek or Sidewinder: its palettes' entries nothing places -
	Sidewinder's landing beacon, which MCC's file has not - name no tag, on the host as on an MCC machine taking its
	layout: their layouts' digests then alike) */
	if (mixed_coop.family == _mixed_coop_family_xbox && mixed_coop_host_layout_level(mixed_coop.level) &&
		((mixed_coop.network_coop && global_network_game_server_get()) || force))
	{
		mixed_coop_layout_unplaced_entries((struct scenario *)scenario_instance->base_address, scenario_group->definition);
	}
	{
		struct scenario const *scenario = scenario_instance->base_address;

		mixed_coop.bsp_count = (short)MIN(scenario->structure_bsp_references.count, MIXED_COOP_MAXIMUM_BSPS);
	}
	mixed_coop.tags = malloc(sizeof(*mixed_coop.tags) * (size_t)MAX(1, mixed_coop.tag_count));
	if (!mixed_coop.tags)
		return;
	{
		long reached = mixed_coop_compute_reach(scenario_instance->base_address, scenario_group->definition);

		platform_log("mixed co-op: %ld of %ld tags can come into play (placed, the AI's, the scripts', the globals' and "
			"what they name)", reached, mixed_coop.tag_count);
	}
	for (index = 0; index < mixed_coop.tag_count; index++)
	{
		struct cache_file_tag_instance_view *instance = &mixed_coop_instances()[index];
		int kind = mixed_coop_tag_kind((unsigned long)instance->group_tag, (unsigned long)instance->parent_group_tags[1] != (unsigned long)NONE ?
			(unsigned long)instance->parent_group_tags[1] : (unsigned long)instance->parent_group_tags[0]);

		/* (the parents: an object's are its own then obje's) */
		if (kind == _mixed_tag_other && (instance->parent_group_tags[0] == 'obje' || instance->parent_group_tags[1] == 'obje'))
			kind = _mixed_tag_gameplay;
		if (instance->group_tag == 'ssce' || instance->group_tag == 'lifi')
			kind = _mixed_tag_other;
		/* (a tag nothing in play names: told at most) */
		if (kind == _mixed_tag_gameplay && mixed_coop_reach && !mixed_coop_reach[index])
			kind = _mixed_tag_told;
		mixed_coop.tags[index].path_hash = instance->name ? mixed_coop_path_hash((uint32_t)instance->group_tag, instance->name) : 0;
		mixed_coop.tags[index].kind = (uint8_t)kind;
		mixed_coop.tags[index].ambiguous = 0;
		mixed_coop.tags[index].digest = kind != _mixed_tag_other ? mixed_coop_tag_digest(index) : 0;
	}
	/* (1.1.3, an MCC host of a multiplayer map: the flag stand and teleporter base collision this hosting holds,
	from an Xbox machine's own files, given to their scenery before anything is placed) */
	if (mixed_coop.network_coop && mixed_collision_host_lacks(mixed_coop.level))
	{
		short model;

		for (model = 0; model < NUMBER_OF_MIXED_COLLISION_MODELS; model++)
			mixed_collision_graft(model, FALSE);
	}
	mixed_coop_scenario_digests(scenario_instance->base_address, scenario_group->definition);
	(void)gameplay;
	(void)sorted;
	mixed_coop.digests.gameplay = mixed_coop_gameplay_digest();
	mixed_coop.digests.tag_count = (uint32_t)mixed_coop.tag_count;
	mixed_coop.loaded = TRUE;
	if (dump && mixed_coop_multiplayer_level && mixed_coop.family == _mixed_coop_family_xbox)
		mixed_collision_dump_models();
	platform_log("mixed co-op: %s's %s level %s loaded%s: %ld tags, layout %08x, gameplay %08x, host's alone %08x "
		"(%ld MCC-only placements left out, %ld scripts not started, %ld Xbox rules)",
		mixed_coop_level_stems[mixed_coop.level], mixed_coop_family_name(mixed_coop.family),
		mixed_coop.network_coop ? "in network co-op" : "", mixed_coop.network_coop ? "" : " (digests only)",
		mixed_coop.tag_count, (unsigned)mixed_coop.digests.layout, (unsigned)mixed_coop.digests.gameplay,
		(unsigned)mixed_coop.digests.host_only, mixed_coop.extras_removed, mixed_coop.scripts_disabled,
		mixed_coop.rules_applied);
	mixed_coop_dump();
}

/* ---------- a structure BSP's collision */

static uint32_t mixed_coop_block_hash(
	uint32_t hash,
	struct tag_block const *block,
	long element_size)
{
	hash = mixed_coop_fnv(hash, &block->count, sizeof(block->count));
	if (block->count > 0 && block->address && block->count <= 0x100000)
		hash = mixed_coop_fnv(hash, block->address, (uint32_t)(block->count * element_size));
	return hash;
}

/* a collision block of floats and longs, its floats on a grid: normals in
steps of 1/4096, distances and points of 1/64 of a unit, the grid moved by
offset steps (0.5 or 0). MCC's BSPs have a value or two a hair off the
Xbox's (a10: one 2D plane a float step off in 14,625; c40's BSP 2: a plane,
a vertex and a 2D plane 0.0005 units off): any one value that close falls
in the same step of at least one of the two grids half a step apart */
static uint32_t mixed_coop_quantized_hash(
	uint32_t hash,
	struct tag_block const *block,
	long element_size,
	char const *layout,
	real offset)
{
	long index;

	hash = mixed_coop_fnv(hash, &block->count, sizeof(block->count));
	if (block->count <= 0 || !block->address || block->count > 0x100000)
		return hash;
	for (index = 0; index < block->count; index++)
	{
		byte const *element = (byte const *)block->address + index * element_size;
		short field;

		for (field = 0; layout[field]; field++)
		{
			long value;

			if (layout[field] == 'l')
			{
				csmemcpy(&value, element + field * 4, 4);
			}
			else
			{
				real number;

				csmemcpy(&number, element + field * 4, 4);
				value = (long)floorf(number * (layout[field] == 'n' ? 4096.0f : 64.0f) + offset);
			}
			hash = mixed_coop_fnv(hash, &value, sizeof(value));
		}
	}
	return hash;
}

static uint32_t mixed_coop_collision_digest(
	struct structure_bsp const *structure_bsp,
	real offset)
{
	uint32_t hash = MIXED_COOP_FNV_BASIS;
	long index;

	if (!structure_bsp)
		return 0;
	for (index = 0; index < structure_bsp->collision_bsp.count && structure_bsp->collision_bsp.address; index++)
	{
		struct collision_bsp const *bsp = (struct collision_bsp const *)structure_bsp->collision_bsp.address + index;

		if (getenv("HALO_MIXED_COOP_DIGEST_DUMP"))
		{
			platform_log("mixed co-op: bsp collision %ld: nodes %ld %08x planes %ld %08x leaves %ld %08x refs %ld %08x "
				"2d %ld %08x surfaces %ld %08x edges %ld %08x vertices %ld %08x", index,
				bsp->bsp3d.nodes.count, (unsigned)mixed_coop_block_hash(MIXED_COOP_FNV_BASIS, &bsp->bsp3d.nodes, sizeof(struct bsp3d_node)),
				bsp->bsp3d.planes.count, (unsigned)mixed_coop_block_hash(MIXED_COOP_FNV_BASIS, &bsp->bsp3d.planes, sizeof(real_plane3d)),
				bsp->leaves.count, (unsigned)mixed_coop_block_hash(MIXED_COOP_FNV_BASIS, &bsp->leaves, 8),
				bsp->bsp2d_references.count, (unsigned)mixed_coop_block_hash(MIXED_COOP_FNV_BASIS, &bsp->bsp2d_references, 8),
				bsp->bsp2d.nodes.count, (unsigned)mixed_coop_block_hash(MIXED_COOP_FNV_BASIS, &bsp->bsp2d.nodes, sizeof(struct bsp2d_node)),
				bsp->surfaces.count, (unsigned)mixed_coop_block_hash(MIXED_COOP_FNV_BASIS, &bsp->surfaces, sizeof(struct collision_surface)),
				bsp->edges.count, (unsigned)mixed_coop_block_hash(MIXED_COOP_FNV_BASIS, &bsp->edges, sizeof(struct collision_edge)),
				bsp->vertices.count, (unsigned)mixed_coop_block_hash(MIXED_COOP_FNV_BASIS, &bsp->vertices, sizeof(struct collision_vertex)));
		}
		if (getenv("HALO_MIXED_COOP_BSP2D_DUMP") && bsp->bsp3d.planes.address && bsp->vertices.address)
		{
			long item;

			for (item = 0; item < bsp->bsp3d.planes.count; item++)
			{
				real_plane3d const *plane = (real_plane3d const *)bsp->bsp3d.planes.address + item;

				platform_log("mixed plane: %ld %ld %.9g %.9g %.9g %.9g", bsp->vertices.count, item, plane->n.i, plane->n.j,
					plane->n.k, plane->d);
			}
			for (item = 0; item < bsp->vertices.count; item++)
			{
				struct collision_vertex const *vertex = (struct collision_vertex const *)bsp->vertices.address + item;

				platform_log("mixed vertex: %ld %ld %.9g %.9g %.9g %ld", bsp->vertices.count, item, vertex->point.x,
					vertex->point.y, vertex->point.z, vertex->first_edge_index);
			}
		}
		if (getenv("HALO_MIXED_COOP_BSP2D_DUMP") && bsp->bsp2d.nodes.address)
		{
			long node;

			for (node = 0; node < bsp->bsp2d.nodes.count; node++)
			{
				struct bsp2d_node const *entry = (struct bsp2d_node const *)bsp->bsp2d.nodes.address + node;

				platform_log("mixed bsp2d: %ld %.9g %.9g %.9g %ld %ld", node, entry->plane.n.i, entry->plane.n.j, entry->plane.d,
					entry->child_indices[0], entry->child_indices[1]);
			}
		}
		hash = mixed_coop_block_hash(hash, &bsp->bsp3d.nodes, sizeof(struct bsp3d_node));
		hash = mixed_coop_quantized_hash(hash, &bsp->bsp3d.planes, sizeof(real_plane3d), "nnnd", offset);
		hash = mixed_coop_block_hash(hash, &bsp->leaves, 8 /* collision_bsp.c's struct collision_leaf */);
		hash = mixed_coop_block_hash(hash, &bsp->bsp2d_references, 8 /* collision_bsp.c's struct bsp2d_reference */);
		hash = mixed_coop_quantized_hash(hash, &bsp->bsp2d.nodes, sizeof(struct bsp2d_node), "nndll", offset);
		hash = mixed_coop_block_hash(hash, &bsp->surfaces, sizeof(struct collision_surface));
		hash = mixed_coop_block_hash(hash, &bsp->edges, sizeof(struct collision_edge));
		hash = mixed_coop_quantized_hash(hash, &bsp->vertices, sizeof(struct collision_vertex), "dddl", offset);
	}
	return hash ? hash : 1;
}

static void mixed_coop_leave(char const *format, ...);

/* (the digests, mixed_coop_structure_bsp_loaded's: r3, measured) */
#define DAMNATION_XBOX_0 0xb60e576bu
#define DAMNATION_XBOX_1 0xc0d695a4u
#define DAMNATION_MCC_0 0xc2699b27u
#define DAMNATION_MCC_1 0xaf7f7ee2u
/* the BSP collision differences the owner accepted, by level and BSP: both
families' digests (either grid), the Xbox's then MCC's. Boarding Action's two
ladders, up to 10 cm apart (Nov 14 2026) */
static struct
{
	char const *stem;
	short bsp_index;
	uint32_t xbox_digests[2];
	uint32_t mcc_digests[2];
} const mixed_coop_accepted_bsps[] =
{
	{ "boardingaction", 0, { 0x7c801536u, 0x82774102u }, { 0xd16ec4b6u, 0x5c96dbdeu } },
	/* (Damnation: Halo PC recompiled the BSP and added invisible two-sided surfaces alone - with them
	colliding with nothing, mixed_coop_invisible_mask, the Xbox's surfaces exactly: no owner's call) */
	{ "damnation", 0, { DAMNATION_XBOX_0, DAMNATION_XBOX_1 }, { DAMNATION_MCC_0, DAMNATION_MCC_1 } },
};

/* the maps whose MCC collision is the Xbox's with its invisible surfaces colliding with nothing: on an MCC
machine in an Xbox-files host's game, that machine's structure BSP's are (the host's simulation has none) */
static char const *const mixed_coop_invisible_mask_maps[] =
{
	"damnation",
	/* (Derelict and Hang 'Em High, rebuilt: their Xbox BSPs have no invisible surface either, PRIOR-ART.md -
	the mask takes Halo PC's invisible walls away; the rest of their collision differs, the host's deciding) */
	"carousel", "hangemhigh",
	/* (r4: Blood Gulch, Battle Creek, Sidewinder - Halo PC's invisible walls; their Xbox BSPs have none) */
	"bloodgulch", "beavercreek", "sidewinder",
};

/* (test: HALO_MIXED_COLLISION_PROBE=<step in wu>) the structure BSP's collision as a grid of rays (down from
above the world bounds, and along x and along y at four heights): each ray's first hit distance (1/64 wu) and
plane normal (1/256), from a 3-D grid of points six ways 8 wu each, hashed per row and written to the log, so two machines' logs compare surface for surface */
static void mixed_coop_collision_probe(
	void)
{
	char const *setting = getenv("HALO_MIXED_COLLISION_PROBE");
	struct structure_bsp *structure_bsp = global_structure_bsp_get();
	struct collision_bsp const *bsp = global_collision_bsp_get();
	real step = setting ? (real)atof(setting) : 0.0f;
	real_rectangle3d bounds;
	long rays = 0, hits = 0;
	real x, y, z;
	static real const directions[6][3] = { { 8, 0, 0 }, { -8, 0, 0 }, { 0, 8, 0 }, { 0, -8, 0 }, { 0, 0, 8 }, { 0, 0, -8 } };

	if (step <= 0.0f || !structure_bsp || !bsp)
		return;
	bounds = structure_bsp->world_bounds;
	for (z = bounds.z0; z <= bounds.z1; z += step)
	{
		for (y = bounds.y0; y <= bounds.y1; y += step)
		{
			uint32_t row = MIXED_COOP_FNV_BASIS;

			for (x = bounds.x0; x <= bounds.x1; x += step)
			{
				real_point3d point;
				short direction;

				point.x = x; point.y = y; point.z = z;
				for (direction = 0; direction < 6; direction++)
				{
					real_vector3d vector;
					struct collision_bsp_test_vector_result result;
					int32_t sample[4] = { -1, 0, 0, 0 };

					vector.i = directions[direction][0]; vector.j = directions[direction][1]; vector.k = directions[direction][2];
					rays++;
					if (collision_bsp_test_vector(0, bsp, 0, NULL, &point, &vector, 1.0f, &result))
					{
						hits++;
						sample[0] = (int32_t)floor(result.t * 8.0f * 64.0f + 0.5f);
						sample[1] = result.plane ? (int32_t)floor(result.plane->n.i * 256.0f + 0.5f) : 0;
						sample[2] = result.plane ? (int32_t)floor(result.plane->n.j * 256.0f + 0.5f) : 0;
						sample[3] = result.plane ? (int32_t)floor(result.plane->n.k * 256.0f + 0.5f) : 0;
					}
					row = mixed_coop_fnv(row, sample, sizeof(sample));
				}
			}
			platform_log("mixed collision probe 3 %.3f/%.3f %08x", z, y, (unsigned)row);
		}
	}
	platform_log("mixed collision probe: %ld rays, %ld hits (step %.3f)", rays, hits, step);
}

static void mixed_coop_invisible_mask(
	void)
{
	short index;

	collision_bsp_invisible_masked = NULL;
	/* (HALO_MIXED_INVISIBLE_MASK=0: MCC's collision as it is - a mixed Damnation game then differs) */
	if (getenv("HALO_MIXED_INVISIBLE_MASK") && !strcmp(getenv("HALO_MIXED_INVISIBLE_MASK"), "0"))
		return;
	if (mixed_coop.level < 0 || mixed_coop.family != _mixed_coop_family_mcc ||
		mixed_coop.host_family != _mixed_coop_family_xbox)
	{
		return;
	}
	for (index = 0; index < NUMBEROF(mixed_coop_invisible_mask_maps); index++)
	{
		if (!csstrcasecmp(mixed_coop_level_stems[mixed_coop.level], mixed_coop_invisible_mask_maps[index]))
		{
			collision_bsp_invisible_masked = global_collision_bsp_get();
			platform_log("mixed play: %s's invisible surfaces collide with nothing on this machine (Halo PC added "
				"them; the Xbox host's %s has none)", mixed_coop_invisible_mask_maps[index],
				mixed_coop_invisible_mask_maps[index]);
		}
	}
}

static boolean mixed_coop_bsp_accepted(
	long bsp_index,
	uint32_t host_digest,
	uint32_t host_digest_offset,
	uint32_t digest,
	uint32_t digest_offset)
{
	short index;

	/* (a map Halo PC rebuilt, an Xbox-files host's game and this machine MCC's: the Xbox-host-decides mode -
	the host's simulation decides where every player is, this machine's prediction corrected to it) */
	if (mixed_coop_rebuilt_level(mixed_coop.level) && mixed_coop.host_family == _mixed_coop_family_xbox &&
		mixed_coop.family == _mixed_coop_family_mcc)
	{
		platform_log("mixed play: %s's BSP %ld: Halo PC rebuilt it; the Xbox-files host's collision decides",
			mixed_coop_level_stems[mixed_coop.level], bsp_index);
		return TRUE;
	}
	for (index = 0; index < NUMBEROF(mixed_coop_accepted_bsps); index++)
	{
		uint32_t const *xd = mixed_coop_accepted_bsps[index].xbox_digests, *md = mixed_coop_accepted_bsps[index].mcc_digests;

		if (mixed_coop.level < 0 || csstrcasecmp(mixed_coop_level_stems[mixed_coop.level], mixed_coop_accepted_bsps[index].stem) ||
			bsp_index != mixed_coop_accepted_bsps[index].bsp_index)
		{
			continue;
		}
		if ((host_digest == xd[0] && host_digest_offset == xd[1] && digest == md[0] && digest_offset == md[1]) ||
			(host_digest == md[0] && host_digest_offset == md[1] && digest == xd[0] && digest_offset == xd[1]))
		{
			platform_log("mixed play: %s's BSP %ld: the Xbox's and MCC's collision differ as the owner accepted",
				mixed_coop_accepted_bsps[index].stem, bsp_index);
			return TRUE;
		}
	}
	return FALSE;
}

static boolean mixed_coop_build_stream(void);

/* (1.1.3, tools/mixed_tables.py) a stream as hex lines in the log ("mixed table hex: <what> <level> <family>
<offset> <hex>"), which the tool reads back */
static void mixed_coop_table_hex(
	char const *what,
	char const *family,
	uint8_t const *bytes,
	uint32_t length)
{
	uint32_t at;

	for (at = 0; at < length; at += 256)
	{
		char hex[2 * 256 + 1];
		uint32_t byte_index, count = MIN(256u, length - at);

		for (byte_index = 0; byte_index < count; byte_index++)
			snprintf(hex + 2 * byte_index, 3, "%02x", bytes[at + byte_index]);
		hex[2 * count] = 0;
		platform_log("mixed table hex: %s %s %s %lu %s", what, mixed_coop_level_stems[mixed_coop.level], family,
			(unsigned long)at, hex);
	}
	platform_log("mixed table: %s's %s (%s files) written to the log (%lu bytes)", mixed_coop_level_stems[mixed_coop.level],
		what, family, (unsigned long)length);
}

/* (1.1.3) a map's layout for a built-in table (an Xbox machine's once its objects are placed, the placements' BSP
flags its own; an MCC machine's as the client by a table finds it, before the Xbox's is made of it) */
static void mixed_coop_table_dump_layout(
	char const *family,
	struct scenario const *scenario)
{
	struct tag_schema_group const *scenario_group = mixed_coop_schema_group('scnr', FALSE);

	if (!getenv("HALO_MIXED_TABLE_OUT") || !getenv("HALO_MIXED_COOP_FORCE") || !mixed_coop_host_layout_level(mixed_coop.level) ||
		!scenario || !scenario_group || !scenario_group->definition)
	{
		return;
	}
	mixed_coop_table_writing = TRUE;
	mixed_coop_layout_build(scenario, scenario_group->definition);
	mixed_coop_table_writing = FALSE;
	if (mixed_coop.layout_stream && mixed_coop.layout_length)
		mixed_coop_table_hex("layout", family, mixed_coop.layout_stream, mixed_coop.layout_length);
	mixed_coop_free(mixed_coop.layout_stream);
	mixed_coop.layout_stream = NULL;
	mixed_coop.layout_length = 0;
}

/* (1.1.3, tools/mixed_tables.py) a built-in table's stream written: an Xbox-files machine with
HALO_MIXED_COOP_FORCE and HALO_MIXED_TABLE_OUT=1 loading a mixed level alone writes the dictionary an
Xbox-files host would send for it, without the netgame flags' places, as hex lines in its log, once its first
structure BSP is loaded (its collision digests in it; later BSPs' 0, not compared) */
boolean pc_menu_tag(long tag_index);

static void mixed_coop_table_write(
	void)
{
	long table_full_count;

	if (!getenv("HALO_MIXED_TABLE_OUT") || !getenv("HALO_MIXED_COOP_FORCE") || mixed_coop.family != _mixed_coop_family_xbox ||
		mixed_coop.network_coop || game_connection() == _game_connection_network_client ||
		game_connection() == _game_connection_network_server)
	{
		return;
	}
	mixed_coop_free(mixed_coop.stream);
	mixed_coop.stream = NULL;
	/* (the map file's own tags alone: the menus' tags this machine added after them, menu_tags.c, are left out - they
	are added or not as the menus load, and no stock host has them) */
	{
		long count = mixed_coop.tag_count;

		while (count > 0 && pc_menu_tag(mixed_coop_instances()[count - 1].tag_index))
			count--;
		if (count != mixed_coop.tag_count)
			platform_log("mixed table: %ld menu tags after the map's %ld left out", mixed_coop.tag_count - count, count);
		table_full_count = mixed_coop.tag_count;
		mixed_coop.tag_count = count;
	}
	mixed_coop_table_writing = TRUE;
	if (!mixed_coop_build_stream())
	{
		mixed_coop.tag_count = table_full_count;
		mixed_coop_table_writing = FALSE;
		platform_log("mixed table: %s's stream could not be built", mixed_coop_level_stems[mixed_coop.level]);
		return;
	}
	mixed_coop_table_writing = FALSE;
	mixed_coop.tag_count = table_full_count;
	mixed_coop_table_hex("table", "xbox", mixed_coop.stream, mixed_coop.stream_length);
	mixed_coop_free(mixed_coop.stream);
	mixed_coop.stream = NULL;
}

void mixed_coop_structure_bsp_loaded(
	short bsp_index,
	struct structure_bsp const *structure_bsp)
{
	uint32_t digest, digest_offset;

	if (!mixed_coop.loaded || bsp_index < 0 || bsp_index >= MIXED_COOP_MAXIMUM_BSPS)
		return;
	digest = mixed_coop_collision_digest(structure_bsp, 0.5f);
	digest_offset = mixed_coop_collision_digest(structure_bsp, 0.0f);
	if (mixed_coop.bsp_digests[bsp_index][0] == digest && mixed_coop.bsp_digests[bsp_index][1] == digest_offset)
		return;
	mixed_coop.bsp_digests[bsp_index][0] = digest;
	mixed_coop.bsp_digests[bsp_index][1] = digest_offset;
	platform_log("mixed co-op: structure BSP %d's collision %08x %08x", bsp_index, (unsigned)digest, (unsigned)digest_offset);
	if (bsp_index == 0)
		mixed_coop_table_write();
	/* (the test's probe on the host: its BSP as it is; a client's once it matched, mixed_coop_dictionary_take) */
	if (global_network_game_server_get())
		mixed_coop_collision_probe();
	/* (the host: to every client that asked for its dictionary - a BSP switched in play; the first, as the map loads
	before the game's time begins, is in the dictionary each machine asks for) */
	if (global_network_game_server_get() && mixed_coop.network_coop && game_time_initialized())
	{
		short slot;

		for (slot = 0; slot < HALO_PORT_MAXIMUM_NETWORK_MACHINES; slot++)
		{
			if (mixed_coop.machines[slot].known && mixed_coop.machines[slot].requested)
			{
				struct
				{
					struct distributed_message_header header;
					struct mixed_coop_bsp_digest entry;
				} message;

				csmemset(&message, 0, sizeof(message));
				message.entry.session = mixed_coop.session;
				message.entry.bsp_index = (uint16_t)bsp_index;
				message.entry.digest = digest;
				message.entry.digest_offset = digest_offset;
				distributed_send_to_machine_reliably(mixed_coop.machines[slot].machine_index, &message,
					DISTRIBUTED_MESSAGE_MIXED_BSP, 1, (word)sizeof(message));
			}
		}
	}
	/* (a client: against the host's, when it has it) */
	else if (mixed_coop.needs_dictionary && mixed_coop.host_bsp_digests[bsp_index][0] &&
		mixed_coop.host_bsp_digests[bsp_index][0] != digest && mixed_coop.host_bsp_digests[bsp_index][1] != digest_offset &&
		!mixed_coop_bsp_accepted(bsp_index, mixed_coop.host_bsp_digests[bsp_index][0], mixed_coop.host_bsp_digests[bsp_index][1],
			digest, digest_offset))
	{
		mixed_coop_leave(T("The host's %s and yours differ in the level's collision (BSP %d): Xbox and MCC files "
			"can't play this level together."), mixed_coop_level_stems[mixed_coop.level], bsp_index);
	}
}

/* ---------- the lobby: the host's */

static struct network_game_server *mixed_coop_hosted_server(
	int *level)
{
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *game;

	*level = -1;
	/* (tests, 1.1.3: HALO_TEST_MIXED_HOST_SILENT=1 hosts as a build without mixed play does - no hello, no answer -
	for a client playing by its table: the state hashes of both still logged) */
	if (!server || network_wire_crossplay() || network_game_is_splitscreen_local() || !mixed_coop_capable() ||
		getenv("HALO_TEST_MIXED_HOST_SILENT"))
	{
		return NULL;
	}
	game = network_game_server_get_game(server);
	if (!game)
		return NULL;
	*level = mixed_coop_level_index(game->map.name);
	/* (a campaign level: co-op alone, no game engine) */
	if (*level >= 0 && !mixed_coop_level_is_multiplayer(*level) && game->variant.game_engine_index != 0)
		*level = -1;
	return *level >= 0 ? server : NULL;
}

static boolean mixed_coop_server_send(
	long machine_index,
	struct mixed_coop_lobby_message const *message)
{
	void *encoded = create_network_game_message(_message_server_mixed_coop, message, sizeof(*message));

	return encoded && network_game_server_port_send_to_machine(machine_index, encoded);
}

static void mixed_coop_client_start_update(void);
static void mixed_coop_layout_send(long machine_index);
static void mixed_coop_client_delta_wants(void);

/* (the host's) a machine's slot let go: its collision stream's reassembly with it */
static void mixed_coop_machine_release(
	short index)
{
	mixed_coop_free(mixed_coop.machines[index].collision_reassembly);
	csmemset(&mixed_coop.machines[index], 0, sizeof(mixed_coop.machines[index]));
}

static void mixed_coop_machines_release(
	void)
{
	short index;

	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_MACHINES; index++)
		mixed_coop_machine_release(index);
}

void mixed_coop_frame(
	void)
{
	int level;
	struct network_game_server *server;
	long machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	unsigned long now;
	short count, index;
	int family;

	/* (a joiner of a PC host's game: Delta Peer's datagrams, and what it still wants of the host's streams) */
	mixed_coop_delta_frame();
	mixed_coop_client_delta_wants();
	/* (r4, a joiner's: its game's start held for the host's layout) */
	if (mixed_coop.start_pending)
		mixed_coop_client_start_update();
	server = mixed_coop_hosted_server(&level);
	if (!server)
	{
		if (mixed_coop.hosting && !global_network_game_server_get())
		{
			mixed_coop.hosting = FALSE;
			mixed_coop_machines_release();
		}
		return;
	}
	now = system_milliseconds();
	if (!mixed_coop.hosting)
	{
		mixed_coop.hosting = TRUE;
		mixed_coop.hello_level = NONE;
		mixed_coop.session = (uint32_t)now ^ ((uint32_t)rand() << 16) ^ 0x4D584350u;
		mixed_coop_machines_release();
		platform_log(mixed_coop_level_is_multiplayer(level) ? "mixed play: hosting %s from the %s files: each machine joining is told" :
			"mixed co-op: hosting co-op on %s from the %s files: each machine joining is told",
			mixed_coop_level_stems[level], mixed_coop_family_name(mixed_coop_local_family(level)));
	}
	family = mixed_coop_local_family(level);
	if (family == _mixed_coop_family_none || network_game_client_get_local_machine_index() == NONE)
		return;
	/* (r4: another map chosen - a map cycle's next game -: each machine told again, its family for that one) */
	if (level != mixed_coop.hello_level)
	{
		if (mixed_coop.hello_level != NONE)
			platform_log("mixed play: the game's map is %s now: each machine told again", mixed_coop_level_stems[level]);
		mixed_coop.hello_level = level;
		for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_MACHINES; index++)
		{
			mixed_coop.machines[index].answered = FALSE;
			mixed_coop.machines[index].hellos = 0;
			mixed_coop.machines[index].hello_time = 0;
			mixed_coop.machines[index].collision_asks = 0;
			mixed_coop.machines[index].collision_ask_time = 0;
			mixed_coop.machines[index].collision_offer_heard = FALSE;
			mixed_coop.machines[index].collision_offered = 0;
			mixed_coop.machines[index].collision_done = FALSE;
			mixed_coop_free(mixed_coop.machines[index].collision_reassembly);
			mixed_coop.machines[index].collision_reassembly = NULL;
		}
	}
	count = network_game_server_port_joined_machines(machines, (short)NUMBEROF(machines));
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_MACHINES; index++)
	{
		short at;
		boolean still = FALSE;

		if (!mixed_coop.machines[index].known)
			continue;
		for (at = 0; at < count; at++)
			still |= machines[at] == mixed_coop.machines[index].machine_index;
		if (!still)
			mixed_coop_machine_release(index);
	}
	for (index = 0; index < count; index++)
	{
		long machine_index = machines[index];

		if (machine_index == network_game_client_get_local_machine_index() ||
			!VALID_INDEX(machine_index, HALO_PORT_MAXIMUM_NETWORK_MACHINES))
		{
			continue;
		}
		if (!mixed_coop.machines[machine_index].known)
		{
			csmemset(&mixed_coop.machines[machine_index], 0, sizeof(mixed_coop.machines[machine_index]));
			mixed_coop.machines[machine_index].known = TRUE;
			mixed_coop.machines[machine_index].machine_index = machine_index;
		}
		/* (said until answered: an older build never answers, and plays as
		before) */
		if (!mixed_coop.machines[machine_index].answered && mixed_coop.machines[machine_index].hellos < 10 &&
			(!mixed_coop.machines[machine_index].hello_time ||
				now - mixed_coop.machines[machine_index].hello_time >= MIXED_COOP_HELLO_RESEND_MILLISECONDS))
		{
			struct mixed_coop_lobby_message hello;

			csmemset(&hello, 0, sizeof(hello));
			hello.command = _mixed_coop_lobby_hello;
			hello.version = (uint16_t)mixed_coop_version_for_level((unsigned)level);
			hello.family = (uint8_t)family;
			hello.level = (uint8_t)level;
			hello.session = mixed_coop.session;
			if (mixed_coop_server_send(machine_index, &hello))
			{
				mixed_coop.machines[machine_index].hello_time = now ? now : 1;
				mixed_coop.machines[machine_index].hellos++;
			}
		}
		/* (1.1.3) an MCC host of a multiplayer map: a machine of the Xbox's files asked for the flag stand and
		teleporter base collision this host lacks (mixed_collision_protocol.h), until it offers (an older build
		never does: it drops the ask, and is refused as before) */
		if (mixed_coop.machines[machine_index].answered && mixed_coop.machines[machine_index].family == _mixed_coop_family_xbox &&
			!mixed_coop.machines[machine_index].collision_offer_heard && mixed_coop.machines[machine_index].collision_asks < 10 &&
			mixed_collision_missing(level) &&
			(!mixed_coop.machines[machine_index].collision_ask_time ||
				now - mixed_coop.machines[machine_index].collision_ask_time >= MIXED_COOP_HELLO_RESEND_MILLISECONDS))
		{
			struct mixed_coop_lobby_message ask;

			csmemset(&ask, 0, sizeof(ask));
			ask.command = _mixed_coop_lobby_collision_ask;
			ask.version = (uint16_t)mixed_coop_version_for_level((unsigned)level);
			ask.family = _mixed_coop_family_mcc;
			ask.level = (uint8_t)level;
			ask.flags = mixed_collision_missing(level);
			ask.session = mixed_coop.session;
			if (mixed_coop_server_send(machine_index, &ask))
			{
				if (!mixed_coop.machines[machine_index].collision_asks)
					platform_log("mixed play: machine %ld (Xbox files) asked for the collision this host's files lack "
						"(flags %u)", machine_index, (unsigned)ask.flags);
				mixed_coop.machines[machine_index].collision_ask_time = now ? now : 1;
				mixed_coop.machines[machine_index].collision_asks++;
			}
		}
		/* (its dictionary request, held for the collision it offered: answered once that is taken or refused, or
		MIXED_COLLISION_WAIT_MILLISECONDS on) */
		if (mixed_coop.machines[machine_index].request_held &&
			(mixed_coop.machines[machine_index].collision_done ||
				now - mixed_coop.machines[machine_index].request_held_since >= MIXED_COLLISION_WAIT_MILLISECONDS))
		{
			struct mixed_coop_request request = mixed_coop.machines[machine_index].held_request;

			if (!mixed_coop.machines[machine_index].collision_done)
				platform_log("mixed play: machine %ld's collision never came whole: its dictionary sent without it",
					machine_index);
			mixed_coop.machines[machine_index].collision_done = TRUE;
			mixed_coop.machines[machine_index].request_held = FALSE;
			mixed_coop_handle_request(machine_index, &request, 1);
		}
		/* (r4) its layout to a machine that asked, once this host's map is loaded and its objects placed (its
		placements' BSP flags this host's own: mixed_coop_layout_build), made at the first ask */
		if (mixed_coop.machines[machine_index].layout_next && mixed_coop.loaded && mixed_coop.level == level &&
			mixed_coop.family == _mixed_coop_family_xbox && mixed_coop_host_layout_level(level) && game_map_loaded())
		{
			struct tag_schema_group const *scenario_group = mixed_coop_schema_group('scnr', FALSE);

			if (!mixed_coop.layout_stream && global_scenario_get() && scenario_group && scenario_group->definition)
				mixed_coop_layout_build(global_scenario_get(), scenario_group->definition);
			if (mixed_coop.layout_stream && mixed_coop.layout_length)
				mixed_coop_layout_send(machine_index);
		}
	}
}

/* (r4) the host's layout to one machine, a few fragments a frame (the lobby's queue to it not crowded) */
static void mixed_coop_layout_send(
	long machine_index)
{
	static struct mixed_coop_layout_message fragment;
	uint16_t count = (uint16_t)((mixed_coop.layout_length + MIXED_COOP_LAYOUT_FRAGMENT_BYTES - 1) / MIXED_COOP_LAYOUT_FRAGMENT_BYTES);
	short sent;

	for (sent = 0; sent < 4 && mixed_coop.machines[machine_index].layout_next; sent++)
	{
		uint16_t index = (uint16_t)(mixed_coop.machines[machine_index].layout_next - 1);
		uint32_t offset = (uint32_t)index * MIXED_COOP_LAYOUT_FRAGMENT_BYTES;
		void *encoded;

		csmemset(&fragment, 0, sizeof(fragment));
		fragment.session = mixed_coop.session;
		fragment.total_bytes = mixed_coop.layout_length;
		fragment.index = index;
		fragment.count = count;
		fragment.bytes = (uint16_t)MIN(MIXED_COOP_LAYOUT_FRAGMENT_BYTES, mixed_coop.layout_length - offset);
		fragment.level = (uint8_t)mixed_coop.level;
		fragment.version = MIXED_COOP_LAYOUT_VERSION;
		csmemcpy(fragment.data, mixed_coop.layout_stream + offset, fragment.bytes);
		encoded = create_network_game_message(_message_server_mixed_layout, &fragment, sizeof(fragment));
		if (!encoded || !network_game_server_port_send_to_machine(machine_index, encoded))
			return;
		mixed_coop.machines[machine_index].layout_next = index + 1u < count ? (short)(index + 2) : 0;
		if (!mixed_coop.machines[machine_index].layout_next)
			platform_log("mixed play: this host's layout of %s sent to machine %ld (%lu bytes, %u fragments)",
				mixed_coop_level_stems[mixed_coop.level], machine_index, (unsigned long)mixed_coop.layout_length,
				(unsigned)count);
	}
}

static boolean mixed_coop_lobby_decode(
	unsigned short *message,
	short message_size,
	short expected_type,
	long packet_class,
	struct mixed_coop_lobby_message *decoded)
{
	short packet_type = expected_type;
	short packet_version = HALO_PORT_NETWORK_GAME_MESSAGE_VERSION;

	message_size -= sizeof(unsigned short);
	if (message_size <= 0 ||
		!decode_network_game_message(decoded, message + 1, &message_size, &packet_type, &packet_version, packet_class) ||
		packet_type != expected_type)
	{
		return FALSE;
	}
	return mixed_coop_lobby_valid(decoded);
}

void mixed_coop_server_handle_lobby(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine,
	unsigned short *message,
	short message_size)
{
	/* (tests: a host without mixed play drops these unread) */
	if (getenv("HALO_TEST_MIXED_HOST_SILENT"))
		return;
	struct mixed_coop_lobby_message answer;
	long machine_index = NONE;

	if (!mixed_coop_lobby_decode(message, message_size, _message_client_mixed_coop, MIXED_COOP_CLASS_CLIENT_PREGAME, &answer) ||
		(answer.command != _mixed_coop_lobby_answer && answer.command != _mixed_coop_lobby_layout_request &&
			answer.command != _mixed_coop_lobby_collision_offer))
	{
		network_event("mixed co-op: an answer that could not be decoded or checked");
		return;
	}
	network_game_server_get_client_machine(server, machine, &machine_index);
	if (!mixed_coop.hosting || !VALID_INDEX(machine_index, HALO_PORT_MAXIMUM_NETWORK_MACHINES) ||
		!mixed_coop.machines[machine_index].known || answer.session != mixed_coop.session)
	{
		return;
	}
	/* (r4) an MCC machine's ask for this host's layout (Blood Gulch ...), its game starting: sent once this
	host's map is loaded (mixed_coop_frame) */
	if (answer.command == _mixed_coop_lobby_layout_request)
	{
		int level;

		if (mixed_coop_hosted_server(&level) && answer.level == (uint8_t)level && mixed_coop_host_layout_level(level) &&
			mixed_coop_local_family(level) == _mixed_coop_family_xbox && !mixed_coop.machines[machine_index].layout_next)
		{
			platform_log("mixed play: machine %ld asks for this host's layout of %s", machine_index,
				mixed_coop_level_stems[level]);
			mixed_coop.machines[machine_index].layout_next = 1;
		}
		return;
	}
	/* (1.1.3) an Xbox machine's offer of the collision this host asked it for: its dictionary request is held for it
	(mixed_coop_handle_request); none offered: nothing waited for */
	if (answer.command == _mixed_coop_lobby_collision_offer)
	{
		int level;

		if (mixed_coop_hosted_server(&level) && answer.level == (uint8_t)level && mixed_collision_missing(level) &&
			mixed_coop.machines[machine_index].family == _mixed_coop_family_xbox &&
			!mixed_coop.machines[machine_index].collision_offer_heard)
		{
			mixed_coop.machines[machine_index].collision_offer_heard = TRUE;
			mixed_coop.machines[machine_index].collision_offered = (uint16_t)(answer.flags & mixed_collision_missing(level));
			platform_log("mixed play: machine %ld offers the collision (flags %u): its dictionary waits for it",
				machine_index, (unsigned)mixed_coop.machines[machine_index].collision_offered);
		}
		return;
	}
	if (!mixed_coop.machines[machine_index].answered)
		platform_log("mixed co-op: machine %ld plays this level from the %s files", machine_index,
			mixed_coop_family_name(answer.family));
	mixed_coop.machines[machine_index].answered = TRUE;
	mixed_coop.machines[machine_index].family = answer.family;
}

/* ---------- the lobby: a client's */

static void mixed_coop_client_take_hello(struct network_game_client *client, struct mixed_coop_lobby_message const *message);

void mixed_coop_client_handle_lobby(
	struct network_game_client *client,
	unsigned short *message,
	short message_size)
{
	struct mixed_coop_lobby_message hello;

	if (!mixed_coop_lobby_decode(message, message_size, _message_server_mixed_coop, MIXED_COOP_CLASS_SERVER_PREGAME, &hello) ||
		(hello.command != _mixed_coop_lobby_hello && hello.command != _mixed_coop_lobby_collision_ask))
	{
		network_event("mixed co-op: a host's message that could not be decoded or checked");
		return;
	}
	/* (1.1.3) an MCC host's ask for the flag stand and teleporter base collision: offered, if this machine plays the
	map from the Xbox's files - sent once it has loaded it (mixed_coop_client_send_collision) */
	if (hello.command == _mixed_coop_lobby_collision_ask)
	{
		struct mixed_coop_lobby_message offer;
		void *encoded;

		if (!mixed_coop_capable() || global_network_game_server_get() || !mixed_coop.host_said ||
			hello.session != mixed_coop.host_session || hello.level != (uint8_t)mixed_coop.host_level ||
			mixed_coop.host_family != _mixed_coop_family_mcc ||
			mixed_coop_local_family(hello.level) != _mixed_coop_family_xbox)
		{
			return;
		}
		if (mixed_coop.collision_asked != (hello.flags & MIXED_COLLISION_ALL_BITS) ||
			mixed_coop.collision_asked_session != hello.session)
		{
			platform_log("mixed play: the MCC host asks for the collision its files lack (flags %u): offered, sent once "
				"the map is loaded", (unsigned)hello.flags);
		}
		mixed_coop.collision_asked = (uint16_t)(hello.flags & MIXED_COLLISION_ALL_BITS);
		mixed_coop.collision_asked_session = hello.session;
		csmemset(&offer, 0, sizeof(offer));
		offer.command = _mixed_coop_lobby_collision_offer;
		offer.version = hello.version;
		offer.family = _mixed_coop_family_xbox;
		offer.level = hello.level;
		offer.flags = mixed_coop.collision_asked;
		offer.session = hello.session;
		encoded = create_network_game_message(_message_client_mixed_coop, &offer, sizeof(offer));
		if (!encoded || !network_game_client_send_to_server(client, encoded))
			network_event("mixed play: the collision offer could not be sent");
		return;
	}
	mixed_coop_client_take_hello(client, &hello);
}

/* (the crossplay wire) a ChupathingyCE host's MIXED_HELLO over Delta Peer (mixed_coop_delta.h), checked there as
the Bungie message is */
void mixed_coop_delta_heard_hello(
	struct mixed_coop_lobby_message const *hello)
{
	struct network_game_client *client = global_network_game_client_get();

	if (!client || !network_wire_crossplay())
		return;
	mixed_coop_client_take_hello(client, hello);
}

/* the host's hello, its family for its level: this machine's answered */
static void mixed_coop_client_take_hello(
	struct network_game_client *client,
	struct mixed_coop_lobby_message const *message)
{
	struct mixed_coop_lobby_message hello = *message, answer;
	void *encoded;

	if (!mixed_coop_capable() || global_network_game_server_get())
		return;
	/* (1.1.3: playing by this build's table already - a hello this late is the game's no more) */
	if (mixed_coop.client_only)
	{
		platform_log("mixed play: the host's hello came after this machine took its table: the table stays");
		return;
	}
	if (!mixed_coop.host_said || mixed_coop.host_session != hello.session)
		platform_log("mixed co-op: the host plays %s from the %s files; this machine from the %s files",
			mixed_coop_level_stems[hello.level], mixed_coop_family_name(hello.family),
			mixed_coop_family_name(mixed_coop_local_family(hello.level)));
	if (mixed_coop.host_session != hello.session)
		mixed_coop.collision_asked = 0;
	mixed_coop.host_said = TRUE;
	mixed_coop.host_family = hello.family;
	mixed_coop.host_level = hello.level;
	mixed_coop.host_session = hello.session;
	csmemset(&answer, 0, sizeof(answer));
	answer.command = _mixed_coop_lobby_answer;
	answer.version = (uint16_t)mixed_coop_version_for_level(hello.level);
	answer.family = (uint8_t)mixed_coop_local_family(hello.level);
	answer.level = hello.level;
	answer.session = hello.session;
	/* (a PC host's game: over Delta Peer) */
	if (network_wire_crossplay())
	{
		if (!mixed_coop_delta_send_answer(&answer, 0, NULL))
			network_event("mixed co-op: the answer could not be sent");
		return;
	}
	encoded = create_network_game_message(_message_client_mixed_coop, &answer, sizeof(answer));
	if (!encoded || !network_game_client_send_to_server(client, encoded))
		network_event("mixed co-op: the answer could not be sent");
}

int mixed_coop_crossplay_may_play(
	char const *level_name)
{
	int level = mixed_coop_level_index(level_name);

	return level >= 0 && mixed_coop_capable() && !network_game_is_splitscreen_local() &&
		mixed_coop_local_family(level) == _mixed_coop_family_mcc;
}

int mixed_coop_host_level_acceptable(
	char const *level_name,
	unsigned long host_identity)
{
	int level = mixed_coop_level_index(level_name);
	int family = mixed_coop_local_family(level);

	if (level < 0 || !mixed_coop_capable())
		return FALSE;
	/* (an MCC host: this machine plays it from either family's files - a map Halo PC rebuilt from MCC's alone:
	an Xbox machine is refused, told it needs an Xbox-files host, mixed_coop_multiplayer_refusal) */
	if (host_identity == MIXED_COOP_MCC_IDENTITY)
	{
		if (family == _mixed_coop_family_none)
			return FALSE;
		if (family == _mixed_coop_family_xbox && mixed_coop_rebuilt_level(level))
			return FALSE;
		return TRUE;
	}
	/* (an Xbox host, or an older one: with MCC's files alone, only once the
	host has said it plays mixed co-op - looked at again as the game loads,
	mixed_coop_client_ready_allowed - its lobby's hello may come after its
	settings) */
	return !host_identity && family == _mixed_coop_family_mcc;
}

int mixed_coop_xbox_layout_here(
	void)
{
	return mixed_coop.layout_taken;
}

int mixed_coop_host_decides_here(
	void)
{
	return mixed_coop.matched && !global_network_game_server_get() && mixed_coop_rebuilt_level(mixed_coop.level) &&
		mixed_coop.host_family == _mixed_coop_family_xbox && mixed_coop.family == _mixed_coop_family_mcc;
}

int mixed_coop_host_decides_machine(
	long machine_index)
{
	return VALID_INDEX(machine_index, HALO_PORT_MAXIMUM_NETWORK_MACHINES) && global_network_game_server_get() &&
		mixed_coop.loaded && mixed_coop_rebuilt_level(mixed_coop.level) && mixed_coop.family == _mixed_coop_family_xbox &&
		mixed_coop.machines[machine_index].known && mixed_coop.machines[machine_index].family == _mixed_coop_family_mcc;
}

/* ---------- leaving */

static void mixed_coop_leave(
	char const *format,
	...)
{
	va_list arguments;

	if (mixed_coop.leaving)
		return;
	va_start(arguments, format);
	vsnprintf(mixed_coop.leave_text, sizeof(mixed_coop.leave_text), format, arguments);
	va_end(arguments);
	mixed_coop.leaving = TRUE;
	platform_log("mixed co-op: refused: %s", mixed_coop.leave_text);
	error(_error_silent, "mixed co-op: refused: %s", mixed_coop.leave_text);
}

/* ---------- (1.1.3, the owner's 8l) a host that never says it plays mixed: refused before the map loads */

/* (network_client_manager.c's states: the lobby) */
#define MIXED_COOP_CLIENT_STATE_PREGAME 2
/* how long a joiner of MCC's files waits for an Xbox-files host's hello (sent at once to a machine that joins, again
each second, mixed_coop_frame) before it takes the host for an older build that cannot play mixed */
#define MIXED_COOP_HELLO_WAIT_MILLISECONDS 8000

/* the mixed level (`*level`) of `game` this machine would play from MCC's files with a host that has not said it
plays mixed: the host's file the Xbox's (an MCC host names its level MIXED_COOP_MCC_IDENTITY) and no hello yet -
an Xbox-files host of this build says so within a second or two; an older one (1.1.2, the official servers'
1.1.1 build) never does, and its game cannot be played with MCC's files */
static boolean mixed_coop_client_awaits_hello(
	struct network_game const *game,
	int *level)
{
	*level = -1;
	/* (the crossplay wire too since a ChupathingyCE host may say it plays mixed, over Delta Peer:
	mixed_coop_delta.h) */
	if (!game || !mixed_coop_capable() || global_network_game_server_get() ||
		network_game_is_splitscreen_local() || mixed_coop.host_said || game->map.version)
	{
		return FALSE;
	}
	*level = mixed_coop_level_index(game->map.name);
	if (*level < 0 || mixed_coop_local_family(*level) != _mixed_coop_family_mcc)
		return FALSE;
	/* (a campaign level: co-op alone - mixed_coop_hosted_server, the host says nothing of another game there) */
	if (!mixed_coop_level_is_multiplayer(*level) && game->variant.game_engine_index != 0)
		return FALSE;
	return TRUE;
}

static void mixed_coop_refuse_older_host(
	struct network_game *game,
	int level,
	char const *where)
{
	platform_log("mixed play: the host never said it plays mixed (an older version): %s refused, %s", mixed_coop_level_stems[level],
		where);
	mixed_coop.leave_level = level + 1;
	/* (a PC host's game, the crossplay wire: refused as before mixed play with PC hosts, in the same words -
	custom_edition_maps.c's) */
	if (network_wire_crossplay())
	{
		mixed_coop_delta_end();
		mixed_coop_leave("%s", T("this is a PC game, and your maps are MCC's: this PC host can only "
			"be joined with the Xbox's maps. Mixed MCC and Xbox games need a Halo CE Vita host or a ChupathingyCE host with "
			"mixed hosting on"));
		return;
	}
	if (mixed_coop_level_is_multiplayer(level))
	{
		/* (no "co-op": a multiplayer game, 8l's Boarding Action on an official server) */
		mixed_coop_leave(T("This game's host plays %s from the Xbox's files on an older version that can't play it with MCC's "
			"files: the host needs to update its game, or join with your Xbox files."),
			game ? custom_edition_maps_level_title(game->map.name) : mixed_coop_level_stems[level]);
	}
	else
	{
		mixed_coop_leave(T("The host plays %s from the Xbox's files and its version can't play co-op with MCC's "
			"files: update the host's game, or play with your Xbox files."), mixed_coop_level_stems[level]);
	}
}

/* ---------- (1.1.3) a host that never says it plays mixed: played by the built-in table */

void platform_ask_question(char const *title, char const *text);
int platform_question_answer(void);
int config_write_string(const char *name, const char *value);

/* whether the player said yes, once, to playing this map Halo PC rebuilt on MCC's files with a host that can't send
its layout (network.mixed_differs_accepted: the levels' stems, commas between) */
static boolean mixed_coop_differs_remembered(
	int level)
{
	char const *list = config_string("network.mixed_differs_accepted");
	char const *stem = mixed_coop_level_stems[level];
	size_t length = strlen(stem);

	while (list && *list)
	{
		char const *end = strchr(list, ',');
		size_t part = end ? (size_t)(end - list) : strlen(list);

		while (part && *list == ' ')
			list++, part--;
		while (part && list[part - 1] == ' ')
			part--;
		if (part == length && !strncasecmp(list, stem, length))
			return TRUE;
		if (!end)
			break;
		list = end + 1;
	}
	return FALSE;
}

static void mixed_coop_differs_remember(
	int level)
{
	char const *list = config_string("network.mixed_differs_accepted");
	char updated[256];

	if (mixed_coop_differs_remembered(level))
		return;
	snprintf(updated, sizeof(updated), "%s%s%s", list && list[0] ? list : "", list && list[0] ? "," : "",
		mixed_coop_level_stems[level]);
	if (!config_write_string("network.mixed_differs_accepted", updated))
		platform_log("mixed play: the answer for %s could not be kept", mixed_coop_level_stems[level]);
}

/* (tests) HALO_TEST_MIXED_DIFFERS_ANSWER=yes or no answers the question at once: 1, 0, or -1 (asked) */
static int mixed_coop_differs_test_answer(
	void)
{
	char const *answer = getenv("HALO_TEST_MIXED_DIFFERS_ANSWER");

	if (!answer || !answer[0])
		return -1;
	return !strcasecmp(answer, "yes") || !strcmp(answer, "1") ? 1 : 0;
}

/* whether this machine plays `level` by its table: 1 yes (a map whose files match, or a map Halo PC rebuilt the
player said yes to, now or once before), 0 the player said no (the game left, told so), -1 the player is asked (the
game's start held, asked again each frame) */
static int mixed_coop_client_only_answer(
	int level)
{
	int answer;

	if (!mixed_coop_rebuilt_level(level) || mixed_coop_differs_remembered(level))
		return 1;
	if (mixed_coop.differs_asked_level != level + 1)
	{
		mixed_coop.differs_asked_level = level + 1;
		answer = mixed_coop_differs_test_answer();
		if (answer < 0)
		{
			platform_log("mixed play: %s is Halo PC's version on this machine's files and the host can't send its "
				"layout: the player is asked", mixed_coop_level_stems[level]);
			platform_ask_question(T("Halo: map"), T("This map is Halo PC's version on your files: some walls and "
				"scenery won't match the host's. Play anyway?"));
			return -1;
		}
	}
	else
	{
		answer = platform_question_answer();
		if (answer < 0)
			return -1;
	}
	mixed_coop.differs_asked_level = 0;
	platform_log("mixed play: %s on Halo PC's version, the host's layout unknown: the player said %s",
		mixed_coop_level_stems[level], answer ? "play anyway" : "no");
	if (!answer)
	{
		mixed_coop.leave_level = level + 1;
		mixed_coop_leave(T("Not joined: %s is Halo PC's version on your MCC files, and this host can't send its layout. "
			"Join with the Xbox's maps to play it as the host does."), custom_edition_maps_level_title(
			mixed_coop_level_stems[level]));
		return 0;
	}
	mixed_coop_differs_remember(level);
	return 1;
}

/* (1.1.3) the host never said it plays mixed (an older build, an OpenCE or ChupathingyCE host): this machine plays
by its built-in table of the Xbox's level, as it would by the host's dictionary - 1; the player asked, -1; else
refused as before (no table, network.mixed_client_only off, or the player said no) - 0 */
static int mixed_coop_client_only_take(
	struct network_game *game,
	int level,
	char const *where)
{
	int answer;

	if (mixed_coop.client_only && mixed_coop.host_level == level)
		return 1;
	if (level < 0 || level >= MIXED_COOP_LEVELS || !mixed_coop_table_present(level) ||
		!config_boolean("network.mixed_client_only"))
	{
		mixed_coop_refuse_older_host(game, level, where);
		return 0;
	}
	answer = mixed_coop_client_only_answer(level);
	if (answer <= 0)
		return answer;
	/* (Delta Peer, a PC host's: its HELLO offered mixed_maps - this machine said what it plays; the host said
	nothing of it: BYE) */
	if (network_wire_crossplay())
		mixed_coop_delta_end();
	mixed_coop.client_only = TRUE;
	mixed_coop.host_said = TRUE;
	mixed_coop.host_family = _mixed_coop_family_xbox;
	mixed_coop.host_level = level;
	mixed_coop.host_session = 0;
	platform_log("mixed play: the host never said it plays mixed (%s): %s played by this build's table of the Xbox's "
		"level%s, %s", network_wire_crossplay() ? "a PC host" : "an older build", mixed_coop_level_stems[level],
		mixed_coop_rebuilt_level(level) ? " (Halo PC rebuilt it: the host's collision decides)" : "", where);
	return 1;
}

/* (1.1.3) the layout a table gives Blood Gulch, Battle Creek or Sidewinder: the Xbox's placements' structure, each
at the place of the MCC placement the table names (its tag the Xbox's), or that MCC placement itself standing in -
made into a stream as the host's (MXL1), into the layout's reassembly (mixed_coop_layout_apply reads it as the
host's) */
static boolean mixed_coop_client_only_layout(
	struct scenario const *scenario,
	struct tag_schema_definition const *scenario_schema)
{
	uint8_t *table = NULL;
	uint32_t length = 0, skeleton_length;
	struct mixed_coop_layout skeleton;
	struct mixed_coop_dictionary_writer writer;
	char why[96];
	uint8_t const *sources;
	uint32_t source_count = 0, source_at = 0;
	short block;
	boolean ok = FALSE;

	if (!mixed_coop_table_layout(mixed_coop.level, &table, &length))
	{
		mixed_coop_leave(T("This build has no table of %s's placements: Xbox and MCC files can't play it together "
			"with this host."), mixed_coop_level_stems[mixed_coop.level]);
		return FALSE;
	}
	if (length < 8 || mixed_coop_read_u32(table) != MIXED_COOP_TABLE_LAYOUT_MAGIC ||
		(skeleton_length = mixed_coop_read_u32(table + 4)) > length - 8 ||
		!mixed_coop_layout_read(table + 8, skeleton_length, &skeleton, why, sizeof(why)) ||
		skeleton.level != (uint8_t)mixed_coop.level)
	{
		platform_log("mixed play: %s's layout table could not be read", mixed_coop_level_stems[mixed_coop.level]);
		goto done;
	}
	sources = table + 8 + skeleton_length;
	source_count = (length - 8 - skeleton_length) / 4;
	if (!mixed_coop.layout_reassembly)
		mixed_coop.layout_reassembly = malloc(sizeof(*mixed_coop.layout_reassembly));
	if (!mixed_coop.layout_reassembly)
		goto done;
	mixed_coop_layout_reassembly_begin(mixed_coop.layout_reassembly, 0, (uint8_t)mixed_coop.level);
	mixed_coop_writer_begin(&writer, mixed_coop.layout_reassembly->bytes, MIXED_COOP_LAYOUT_MAXIMUM_BYTES);
	mixed_coop_writer_u32(&writer, MIXED_COOP_LAYOUT_MAGIC);
	mixed_coop_writer_u16(&writer, MIXED_COOP_LAYOUT_VERSION);
	mixed_coop_writer_u8(&writer, (uint8_t)mixed_coop.level);
	mixed_coop_writer_u8(&writer, (uint8_t)(2 * NUMBEROF(mixed_coop_layout_blocks) + 1));
	for (block = 0; block < NUMBEROF(mixed_coop_layout_blocks); block++)
	{
		int palette_kind = mixed_coop_layout_blocks[block].palette_kind, kind = mixed_coop_layout_blocks[block].placements_kind;
		struct tag_block const *local_palette = (struct tag_block const *)((byte const *)scenario +
			mixed_coop_layout_blocks[block].palette_offset);
		struct tag_block const *local = (struct tag_block const *)((byte const *)scenario +
			mixed_coop_layout_blocks[block].placements_offset);
		long element_size = mixed_coop_scenario_element_size(scenario_schema, mixed_coop_layout_blocks[block].placements);
		uint32_t palette_count = skeleton.blocks[palette_kind].count, count = skeleton.blocks[kind].count, index;
		uint32_t stand_ins = 0;
		uint8_t const *palette_bytes = skeleton.blocks[palette_kind].elements;
		uint8_t const *placement_bytes = skeleton.blocks[kind].elements;

		if (!palette_bytes || !placement_bytes || skeleton.blocks[palette_kind].element_bytes != MIXED_COOP_LAYOUT_PALETTE_BYTES ||
			skeleton.blocks[kind].element_bytes != (uint16_t)element_size || !local->address || source_at + count > source_count)
		{
			platform_log("mixed play: %s's layout table is another version's", mixed_coop_level_stems[mixed_coop.level]);
			goto done;
		}
		/* (the stand-ins' tags: each one's MCC palette entry, after the Xbox's) */
		for (index = 0; index < count; index++)
		{
			stand_ins += sources[4 * (source_at + index) + 2] == _mixed_coop_table_source_stand_in;
		}
		mixed_coop_writer_u8(&writer, (uint8_t)palette_kind);
		mixed_coop_writer_u8(&writer, 0);
		mixed_coop_writer_u16(&writer, MIXED_COOP_LAYOUT_PALETTE_BYTES);
		mixed_coop_writer_u32(&writer, palette_count + stand_ins);
		for (index = 0; index < palette_count * MIXED_COOP_LAYOUT_PALETTE_BYTES; index++)
			mixed_coop_writer_u8(&writer, palette_bytes[index]);
		for (index = 0; index < count; index++)
		{
			uint16_t source = (uint16_t)(sources[4 * (source_at + index)] | sources[4 * (source_at + index) + 1] << 8);
			struct scenario_object_datum const *mcc;
			short at;

			if (sources[4 * (source_at + index) + 2] != _mixed_coop_table_source_stand_in)
				continue;
			if (source >= local->count)
				goto bad_source;
			mcc = (struct scenario_object_datum const *)((byte const *)local->address + source * element_size);
			if (mcc->palette_entry_index < 0 || mcc->palette_entry_index >= local_palette->count || !local_palette->address)
				goto bad_source;
			{
				struct scenario_object_palette_entry const *entry =
					(struct scenario_object_palette_entry const *)local_palette->address + mcc->palette_entry_index;
				struct cache_file_tag_instance_view *instance = mixed_coop_instance(entry->reference.index);

				mixed_coop_writer_u32(&writer, instance ? (uint32_t)instance->group_tag : 0);
				mixed_coop_writer_u32(&writer, instance && instance->name ? mixed_coop_tag_path_hash(entry->reference.index) : 0);
				for (at = 0; at < MIXED_COOP_LAYOUT_PATH_BYTES; at++)
					mixed_coop_writer_u8(&writer, 0);
			}
		}
		mixed_coop_writer_u8(&writer, (uint8_t)kind);
		mixed_coop_writer_u8(&writer, 0);
		mixed_coop_writer_u16(&writer, (uint16_t)element_size);
		mixed_coop_writer_u32(&writer, count);
		{
			uint32_t next_stand_in = palette_count;

			for (index = 0; index < count; index++)
			{
				uint8_t const *xbox_element = placement_bytes + index * element_size;
				uint16_t source = (uint16_t)(sources[4 * (source_at + index)] | sources[4 * (source_at + index) + 1] << 8);
				uint8_t kind_of_source = sources[4 * (source_at + index) + 2];
				uint8_t element[MIXED_COOP_LAYOUT_MAXIMUM_PLACEMENT_BYTES];
				uint8_t const *mcc;
				long at;

				if (source >= local->count || element_size > (long)sizeof(element) || element_size < 32)
					goto bad_source;
				mcc = (uint8_t const *)local->address + source * element_size;
				csmemcpy(element, xbox_element, (size_t)element_size);
				/* (its place and turn: the MCC placement's - scenario_object_datum's position and rotation) */
				csmemcpy(element + 8, mcc + 8, 24);
				if (kind_of_source == _mixed_coop_table_source_stand_in)
				{
					/* (the MCC object itself, its own tag, at its place: an Xbox placement with nothing of its own near
					on this machine's file, which holds the Xbox's index for the objects after it) */
					short palette_index = (short)next_stand_in++;

					csmemcpy(element, &palette_index, sizeof(palette_index));
				}
				for (at = 0; at < element_size; at++)
					mixed_coop_writer_u8(&writer, element[at]);
			}
		}
		platform_log("mixed play: %s's %s by the table: %lu Xbox placements (%lu stood in by MCC's own), this file's %ld",
			mixed_coop_level_stems[mixed_coop.level], mixed_coop_layout_blocks[block].placements, (unsigned long)count,
			(unsigned long)stand_ins, local->count);
		source_at += count;
	}
	/* (no netgame flags: this machine keeps its own, mixed_coop_layout_apply) */
	mixed_coop_writer_u8(&writer, MIXED_COOP_LAYOUT_NETGAME_FLAGS);
	mixed_coop_writer_u8(&writer, 0);
	mixed_coop_writer_u16(&writer, MIXED_COOP_NETGAME_FLAG_BYTES);
	mixed_coop_writer_u32(&writer, 0);
	mixed_coop.layout_reassembly->total_bytes = mixed_coop_writer_end(&writer);
	if (!mixed_coop.layout_reassembly->total_bytes)
		goto done;
	mixed_coop.layout_complete = TRUE;
	ok = TRUE;
	goto done;
bad_source:
	platform_log("mixed play: %s's layout table names a placement this file has not", mixed_coop_level_stems[mixed_coop.level]);
done:
	mixed_coop_table_free(table);
	if (!ok)
	{
		mixed_coop_leave(T("Your MCC files' %s isn't the one this build's table was made for: Xbox and MCC files can't "
			"play it together with this host."), mixed_coop_level_stems[mixed_coop.level]);
	}
	return ok;
}

/* (the crossplay wire: a PC host's game) the host asked over Delta Peer whether it plays mixed (mixed_coop_delta.h);
its MIXED_HELLO then comes as a Vita host's hello does */
static boolean mixed_coop_crossplay_asked;

static void mixed_coop_crossplay_ask(
	struct network_game_client *client)
{
	struct transport_address address;
	short machine_index = network_game_client_get_local_machine_index();

	if (!network_wire_crossplay() || mixed_coop_crossplay_asked || machine_index == NONE)
		return;
	csmemset(&address, 0, sizeof(address));
	network_game_client_get_remote_server_address(client, &address);
	mixed_coop_delta_begin((unsigned long)address.address.ipv4_address, machine_index, network_wire_version());
	mixed_coop_crossplay_asked = TRUE;
}

/* (the crossplay wire) whether the host's Delta Peer said it does not play mixed (LEGACY, a WELCOME without
mixed_maps, or nothing in 4 s): refused at once rather than at the wait's end */
static boolean mixed_coop_crossplay_said_no(
	void)
{
	return network_wire_crossplay() && mixed_coop_crossplay_asked &&
		(mixed_coop_delta_said_no() || (!mixed_coop_delta_active() && !mixed_coop_delta_agreed()));
}

/* (the lobby's) a host that has not said it plays mixed in MIXED_COOP_HELLO_WAIT_MILLISECONDS: refused there */
static void mixed_coop_client_lobby_update(
	struct network_game_client *client)
{
	struct network_game *game;
	unsigned long now = system_milliseconds();
	int level;

	if (!client || mixed_coop.leaving || mixed_coop.start_pending ||
		network_game_client_get_state(client, NULL) != MIXED_COOP_CLIENT_STATE_PREGAME)
	{
		mixed_coop.lobby_hello_since = 0;
		return;
	}
	game = network_game_client_get_game(client);
	if (!mixed_coop_client_awaits_hello(game, &level))
	{
		mixed_coop.lobby_hello_since = 0;
		return;
	}
	if (!mixed_coop.lobby_hello_since)
	{
		mixed_coop.lobby_hello_since = now ? now : 1;
		platform_log("mixed play: the host's game is on %s, played here from MCC's files: waiting for the host to say it "
			"plays mixed", mixed_coop_level_stems[level]);
	}
	/* (a PC host's game: asked over Delta Peer, which a ChupathingyCE host with mixed play speaks - once this
	machine has its machine index) */
	mixed_coop_crossplay_ask(client);
	/* (a PC host whose Delta Peer said no, or said nothing in its 4 s: refused now) */
	if (now - mixed_coop.lobby_hello_since >= MIXED_COOP_HELLO_WAIT_MILLISECONDS || mixed_coop_crossplay_said_no())
	{
		/* (1.1.3: played by this build's table; the player asked first on a map Halo PC rebuilt - each frame until
		answered) */
		if (mixed_coop_client_only_take(game, level, "in its lobby, before the map loads") < 0)
			return;
		mixed_coop.lobby_hello_since = 0;
	}
}

int mixed_coop_client_update(
	struct network_game_client *client)
{
	mixed_coop_client_lobby_update(client);
	if (!mixed_coop.leaving)
		return TRUE;
	{
		int level = mixed_coop.leave_level ? mixed_coop.leave_level - 1 : mixed_coop.level;

		platform_show_message(mixed_coop_level_is_multiplayer(level) ? T("Halo: map") : T("Halo: co-op"), mixed_coop.leave_text);
	}
	mixed_coop.leaving = FALSE;
	mixed_coop.leave_level = 0;
	return FALSE;
}

void mixed_coop_client_dispose(
	void)
{
	/* (a PC host's game: Delta Peer's BYE) */
	mixed_coop_delta_end();
	mixed_coop_crossplay_asked = FALSE;
	csmemset(&mixed_coop_delta_dictionary, 0, sizeof(mixed_coop_delta_dictionary));
	csmemset(&mixed_coop_delta_layout, 0, sizeof(mixed_coop_delta_layout));
	mixed_coop.host_said = FALSE;
	mixed_coop.host_family = _mixed_coop_family_none;
	mixed_coop.collision_asked = 0;
	mixed_coop.collision_asked_session = 0;
	mixed_coop.needs_dictionary = FALSE;
	mixed_coop.matched = FALSE;
	mixed_coop.requested = FALSE;
	mixed_coop_free(mixed_coop.reassembly);
	mixed_coop.reassembly = NULL;
	mixed_coop_free(mixed_coop.layout_reassembly);
	mixed_coop.layout_reassembly = NULL;
	mixed_coop.layout_complete = FALSE;
	mixed_coop.start_pending = NULL;
	mixed_coop.start_waits_hello = FALSE;
	mixed_coop.lobby_hello_since = 0;
	mixed_coop.client_only = FALSE;
	mixed_coop.start_waits_answer = FALSE;
	if (mixed_coop.differs_asked_level)
		platform_ask_question(NULL, NULL);
	mixed_coop.differs_asked_level = 0;
}

/* ---------- (r4) a joiner's: the host's layout before the map loads */

/* how long a joiner waits for the host's layout once the game starts, and between its asks */
#define MIXED_COOP_LAYOUT_WAIT_MILLISECONDS 45000
#define MIXED_COOP_LAYOUT_ASK_MILLISECONDS 2000

static void mixed_coop_layout_ask(
	struct network_game_client *client,
	int level)
{
	struct mixed_coop_lobby_message ask;
	void *encoded;
	unsigned long now = system_milliseconds();

	csmemset(&ask, 0, sizeof(ask));
	ask.command = _mixed_coop_lobby_layout_request;
	ask.version = (uint16_t)mixed_coop_version_for_level((unsigned)level);
	ask.family = _mixed_coop_family_mcc;
	ask.level = (uint8_t)level;
	ask.session = mixed_coop.host_session;
	/* (a PC host's game: over Delta Peer, with the pieces still wanted) */
	if (network_wire_crossplay())
	{
		uint16_t first;
		uint8_t bits[MIXED_COOP_DELTA_LAYOUT_WANT_BITS / 8];

		mixed_coop_delta_pieces_wanted(&mixed_coop_delta_layout, &first, bits, MIXED_COOP_DELTA_LAYOUT_WANT_BITS);
		if (mixed_coop_delta_send_answer(&ask, first, bits))
			mixed_coop.layout_asked_time = now ? now : 1;
		return;
	}
	encoded = create_network_game_message(_message_client_mixed_coop, &ask, sizeof(ask));
	if (encoded && network_game_client_send_to_server(client, encoded))
		mixed_coop.layout_asked_time = now ? now : 1;
}

int mixed_coop_client_start_waits(
	struct network_game_client *client)
{
	struct network_game *game = client ? network_game_client_get_game(client) : NULL;
	int level;

	if (!game || !mixed_coop_capable() || global_network_game_server_get())
		return FALSE;
	/* (1.1.3, the owner's 8l: an MCC-only Vita joined an official server's game in progress - its 1.1.1 build plays
	no mixed games - loaded MCC's Boarding Action whole, 15 s, and only then was refused) the game's start held for
	the host's hello; a host that never says it plays mixed is refused before the map loads */
	if (mixed_coop_client_awaits_hello(game, &level))
	{
		mixed_coop.host_level = level;
		mixed_coop.start_pending = client;
		mixed_coop.start_pending_since = system_milliseconds();
		mixed_coop.start_waits_hello = TRUE;
		platform_log("mixed play: the game starts on %s, played here from MCC's files: held until the host says it plays mixed",
			mixed_coop_level_stems[level]);
		return TRUE;
	}
	level = mixed_coop_level_index(game->map.name);
	/* (1.1.3, by a table: no layout to ask for - the table has it; a map Halo PC rebuilt asked about first) */
	if (mixed_coop.client_only)
	{
		if (level < 0 || mixed_coop_local_family(level) != _mixed_coop_family_mcc)
			return FALSE;
		mixed_coop.host_level = level;
		if (!mixed_coop_table_present(level))
		{
			mixed_coop.leave_level = level + 1;
			mixed_coop_leave(T("This game's host plays %s from the Xbox's files on a version that can't play it with MCC's "
				"files: the host needs to update its game, or join with your Xbox files."),
				custom_edition_maps_level_title(game->map.name));
			return FALSE;
		}
		if (mixed_coop_client_only_answer(level) >= 0)
			return FALSE;
		mixed_coop.start_waits_answer = TRUE;
		mixed_coop.start_pending = client;
		mixed_coop.start_pending_since = system_milliseconds();
		return TRUE;
	}
	/* (the host's hello may name the map before this one - a map cycle's next game: the host's family as it said,
	the map the game settings name; the host answers for the map it plays) */
	if (!mixed_coop_host_layout_level(level) || !mixed_coop.host_said ||
		mixed_coop.host_family != _mixed_coop_family_xbox || mixed_coop_local_family(level) != _mixed_coop_family_mcc)
	{
		return FALSE;
	}
	mixed_coop.host_level = level;
	if (mixed_coop.layout_complete && mixed_coop.layout_reassembly &&
		mixed_coop.layout_reassembly->session == mixed_coop.host_session && mixed_coop.layout_reassembly->level == (uint8_t)level)
	{
		return FALSE;
	}
	if (!mixed_coop.layout_reassembly)
		mixed_coop.layout_reassembly = malloc(sizeof(*mixed_coop.layout_reassembly));
	if (!mixed_coop.layout_reassembly)
		return FALSE;
	mixed_coop_layout_reassembly_begin(mixed_coop.layout_reassembly, mixed_coop.host_session, (uint8_t)level);
	/* (a PC host's game: its pieces over Delta Peer, into the same bytes) */
	mixed_coop_delta_pieces_begin(&mixed_coop_delta_layout, _mixed_coop_delta_stream_layout, mixed_coop.host_session,
		(uint8_t)level, mixed_coop.layout_reassembly->bytes, MIXED_COOP_LAYOUT_MAXIMUM_BYTES);
	mixed_coop.layout_complete = FALSE;
	mixed_coop.start_pending = client;
	mixed_coop.start_pending_since = system_milliseconds();
	platform_log("mixed play: the game starts on %s: the Xbox-files host's placements asked for before loading it",
		mixed_coop_level_stems[level]);
	mixed_coop_layout_ask(client, level);
	return TRUE;
}

static void mixed_coop_client_start_update(
	void)
{
	struct network_game_client *client = global_network_game_client_get();
	unsigned long now = system_milliseconds();

	if (!client || client != mixed_coop.start_pending ||
		network_game_client_get_state(client, NULL) != MIXED_COOP_CLIENT_STATE_PREGAME)
	{
		platform_log("mixed play: the game left before the host's placements came");
		mixed_coop.start_pending = NULL;
		mixed_coop.start_waits_hello = FALSE;
		return;
	}
	if (mixed_coop.start_waits_hello)
	{
		if (mixed_coop.host_said)
		{
			mixed_coop.start_waits_hello = FALSE;
			mixed_coop.start_pending = NULL;
			platform_log("mixed play: the host said it plays mixed (%lu ms): the game's start goes on",
				now - mixed_coop.start_pending_since);
			/* (its layout next, a map whose placements are the host's; else the map loads now) */
			if (!mixed_coop_client_start_waits(client) && !network_game_client_game_has_started(client))
				network_event("network_game_client_game_has_started() failed");
			return;
		}
		/* (a PC host's game: asked over Delta Peer; its no said at once) */
		mixed_coop_crossplay_ask(client);
		if (now - mixed_coop.start_pending_since >= MIXED_COOP_HELLO_WAIT_MILLISECONDS || mixed_coop_crossplay_said_no())
		{
			/* (1.1.3: played by this build's table, the start held while the player is asked) */
			int taken = mixed_coop_client_only_take(network_game_client_get_game(client), mixed_coop.host_level,
				"as its game starts, before the map loads");

			if (taken < 0)
				return;
			mixed_coop.start_waits_hello = FALSE;
			mixed_coop.start_pending = NULL;
			if (taken > 0 && !network_game_client_game_has_started(client))
				network_event("network_game_client_game_has_started() failed");
		}
		return;
	}
	/* (1.1.3, by a table: a map cycle's next map Halo PC rebuilt, the player asked) */
	if (mixed_coop.start_waits_answer)
	{
		int answer = mixed_coop_client_only_answer(mixed_coop.host_level);

		if (answer < 0)
			return;
		mixed_coop.start_waits_answer = FALSE;
		mixed_coop.start_pending = NULL;
		if (answer > 0 && !network_game_client_game_has_started(client))
			network_event("network_game_client_game_has_started() failed");
		return;
	}
	if (mixed_coop.layout_complete)
	{
		mixed_coop.start_pending = NULL;
		platform_log("mixed play: the host's placements here (%lu bytes, %lu ms): loading the map",
			(unsigned long)mixed_coop.layout_reassembly->total_bytes, now - mixed_coop.start_pending_since);
		if (!network_game_client_game_has_started(client))
			network_event("network_game_client_game_has_started() failed");
		return;
	}
	if (now - mixed_coop.start_pending_since >= MIXED_COOP_LAYOUT_WAIT_MILLISECONDS)
	{
		mixed_coop.start_pending = NULL;
		mixed_coop_leave(T("The host didn't send its placements of %s: with Xbox and MCC files together it needs a "
			"host of this version with the Xbox's maps."), mixed_coop_level_stems[mixed_coop.host_level]);
		return;
	}
	if (now - mixed_coop.layout_asked_time >= (network_wire_crossplay() ? MIXED_COOP_DELTA_WANT_MILLISECONDS :
		MIXED_COOP_LAYOUT_ASK_MILLISECONDS))
	{
		mixed_coop_layout_ask(client, mixed_coop.host_level);
	}
}

void mixed_coop_client_handle_layout(
	struct network_game_client *client,
	unsigned short *message,
	short message_size)
{
	static struct mixed_coop_layout_message fragment;
	struct mixed_coop_layout layout;
	short packet_type = _message_server_mixed_layout;
	short packet_version = HALO_PORT_NETWORK_GAME_MESSAGE_VERSION;
	char why[96];
	int result;

	(void)client;
	message_size -= sizeof(unsigned short);
	csmemset(&fragment, 0, sizeof(fragment));
	if (message_size <= 0 ||
		!decode_network_game_message(&fragment, message + 1, &message_size, &packet_type, &packet_version,
			MIXED_COOP_CLASS_SERVER_PREGAME) || packet_type != _message_server_mixed_layout)
	{
		network_event("mixed play: a host's layout fragment that could not be decoded");
		return;
	}
	if (!mixed_coop.start_pending || !mixed_coop.layout_reassembly || mixed_coop.layout_complete)
		return;
	result = mixed_coop_layout_reassembly_take(mixed_coop.layout_reassembly, &fragment);
	if (result == _mixed_coop_fragment_refused)
	{
		network_event("mixed play: a host's layout fragment refused");
		return;
	}
	if (result != _mixed_coop_fragment_complete)
		return;
	if (!mixed_coop_layout_read(mixed_coop.layout_reassembly->bytes, mixed_coop.layout_reassembly->total_bytes, &layout, why,
		sizeof(why)))
	{
		mixed_coop.start_pending = NULL;
		mixed_coop_leave(T("The host's placements of %s couldn't be read (%s)."), mixed_coop_level_stems[mixed_coop.host_level],
			why);
		return;
	}
	mixed_coop.layout_complete = TRUE;
}

/* ---------- after the load: the host's dictionary */

/* (1.1.3) the family of the machine the dictionary is built for */
static int mixed_coop_stream_family;

static boolean mixed_coop_build_stream(
	void)
{
	struct mixed_coop_dictionary_writer writer;
	uint32_t length;
	long index, graphs = 0;
	uint32_t graph_count_at;

	if (mixed_coop.stream)
		return TRUE;
	mixed_coop.stream = malloc(MIXED_COOP_DICTIONARY_MAXIMUM_BYTES);
	if (!mixed_coop.stream)
		return FALSE;
	mixed_coop_writer_begin(&writer, mixed_coop.stream, MIXED_COOP_DICTIONARY_MAXIMUM_BYTES);
	mixed_coop_writer_u32(&writer, MIXED_COOP_DICTIONARY_MAGIC);
	mixed_coop_writer_u16(&writer, (uint16_t)mixed_coop_version_for_level((unsigned)mixed_coop.level));
	mixed_coop_writer_u8(&writer, (uint8_t)mixed_coop.family);
	mixed_coop_writer_u8(&writer, (uint8_t)mixed_coop.level);
	mixed_coop_writer_u32(&writer, (uint32_t)mixed_coop.tag_count);
	mixed_coop_writer_u32(&writer, mixed_coop.digests.layout);
	mixed_coop_writer_u32(&writer, mixed_coop.digests.gameplay);
	mixed_coop_writer_u32(&writer, mixed_coop.digests.host_only);
	mixed_coop_writer_u32(&writer, mixed_coop.digests.layout_offset);
	mixed_coop_writer_u16(&writer, (uint16_t)mixed_coop.bsp_count);
	mixed_coop_writer_u16(&writer, 0);
	for (index = 0; index < mixed_coop.bsp_count; index++)
	{
		mixed_coop_writer_u32(&writer, mixed_coop.bsp_digests[index][0]);
		mixed_coop_writer_u32(&writer, mixed_coop.bsp_digests[index][1]);
	}
	for (index = 0; index < mixed_coop.tag_count; index++)
	{
		uint32_t digest = mixed_coop.tags[index].digest;
		short model;

		for (model = 0; model < NUMBER_OF_MIXED_COLLISION_MODELS; model++)
		{
			if (mixed_collision_held[model].grafted && mixed_collision_held[model].scenery_absolute == index &&
				mixed_coop_stream_family == _mixed_coop_family_mcc)
			{
				digest = mixed_collision_held[model].digest_before;
			}
		}
		mixed_coop_writer_u32(&writer, mixed_coop.tags[index].path_hash);
		mixed_coop_writer_u32(&writer, digest);
		mixed_coop_writer_u32(&writer, (uint32_t)mixed_coop_instances()[index].tag_index);
	}
	graph_count_at = writer.length;
	mixed_coop_writer_u32(&writer, 0);
	for (index = 0; index < mixed_coop.tag_count && graphs < MIXED_COOP_MAXIMUM_GRAPHS; index++)
	{
		struct cache_file_tag_instance_view *instance = &mixed_coop_instances()[index];
		struct animation_graph const *graph;
		long animation;
		long count;

		if (instance->group_tag != ANIMATION_GRAPH_TAG || !instance->base_address)
			continue;
		graph = instance->base_address;
		count = MIN(graph->animations.count, MIXED_COOP_MAXIMUM_ANIMATIONS);
		mixed_coop_writer_u16(&writer, (uint16_t)index);
		mixed_coop_writer_u16(&writer, (uint16_t)count);
		for (animation = 0; animation < count; animation++)
		{
			struct animation const *entry = (struct animation const *)graph->animations.address + animation;

			mixed_coop_writer_u32(&writer, mixed_coop_name_hash(entry->name, TAG_STRING_LENGTH));
		}
		graphs++;
	}
	if (!writer.overflow)
	{
		uint8_t *at = mixed_coop.stream + graph_count_at;

		at[0] = (uint8_t)graphs; at[1] = (uint8_t)(graphs >> 8); at[2] = (uint8_t)(graphs >> 16); at[3] = (uint8_t)(graphs >> 24);
	}
	/* (version 2, a multiplayer map's: its netgame flags) */
	if (mixed_coop_version_for_level((unsigned)mixed_coop.level) == MIXED_COOP_VERSION)
	{
		struct scenario const *scenario = global_scenario_get();
		long count = scenario && scenario->netgame_flags.address ? MIN(scenario->netgame_flags.count, MIXED_COOP_MAXIMUM_NETGAME_FLAGS) : 0;
		long flag;

		mixed_coop_writer_u16(&writer, (uint16_t)count);
		mixed_coop_writer_u16(&writer, 0);
		for (flag = 0; flag < count; flag++)
		{
			struct scenario_netgame_flag const *entry = (struct scenario_netgame_flag const *)scenario->netgame_flags.address + flag;
			real const values[4] = { entry->position.x, entry->position.y, entry->position.z, entry->facing };
			real const unplaced[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			/* (1.1.3, the built-in tables, mixed_coop_tables.h: the flags' types and teams alone, no places - a
			client playing by a table keeps its own file's places) */
			real const *written = mixed_coop_table_writing ? unplaced : values;
			short value;

			for (value = 0; value < 4; value++)
			{
				uint32_t word;

				csmemcpy(&word, &written[value], sizeof(word));
				mixed_coop_writer_u32(&writer, word);
			}
			mixed_coop_writer_u16(&writer, (uint16_t)entry->type);
			mixed_coop_writer_u16(&writer, (uint16_t)entry->team_index);
		}
	}
	length = mixed_coop_writer_end(&writer);
	if (!length)
	{
		platform_log("mixed co-op: the dictionary does not fit %d bytes", MIXED_COOP_DICTIONARY_MAXIMUM_BYTES);
		mixed_coop_free(mixed_coop.stream);
		mixed_coop.stream = NULL;
		return FALSE;
	}
	mixed_coop.stream_length = length;
	platform_log("mixed co-op: the dictionary: %ld tags, %ld animation graphs, %lu bytes", mixed_coop.tag_count, graphs,
		(unsigned long)length);
	return TRUE;
}

void mixed_coop_handle_request(
	long machine_index,
	void const *entries,
	short count)
{
	/* (tests: a host without mixed play drops these unread) */
	if (getenv("HALO_TEST_MIXED_HOST_SILENT"))
		return;
	struct mixed_coop_request request;
	uint32_t offset;
	uint16_t fragments, fragment;

	if (count < 1 || !global_network_game_server_get() || !mixed_coop.loaded || !mixed_coop.network_coop ||
		!VALID_INDEX(machine_index, HALO_PORT_MAXIMUM_NETWORK_MACHINES))
	{
		return;
	}
	csmemcpy(&request, entries, sizeof(request));
	if (!mixed_coop_request_valid(&request) || request.level != (uint8_t)mixed_coop.level)
	{
		platform_log("mixed co-op: machine %ld's request refused (not this level's, or not checked)", machine_index);
		return;
	}
	if (!mixed_coop.machines[machine_index].known)
	{
		csmemset(&mixed_coop.machines[machine_index], 0, sizeof(mixed_coop.machines[machine_index]));
		mixed_coop.machines[machine_index].known = TRUE;
		mixed_coop.machines[machine_index].machine_index = machine_index;
	}
	mixed_coop.machines[machine_index].family = request.family;
	mixed_coop.machines[machine_index].mixed = request.family != (uint8_t)mixed_coop.family;
	/* (1.1.3) the collision this machine offered first (it comes before its request): held until then
	(mixed_coop_frame) */
	if (mixed_coop.machines[machine_index].collision_offered && !mixed_coop.machines[machine_index].collision_done)
	{
		if (!mixed_coop.machines[machine_index].request_held)
		{
			platform_log("mixed play: machine %ld's dictionary request held for the collision it offered", machine_index);
			mixed_coop.machines[machine_index].request_held = TRUE;
			mixed_coop.machines[machine_index].request_held_since = system_milliseconds();
		}
		mixed_coop.machines[machine_index].held_request = request;
		return;
	}
	platform_log("mixed co-op: machine %ld (%s files) asks for the dictionary: layout %08x (the host's %08x), gameplay %08x "
		"(%08x), host's alone %08x (%08x)", machine_index, mixed_coop_family_name(request.family),
		(unsigned)request.digests.layout, (unsigned)mixed_coop.digests.layout, (unsigned)request.digests.gameplay,
		(unsigned)mixed_coop.digests.gameplay, (unsigned)request.digests.host_only, (unsigned)mixed_coop.digests.host_only);
	/* (1.1.3: with the flag stand's collision an Xbox machine sent, an MCC machine is sent the digests of MCC's own
	files - what it has; an Xbox machine this host's now) */
	if (mixed_collision_held[0].grafted || mixed_collision_held[1].grafted)
	{
		mixed_coop_free(mixed_coop.stream);
		mixed_coop.stream = NULL;
		mixed_coop.stream_length = 0;
	}
	mixed_coop_stream_family = request.family;
	if (!mixed_coop_build_stream())
		return;
	/* (each structure BSP the host has loaded since the map loaded: as it
	is now, the stream rebuilt) */
	{
		uint32_t at = 4 + 2 + 1 + 1 + 4 + 16 + 4;

		long index;
		uint32_t check;

		for (index = 0; index < mixed_coop.bsp_count * 2; index++)
		{
			uint32_t digest = mixed_coop.bsp_digests[index / 2][index % 2];
			uint8_t *bytes = mixed_coop.stream + at + index * 4;

			bytes[0] = (uint8_t)digest; bytes[1] = (uint8_t)(digest >> 8); bytes[2] = (uint8_t)(digest >> 16);
			bytes[3] = (uint8_t)(digest >> 24);
		}
		check = mixed_coop_fnv(MIXED_COOP_FNV_BASIS, mixed_coop.stream, mixed_coop.stream_length - 4);
		mixed_coop.stream[mixed_coop.stream_length - 4] = (uint8_t)check;
		mixed_coop.stream[mixed_coop.stream_length - 3] = (uint8_t)(check >> 8);
		mixed_coop.stream[mixed_coop.stream_length - 2] = (uint8_t)(check >> 16);
		mixed_coop.stream[mixed_coop.stream_length - 1] = (uint8_t)(check >> 24);
	}
	fragments = (uint16_t)((mixed_coop.stream_length + MIXED_COOP_FRAGMENT_BYTES - 1) / MIXED_COOP_FRAGMENT_BYTES);
	for (fragment = 0, offset = 0; fragment < fragments; fragment++, offset += MIXED_COOP_FRAGMENT_BYTES)
	{
		static struct
		{
			struct distributed_message_header header;
			struct mixed_coop_fragment fragment;
		} message;

		csmemset(&message, 0, sizeof(message));
		message.fragment.session = request.session;
		message.fragment.total_bytes = mixed_coop.stream_length;
		message.fragment.index = fragment;
		message.fragment.count = fragments;
		message.fragment.bytes = (uint16_t)MIN(MIXED_COOP_FRAGMENT_BYTES, mixed_coop.stream_length - offset);
		csmemcpy(message.fragment.data, mixed_coop.stream + offset, message.fragment.bytes);
		distributed_send_to_machine_reliably(machine_index, &message, DISTRIBUTED_MESSAGE_MIXED_DICTIONARY, 1,
			(word)(sizeof(message.header) + offsetof(struct mixed_coop_fragment, data) + message.fragment.bytes));
	}
	mixed_coop.machines[machine_index].requested = TRUE;
	platform_log("mixed co-op: the dictionary sent to machine %ld (%u pieces)", machine_index, (unsigned)fragments);
}

/* (1.1.3, the host's) a piece of the collision stream machine_index offered: whole, each model it holds no copy of
taken (mixed_collision_take_model) and given to its scenery (mixed_collision_graft); its dictionary request then
answered (mixed_coop_frame) */
void mixed_coop_handle_collision(
	long machine_index,
	void const *entries,
	short count,
	unsigned short entries_size)
{
	/* (tests: a host without mixed play drops these unread) */
	if (getenv("HALO_TEST_MIXED_HOST_SILENT"))
		return;
	struct mixed_coop_fragment fragment;
	struct mixed_collision_stream stream;
	char why[160];
	int result, level;
	uint8_t index;

	if (count < 1 || !VALID_INDEX(machine_index, HALO_PORT_MAXIMUM_NETWORK_MACHINES) || !mixed_coop.hosting ||
		!mixed_coop.loaded || !mixed_coop.network_coop || !mixed_coop.machines[machine_index].known ||
		!mixed_coop.machines[machine_index].collision_offered || mixed_coop.machines[machine_index].collision_done ||
		!mixed_coop_hosted_server(&level) || level != mixed_coop.level || entries_size < offsetof(struct mixed_coop_fragment, data))
	{
		return;
	}
	csmemset(&fragment, 0, sizeof(fragment));
	csmemcpy(&fragment, entries, MIN((size_t)entries_size, sizeof(fragment)));
	if (!mixed_coop.machines[machine_index].collision_reassembly)
	{
		mixed_coop.machines[machine_index].collision_reassembly = malloc(sizeof(struct mixed_collision_reassembly));
		if (!mixed_coop.machines[machine_index].collision_reassembly)
		{
			mixed_coop.machines[machine_index].collision_done = TRUE;
			return;
		}
		mixed_collision_reassembly_begin(mixed_coop.machines[machine_index].collision_reassembly, mixed_coop.session);
	}
	result = mixed_collision_reassembly_take(mixed_coop.machines[machine_index].collision_reassembly, &fragment,
		MIN((uint32_t)entries_size, (uint32_t)sizeof(fragment)));
	if (result == _mixed_coop_fragment_taken)
		return;
	if (result == _mixed_coop_fragment_complete)
	{
		struct mixed_collision_reassembly *reassembly = mixed_coop.machines[machine_index].collision_reassembly;

		why[0] = 0;
		if (!mixed_collision_stream_read(reassembly->bytes, reassembly->total_bytes, &stream, why, sizeof(why)) ||
			stream.level != (uint8_t)mixed_coop.level)
		{
			platform_log("mixed play: machine %ld's collision not taken: %s", machine_index,
				why[0] ? why : "another map's");
		}
		else
		{
			for (index = 0; index < stream.model_count; index++)
			{
				int model = stream.models[index].model;

				if (!(mixed_coop.machines[machine_index].collision_offered & (1u << model)) ||
					!(mixed_collision_missing(level) & (1u << model)))
				{
					continue;
				}
				if (mixed_collision_take_model(machine_index, model, stream.models[index].data, stream.models[index].bytes))
					mixed_collision_graft(model, TRUE);
			}
		}
	}
	else
		platform_log("mixed play: machine %ld's collision piece refused: its stream not taken", machine_index);
	mixed_coop_free(mixed_coop.machines[machine_index].collision_reassembly);
	mixed_coop.machines[machine_index].collision_reassembly = NULL;
	mixed_coop.machines[machine_index].collision_done = TRUE;
}

/* ---------- after the load: a client's */

static uint32_t mixed_coop_client_session;
/* (the crossplay wire) the request, said again with the pieces still wanted, and when it last was */
static struct mixed_coop_request mixed_coop_delta_request;
static unsigned long mixed_coop_delta_wanted_time;

static void mixed_coop_client_delta_wants(
	void)
{
	unsigned long now = system_milliseconds();
	uint16_t first;
	uint8_t bits[MIXED_COOP_DELTA_DICTIONARY_WANT_BITS / 8];

	if (!network_wire_crossplay() || !mixed_coop.requested || mixed_coop.matched || mixed_coop.leaving ||
		!mixed_coop.reassembly || !mixed_coop_delta_agreed() || now - mixed_coop_delta_wanted_time < MIXED_COOP_DELTA_WANT_MILLISECONDS)
	{
		return;
	}
	mixed_coop_delta_pieces_wanted(&mixed_coop_delta_dictionary, &first, bits, MIXED_COOP_DELTA_DICTIONARY_WANT_BITS);
	mixed_coop_delta_send_request(&mixed_coop_delta_request, first, bits);
	mixed_coop_delta_wanted_time = now ? now : 1;
}

/* (1.1.3, an Xbox machine an MCC host asked) the flag stand and teleporter base collision models of the map this
machine loaded from its own files, sent to the host before its dictionary request (mixed_collision_protocol.h):
only those asked for, only the host, nothing kept */
static void mixed_coop_client_send_collision(
	void)
{
	static uint8_t model_bytes[MIXED_COLLISION_MAXIMUM_MODEL_BYTES];
	struct mixed_collision_writer writer;
	uint8_t *stream;
	uint32_t length, offset;
	uint16_t fragments, fragment;
	short model;

	if (!mixed_coop.collision_asked || mixed_coop.collision_asked_session != mixed_coop.host_session ||
		mixed_coop.host_family != _mixed_coop_family_mcc || mixed_coop.family != _mixed_coop_family_xbox ||
		!mixed_coop_level_is_multiplayer(mixed_coop.level) || network_wire_crossplay())
	{
		return;
	}
	stream = malloc(MIXED_COLLISION_MAXIMUM_STREAM_BYTES);
	if (!stream)
		return;
	mixed_collision_writer_begin(&writer, stream, MIXED_COLLISION_MAXIMUM_STREAM_BYTES, (uint8_t)mixed_coop.level);
	for (model = 0; model < NUMBER_OF_MIXED_COLLISION_MODELS; model++)
	{
		long scenery = (mixed_coop.collision_asked & (1u << model)) ? mixed_coop_find_tag('scen', mixed_collision_model_path(model)) : NONE;
		struct object_definition const *definition = scenery != NONE ? mixed_coop_instances()[scenery].base_address : NULL;
		struct cache_file_tag_instance_view *collision = definition ? mixed_coop_instance(definition->object.collision_model.index) : NULL;
		char why[160];
		uint32_t bytes;

		if (!collision || collision->group_tag != 'coll' || !collision->base_address)
			continue;
		why[0] = 0;
		bytes = mixed_collision_model_write(collision->base_address, model_bytes, sizeof(model_bytes), why, sizeof(why));
		if (!bytes)
		{
			platform_log("mixed play: %s's collision not sent: %s", mixed_collision_model_path(model), why);
			continue;
		}
		{
			uint8_t digest[MIXED_COLLISION_DIGEST_BYTES];
			char text[MIXED_COLLISION_DIGEST_BYTES * 2 + 1];

			crypto_blake2b(digest, sizeof(digest), model_bytes, bytes);
			mixed_collision_digest_text(digest, text);
			platform_log("mixed play: %s's collision from this machine's files: %lu bytes, digest %s",
				mixed_collision_model_path(model), (unsigned long)bytes, text);
		}
#ifndef HALO_RELEASE
		/* (test, debug builds: HALO_MIXED_COLLISION_TAMPER=1 sends a model a vertex off - the host must refuse it) */
		if (getenv("HALO_MIXED_COLLISION_TAMPER") && bytes > MIXED_COLLISION_ROOT_BYTES + 8)
		{
			model_bytes[bytes - 8] ^= 0x40;
			platform_log("mixed play: (test) %s's collision sent damaged", mixed_collision_model_path(model));
		}
#endif
		mixed_collision_writer_model(&writer, (uint8_t)model, model_bytes, bytes);
	}
	length = mixed_collision_writer_end(&writer);
	if (!length)
	{
		platform_log("mixed play: no collision the host asked for is in this machine's map: none sent");
		mixed_coop_free(stream);
		return;
	}
	fragments = (uint16_t)((length + MIXED_COLLISION_FRAGMENT_BYTES - 1) / MIXED_COLLISION_FRAGMENT_BYTES);
	for (fragment = 0, offset = 0; fragment < fragments; fragment++, offset += MIXED_COLLISION_FRAGMENT_BYTES)
	{
		static struct
		{
			struct distributed_message_header header;
			struct mixed_coop_fragment fragment;
		} message;

		csmemset(&message, 0, sizeof(message));
		message.fragment.session = mixed_coop.host_session;
		message.fragment.total_bytes = length;
		message.fragment.index = fragment;
		message.fragment.count = fragments;
		message.fragment.bytes = (uint16_t)MIN(MIXED_COLLISION_FRAGMENT_BYTES, length - offset);
		csmemcpy(message.fragment.data, stream + offset, message.fragment.bytes);
		distributed_send(&message, DISTRIBUTED_MESSAGE_MIXED_COLLISION, 1,
			(word)(sizeof(message.header) + offsetof(struct mixed_coop_fragment, data) + message.fragment.bytes),
			_distributed_to_host_reliably);
	}
	platform_log("mixed play: the collision the MCC host asked for sent (%lu bytes, %u pieces)", (unsigned long)length,
		(unsigned)fragments);
	mixed_coop_free(stream);
}

static void mixed_coop_dictionary_take(uint8_t const *bytes, uint32_t length);

int mixed_coop_client_ready_allowed(
	void)
{
	struct network_game *game;
	unsigned long now = system_milliseconds();

	if (game_connection() != _game_connection_network_client || !mixed_coop.network_coop || !mixed_coop.loaded)
		return TRUE;
	if (mixed_coop.matched || mixed_coop.leaving)
		return mixed_coop.matched;
	game = network_game_get_game();
	/* (an Xbox machine whose host said nothing (an older host, or an Xbox
	one): the game as before) */
	if (!mixed_coop.host_said)
	{
		if (mixed_coop.family == _mixed_coop_family_mcc)
		{
			/* (refused before the load since 8l - mixed_coop_client_start_waits -; this one only if that was missed:
			1.1.3, by a table where the level needs no layout) */
			if (mixed_coop_host_layout_level(mixed_coop.level))
				mixed_coop_refuse_older_host(game, mixed_coop.level, "with the level loaded");
			else if (mixed_coop_client_only_take(game, mixed_coop.level, "with the level loaded") > 0)
				return mixed_coop_client_ready_allowed();
			return FALSE;
		}
		return TRUE;
	}
	/* (both of the Xbox's: as before) */
	if (mixed_coop.host_family == _mixed_coop_family_xbox && mixed_coop.family == _mixed_coop_family_xbox)
		return TRUE;
	mixed_coop.needs_dictionary = TRUE;
	/* (1.1.3, by a table: the Xbox's level's dictionary this build carries, taken as a host's) */
	if (mixed_coop.client_only)
	{
		if (!mixed_coop.requested)
		{
			uint8_t *table = NULL;
			uint32_t length = 0;

			mixed_coop.requested = TRUE;
			mixed_coop.requested_time = now;
			if (!mixed_coop_table_dictionary(mixed_coop.level, &table, &length))
			{
				mixed_coop_leave(T("This build has no table of %s: Xbox and MCC files can't play it together with this "
					"host."), mixed_coop_level_stems[mixed_coop.level]);
				return FALSE;
			}
			platform_log("mixed play: %s's dictionary from this build's table (%lu bytes)", mixed_coop_level_stems[mixed_coop.level],
				(unsigned long)length);
			mixed_coop_dictionary_take(table, length);
			mixed_coop_table_free(table);
		}
		return mixed_coop.matched;
	}
	if (!mixed_coop.requested)
	{
		struct
		{
			struct distributed_message_header header;
			struct mixed_coop_request request;
		} message;

		if (!mixed_coop.reassembly)
			mixed_coop.reassembly = malloc(sizeof(*mixed_coop.reassembly));
		if (!mixed_coop.reassembly)
		{
			mixed_coop_leave(T("Not enough memory to check the host's level."));
			return FALSE;
		}
		mixed_coop_client_session = mixed_coop.host_session;
		mixed_coop_reassembly_begin(mixed_coop.reassembly, mixed_coop_client_session);
		csmemset(&message, 0, sizeof(message));
		message.request.version = (uint16_t)mixed_coop_version_for_level((unsigned)mixed_coop.level);
		message.request.family = (uint8_t)mixed_coop.family;
		message.request.level = (uint8_t)mixed_coop.level;
		message.request.session = mixed_coop_client_session;
		message.request.digests = mixed_coop.digests;
		/* (a PC host's game: over Delta Peer, said again with the pieces still wanted until the dictionary is whole,
		mixed_coop_client_delta_wants) */
		/* (1.1.3: an MCC host's flag stand and teleporter base collision, from this machine's files, first) */
		mixed_coop_client_send_collision();
		if (network_wire_crossplay())
		{
			mixed_coop_delta_request = message.request;
			mixed_coop_delta_pieces_begin(&mixed_coop_delta_dictionary, _mixed_coop_delta_stream_dictionary,
				mixed_coop_client_session, (uint8_t)mixed_coop.level, mixed_coop.reassembly->bytes,
				MIXED_COOP_DICTIONARY_MAXIMUM_BYTES);
			mixed_coop_delta_send_request(&mixed_coop_delta_request, 0, NULL);
			mixed_coop_delta_wanted_time = now ? now : 1;
		}
		else
			distributed_send(&message, DISTRIBUTED_MESSAGE_MIXED_REQUEST, 1, (word)sizeof(message), _distributed_to_host_reliably);
		mixed_coop.requested = TRUE;
		mixed_coop.requested_time = now;
		platform_log("mixed co-op: the host plays %s from the %s files, this machine from the %s files: its dictionary asked for",
			mixed_coop_level_stems[mixed_coop.level], mixed_coop_family_name(mixed_coop.host_family),
			mixed_coop_family_name(mixed_coop.family));
		return FALSE;
	}
	if (now - mixed_coop.requested_time >= MIXED_COOP_DICTIONARY_WAIT_MILLISECONDS)
	{
		mixed_coop_leave(T("The host didn't send what's needed to play %s together from Xbox and MCC files."),
			mixed_coop_level_stems[mixed_coop.level]);
	}
	return FALSE;
}

/* a local tag by path hash: a binary search of the sorted copy */
struct mixed_coop_lookup
{
	uint32_t path_hash;
	long tag_index;
};

static int mixed_coop_compare_lookups(
	void const *first,
	void const *second)
{
	uint32_t a = ((struct mixed_coop_lookup const *)first)->path_hash;
	uint32_t b = ((struct mixed_coop_lookup const *)second)->path_hash;

	return a < b ? -1 : a > b ? 1 : 0;
}

static long mixed_coop_find(
	struct mixed_coop_lookup const *lookups,
	long count,
	uint32_t path_hash)
{
	long low = 0, high = count - 1;

	while (low <= high)
	{
		long middle = (low + high) / 2;

		if (lookups[middle].path_hash == path_hash)
		{
			/* (a hash two tags have: neither) */
			if ((middle > 0 && lookups[middle - 1].path_hash == path_hash) ||
				(middle + 1 < count && lookups[middle + 1].path_hash == path_hash))
			{
				return NONE;
			}
			return lookups[middle].tag_index;
		}
		if (lookups[middle].path_hash < path_hash)
			low = middle + 1;
		else
			high = middle - 1;
	}
	return NONE;
}

static void mixed_coop_tag_text(
	long absolute_index,
	char *text,
	int size)
{
	struct cache_file_tag_instance_view *instance = &mixed_coop_instances()[absolute_index];
	long group = instance->group_tag == 'mod2' ? 'mode' : instance->group_tag;
	static char const *const names[][2] =
	{
		{ "weap", "weapon" }, { "proj", "projectile" }, { "vehi", "vehicle" }, { "bipd", "biped" },
		{ "eqip", "equipment" }, { "jpt!", "damage" }, { "phys", "physics" }, { "coll", "collision model" },
		{ "scen", "scenery" }, { "mach", "machine" }, { "ctrl", "control" }, { "garb", "garbage" },
		{ "pphy", "point physics" }, { "matg", "globals" },
	};
	char group_text[5] = { (char)(group >> 24), (char)(group >> 16), (char)(group >> 8), (char)group, 0 };
	char const *kind = group_text;
	short index;

	for (index = 0; index < NUMBEROF(names); index++)
	{
		if (!strcmp(names[index][0], group_text))
			kind = names[index][1];
	}
	snprintf(text, (size_t)size, "%s (%s)", instance->name ? instance->name : "?", kind);
}

/* whether network.mixed_coop_accept names this tag's path */
static boolean mixed_coop_accepted(
	long absolute_index)
{
	char const *setting = getenv("HALO_MIXED_COOP_ACCEPT") ? getenv("HALO_MIXED_COOP_ACCEPT") :
		config_string("network.mixed_coop_accept");
	char const *name = mixed_coop_instances()[absolute_index].name;
	size_t length;

	short decision;

	for (decision = 0; name && decision < NUMBEROF(mixed_coop_decision_tags); decision++)
	{
		if (!csstrcasecmp(name, mixed_coop_decision_tags[decision].path) && mixed_coop_decided(mixed_coop_decision_tags[decision].decision))
			return TRUE;
	}
	/* (an Xbox-files host's game, this machine MCC's: the host decides) */
	if (mixed_coop.host_family == _mixed_coop_family_xbox && mixed_coop.family == _mixed_coop_family_mcc &&
		(mixed_coop_xbox_host_decides_tag(name) || mixed_coop_stand_in_host_decides(name)))
	{
		return TRUE;
	}

	if (!setting || !setting[0] || !name)
		return FALSE;
	length = strlen(name);
	while (*setting)
	{
		char const *end = strchr(setting, ',');
		size_t part = end ? (size_t)(end - setting) : strlen(setting);

		while (part && *setting == ' ')
			setting++, part--;
		if (part == length && !strncasecmp(setting, name, length))
			return TRUE;
		if (!end)
			break;
		setting = end + 1;
	}
	return FALSE;
}

static void mixed_coop_dictionary_take(
	uint8_t const *bytes,
	uint32_t length)
{
	boolean layout_differs = FALSE;
	struct mixed_coop_dictionary dictionary;
	char why[96];
	struct mixed_coop_lookup *lookups;
	long index, gameplay_differences = 0, xbox_host_needed = 0, told_differences = 0, host_only_tags = 0, mapped = 0;
	long stand_differences = 0;
	char first_difference[200];
	uint32_t offset = 0;
	uint16_t host_graph, animation_count;
	uint8_t const *name_hashes;
	/* (the game's game type: what of an MCC host's game can come into play, mixed_coop_xbox_host_tag_in_play) */
	struct network_game const *game_settings = network_game_get_game();
	short game_type = game_settings ? (short)game_settings->variant.game_engine_index : game_engine_none;
	char in_play[160];

	first_difference[0] = 0;
	if (!mixed_coop_dictionary_read(bytes, length, &dictionary, why, sizeof(why)))
	{
		mixed_coop_leave(T("The host's description of the level couldn't be read (%s)."), why);
		return;
	}
	if (dictionary.level != (uint8_t)mixed_coop.level)
	{
		mixed_coop_leave(T("The host plays another level than this machine loaded."));
		return;
	}
	platform_log("mixed co-op: the host's dictionary: %s files, %lu tags, layout %08x (this machine's %08x), "
		"gameplay %08x (%08x), host's alone %08x (%08x), %u BSPs, %lu animation graphs",
		mixed_coop_family_name(dictionary.family), (unsigned long)dictionary.tag_count, (unsigned)dictionary.digests.layout,
		(unsigned)mixed_coop.digests.layout, (unsigned)dictionary.digests.gameplay, (unsigned)mixed_coop.digests.gameplay,
		(unsigned)dictionary.digests.host_only, (unsigned)mixed_coop.digests.host_only, (unsigned)dictionary.bsp_count,
		(unsigned long)dictionary.graph_count);
	/* (a layout that differs is said once the tags are compared: a tag that
	differs is often why - a collision model the other family has not makes
	its placements not collide, the flag stand's - and names it) */
	if (dictionary.digests.layout != mixed_coop.digests.layout &&
		dictionary.digests.layout_offset != mixed_coop.digests.layout_offset)
	{
		layout_differs = TRUE;
	}
	if (dictionary.digests.host_only != mixed_coop.digests.host_only)
		platform_log("mixed co-op: the host's scripts and AI differ from this machine's (the host runs them: told, not refused)");
	/* (this machine's tags by path hash) */
	lookups = malloc(sizeof(*lookups) * (size_t)MAX(1, mixed_coop.tag_count));
	mixed_coop.host_to_local = malloc(sizeof(long) * dictionary.tag_count);
	mixed_coop.local_to_host = malloc(sizeof(long) * (size_t)MAX(1, mixed_coop.tag_count));
	mixed_coop.host_datums = malloc(sizeof(long) * dictionary.tag_count);
	mixed_coop.graph_offsets = malloc(sizeof(long) * (size_t)MAX(1, mixed_coop.tag_count));
	mixed_coop.graph_counts = malloc(sizeof(short) * (size_t)MAX(1, mixed_coop.tag_count));
	mixed_coop.animation_pool = malloc(sizeof(short) * 65536);
	if (!lookups || !mixed_coop.host_to_local || !mixed_coop.local_to_host || !mixed_coop.host_datums || !mixed_coop.graph_offsets ||
		!mixed_coop.graph_counts || !mixed_coop.animation_pool)
	{
		mixed_coop_free(lookups);
		mixed_coop_leave(T("Not enough memory to check the host's level."));
		return;
	}
	for (index = 0; index < mixed_coop.tag_count; index++)
	{
		lookups[index].path_hash = mixed_coop.tags[index].path_hash;
		lookups[index].tag_index = mixed_coop_instances()[index].tag_index;
		mixed_coop.local_to_host[index] = NONE;
		mixed_coop.graph_offsets[index] = NONE;
		mixed_coop.graph_counts[index] = 0;
	}
	qsort(lookups, (size_t)mixed_coop.tag_count, sizeof(*lookups), mixed_coop_compare_lookups);
	mixed_coop.host_tag_count = (long)dictionary.tag_count;
	for (index = 0; index < (long)dictionary.tag_count; index++)
	{
		struct mixed_coop_dictionary_tag tag;
		long local;

		mixed_coop_dictionary_tag_get(&dictionary, (uint32_t)index, &tag);
		mixed_coop.host_datums[index] = (long)tag.datum;
		local = mixed_coop_find(lookups, mixed_coop.tag_count, tag.path_hash);
		mixed_coop.host_to_local[index] = local;
		if (local == NONE)
		{
			host_only_tags++;
			continue;
		}
		mapped++;
		mixed_coop.local_to_host[(short)local] = index;
		if (tag.digest && mixed_coop.tags[(short)local].digest && tag.digest != mixed_coop.tags[(short)local].digest)
		{
			char text[160];

			mixed_coop_tag_text((short)local, text, sizeof(text));
			if (mixed_coop.tags[(short)local].kind == _mixed_tag_gameplay && mixed_coop_accepted((short)local))
			{
				platform_log("mixed co-op: gameplay differs, accepted (the owner's decisions, or network.mixed_coop_accept): %s", text);
			}
			else if (mixed_coop.tags[(short)local].kind == _mixed_tag_gameplay &&
				dictionary.family == _mixed_coop_family_mcc && mixed_coop.family == _mixed_coop_family_xbox &&
				mixed_coop_xbox_host_decides_tag(mixed_coop_instances()[(short)local].name) &&
				!mixed_coop_xbox_host_tag_in_play(mixed_coop_instances()[(short)local].name, game_type, in_play, sizeof(in_play)))
			{
				/* (1.1.3, the owner, Nov 24: an MCC host's game with an Xbox machine in it is refused for the flag
				stand, the teleporter base or the needler only when one can come into play in this game) */
				platform_log("mixed play: differs, not in this game (%s): %s", in_play, text);
			}
			else if (mixed_coop.tags[(short)local].kind == _mixed_tag_gameplay)
			{
				if (mixed_coop_xbox_host_decides_tag(mixed_coop_instances()[(short)local].name))
				{
					xbox_host_needed++;
					if (strncasecmp(mixed_coop_instances()[(short)local].name, "item collections\\", 17))
						stand_differences++;
					if (dictionary.family == _mixed_coop_family_mcc && mixed_coop.family == _mixed_coop_family_xbox &&
						mixed_coop_xbox_host_tag_in_play(mixed_coop_instances()[(short)local].name, game_type, in_play,
							sizeof(in_play)))
					{
						platform_log("mixed play: in this game: %s", in_play);
					}
				}
				if (!gameplay_differences++)
					snprintf(first_difference, sizeof(first_difference), "%s", text);
				platform_log("mixed co-op: gameplay differs: %s (host %08x, here %08x)", text, (unsigned)tag.digest,
					(unsigned)mixed_coop.tags[(short)local].digest);
			}
			else
			{
				told_differences++;
				if (told_differences <= 40)
					platform_log("mixed co-op: differs (told, not refused): %s", text);
			}
		}
	}
	mixed_coop_free(lookups);
	/* (the animations, by name, of each graph both have) */
	while (mixed_coop_dictionary_graph_next(&dictionary, &offset, &host_graph, &animation_count, &name_hashes))
	{
		long local = mixed_coop.host_to_local[host_graph];
		struct cache_file_tag_instance_view *instance;
		struct animation_graph const *graph;
		short animation;

		if (local == NONE || !(instance = mixed_coop_instance(local)) || instance->group_tag != ANIMATION_GRAPH_TAG ||
			mixed_coop.animation_pool_used + animation_count > 65536)
		{
			continue;
		}
		graph = instance->base_address;
		mixed_coop.graph_offsets[(short)local] = mixed_coop.animation_pool_used;
		mixed_coop.graph_counts[(short)local] = (short)animation_count;
		for (animation = 0; animation < (short)animation_count; animation++)
		{
			uint32_t name_hash = mixed_coop_read_u32(name_hashes + animation * 4);
			short found = NONE, local_animation;

			if (animation < graph->animations.count &&
				mixed_coop_name_hash(((struct animation const *)graph->animations.address)[animation].name, TAG_STRING_LENGTH) ==
					name_hash)
			{
				found = animation;
			}
			for (local_animation = 0; found == NONE && local_animation < graph->animations.count; local_animation++)
			{
				if (mixed_coop_name_hash(((struct animation const *)graph->animations.address)[local_animation].name,
					TAG_STRING_LENGTH) == name_hash)
				{
					found = local_animation;
				}
			}
			mixed_coop.animation_pool[mixed_coop.animation_pool_used + animation] = found;
		}
		mixed_coop.animation_pool_used += animation_count;
	}
	for (index = 0; index < dictionary.bsp_count && index < MIXED_COOP_MAXIMUM_BSPS; index++)
	{
		mixed_coop.host_bsp_digests[index][0] = dictionary.bsp_digests[index][0];
		mixed_coop.host_bsp_digests[index][1] = dictionary.bsp_digests[index][1];
		if (dictionary.bsp_digests[index][0] && mixed_coop.bsp_digests[index][0] &&
			dictionary.bsp_digests[index][0] != mixed_coop.bsp_digests[index][0] &&
			dictionary.bsp_digests[index][1] != mixed_coop.bsp_digests[index][1] &&
			!mixed_coop_bsp_accepted(index, dictionary.bsp_digests[index][0], dictionary.bsp_digests[index][1],
				mixed_coop.bsp_digests[index][0], mixed_coop.bsp_digests[index][1]))
		{
			mixed_coop_leave(T("The host's %s and yours differ in the level's collision (BSP %ld): Xbox and MCC files "
				"can't play this level together."), mixed_coop_level_stems[mixed_coop.level], index);
			return;
		}
	}
	platform_log("mixed co-op: %ld of the host's tags are this machine's, %ld the host's alone; %ld gameplay tags differ, "
		"%ld others (told); %ld animations mapped", mapped, host_only_tags, gameplay_differences, told_differences,
		mixed_coop.animation_pool_used);
	if (gameplay_differences && xbox_host_needed == gameplay_differences && !stand_differences)
	{
		/* (1.1.3: the flag stand and teleporter base the host plays with the Xbox's collision now - its needler alone
		is Halo PC's, in a game that can spawn it) */
		mixed_coop_leave(T("The host plays %s from MCC's files, whose Needler is Halo PC's: with Xbox and MCC files "
			"together this map's games with the Needler need an Xbox-files host. Ask a player with the Xbox's maps to "
			"host."), mixed_coop_level_stems[mixed_coop.level]);
		return;
	}
	if (gameplay_differences && xbox_host_needed == gameplay_differences)
	{
		mixed_coop_leave(T("The host plays %s from MCC's files: with Xbox and MCC files together this map needs an "
			"Xbox-files host (MCC's files have no collision for the flag stand and teleporter base). Ask a player with "
			"the Xbox's maps to host."), mixed_coop_level_stems[mixed_coop.level]);
		return;
	}
	if (gameplay_differences)
	{
		if (gameplay_differences > 1)
			mixed_coop_leave(T("The host's %s and yours play differently (%s and %ld more): Xbox and MCC files can't play "
				"this level together."), mixed_coop_level_stems[mixed_coop.level], first_difference, gameplay_differences - 1);
		else
			mixed_coop_leave(T("The host's %s and yours play differently (%s): Xbox and MCC files can't play this level "
				"together."), mixed_coop_level_stems[mixed_coop.level], first_difference);
		return;
	}
	/* (1.1.3, by a table on a map Halo PC rebuilt: the Xbox's placements on MCC's places - the host's collision and
	objects decide) */
	if (layout_differs && mixed_coop.client_only && mixed_coop_rebuilt_level(mixed_coop.level))
	{
		platform_log("mixed play: %s's placements differ from the Xbox's on this machine's files (Halo PC rebuilt it): "
			"played with the host's collision deciding", mixed_coop_level_stems[mixed_coop.level]);
		layout_differs = FALSE;
	}
	if (layout_differs)
	{
		mixed_coop_leave(T("The host's %s and yours place the level's objects differently: Xbox and MCC files can't "
			"play this level together."), mixed_coop_level_stems[mixed_coop.level]);
		return;
	}
	/* (a multiplayer map: the host's netgame flags, the same ones - their
	types and teams - in places Halo PC moved a little: this machine takes
	the host's places) */
	if (mixed_coop_level_is_multiplayer(mixed_coop.level))
	{
		struct scenario *scenario = global_scenario_get();
		long flag, moved = 0;

		/* (1.1.3, by a table on a map Halo PC rebuilt: this file's own flags, the host's unknown - teleporters a
		little off are the host's to correct) */
		if (mixed_coop.client_only && mixed_coop_rebuilt_level(mixed_coop.level))
		{
			platform_log("mixed play: %s's netgame flags this file's own (%ld; the Xbox's %lu)",
				mixed_coop_level_stems[mixed_coop.level], scenario ? scenario->netgame_flags.count : 0L,
				(unsigned long)dictionary.netgame_flag_count);
		}
		else if (!scenario || dictionary.netgame_flag_count != (uint32_t)scenario->netgame_flags.count)
		{
			mixed_coop_leave(T("The host's %s and yours have different flags (teleporters, CTF flags, hills): Xbox and MCC "
				"files can't play this map together."), mixed_coop_level_stems[mixed_coop.level]);
			return;
		}
		for (flag = 0; !(mixed_coop.client_only && mixed_coop_rebuilt_level(mixed_coop.level)) &&
			flag < scenario->netgame_flags.count; flag++)
		{
			struct scenario_netgame_flag const *entry = (struct scenario_netgame_flag const *)scenario->netgame_flags.address + flag;
			struct mixed_coop_netgame_flag host;

			if (!mixed_coop_dictionary_netgame_flag_get(&dictionary, (uint32_t)flag, &host) ||
				host.type != (uint16_t)entry->type || host.team != (uint16_t)entry->team_index)
			{
				mixed_coop_leave(T("The host's %s and yours have different flags (teleporters, CTF flags, hills): Xbox and MCC "
					"files can't play this map together."), mixed_coop_level_stems[mixed_coop.level]);
				return;
			}
		}
		/* (by a table: the flags' types and teams checked above, their places this file's own - the table has none) */
		for (flag = 0; !mixed_coop.client_only && flag < scenario->netgame_flags.count; flag++)
		{
			struct scenario_netgame_flag *entry = (struct scenario_netgame_flag *)scenario->netgame_flags.address + flag;
			struct mixed_coop_netgame_flag host;

			mixed_coop_dictionary_netgame_flag_get(&dictionary, (uint32_t)flag, &host);
			if (entry->position.x != host.position[0] || entry->position.y != host.position[1] ||
				entry->position.z != host.position[2] || entry->facing != host.facing)
			{
				platform_log("mixed play: netgame flag %ld (type %d) at the host's %.3f %.3f %.3f facing %.4f (this file's %.3f "
					"%.3f %.3f facing %.4f)", flag, (int)entry->type, host.position[0], host.position[1], host.position[2],
					host.facing, entry->position.x, entry->position.y, entry->position.z, entry->facing);
				entry->position.x = host.position[0];
				entry->position.y = host.position[1];
				entry->position.z = host.position[2];
				entry->facing = host.facing;
				moved++;
			}
		}
		platform_log("mixed play: %ld of %ld netgame flags placed as the host's", moved, scenario->netgame_flags.count);
	}
	mixed_coop.matched = TRUE;
	mixed_coop_invisible_mask();
	mixed_coop_collision_probe();
	platform_log("mixed co-op: the host's level matches this machine's: playing it together");
}

void mixed_coop_handle_dictionary(
	void const *entries,
	short count,
	unsigned short entries_size)
{
	static struct mixed_coop_fragment fragment;
	int result;
	unsigned short head = (unsigned short)offsetof(struct mixed_coop_fragment, data);

	if (count < 1 || !mixed_coop.reassembly || mixed_coop.matched || mixed_coop.leaving || entries_size < head)
		return;
	/* (its header, then no more bytes than it says and the message has) */
	csmemset(&fragment, 0, sizeof(fragment));
	csmemcpy(&fragment, entries, head);
	if (fragment.bytes > MIXED_COOP_FRAGMENT_BYTES || fragment.bytes > entries_size - head)
	{
		network_event("mixed co-op: a piece of the host's dictionary longer than its message");
		return;
	}
	csmemcpy(fragment.data, (byte const *)entries + head, fragment.bytes);
	result = mixed_coop_reassembly_take(mixed_coop.reassembly, &fragment);
	if (result == _mixed_coop_fragment_refused)
	{
		network_event("mixed co-op: a piece of the host's dictionary refused");
		return;
	}
	if (result == _mixed_coop_fragment_complete)
		mixed_coop_dictionary_take(mixed_coop.reassembly->bytes, mixed_coop.reassembly->total_bytes);
}

/* (the crossplay wire) a piece of the host's dictionary or layout over Delta Peer */
void mixed_coop_delta_heard_piece(
	uint8_t const *payload,
	int size)
{
	int result;

	if (!network_wire_crossplay() || size < 1)
		return;
	if (payload[0] == _mixed_coop_delta_stream_dictionary)
	{
		if (!mixed_coop.requested || mixed_coop.matched || mixed_coop.leaving || !mixed_coop.reassembly ||
			mixed_coop_delta_dictionary.bytes != mixed_coop.reassembly->bytes ||
			mixed_coop_delta_dictionary.received == mixed_coop_delta_dictionary.count && mixed_coop_delta_dictionary.count)
		{
			return;
		}
		result = mixed_coop_delta_take_piece(&mixed_coop_delta_dictionary, payload, size);
		if (result == _mixed_coop_fragment_refused)
			network_event("mixed co-op: a piece of the host's dictionary refused");
		else if (result == _mixed_coop_fragment_complete)
			mixed_coop_dictionary_take(mixed_coop.reassembly->bytes, mixed_coop_delta_dictionary.total_bytes);
		return;
	}
	if (payload[0] == _mixed_coop_delta_stream_layout)
	{
		struct mixed_coop_layout layout;
		char why[96];

		if (!mixed_coop.start_pending || !mixed_coop.layout_reassembly || mixed_coop.layout_complete ||
			mixed_coop_delta_layout.bytes != mixed_coop.layout_reassembly->bytes)
		{
			return;
		}
		result = mixed_coop_delta_take_piece(&mixed_coop_delta_layout, payload, size);
		if (result == _mixed_coop_fragment_refused)
		{
			network_event("mixed play: a host's layout piece refused");
			return;
		}
		if (result != _mixed_coop_fragment_complete)
			return;
		/* (as the Bungie message's reassembly has it whole: mixed_coop_client_start_waits checks it) */
		mixed_coop.layout_reassembly->total_bytes = mixed_coop_delta_layout.total_bytes;
		if (!mixed_coop_layout_read(mixed_coop.layout_reassembly->bytes, mixed_coop.layout_reassembly->total_bytes, &layout, why,
			sizeof(why)))
		{
			mixed_coop.start_pending = NULL;
			mixed_coop_leave(T("The host's placements of %s couldn't be read (%s)."), mixed_coop_level_stems[mixed_coop.host_level],
				why);
			return;
		}
		mixed_coop.layout_complete = TRUE;
	}
}

/* (the crossplay wire) a structure BSP's collision digest over Delta Peer */
void mixed_coop_delta_heard_bsp(
	struct mixed_coop_bsp_digest const *bsp)
{
	if (network_wire_crossplay())
		mixed_coop_handle_bsp_digest(bsp, 1);
}

void mixed_coop_handle_bsp_digest(
	void const *entries,
	short count)
{
	struct mixed_coop_bsp_digest entry;

	if (count < 1 || !mixed_coop.needs_dictionary)
		return;
	csmemcpy(&entry, entries, sizeof(entry));
	if (entry.session != mixed_coop_client_session || entry.bsp_index >= MIXED_COOP_MAXIMUM_BSPS || entry.pad || !entry.digest)
		return;
	mixed_coop.host_bsp_digests[entry.bsp_index][0] = entry.digest;
	mixed_coop.host_bsp_digests[entry.bsp_index][1] = entry.digest_offset;
	if (mixed_coop.bsp_digests[entry.bsp_index][0] && mixed_coop.bsp_digests[entry.bsp_index][0] != entry.digest &&
		mixed_coop.bsp_digests[entry.bsp_index][1] != entry.digest_offset &&
		!mixed_coop_bsp_accepted(entry.bsp_index, entry.digest, entry.digest_offset, mixed_coop.bsp_digests[entry.bsp_index][0],
			mixed_coop.bsp_digests[entry.bsp_index][1]))
	{
		mixed_coop_leave(T("The host's %s and yours differ in the level's collision (BSP %d): Xbox and MCC files "
			"can't play this level together."), mixed_coop_level_stems[mixed_coop.level], (int)entry.bsp_index);
	}
}

/* ---------- translation */

int mixed_coop_translating(
	void)
{
	return mixed_coop.matched && mixed_coop.host_to_local != NULL;
}

int mixed_coop_holding(
	void)
{
	return game_connection() == _game_connection_network_client && mixed_coop.needs_dictionary && !mixed_coop.matched;
}

long mixed_coop_tag_from_host(
	long host_tag_index)
{
	short absolute_index = (short)host_tag_index;

	if (!mixed_coop_translating() || host_tag_index == NONE)
		return host_tag_index;
	if (absolute_index < 0 || absolute_index >= mixed_coop.host_tag_count || mixed_coop.host_to_local[absolute_index] == NONE)
	{
		mixed_coop.unmapped_tags++;
		return NONE;
	}
	return mixed_coop.host_to_local[absolute_index];
}

long mixed_coop_tag_to_host(
	long local_tag_index)
{
	short absolute_index = (short)local_tag_index;
	long host_index;

	if (!mixed_coop_translating() || local_tag_index == NONE)
		return local_tag_index;
	if (absolute_index < 0 || absolute_index >= mixed_coop.tag_count || mixed_coop.local_to_host[absolute_index] == NONE)
	{
		mixed_coop.unmapped_tags++;
		return NONE;
	}
	host_index = mixed_coop.local_to_host[absolute_index];
	/* (the host's datum, salt and all: a host checks a tag index whole,
	tag_index_is_group) */
	return host_index >= 0 && host_index < mixed_coop.host_tag_count ? mixed_coop.host_datums[host_index] : NONE;
}

short mixed_coop_animation_from_host(
	long local_graph_tag_index,
	short host_animation_index)
{
	short absolute_index = (short)local_graph_tag_index;

	if (!mixed_coop_translating() || host_animation_index == NONE)
		return host_animation_index;
	if (local_graph_tag_index == NONE || absolute_index < 0 || absolute_index >= mixed_coop.tag_count ||
		mixed_coop.graph_offsets[absolute_index] == NONE || host_animation_index < 0 ||
		host_animation_index >= mixed_coop.graph_counts[absolute_index])
	{
		mixed_coop.unmapped_animations++;
		return NONE;
	}
	return mixed_coop.animation_pool[mixed_coop.graph_offsets[absolute_index] + host_animation_index];
}

/* the modules' own (their entries' layouts are theirs) */
boolean network_objects_mixed_translate(unsigned char type, void *entries, short count);
boolean network_damage_mixed_translate(unsigned char type, void *entries, short count, boolean outgoing);
boolean network_actors_mixed_translate(void *entries, short count);
boolean network_coop_mixed_translate(unsigned char type, void *entries, short count);
boolean network_distributed_mixed_translate(unsigned char type, void *entries, short count);

int mixed_coop_translate_incoming(
	unsigned char type,
	void *entries,
	short count,
	unsigned short entries_size)
{
	(void)entries_size;
	if (game_connection() != _game_connection_network_client)
		return TRUE;
	if (mixed_coop_holding())
	{
		/* (until the dictionary is in: the host's messages that name tags
		dropped, as lost ones) */
		switch (type)
		{
		case _distributed_message_object_changes:
		case _distributed_message_pickups:
		case _distributed_message_damage_events:
		case _distributed_message_damage_animations:
		case _distributed_message_actor_states:
		case _distributed_message_coop_events:
		case _distributed_message_coop_device_groups:
		case _distributed_message_coop_object_transforms:
		case _distributed_message_coop_object_looks:
		case _distributed_message_coop_device_states:
			return FALSE;
		default:
			return TRUE;
		}
	}
	if (!mixed_coop_translating())
		return TRUE;
	switch (type)
	{
	case _distributed_message_object_changes:
	case _distributed_message_damage_animations:
		return network_objects_mixed_translate(type, entries, count);
	case _distributed_message_damage_events:
		return network_damage_mixed_translate(type, entries, count, FALSE);
	case _distributed_message_actor_states:
		return network_actors_mixed_translate(entries, count);
	case _distributed_message_coop_events:
	case _distributed_message_coop_device_groups:
	case _distributed_message_coop_object_transforms:
	case _distributed_message_coop_object_looks:
	case _distributed_message_coop_device_states:
		return network_coop_mixed_translate(type, entries, count);
	case _distributed_message_pickups:
		return network_distributed_mixed_translate(type, entries, count);
	default:
		return TRUE;
	}
}

void mixed_coop_translate_outgoing(
	unsigned char type,
	void *entries,
	short count)
{
	if (game_connection() != _game_connection_network_client || !mixed_coop_translating())
		return;
	if (type == _distributed_message_hit_reports)
		network_damage_mixed_translate(type, entries, count, TRUE);
}

/* ---------- tests: the state both machines agree on */

void mixed_coop_placed_hash(
	void)
{
	struct object_iterator iterator;
	struct object_datum *object;
	uint32_t placed = MIXED_COOP_FNV_BASIS;
	long objects = 0;

	if (mixed_coop.loaded && mixed_coop.family == _mixed_coop_family_xbox && !mixed_coop.network_coop)
		mixed_coop_table_dump_layout("xbox", global_scenario_get());
	if (!getenv("HALO_MIXED_COOP_HASH") || !mixed_coop.loaded || !mixed_coop.network_coop)
		return;
	/* (every object the map placed, by its index and the path of its
	definition, before a script or the AI has run: the placed objects at
	the same indices on every machine) */
	object_iterator_new(&iterator, _object_mask_all, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		uint32_t path = mixed_coop_tag_path_hash(object->definition_index);
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.index);

		placed = mixed_coop_fnv(placed, &absolute_index, sizeof(absolute_index));
		placed = mixed_coop_fnv(placed, &path, sizeof(path));
		objects++;
		if (getenv("HALO_MIXED_COOP_HASH_DUMP"))
		{
			struct cache_file_tag_instance_view *instance = mixed_coop_instance(object->definition_index);

			error(_error_silent, "mixed placedobject %04lx %d %s", (unsigned long)absolute_index, (int)object->object.type,
				instance && instance->name ? instance->name : "?");
		}
	}
	error(_error_silent, "mixed placed %ld %08x", objects, (unsigned)placed);
}

/* (test: HALO_MIXED_STAND_PROBE=1 with HALO_MIXED_COOP_HASH=1, every 5 s of the game) what a flag stand and a teleporter
base are to this machine's simulation: a ray down onto each one's middle from over it - the stand's own top when it
collides, the ground under it when it does not (MCC's files' stand on an MCC host that holds no collision) - and where
each CTF flag is: lines both machines of a mixed game must agree on */
static void mixed_coop_stand_probe(
	void)
{
	struct object_iterator iterator;
	struct object_datum *object;

	if (!getenv("HALO_MIXED_STAND_PROBE") || game_time_get() % 150 != 0)
		return;
	object_iterator_new(&iterator, _object_mask_scenery | _object_mask_weapon, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.index);
		struct cache_file_tag_instance_view *instance = mixed_coop_instance(object->definition_index);
		char const *name = instance && instance->name ? instance->name : "";

		if (object->object.type == _object_type_scenery &&
			(!csstrcasecmp(name, mixed_collision_model_path(0)) || !csstrcasecmp(name, mixed_collision_model_path(1))))
		{
			struct collision_result result;
			real_point3d point = object->object.bounding_sphere_center;
			real_vector3d down = { 0.0f, 0.0f, 0.0f };
			boolean hit;

			/* (from just over its bounding sphere: under an overhang too) */
			point.z += object->object.bounding_sphere_radius + 0.1f;
			down.k = -2.0f * object->object.bounding_sphere_radius - 0.2f;
			hit = collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
				FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit), &point, &down, NONE, &result);
			platform_log("mixed stand probe: %ld %s %04lx at %.3f %.3f %.3f: %s z %.3f", (long)game_time_get(),
				strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name, (unsigned long)absolute_index, object->object.position.x,
				object->object.position.y, object->object.position.z,
				!hit ? "nothing" : result.object_index == iterator.index ? "the stand" : "the ground", hit ? result.point.z : 0.0f);
		}
		else if (object->object.type == _object_type_weapon && strstr(name, "weapons\\flag\\"))
		{
			platform_log("mixed stand probe: %ld flag %04lx at %.3f %.3f %.3f", (long)game_time_get(), (unsigned long)absolute_index,
				object->object.position.x, object->object.position.y, object->object.position.z);
		}
	}
}

void mixed_coop_tick_hash(
	void)
{
	static int enabled = -1;
	static long placed_logged_for = NONE;
	struct object_iterator iterator;
	struct object_datum *object;
	uint32_t synced = MIXED_COOP_FNV_BASIS, named = 0, placed = MIXED_COOP_FNV_BASIS;
	long objects = 0, synced_objects = 0;
	unsigned long synced_mask = _object_mask_biped | _object_mask_vehicle | _object_mask_weapon | _object_mask_equipment;

	if (enabled < 0)
		enabled = getenv("HALO_MIXED_COOP_HASH") && getenv("HALO_MIXED_COOP_HASH")[0] == '1';
	if (!enabled || !game_in_progress() || !mixed_coop.loaded || !mixed_coop.network_coop)
		return;
	if (mixed_coop_holding())
		return;
	mixed_coop_stand_probe();
	/* (what the netcode makes the same on every machine (network_objects.c,
	network_coop.c): the units, vehicles, weapons and equipment at the host's
	object index, each by its index and the path of its definition - the
	host's on a translating client is the same path, its own tag -; scenery,
	devices and the rest by their definition's path and their name, wherever
	they are: the host's scripts create named scenery a client creates by
	its name in a slot of its own) */
	object_iterator_new(&iterator, _object_mask_all & ~(_object_mask_garbage | _object_mask_projectile), 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		uint32_t path = mixed_coop_tag_path_hash(object->definition_index);
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.index);
		short type = object->object.type;

		if (TEST_FLAG(synced_mask, type))
		{
			synced = mixed_coop_fnv(synced, &absolute_index, sizeof(absolute_index));
			synced = mixed_coop_fnv(synced, &path, sizeof(path));
			synced = mixed_coop_fnv(synced, &type, sizeof(type));
			synced_objects++;
		}
		else
		{
			uint32_t one = mixed_coop_fnv(MIXED_COOP_FNV_BASIS, &path, sizeof(path));

			one = mixed_coop_fnv(one, &object->object.name_index, sizeof(object->object.name_index));
			named += one;
		}
		placed = mixed_coop_fnv(placed, &absolute_index, sizeof(absolute_index));
		placed = mixed_coop_fnv(placed, &path, sizeof(path));
		objects++;
		if (getenv("HALO_MIXED_COOP_HASH_DUMP") && atol(getenv("HALO_MIXED_COOP_HASH_DUMP")) == (long)game_time_get())
		{
			struct cache_file_tag_instance_view *instance = mixed_coop_instance(object->definition_index);

			error(_error_silent, "mixed object %ld %04lx %d %d %s", (long)game_time_get(), (unsigned long)absolute_index,
				(int)type, (int)object->object.name_index, instance && instance->name ? instance->name : "?");
		}
	}
	(void)placed_logged_for;
	(void)placed;
	error(_error_silent, "mixed hash %ld %ld %08x %ld %08x", (long)game_time_get(), synced_objects,
		(unsigned)mixed_coop_fnv(synced, &named, sizeof(named)), objects, (unsigned)named);
}
