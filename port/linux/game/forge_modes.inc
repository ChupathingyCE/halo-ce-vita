/*
FORGE_MODES.INC

The game modes made in Forge (included by forge.c after
forge_mode_markers.inc, for its statics; the design:
docs/superpowers/specs/2026-11-05-forge-warzone-firefight-design.md). This
step: FIREFIGHT, local - co-op against the AI's waves on a Forge map
variant's AI spawn zones (a stock map's built-in layout when the variant has
none: forge_modes_builtins), rounds of waves and sets of rounds, the team's
lives, a score for each kill, the difficulty climbing; its HUD; the game
over or won and the carnage report.

Firefight round 3 (the owner, Nov 5: "Firefight didn't really have bosses,
it had bonus rounds"), as ODST's and Reach's: no named bosses, no boss's
health bar, no "A boss has come" - a layout's boss markers are Warzone's,
its code kept for that game type (forge_modes_queue_boss, only when Warzone
runs). Each round's last wave is the HEAVY WAVE, unannounced: Reach's
heavier squads - every enemy of it a rank up (a minor its major, as
Thunderstorm does), the zones of HEAVY WAVE ONLY with it (a built-in
layout's Hunters and Elite commanders). After each set a BONUS ROUND
(forge_modes_bonus_*): a minute (the bonus round marker's time) of a Grunt
rush, each kill its big points (the marker's), a death no life, its timer on
the HUD, its total at the end (and the carnage report's BONUS).

Built-in layouts (forge_modes_builtins: Blood Gulch, Battle Creek, Hang
'Em High): our own places - the players' start and respawn points, the AI spawn
zones of each round and wave, their drop points, the bonus round's place -
chosen from each map's own starting locations ("forge mode places" logs
them), put in the Forge layout's table as the map loads (no Forge needed
first; nothing of the map's copied); a map variant with Firefight zones of
its own plays its own. A map with neither: zones at the map's own starting
locations away from the player (forge_modes_zones_from_map).

Asked for as Command's game is (forge_modes_game_request: the console's
"forge mode firefight", the Forge lobby's START FIREFIGHT, HALO_TEST_FIREFIGHT_GAME
for the tests): the next Forge game a Firefight game. The map built out in
memory as it loads (the zones' squads' recipes: Command's command_marines and
command_covenant, mode_flood, mode_sentinels; nothing written anywhere); once
the player has spawned the monitor is gone (no flipping to it while the game
runs) and the first round begins.

The AI: Command mode's squads (command_squads.h, hosted while the game runs),
each a squad of Halo CE's own characters of the zone's kind, made on the
ground within the zone, on its species' own AI team (hostile to the players)
or the players' (a zone of TEAM RED: allies). A wave waits for room within
the budget (forge_modes_enemy_cap: 16 enemies alive, 32 when no ally lives);
each enemy squad hunts the nearest living player (ATTACK, chosen again every
few seconds). A wave is done when its enemies are dead (or a last one or two
hold out FORGE_MODES_STRAGGLER_SECONDS); the round when its waves are; the
set when its rounds are.

Lives: the team's pool (the settings' lives, 0 unlimited; a round cleared
gives some back): a death takes one; with none left the dead wait (the
engine's respawn held), and when no player lives the game is over.

The score: each enemy killed is its kind's points (or a boss's own) to the
player who hurt it last (the unit's own attackers' record); a round cleared
adds its bonus; each skull on adds its share (x1.1 to x1.5).

Threads: forge_modes_update runs in forge_update (the main thread, before
the tick); forge_modes_draw (the HUD's) reads what it wrote.
*/

/* (menu_functions.c) the profile's name, as the nameplate shows it */
void menus_player_name(char *name, int size);

/* (1.1.x: Halo CE alone) the modes' squads are Command's (command_squads.h);
1.2.0's forge_modes_h2.inc puts a Halo 2 mission's own between these names
and Command's */
#define forge_modes_squad_spawn_team command_squad_spawn_team
#define forge_modes_squad_send_by_air command_squad_send_by_air
#define forge_modes_squad_living_count command_squad_living_count
#define forge_modes_squad_release command_squad_release
#define forge_modes_squad_airborne command_squad_airborne
#define forge_modes_squad_middle command_squad_middle
#define forge_modes_squad_units command_squad_units
#define forge_modes_squad_order_hosted command_squad_order_hosted

/* ---------- constants */

enum
{
	_forge_mode_none = 0,
	_forge_mode_firefight,
	_forge_mode_warzone,
	NUMBER_OF_FORGE_MODES
};

enum
{
	_forge_modes_state_none = 0,
	/* (a round's start: its banner, then its first wave) */
	_forge_modes_state_round_start,
	_forge_modes_state_wave,
	/* (between waves) */
	_forge_modes_state_wave_break,
	/* (a round cleared: its bonus, lives back, the map's weapons back) */
	_forge_modes_state_round_break,
	/* (after a set: the bonus round's banner, its minute, its total) */
	_forge_modes_state_bonus_start,
	_forge_modes_state_bonus,
	_forge_modes_state_bonus_end,
	_forge_modes_state_over,
	NUMBER_OF_FORGE_MODES_STATES
};

#define FORGE_MODES_MAXIMUM_SQUADS 24
#define FORGE_MODES_MAXIMUM_UNITS 96
#define FORGE_MODES_MAXIMUM_PENDING 24
/* the enemies' share of the AI (the design's section 5): 16, or 32 with no
ally alive */
#define FORGE_MODES_TEAM_AI 16
#define FORGE_MODES_ALL_AI 32
#define FORGE_MODES_THINK_TICKS 15
#define FORGE_MODES_TARGET_TICKS 90
#define FORGE_MODES_ROUND_START_SECONDS 6
#define FORGE_MODES_WAVE_BREAK_SECONDS 4
#define FORGE_MODES_ROUND_BREAK_SECONDS 10
#define FORGE_MODES_STRAGGLER_SECONDS 25
#define FORGE_MODES_OVER_SECONDS 8
#define FORGE_MODES_ROUND_BONUS 500
#define FORGE_MODES_OBJECTIVE_BONUS 250
/* the bonus round: its banner's seconds before it, its total's after; the
enemies it keeps coming (the room allowing); its defaults with no marker
(forge_mode_markers.inc's) */
#define FORGE_MODES_BONUS_START_SECONDS 5
#define FORGE_MODES_BONUS_END_SECONDS 7
#define FORGE_MODES_BONUS_ALIVE 12
/* the dead hold this long (ticks) at least while no life is left */
#define FORGE_MODES_RESPAWN_HOLD 45
/* a ground squad's places out of every living player's sight: this many
points of its zone tried; a player sees a point within his view's cone
(this cosine of its half angle: wider than the widest field of view, the
turn of a head taken in) with a clear line to it, or nearer than this; with
every point in sight it waits, and after this long it comes by dropship */
#define FORGE_MODES_HIDDEN_TRIES 16
#define FORGE_MODES_SIGHT_COSINE 0.42f
#define FORGE_MODES_SIGHT_NEAR 8.f
#define FORGE_MODES_HIDDEN_WAIT_SECONDS 8
/* allies follow the nearest player when farther than this */
#define FORGE_MODES_ALLY_FOLLOW 9.f
/* (a queued squad's yaw: none given, its drop point's or chosen) */
#define FORGE_MODES_NO_YAW 1000.f

/* each squad of the table (forge_files.h _forge_mode_squad_*' order): its
composition for command_squad_spawn_team ("name:count,...", the first
leading; each part a member's actor variant's name), its recipe (the build
out in memory), whether it is the campaign's humans */
static struct
{
	char const *composition;
	char const *recipe;
} const forge_modes_squads[NUMBER_OF_FORGE_MODE_SQUADS] =
{
	{ "marine assault rifle:4", "command_marines" },
	{ "marine_armored assault rifle:3,marine_armored sniper rifle:1", "command_marines" },
	{ "grunt major needler:1,grunt minor plasma pistol:3", "mode_covenant" },
	{ "jackal minor plasma pistol:2,grunt minor plasma pistol:2", "mode_covenant" },
	{ "elite minor plasma rifle:1,grunt minor plasma pistol:3", "mode_covenant" },
	{ "elite major needler:1,elite minor plasma rifle:1", "mode_covenant" },
	{ "stealth elite plasma rifle:2", "mode_covenant" },
	{ "hunter:2", "mode_covenant" },
	{ "floodcombat_human assault rifle:2,floodcombat elite plasma rifle:2", "mode_flood" },
	{ "floodcarrier:1,flood_infection:1", "mode_flood" },
	{ "sentinel major:1,sentinel:3", "mode_sentinels" },
	{ "grunt minor plasma pistol:1", "mode_covenant" },
	{ "elite commander energy sword:1,elite major needler:2", "mode_covenant" },
};

/* each boss kind (_forge_mode_boss_*): its one unit's actor variant */
static struct
{
	char const *variant;
	char const *recipe;
	/* its escort's squad (NONE: none) */
	short escort;
} const forge_modes_bosses[NUMBER_OF_FORGE_MODE_BOSSES] =
{
	{ "elite commander energy sword", "mode_covenant", _forge_mode_squad_elite_grunts },
	{ "elite major needler", "mode_covenant", _forge_mode_squad_grunts },
	{ "stealth elite plasma rifle", "mode_covenant", NONE },
	{ "hunter", "mode_covenant", NONE },
	{ "floodcarrier", "mode_flood", _forge_mode_squad_flood_combat },
	{ "sentinel major", "mode_sentinels", _forge_mode_squad_sentinels },
};

/* the points of a kill, by its actor variant's name (the first part found;
the design's section 3) */
static struct
{
	char const *name;
	short points;
} const forge_modes_points[] =
{
	{ "commander", 80 }, { "stealth", 60 }, { "elite major", 50 }, { "elite minor", 30 }, { "hunter", 100 },
	{ "jackal", 20 }, { "grunt major", 15 }, { "grunt", 10 }, { "floodcarrier", 10 }, { "flood_infection", 2 },
	{ "floodcombat", 20 }, { "sentinel major", 50 }, { "sentinel", 30 },
};

/* the built-in layouts (Firefight round 3): a stock map's own Firefight, no
Forge needed first. Our places on the map (chosen from its own starting
locations, "forge mode places"): the players' start (initial spawns, the
respawn points the same), the AI spawn zones (a squad, how many, its
rounds' and waves' masks - 0 all; FORGE_FILE_MODE_HEAVY_WAVE alone: the
heavy wave only -, its radius, by dropship from which way: a hostile drop
point of ours there, its yaw), the bonus round's place */
#define FORGE_MODES_BUILTIN_STARTS 4
#define FORGE_MODES_BUILTIN_ZONES 8
#define FORGE_MODES_GROUND 1000.f
struct forge_modes_builtin_zone
{
	real x, y, z;
	short squad;
	unsigned char count;
	unsigned char rounds;
	unsigned char waves;
	unsigned char radius;
	/* (by dropship: from this yaw; FORGE_MODES_GROUND on the ground) */
	real yaw;
};
static struct
{
	char const *map;
	/* (x, y, z, facing) */
	real starts[FORGE_MODES_BUILTIN_STARTS][4];
	struct forge_modes_builtin_zone zones[FORGE_MODES_BUILTIN_ZONES];
	/* the bonus round's place (x, y, z, radius) */
	real bonus[4];
} const forge_modes_builtins[] =
{
	/* Blood Gulch (Firefight 1 and 2's own zones): the players at the blue
	base's front, the Covenant from the canyon's middle and both its ends */
	{ "bloodgulch",
		{ { 46.09f, -88.70f, 0.04f, -1.05f }, { 48.85f, -87.47f, 0.08f, -1.05f }, { 42.51f, -90.53f, 0.17f, -1.05f },
			{ 50.48f, -86.17f, 0.11f, -1.05f } },
		{
			{ 63.f, -117.f, 1.f, _forge_mode_squad_grunts, 4, 0, 0, 5, FORGE_MODES_GROUND },
			{ 74.f, -103.f, 1.f, _forge_mode_squad_grunts_jackals, 4, 0, 0x1E, 5, 2.18f },
			{ 52.f, -131.f, 1.f, _forge_mode_squad_elite_grunts, 4, 0x06, 0x1C, 5, -0.96f },
			{ 69.f, -127.f, 1.f, _forge_mode_squad_elites, 2, 0x04, 0x18, 5, -0.96f },
			{ 58.f, -100.f, 1.f, _forge_mode_squad_hunters, 2, 0, FORGE_FILE_MODE_HEAVY_WAVE, 5, 2.18f },
			{ 64.f, -112.f, 1.f, _forge_mode_squad_elite_commanders, 3, 0x06, FORGE_FILE_MODE_HEAVY_WAVE, 5, -0.96f },
		},
		{ 63.f, -117.f, 1.f, 10.f } },
	/* Battle Creek: the players by the creek before the blue base, the
	Covenant out of the red base, then from behind them */
	{ "beavercreek",
		{ { 11.38f, 18.55f, -0.22f, 0.f }, { 11.28f, 8.58f, -0.22f, 0.f }, { 7.21f, 7.15f, -0.22f, 0.3f },
			{ 11.3f, 13.6f, -0.22f, 0.f } },
		{
			{ 25.54f, 8.19f, -0.22f, _forge_mode_squad_grunts, 4, 0, 0, 3, FORGE_MODES_GROUND },
			{ 27.04f, 18.03f, -0.22f, _forge_mode_squad_grunts_jackals, 4, 0, 0x1E, 3, FORGE_MODES_GROUND },
			{ 30.03f, 12.17f, -0.22f, _forge_mode_squad_elite_grunts, 4, 0x06, 0x1C, 3, FORGE_MODES_GROUND },
			{ -2.11f, 10.44f, -0.15f, _forge_mode_squad_elites, 2, 0x04, 0x18, 3, FORGE_MODES_GROUND },
			{ 22.14f, 20.37f, -0.22f, _forge_mode_squad_hunters, 2, 0, FORGE_FILE_MODE_HEAVY_WAVE, 3, FORGE_MODES_GROUND },
			{ 0.2f, 16.98f, -0.15f, _forge_mode_squad_elite_commanders, 3, 0x06, FORGE_FILE_MODE_HEAVY_WAVE, 3,
				FORGE_MODES_GROUND },
		},
		{ 27.8f, 13.5f, -0.22f, 4.f } },
	/* Hang 'Em High: the players on the red side's floor, the Covenant
	among the blue side's tombstones and the far corners */
	{ "hangemhigh",
		{ { 10.84f, 2.19f, -7.95f, -0.88f }, { 12.82f, 7.16f, -7.95f, -0.88f }, { 10.69f, -5.99f, -7.95f, -0.6f },
			{ 6.79f, -7.93f, -7.95f, -0.6f } },
		{
			{ 25.20f, -16.84f, -7.95f, _forge_mode_squad_grunts, 4, 0, 0, 4, FORGE_MODES_GROUND },
			{ 28.17f, -10.00f, -7.95f, _forge_mode_squad_grunts_jackals, 4, 0, 0x1E, 4, FORGE_MODES_GROUND },
			{ 6.80f, -22.14f, -7.95f, _forge_mode_squad_elite_grunts, 4, 0x06, 0x1C, 4, FORGE_MODES_GROUND },
			{ 35.16f, 4.97f, -7.95f, _forge_mode_squad_elites, 2, 0x04, 0x18, 4, FORGE_MODES_GROUND },
			{ 25.21f, -22.25f, -7.95f, _forge_mode_squad_hunters, 2, 0, FORGE_FILE_MODE_HEAVY_WAVE, 4, FORGE_MODES_GROUND },
			{ 31.10f, -7.02f, -7.95f, _forge_mode_squad_elite_commanders, 3, 0x06, FORGE_FILE_MODE_HEAVY_WAVE, 4,
				FORGE_MODES_GROUND },
		},
		{ 26.f, -14.f, -7.95f, 8.f } },
};

/* ---------- structures */

struct forge_modes_squad
{
	long handle;
	/* (Warzone's boss's; the bonus round's rush) */
	boolean boss;
	boolean bonus;
	boolean ally;
	long target_unit_index;
	long next_target_time;
	/* (brought by dropship: still in the air when last looked at) */
	boolean flying;
};

struct forge_modes_unit
{
	long unit_index;
	short points;
	boolean boss;
	/* (the bonus round's: its points the bonus round's, its kill its total's) */
	boolean bonus;
	boolean ally;
	boolean counted;
};

/* a wave's squad waiting for room (made as the cap allows) */
struct forge_modes_pending
{
	short squad;
	short count;
	real_point3d at;
	real radius;
	boolean ally;
	/* (the heavy wave's: its members a rank up; the bonus round's) */
	boolean heavy;
	boolean bonus;
	/* (Warzone's boss's: its kind, its marker's points and toughness) */
	short boss_kind;
	short boss_points;
	short boss_toughness;
	short boss_name;
	/* by dropship (to the nearest drop point, or over its zone from yaw);
	the ground's: since when every point of its zone is in sight (NONE not
	waiting) */
	boolean dropship;
	real yaw;
	long wait_since;
};

/* ---------- globals */

static struct
{
	short requested;
	short pending;
	short running;
	boolean over;
	boolean won;
	struct forge_file_firefight_settings firefight;
	unsigned short skulls;
	unsigned short skulls_on;
	short set, round, wave;
	short state;
	long state_time;
	long next_think_time;
	long score;
	long kills;
	long deaths;
	/* (lives: remaining; NONE unlimited) */
	short lives;
	short difficulty;
	short difficulty_before;
	/* this wave's: its enemies made, still to be made, since when one or
	two hold out; whether a squad of it could not be made at all (no such
	characters loaded: the mode's packs not built, or no memory for them) */
	short wave_made;
	boolean nothing_made;
	long straggler_since;
	struct forge_modes_squad squads[FORGE_MODES_MAXIMUM_SQUADS];
	struct forge_modes_unit units[FORGE_MODES_MAXIMUM_UNITS];
	short unit_count;
	struct forge_modes_pending pending_squads[FORGE_MODES_MAXIMUM_PENDING];
	short pending_count;
	/* the local player's unit last frame (a death seen once) */
	long player_unit_index;
	long player_kills;
	char banner[96];
	char banner_line[96];
	unsigned long banner_until;
	/* (the layout's zones: the variant's, a built-in one's - its index in
	forge_modes_builtins, else NONE -, or the map's own starts') */
	short builtin;
	/* the bonus round's settings (its marker's, or ours), when it ends, its
	kills and points this time, every bonus round's points */
	boolean bonus_on;
	short bonus_seconds;
	short bonus_points;
	short bonus_squad;
	short bonus_count;
	long bonus_until;
	long bonus_kills;
	long bonus_score;
	long bonus_total;
	short bonus_next_zone;
	char boss_name[48];
	long boss_unit_index;
	/* (the carnage report) the waves cleared, the bosses killed, each
	player's (absolute index) score, kills and deaths; whether the last
	game is the map's still */
	long waves_cleared;
	long bosses_killed;
	long player_scores[FORGE_SPAWN_PLAYERS];
	long player_kill_counts[FORGE_SPAWN_PLAYERS];
	long player_deaths[FORGE_SPAWN_PLAYERS];
	boolean reported;
	/* this round's start (its time limit's), its objective (a Firefight
	objective marker's index in the spawn table, NONE none), how far it is
	(ticks: held, or breached), done, and when it was last looked at */
	long round_start_time;
	short objective;
	long objective_ticks;
	boolean objective_done;
	long objective_time;
	/* (tests, "mode hunt": the player kept facing the nearest enemy until
	then, his own shots - HALO_TEST_COMMANDS' "@aim pad" trigger - the kills) */
	long hunt_until;
	/* (tests, "mode watch": the player kept facing the nearest enemy in a
	ship - its dropship - until then) */
	long watch_until;
	long shot_ships[8];
} forge_modes = { _forge_mode_none };

/* ---------- private code */

static void forge_modes_end(boolean won);
static struct unit_datum *forge_modes_living_unit(long unit_index);

static boolean forge_modes_running(
	void)
{
	return forge_modes.running != _forge_mode_none;
}

/* (forge_variant_build) whether the next Forge game is asked to be one */
static boolean forge_modes_asked(
	void)
{
#ifndef HALO_RELEASE
	if (getenv("HALO_TEST_FIREFIGHT_GAME"))
		return TRUE;
#endif
	return forge_modes.requested != _forge_mode_none;
}

/* a UTF-8 text as the game's 16-bit text (the Basic Multilingual Plane) */
static void forge_modes_wide(
	char const *text,
	wchar_t *wide,
	int size)
{
	unsigned char const *at = (unsigned char const *)text;
	int length = 0;

	while (*at && length < size - 1)
	{
		unsigned long code = *at++;

		if (code >= 0xE0 && at[0] && at[1])
		{
			code = ((code & 0x0F) << 12) | ((unsigned long)(at[0] & 0x3F) << 6) | (at[1] & 0x3F);
			at += 2;
		}
		else if (code >= 0xC0 && at[0])
			code = ((code & 0x1F) << 6) | (*at++ & 0x3F);
		wide[length++] = (wchar_t)code;
	}
	wide[length] = 0;
}

/* a banner also said in the game's own messages (seen when the HUD is not
drawn: a dead player's) */
static void forge_modes_tell(
	char const *title,
	char const *line)
{
	char text[200];
	wchar_t wide[200];
	long player_index = local_player_get_player_index(forge_globals.local_player_index);

	/* (alive, the HUD's banner says it) */
	if (player_index == NONE || player_get(player_index)->local_player_index == NONE ||
		forge_modes_living_unit(player_get(player_index)->unit_index))
	{
		return;
	}
	snprintf(text, sizeof(text), "%s%s%s", title ? title : "", line && line[0] ? ": " : "", line ? line : "");
	forge_modes_wide(text, wide, (int)NUMBEROF(wide));
	hud_print_message(player_get(player_index)->local_player_index, wide);
}

static void forge_modes_banner(
	char const *title,
	char const *line,
	real seconds)
{
	forge_modes_tell(title, line);
	snprintf(forge_modes.banner, sizeof(forge_modes.banner), "%s", title ? title : "");
	snprintf(forge_modes.banner_line, sizeof(forge_modes.banner_line), "%s", line ? line : "");
	forge_modes.banner_until = system_milliseconds() + (unsigned long)(seconds * 1000.f);
	platform_log("forge modes: %s%s%s", forge_modes.banner, line && line[0] ? " - " : "", forge_modes.banner_line);
}

static long forge_modes_local_player(
	void)
{
	return local_player_get_player_index(forge_globals.local_player_index);
}

/* a unit's living (not dead, not gone) */
static struct unit_datum *forge_modes_living_unit(
	long unit_index)
{
	struct object_datum *object = object_try_and_get(unit_index);

	if (!object || !(FLAG(object->object.type) & _object_mask_unit) || TEST_FLAG(object->object.damage_flags, _object_dead_bit))
		return NULL;
	return (struct unit_datum *)object;
}

static short forge_modes_points_of(
	long actor_variant_index)
{
	char const *name = actor_variant_index != NONE ? tag_get_name(actor_variant_index) : NULL;
	short index;

	for (index = 0; name && index < (short)NUMBEROF(forge_modes_points); index++)
	{
		if (strstr(name, forge_modes_points[index].name))
			return forge_modes_points[index].points;
	}
	return 10;
}

/* the living enemies and allies the modes made */
static short forge_modes_alive(
	boolean allies)
{
	short index;
	short alive = 0;

	for (index = 0; index < forge_modes.unit_count; index++)
	{
		if (forge_modes.units[index].ally == allies && forge_modes_living_unit(forge_modes.units[index].unit_index))
			alive++;
	}
	return alive;
}

/* how many more enemies may be made now (the design's section 5) */
static short forge_modes_enemy_room(
	void)
{
	short allies = forge_modes_alive(TRUE);
	short enemies = forge_modes_alive(FALSE);
	short cap = allies > 0 ? FORGE_MODES_TEAM_AI : FORGE_MODES_ALL_AI;

	cap = (short)MIN(cap, FORGE_MODES_ALL_AI - allies);
	return (short)MAX(0, cap - enemies);
}

/* the engine's team of the players (a free for all's: the local player's) */
static short forge_modes_player_team(
	void)
{
	long player_index = forge_modes_local_player();

	return player_index != NONE ? (short)player_get(player_index)->team_index : (short)_game_team_player;
}

/* the first actor variant with name in its tag's name that names its unit
(the map's, the packs', the build-out's) */
static long forge_modes_variant_find(
	char const *name)
{
	struct tag_iterator iterator;
	long tag_index;

	tag_iterator_new(&iterator, ACTOR_VARIANT_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		char const *tag_name = tag_get_name(tag_index);

		if (tag_name && strstr(tag_name, name) && actor_variant_definition_get(tag_index)->unit_reference.index != NONE)
			return tag_index;
	}
	return NONE;
}

/* a squad's members' engine team: the players' for allies, else its
species' own (the first member's unit's default team: the Covenant's, the
Flood's, the Sentinels'; the humans' marines hostile too) */
static short forge_modes_squad_team(
	char const *composition,
	boolean ally)
{
	char name[64];
	char const *colon;
	long variant;

	if (ally)
		return forge_modes_player_team();
	colon = strchr(composition, ':');
	snprintf(name, sizeof(name), "%.*s", colon ? (int)(colon - composition) : (int)strlen(composition), composition);
	variant = forge_modes_variant_find(name);
	if (variant != NONE)
	{
		long unit_definition = actor_variant_definition_get(variant)->unit_reference.index;
		short team = unit_definition != NONE ? unit_definition_get(unit_definition)->unit.default_team : (short)NONE;

		if (team > _game_team_player && team < NUMBER_OF_SOLO_CAMPAIGN_TEAMS)
			return team;
	}
	return _game_team_covenant;
}

/* a point within radius of at, on the ground there */
static void forge_modes_place_in(
	real_point3d const *at,
	real radius,
	real_point3d *place)
{
	real angle = real_random_range(0.f, (real)(2.0 * 3.14159265358979));
	real distance = real_random_range(0.f, MAX(0.f, radius - 1.f));

	*place = *at;
	place->x += cosine(angle) * distance;
	place->y += sine(angle) * distance;
	place->z += 1.5f;
	forge_spawn_grounded(place);
}

/* whether a living player sees a point of the ground (a unit standing
there: its middle or its head) - within his view's cone with a clear line
to it, or near him */
static boolean forge_modes_point_seen(
	real_point3d const *point)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct unit_datum *unit = forge_modes_living_unit(player->unit_index);
		real_point3d eye;
		short height;

		if (!unit)
			continue;
		object_get_origin(player->unit_index, &eye);
		eye.z += 0.6f;
		for (height = 0; height < 2; height++)
		{
			real_point3d target = *point;
			real_vector3d line;
			real distance;
			struct collision_result collision;

			target.z += height ? 1.6f : 0.7f;
			vector_from_points3d(&eye, &target, &line);
			distance = magnitude3d(&line);
			if (distance < FORGE_MODES_SIGHT_NEAR)
				return TRUE;
			if (dot_product3d(&line, &unit->unit.looking_vector) < FORGE_MODES_SIGHT_COSINE * distance)
				break;
			if (!collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
					FLAG(_collision_test_objects_bit) | _collision_test_objects_sight_blocking_flags, &eye, &line, player->unit_index,
					&collision))
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* a point within radius of at, on the ground there, that no living player
sees (forge_modes_point_seen); FALSE if every one tried is in sight */
static boolean forge_modes_place_hidden(
	real_point3d const *at,
	real radius,
	real_point3d *place)
{
	short attempt;

	for (attempt = 0; attempt < FORGE_MODES_HIDDEN_TRIES; attempt++)
	{
		forge_modes_place_in(at, radius, place);
		if (!forge_modes_point_seen(place))
			return TRUE;
	}
	return FALSE;
}

/* where a squad of pending lands by dropship: the nearest drop point of
its side (a zone's of the players: RED or ANY; the hostile's: HOSTILE or
ANY), its yaw and ship; with none, over its zone, its ship coming from the
yaw given or from away from the nearest player */
static void forge_modes_drop_point(
	struct forge_modes_pending const *pending,
	real_point3d *at,
	real *yaw,
	short *ship)
{
	short index;
	real best = 1e30f;

	*at = pending->at;
	*yaw = pending->yaw;
	*ship = COMMAND_SQUADS_SHIP_SPECIES;
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];
		real distance;

		if (spawn->kind != _forge_mode_marker_drop_point || spawn->team == FORGE_MODE_TEAM_BLUE ||
			(spawn->team == FORGE_MODE_TEAM_RED && !pending->ally) || (spawn->team == FORGE_MODE_TEAM_HOSTILE && pending->ally))
		{
			continue;
		}
		distance = distance3d(&spawn->home.position, &pending->at);
		if (distance < best)
		{
			best = distance;
			*at = spawn->home.position;
			*yaw = arctangent(spawn->home.forward.j, spawn->home.forward.i);
			*ship = (short)(spawn->variant % NUMBER_OF_FORGE_MODE_SHIPS);
		}
	}
	if (*yaw == FORGE_MODES_NO_YAW)
	{
		struct data_iterator iterator;
		struct player_datum *player;
		real nearest = 1e30f;

		*yaw = 0.f;
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			real_point3d origin;

			if (!forge_modes_living_unit(player->unit_index))
				continue;
			object_get_origin(player->unit_index, &origin);
			if (distance3d(&origin, at) < nearest)
			{
				nearest = distance3d(&origin, at);
				*yaw = arctangent(at->y - origin.y, at->x - origin.x);
			}
		}
	}
}

/* (Thunderstorm) a member's kind ranked up: its species' major with the same
weapon, else its species' major, else as it is */
static void forge_modes_rank_up(
	char *name,
	size_t size)
{
	char const *minor = strstr(name, " minor");
	char major[64];

	if (!minor)
		return;
	snprintf(major, sizeof(major), "%.*s major%s", (int)(minor - name), name, minor + 6);
	if (forge_modes_variant_find(major) == NONE)
		snprintf(major, sizeof(major), "%.*s major", (int)(minor - name), name);
	if (forge_modes_variant_find(major) != NONE)
		snprintf(name, size, "%s", major);
}

/* a squad's members not yet counted (its first, or those a ship's seats had
no room for, made as it lands): the modes' units, a Mythic skull's on each
enemy */
static void forge_modes_register(
	short slot,
	real toughness)
{
	struct forge_modes_squad const *squad = &forge_modes.squads[slot];
	long units[COMMAND_SQUADS_MEMBERS];
	short index, other;

	forge_modes_squad_units(squad->handle, units);
	for (index = 0; index < COMMAND_SQUADS_MEMBERS; index++)
	{
		struct forge_modes_unit *unit;
		struct unit_datum *datum = forge_modes_living_unit(units[index]);

		if (!datum || forge_modes.unit_count >= FORGE_MODES_MAXIMUM_UNITS)
			continue;
		for (other = 0; other < forge_modes.unit_count && forge_modes.units[other].unit_index != units[index]; other++)
			;
		if (other < forge_modes.unit_count)
			continue;
		unit = &forge_modes.units[forge_modes.unit_count++];
		csmemset(unit, 0, sizeof(*unit));
		unit->unit_index = units[index];
		unit->ally = squad->ally;
		unit->points = (short)forge_modes_points_of(datum->unit.actor_index != NONE ?
			actor_get(datum->unit.actor_index)->meta.variant_definition_index : NONE);
		unit->boss = squad->boss;
		/* (the bonus round's: its big points a kill) */
		unit->bonus = squad->bonus;
		if (squad->bonus)
			unit->points = forge_modes.bonus_points;
		/* (a boss tougher by its marker's toughness; a Mythic skull's on every
		enemy) */
		{
			real times = toughness * (!squad->ally && (forge_modes.skulls_on & FORGE_MODE_SKULL_MYTHIC) ? 2.f : 1.f);

			if (times > 1.f)
			{
				real body = datum->object.maximum_body_vitality * times;
				real shield = datum->object.maximum_shield_vitality * times;

				object_initialize_vitality(units[index], &body, &shield);
			}
		}
		if (!squad->ally)
			forge_modes.wave_made++;
	}
}

/* a squad made of pending (as many of it as fit now): FALSE when no room,
or (a ground squad) while every point of its zone is in sight */
static boolean forge_modes_spawn(
	struct forge_modes_pending *pending)
{
	char composition[160];
	real_point3d place;
	long handle;
	short slot;
	short index;
	boolean flown = FALSE;
	short room = pending->ally ? (short)MAX(0, FORGE_MODES_TEAM_AI - forge_modes_alive(TRUE)) : forge_modes_enemy_room();
	short count = (short)MIN(pending->count, COMMAND_SQUADS_MEMBERS);
	boolean boss = pending->boss_kind != NONE;

	if (room <= 0)
		return FALSE;
	for (slot = 0; slot < FORGE_MODES_MAXIMUM_SQUADS && forge_modes.squads[slot].handle != NONE; slot++)
		;
	if (slot >= FORGE_MODES_MAXIMUM_SQUADS || forge_modes.unit_count >= FORGE_MODES_MAXIMUM_UNITS - COMMAND_SQUADS_MEMBERS)
		return FALSE;
	if (boss)
		snprintf(composition, sizeof(composition), "%s:1", forge_modes_bosses[pending->boss_kind].variant);
	else
	{
		/* (the squad's kinds, as many members as there is room: its first
		ones, the leader first) */
		char const *text = forge_modes_squads[pending->squad].composition;
		short members = 0;
		size_t length = 0;

		count = (short)MIN(count, room);
		composition[0] = 0;
		while (*text && members < count)
		{
			char name[64];
			int part_count = 1;
			char const *end = strchr(text, ',');
			char const *colon = strchr(text, ':');
			int take;

			if (!end)
				end = text + strlen(text);
			if (colon && colon < end)
			{
				snprintf(name, sizeof(name), "%.*s", (int)(colon - text), text);
				part_count = atoi(colon + 1);
			}
			else
				snprintf(name, sizeof(name), "%.*s", (int)(end - text), text);
			/* (Thunderstorm's, and the heavy wave's: a rank up) */
			if (!pending->ally && ((forge_modes.skulls_on & FORGE_MODE_SKULL_THUNDERSTORM) || pending->heavy))
				forge_modes_rank_up(name, sizeof(name));
			take = MIN(part_count, count - members);
			if (take > 0)
			{
				snprintf(composition + length, sizeof(composition) - length, "%s%s:%d", length ? "," : "", name, take);
				length = strlen(composition);
				members = (short)(members + take);
			}
			text = *end ? end + 1 : end;
		}
		/* (a squad of fewer kinds than members asked: its last kind again) */
		if (members < count && length)
		{
			char *last = strrchr(composition, ':');

			if (last)
				snprintf(last + 1, sizeof(composition) - (size_t)(last + 1 - composition), "%d", atoi(last + 1) + (count - members));
		}
		count = (short)MAX(count, members);
	}
	{
		short team = forge_modes_squad_team(composition, pending->ally);
		boolean waited = pending->wait_since != NONE &&
			game_time_get() - pending->wait_since >= FORGE_MODES_HIDDEN_WAIT_SECONDS * TICKS_PER_SECOND;

		handle = NONE;
		/* (by dropship: the zone's, or a ground squad that has waited too long
		for a place out of sight) */
		if (pending->dropship || waited)
		{
			real yaw;
			short ship;

			forge_modes_drop_point(pending, &place, &yaw, &ship);
			handle = forge_modes_squad_send_by_air(team, composition, &place, yaw, ship);
			if (handle == NONE)
				platform_log("forge modes: a squad of %s cannot come by dropship to %.2f %.2f %.2f: on the ground", composition,
					place.x, place.y, place.z);
			else
			{
				flown = TRUE;
				if (waited && !pending->dropship)
					platform_log("forge modes: every point of its zone in sight for %d s: the squad comes by dropship",
						FORGE_MODES_HIDDEN_WAIT_SECONDS);
			}
		}
		if (handle == NONE)
		{
			if (!forge_modes_place_hidden(&pending->at, pending->radius, &place))
			{
				if (pending->wait_since == NONE)
				{
					pending->wait_since = game_time_get();
					platform_log("forge modes: a squad of %s waits: every point of its zone is in a player's sight", composition);
				}
				return FALSE;
			}
			handle = forge_modes_squad_spawn_team(team, composition, &place);
		}
	}
	if (handle == NONE)
	{
		platform_log("forge modes: a squad of %s could not be made (no such characters loaded?)", composition);
		pending->count = 0;
		forge_modes.nothing_made = TRUE;
		return TRUE;
	}
	forge_modes.squads[slot].handle = handle;
	forge_modes.squads[slot].boss = boss;
	forge_modes.squads[slot].bonus = pending->bonus;
	forge_modes.squads[slot].ally = pending->ally;
	forge_modes.squads[slot].target_unit_index = NONE;
	forge_modes.squads[slot].next_target_time = 0;
	forge_modes.squads[slot].flying = flown;
	{
		short first = forge_modes.unit_count;

		forge_modes_register(slot, boss ? (real)MAX(1, pending->boss_toughness) : 1.f);
		for (index = first; boss && index < forge_modes.unit_count; index++)
		{
			forge_modes.units[index].points = pending->boss_points;
			forge_modes.boss_unit_index = forge_modes.units[index].unit_index;
			snprintf(forge_modes.boss_name, sizeof(forge_modes.boss_name), "%s",
				T(forge_mode_boss_names[pending->boss_name % FORGE_FILE_MODE_BOSS_NAMES]));
		}
	}
	pending->count = (short)(pending->count - count);
	platform_log("forge modes: %s squad of %s %s %.2f %.2f %.2f (enemies %d, allies %d)", boss ? "a boss's" :
		pending->ally ? "an allied" : pending->bonus ? "a bonus round's" : pending->heavy ? "a heavy wave's" : "an enemy", composition, flown ? "flying in to" : "made out of sight at", place.x, place.y,
		place.z, forge_modes_alive(FALSE), forge_modes_alive(TRUE));
	/* (Warzone's boss: no banner of Firefight's - Firefight has none) */
	if (boss)
		pending->boss_kind = NONE;
	return TRUE;
}

/* (the pending squads the next queued are: the heavy wave's, the bonus
round's) */
static boolean forge_modes_queue_heavy;
static boolean forge_modes_queue_bonus;

static void forge_modes_queue(
	short squad,
	short count,
	real_point3d const *at,
	real radius,
	boolean ally,
	boolean dropship,
	real yaw)
{
	struct forge_modes_pending *pending;

	while (count > 0 && forge_modes.pending_count < FORGE_MODES_MAXIMUM_PENDING)
	{
		pending = &forge_modes.pending_squads[forge_modes.pending_count++];
		csmemset(pending, 0, sizeof(*pending));
		pending->squad = squad;
		pending->count = (short)MIN(count, COMMAND_SQUADS_MEMBERS);
		pending->at = *at;
		pending->radius = radius;
		pending->ally = ally;
		pending->heavy = !ally && forge_modes_queue_heavy;
		pending->bonus = !ally && forge_modes_queue_bonus;
		pending->boss_kind = NONE;
		pending->dropship = dropship;
		pending->yaw = yaw;
		pending->wait_since = NONE;
		count = (short)(count - pending->count);
	}
}

static void forge_modes_queue_boss(
	short kind,
	short points,
	short toughness,
	short name,
	real_point3d const *at,
	boolean dropship,
	real yaw)
{
	struct forge_modes_pending *pending;

	if (forge_modes.pending_count >= FORGE_MODES_MAXIMUM_PENDING)
		return;
	/* (the boss first, before the wave's others) */
	csmemmove(&forge_modes.pending_squads[1], &forge_modes.pending_squads[0],
		(size_t)forge_modes.pending_count * sizeof(forge_modes.pending_squads[0]));
	forge_modes.pending_count++;
	pending = &forge_modes.pending_squads[0];
	csmemset(pending, 0, sizeof(*pending));
	pending->squad = NONE;
	pending->count = 1;
	pending->at = *at;
	pending->radius = 2.f;
	pending->boss_kind = kind;
	pending->boss_points = points;
	pending->boss_toughness = toughness;
	pending->boss_name = name;
	pending->dropship = dropship;
	pending->yaw = yaw;
	pending->wait_since = NONE;
	if (forge_modes_bosses[kind].escort != NONE)
		forge_modes_queue(forge_modes_bosses[kind].escort, 3, at, 5.f, FALSE, dropship, yaw);
}

/* whether a mask (0: all) holds number (from 1) */
static boolean forge_modes_mask_has(
	unsigned char mask,
	short number)
{
	return !mask || (mask & (1 << (number - 1)));
}

/* (Firefight round 3) the heavy wave: each round's last, Reach's heavier
squads, unannounced */
static boolean forge_modes_heavy_wave(
	void)
{
	return forge_modes.wave == forge_modes.firefight.waves;
}

/* this wave's squads queued: the layout's zones (Firefight's or both modes'
- the variant's or the built-in layout's) of its round and wave; the heavy
wave's a rank up, with its HEAVY WAVE ONLY zones */
static void forge_modes_wave_begin(
	void)
{
	short index;
	short queued = 0;
	boolean heavy = forge_modes_heavy_wave();
	/* (later waves of a round: one more of each zone each two waves) */
	short more = (short)((forge_modes.wave - 1) / 2 + (forge_modes.set - 1));

	forge_modes.wave_made = 0;
	forge_modes.nothing_made = FALSE;
	forge_modes.straggler_since = NONE;
	forge_modes.pending_count = 0;
	forge_modes_queue_heavy = heavy;
	forge_modes_queue_bonus = FALSE;
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];
		boolean ally = spawn->team == FORGE_MODE_TEAM_RED;
		boolean wave_fits;

		if (spawn->game_types && !(spawn->game_types & FORGE_FILE_GAME_TYPE_FIREFIGHT))
			continue;
		/* (a boss: Warzone's alone - its later game type; Firefight has
		none) */
		if (spawn->kind == _forge_mode_marker_boss)
		{
			if (forge_modes.running == _forge_mode_warzone && forge_modes_mask_has(spawn->mode.rounds, forge_modes.round))
				forge_modes_queue_boss(spawn->variant % NUMBER_OF_FORGE_MODE_BOSSES, spawn->mode.points,
					spawn->mode.toughness, spawn->mode.index, &spawn->home.position, (spawn->mode.flags & FORGE_MODE_FLAG_DROPSHIP) != 0,
					FORGE_MODES_NO_YAW);
			continue;
		}
		if (spawn->kind != _forge_mode_marker_ai_zone || spawn->team == FORGE_MODE_TEAM_BLUE)
			continue;
		/* (allies once, at the round's first wave) */
		if (ally && (forge_modes.wave != 1 || !forge_modes.firefight.allies))
			continue;
		wave_fits = spawn->mode.waves == FORGE_FILE_MODE_HEAVY_WAVE ? heavy :
			forge_modes_mask_has((unsigned char)(spawn->mode.waves & (FORGE_FILE_MODE_HEAVY_WAVE - 1)), forge_modes.wave) ||
			(heavy && (spawn->mode.waves & FORGE_FILE_MODE_HEAVY_WAVE));
		if (!forge_modes_mask_has(spawn->mode.rounds, forge_modes.round) || !wave_fits)
			continue;
		forge_modes_queue(spawn->variant % NUMBER_OF_FORGE_MODE_SQUADS, (short)MIN(8, spawn->count + (ally ? 0 : more)),
			&spawn->home.position, forge_mode_marker_radius(spawn), ally, (spawn->mode.flags & FORGE_MODE_FLAG_DROPSHIP) != 0,
			FORGE_MODES_NO_YAW);
		queued++;
	}
	forge_modes_queue_heavy = FALSE;
	platform_log("forge modes: set %d round %d wave %d%s: %d squads to come (%d zones)", forge_modes.set, forge_modes.round,
		forge_modes.wave, heavy ? " (the heavy wave)" : "", forge_modes.pending_count, queued);
}

static void forge_modes_state(
	short state)
{
	forge_modes.state = state;
	forge_modes.state_time = game_time_get();
}

/* the difficulty this set and round: the start's, plus the ramp */
static void forge_modes_difficulty_update(
	void)
{
	short difficulty = forge_modes.firefight.difficulty;

	if (forge_modes.firefight.ramp == 1)
		difficulty = (short)(difficulty + forge_modes.set - 1);
	else if (forge_modes.firefight.ramp == 2)
		difficulty = (short)(difficulty + (forge_modes.set - 1) * forge_modes.firefight.rounds + forge_modes.round - 1);
	difficulty = (short)PIN(difficulty, _game_difficulty_level_easy, _game_difficulty_level_impossible);
	if (difficulty != forge_modes.difficulty)
	{
		forge_modes.difficulty = difficulty;
		game_difficulty_level_set(difficulty);
		platform_log("forge modes: difficulty %d", difficulty);
	}
}

/* the skulls 1.2.0's engine's Halo 2 skull hooks carry out (Catch's
grenades, Black Eye's shields, Famine's dropped rounds, Blind's HUD), as a
game mode's; Mythic, Thunderstorm and Iron are the modes' own (the squads'
toughness and ranks, the lives) */
static void forge_modes_engine_skulls(
	unsigned short skulls)
{
	/* (1.1.x: Halo CE alone, no Halo 2 skull hooks in the engine - these
	four skulls are 1.2.0's; a layout asking for them is logged, not
	followed. Mythic, Thunderstorm and Iron are the modes' own and work) */
	static unsigned short last = 0;
	unsigned short wanted = (unsigned short)(skulls & (FORGE_MODE_SKULL_CATCH | FORGE_MODE_SKULL_BLACK_EYE |
		FORGE_MODE_SKULL_FAMINE | FORGE_MODE_SKULL_BLIND));

	if (wanted && wanted != last)
		platform_log("forge modes: skulls 0x%x (Catch, Black Eye, Famine, Blind) are not in this version", wanted);
	last = wanted;
}

/* the skulls on this set: the map maker's list, one more each set or all */
static void forge_modes_skulls_update(
	void)
{
	short bit;
	short on = 0;
	short wanted = forge_modes.firefight.skull_mode == 2 ? 16 : forge_modes.firefight.skull_mode == 1 ? (short)(forge_modes.set - 1) : 0;

	forge_modes.skulls_on = 0;
	for (bit = 0; bit < 16 && on < wanted; bit++)
	{
		if (forge_modes.skulls & (1 << bit))
		{
			forge_modes.skulls_on |= (unsigned short)(1 << bit);
			on++;
		}
	}
	forge_modes_engine_skulls(forge_modes.skulls_on);
}

static real forge_modes_score_multiplier(
	void)
{
	short bit;
	real multiplier = 1.f;

	for (bit = 0; bit < 16; bit++)
	{
		if (forge_modes.skulls_on & (1 << bit))
			multiplier += 0.2f;
	}
	return multiplier;
}

/* this round's objective: the layout's first Firefight objective of its
round (a defended object's hull needs the damage step: not yet, skipped) */
static void forge_modes_objective_choose(
	void)
{
	short index;

	forge_modes.objective = NONE;
	forge_modes.objective_ticks = 0;
	forge_modes.objective_done = FALSE;
	forge_modes.objective_time = NONE;
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];

		/* (KILL THE BOSS: none - Firefight has no bosses -; a file's own
		skipped) */
		if (spawn->kind != _forge_mode_marker_ff_objective || !forge_modes_mask_has(spawn->mode.rounds, forge_modes.round) ||
			spawn->variant == _forge_mode_objective_defend_object || spawn->variant == _forge_mode_objective_kill_boss)
		{
			continue;
		}
		forge_modes.objective = index;
		platform_log("forge modes: round %d's objective: %s at %.2f %.2f %.2f, radius %.1f, %d s", forge_modes.round,
			forge_mode_objective_names[spawn->variant % NUMBER_OF_FORGE_MODE_OBJECTIVES], spawn->home.position.x,
			spawn->home.position.y, spawn->home.position.z, forge_mode_marker_radius(spawn), spawn->mode.seconds);
		return;
	}
}

/* the round's objective and time limit looked at: a hold zone's player
inside counts up, a defended zone's enemies inside count to its loss; the
round's time run out ends the game or takes a life */
static void forge_modes_objective_update(
	void)
{
	long now = game_time_get();
	long elapsed = forge_modes.objective_time != NONE ? now - forge_modes.objective_time : 0;
	struct forge_spawn const *spawn = forge_modes.objective != NONE ? &forge_globals.spawns[forge_modes.objective] : NULL;

	forge_modes.objective_time = now;
	if (forge_modes.state != _forge_modes_state_wave && forge_modes.state != _forge_modes_state_wave_break)
		return;
	/* (the round's time) */
	if (forge_modes.firefight.round_minutes &&
		now - forge_modes.round_start_time >= (long)forge_modes.firefight.round_minutes * 60 * TICKS_PER_SECOND)
	{
		if (forge_modes.firefight.round_time_rule == 1 && forge_modes.lives != 0)
		{
			if (forge_modes.lives != NONE)
				forge_modes.lives--;
			forge_modes.round_start_time = now;
			forge_modes_banner(T("Time's up"), T("A life lost"), 4.f);
		}
		else
		{
			forge_modes_banner(T("Time's up"), NULL, 3.f);
			forge_modes_end(FALSE);
			return;
		}
	}
	if (!spawn || forge_modes.objective_done || spawn->kind != _forge_mode_marker_ff_objective)
		return;
	if (spawn->variant == _forge_mode_objective_hold_zone || spawn->variant == _forge_mode_objective_defend_zone)
	{
		real radius = forge_mode_marker_radius(spawn);
		boolean inside = FALSE;
		short index;

		if (spawn->variant == _forge_mode_objective_hold_zone)
		{
			struct data_iterator iterator;
			struct player_datum *player;

			data_iterator_new(&iterator, player_data);
			while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL && !inside)
			{
				real_point3d at;

				if (!forge_modes_living_unit(player->unit_index))
					continue;
				object_get_origin(player->unit_index, &at);
				inside = distance3d(&at, &spawn->home.position) <= radius;
			}
		}
		else
		{
			for (index = 0; index < forge_modes.unit_count && !inside; index++)
			{
				real_point3d at;

				if (forge_modes.units[index].ally || !forge_modes_living_unit(forge_modes.units[index].unit_index))
					continue;
				object_get_origin(forge_modes.units[index].unit_index, &at);
				inside = distance3d(&at, &spawn->home.position) <= radius;
			}
		}
		if (inside)
			forge_modes.objective_ticks += elapsed;
		if (forge_modes.objective_ticks >= (long)spawn->mode.seconds * TICKS_PER_SECOND)
		{
			if (spawn->variant == _forge_mode_objective_hold_zone)
			{
				forge_modes.objective_done = TRUE;
				forge_modes.score += (long)(FORGE_MODES_OBJECTIVE_BONUS * forge_modes_score_multiplier());
				forge_modes_banner(T("Objective complete"), T("The zone is held"), 4.f);
			}
			else
			{
				/* (the zone lost: a life, or the game) */
				forge_modes.objective_ticks = 0;
				if (forge_modes.lives == NONE || forge_modes.lives > 0)
				{
					if (forge_modes.lives != NONE)
						forge_modes.lives--;
					forge_modes_banner(T("The zone is lost"), T("A life lost"), 4.f);
				}
				else
				{
					forge_modes_banner(T("The zone is lost"), NULL, 3.f);
					forge_modes_end(FALSE);
				}
			}
		}
	}
}

static void forge_modes_round_begin(
	void)
{
	char title[64];
	char line[96];

	forge_modes.wave = 1;
	forge_modes_difficulty_update();
	forge_modes_skulls_update();
	forge_modes.round_start_time = game_time_get() + FORGE_MODES_ROUND_START_SECONDS * TICKS_PER_SECOND;
	forge_modes_objective_choose();
	snprintf(title, sizeof(title), T("Round %d"), forge_modes.round);
	if (forge_modes.firefight.sets)
		snprintf(line, sizeof(line), T("Set %d of %d"), forge_modes.set, forge_modes.firefight.sets);
	else
		snprintf(line, sizeof(line), T("Set %d"), forge_modes.set);
	forge_modes_banner(title, line, FORGE_MODES_ROUND_START_SECONDS);
	forge_modes_state(_forge_modes_state_round_start);
}

static void forge_modes_end(
	boolean won)
{
	char line[96];

	forge_modes.over = TRUE;
	forge_modes.won = won;
	snprintf(line, sizeof(line), T("Score %ld"), forge_modes.score);
	forge_modes_banner(won ? T("Firefight won") : T("Game over"), line, FORGE_MODES_OVER_SECONDS + 2);
	forge_modes_state(_forge_modes_state_over);
	platform_log("forge modes: %s at set %d round %d wave %d: score %ld, kills %ld, deaths %ld", won ? "won" : "over",
		forge_modes.set, forge_modes.round, forge_modes.wave, forge_modes.score, forge_modes.kills, forge_modes.deaths);
}

/* kills counted (each enemy once: its points to the player who hurt it
last, a Mythic or other skull's share on top) */
static void forge_modes_kills_update(
	void)
{
	short index;

	for (index = 0; index < forge_modes.unit_count; index++)
	{
		struct forge_modes_unit *unit = &forge_modes.units[index];
		struct object_datum *object;
		long killer = NONE;
		unsigned long latest = 0;
		short attacker;

		if (unit->counted || forge_modes_living_unit(unit->unit_index))
			continue;
		unit->counted = TRUE;
		if (unit->ally)
			continue;
		object = object_try_and_get(unit->unit_index);
		if (object && (FLAG(object->object.type) & _object_mask_unit))
		{
			struct unit_datum const *datum = (struct unit_datum const *)object;

			for (attacker = 0; attacker < MAXIMUM_ATTACKERS_PER_UNIT; attacker++)
			{
				if (datum->unit.attackers[attacker].player_index != NONE && datum->unit.attackers[attacker].game_time_stamp >= latest)
				{
					latest = datum->unit.attackers[attacker].game_time_stamp;
					killer = datum->unit.attackers[attacker].player_index;
				}
			}
		}
		forge_modes.kills++;
		/* (Warzone's boss: no banner of Firefight's) */
		if (unit->unit_index == forge_modes.boss_unit_index)
		{
			forge_modes.boss_unit_index = NONE;
			forge_modes.bosses_killed++;
		}
		/* (a kill no player made - the AI's own, a fall - scores nothing) */
		if (killer != NONE)
		{
			long points = (long)((real)unit->points * forge_modes_score_multiplier() + 0.5f);

			short absolute = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(killer);

			forge_modes.score += points;
			/* (the bonus round's kill: its total too) */
			if (unit->bonus)
			{
				forge_modes.bonus_kills++;
				forge_modes.bonus_score += points;
				forge_modes.bonus_total += points;
			}
			if (absolute >= 0 && absolute < FORGE_SPAWN_PLAYERS)
			{
				forge_modes.player_scores[absolute] += points;
				forge_modes.player_kill_counts[absolute]++;
			}
			if (killer == forge_modes_local_player())
				forge_modes.player_kills++;
		}
	}
}

/* the squads' targets: each enemy squad on the nearest living player */
static void forge_modes_targets_update(
	void)
{
	short slot;
	long now = game_time_get();

	for (slot = 0; slot < FORGE_MODES_MAXIMUM_SQUADS; slot++)
	{
		struct forge_modes_squad *squad = &forge_modes.squads[slot];
		real_point3d middle;
		struct data_iterator iterator;
		struct player_datum *player;
		long best = NONE;
		real best_distance = 1e30f;

		if (squad->handle == NONE)
			continue;
		if (forge_modes_squad_living_count(squad->handle) <= 0)
		{
			forge_modes_squad_release(squad->handle);
			squad->handle = NONE;
			continue;
		}
		/* (a squad by dropship: down, its members the seats had no room for
		counted; its orders from now) */
		if (squad->flying && !forge_modes_squad_airborne(squad->handle))
		{
			squad->flying = FALSE;
			squad->next_target_time = 0;
			forge_modes_register(slot, 1.f);
			if (forge_modes_squad_middle(squad->handle, &middle))
				platform_log("forge modes: a squad down from its dropship at %.2f %.2f %.2f (%d of it)", middle.x, middle.y, middle.z,
					forge_modes_squad_living_count(squad->handle));
		}
		if (squad->flying || now < squad->next_target_time || !forge_modes_squad_middle(squad->handle, &middle))
			continue;
		squad->next_target_time = now + FORGE_MODES_TARGET_TICKS;
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			real_point3d at;
			real distance;

			if (!forge_modes_living_unit(player->unit_index))
				continue;
			object_get_origin(player->unit_index, &at);
			distance = distance3d(&at, &middle);
			if (distance < best_distance)
			{
				best_distance = distance;
				best = player->unit_index;
			}
		}
		/* (allies: with the nearest player, following him when he goes) */
		if (squad->ally)
		{
			if (best != NONE && best_distance > FORGE_MODES_ALLY_FOLLOW)
			{
				real_point3d at;

				object_get_origin(best, &at);
				at.x += (middle.x - at.x) * 4.f / best_distance;
				at.y += (middle.y - at.y) * 4.f / best_distance;
				forge_modes_squad_order_hosted(squad->handle, _command_squad_move, &at, NONE);
			}
			continue;
		}
		if (best != NONE && best != squad->target_unit_index)
		{
			if (forge_modes_squad_order_hosted(squad->handle, _command_squad_attack, NULL, best))
				squad->target_unit_index = best;
		}
		else if (best == NONE && squad->target_unit_index != NONE)
		{
			squad->target_unit_index = NONE;
			forge_modes_squad_order_hosted(squad->handle, _command_squad_hold, NULL, NONE);
		}
	}
}

/* the weapon tag whose name ends so (the map's: every multiplayer map has
the assault rifle and the pistol) */
static long forge_modes_weapon_find(
	char const *name)
{
	struct tag_iterator iterator;
	long tag_index;
	size_t length = strlen(name);

	tag_iterator_new(&iterator, WEAPON_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		char const *tag_name = tag_get_name(tag_index);
		size_t tag_length = tag_name ? strlen(tag_name) : 0;

		if (tag_length >= length && !strcmp(tag_name + tag_length - length, name))
			return tag_index;
	}
	return NONE;
}

/* a player's spawn in FIREFIGHT: the map's own assault rifle and pistol,
two grenades of each kind (Halo CE's multiplayer start, the Forge game's
plasma pistol taken) */
static void forge_modes_loadout(
	long unit_index)
{
	static char const *const weapons[] = { "weapons\\pistol\\pistol", "weapons\\assault rifle\\assault rifle" };
	struct unit_datum *unit = forge_modes_living_unit(unit_index);
	short index;

	if (!unit)
		return;
	unit_delete_all_weapons(unit_index);
	for (index = 0; index < (short)NUMBEROF(weapons); index++)
	{
		long tag_index = forge_modes_weapon_find(weapons[index]);
		struct object_placement_data placement;
		long weapon_index;

		if (tag_index == NONE)
			continue;
		object_placement_data_new(&placement, tag_index, unit_index);
		weapon_index = object_new(&placement);
		if (weapon_index != NONE && !unit_add_weapon_to_inventory(unit_index, weapon_index, TRUE))
			object_delete(weapon_index);
	}
	unit->unit.grenade_counts[_unit_grenade_human_fragmentation] = 2;
	unit->unit.grenade_counts[_unit_grenade_covenant_plasma] = 2;
}

/* the lives: a death takes one; none left, the dead wait; no one alive, the
game is over */
static void forge_modes_lives_update(
	void)
{
	long player_index = forge_modes_local_player();
	struct player_datum *player;

	if (player_index == NONE)
		return;
	player = player_get(player_index);
	if (forge_modes_living_unit(player->unit_index))
	{
		/* (a spawn: FIREFIGHT's loadout) */
		if (forge_modes.player_unit_index != player->unit_index)
			forge_modes_loadout(player->unit_index);
		forge_modes.player_unit_index = player->unit_index;
		return;
	}
	/* (a death seen once) */
	if (forge_modes.player_unit_index != NONE)
	{
		forge_modes.player_unit_index = NONE;
		forge_modes.deaths++;
		if (DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) >= 0 && DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) < FORGE_SPAWN_PLAYERS)
			forge_modes.player_deaths[DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index)]++;
		/* (the bonus round's death: no life - ODST's and Reach's) */
		if (forge_modes.state == _forge_modes_state_bonus_start || forge_modes.state == _forge_modes_state_bonus ||
			forge_modes.state == _forge_modes_state_bonus_end)
		{
			platform_log("forge modes: a death in the bonus round: no life lost");
			return;
		}
		if (forge_modes.lives != NONE)
		{
			short cost = (forge_modes.skulls_on & FORGE_MODE_SKULL_IRON) ? 2 : 1;

			if (forge_modes.lives > 0)
			{
				forge_modes.lives = (short)MAX(0, forge_modes.lives - cost);
				platform_log("forge modes: a life lost: %d left", forge_modes.lives);
				return;
			}
			/* (none left: no one alive is the game over - one player here;
			network play's teammates with the modes' network step) */
			forge_modes_end(FALSE);
			return;
		}
	}
	/* (a death no life paid for: held until the game is over - network play's
teammates, with the modes' network step, wait for the next round) */
	if (forge_modes.over && player->respawn_timer < FORGE_MODES_RESPAWN_HOLD)
		player->respawn_timer = FORGE_MODES_RESPAWN_HOLD;
}

#ifndef HALO_RELEASE
/* (tests, "mode hunt") the player brought near the nearest enemy on the
ground (within 14 wu) and turned to it: the shots are his own, the kill
credit the game's own record of them */
static void forge_modes_hunt(
	void)
{
	long player_index = forge_modes_local_player();
	long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;
	real_point3d eye;
	short index;
	long best = NONE;
	real best_distance = 1e30f;

	boolean watch = game_time_get() < forge_modes.watch_until;
	boolean hunt = game_time_get() < forge_modes.hunt_until;

	if ((!hunt && !watch) || !forge_modes_living_unit(unit_index))
		return;
	/* (hunting: a ship flying in watched first - the shots at it his own) */
	if (hunt && !watch)
	{
		for (index = 0; index < forge_modes.unit_count && !watch; index++)
		{
			struct unit_datum *unit = forge_modes_living_unit(forge_modes.units[index].unit_index);

			watch = unit && !forge_modes.units[index].ally && unit->object.parent_object_index != NONE;
		}
	}
	object_get_origin(unit_index, &eye);
	eye.z += 0.6f;
	for (index = 0; index < forge_modes.unit_count; index++)
	{
		struct unit_datum *unit = forge_modes_living_unit(forge_modes.units[index].unit_index);
		real_point3d at;

		if (!unit || forge_modes.units[index].ally || (unit->object.parent_object_index != NONE) != watch)
			continue;
		object_get_origin(forge_modes.units[index].unit_index, &at);
		if (distance3d(&at, &eye) < best_distance)
		{
			best_distance = distance3d(&at, &eye);
			/* (watching: the ship it rides) */
			best = watch ? unit->object.parent_object_index : forge_modes.units[index].unit_index;
		}
	}
	if (best == NONE)
		return;
	/* (tests, HALO_TEST_FIREFIGHT_SHOTS: a picture of each ship as it comes
	near, once) */
	if (watch && best_distance < 60.f && getenv("HALO_TEST_FIREFIGHT_SHOTS"))
	{
		extern char halo_screenshot_name[64];
		short shot;

		for (shot = 0; shot < (short)NUMBEROF(forge_modes.shot_ships) && forge_modes.shot_ships[shot] != best; shot++)
			;
		if (shot >= (short)NUMBEROF(forge_modes.shot_ships))
		{
			csmemmove(&forge_modes.shot_ships[1], &forge_modes.shot_ships[0],
				sizeof(forge_modes.shot_ships) - sizeof(forge_modes.shot_ships[0]));
			forge_modes.shot_ships[0] = best;
			snprintf(halo_screenshot_name, sizeof(halo_screenshot_name), "ship-%ld", (long)game_time_get());
			platform_log("forge modes: shot of the ship %lx at %.0f wu: %s", (unsigned long)best, best_distance, halo_screenshot_name);
		}
	}
	if (best_distance > 14.f && !watch)
	{
		real_point3d at, place;

		object_get_origin(best, &at);
		place = at;
		place.x += (eye.x - at.x) * 10.f / best_distance;
		place.y += (eye.y - at.y) * 10.f / best_distance;
		place.z += 1.5f;
		forge_spawn_grounded(&place);
		object_set_position(unit_index, &place, NULL, NULL);
		object_get(unit_index)->object.translational_velocity = *global_zero_vector3d;
		platform_log("forge modes: hunt: the player put 10 wu from an enemy at %.2f %.2f %.2f", at.x, at.y, at.z);
		object_get_origin(unit_index, &eye);
		eye.z += 0.6f;
	}
	{
		real_vector3d direction;
		struct player_control *control = player_control_get(forge_globals.local_player_index);

		vector_from_points3d(&eye, &object_get(best)->object.bounding_sphere_center, &direction);
		if (normalize3d(&direction) <= 0.f || !control)
			return;
		control->desired_angles.yaw = arctangent(direction.j, direction.i);
		if (control->desired_angles.yaw < 0.f)
			control->desired_angles.yaw += 2.f * _pi;
		control->desired_angles.pitch = (real)asin(PIN(direction.k, -0.98f, 0.98f));
	}
}
#endif

/* the queue's squads made as the room comes (a wave's, the bonus round's) */
static void forge_modes_pending_update(
	void)
{
	short index;

	for (index = 0; index < forge_modes.pending_count;)
	{
		if (forge_modes.pending_squads[index].count <= 0)
		{
			csmemmove(&forge_modes.pending_squads[index], &forge_modes.pending_squads[index + 1],
				(size_t)(forge_modes.pending_count - index - 1) * sizeof(forge_modes.pending_squads[0]));
			forge_modes.pending_count--;
			continue;
		}
		if (!forge_modes_spawn(&forge_modes.pending_squads[index]))
			break;
		if (forge_modes.pending_squads[index].count > 0)
			break;
	}
}

/* (forge_modes_begin) the bonus round's settings: the layout's first bonus
round marker's (its time, its points a kill, its squad and how many, or the
bonus round off), else ours - a minute of Grunts, 100 points a kill */
static void forge_modes_bonus_settings(
	void)
{
	short index;

	forge_modes.bonus_on = TRUE;
	forge_modes.bonus_seconds = FORGE_MODE_BONUS_SECONDS;
	forge_modes.bonus_points = FORGE_MODE_BONUS_POINTS;
	forge_modes.bonus_squad = _forge_mode_squad_grunts;
	forge_modes.bonus_count = 4;
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];

		if (spawn->kind != _forge_mode_marker_bonus_round)
			continue;
		forge_modes.bonus_on = !(spawn->mode.flags & FORGE_MODE_FLAG_OFF);
		forge_modes.bonus_seconds = (short)PIN(spawn->mode.seconds, FORGE_FILE_BONUS_MINIMUM_SECONDS, FORGE_FILE_BONUS_MAXIMUM_SECONDS);
		forge_modes.bonus_points = (short)PIN(spawn->mode.points, 25, 500);
		forge_modes.bonus_squad = (short)(spawn->variant % NUMBER_OF_FORGE_MODE_SQUADS);
		forge_modes.bonus_count = (short)PIN(spawn->count, 1, 8);
		break;
	}
}

/* the bonus round's rush kept coming: a squad at a time while fewer than
FORGE_MODES_BONUS_ALIVE live, from the bonus round markers in turn, else
from the layout's hostile zones (Grunts, on the ground) */
static void forge_modes_bonus_queue(
	void)
{
	short places[16];
	short count = 0;
	short index;
	boolean markers = FALSE;
	struct forge_spawn const *spawn;

	if (forge_modes.pending_count > 0 || forge_modes_alive(FALSE) + forge_modes.bonus_count > FORGE_MODES_BONUS_ALIVE + 2)
		return;
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE && count < (short)NUMBEROF(places); index++)
	{
		spawn = &forge_globals.spawns[index];
		if (spawn->kind == _forge_mode_marker_bonus_round && !(spawn->mode.flags & FORGE_MODE_FLAG_OFF))
		{
			if (!markers)
				count = 0;
			markers = TRUE;
			places[count++] = index;
		}
		else if (!markers && spawn->kind == _forge_mode_marker_ai_zone && spawn->team == FORGE_MODE_TEAM_NONE &&
			(!spawn->game_types || (spawn->game_types & FORGE_FILE_GAME_TYPE_FIREFIGHT)))
		{
			places[count++] = index;
		}
	}
	if (!count)
		return;
	spawn = &forge_globals.spawns[places[forge_modes.bonus_next_zone++ % count]];
	forge_modes_queue_bonus = TRUE;
	forge_modes_queue(markers ? forge_modes.bonus_squad : (short)_forge_mode_squad_grunts, forge_modes.bonus_count,
		&spawn->home.position, MAX(4.f, forge_mode_marker_radius(spawn)), FALSE,
		markers && (spawn->mode.flags & FORGE_MODE_FLAG_DROPSHIP) != 0, FORGE_MODES_NO_YAW);
	forge_modes_queue_bonus = FALSE;
}

/* the next set (a round's banner), or the game won after the last */
static void forge_modes_set_next(
	void)
{
	if (forge_modes.firefight.sets && forge_modes.set >= forge_modes.firefight.sets)
	{
		forge_modes_end(TRUE);
		return;
	}
	forge_modes.set++;
	forge_modes.round = 1;
	forge_modes_round_begin();
}

/* (a set's last round cleared) the bonus round's banner */
static void forge_modes_bonus_begin(
	void)
{
	char line[96];

	forge_modes.bonus_kills = 0;
	forge_modes.bonus_score = 0;
	forge_modes.pending_count = 0;
	snprintf(line, sizeof(line), T("%d seconds: %d points a kill"), forge_modes.bonus_seconds,
		(int)(forge_modes.bonus_points * forge_modes_score_multiplier() + 0.5f));
	forge_modes_banner(T("Bonus round"), line, FORGE_MODES_BONUS_START_SECONDS);
	forge_modes_state(_forge_modes_state_bonus_start);
	platform_log("forge modes: set %d's bonus round: %d s, %d points a kill, squads of %s", forge_modes.set,
		forge_modes.bonus_seconds, forge_modes.bonus_points, forge_modes_squads[forge_modes.bonus_squad].composition);
}

/* the bonus round's time up: its enemies left gone (no points), its total */
static void forge_modes_bonus_finish(
	void)
{
	char line[96];
	short index;
	short erased = 0;

	forge_modes.pending_count = 0;
	for (index = 0; index < forge_modes.unit_count; index++)
	{
		struct forge_modes_unit *unit = &forge_modes.units[index];
		struct unit_datum *datum = forge_modes_living_unit(unit->unit_index);

		if (!datum || unit->ally || unit->counted)
			continue;
		unit->counted = TRUE;
		if (datum->unit.actor_index != NONE)
			actor_delete(datum->unit.actor_index, FALSE);
		if (datum->unit.player_index == NONE)
			object_delete(unit->unit_index);
		erased++;
	}
	snprintf(line, sizeof(line), T("%ld kills: %ld points"), forge_modes.bonus_kills, forge_modes.bonus_score);
	forge_modes_banner(T("Bonus round over"), line, FORGE_MODES_BONUS_END_SECONDS);
	forge_modes_state(_forge_modes_state_bonus_end);
	platform_log("forge modes: the bonus round over: %ld kills, %ld points (%ld in all), %d left gone", forge_modes.bonus_kills,
		forge_modes.bonus_score, forge_modes.bonus_total, erased);
}

static void forge_modes_firefight_update(
	void)
{
	long now = game_time_get();
	long in_state = now - forge_modes.state_time;

#ifndef HALO_RELEASE
	forge_modes_hunt();
#endif
	forge_modes_kills_update();
	forge_modes_targets_update();
	if (!forge_modes.over)
		forge_modes_lives_update();
	if (!forge_modes.over)
		forge_modes_objective_update();
	switch (forge_modes.state)
	{
	case _forge_modes_state_round_start:
		if (in_state >= FORGE_MODES_ROUND_START_SECONDS * TICKS_PER_SECOND)
		{
			forge_modes_wave_begin();
			forge_modes_state(_forge_modes_state_wave);
		}
		break;
	case _forge_modes_state_wave:
	{
		short enemies;

		/* (the queue made as room comes) */
		forge_modes_pending_update();
		enemies = forge_modes_alive(FALSE);
		if (forge_modes.pending_count > 0)
			break;
		/* (a wave none of whose squads could be made - the mode's packs not
		built, or no memory for them on a Vita: 1.1.5 - is no game: said, and
		over, rather than a round of nothing cleared) */
		if (enemies == 0 && forge_modes.wave_made == 0 && forge_modes.nothing_made)
		{
			forge_modes_banner(T("No enemies could be made"), T("The mode's packs are not built, or there is no memory"), 6.f);
			forge_modes_end(FALSE);
			break;
		}
		/* (done: all dead, or one or two holding out a while) */
		if (enemies > 0 && enemies <= 2)
		{
			if (forge_modes.straggler_since == NONE)
				forge_modes.straggler_since = now;
		}
		else
			forge_modes.straggler_since = NONE;
		if (enemies == 0 || (forge_modes.straggler_since != NONE &&
				now - forge_modes.straggler_since >= FORGE_MODES_STRAGGLER_SECONDS * TICKS_PER_SECOND))
		{
			platform_log("forge modes: wave %d of round %d done (%d made, %d left)", forge_modes.wave, forge_modes.round,
				forge_modes.wave_made, enemies);
			forge_modes.waves_cleared++;
			/* (a round whose zone is not held yet: its last wave again) */
			if (forge_modes.wave >= forge_modes.firefight.waves && forge_modes.objective != NONE && !forge_modes.objective_done &&
				forge_globals.spawns[forge_modes.objective].variant == _forge_mode_objective_hold_zone)
			{
				forge_modes_banner(T("Hold the zone"), T("to clear the round"), 4.f);
				forge_modes_state(_forge_modes_state_wave_break);
			}
			else if (forge_modes.wave >= forge_modes.firefight.waves)
			{
				char line[96];

				forge_modes.score += (long)(FORGE_MODES_ROUND_BONUS * forge_modes_score_multiplier());
				if (forge_modes.lives != NONE)
					forge_modes.lives = (short)MIN(99, forge_modes.lives + forge_modes.firefight.lives_per_round);
				snprintf(line, sizeof(line), T("Bonus %d"), (int)(FORGE_MODES_ROUND_BONUS * forge_modes_score_multiplier()));
				forge_modes_banner(T("Round complete"), line, FORGE_MODES_ROUND_BREAK_SECONDS - 2);
				forge_modes_state(_forge_modes_state_round_break);
			}
			else
			{
				forge_modes.wave++;
				forge_modes_state(_forge_modes_state_wave_break);
			}
		}
		break;
	}
	case _forge_modes_state_wave_break:
		if (in_state >= FORGE_MODES_WAVE_BREAK_SECONDS * TICKS_PER_SECOND)
		{
			forge_modes_wave_begin();
			forge_modes_state(_forge_modes_state_wave);
		}
		break;
	case _forge_modes_state_round_break:
		if (in_state >= FORGE_MODES_ROUND_BREAK_SECONDS * TICKS_PER_SECOND)
		{
			/* (a set's last round: its bonus round, then the next set) */
			if (forge_modes.round >= forge_modes.firefight.rounds)
			{
				if (forge_modes.bonus_on)
					forge_modes_bonus_begin();
				else
					forge_modes_set_next();
				break;
			}
			forge_modes.round++;
			forge_modes_round_begin();
		}
		break;
	case _forge_modes_state_bonus_start:
		if (in_state >= FORGE_MODES_BONUS_START_SECONDS * TICKS_PER_SECOND)
		{
			forge_modes.bonus_until = now + (long)forge_modes.bonus_seconds * TICKS_PER_SECOND;
			forge_modes.wave_made = 0;
			forge_modes.bonus_next_zone = 0;
			forge_modes_state(_forge_modes_state_bonus);
			forge_modes_bonus_queue();
		}
		break;
	case _forge_modes_state_bonus:
		if (now >= forge_modes.bonus_until)
		{
			forge_modes_bonus_finish();
			break;
		}
		forge_modes_bonus_queue();
		forge_modes_pending_update();
		break;
	case _forge_modes_state_bonus_end:
		if (in_state >= FORGE_MODES_BONUS_END_SECONDS * TICKS_PER_SECOND)
			forge_modes_set_next();
		break;
	case _forge_modes_state_over:
		if (in_state >= FORGE_MODES_OVER_SECONDS * TICKS_PER_SECOND && forge_modes.running)
		{
			forge_modes.running = _forge_mode_none;
			command_squads_host(FALSE);
			forge_modes_engine_skulls(0);
			platform_log("forge modes: the game ends (the carnage report)");
			game_engine_end_game();
		}
		break;
	}
}

/* the layout's Firefight zones (hostile or allied, of Firefight's or both
modes') */
static short forge_modes_zones_count(
	void)
{
	short index;
	short count = 0;

	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];

		if (spawn->kind == _forge_mode_marker_ai_zone && spawn->team != FORGE_MODE_TEAM_BLUE &&
			(!spawn->game_types || (spawn->game_types & FORGE_FILE_GAME_TYPE_FIREFIGHT)))
		{
			count++;
		}
	}
	return count;
}

/* a mode marker of ours added to the layout's table (a built-in layout's,
the map's own starts'), as a map variant's record would be */
static boolean forge_modes_marker_add(
	short kind,
	real_point3d const *at,
	real yaw,
	short variant,
	short count,
	short radius,
	unsigned char rounds,
	unsigned char waves,
	boolean dropship)
{
	struct forge_file_record record;
	struct forge_transform home;

	csmemset(&record, 0, sizeof(record));
	csmemset(&home, 0, sizeof(home));
	record.team = kind == _forge_mode_marker_drop_point ? FORGE_MODE_TEAM_HOSTILE : FORGE_MODE_TEAM_NONE;
	record.variant = (unsigned char)variant;
	record.count = (unsigned char)count;
	record.game_types = FORGE_FILE_GAME_TYPE_FIREFIGHT;
	record.mode.radius = (unsigned char)(radius * FORGE_FILE_MODE_RADIUS_UNITS);
	record.mode.rounds = rounds;
	record.mode.waves = waves;
	record.mode.flags = (unsigned char)(dropship ? FORGE_MODE_FLAG_DROPSHIP : 0);
	if (kind == _forge_mode_marker_bonus_round)
	{
		record.mode.seconds = FORGE_MODE_BONUS_SECONDS;
		record.mode.points = FORGE_MODE_BONUS_POINTS;
	}
	if (kind == _forge_mode_marker_drop_point)
	{
		record.game_types = 0;
		csmemset(&record.mode, 0, sizeof(record.mode));
	}
	home.position = *at;
	home.forward.i = cosine(yaw);
	home.forward.j = sine(yaw);
	home.up = *global_up3d;
	return forge_mode_marker_add(0, kind, &home, &record) != NULL;
}

/* (forge_modes_map_loaded) a stock map's built-in layout put in the
Forge layout's table, when the map variant has no Firefight zone: its
start's initial spawns and respawn points, its zones (by dropship: a
hostile drop point of ours on each, facing the way the ship comes), its
bonus round's place */
static void forge_modes_builtin_load(
	char const *scenario_name)
{
	char map[FORGE_FILE_MAP_NAME_SIZE];
	short builtin, index;
	short zones = 0;

	forge_modes.builtin = NONE;
	if (forge_modes_zones_count() || !forge_map_short_name(scenario_name, map, sizeof(map)))
		return;
	builtin = forge_modes_builtin_of(map);
	if (builtin == NONE)
		return;
	for (index = 0; index < FORGE_MODES_BUILTIN_STARTS; index++)
	{
		real const *start = forge_modes_builtins[builtin].starts[index];
		struct forge_transform home;
		short kind;

		csmemset(&home, 0, sizeof(home));
		home.position.x = start[0];
		home.position.y = start[1];
		home.position.z = start[2];
		home.forward.i = cosine(start[3]);
		home.forward.j = sine(start[3]);
		home.up = *global_up3d;
		for (kind = 0; kind < 2; kind++)
			forge_spawn_add(0, kind ? _forge_spawn_respawn : _forge_spawn_initial, NONE, 0, 0, &home);
	}
	for (index = 0; index < FORGE_MODES_BUILTIN_ZONES; index++)
	{
		struct forge_modes_builtin_zone const *zone = &forge_modes_builtins[builtin].zones[index];
		real_point3d at;
		boolean dropship = zone->yaw != FORGE_MODES_GROUND;

		if (!zone->count)
			continue;
		at.x = zone->x;
		at.y = zone->y;
		at.z = zone->z;
		if (forge_modes_marker_add(_forge_mode_marker_ai_zone, &at, 0.f, zone->squad, zone->count, zone->radius, zone->rounds,
				zone->waves, dropship))
		{
			zones++;
		}
		if (dropship)
			forge_modes_marker_add(_forge_mode_marker_drop_point, &at, zone->yaw, _forge_mode_ship_by_species, 0, 0, 0, 0, FALSE);
	}
	{
		real const *bonus = forge_modes_builtins[builtin].bonus;
		real_point3d at = { bonus[0], bonus[1], bonus[2] };

		forge_modes_marker_add(_forge_mode_marker_bonus_round, &at, 0.f, _forge_mode_squad_grunts, 4, (short)bonus[3], 0, 0, FALSE);
	}
	forge_modes.builtin = builtin;
	platform_log("forge modes: %s's built-in Firefight layout: %d zones, its start and bonus round", map, zones);
}

/* (forge_modes_begin) no zone of the variant's nor a built-in layout's:
zones at the map's own starting locations away from the player - up to
four, each the farthest from the player and the others -, our squads the
built-in layouts' kinds, on the ground */
static void forge_modes_zones_from_map(
	void)
{
	static struct
	{
		short squad;
		unsigned char count, rounds, waves;
	} const kinds[] =
	{
		{ _forge_mode_squad_grunts, 4, 0, 0 },
		{ _forge_mode_squad_grunts_jackals, 4, 0, 0x1E },
		{ _forge_mode_squad_elite_grunts, 4, 0x06, 0x1C },
		{ _forge_mode_squad_elites, 2, 0x04, 0x18 },
	};
	struct scenario *scenario = global_scenario_get();
	long player_index = forge_modes_local_player();
	real_point3d player;
	real_point3d chosen[NUMBEROF(kinds)];
	short count = 0;

	if (!scenario || player_index == NONE || !forge_modes_living_unit(player_get(player_index)->unit_index))
		return;
	object_get_origin(player_get(player_index)->unit_index, &player);
	while (count < (short)NUMBEROF(kinds))
	{
		long index, best = NONE;
		real best_distance = 25.f;

		for (index = 0; index < scenario->players.count; index++)
		{
			struct player_starting_location const *start =
				TAG_BLOCK_GET_ELEMENT(&scenario->players, index, struct player_starting_location);
			real nearest = distance3d(&start->position, &player);
			short other;

			for (other = 0; other < count; other++)
				nearest = MIN(nearest, distance3d(&start->position, &chosen[other]) * 2.f);
			if (nearest > best_distance)
			{
				best_distance = nearest;
				best = index;
			}
		}
		if (best == NONE)
			break;
		chosen[count] = TAG_BLOCK_GET_ELEMENT(&scenario->players, best, struct player_starting_location)->position;
		forge_modes_marker_add(_forge_mode_marker_ai_zone, &chosen[count], 0.f, kinds[count].squad, kinds[count].count, 5,
			kinds[count].rounds, kinds[count].waves, FALSE);
		count++;
	}
	/* (the heavy wave's Hunters at the farthest) */
	if (count)
		forge_modes_marker_add(_forge_mode_marker_ai_zone, &chosen[0], 0.f, _forge_mode_squad_hunters, 2, 5, 0,
			FORGE_FILE_MODE_HEAVY_WAVE, FALSE);
	platform_log("forge modes: no Firefight zones: %d from the map's own starts", count);
}

/* the Firefight game begun, once the player has spawned: the monitor gone,
the squads hosted, the first round */
static void forge_modes_begin(
	void)
{
	short index;

	forge_modes.pending = _forge_mode_none;
	forge_modes.running = _forge_mode_firefight;
	forge_modes.over = FALSE;
	forge_modes.won = FALSE;
	forge_modes.firefight = forge_globals.mode_settings.firefight;
	forge_modes.skulls = forge_globals.mode_settings.skulls;
#ifndef HALO_RELEASE
	/* (tests: HALO_TEST_FIREFIGHT="sets rounds waves lives [skulls skull mode]") */
	{
		char const *test = getenv("HALO_TEST_FIREFIGHT");
		int sets, rounds, waves, lives, skulls = -1, skull_mode = -1;

		if (test && sscanf(test, "%d %d %d %d %d %d", &sets, &rounds, &waves, &lives, &skulls, &skull_mode) >= 4)
		{
			forge_modes.firefight.sets = (unsigned char)PIN(sets, 0, 5);
			forge_modes.firefight.rounds = (unsigned char)PIN(rounds, 1, 5);
			forge_modes.firefight.waves = (unsigned char)PIN(waves, 1, 5);
			forge_modes.firefight.lives = (unsigned char)PIN(lives, 0, 30);
			if (skulls >= 0)
				forge_modes.skulls = (unsigned short)(skulls & FORGE_MODE_SKULLS_KNOWN);
			if (skull_mode >= 0)
				forge_modes.firefight.skull_mode = (unsigned char)PIN(skull_mode, 0, 2);
		}
	}
#endif
	forge_modes.set = forge_modes.round = 1;
	forge_modes.wave = 1;
	forge_modes.score = forge_modes.kills = forge_modes.deaths = forge_modes.player_kills = 0;
	forge_modes.waves_cleared = forge_modes.bosses_killed = 0;
	forge_modes.bonus_kills = forge_modes.bonus_score = forge_modes.bonus_total = 0;
	forge_modes.bonus_next_zone = 0;
	csmemset(forge_modes.player_scores, 0, sizeof(forge_modes.player_scores));
	csmemset(forge_modes.player_kill_counts, 0, sizeof(forge_modes.player_kill_counts));
	csmemset(forge_modes.player_deaths, 0, sizeof(forge_modes.player_deaths));
	forge_modes.reported = TRUE;
	forge_modes.lives = forge_modes.firefight.lives ? (short)forge_modes.firefight.lives : (short)NONE;
	forge_modes.unit_count = 0;
	forge_modes.pending_count = 0;
	forge_modes.player_unit_index = NONE;
	forge_modes.boss_unit_index = NONE;
	forge_modes.difficulty_before = game_difficulty_level_get();
	forge_modes.difficulty = NONE;
	for (index = 0; index < FORGE_MODES_MAXIMUM_SQUADS; index++)
		forge_modes.squads[index].handle = NONE;
	if (forge_globals.active)
		forge_end();
	command_squads_host(TRUE);
	/* (no zone of the variant's nor a built-in layout's: the map's own
	starts') */
	{
		boolean from_map = FALSE;

		if (!forge_modes_zones_count())
		{
			forge_modes_zones_from_map();
			from_map = TRUE;
		}
		forge_modes_bonus_settings();
		platform_log("forge modes: FIREFIGHT begun on %s: %d sets, %d rounds, %d waves, lives %d, difficulty %d, ramp %d, "
			"skulls %d (mode %d), %d zones (%s), bonus round %s (%d s, %d points a kill)", main_get_map_name() ? main_get_map_name() : "?",
			forge_modes.firefight.sets, forge_modes.firefight.rounds, forge_modes.firefight.waves, forge_modes.lives,
			forge_modes.firefight.difficulty, forge_modes.firefight.ramp, forge_modes.skulls, forge_modes.firefight.skull_mode,
			forge_modes_zones_count(), from_map ? "the map's own starts'" : forge_modes.builtin != NONE ? "the built-in layout's" :
			"the map variant's", forge_modes.bonus_on ? "on" : "off", forge_modes.bonus_seconds, forge_modes.bonus_points);
	}
	forge_modes_round_begin();
}

/* the recipes the game's squads need, loaded now from the modes' own pack
files (PREPARING COMMAND's; one not built yet built now, as the map loads):
Command's and the modes' own */
static void forge_modes_build_out(
	void)
{
	char const *wanted[8];
	short count = 0;
	short index;

	/* (each zone's and boss's recipe, once; Blood Gulch's own the Covenant's) */
	for (index = -1; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
	{
		char const *recipe = NULL;
		short other;

		if (index < 0)
			recipe = "mode_covenant";
		else if (forge_globals.spawns[index].kind == _forge_mode_marker_ai_zone)
			recipe = forge_modes_squads[forge_globals.spawns[index].variant % NUMBER_OF_FORGE_MODE_SQUADS].recipe;
		else if (forge_globals.spawns[index].kind == _forge_mode_marker_boss)
			recipe = forge_modes_bosses[forge_globals.spawns[index].variant % NUMBER_OF_FORGE_MODE_BOSSES].recipe;
		if (!recipe)
			continue;
		for (other = 0; other < count && strcmp(wanted[other], recipe); other++)
			;
		if (other == count && count < (short)NUMBEROF(wanted))
			wanted[count++] = recipe;
	}
	/* (the dropships and Pelicans the squads may come in: Command's, b40's)
	*/
	if (count < (short)NUMBEROF(wanted))
		wanted[count++] = "command_dropships";
	for (index = 0; index < count; index++)
	{
		unsigned long before = scene_packs_reserved_bytes();
		unsigned long long started = vita_host_time_us();

		/* (Forge's own file pack of the kind when the session has it: its
		characters are there, the same tags) */
		{
			static char const *const same[][2] =
			{
				{ "command_marines", "marine_ai" }, { "mode_covenant", "covenant_ai" }, { "mode_flood", "flood_ai" },
				{ "mode_sentinels", "sentinel_ai" },
			};
			short kind;
			boolean have = FALSE;

			for (kind = 0; kind < (short)NUMBEROF(same); kind++)
			{
				short pack = forge_pack_of_name(same[kind][1]);

				if (!strcmp(same[kind][0], wanted[index]) && pack != NONE && (forge_globals.packs_session & (1UL << pack)))
					have = TRUE;
			}
			if (have)
			{
				platform_log("forge modes: build-out: %s's characters are in the session's Forge pack", wanted[index]);
				continue;
			}
		}
		if (scene_packs_load_now(wanted[index]))
		{
			platform_log("forge modes: build-out: %s loaded in %.1f s (%lu KB more of the tag cache)", wanted[index],
				(double)(vita_host_time_us() - started) / 1000000.0, (scene_packs_reserved_bytes() - before) / 1024);
		}
		else
		{
			char why[160];
			long progress = 0;

			why[0] = 0;
			scene_packs_status(wanted[index], &progress, why, sizeof(why));
			platform_log("forge modes: build-out: %s not loaded: %s", wanted[index], why[0] ? why : "?");
		}
	}
}

/* ---------- public code (forge.c's) */

/* (forge_map_loaded, after the layout's markers) the last game's forgotten;
a game asked for begun on its map once the player has spawned */
static void forge_modes_map_loaded(
	char const *scenario_name,
	boolean forge_game)
{
	if (forge_modes.running && forge_modes.difficulty != NONE)
		game_difficulty_level_set(forge_modes.difficulty_before);
	forge_modes.running = _forge_mode_none;
	forge_modes.pending = _forge_mode_none;
	forge_modes.over = FALSE;
	forge_modes.reported = FALSE;
	forge_modes_engine_skulls(0);
	forge_modes.banner_until = 0;
#ifndef HALO_RELEASE
	if (getenv("HALO_TEST_FIREFIGHT_GAME") && forge_game)
		forge_modes.requested = _forge_mode_firefight;
#endif
	if (forge_modes.requested == _forge_mode_none)
		return;
	if (!forge_game || !scenario_name)
	{
		forge_modes.requested = _forge_mode_none;
		return;
	}
	forge_modes.pending = forge_modes.requested;
	forge_modes.requested = _forge_mode_none;
	/* (the map variant's Firefight zones, else the map's built-in layout:
	before anyone spawns, its start theirs) */
	forge_modes_builtin_load(scenario_name);
	forge_modes_build_out();
	platform_log("forge modes: %s loaded for a FIREFIGHT game", scenario_name);
}

/* (forge_update, each frame) the game asked for begun once the player has
spawned; the game run */
static void forge_modes_update(
	void)
{
	if (forge_modes.pending != _forge_mode_none && !forge_modes.running)
	{
		long player_index = forge_modes_local_player();

		if (forge_globals.game_active && player_index != NONE && forge_modes_living_unit(player_get(player_index)->unit_index) &&
			!game_time_get_paused())
		{
			forge_modes_begin();
		}
	}
	if (!forge_modes.running || game_time_get_paused())
		return;
	if (!forge_globals.game_active || game_connection() != _game_connection_local)
	{
		forge_modes.running = _forge_mode_none;
		command_squads_host(FALSE);
		forge_modes_engine_skulls(0);
		return;
	}
	if (game_time_get() < forge_modes.next_think_time && forge_modes.state != _forge_modes_state_wave &&
		forge_modes.state != _forge_modes_state_bonus)
	{
		return;
	}
	forge_modes.next_think_time = game_time_get() + FORGE_MODES_THINK_TICKS;
	forge_modes_firefight_update();
}

/* the skulls' names (FORGE_MODE_SKULL_*' order: Halo 2's) */
static char const *const forge_modes_skull_names[] =
{
	"MYTHIC", "CATCH", "BLACK EYE", "FAMINE", "THUNDERSTORM", "IRON", "BLIND",
};

/* (forge_draw's, the HUD) in the Halo 3 menus' look, as Command mode's
(command_hud.h): the game's box at the bottom right, where ODST's and
Reach's Firefight keep their score and lives (Halo 3's scoreboard's place;
Firefight round 3: at the top left it lay over the game's own messages -
"Respawn in", "was killed by") - its set and round, the wave's band filling
as the wave's enemies die, the score, the lives, the skulls on, the round's
objective and time; in the bonus round its time's band running down, its
kills and points. The banner in the middle. Blind: the banner only */
#define FORGE_MODES_HUD_BOX 250.f
#define FORGE_MODES_HUD_ROW 17.f
/* (the box's bottom above the screen's: clear of the game's own lower
corner) */
#define FORGE_MODES_HUD_BOTTOM 418.f

static void forge_modes_draw(
	short local_player_index)
{
	real width, x, y;
	real const box = FORGE_MODES_HUD_BOX;
	char line[128];
	char right[64];
	boolean bonus = forge_modes.state == _forge_modes_state_bonus_start || forge_modes.state == _forge_modes_state_bonus ||
		forge_modes.state == _forge_modes_state_bonus_end;

	if (!forge_modes.running || local_player_index != forge_globals.local_player_index || game_time_get_paused())
		return;
	if (!command_hud_frame_begin(&width))
		return;
	if (!(forge_modes.skulls_on & FORGE_MODE_SKULL_BLIND))
	{
		short enemies = forge_modes_alive(FALSE);
		short made = (short)MAX(forge_modes.wave_made, enemies);
		real fill = forge_modes.state == _forge_modes_state_wave && made > 0 ? (real)(made - enemies) / (real)made :
			forge_modes.state == _forge_modes_state_wave ? 0.f : 1.f;
		short rows = 3;
		boolean objective = !bonus && forge_modes.objective != NONE && !forge_modes.objective_done &&
			forge_modes.objective < forge_globals.spawn_count;
		boolean round_time = !bonus && forge_modes.firefight.round_minutes;

		rows = (short)(rows + (forge_modes.skulls_on ? 1 : 0) + (objective ? 1 : 0) + (round_time ? 1 : 0));
		/* (its rows from the bottom right up) */
		x = width - box - 12.f;
		y = FORGE_MODES_HUD_BOTTOM - 19.f - FORGE_MODES_HUD_ROW * (real)(rows - 1);
		/* (the title strip: the mode, its set and round - or the bonus
		round) */
		command_hud_draw_art(COMMAND_HUD_ART_STRIP, x, y, x + box, y + 18.f, 0.85f);
		command_hud_draw_text(COMMAND_HUD_FONT_ITEM, x + 6.f, y + 2.f, x + box, 0, COMMAND_HUD_LIGHT, T(FORGE_FIREFIGHT_NAME));
		if (bonus)
			snprintf(line, sizeof(line), "%s", T("BONUS ROUND"));
		else if (forge_modes.firefight.sets)
			snprintf(line, sizeof(line), T("SET %d/%d  ROUND %d/%d"), forge_modes.set, forge_modes.firefight.sets, forge_modes.round,
				forge_modes.firefight.rounds);
		else
			snprintf(line, sizeof(line), T("SET %d  ROUND %d/%d"), forge_modes.set, forge_modes.round, forge_modes.firefight.rounds);
		command_hud_draw_text(COMMAND_HUD_FONT_ITEM, x, y + 2.f, x + box - 6.f, 1, bonus ? COMMAND_HUD_ORANGE : COMMAND_HUD_DIM, line);
		y += 19.f;
		if (bonus)
		{
			/* (the bonus round's time: its band running down, its kills) */
			long left = forge_modes.state == _forge_modes_state_bonus ? MAX(0, forge_modes.bonus_until - game_time_get()) :
				forge_modes.state == _forge_modes_state_bonus_start ? (long)forge_modes.bonus_seconds * TICKS_PER_SECOND : 0;
			long seconds = (left + TICKS_PER_SECOND - 1) / TICKS_PER_SECOND;

			fill = forge_modes.bonus_seconds > 0 ? (real)left / (real)((long)forge_modes.bonus_seconds * TICKS_PER_SECOND) : 0.f;
			command_hud_draw_art(COMMAND_HUD_ART_PANEL, x, y, x + box, y + 16.f, 0.8f);
			command_hud_draw_art(COMMAND_HUD_ART_BAND_RED, x, y, x + MAX(4.f, box * PIN(fill, 0.f, 1.f)), y + 16.f, 0.95f);
			snprintf(line, sizeof(line), T("TIME %ld:%02ld"), seconds / 60, seconds % 60);
			command_hud_draw_text(COMMAND_HUD_FONT_ITEM, x + 6.f, y + 1.f, x + box, 0, seconds <= 10 && left > 0 ? COMMAND_HUD_RED :
				COMMAND_HUD_LIGHT, line);
			snprintf(right, sizeof(right), T("KILLS %ld"), forge_modes.bonus_kills);
			command_hud_draw_text(COMMAND_HUD_FONT_ITEM, x, y + 1.f, x + box - 6.f, 1, COMMAND_HUD_LIGHT, right);
		}
		else
		{
			/* (the wave: its band filled by the enemies killed) */
			command_hud_draw_art(COMMAND_HUD_ART_PANEL, x, y, x + box, y + 16.f, 0.8f);
			command_hud_draw_art(COMMAND_HUD_ART_BAND_BLUE, x, y, x + MAX(4.f, box * PIN(fill, 0.f, 1.f)), y + 16.f, 0.95f);
			snprintf(line, sizeof(line), T("WAVE %d/%d"), forge_modes.wave, forge_modes.firefight.waves);
			command_hud_draw_text(COMMAND_HUD_FONT_ITEM, x + 6.f, y + 1.f, x + box, 0, COMMAND_HUD_LIGHT, line);
			snprintf(right, sizeof(right), T("ENEMIES %d"), enemies);
			command_hud_draw_text(COMMAND_HUD_FONT_ITEM, x, y + 1.f, x + box - 6.f, 1, COMMAND_HUD_LIGHT, right);
		}
		y += FORGE_MODES_HUD_ROW;
		/* (the score, the lives - in the bonus round its points) */
		command_hud_draw_art(COMMAND_HUD_ART_PANEL, x, y, x + box, y + 16.f, 0.8f);
		command_hud_draw_text(COMMAND_HUD_FONT_ITEM, x + 6.f, y + 1.f, x + box, 0, COMMAND_HUD_ITEM, T("SCORE"));
		snprintf(line, sizeof(line), "%ld", forge_modes.score);
		command_hud_draw_text(COMMAND_HUD_FONT_ITEM, x + 60.f, y + 1.f, x + box * 0.5f, 0, COMMAND_HUD_LIGHT, line);
		command_hud_draw_text(COMMAND_HUD_FONT_ITEM, x + box * 0.5f + 6.f, y + 1.f, x + box, 0, COMMAND_HUD_ITEM,
			bonus ? T("BONUS") : T("LIVES"));
		/* (unlimited: a dash) */
		if (bonus)
			snprintf(line, sizeof(line), "%ld", forge_modes.bonus_score);
		else if (forge_modes.lives == NONE)
			snprintf(line, sizeof(line), "%s", "-");
		else
			snprintf(line, sizeof(line), "%d", forge_modes.lives);
		command_hud_draw_text(COMMAND_HUD_FONT_ITEM, x, y + 1.f, x + box - 6.f, 1,
			bonus ? COMMAND_HUD_ORANGE : forge_modes.lives != NONE && forge_modes.lives <= 1 ? COMMAND_HUD_RED : COMMAND_HUD_LIGHT, line);
		y += FORGE_MODES_HUD_ROW;
		/* (the skulls on) */
		if (forge_modes.skulls_on)
		{
			short bit;
			size_t length = 0;

			line[0] = 0;
			for (bit = 0; bit < (short)NUMBEROF(forge_modes_skull_names); bit++)
			{
				if (forge_modes.skulls_on & (1 << bit))
				{
					snprintf(line + length, sizeof(line) - length, "%s%s", length ? "  " : "", T(forge_modes_skull_names[bit]));
					length = strlen(line);
				}
			}
			command_hud_draw_art(COMMAND_HUD_ART_STRIP, x, y, x + box, y + 16.f, 0.85f);
			command_hud_draw_text(COMMAND_HUD_FONT_SMALL, x + 6.f, y + 2.f, x + box, 0, COMMAND_HUD_ORANGE, line);
			y += FORGE_MODES_HUD_ROW;
		}
		/* (the round's objective, its time) */
		if (objective)
		{
			struct forge_spawn const *spawn = &forge_globals.spawns[forge_modes.objective];

			if (spawn->variant == _forge_mode_objective_hold_zone)
				snprintf(line, sizeof(line), T("HOLD THE ZONE %ld/%d S"), forge_modes.objective_ticks / TICKS_PER_SECOND, spawn->mode.seconds);
			else
				snprintf(line, sizeof(line), T("DEFEND THE ZONE: BREACHED %ld/%d S"), forge_modes.objective_ticks / TICKS_PER_SECOND,
					spawn->mode.seconds);
			command_hud_draw_art(COMMAND_HUD_ART_STRIP, x, y, x + box, y + 16.f, 0.85f);
			command_hud_draw_text(COMMAND_HUD_FONT_ITEM, x + 6.f, y + 1.f, x + box, 0, COMMAND_HUD_ORANGE, line);
			y += FORGE_MODES_HUD_ROW;
		}
		if (round_time)
		{
			long left = (long)forge_modes.firefight.round_minutes * 60 - (game_time_get() - forge_modes.round_start_time) / TICKS_PER_SECOND;

			left = MAX(0, left);
			command_hud_draw_art(COMMAND_HUD_ART_PANEL, x, y, x + box, y + 16.f, 0.8f);
			command_hud_draw_text(COMMAND_HUD_FONT_ITEM, x + 6.f, y + 1.f, x + box, 0, COMMAND_HUD_ITEM, T("ROUND TIME"));
			snprintf(line, sizeof(line), "%ld:%02ld", left / 60, left % 60);
			command_hud_draw_text(COMMAND_HUD_FONT_ITEM, x, y + 1.f, x + box - 6.f, 1, left < 30 ? COMMAND_HUD_RED : COMMAND_HUD_LIGHT,
				line);
		}
	}
	/* (the banner: a round's start, the bonus round's, the end) */
	if (forge_modes.banner[0] && system_milliseconds() < forge_modes.banner_until)
	{
		real top = 96.f;

		command_hud_draw_art(COMMAND_HUD_ART_STRIP, width * 0.5f - 170.f, top, width * 0.5f + 170.f, top + (forge_modes.banner_line[0] ? 50.f : 32.f),
			0.85f);
		command_hud_draw_text(COMMAND_HUD_FONT_TITLE, 0.f, top + 4.f, width, 2, COMMAND_HUD_ORANGE, forge_modes.banner);
		if (forge_modes.banner_line[0])
			command_hud_draw_text(COMMAND_HUD_FONT_ITEM, 0.f, top + 31.f, width, 2, COMMAND_HUD_LIGHT, forge_modes.banner_line);
	}
}

/* a map's built-in Firefight layout (its short name: bloodgulch), NONE
none */
short forge_modes_builtin_of(
	char const *map)
{
	short index;

	for (index = 0; map && index < (short)NUMBEROF(forge_modes_builtins); index++)
	{
		if (!strcmp(forge_modes_builtins[index].map, map))
			return index;
	}
	return NONE;
}

void forge_modes_game_request(
	short mode)
{
	forge_modes.requested = mode == FORGE_MODE_REQUEST_FIREFIGHT ? _forge_mode_firefight : _forge_mode_none;
	if (mode == FORGE_MODE_REQUEST_FIREFIGHT)
		platform_log("forge modes: the next Forge game a FIREFIGHT game");
}

boolean forge_modes_carnage_report(
	struct carnage_report *report,
	boolean in_game)
{
	static struct carnage_report_row players[CARNAGE_REPORT_MAXIMUM_ROWS];
	struct carnage_report_row *team;
	long player_count = 0;
	long index;
	/* (the players' team's colour, as Command's rows: red's, blue's) */
	short color = forge_modes_player_team() == 1 ? 3 : 2;

	if (!forge_modes.reported)
		return FALSE;
	for (index = 0; index < report->row_count && player_count < CARNAGE_REPORT_MAXIMUM_ROWS; index++)
	{
		if (!report->rows[index].team_row)
			players[player_count++] = report->rows[index];
	}
	report->type = CARNAGE_REPORT_TYPE_FIREFIGHT;
	report->teams = TRUE;
	report->team_count = 1;
	report->winning_row = NONE;
	report->winning_team = !in_game && forge_modes.over && forge_modes.won ? 0 : NONE;
	team = &report->rows[0];
	csmemset(team, 0, sizeof(*team));
	team->team_row = TRUE;
	team->team = 0;
	team->color = color;
	team->won = report->winning_team == 0;
	team->player_index = NONE;
	team->machine = NONE;
	team->local_player_index = NONE;
	snprintf(team->name, sizeof(team->name), "%s", T(FORGE_FIREFIGHT_NAME));
	snprintf(team->score, sizeof(team->score), "%ld", forge_modes.score);
	team->score_value = forge_modes.score;
	team->kills = forge_modes.kills;
	team->deaths = forge_modes.deaths;
	team->objective[0] = forge_modes.waves_cleared;
	/* (the bonus rounds' points: Firefight has no bosses) */
	team->objective[1] = forge_modes.bonus_total;
	report->row_count = 1;
	for (index = 0; index < player_count && report->row_count < CARNAGE_REPORT_MAXIMUM_ROWS; index++)
	{
		struct carnage_report_row *player = &report->rows[report->row_count++];
		long absolute = players[index].player_index != NONE ? DATUM_INDEX_TO_ABSOLUTE_INDEX(players[index].player_index) : NONE;

		*player = players[index];
		player->team = 0;
		player->color = color;
		/* (a local game's player has no name of the game's: his profile's,
		as his nameplate shows it - Command's rows' fix) */
		if (player->local && (!player->name[0] || !strcmp(player->name, "-")))
			menus_player_name(player->name, (int)sizeof(player->name));
		player->place = 0;
		player->tied = FALSE;
		player->won = team->won;
		if (absolute >= 0 && absolute < FORGE_SPAWN_PLAYERS)
		{
			player->score_value = forge_modes.player_scores[absolute];
			player->kills = forge_modes.player_kill_counts[absolute];
			player->deaths = forge_modes.player_deaths[absolute];
		}
		snprintf(player->score, sizeof(player->score), "%ld", player->score_value);
		player->objective[0] = forge_modes.waves_cleared;
		player->objective[1] = 0;
		team->assists += player->assists;
		team->local |= player->local;
	}
	report->player_count = player_count;
	return TRUE;
}

/* (forge_command: "mode ...") the tests' and the console's */
static boolean forge_modes_command(
	char const *rest)
{
	if (!strcmp(rest, "firefight"))
	{
		forge_modes_game_request(FORGE_MODE_REQUEST_FIREFIGHT);
		return TRUE;
	}
	if (!strcmp(rest, "where"))
	{
		platform_log("forge modes: %s, state %d, set %d round %d wave %d, score %ld, kills %ld (the player's %ld), deaths %ld, "
			"lives %d, enemies %d, allies %d, pending %d, difficulty %d, skulls on %d", forge_modes.running ? "running" : "not running",
			forge_modes.state, forge_modes.set, forge_modes.round, forge_modes.wave, forge_modes.score, forge_modes.kills,
			forge_modes.player_kills, forge_modes.deaths, forge_modes.lives, forge_modes_alive(FALSE), forge_modes_alive(TRUE),
			forge_modes.pending_count, game_difficulty_level_get(), forge_modes.skulls_on);
		/* (and how far the nearest enemy is from the player) */
		{
			long player_index = forge_modes_local_player();
			long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;
			real nearest = -1.f;
			short index;

			for (index = 0; forge_modes_living_unit(unit_index) && index < forge_modes.unit_count; index++)
			{
				real_point3d a, b;

				if (forge_modes.units[index].ally || !forge_modes_living_unit(forge_modes.units[index].unit_index))
					continue;
				object_get_origin(unit_index, &a);
				object_get_origin(forge_modes.units[index].unit_index, &b);
				if (nearest < 0.f || distance3d(&a, &b) < nearest)
					nearest = distance3d(&a, &b);
			}
			platform_log("forge modes: the nearest enemy %.1f wu from the player", nearest);
		}
		return TRUE;
	}
	/* (tests) "mode clear": every enemy killed as the local player's kill;
	"mode die": the player killed */
	if (!strcmp(rest, "clear") && forge_modes.running)
	{
		short index;
		long player_index = forge_modes_local_player();

		for (index = 0; index < forge_modes.unit_count; index++)
		{
			struct unit_datum *unit = forge_modes_living_unit(forge_modes.units[index].unit_index);

			if (!unit || forge_modes.units[index].ally)
				continue;
			unit->unit.attackers[0].player_index = player_index;
			unit->unit.attackers[0].object_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;
			unit->unit.attackers[0].game_time_stamp = (unsigned long)game_time_get();
			unit->unit.attackers[0].damage_inflicted = 1.f;
			unit_kill(forge_modes.units[index].unit_index);
		}
		platform_log("forge modes: clear: every enemy killed");
		return TRUE;
	}
	/* (tests) "mode hunt <seconds>": the player kept near and facing the
	nearest enemy (forge_modes_hunt); "mode ally [squad]": an allied squad
	of the table (the marines by default) by Pelican to the player's place */
	if (!strncmp(rest, "hunt ", 5) && forge_modes.running)
	{
		forge_modes.hunt_until = game_time_get() + atol(rest + 5) * TICKS_PER_SECOND;
		platform_log("forge modes: hunt for %ld s", atol(rest + 5));
		return TRUE;
	}
	if (!strncmp(rest, "watch ", 6) && forge_modes.running)
	{
		forge_modes.watch_until = game_time_get() + atol(rest + 6) * TICKS_PER_SECOND;
		return TRUE;
	}
	if (!strncmp(rest, "ally", 4) && forge_modes.running)
	{
		long player_index = forge_modes_local_player();
		long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;
		real_point3d at;
		short squad = rest[4] == ' ' ? (short)atoi(rest + 5) : (short)_forge_mode_squad_marines;

		if (!forge_modes_living_unit(unit_index))
			return FALSE;
		object_get_origin(unit_index, &at);
		at.x += 6.f;
		forge_modes_queue((short)(squad % NUMBER_OF_FORGE_MODE_SQUADS), 4, &at, 4.f, TRUE, TRUE, FORGE_MODES_NO_YAW);
		platform_log("forge modes: ally: a squad of %s queued by air to %.2f %.2f %.2f",
			forge_modes_squads[squad % NUMBER_OF_FORGE_MODE_SQUADS].composition, at.x, at.y, at.z);
		return TRUE;
	}
	/* (tests) "mode put x y z": the player there */
	if (!strncmp(rest, "put ", 4))
	{
		real_point3d at;
		long player_index = forge_modes_local_player();
		long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;

		if (sscanf(rest + 4, "%f %f %f", &at.x, &at.y, &at.z) != 3 || !forge_modes_living_unit(unit_index))
			return FALSE;
		object_set_position(unit_index, &at, NULL, NULL);
		object_get(unit_index)->object.translational_velocity = *global_zero_vector3d;
		platform_log("forge modes: put: the player at %.2f %.2f %.2f", at.x, at.y, at.z);
		return TRUE;
	}
#ifndef HALO_RELEASE
	/* (tests) "mode places": the map's own starting locations and netgame
	flags logged (the built-in layouts' places were chosen from them) */
	if (!strcmp(rest, "places"))
	{
		struct scenario *scenario = global_scenario_get();
		long index;

		for (index = 0; scenario && index < scenario->players.count; index++)
		{
			struct player_starting_location const *start =
				TAG_BLOCK_GET_ELEMENT(&scenario->players, index, struct player_starting_location);

			platform_log("forge modes: places: start %ld at %.2f %.2f %.2f facing %.2f team %d types %d %d %d %d", index,
				start->position.x, start->position.y, start->position.z, start->facing, start->team_index, start->game_types[0],
				start->game_types[1], start->game_types[2], start->game_types[3]);
		}
		for (index = 0; scenario && index < scenario->netgame_flags.count; index++)
		{
			struct scenario_netgame_flag const *flag =
				TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);

			platform_log("forge modes: places: flag %ld type %d team %d at %.2f %.2f %.2f facing %.2f", index, flag->type,
				flag->team_index, flag->position.x, flag->position.y, flag->position.z, flag->facing);
		}
		return TRUE;
	}
#endif
	if (!strcmp(rest, "die") && forge_modes.running)
	{
		long player_index = forge_modes_local_player();

		if (player_index != NONE && forge_modes_living_unit(player_get(player_index)->unit_index))
			unit_kill(player_get(player_index)->unit_index);
		return TRUE;
	}
	return FALSE;
}
