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
#include "objects/damage_resistances.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"

#include "mcc_content.h"

/* (collision_bsp.c) the collision BSP whose invisible surfaces collide with nothing, or NULL */
extern struct collision_bsp const *collision_bsp_invisible_masked;
#include "mixed_coop.h"
#include "network_distributed.h"
#include "network_wire.h"
#include "tag_schema.h"
#include "../src/lang.h"

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

/* (network_messages.c's packet table: the classes of the lobby's) */
#define MIXED_COOP_CLASS_CLIENT_PREGAME 3
#define MIXED_COOP_CLASS_SERVER_PREGAME 2

/* how long a client waits for the host's dictionary before it leaves */
#define MIXED_COOP_DICTIONARY_WAIT_MILLISECONDS 20000
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
};

/* the multiplayer maps Halo PC rebuilt (the research: walls, ceilings,
invisible walls, spawns, vehicles - MCC has Halo PC's): never one mixed
game, refused with this said (mixed_coop_multiplayer_refusal) */
static struct
{
	char const *stem;
	char const *title;
	char const *why;
} const mixed_coop_rebuilt_maps[] =
{
	{ "bloodgulch", "Blood Gulch", "rebuilt walls, a lower ceiling, invisible walls and other vehicles" },
	{ "beavercreek", "Battle Creek", "rebuilt collision" },
	{ "carousel", "Derelict", "rebuilt collision" },
	{ "hangemhigh", "Hang 'Em High", "rebuilt collision" },
	{ "sidewinder", "Sidewinder", "rebuilt collision" },
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
	} machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	/* the host's dictionary stream (built once a map) */
	uint8_t *stream;
	uint32_t stream_length;

	/* --- the lobby: a client's */
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
} mixed_coop;

/* the game's free asserts on NULL (debug_memory.c) */
static void mixed_coop_free(
	void *pointer)
{
	if (pointer)
		free(pointer);
}

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
		snprintf(text, sizeof(text), T("Halo PC rebuilt %s (%s), and MCC's files have Halo PC's. MCC's %s and the "
			"Xbox's can't be played together; everyone needs the same files."), mixed_coop_rebuilt_maps[index].title,
			mixed_coop_rebuilt_maps[index].why, mixed_coop_rebuilt_maps[index].title);
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

static boolean mixed_coop_host_rename(
	char *map_name,
	long size,
	boolean said)
{
	int level = mixed_coop_level_index_mcc_name(map_name);
	char const *stem;

	if (!mixed_coop_level_is_multiplayer(level) || !mixed_coop_capable() || network_wire_crossplay())
		return FALSE;
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
	Halo PC's mp_needler. TRUE: the Xbox's needler on MCC machines */
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
	{ "needler", FALSE },
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
	collision_bsp_invisible_masked = NULL;
	csmemset(mixed_coop.bsp_digests, 0, sizeof(mixed_coop.bsp_digests));
	csmemset(mixed_coop.host_bsp_digests, 0, sizeof(mixed_coop.host_bsp_digests));
	mixed_coop.loaded = FALSE;
	mixed_coop.network_coop = FALSE;
	mixed_coop.extras_removed = mixed_coop.scripts_disabled = mixed_coop.rules_applied = 0;
	mixed_coop.matched = FALSE;
	mixed_coop.requested = FALSE;
	mixed_coop.needs_dictionary = FALSE;
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
		game_connection() == _game_connection_network_server) && !network_wire_crossplay() && game &&
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
	}
	mixed_coop.scenario_index = scenario_index;
	mixed_coop.tag_count = halo_loaded_tag_count;
	mixed_coop.bsp_count = 0;
	mixed_coop_walk_custom_edition = custom_edition != 0;
	mixed_coop_trace_filter = getenv("HALO_MIXED_COOP_TRACE");
	scenario_group = mixed_coop_schema_group('scnr', FALSE);
	if (!scenario_group || !scenario_group->definition)
		return;
	/* (an MCC level played in network co-op: the Xbox's) */
	if (mixed_coop.family == _mixed_coop_family_mcc && (mixed_coop.network_coop || force))
	{
		mixed_coop_make_xbox_scenario((struct scenario *)scenario_instance->base_address, scenario_group->definition);
		mixed_coop_rules();
	}
	if (mixed_coop.network_coop || force)
		mixed_coop_decisions_apply();
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
	mixed_coop_scenario_digests(scenario_instance->base_address, scenario_group->definition);
	/* (gameplay: the gameplay tags' digests by path, in path order) */
	sorted = malloc(sizeof(*sorted) * (size_t)MAX(1, mixed_coop.tag_count));
	if (sorted)
	{
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
	}
	mixed_coop.digests.gameplay = gameplay;
	mixed_coop.digests.tag_count = (uint32_t)mixed_coop.tag_count;
	mixed_coop.loaded = TRUE;
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
static char const *const mixed_coop_invisible_mask_maps[] = { "damnation" };

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
	/* (the test's probe on the host: its BSP as it is; a client's once it matched, mixed_coop_dictionary_take) */
	if (global_network_game_server_get())
		mixed_coop_collision_probe();
	/* (the host: to every client that asked for its dictionary) */
	if (global_network_game_server_get() && mixed_coop.network_coop)
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
	if (!server || network_wire_crossplay() || network_game_is_splitscreen_local() || !mixed_coop_capable())
		return NULL;
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

void mixed_coop_frame(
	void)
{
	int level;
	struct network_game_server *server = mixed_coop_hosted_server(&level);
	long machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	unsigned long now;
	short count, index;
	int family;

	if (!server)
	{
		if (mixed_coop.hosting && !global_network_game_server_get())
		{
			mixed_coop.hosting = FALSE;
			csmemset(mixed_coop.machines, 0, sizeof(mixed_coop.machines));
		}
		return;
	}
	now = system_milliseconds();
	if (!mixed_coop.hosting)
	{
		mixed_coop.hosting = TRUE;
		mixed_coop.session = (uint32_t)now ^ ((uint32_t)rand() << 16) ^ 0x4D584350u;
		csmemset(mixed_coop.machines, 0, sizeof(mixed_coop.machines));
		platform_log(mixed_coop_level_is_multiplayer(level) ? "mixed play: hosting %s from the %s files: each machine joining is told" :
			"mixed co-op: hosting co-op on %s from the %s files: each machine joining is told",
			mixed_coop_level_stems[level], mixed_coop_family_name(mixed_coop_local_family(level)));
	}
	family = mixed_coop_local_family(level);
	if (family == _mixed_coop_family_none || network_game_client_get_local_machine_index() == NONE)
		return;
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
			csmemset(&mixed_coop.machines[index], 0, sizeof(mixed_coop.machines[index]));
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
	struct mixed_coop_lobby_message answer;
	long machine_index = NONE;

	if (!mixed_coop_lobby_decode(message, message_size, _message_client_mixed_coop, MIXED_COOP_CLASS_CLIENT_PREGAME, &answer) ||
		answer.command != _mixed_coop_lobby_answer)
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
	if (!mixed_coop.machines[machine_index].answered)
		platform_log("mixed co-op: machine %ld plays this level from the %s files", machine_index,
			mixed_coop_family_name(answer.family));
	mixed_coop.machines[machine_index].answered = TRUE;
	mixed_coop.machines[machine_index].family = answer.family;
}

/* ---------- the lobby: a client's */

void mixed_coop_client_handle_lobby(
	struct network_game_client *client,
	unsigned short *message,
	short message_size)
{
	struct mixed_coop_lobby_message hello, answer;
	void *encoded;

	if (!mixed_coop_lobby_decode(message, message_size, _message_server_mixed_coop, MIXED_COOP_CLASS_SERVER_PREGAME, &hello) ||
		hello.command != _mixed_coop_lobby_hello)
	{
		network_event("mixed co-op: a host's message that could not be decoded or checked");
		return;
	}
	if (!mixed_coop_capable() || global_network_game_server_get())
		return;
	if (!mixed_coop.host_said || mixed_coop.host_session != hello.session)
		platform_log("mixed co-op: the host plays %s from the %s files; this machine from the %s files",
			mixed_coop_level_stems[hello.level], mixed_coop_family_name(hello.family),
			mixed_coop_family_name(mixed_coop_local_family(hello.level)));
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
	encoded = create_network_game_message(_message_client_mixed_coop, &answer, sizeof(answer));
	if (!encoded || !network_game_client_send_to_server(client, encoded))
		network_event("mixed co-op: the answer could not be sent");
}

int mixed_coop_host_level_acceptable(
	char const *level_name,
	unsigned long host_identity)
{
	int level = mixed_coop_level_index(level_name);
	int family = mixed_coop_local_family(level);

	if (level < 0 || !mixed_coop_capable())
		return FALSE;
	/* (an MCC host: this machine plays it from either family's files) */
	if (host_identity == MIXED_COOP_MCC_IDENTITY)
	{
		if (family == _mixed_coop_family_none)
			return FALSE;
		return TRUE;
	}
	/* (an Xbox host, or an older one: with MCC's files alone, only once the
	host has said it plays mixed co-op - looked at again as the game loads,
	mixed_coop_client_ready_allowed - its lobby's hello may come after its
	settings) */
	return !host_identity && family == _mixed_coop_family_mcc;
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

int mixed_coop_client_update(
	struct network_game_client *client)
{
	(void)client;
	if (!mixed_coop.leaving)
		return TRUE;
	platform_show_message(mixed_coop_level_is_multiplayer(mixed_coop.level) ? T("Halo: map") : T("Halo: co-op"), mixed_coop.leave_text);
	mixed_coop.leaving = FALSE;
	return FALSE;
}

void mixed_coop_client_dispose(
	void)
{
	mixed_coop.host_said = FALSE;
	mixed_coop.host_family = _mixed_coop_family_none;
	mixed_coop.needs_dictionary = FALSE;
	mixed_coop.matched = FALSE;
	mixed_coop.requested = FALSE;
	mixed_coop_free(mixed_coop.reassembly);
	mixed_coop.reassembly = NULL;
}

/* ---------- after the load: the host's dictionary */

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
		mixed_coop_writer_u32(&writer, mixed_coop.tags[index].path_hash);
		mixed_coop_writer_u32(&writer, mixed_coop.tags[index].digest);
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
			short value;

			for (value = 0; value < 4; value++)
			{
				uint32_t word;

				csmemcpy(&word, &values[value], sizeof(word));
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
	platform_log("mixed co-op: machine %ld (%s files) asks for the dictionary: layout %08x (the host's %08x), gameplay %08x "
		"(%08x), host's alone %08x (%08x)", machine_index, mixed_coop_family_name(request.family),
		(unsigned)request.digests.layout, (unsigned)mixed_coop.digests.layout, (unsigned)request.digests.gameplay,
		(unsigned)mixed_coop.digests.gameplay, (unsigned)request.digests.host_only, (unsigned)mixed_coop.digests.host_only);
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

/* ---------- after the load: a client's */

static uint32_t mixed_coop_client_session;

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
	(void)game;
	/* (an Xbox machine whose host said nothing (an older host, or an Xbox
	one): the game as before) */
	if (!mixed_coop.host_said)
	{
		if (mixed_coop.family == _mixed_coop_family_mcc)
		{
			mixed_coop_leave(T("The host plays %s from the Xbox's files and its version can't play co-op with MCC's "
				"files: update the host's game, or play with your Xbox files."), mixed_coop_level_stems[mixed_coop.level]);
			return FALSE;
		}
		return TRUE;
	}
	/* (both of the Xbox's: as before) */
	if (mixed_coop.host_family == _mixed_coop_family_xbox && mixed_coop.family == _mixed_coop_family_xbox)
		return TRUE;
	mixed_coop.needs_dictionary = TRUE;
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
		mixed_coop_xbox_host_decides_tag(name))
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
	char first_difference[200];
	uint32_t offset = 0;
	uint16_t host_graph, animation_count;
	uint8_t const *name_hashes;

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
			else if (mixed_coop.tags[(short)local].kind == _mixed_tag_gameplay)
			{
				if (mixed_coop_xbox_host_decides_tag(mixed_coop_instances()[(short)local].name))
					xbox_host_needed++;
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

		if (!scenario || dictionary.netgame_flag_count != (uint32_t)scenario->netgame_flags.count)
		{
			mixed_coop_leave(T("The host's %s and yours have different flags (teleporters, CTF flags, hills): Xbox and MCC "
				"files can't play this map together."), mixed_coop_level_stems[mixed_coop.level]);
			return;
		}
		for (flag = 0; flag < scenario->netgame_flags.count; flag++)
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
		for (flag = 0; flag < scenario->netgame_flags.count; flag++)
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
