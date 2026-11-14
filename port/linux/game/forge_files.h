/*
FORGE_FILES.H

Forge's map variants as files (forge_files.c): the layout file of the
design (docs/superpowers/specs/2026-10-18-forge-design.md, "The layout
file", "Validation"), written and read here, and checked here before
anything reads a byte of it, ours or another player's alike. Pure C with
no game dependency (the unit test and the fuzzer build it alone:
port/vita/tests/forge_files_test.c).

A map variant is <name>.hmv ("Halo map variant") in the Forge folder's
folder of its map (forge_store.h). Binary, little-endian:

	header (64 bytes)
	  magic "HFRG", version u16 (1 to 3), header size u16 (64), flags u32
	  (bit 0: it has netgame spawns), map name char[32] (the scenario's
	  short name, bloodgulch), map hash u32 (the cache file's checksum: a
	  mismatch warns), pack count u16, path count u16, record count u16,
	  meta size u16, payload size u32, checksum u32 (CRC-32 of the payload)
	packs   pack count x 32: { recipe hash u32[2], name char[24] }
	paths   path count x { group tag u32, length u8, path char[length] }
	records record count x 48: { path index u16, kind u8, team i8,
	        position f32[3], yaw pitch roll f32[3], respawn s u16,
	        count u8, game types u16, variant u8, flags u8,
	        AI team u8 (version 2; 0 in version 1), reserved u8[12] }
	meta    meta size bytes: title, author, description and date, each
	        UTF-8 and ended by a zero byte, in that order

Tag paths, never tag indices: a record whose tag the map does not have is
skipped when the variant is applied (forge.c), not refused here.

Version 2 (Oct 18 2026) adds an AI character's settings to its record (a
record of an actor variant, `actv`, only): its AI team (the byte that was
version 1's first reserved one: FORGE_AI_TEAM_*) and its brain (flag bit 1,
FORGE_RECORD_AI_OFF: a frozen, posed character). Version 1 files are read
as ever (that byte and bit must be zero in them); a layout is written as the
lowest version that holds it (version 1 unless a character has an AI
setting), so a variant without them stays readable by older builds, and a
file read writes back as its own version.

Version 3 (Oct 22 2026) adds a character's respawn (flag bit 2,
FORGE_RECORD_AI_RESPAWN, a character's only): dead, it is placed again where
the editor put it after its record's respawn time (1 s or more), by the host
of the game, in Forge and in the variant played. A file of version 2 or 1
holds none (that bit must be zero in them, whatever its respawn time says:
every character of an older variant stays dead, as it did); a layout with a
respawning character is written as version 3, any other as before.

Version 4 (Oct 22 2026) gives meaning to the player spawn record
(_forge_record_player_spawn, no path: the Forge palette's SPAWNING, Halo 3's
Respawn Point and Initial Spawn): its place and yaw, its team (-1 any, 0 red,
1 blue), its game types (FORGE_FILE_GAME_TYPE_* bits, 0 for all; infection's
bit only here) and flag bit 3, FORGE_RECORD_SPAWN_INITIAL (an initial spawn:
a round's first; else a respawn point); respawn, count, variant and AI team
zero. A layout with a player spawn is written as version 4 (whatever else it
holds), any other as before, so a variant without spawn points stays readable
by older builds; a version 4 file has one at least. The game reads player
spawns of a version 4 file only (an older file's, never written by a build,
are skipped as ever).

Version 5 (Oct 30 2026) adds teleporters: a sender (_forge_record_teleporter_in)
and a receiver (_forge_record_teleporter_out), no path, linked by their
channel (the team byte: 0 to FORGE_FILE_MAXIMUM_TELEPORTER_CHANNELS - 1; the
engine's netgame flags' team index, as a map's own teleporters pair theirs),
their yaw (pitch and roll zero) and nothing else; at most
FORGE_FILE_MAXIMUM_TELEPORTERS of them, every sender's channel with a
receiver (a sender that leads nowhere is not one a writer makes). A file of
version 4 or before holds none (the kinds were never written: refused there);
a layout with a teleporter is written as version 5, and a version 5 file has
one at least.

Version 6 (Oct 30 2026) adds game type objects: a record of kind
_forge_record_flag, no path, is one of the engine's netgame flags
(scenario_definitions.h), its type in the variant byte from a fixed list
(FORGE_FILE_FLAG_TYPE_*: a CTF flag's spot, a King of the Hill hill's point,
an oddball's spawn, a race checkpoint) and its index in the team byte (a CTF
flag's team, 0 red or 1 blue; a hill's number, below FORGE_FILE_MAXIMUM_HILLS;
an oddball's ball, below FORGE_FILE_MAXIMUM_ODDBALLS; a checkpoint's order,
below FORGE_FILE_MAXIMUM_RACE_CHECKPOINTS), its yaw and nothing else; at most
FORGE_FILE_MAXIMUM_GAME_OBJECTS of them, a hill's points at most
FORGE_FILE_MAXIMUM_HILL_POINTS. Played, a type the variant has replaces the
map's own flags of that type for the game (a CTF flag a team's), one it has
none of keeps the map's. A file of version 5 or before holds none (the kind
was never written: refused there); a layout with one is written as version
6, and a version 6 file has one at least.

Version 7 (Oct 30 2026) adds an object's physics (Halo 3's Physics: Normal,
Fixed, Phased): flag bit 4, FORGE_RECORD_PHYSICS_FIXED (it never moves: held
where the editor put it), or flag bit 5, FORGE_RECORD_PHYSICS_PHASED (no
collision: players, vehicles and objects pass through it; others' collision
as ever), never both, on an object's record (_forge_record_object) that is
not a character's only. A file of version 6 or before holds neither (the
bits must be zero in them); a layout with one is written as version 7, and
a version 7 file has one at least.

Version 8 (Nov 5 2026) adds the game modes' setup (FIREFIGHT and WARZONE,
port/linux/game/forge_modes.c; the design:
docs/superpowers/specs/2026-11-05-forge-warzone-firefight-design.md): the
mode markers, records of the kinds from _forge_record_ai_zone on (an AI
spawn zone, a boss, a base, a core, a drop point, a Firefight objective), no
path, their 12 reserved bytes their mode fields (struct
forge_file_mode_fields, each kind's own checked: forge_file_mode_record_check;
every other kind's, and every older version's, still zeros); a player spawn's
game types FIREFIGHT's and WARZONE's bits too; and the modes' settings
(header flag bit 1, FORGE_FILE_FLAG_MODE_SETTINGS: a 64-byte block between
the records and the meta). A layout with a mode marker or the settings is
written as version 8, any other as before, so a variant without them stays
readable by older builds (which refuse a version 5 file as another
version); a version 8 file has one at least.

Version 9 (Nov 8 2026, the mode layouts: Ark Editor and Forge) adds a
BOUNDARY: a record of kind _forge_record_boundary, no path, a box on the
map that blocks off an area for a mode's game (a campaign Firefight map's
rest of the mission) - its centre the position, its yaw the angles' first,
its half extents in the mode fields (points: along its facing, seconds:
across it, both quarter world units 1 to FORGE_FILE_BOUNDARY_MAXIMUM_HALF_UNITS;
radius: its half height, quarter world units 1 to 255), its kind in the
variant byte (FORGE_FILE_BOUNDARY_WALL: players and AI are held out of it;
FORGE_FILE_BOUNDARY_KILL: a player inside it dies as in a map's own kill
zone), nothing else; at most FORGE_FILE_MAXIMUM_BOUNDARIES. A file of
version 8 or before holds none (the kind was never written: refused there);
a layout with one is written as version 9, and a version 9 file has one at
least.

Version 10 (Nov 9 2026, the mode layouts step 2) adds the LOBBY CAMERA: a
record of kind _forge_record_camera, no path, where the lobby's view of the
map stands - its position, its yaw the angles' first, its pitch in the mode
fields' radius as degrees up from level plus FORGE_FILE_CAMERA_PITCH_OFFSET
(0 to 2 * FORGE_FILE_CAMERA_PITCH_OFFSET: straight down to straight up),
nothing else; one a layout at most. A layout with one is written as version
10, and a version 10 file has one.

Version 11 (Nov 10 2026, the mode layouts step 4: Ark Editor's BLANK CANVAS)
adds the CANVAS: which of the map's OWN placed things the layout takes away
while it is played or edited - the scenario's scenery (a Halo 2 level's
crates with it), bipeds, vehicles, weapons, equipment, machines, controls,
light fixtures, sound scenery and a multiplayer map's item spawns (netgame
equipment). Nothing of the map is in the file and the map's file is never
changed: the placements are named by their category and their index in the
scenario's own blocks, and are simply not made (forge_canvas.inc) in a game
of the layout. Header flag bit 2 (FORGE_FILE_FLAG_CANVAS): a block between
the mode settings and the meta - its version u8 (FORGE_FILE_CANVAS_VERSION),
a zero, the categories taken away whole u16 (a bit each, _forge_canvas_*),
the entries' count u16 (at most FORGE_FILE_MAXIMUM_CANVAS_ENTRIES), a zero
u16; then the entries, 4 bytes each: category u8, kept u8, index u16 (at
most FORGE_FILE_CANVAS_MAXIMUM_INDEX). An entry of a category taken away
whole is one placement KEPT (restored one by one; kept 1); an entry of any
other category is one placement taken away (kept 0). The entries ascend
strictly (category, then index): one way to write a canvas. A layout with a
canvas is written as version 11; a version 11 file has one that takes
something away or keeps something.
*/

#ifndef __FORGE_FILES_H
#define __FORGE_FILES_H

/* ---------- constants */

#define FORGE_FILE_EXTENSION ".hmv"
/* the version written when a layout needs it, and the oldest read */
#define FORGE_FILE_VERSION 11
#define FORGE_FILE_FIRST_VERSION 1
#define FORGE_FILE_HEADER_SIZE 64
#define FORGE_FILE_MAXIMUM_SIZE 65536
#define FORGE_FILE_MAXIMUM_PATHS 256
#define FORGE_FILE_MAXIMUM_PACKS 8
/* the Forge budget's objects (forge.h FORGE_MAXIMUM_OBJECTS) */
#define FORGE_FILE_MAXIMUM_RECORDS 256
#define FORGE_FILE_MAXIMUM_META 2048
#define FORGE_FILE_PATH_MAXIMUM_LENGTH 127
#define FORGE_FILE_PACK_SIZE 32
#define FORGE_FILE_PACK_NAME_SIZE 24
#define FORGE_FILE_RECORD_SIZE 48
/* (version 1's 13 reserved bytes: version 2's AI team, then 12) */
#define FORGE_FILE_RECORD_RESERVED_SIZE 12
/* the texts' room, each one's terminator included */
#define FORGE_FILE_MAP_NAME_SIZE 32
#define FORGE_FILE_TITLE_SIZE 32
#define FORGE_FILE_AUTHOR_SIZE 32
#define FORGE_FILE_DESCRIPTION_SIZE 128
/* "YYYY-MM-DD HH:MM", or empty */
#define FORGE_FILE_DATE_SIZE 17
/* a file name: <stem>.hmv, the stem [a-z0-9_-] */
#define FORGE_FILE_NAME_SIZE 40
#define FORGE_FILE_STEM_MAXIMUM_LENGTH 28
/* the flying camera's and the game's world: every coordinate within it */
#define FORGE_FILE_MAXIMUM_COORDINATE 5000.f
#define FORGE_FILE_MAXIMUM_RESPAWN_SECONDS 600
#define FORGE_FILE_MAXIMUM_COUNT 16
#define FORGE_FILE_MAXIMUM_TEAM 7
#define FORGE_FILE_MAXIMUM_VARIANT 15
/* the game types (ctf, slayer, oddball, king of the hill, race; version 4's
player spawns: infection too), a bit each in the engine's order */
#define FORGE_FILE_GAME_TYPES_MASK 0x1F
#define FORGE_FILE_GAME_TYPE_CTF (1 << 0)
#define FORGE_FILE_GAME_TYPE_SLAYER (1 << 1)
#define FORGE_FILE_GAME_TYPE_ODDBALL (1 << 2)
#define FORGE_FILE_GAME_TYPE_KING (1 << 3)
#define FORGE_FILE_GAME_TYPE_RACE (1 << 4)
#define FORGE_FILE_GAME_TYPE_INFECTION (1 << 5)
#define FORGE_FILE_SPAWN_GAME_TYPES_MASK 0x3F
/* (version 8) the game modes: a player spawn's game types (with the six
above), and a mode marker's (0: both) */
#define FORGE_FILE_GAME_TYPE_FIREFIGHT (1 << 6)
#define FORGE_FILE_GAME_TYPE_WARZONE (1 << 7)
#define FORGE_FILE_MODE_GAME_TYPES_MASK (FORGE_FILE_GAME_TYPE_FIREFIGHT | FORGE_FILE_GAME_TYPE_WARZONE)
#define FORGE_FILE_SPAWN_GAME_TYPES_MASK_8 0xFF
/* the mode settings block's size (version 8) */
#define FORGE_FILE_MODE_SETTINGS_SIZE 64
#define FORGE_FILE_MODE_SETTINGS_VERSION 1
/* a player spawn's teams: any (-1), red, blue */
#define FORGE_FILE_MAXIMUM_SPAWN_TEAM 1
/* (version 5) teleporters: their channels (a record's team), the most of
them in a layout */
#define FORGE_FILE_MAXIMUM_TELEPORTER_CHANNELS 16
#define FORGE_FILE_MAXIMUM_TELEPORTERS 32
/* (version 6) game type objects: the netgame flag types a record may be
(the engine's _netgame_flag_*, scenario_definitions.h), each one's indices,
the most of them in a layout and of one hill's points (the engine's
MAXIMUM_HILL_POINTS) */
#define FORGE_FILE_FLAG_TYPE_CTF 0
#define FORGE_FILE_FLAG_TYPE_ODDBALL 2
#define FORGE_FILE_FLAG_TYPE_RACE 3
#define FORGE_FILE_FLAG_TYPE_HILL 8
#define FORGE_FILE_MAXIMUM_CTF_TEAMS 2
#define FORGE_FILE_MAXIMUM_HILLS 16
#define FORGE_FILE_MAXIMUM_ODDBALLS 16
#define FORGE_FILE_MAXIMUM_RACE_CHECKPOINTS 32
#define FORGE_FILE_MAXIMUM_GAME_OBJECTS 64
#define FORGE_FILE_MAXIMUM_HILL_POINTS 12
/* a record's path index when its kind names no object */
#define FORGE_FILE_NO_PATH 0xFFFF

/* the header's flags */
enum
{
	FORGE_FILE_FLAG_NETGAME_SPAWNS = 1 << 0,
	/* (version 8) the mode settings block follows the records */
	FORGE_FILE_FLAG_MODE_SETTINGS = 1 << 1,
	/* (version 11) the canvas block follows the mode settings */
	FORGE_FILE_FLAG_CANVAS = 1 << 2,
	FORGE_FILE_FLAGS_VERSION_7 = FORGE_FILE_FLAG_NETGAME_SPAWNS,
	FORGE_FILE_FLAGS_VERSION_10 = FORGE_FILE_FLAG_NETGAME_SPAWNS | FORGE_FILE_FLAG_MODE_SETTINGS,
	FORGE_FILE_FLAGS_KNOWN = FORGE_FILE_FLAG_NETGAME_SPAWNS | FORGE_FILE_FLAG_MODE_SETTINGS | FORGE_FILE_FLAG_CANVAS,
};

/* a record's flags */
enum
{
	/* placed when the variant starts (else only a netgame's spawn places it) */
	FORGE_RECORD_PLACE_AT_START = 1 << 0,
	/* (version 2, a character's only) its AI's brain off: it stands as
	placed, thinks, moves and fights not (the engine's ai_braindead) */
	FORGE_RECORD_AI_OFF = 1 << 1,
	/* (version 3, a character's only) dead, it is placed again after its
	respawn time */
	FORGE_RECORD_AI_RESPAWN = 1 << 2,
	/* (version 4, a player spawn's only) an initial spawn (a round's first
	spawns), else a respawn point */
	FORGE_RECORD_SPAWN_INITIAL = 1 << 3,
	/* (version 7, an object's that is no character's only) its physics:
	fixed (never moves), or phased (no collision); never both */
	FORGE_RECORD_PHYSICS_FIXED = 1 << 4,
	FORGE_RECORD_PHYSICS_PHASED = 1 << 5,
	FORGE_RECORD_PHYSICS_MASK = FORGE_RECORD_PHYSICS_FIXED | FORGE_RECORD_PHYSICS_PHASED,
	FORGE_RECORD_FLAGS_VERSION_1 = FORGE_RECORD_PLACE_AT_START,
	FORGE_RECORD_FLAGS_VERSION_2 = FORGE_RECORD_PLACE_AT_START | FORGE_RECORD_AI_OFF,
	FORGE_RECORD_FLAGS_VERSION_3 = FORGE_RECORD_PLACE_AT_START | FORGE_RECORD_AI_OFF | FORGE_RECORD_AI_RESPAWN,
	FORGE_RECORD_FLAGS_VERSION_4 = FORGE_RECORD_FLAGS_VERSION_3 | FORGE_RECORD_SPAWN_INITIAL,
	FORGE_RECORD_FLAGS_KNOWN = FORGE_RECORD_FLAGS_VERSION_4 | FORGE_RECORD_PHYSICS_MASK,
};

/* (version 2) a character's AI team: whom it fights. The engine's teams
(game.h _game_team_*), and the player's own */
enum
{
	/* the character's own (its unit's default team: the Covenant's for an
	elite, the humans' for a marine) */
	FORGE_AI_TEAM_DEFAULT = 0,
	/* the player's own team: fights for the player, never against */
	FORGE_AI_TEAM_PLAYER,
	/* the engine's AI teams: each the enemy of the others and of the
	player (a campaign map's script may ally the humans with the player) */
	FORGE_AI_TEAM_HUMAN,
	FORGE_AI_TEAM_COVENANT,
	FORGE_AI_TEAM_FLOOD,
	FORGE_AI_TEAM_SENTINEL,
	NUMBER_OF_FORGE_AI_TEAMS
};

/* a record's kind */
enum
{
	_forge_record_object = 0,
	_forge_record_squad,
	_forge_record_player_spawn,
	_forge_record_weapon_spawn,
	_forge_record_vehicle_spawn,
	/* (version 6) a game type object: variant its FORGE_FILE_FLAG_TYPE_*,
	team its index */
	_forge_record_flag,
	/* (never written: refused) */
	_forge_record_hill,
	/* (version 5) a teleporter's sender and its receiver: team its channel */
	_forge_record_teleporter_in,
	_forge_record_teleporter_out,
	_forge_record_kill_zone,
	/* (version 8: the mode markers, no path, their mode fields) */
	_forge_record_ai_zone,
	_forge_record_boss,
	_forge_record_base,
	_forge_record_core,
	_forge_record_drop_point,
	_forge_record_ff_objective,
	/* (Firefight round 3, Nov 8 2026) a BONUS ROUND marker: where the
	bonus round's rush comes from and its settings */
	_forge_record_bonus_round,
	/* (version 9) a BOUNDARY box: its half extents in the mode fields, its
	kind in the variant byte */
	_forge_record_boundary,
	/* (version 10) the lobby camera */
	_forge_record_camera,
	NUMBER_OF_FORGE_RECORD_KINDS,
	FIRST_FORGE_MODE_RECORD_KIND = _forge_record_ai_zone
};

/* (version 8) the mode markers' ids, saved in files: stable, new ones at the
end (forge_modes.c's tables are in this order) */
enum
{
	/* an AI spawn zone's squad (its record's variant) */
	_forge_mode_squad_marines = 0,
	_forge_mode_squad_armored_marines,
	_forge_mode_squad_grunts,
	_forge_mode_squad_grunts_jackals,
	_forge_mode_squad_elite_grunts,
	_forge_mode_squad_elites,
	_forge_mode_squad_stealth_elites,
	_forge_mode_squad_hunters,
	_forge_mode_squad_flood_combat,
	_forge_mode_squad_flood_pod,
	_forge_mode_squad_sentinels,
	_forge_mode_squad_shade_crew,
	/* (Firefight round 3) an Elite commander with his majors: a heavy
	wave's */
	_forge_mode_squad_elite_commanders,
	NUMBER_OF_FORGE_MODE_SQUADS
};

enum
{
	/* a boss's kind (its record's variant) */
	_forge_mode_boss_elite_commander = 0,
	_forge_mode_boss_elite_major,
	_forge_mode_boss_stealth_elite,
	_forge_mode_boss_hunter,
	_forge_mode_boss_flood_carrier,
	_forge_mode_boss_sentinel_major,
	NUMBER_OF_FORGE_MODE_BOSSES
};

/* a boss's name: one of forge_modes.c's own list (never typed text) */
#define FORGE_FILE_MODE_BOSS_NAMES 16

enum
{
	/* a drop point's ship (its record's variant) */
	_forge_mode_ship_by_species = 0,
	_forge_mode_ship_pelican,
	_forge_mode_ship_covenant_dropship,
	NUMBER_OF_FORGE_MODE_SHIPS
};

enum
{
	/* a Firefight objective's kind (its record's variant) */
	_forge_mode_objective_defend_zone = 0,
	_forge_mode_objective_hold_zone,
	_forge_mode_objective_defend_object,
	_forge_mode_objective_kill_boss,
	NUMBER_OF_FORGE_MODE_OBJECTIVES
};

/* a mode marker's team (its record's team): an AI spawn zone's or a boss's
side, a base's owner at the start, a core's team, a drop point's side */
enum
{
	/* (a zone, a boss, a drop point: hostile to everyone - its species' own
	AI team -; a base: no one's; a drop point: any side's) */
	FORGE_MODE_TEAM_NONE = -1,
	FORGE_MODE_TEAM_RED = 0,
	FORGE_MODE_TEAM_BLUE = 1,
	/* (a drop point's only: the hostile AI's, apart from any side's) */
	FORGE_MODE_TEAM_HOSTILE = 2,
};

/* a mode marker's flags (its mode fields' flags) */
enum
{
	/* (a base) a team's home base: taken only once that team's other bases
	are lost; a core's shield is down while the other team holds it */
	FORGE_MODE_FLAG_HOME = 1 << 0,
	/* (a zone, a boss) brought by dropship to the nearest drop point, not
	made on the ground */
	FORGE_MODE_FLAG_DROPSHIP = 1 << 1,
	/* (a boss, Warzone) back after its respawn time once killed */
	FORGE_MODE_FLAG_RETURNS = 1 << 2,
	/* (a bonus round marker) the bonus round off */
	FORGE_MODE_FLAG_OFF = 1 << 3,
	FORGE_MODE_FLAGS_KNOWN = FORGE_MODE_FLAG_HOME | FORGE_MODE_FLAG_DROPSHIP | FORGE_MODE_FLAG_RETURNS | FORGE_MODE_FLAG_OFF,
};

/* the Firefight skulls (the settings' skull list): Halo 2's, by its names,
applied to Halo CE's AI (the owner, Nov 5) */
enum
{
	FORGE_MODE_SKULL_MYTHIC = 1 << 0,
	FORGE_MODE_SKULL_CATCH = 1 << 1,
	FORGE_MODE_SKULL_BLACK_EYE = 1 << 2,
	FORGE_MODE_SKULL_FAMINE = 1 << 3,
	FORGE_MODE_SKULL_THUNDERSTORM = 1 << 4,
	FORGE_MODE_SKULL_IRON = 1 << 5,
	FORGE_MODE_SKULL_BLIND = 1 << 6,
	FORGE_MODE_SKULLS_KNOWN = 0x7F,
};

/* the mode markers' ranges (forge_file_mode_record_check) */
#define FORGE_FILE_MAXIMUM_MODE_MARKERS 64
#define FORGE_FILE_MAXIMUM_BASES 5
#define FORGE_FILE_MAXIMUM_DROP_POINTS 8
/* a radius in quarter world units */
#define FORGE_FILE_MODE_RADIUS_UNITS 4
/* the rounds' and the waves' masks: rounds 1 to 5; waves 1 to 5 and the
heavy wave (0: all). The heavy wave is each round's last (Firefight round 3:
Reach's, heavier squads; before it the set's last wave, its bosses'): a
zone of that bit alone comes on it only, HEAVY WAVE ONLY */
#define FORGE_FILE_MODE_ROUNDS_MASK 0x1F
#define FORGE_FILE_MODE_WAVES_MASK 0x3F
#define FORGE_FILE_MODE_HEAVY_WAVE (1 << 5)
#define FORGE_FILE_MODE_BOSS_WAVE FORGE_FILE_MODE_HEAVY_WAVE
/* a bonus round marker's seconds and points a kill (Firefight round 3) */
#define FORGE_FILE_BONUS_MINIMUM_SECONDS 30
#define FORGE_FILE_BONUS_MAXIMUM_SECONDS 120
/* (version 9) a boundary's kinds (its record's variant), its half extents'
range (quarter world units: 500 world units at most along and across, its
half height a byte's), and the most of them in a layout */
#define FORGE_FILE_BOUNDARY_WALL 0
#define FORGE_FILE_BOUNDARY_KILL 1
#define NUMBER_OF_FORGE_FILE_BOUNDARY_KINDS 2
#define FORGE_FILE_BOUNDARY_MAXIMUM_HALF_UNITS 2000
#define FORGE_FILE_BOUNDARY_MAXIMUM_HEIGHT_UNITS 255
#define FORGE_FILE_MAXIMUM_BOUNDARIES 32
/* (version 10) the lobby camera's pitch, degrees up from level, kept as
pitch + this in its radius byte; one a layout */
#define FORGE_FILE_CAMERA_PITCH_OFFSET 90
#define FORGE_FILE_MAXIMUM_CAMERAS 1
/* (version 11) the canvas: the categories of the map's own placements (ids
saved in files: stable, new ones at the end), its block's sizes and ranges */
enum
{
	_forge_canvas_scenery = 0,
	_forge_canvas_bipeds,
	_forge_canvas_vehicles,
	_forge_canvas_weapons,
	_forge_canvas_equipment,
	_forge_canvas_machines,
	_forge_canvas_controls,
	_forge_canvas_light_fixtures,
	_forge_canvas_sound_scenery,
	/* a multiplayer map's item spawns (the scenario's netgame equipment) */
	_forge_canvas_items,
	NUMBER_OF_FORGE_CANVAS_CATEGORIES
};
#define FORGE_FILE_CANVAS_VERSION 1
#define FORGE_FILE_CANVAS_HEADER_SIZE 8
#define FORGE_FILE_CANVAS_ENTRY_SIZE 4
#define FORGE_FILE_MAXIMUM_CANVAS_ENTRIES 1024
#define FORGE_FILE_CANVAS_MAXIMUM_INDEX 8191
#define FORGE_FILE_CANVAS_CATEGORIES_MASK ((1 << NUMBER_OF_FORGE_CANVAS_CATEGORIES) - 1)
/* what BLANK CANVAS takes away unless told otherwise: all but the doors and
machines, the controls, the light fixtures and the sound scenery */
#define FORGE_FILE_CANVAS_DEFAULT ((1 << _forge_canvas_scenery) | (1 << _forge_canvas_bipeds) | (1 << _forge_canvas_vehicles) | \
	(1 << _forge_canvas_weapons) | (1 << _forge_canvas_equipment) | (1 << _forge_canvas_items))

/* why a file was refused (forge_file_decode), in the order checked */
enum
{
	_forge_file_ok = 0,
	_forge_file_too_small,
	_forge_file_too_large,
	_forge_file_bad_magic,
	_forge_file_bad_version,
	_forge_file_bad_header_size,
	_forge_file_bad_flags,
	_forge_file_bad_map_name,
	_forge_file_wrong_map,
	_forge_file_too_many,
	_forge_file_bad_payload_size,
	_forge_file_bad_checksum,
	_forge_file_bad_pack,
	_forge_file_bad_path,
	_forge_file_bad_group,
	_forge_file_duplicate_path,
	_forge_file_bad_record,
	_forge_file_bad_meta,
	_forge_file_no_room,
	NUMBER_OF_FORGE_FILE_RESULTS
};

/* ---------- structures */

struct forge_file_pack
{
	unsigned int recipe[2];
	char name[FORGE_FILE_PACK_NAME_SIZE + 1];
};

struct forge_file_path
{
	unsigned int group_tag;
	/* (its length, at most FORGE_FILE_PATH_MAXIMUM_LENGTH) */
	char path[FORGE_FILE_PATH_MAXIMUM_LENGTH + 1];
};

/* (version 8) a mode marker's fields, its record's 12 reserved bytes in
this order: radius u8 (quarter world units), rounds u8, waves u8, points
u16, toughness u8, seconds u16, start minute u8, index u8, flags u8, a zero */
struct forge_file_mode_fields
{
	unsigned char radius;
	/* (FORGE_FILE_MODE_ROUNDS_MASK, FORGE_FILE_MODE_WAVES_MASK; 0 all) */
	unsigned char rounds;
	unsigned char waves;
	/* a boss's points, a core's or a defended object's hull; a bonus round's
	points a kill */
	unsigned short points;
	/* a boss's body and shield, times this (1 to 8) */
	unsigned char toughness;
	/* a base's capture time, a Firefight objective's time, a bonus round's
	length */
	unsigned short seconds;
	/* (Warzone) the minute a zone's waves or a boss begin */
	unsigned char start_minute;
	/* a boss's name (below FORGE_FILE_MODE_BOSS_NAMES); a base's letter (0
	to FORGE_FILE_MAXIMUM_BASES - 1, A to E, each base its own); an AI spawn
	zone's base it defends (0 none, else its letter + 1: its squads hold
	that base, which cannot be taken while one of them lives) */
	unsigned char index;
	/* FORGE_MODE_FLAG_* */
	unsigned char flags;
};

struct forge_file_record
{
	unsigned short path_index;
	unsigned char kind;
	signed char team;
	float position[3];
	/* yaw, pitch, roll, radians, within +-pi */
	float angles[3];
	unsigned short respawn_seconds;
	unsigned char count;
	unsigned short game_types;
	unsigned char variant;
	unsigned char flags;
	/* (version 2, a character's only: FORGE_AI_TEAM_*; else 0) */
	unsigned char ai_team;
	/* (version 8, a mode marker's only; else zeros) */
	struct forge_file_mode_fields mode;
};

/* (version 8) the Firefight settings (the design's section 3), each a byte
but the point limit */
struct forge_file_firefight_settings
{
	/* 0 endless, 1 to 5 (3) */
	unsigned char sets;
	/* 1 to 5 (3) */
	unsigned char rounds;
	/* 1 to 5 (5) */
	unsigned char waves;
	/* the team's lives: 0 unlimited, 1 to 30 (7) */
	unsigned char lives;
	/* given back for each round cleared, 0 to 3 (1) */
	unsigned char lives_per_round;
	/* easy, normal, heroic, legendary (normal: 1) */
	unsigned char difficulty;
	/* off, each set, each round (each set: 1) */
	unsigned char ramp;
	/* off, one more each set, all from the start (each set: 1) */
	unsigned char skull_mode;
	/* 0 off, 3 to 15 (0) */
	unsigned char round_minutes;
	/* round time run out: 0 the game over, 1 a life lost (0) */
	unsigned char round_time_rule;
	/* the players' allied AI on (1) */
	unsigned char allies;
	/* the map's weapons put back between rounds (1) */
	unsigned char resupply;
};

/* (version 8) the Warzone settings */
struct forge_file_warzone_settings
{
	/* 250 to 2000 (1000) */
	unsigned short point_limit;
	/* the cores on (1): destroyed, the other team wins early */
	unsigned char cores;
	/* points: a player killed 0 to 100 (5), a base captured 0 to 100 (50),
	each base held each 5 s 0 to 100 (1) */
	unsigned char player_kill_points;
	unsigned char base_capture_points;
	unsigned char base_hold_points;
	/* the first wave after 0 to 300 s (30), then every 30 to 300 s (90) */
	unsigned short first_wave_seconds;
	unsigned short wave_seconds;
	/* the AI's shares: hostile 0 to 32 (16), each team's allies 0 to 16 (8);
	32 in all at most */
	unsigned char hostile_ai;
	unsigned char red_ai;
	unsigned char blue_ai;
	/* 0 none, to 60 (20) */
	unsigned char time_limit_minutes;
};

struct forge_file_mode_settings
{
	struct forge_file_firefight_settings firefight;
	struct forge_file_warzone_settings warzone;
	/* the Firefight skulls the map maker chose (FORGE_MODE_SKULL_*) */
	unsigned short skulls;
};

/* (version 11) the canvas: the categories taken away whole, and the single
placements kept of those or taken away of the others */
struct forge_file_canvas_entry
{
	unsigned char category;
	unsigned char kept;
	unsigned short index;
};
struct forge_file_canvas
{
	unsigned short categories;
	short entry_count;
	struct forge_file_canvas_entry entries[FORGE_FILE_MAXIMUM_CANVAS_ENTRIES];
};
/* a map variant, as read or to be written */
struct forge_file_layout
{
	/* the file's version: as read; to write, 0 for the lowest that holds
	the layout (forge_file_layout_version) */
	unsigned short version;
	unsigned int flags;
	char map_name[FORGE_FILE_MAP_NAME_SIZE];
	unsigned int map_hash;
	short pack_count;
	short path_count;
	short record_count;
	struct forge_file_pack packs[FORGE_FILE_MAXIMUM_PACKS];
	struct forge_file_path paths[FORGE_FILE_MAXIMUM_PATHS];
	struct forge_file_record records[FORGE_FILE_MAXIMUM_RECORDS];
	char title[FORGE_FILE_TITLE_SIZE];
	char author[FORGE_FILE_AUTHOR_SIZE];
	char description[FORGE_FILE_DESCRIPTION_SIZE];
	char date[FORGE_FILE_DATE_SIZE];
	/* (version 8, with FORGE_FILE_FLAG_MODE_SETTINGS; else zeros) */
	struct forge_file_mode_settings modes;
	/* (version 11, with FORGE_FILE_FLAG_CANVAS; else zeros) */
	struct forge_file_canvas canvas;
};

/* ---------- prototypes/FORGE_FILES.C */

/* the CRC-32 (IEEE) of size bytes, from crc (0 to start) */
unsigned int forge_file_crc32(
	unsigned int crc,
	unsigned char const *bytes,
	int size);

/* a file read: every check of the design, in order, before anything is
taken from it (expected_map: the map it must be of, NULL for any). The
layout is filled only when it passes; _forge_file_ok, else why not (and,
in why, the words of it for the log, when why is not NULL) */
int forge_file_decode(
	unsigned char const *bytes,
	int size,
	char const *expected_map,
	struct forge_file_layout *layout,
	char *why,
	int why_size);

/* a layout written: checked as a file read is (a layout that would not be
read back is not written); the bytes written, or -1 (why in result when
not NULL) */
int forge_file_encode(
	struct forge_file_layout const *layout,
	unsigned char *bytes,
	int capacity,
	int *result);

/* a layout's own checks (the ones a file read gets after its framing):
_forge_file_ok or why not */
int forge_file_layout_check(
	struct forge_file_layout const *layout,
	char const *expected_map);

/* the version a layout is written as: its own when set, else the lowest
that holds it (11 with a canvas, 10 with a lobby camera, 9 with a boundary, 8 with a mode marker or the mode settings, 7 with an object's physics, 6 with a game type object, 5 with a teleporter, 4 with a player spawn, 3 with a
respawning character, 2 when a record has an AI setting) */
int forge_file_layout_version(
	struct forge_file_layout const *layout);

/* (version 8) the mode settings' defaults (the design's), and whether
settings are all within their ranges */
void forge_file_mode_settings_default(
	struct forge_file_mode_settings *settings);
int forge_file_mode_settings_valid(
	struct forge_file_mode_settings const *settings);

/* whether a record's kind is a mode marker's (version 8) */
int forge_file_kind_is_mode_marker(
	int kind);

/* a result's words (English, for the log) */
char const *forge_file_result_name(
	int result);

/* whether a group tag is one Forge places (vehi weap eqip scen bipd mach
ctrl lifi actv) */
int forge_file_group_allowed(
	unsigned int group_tag);

/* whether a tag path is one a file may name: 1 to 127 characters of
[a-z0-9_ \-.], no "..", no empty part, not starting or ending with \ */
int forge_file_path_valid(
	char const *path,
	int length);

/* whether a map's short name is [a-z0-9_], 1 to 31 characters */
int forge_file_map_name_valid(
	char const *name);

/* whether a text is valid UTF-8 of at most maximum bytes with no control
character */
int forge_file_text_valid(
	char const *text,
	int maximum);

/* whether a file name is a map variant's: <stem>.hmv, the stem 1 to 28 of
[a-z0-9_-]; nothing else is ever opened in the Forge folder */
int forge_file_name_valid(
	char const *file_name);

/* a file name made from a title (lower case letters and digits, the rest
'_', at most 24), with a number after it when number > 1: "gulch_2.hmv" */
void forge_file_name_from_title(
	char const *title,
	int number,
	char *file_name,
	int size);

/* a received variant's file name, made by us from its hash, never from the
sender: "r0123abcd.hmv" */
void forge_file_name_received(
	unsigned int hash,
	char *file_name,
	int size);

/* (version 6) a game type object's index limit for its flag type
(FORGE_FILE_FLAG_TYPE_*): the indices below it; 0 for a type not in the
list */
int forge_file_flag_type_indices(
	int flag_type);

/* an angle in radians brought within +-pi (the writer's) */
float forge_file_angle_reduce(
	float angle);

#endif
