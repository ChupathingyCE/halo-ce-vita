/*
COMMAND.C

Command mode, the commander's part (the design:
docs/superpowers/specs/2026-11-05-command-mode-design.md): a player above a
Halo CE multiplayer map ordering the side's AI squads, and dropping into
any of the side's units to play it first-person; the AI takes the unit
back when the player leaves it. Nothing changes until it is started (the
console's "command on"); the game type around it (the sides, the HQ and
its upgrades, the economy, the objectives) comes after this prototype.

The commander: the player's own unit stays where it stood, hidden and
unhurt (as Forge's monitor leaves the Spartan, forge.c), its controls not
read (the director's Forge block, director.c), and the director flies the
flying camera above the ground, pitched down at a point on it, the focus:
the screen's centre, where the camera's line meets the level, is the
cursor (the camera's line passes through the focus). The controls (the
docs/superpowers/specs/2026-11-05-command-mode-design.md, section 5, has
them all; the Vita's in brackets, the desktop keyboard's in braces):
- Back + D-pad up [Select + D-pad up] {Tab} (Back + the right stick's click
  too, Forge's flip): into his own Spartan, wherever it stands, and back
  above the map (xinput_sdl.c keeps the combo from the game; forge.c leaves
  the monitor alone while command mode runs);
- left stick {W A S D, the screen's edges}: pan; right stick {Q E, the
  wheel}: turn about the focus, zoom;
- A [Cross] {left click, a drag's box}: choose the squad at the cursor,
  held a growing circle of them; LB [D-pad right] {Ctrl + A}: all; RB
  [D-pad left] {B}: the camera to the base, twice to the last alert;
- RT [R] {right click}: order the squads chosen there: MOVE, or ATTACK an
  enemy there; X [Square] {H}: HOLD where they stand.

A squad is this file's own table: a side, up to COMMAND_SQUAD_SIZE units,
each a unit with a free actor of its actor variant (ai_attach_free, as
Forge places a character), the order and its point. No AI of its own: an
order steers the engine's actors as Halo 2's command scripts do
(h2_ai.c's h2_ai_actor_orders): each tick, after the actor's action has
given its orders (actors.c, command_actor_orders), actor_move_to_point to
the member's place by the order's point (a ring about it, so that they do
not stand on one another) - the AI's own path searches over the level's
walkable surfaces, which every multiplayer map carries. MOVE: until there
(COMMAND_ARRIVED of its place), fighting as it goes; then HOLD there. ATTACK:
MOVE at the target's place, seen by the squad at once, a member with a
target of its own fighting it. HOLD: the actor's own behaviour, a member
further than COMMAND_HOLD_LEASH from its place walked back.

First person (the owner, Nov 5): the commander's own Spartan alone - an AI's
unit is never played -: the combo takes him into it wherever it stands (its
spawn his base's) and back above the map; it stands where he left it while
he commands, seen and hurt as any Spartan, and dead respawns at the base.

Threads: as Forge's. command_update runs on the main thread after the
cameras update and before the tick thread starts (main.c); the console's
commands run at the top of the main loop; command_actor_orders on the tick
reads the table they wrote; command_draw reads only what command_update
wrote, and the objects' places.

The console's commands (command_command; the tests use them through
HALO_TEST_COMMANDS):
  command [on|off]           the commander's view, toggled or set
  command spawn red|blue <count> <name>[:n,name:n...] [at x y z]
                             a squad of that many of the first actor variant
                             with name in its tag's name, at the cursor (or
                             there), on that side
  command select <n>         squad n chosen (its number on the HUD)
  command move [x y z]       the squad chosen ordered to the cursor (or there)
  command attack [n]         the squad chosen ordered at the unit under the
                             cursor (or at enemy squad n's first unit)
  command hold               the squad chosen holds where it is
                             (a move onto the enemy flag is the Spartan's
                             flag run)
  command objective territories|flag|both, command score N, command time M
                             the next Command game's objectives, score
                             limit, time limit (minutes; 0: none)
  command flag               the flags and the scores in the log
  command end red|blue|draw  the game over now
  command put x y z          the Spartan (or the unit he is in) put there
  command possess            into his Spartan, first person (the combo)
  command leave              back above the map
  command camera x y yaw distance [pitch]  the focus and the camera put
                             there (degrees)
  command press <buttons> [frames]  the commander's buttons pressed (a b x y
                             black white lt rt up down left right back combo,
                             joined by +), for frames frames (2 by default)
  command stick lx ly rx ry [frames]  the sticks held (-1 to 1)
  command where              every squad in the log: its order, each
                             unit's place, its distance to its place, its
                             actor's state, who has it
Each logs what it did ("command: ...").
*/

#include "cseries.h"
#include "real_math.h"
#include "ai/ai.h"
#include "ai/ai_script.h"
#include "ai/actor_definitions.h"
#include "ai/actors.h"
#include "ai/props.h"
#include "ai/path.h"
#include "cache/cache_files.h"
#include "camera/observer.h"
#include "game/game.h"
#include "game/game_globals.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "hs/object_lists.h"
#include "input/input.h"
#include "items/items.h"
#include "items/weapon_definitions.h"
#include "items/weapons.h"
#include "main/main.h"
#include "objects/damage.h"
#include "objects/objects.h"
#include "objects/object_types.h"
#include "physics/collisions.h"
#include "rasterizer/rasterizer.h"
#include "bitmaps/bitmap_group.h"
#include "cache/texture_cache.h"
#include "interface/hud_draw.h"
#include "rasterizer/rasterizer_geometry.h"
#include "rasterizer/xbox/rasterizer_xbox_draw_primitives.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "render/render_cameras_internal.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "units/units.h"
#include "units/dialogue_definitions.h"
#include "ai/ai_communication.h"
#include "units/unit_definitions.h"
#include "units/vehicle_definitions.h"
#include "units/vehicles.h"

#include "command.h"
#include "command_squads.h"
#include "carnage_report.h"
#include "command_hud.h"
#include "forge.h"
#include "scene_packs.h"
#include "command_net_protocol.h"
#include "network_distributed.h"
#include "network_anticheat.h"
/* (the desktop's button icons: display.button_icons) */
#if !defined(HALO_VITA) && !defined(HALO_ANDROID)
#define COMMAND_BUTTON_ICONS 1
#include "halo_button_icons.h"
#endif
#include "halo_menus.h"
#ifdef HALO_BUTTON_GLYPHS
#include "../../vita/include/vita_controls.h"
#endif
#include "halo_ui_pointer.h"
#include "../src/lang.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);
int halo_linux_mouse_look(short gamepad_index, float *yaw, float *pitch);
unsigned long halo_linux_forge_keys(float *wheel_notches);
long hud_get_font_index(void);
unsigned long system_milliseconds(void);
unsigned long long vita_host_time_us(void);
char *strtok_r(char *text, char const *separators, char **saved);
/* (units.c: the campaign scripts' vehicle_load_magic and vehicle_unload) */
short vehicle_scripting_load_magic(long vehicle_index, char const *seat_name, long object_list_index);
short vehicle_scripting_unload(long vehicle_index, char const *seat_name);

/* (menu_functions.c) the local player's name, as his nameplate shows it */
void menus_player_name(char *name, int size);

/* (director.c) whether the flying camera is still the player's */
boolean director_forge_camera_flying(short local_player_index);

/* (players.c) a new object's change colours, as a player's are given */
void placement_data_set_change_color(struct object_placement_data *placement_data, real_rgb_color const *change_color);

/* (forge.c, xinput_sdl.c) Forge's flip combo held */
extern int forge_combo_held;

/* (xinput_sdl.c's forge_combo_filter) set while command mode runs: Back + D-pad
up is its combo too on a desktop's pad (the owner's Select + D-pad up) */
int command_combo_dpad = 0;

/* (port/vita/platform/vita_pad.c) set while the HQ panel is open above the
map: the Vita's buttons in the menus' fixed layout meanwhile - its D-pad the
D-pad (in play it is the sticks' clicks and the black and white buttons),
Cross A, Circle B -, so the panel moves and buys as every other menu does */
int command_menu_pad = 0;

/* (the HQ panel: its row moved again while the D-pad or the left stick is
held, as the menus' lists do) */
#define COMMAND_HQ_REPEAT_FIRST_MILLISECONDS 350
#define COMMAND_HQ_REPEAT_MILLISECONDS 120
#define COMMAND_HQ_STICK_PUSH 0.5f
static short command_hq_held_direction = 0;
static unsigned long command_hq_repeat_at = 0;

/* (objects.c, forge.c) set around object_new: the object the tag names, not
the game type's vehicle set's */
extern int forge_placing_exact;

/* ---------- constants */

enum
{
	/* (both teams' squads, the bases' Shade crews, the Spartan: within the
	selection's mask's 32 bits) */
	COMMAND_MAXIMUM_SQUADS = 24,
	COMMAND_SQUAD_SIZE = 4,
	COMMAND_LINE_SIZE = 128,
	COMMAND_NOTICE_MILLISECONDS = 3000,
	COMMAND_POSSESSED_HINT_MILLISECONDS = 6000,
	COMMAND_DOUBLE_TAP_MILLISECONDS = 400,
	/* A held this long is a sweep, not a tap */
	COMMAND_SWEEP_MILLISECONDS = 250,
	/* (forge.c's keyboard keys: halo_linux_forge_keys) */
	COMMAND_KEY_SHIFT = 1 << 0,
	COMMAND_KEY_TAB = 1 << 2,
	COMMAND_KEY_WHEEL = 1 << 3,
	/* (xinput_sdl.c's, past Forge's: command mode's own keys) */
	COMMAND_KEY_Q = 1 << 4,
	COMMAND_KEY_E = 1 << 5,
	COMMAND_KEY_H = 1 << 6,
	COMMAND_KEY_B = 1 << 7,
	COMMAND_KEY_CTRL_A = 1 << 8,
	/* (the HQ: F, its rows 1 to 9) */
	COMMAND_KEY_F = 1 << 9,
	COMMAND_KEY_DIGIT_FIRST = 10,
};

/* each base's Shades (the owner: two on each base, manned by its own Grunts) */
#define COMMAND_SHADES_PER_BASE 4
/* the Shades a base has: two, and two more with the defence upgrade */
#define COMMAND_SHADES_STANDARD 2

/* the sides (the owner, Nov 5: red and blue, each an army of marines and
the Covenant together; the commander's red for now) */
enum
{
	_command_side_red = 0,
	_command_side_blue,
	NUMBER_OF_COMMAND_SIDES
};

/* a squad's order */
enum
{
	_command_order_none = 0,
	_command_order_move,
	_command_order_attack,
	_command_order_hold,
	/* (the AI side's waves, until the AI commander) on to a point, each
	member fighting what it meets on the way */
	_command_order_advance,
	/* (a Spartan's) the enemy flag fetched and brought to its own stand,
	again and again (command_flag_goal) */
	_command_order_flag,
	NUMBER_OF_COMMAND_ORDERS
};

/* the camera: its height over the focus as a distance along its line, its
pitch, how fast the focus goes (the distance's share a second) and turns */
#define COMMAND_DISTANCE_DEFAULT 20.f
#define COMMAND_DISTANCE_MINIMUM 8.f
#define COMMAND_DISTANCE_MAXIMUM 45.f
#define COMMAND_PITCH_DEFAULT (-55.f * _pi / 180.f)
#define COMMAND_PITCH_MINIMUM (-85.f * _pi / 180.f)
#define COMMAND_PITCH_MAXIMUM (-25.f * _pi / 180.f)
#define COMMAND_PAN_RATE 1.1f
#define COMMAND_PAN_FAST 3.f
#define COMMAND_TURN_RATE 2.2f
#define COMMAND_ZOOM_RATE 1.4f
#define COMMAND_WHEEL_ZOOM 0.12f
#define COMMAND_STICK_DEAD_ZONE 0.2f
/* the camera kept this far off the level it would see from inside, and at
least this far from the focus */
#define COMMAND_CLIP_MARGIN 1.5f
#define COMMAND_CLIP_MINIMUM 4.f
/* the view's focus kept this far past the map's spawns, flags and items */
#define COMMAND_BOUNDS_MARGIN 12.f
/* how far the cursor's line looks */
#define COMMAND_PICK_RANGE 400.f
/* a squad this near the cursor is chosen by A; A held sweeps a circle from
the first radius to the last, growing this much a second */
#define COMMAND_SELECT_RADIUS 6.f
#define COMMAND_SWEEP_RADIUS_MINIMUM 4.f
#define COMMAND_SWEEP_RADIUS_MAXIMUM 30.f
#define COMMAND_SWEEP_RADIUS_RATE 18.f
/* a member's place on a ring this wide about the order's point */
#define COMMAND_RING_RADIUS 1.8f
/* a member this near its place has arrived; holding, one further than the
leash is walked back */
#define COMMAND_ARRIVED 2.5f
#define COMMAND_HOLD_LEASH 6.f
/* a Spartan on a flag run goes on until this near the flag, or his stand */
#define COMMAND_FLAG_RUN_ARRIVED 0.3f
/* a place about an order's point (a member's on its ring, a squad's of an
order given to several) is on the point's own ground: no higher or lower
than this from it (not a base's roof over its floor, nor a rock's top), and
nothing of the level between them a unit's knee high; else drawn in toward
the point */
#define COMMAND_PLACE_RISE 1.6f
#define COMMAND_PLACE_LINE_HEIGHT 0.5f
/* (a squad's own point of an order given to several) how much lower than
the order's point it may be: a small hill's foot about it, not all of them
crowding its top */
#define COMMAND_PLACE_DROP 6.f

/* (actors.c's actor_default_state_alert and _none, as forge.c names them) */
#define COMMAND_ACTOR_DEFAULT_STATE_NONE 0
#define COMMAND_ACTOR_DEFAULT_STATE_ALERT 2
/* (ai_script.c's _ai_unit_effect_magic_sight) */
#define COMMAND_AI_UNIT_EFFECT_MAGIC_SIGHT 3
/* (actor_moving.c's _actor_mode_combat: an actor's state.mode) */
#define COMMAND_ACTOR_MODE_COMBAT 3

/* ---------- structures */

struct command_squad
{
	boolean used;
	short side;
	/* its number on the HUD (from 1) */
	short number;
	long actor_variant_index;
	/* each member's actor variant (a squad of several kinds: an Elite leading
	Grunts) */
	long member_variants[COMMAND_SQUAD_SIZE];
	char name[48];
	long units[COMMAND_SQUAD_SIZE];
	short order;
	real_point3d point;
	long surface_index;
	/* each member's place's ground and its surface (the path searches'
	destination: the surface under that place, not the middle's) */
	real_point3d places[COMMAND_SQUAD_SIZE];
	long place_surfaces[COMMAND_SQUAD_SIZE];
	long target_index;
	/* (the move's start: game time, for the log) */
	long order_time;
	long no_path_logged_time;
	/* (alerts) its units' body and shield, summed, last frame */
	real last_vitality;
	boolean arrived;
	/* riding in to its base by dropship (command_flights): not chosen nor
	ordered until it is down */
	boolean airborne;
	/* the commander's Spartan as AI (command_spartan_to_ai): the list's
	"Spartan", its one unit his Spartan (NONE while it respawns) */
	boolean hero;
	/* (the objectives' tally, command_losses_update) each member's death
	counted */
	boolean counted[COMMAND_SQUAD_SIZE];
	/* a base's Shade's crew (it mans the Shade: never ordered by the AI
	commander) */
	boolean crew;
	/* (the AI commander's) the territory it was sent to take (NONE: none) */
	short ai_goal;
	/* (command_squads.h) another mode's: on its engine team, never in
	Command's lists, no notices */
	boolean hosted;
	short team;
	/* (the lag Ben's log showed: a member with no path to its place searched
	again for it and for the order's point every tick, two full searches each)
	for the order given at path_order_time: the members whose place has no
	path (a bit each: they go to the order's point), and when one with no
	path at all may search again */
	long path_order_time;
	unsigned long place_unreachable;
	long path_retry_time[COMMAND_SQUAD_SIZE];
};

struct command_pad
{
	boolean down[NUMBER_OF_GAMEPAD_BUTTONS];
	real sticks[NUMBER_OF_GAMEPAD_STICKS][2];
	real look_yaw;
	real look_pitch;
	unsigned long keys;
	real wheel;
	boolean toggle;
};

static struct
{
	boolean active;
	short local_player_index;
	/* (the economy, command_economy_*) each side's resources, the next
	payday; the HQ panel open and its row; the Spartan's first spawn paid
	(free) */
	long resources[NUMBER_OF_COMMAND_SIDES];
	long next_income_time;
	boolean hq_open;
	short hq_row;
	/* the unit dropped into, NONE above the map */
	long possessed_unit_index;
	short possessed_squad;
	/* the camera: the focus on the ground, the yaw, the pitch, the distance;
	and what the flying camera was last given (camera_*) */
	real_point3d focus;
	real yaw;
	real pitch;
	real distance;
	boolean camera_placed;
	real_point3d camera_position;
	/* (command_camera_pose) the distance last given, nearer where the level
	stood between */
	real clip_distance;
	real camera_yaw;
	real camera_pitch;
	boolean camera_jump;
	/* the cursor: where the camera's line meets the level, its surface, the
	unit under it */
	boolean cursor_valid;
	real_point3d cursor;
	long cursor_surface_index;
	long cursor_unit_index;
	struct command_squad squads[COMMAND_MAXIMUM_SQUADS];
	/* the squads chosen (a bit each), the first of them */
	unsigned long selection;
	short selected;
	/* A held: when it went down (the sweep's circle grows as it is held) */
	boolean sweep_down;
	unsigned long sweep_since;
	real sweep_radius;
	/* the black button's last press (twice quickly: the last alert), the
	last alert (a squad of his hurt), where he began */
	unsigned long jump_pressed_at;
	boolean alert_valid;
	real_point3d alert;
	unsigned long alert_at;
	boolean start_valid;
	real_point3d start;
	short next_number;
	boolean was_down[NUMBER_OF_GAMEPAD_BUTTONS];
	boolean toggle_was_down;
	unsigned long keys_was_down;
	/* the console's: buttons pressed, sticks held, for frames */
	short test_buttons_frames;
	boolean test_down[NUMBER_OF_GAMEPAD_BUTTONS];
	boolean test_toggle;
	short test_sticks_frames;
	real test_sticks[NUMBER_OF_GAMEPAD_STICKS][2];
	/* (the desktop, the keyboard and mouse's player: command_pointer_*) the
	mouse's pointer over the view, in the screen's pixels (the game's own,
	SCREEN_HEIGHT high), a left button's drag from where it went down */
	boolean view_valid;
	struct render_camera view_camera;
	struct render_frustum view_frustum;
	boolean pointer_valid;
	real pointer_x;
	real pointer_y;
	/* (tests: "command mouse x y frames": the pointer held there, the real
	one's frames not taken meanwhile) */
	short test_pointer_frames;
	boolean drag_down;
	real drag_x;
	real drag_y;
	/* the Command game (the Forge lobby's START COMMAND): asked for, its map
	loaded and built out (begun once the player has spawned), running; the
	next wave's game time, the waves sent; the map's bounds for the view */
	boolean game_requested;
	boolean game_pending;
	boolean game_running;
	long next_wave_time;
	/* each side's next free wave (with the barracks they come sooner), and
	how many it has had */
	long next_side_wave_time[NUMBER_OF_COMMAND_SIDES];
	short side_waves[NUMBER_OF_COMMAND_SIDES];
	short waves;
	boolean bounds_valid;
	real bounds[4];
	/* each base's Shades (their objects, NONE: not there) */
	long shades[NUMBER_OF_COMMAND_SIDES][COMMAND_SHADES_PER_BASE];
	/* (command_toggle_request) */
	int volatile toggle_requested;
	/* the way back said this long after dropping in */
	unsigned long possessed_hint_until;
	/* the HUD's */
	char notice[COMMAND_LINE_SIZE];
	unsigned long notice_until;
	/* (command_squads.h) another game mode hosts the squads: their orders
	run without the commander's view (the last field: the initializer
	is by position) */
	boolean squads_hosted;
	/* the side this machine's commander plays (red in a local game; his
	team's in a network game: command_player_side) */
	short side;
} command_globals = { FALSE, 0 };

/* (the AI commander's, command_ai_*) */
#define COMMAND_AI_SQUADS_FIRST 2
#define COMMAND_AI_SQUADS_SECOND 3
/* (defending) enemies this near its rally point are an attack on its base */
#define COMMAND_AI_BASE_THREAT 25.f

/* (C13, the owner, Nov 7) the AI commander's difficulty, the Command
lobby's row: how fast it reacts and buys, how it expands, defends and
attacks. The soldiers - both sides' - always fight on Halo CE's Legendary
rules whatever this is (command_legendary_rules) */
enum
{
	_command_ai_easy = 0,
	_command_ai_normal,
	_command_ai_heroic,
	_command_ai_legendary,
	NUMBER_OF_COMMAND_AI_LEVELS
};

static struct
{
	char const *name;
	char const *log_name;
	/* how often it thinks (reacts) */
	short think_seconds;
	/* its first squad bought this long after the start */
	short first_buy_seconds;
	/* the squads it keeps in the field at most */
	short field_squads;
	/* squads with nothing to take, held at its base until this many: then
	they attack the enemy's base */
	short attack_squads;
	/* the squads sent to one territory at most */
	short per_goal;
	/* the resources it keeps back from more squads (a slow spender) */
	short reserve;
	/* a territory it holds, or its base, under attack: squads sent back */
	boolean defends;
	/* its flag taken: the carrier hunted */
	boolean hunts_carrier;
	/* the late game's Spartan bought */
	boolean buys_spartan;
	/* the upgrades it builds, in order, after its first squads (keys) */
	char const *upgrades[6];
} const command_ai_levels[NUMBER_OF_COMMAND_AI_LEVELS] =
{
	{ N_("Easy"), "easy", 8, 40, 3, 99, 1, 120, FALSE, FALSE, FALSE, { "barracks", "supply", NULL } },
	{ N_("Normal"), "normal", 4, 15, 5, 4, 2, 40, TRUE, TRUE, TRUE,
		{ "barracks", "supply", "defence", "armory", "vehiclebay", NULL } },
	{ N_("Heroic"), "heroic", 2, 4, 7, 3, 2, 0, TRUE, TRUE, TRUE,
		{ "supply", "barracks", "armory", "defence", "vehiclebay", NULL } },
	{ N_("Legendary"), "legendary", 1, 0, 8, 2, 3, 0, TRUE, TRUE, TRUE,
		{ "supply", "barracks", "armory", "defence", "vehiclebay", NULL } },
};

/* (the lobby's AI COMMANDER row; Heroic unless chosen) */
static short command_ai_level = _command_ai_heroic;
/* the Hunter pair bought when this rich */
#define COMMAND_AI_HUNTERS_RICH 320

static struct
{
	boolean plays;
	long next_think_time;
	short next_squad_kind;
} command_ai[NUMBER_OF_COMMAND_SIDES];
#ifndef HALO_RELEASE
/* (tests: "aiside", HALO_TEST_COMMAND_NO_AI) a side's AI commander kept from
buying and ordering */
static boolean command_ai_test_off[NUMBER_OF_COMMAND_SIDES];
/* ("aiside <side> play": the AI commander plays a side with its human
commander above it - AI against AI, the lag's measures) */
static boolean command_ai_test_play[NUMBER_OF_COMMAND_SIDES];
#endif

/* each side's Spartan (the AI commander's and a human side's alike: made
at its base's rally point once bought, AI while no one is in it, respawned
there for COMMAND_SPARTAN_COST): its unit (NONE: none), when it may come
again, its death counted, its first spawn (free) made; the player in it
(an absolute index: the side's commander who took it over, NONE: the AI
drives it) */
static struct
{
	long unit_index;
	long respawn_time;
	boolean counted;
	boolean paid;
	short possessor;
	/* (given its AI) the game time its new actor sees the enemies near it
	(its first update past: NONE, none pending) */
	long aware_time;
} command_spartans[NUMBER_OF_COMMAND_SIDES];

/* (each human's, the host's, by absolute player index) the body a
commander has while he is above the map: unseen, unhurt, ignored by the
AI (his game spawn, or one made when he left his Spartan) */
#define COMMAND_PLAYERS 128
static long command_seats[COMMAND_PLAYERS];

static char const *const command_side_names[NUMBER_OF_COMMAND_SIDES] = { "red", "blue" };

/* (C1: the Shades' gunners firing: the hits their shots made, each
side's, the console's "where" says) */
static long command_crew_hits[NUMBER_OF_COMMAND_SIDES];
static real command_crew_damage[NUMBER_OF_COMMAND_SIDES];
static char const *const command_order_log_names[NUMBER_OF_COMMAND_ORDERS] = { "none", "move", "attack", "hold", "advance", "flag" };
static char const *const command_order_names[NUMBER_OF_COMMAND_ORDERS] =
{
	N_("IDLE"), N_("MOVE"), N_("ATTACK"), N_("HOLD"), N_("ADVANCE"), N_("FLAG"),
};

/* ---------- private code */

static void command_notice(
	char const *format,
	...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(command_globals.notice, sizeof(command_globals.notice), format, arguments);
	va_end(arguments);
	command_globals.notice_until = system_milliseconds() + COMMAND_NOTICE_MILLISECONDS;
}

/* what the host's game says to the commanders (a notice each: shown on the
host to its own commander, sent to a network game's clients -
command_net.inc - each shown to the commanders it is for). subject: the
side it is of; a and b its numbers */
enum
{
	/* (the subject's) a squad's order: a its number (0: the Spartan), b the order */
	_command_event_order = 0,
	/* (the subject's) a squad down by air: a its number */
	_command_event_arrived,
	/* (the subject's) a squad inbound: a its number, b the ship */
	_command_event_inbound,
	/* (everyone's) the subject's ship shot down: a its squad's number, b the ship */
	_command_event_ship_lost,
	/* (everyone's) a territory taken by the subject: a its letter (0: a supply
	point), b its number */
	_command_event_territory,
	/* (the subject's) an upgrade built: a its HQ row */
	_command_event_built,
	/* (the subject's) a buy or a request refused: a why (command_reasons) */
	_command_event_refused,
	/* (the subject's) a squad ordered that leaves later: a its row, b seconds */
	_command_event_ordered,
	/* (the subject's) an upgrade begun: a its row */
	_command_event_building,
	/* (the subject's) the Spartan's secondary chosen: a the pick */
	_command_event_secondary,
	/* (everyone's) the subject's flag taken, scored, dropped, returned */
	_command_event_flag_taken,
	_command_event_flag_scored,
	_command_event_flag_dropped,
	_command_event_flag_returned,
	/* (everyone's) the game won: a the winner (COMMAND_DRAW: a draw) */
	_command_event_winner,
	/* (the subject's) its Spartan down, its Spartan here */
	_command_event_spartan_down,
	_command_event_spartan_here,
	NUMBER_OF_COMMAND_EVENTS
};
/* (an event's audience) every commander's */
#define COMMAND_EVENT_EVERYONE (-2)

/* the reasons a buy or a request is refused, as the notices say them */
static char const *const command_reasons[] =
{
	"",
	N_("Needs the barracks"), N_("No room for more troops"), N_("Needs the barracks and the armory"),
	N_("Already in the field"), N_("Needs the armory"), N_("Already built"), N_("Being built"), N_("Can't afford"),
	N_("Those troops can't come: their files are missing"), N_("No Spartan yet: the HQ sells one"),
	N_("Your Spartan is respawning"), N_("Another commander is in your Spartan"),
};
#define COMMAND_REASONS ((short)NUMBEROF(command_reasons))

/* a reason's number (0: none known) */
static short command_reason_of(
	char const *why)
{
	short index;

	for (index = 1; why && index < COMMAND_REASONS; index++)
	{
		if (!strcmp(command_reasons[index], why))
			return index;
	}
	return 0;
}

static void command_event(short audience, short subject, short code, long a, long b);
/* (network play, command_net.inc) each frame's: the host's state sent, a
client's requests */
static void command_net_frame(void);
static boolean command_net_command(char const *text);

static struct unit_datum *command_living_unit(
	long unit_index)
{
	struct object_datum *object = object_try_and_get(unit_index);

	if (!object || !(FLAG(object->object.type) & _object_mask_unit) ||
		TEST_FLAG(object->object.damage_flags, _object_dead_bit))
	{
		return NULL;
	}
	return (struct unit_datum *)object;
}

static long command_player_index(
	void)
{
	return local_player_get_player_index(command_globals.local_player_index);
}

/* a side's team: the UNSC's the commander's own (his allies, as Forge's
"player" AI team), the Covenant's the Covenant's */
/* a side's team, whatever the species of its units (a grunt of red's is
red's, as Forge gives a character the player's team): red the commander's
own (his allies), blue another the engine's allegiance makes his enemy */
/* whether the Command game is a network game's (the host's or a client's) */
static boolean command_network(
	void)
{
	return game_connection() == _game_connection_network_server || game_connection() == _game_connection_network_client;
}

/* (network play) this machine is a client of the host's Command game: the
host runs the game, this machine shows it and asks */
static boolean command_client(
	void)
{
	return game_connection() == _game_connection_network_client;
}

static short command_side_team(
	short side)
{
	long player_index;
	short red;

	/* (a network game: a team game, red's team 0 and blue's 1 - each human
	commands his team's side) */
	if (command_network())
		return side == _command_side_blue ? 1 : 0;
	player_index = command_player_index();
	red = player_index != NONE ? (short)player_get(player_index)->team_index : (short)_game_team_player;
	if (side == _command_side_red)
		return red;
	return red == _game_team_covenant ? (short)_game_team_human : (short)_game_team_covenant;
}

/* the side a player commands: his team's in a network game (NONE: neither
red's nor blue's), red in a local game */
static short command_player_side(
	long player_index)
{
	struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;

	if (!player)
		return NONE;
	if (!command_network())
		return _command_side_red;
	if (player->team_index == 0)
		return _command_side_red;
	if (player->team_index == 1)
		return _command_side_blue;
	return NONE;
}

/* a side's Spartan's unit, NONE */
static long command_side_spartan(
	short side)
{
	return side >= 0 && side < NUMBER_OF_COMMAND_SIDES ? command_spartans[side].unit_index : NONE;
}

/* this machine's commander's Spartan */
static long command_my_spartan(
	void)
{
	return command_side_spartan(command_globals.side);
}

/* a player's absolute index (for the seats' table: NONE past it) */
static short command_player_slot(
	long player_index)
{
	long absolute;

	if (player_index == NONE)
		return NONE;
	absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	return absolute >= 0 && absolute < COMMAND_PLAYERS ? (short)absolute : NONE;
}

/* whether a unit is a commander's seat (above the map) */
static boolean command_unit_is_seat(
	long unit_index)
{
	short slot;

	if (unit_index == NONE)
		return FALSE;
	/* (a client: a player's unit that is no side's Spartan) */
	if (command_client())
	{
		struct object_datum *object = object_try_and_get(unit_index);

		return object && (FLAG(object->object.type) & _object_mask_unit) &&
			((struct unit_datum *)object)->unit.player_index != NONE &&
			unit_index != command_side_spartan(_command_side_red) && unit_index != command_side_spartan(_command_side_blue);
	}
	for (slot = 0; slot < COMMAND_PLAYERS; slot++)
	{
		if (command_seats[slot] == unit_index)
			return TRUE;
	}
	return FALSE;
}

static short command_squad_living(
	struct command_squad const *squad)
{
	short index;
	short living = 0;

	for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
	{
		if (command_living_unit(squad->units[index]))
			living++;
	}
	return living;
}

/* the squad's first living unit (NONE if none) */
static long command_squad_leader(
	struct command_squad const *squad)
{
	short index;

	for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
	{
		if (command_living_unit(squad->units[index]))
			return squad->units[index];
	}
	return NONE;
}

static boolean command_squad_center(
	struct command_squad const *squad,
	real_point3d *center)
{
	short index;
	short count = 0;

	center->x = center->y = center->z = 0.f;
	for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
	{
		real_point3d origin;

		if (!command_living_unit(squad->units[index]))
			continue;
		object_get_origin(squad->units[index], &origin);
		center->x += origin.x;
		center->y += origin.y;
		center->z += origin.z;
		count++;
	}
	if (!count)
		return FALSE;
	center->x /= count;
	center->y /= count;
	center->z /= count;
	return TRUE;
}

static void command_selection_set(unsigned long mask, char const *how);
static void command_edge_scroll(real *i, real *j);
void command_pointer_update(struct halo_ui_pointer const *pointer, boolean present);

/* the squad whose unit unit_index is (its member slot too), NULL if none */
static struct command_squad *command_squad_of_unit(
	long unit_index,
	short *member)
{
	short squad_index;

	if (unit_index == NONE)
		return NULL;
	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad *squad = &command_globals.squads[squad_index];
		short index;

		if (!squad->used)
			continue;
		for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
		{
			if (squad->units[index] == unit_index)
			{
				if (member)
					*member = index;
				return squad;
			}
		}
	}
	return NULL;
}

static struct command_squad *command_squad_selected(
	void)
{
	if (command_globals.selected < 0 || command_globals.selected >= COMMAND_MAXIMUM_SQUADS ||
		!command_globals.squads[command_globals.selected].used || !(command_globals.selection & (1UL << command_globals.selected)))
	{
		return NULL;
	}
	return &command_globals.squads[command_globals.selected];
}

/* a member's place by its squad's order's point: the first in the middle,
the others on a ring about it */
static void command_member_place(
	struct command_squad const *squad,
	short member,
	real_point3d *place)
{
	*place = squad->point;
	if (member > 0)
	{
		real angle = (real)(member - 1) * (2.f * _pi / (real)(COMMAND_SQUAD_SIZE - 1));

		place->x += COMMAND_RING_RADIUS * (real)cos(angle);
		place->y += COMMAND_RING_RADIUS * (real)sin(angle);
	}
}

static boolean command_ground(real_point3d const *point, real_point3d *ground, long *surface_index);

/* the ground at a place about an order's point (center, on its ground,
surface its surface or NONE): the place's own on the point's level, else
the place drawn in halfway, a quarter, else the point itself. The ground
under a place is looked for from just over the point's height, not from
over everything (a base's floor, not its roof; the canyon's floor beside a
rock, not the rock's top), and the place taken only on the point's level
(no higher than COMMAND_PLACE_RISE over it, no lower than drop under it)
with nothing of the level between them (a base's wall, a rock): a unit sent
there walks the point's ground, not a long way round up a hill - or
nowhere */
static void command_place_ground(
	real_point3d const *center,
	long center_surface_index,
	real_point3d const *place,
	real drop,
	real_point3d *ground,
	long *surface_index)
{
	short attempt;

	for (attempt = 0; attempt < 3; attempt++)
	{
		static real const share[3] = { 1.f, 0.5f, 0.25f };
		real_point3d at = *center;
		real_point3d start;
		real_vector3d vector = { 0.f, 0.f, 0.f };
		struct collision_result collision;

		at.x += (place->x - center->x) * share[attempt];
		at.y += (place->y - center->y) * share[attempt];
		start = at;
		start.z = center->z + COMMAND_PLACE_RISE;
		vector.k = -COMMAND_PLACE_RISE - drop;
		if (!collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
				&start, &vector, NONE, &collision))
		{
			continue;
		}
		at = collision.point;
		/* (nothing of the level between the point and it, knee high - over
		a lower place, level from the point: over a hill's shoulder down to
		its foot, still into a wall) */
		{
			real_point3d from = *center;
			struct collision_result between;

			from.z += COMMAND_PLACE_LINE_HEIGHT;
			vector.i = at.x - center->x;
			vector.j = at.y - center->y;
			vector.k = MAX(at.z - center->z, 0.f);
			if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
					FLAG(_collision_test_back_facing_surfaces_bit), &from, &vector, NONE, &between))
			{
				continue;
			}
		}
		*ground = at;
		*surface_index = collision.surface_index;
		return;
	}
	*ground = *center;
	*surface_index = center_surface_index;
}

/* every member's place for the squad's point, each on the ground under it */
static void command_squad_places(
	struct command_squad *squad)
{
	short member;

	for (member = 0; member < COMMAND_SQUAD_SIZE; member++)
	{
		real_point3d place;

		command_member_place(squad, member, &place);
		if (member == 0)
			squad->place_surfaces[member] = squad->surface_index;
		else
			command_place_ground(&squad->point, squad->surface_index, &place, COMMAND_PLACE_RISE, &place, &squad->place_surfaces[member]);
		squad->places[member] = place;
	}
}

static real command_distance2d(
	real_point3d const *a,
	real_point3d const *b)
{
	real dx = a->x - b->x;
	real dy = a->y - b->y;

	return (real)sqrt(dx * dx + dy * dy);
}

/* the level (and its units) under the camera's line: the cursor */
static void command_cursor_update(
	void)
{
	struct observer_result const *camera = observer_get_camera(command_globals.local_player_index);
	struct collision_result collision;
	real_vector3d vector;

	command_globals.cursor_valid = FALSE;
	command_globals.cursor_unit_index = NONE;
	if (!camera)
		return;
	vector.i = camera->forward.i * COMMAND_PICK_RANGE;
	vector.j = camera->forward.j * COMMAND_PICK_RANGE;
	vector.k = camera->forward.k * COMMAND_PICK_RANGE;
	if (collision_test_vector(
			FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
			FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_bipeds_bit) |
			FLAG(_collision_test_objects_vehicles_bit),
			&camera->position,
			&vector,
			command_my_spartan(),
			&collision))
	{
		command_globals.cursor_valid = TRUE;
		command_globals.cursor = collision.point;
		command_globals.cursor_surface_index = collision.type == _collision_result_structure ? collision.surface_index : NONE;
		if (collision.type == _collision_result_object && object_try_and_get(collision.object_index))
		{
			long hit = object_get_ultimate_parent(collision.object_index);

			if (command_living_unit(hit))
				command_globals.cursor_unit_index = hit;
		}
	}
}

/* the ground under a point (its height, its surface), FALSE if none within
reach */
static boolean command_ground(
	real_point3d const *point,
	real_point3d *ground,
	long *surface_index)
{
	struct collision_result collision;
	real_point3d start;
	real_vector3d down = { 0.f, 0.f, 0.f };
	short attempt;

	/* (near the point first - under an overhang the ground, not the rock
	above; in a base its floor, not its roof a little over the point -,
	then from higher, then from high over it) */
	for (attempt = 0; attempt < 3; attempt++)
	{
		static real const above[3] = { 0.5f, 3.f, 60.f };

		start = *point;
		start.z += above[attempt];
		down.k = attempt == 2 ? -200.f : -12.f;
		if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
				&start, &down, NONE, &collision))
		{
			*ground = collision.point;
			if (surface_index)
				*surface_index = collision.surface_index;
			return TRUE;
		}
	}
	return FALSE;
}

/* the top of what stands under a point (the camera's focus: a base's roof,
not its floor), FALSE if nothing within reach */
static boolean command_ground_top(
	real_point3d const *point,
	real_point3d *ground)
{
	struct collision_result collision;
	real_point3d start;
	real_vector3d down = { 0.f, 0.f, 0.f };
	short attempt;

	/* (from high over it - a roof over the floor -, then from near: a ray
	begun outside the level meets nothing) */
	for (attempt = 0; attempt < 3; attempt++)
	{
		static real const above[3] = { 40.f, 12.f, 3.f };

		start = *point;
		start.z += above[attempt];
		down.k = -(above[attempt] + 60.f);
		if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
				&start, &down, NONE, &collision))
		{
			*ground = collision.point;
			return TRUE;
		}
	}
	return FALSE;
}

/* (C3, the owner: the camera kept at a steady height over the ground, not
zooming with every hill) the ground's height about a point, smoothed: the
mean of the tops under it and about it (COMMAND_GROUND_SMOOTH_RADIUS), so a
bump, a rock or a base's roof barely moves the view */
#define COMMAND_GROUND_SMOOTH_RADIUS 9.f
#define COMMAND_GROUND_SMOOTH_RATE 1.5f
/* (a hill between the camera and the focus) the camera rises over it, at
once; it comes down again this slowly */
#define COMMAND_CAMERA_RISE_DOWN_RATE 0.8f

static boolean command_ground_smooth(
	real_point3d const *point,
	real *height)
{
	real total = 0.f;
	short count = 0;
	short index;

	for (index = 0; index < 9; index++)
	{
		real_point3d sample = *point;
		real_point3d ground;

		if (index > 0)
		{
			real angle = (real)(index - 1) * _pi / 4.f;

			sample.x += (real)cos(angle) * COMMAND_GROUND_SMOOTH_RADIUS;
			sample.y += (real)sin(angle) * COMMAND_GROUND_SMOOTH_RADIUS;
		}
		if (command_ground_top(&sample, &ground))
		{
			total += ground.z;
			count++;
		}
	}
	if (!count)
		return FALSE;
	*height = total / (real)count;
	return TRUE;
}

/* the camera's rise over a hill between it and the focus (eased down), its
pitch as the view has it (toward the focus from where it is) */
static real command_camera_rise = 0.f;
static real command_camera_view_pitch = COMMAND_PITCH_DEFAULT;

/* the camera's pose: its tilt and its distance the zoom's - never pulled in
by the level -, raised where a hill stands between it and the focus (so that
it never sees from inside it), and above the ground under it. seconds: how
far the rise eases back down (0: at once) */
static void command_camera_pose(
	real seconds,
	real_point3d *position,
	real_vector3d *forward)
{
	real_point3d from = command_globals.focus;
	real distance = command_globals.distance;
	real_point3d ground;
	real rise = 0.f;
	real_vector3d look;
	short pass;

	forward->i = (real)(cos(command_globals.pitch) * cos(command_globals.yaw));
	forward->j = (real)(cos(command_globals.pitch) * sin(command_globals.yaw));
	forward->k = (real)sin(command_globals.pitch);
	position->x = command_globals.focus.x - forward->i * distance;
	position->y = command_globals.focus.y - forward->j * distance;
	position->z = command_globals.focus.z - forward->k * distance;
	/* (the line's start over the ground at the focus: the smoothed focus
	can be under a hilltop) */
	if (command_ground_top(&from, &ground) && from.z < ground.z)
		from.z = ground.z;
	from.z += 0.5f;
	/* (a hill between: the camera raised until the line from the focus to
	it passes over what was met, with a margin) */
	for (pass = 0; pass < 3; pass++)
	{
		struct collision_result collision;
		real_vector3d back;
		real_point3d camera = *position;

		camera.z += rise;
		back.i = camera.x - from.x;
		back.j = camera.y - from.y;
		back.k = camera.z - from.z;
		if (!collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
				FLAG(_collision_test_back_facing_surfaces_bit), &from, &back, NONE, &collision))
		{
			break;
		}
		{
			real hit = (real)sqrt((collision.point.x - from.x) * (collision.point.x - from.x) +
				(collision.point.y - from.y) * (collision.point.y - from.y));
			real across = (real)sqrt(back.i * back.i + back.j * back.j);
			real wanted = hit > 0.5f ?
				from.z + (collision.point.z + COMMAND_CLIP_MARGIN - from.z) * across / hit :
				collision.point.z + COMMAND_CLIP_MARGIN;

			if (wanted - position->z <= rise + 0.05f)
				break;
			rise = wanted - position->z;
		}
	}
	/* (up at once, down eased: a hill passing behind does not jolt it) */
	if (seconds > 0.f && rise < command_camera_rise)
		rise = command_camera_rise + (rise - command_camera_rise) * PIN(seconds * COMMAND_CAMERA_RISE_DOWN_RATE, 0.f, 1.f);
	command_camera_rise = rise;
	position->z += rise;
	if (command_ground_top(position, &ground) && position->z < ground.z + COMMAND_CLIP_MARGIN)
		position->z = ground.z + COMMAND_CLIP_MARGIN;
	/* (the view toward the focus from where the camera is) */
	look.i = command_globals.focus.x - position->x;
	look.j = command_globals.focus.y - position->y;
	look.k = command_globals.focus.z - position->z;
	if (normalize3d(&look) > 0.f)
	{
		*forward = look;
		command_camera_view_pitch = (real)asin(PIN(look.k, -1.f, 1.f));
	}
	else
		command_camera_view_pitch = command_globals.pitch;
}

/* the flying camera put above the focus */
static void command_camera_place(
	void)
{
	real_vector3d forward;

	command_globals.clip_distance = 0.f;
	command_camera_rise = 0.f;
	command_camera_pose(0.f, &command_globals.camera_position, &forward);
	command_globals.camera_yaw = command_globals.yaw;
	command_globals.camera_pitch = command_camera_view_pitch;
	director_forge_camera(command_globals.local_player_index, TRUE, &command_globals.camera_position, &forward);
	command_globals.camera_placed = TRUE;
}

/* the view from above, from where the player's unit (or the camera) is */
static void command_view_begin(
	void)
{
	real_point3d from;
	real_point3d ground;
	struct observer_result const *camera = observer_get_camera(command_globals.local_player_index);

	if (object_try_and_get(command_globals.possessed_unit_index))
		object_get_origin(command_globals.possessed_unit_index, &from);
	else if (object_try_and_get(command_my_spartan()))
		object_get_origin(command_my_spartan(), &from);
	else if (camera)
		from = camera->position;
	else
		from.x = from.y = from.z = 0.f;
	command_globals.focus = from;
	if (command_ground(&from, &ground, NULL))
		command_globals.focus.z = ground.z;
	if (camera)
		command_globals.yaw = (real)atan2(camera->forward.j, camera->forward.i);
	command_camera_place();
}

/* the map built out for command mode: what a multiplayer map lacks - the
AI characters and their data, the dropships - from the player's own
campaign maps, the modes' own pack files (PREPARING COMMAND built them;
one not built yet is built now, as the map loads), loaded now, each once a
map */
static char const *const command_build_out_packs[] = { "command_marines", "command_covenant", "command_dropships" };

static void command_build_out(
	void)
{
	short index;

	/* (all three asked for first: the builds of those made of the same map
	share its read, scene_packs_load_now - command_covenant's and
	command_dropships' b40) */
	for (index = 0; index < (short)NUMBEROF(command_build_out_packs); index++)
		scene_packs_prebuild(command_build_out_packs[index]);
	for (index = 0; index < (short)NUMBEROF(command_build_out_packs); index++)
	{
		char why[160];
		long progress = 0;
		unsigned long before = scene_packs_reserved_bytes();
		unsigned long long started = vita_host_time_us();

		if (scene_packs_load_now(command_build_out_packs[index]))
		{
			platform_log("command: build-out: %s loaded in %.1f s (%lu KB more of the tag cache, %lu KB held by packs)",
				command_build_out_packs[index], (double)(vita_host_time_us() - started) / 1000000.0,
				(scene_packs_reserved_bytes() - before) / 1024, scene_packs_reserved_bytes() / 1024);
		}
		else
		{
			why[0] = 0;
			scene_packs_status(command_build_out_packs[index], &progress, why, sizeof(why));
			platform_log("command: build-out: %s not loaded: %s", command_build_out_packs[index], why[0] ? why : "?");
		}
	}
}

struct command_squad;
static struct command_squad *command_hero_squad(boolean make);
static void command_bounds_build(void);
static void command_player_set_unit(long player_index, long unit_index);
static void command_seat_prepare(long unit_index);
static long command_seat_new(long player_index, short side);
static void command_spartan_ai_attach(short side);
static boolean command_possess(long player_index, char const **why);
static void command_unpossess(short side, char const *why);
static void command_view_follow(void);
static boolean command_spartan_owned[NUMBER_OF_COMMAND_SIDES];
static real_point3d command_seat_spots[COMMAND_PLAYERS];
static boolean command_seat_spot_valid[COMMAND_PLAYERS];

static boolean command_begin(
	void)
{
	long player_index;
	struct unit_datum *unit;
	short side;

	if (command_globals.active)
		return TRUE;
	/* (a network game: a Command game's alone, the host's or a client's of
	it - the prototype's "command on" is a local game's) */
	if (game_connection() != _game_connection_local && !command_globals.game_pending && !command_globals.game_running)
	{
		platform_log("command: only in a local game, or a network Command game");
		return FALSE;
	}
	command_globals.local_player_index = 0;
	player_index = command_player_index();
	if (player_index == NONE || !(unit = command_living_unit(player_get(player_index)->unit_index)))
	{
		platform_log("command: no local player with a living unit");
		return FALSE;
	}
	side = command_player_side(player_index);
	if (side == NONE)
	{
		platform_log("command: the player is on neither red's nor blue's team");
		return FALSE;
	}
	command_globals.side = side;
	/* (the view's bounds: the map's scenario may not have been there yet as
	it loaded) */
	if (!command_globals.bounds_valid)
		command_bounds_build();
	/* (Forge's monitor first back to its Spartan: one flying camera) */
	if (forge_monitor_active())
		forge_command("off");
	/* (a Command game's map was built out as it loaded) */
	if (!command_globals.game_pending)
		command_build_out();
	players_unzoom_all();
	command_globals.possessed_unit_index = NONE;
	command_globals.possessed_squad = NONE;
	command_globals.pitch = COMMAND_PITCH_DEFAULT;
	command_globals.distance = COMMAND_DISTANCE_DEFAULT;
	command_globals.active = TRUE;
	command_combo_dpad = 1;
	object_get_origin(player_get(player_index)->unit_index, &command_globals.start);
	command_globals.start_valid = TRUE;
	command_view_begin();
	/* (the prototype's "command on", no Command game: his own unit is his
	Spartan, the AI's while he commands, and he is above the map in a seat
	made where it stands) */
	if (!command_globals.game_pending && !command_globals.game_running)
	{
		long spartan_index = player_get(player_index)->unit_index;
		short slot = command_player_slot(player_index);

		command_spartans[side].unit_index = spartan_index;
		command_spartans[side].possessor = NONE;
		command_spartan_owned[side] = TRUE;
		if (slot != NONE)
		{
			command_seat_spots[slot] = command_globals.start;
			command_seat_spot_valid[slot] = TRUE;
			if (command_seat_new(player_index, side) != NONE)
				command_player_set_unit(player_index, command_seats[slot]);
		}
		command_spartan_ai_attach(side);
	}
	platform_log("command: %s on (%s): the commander above %.2f %.2f %.2f (yaw %.0f, distance %.1f)", COMMAND_MODE_NAME,
		command_side_names[side], command_globals.focus.x, command_globals.focus.y, command_globals.focus.z,
		command_globals.yaw * 180.f / _pi, command_globals.distance);
	command_notice("%s", T(COMMAND_MODE_NAME));

	return TRUE;
}

static void command_leave(
	char const *why);

static void command_end(
	void)
{
	long player_index = command_player_index();
	short side = command_globals.side;
	short slot = command_player_slot(player_index);

	if (!command_globals.active)
		return;
	/* (the player back in his Spartan, its actor let go; his seat gone, the
	Spartan's entry with it - a local game's: the host's game decides a
	network one's) */
	if (!command_network() && player_index != NONE && side != NONE)
	{
		char const *why = NULL;
		long seat_index = slot != NONE ? command_seats[slot] : NONE;

		if (command_globals.possessed_unit_index == NONE && command_possess(player_index, &why) && seat_index != NONE &&
			object_try_and_get(seat_index))
		{
			object_delete(seat_index);
		}
		if (slot != NONE)
			command_seats[slot] = NONE;
		command_spartans[side].possessor = NONE;
		if (!command_globals.game_running)
		{
			struct command_squad *hero = command_hero_squad(FALSE);

			if (hero)
				hero->used = FALSE;
			command_spartans[side].unit_index = NONE;
			command_spartan_owned[side] = FALSE;
		}
	}
	director_forge_camera(command_globals.local_player_index, FALSE, NULL, NULL);
	command_globals.active = FALSE;
	command_combo_dpad = 0;
	command_globals.camera_placed = FALSE;
	command_globals.possessed_unit_index = NONE;
	platform_log("command: off: the player back in his own unit");
}

/* ---- squads */

static long command_actor_variant_find(
	char const *name)
{
	struct tag_iterator iterator;
	long tag_index;

	tag_iterator_new(&iterator, ACTOR_VARIANT_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		char const *tag_name = tag_get_name(tag_index);

		if (tag_get_group_tag(tag_index) != ACTOR_VARIANT_DEFINITION_TAG || !tag_name)
			continue;
		if (strstr(tag_name, name) && actor_variant_definition_get(tag_index)->unit_reference.index != NONE)
			return tag_index;
	}
	return NONE;
}


/* (command_squads.h) a hosted squad's engine team while it is made (NONE:
the side's, every Command squad's) */
static short command_spawn_team = NONE;

/* a unit of the actor variant, with a free actor, on the side's team (as
forge.c's forge_place places a character) */
static long command_unit_spawn(
	long actor_variant_index,
	short side,
	real_point3d const *position,
	real yaw)
{
	struct actor_variant_definition *variant = actor_variant_definition_get(actor_variant_index);
	struct object_placement_data placement;
	struct unit_datum *unit;
	long object_index;
	short team = command_spawn_team != NONE ? command_spawn_team : command_side_team(side);

	object_placement_data_new(&placement, variant->unit_reference.index, NONE);
	SET_FLAG(placement.flags, _new_object_never_automatically_delete_bit, TRUE);
	placement.position = *position;
	placement.forward.i = (real)cos(yaw);
	placement.forward.j = (real)sin(yaw);
	placement.forward.k = 0.f;
	object_index = object_new(&placement);
	if (object_index == NONE)
		return NONE;
	if (variant->actor_reference.index != NONE)
		actor_customize_unit(actor_variant_index, object_index);
	ai_scripting_attach_free(object_index, actor_variant_index);
	unit = unit_get(object_index);
	unit->object.owner_team_index = team;
	/* (always active, as ai_force_active makes an encounter: a dormant
	actor is not updated - out of the players' sight with nothing to do it
	goes dormant - and the commander's orders must reach it anywhere) */
	if (unit->unit.actor_index != NONE)
	{
		struct actor_datum *actor = actor_get(unit->unit.actor_index);

		actor->meta.team_index = team;
		actor->meta.force_active = TRUE;
		actor_set_dormant(unit->unit.actor_index, FALSE);
	}
	return object_index;
}

/* a squad of the side at a point: its kinds, "name:count,name:count" (the
first named leads), each the first actor variant with that name in its tag's
name (the map's, the packs'); count alone (a name with no ':') that many of
one kind. NULL if none could be */
static struct command_squad *command_squad_spawn(
	short side,
	short count,
	char const *composition,
	real_point3d const *at)
{
	struct command_squad *squad = NULL;
	short squad_index;
	short index;
	short members = 0;
	real_point3d ground;
	long surface_index = NONE;
	long variants[COMMAND_SQUAD_SIZE];
	char text[160];
	char *part;
	char *saved = NULL;
	char const *leaf;
	char names[COMMAND_SQUAD_SIZE][48];

	snprintf(text, sizeof(text), "%s", composition);
	for (part = strtok_r(text, ",", &saved); part && members < COMMAND_SQUAD_SIZE; part = strtok_r(NULL, ",", &saved))
	{
		char *colon = strrchr(part, ':');
		short part_count = count;
		long variant;

		if (colon)
		{
			*colon = 0;
			part_count = (short)atoi(colon + 1);
		}
		while (*part == ' ')
			part++;
		variant = command_actor_variant_find(part);
		if (variant == NONE)
		{
			platform_log("command: spawn: no actor variant named like \"%s\" (the map's tags and the packs loaded)", part);
			return NULL;
		}
		for (index = 0; index < part_count && members < COMMAND_SQUAD_SIZE; index++)
		{
			snprintf(names[members], sizeof(names[members]), "%s", part);
			variants[members++] = variant;
		}
	}
	if (!members)
		return NULL;
	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		if (!command_globals.squads[squad_index].used)
		{
			squad = &command_globals.squads[squad_index];
			break;
		}
	}
	if (!squad)
	{
		platform_log("command: spawn: the squads' table is full (%d)", COMMAND_MAXIMUM_SQUADS);
		return NULL;
	}
	csmemset(squad, 0, sizeof(*squad));
	squad->ai_goal = NONE;
	for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
	{
		squad->units[index] = NONE;
		squad->member_variants[index] = index < members ? variants[index] : NONE;
	}
	squad->used = TRUE;
	squad->side = side;
	squad->number = ++command_globals.next_number;
	squad->actor_variant_index = variants[0];
	leaf = strrchr(tag_get_name(variants[0]), '\\');
	snprintf(squad->name, sizeof(squad->name), "%s%s", leaf ? leaf + 1 : tag_get_name(variants[0]),
		variants[members - 1] != variants[0] ? " +" : "");
	squad->target_index = NONE;
	squad->point = *at;
	if (command_ground(at, &ground, &surface_index))
		squad->point = ground;
	squad->surface_index = surface_index;
	squad->order = _command_order_hold;
	squad->arrived = TRUE;
	command_squad_places(squad);
	for (index = 0; index < members; index++)
	{
		real_point3d place = squad->places[index];

		place.z += 0.5f;
		squad->units[index] = command_unit_spawn(variants[index], side, &place,
			(real)index * 0.7f + (side == _command_side_red ? 0.f : _pi));
	}
	ai_update_team_status();
	platform_log("command: squad %d (%s, %s) of %d spawned at %.2f %.2f %.2f, team %d", squad->number, squad->name,
		command_side_names[side], command_squad_living(squad), squad->point.x, squad->point.y, squad->point.z,
		command_spawn_team != NONE ? command_spawn_team : command_side_team(side));
	/* (the commander's first squad chosen) */
	if (command_squad_selected() == NULL && side == command_globals.side && command_globals.active)
		command_selection_set(1UL << (squad - command_globals.squads), "as it came");
	return squad;
}

static void command_squad_order(
	struct command_squad *squad,
	short order,
	real_point3d const *point,
	long surface_index,
	long target_index)
{
	real_point3d center;
	real distance = 0.f;

	squad->order = order;
	squad->point = *point;
	squad->surface_index = surface_index;
	/* (its point's surface, when the order gave none: the path searches'
	destination) */
	if (surface_index == NONE)
	{
		real_point3d ground;

		command_ground(point, &ground, &squad->surface_index);
	}
	squad->target_index = target_index;
	squad->order_time = game_time_get();
	squad->arrived = order == _command_order_hold;
	command_squad_places(squad);
	/* (a member's actor gone dormant - out of sight, nothing to do - woken:
	a dormant actor is not updated, its order never reached) */
	{
		short index;

		for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
		{
			struct unit_datum *unit = command_living_unit(squad->units[index]);

			if (unit && unit->unit.actor_index != NONE)
				actor_set_dormant(unit->unit.actor_index, FALSE);
		}
	}
	if (command_squad_center(squad, &center))
		distance = command_distance2d(&center, point);
	/* (attacking a unit: the squad sees it at once) */
	if (order == _command_order_attack && target_index != NONE)
	{
		short index;

		for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
		{
			struct unit_datum *unit = command_living_unit(squad->units[index]);

			if (unit && unit->unit.actor_index != NONE)
			{
				/* (as ai_magically_see_unit, for this actor: ai_script.c) */
				long prop_index = prop_get_base_by_unit_index(unit->unit.actor_index, target_index, TRUE, FALSE);

				if (prop_index != NONE)
					actor_handle_unit_effect(unit->unit.actor_index, prop_index, COMMAND_AI_UNIT_EFFECT_MAGIC_SIGHT);
			}
		}
	}
	platform_log("command: squad %d ordered %s to %.2f %.2f %.2f (surface %ld), %.1f wu from its centre, tick %ld",
		squad->number, command_order_log_names[order], point->x, point->y, point->z, surface_index, distance,
		(long)game_time_get());
	if (squad->hosted)
		return;
	/* (the commander's own squads' orders said: not the AI commander's) */
	if (!command_ai[squad->side].plays)
		command_event(squad->side, squad->side, _command_event_order, squad->hero ? 0 : squad->number, order);
}

/* (the objectives) a Spartan's flag run's goal now: the enemy flag where it
is, or, carrying it, his own stand; FALSE if no flag is in play. Whether a
point is at the enemy flag (an order there is a flag run) */
static boolean command_flag_goal(struct command_squad const *squad, real_point3d *goal);
static boolean command_flag_at(short side, real_point3d const *point);

/* (the log) how far each member has walked since its squad's order, the
most of them said on arriving: a way round is seen against the straight
distance the order's line gives */
static struct
{
	long order_time;
	real walked[COMMAND_SQUAD_SIZE];
	real_point3d last[COMMAND_SQUAD_SIZE];
	/* (a member that stopped) where, since when */
	real_point3d still[COMMAND_SQUAD_SIZE];
	long still_since[COMMAND_SQUAD_SIZE];
} command_squad_walks[COMMAND_MAXIMUM_SQUADS];
/* a member this near its place, standing (within a step) for this long,
has gone as near as it can */
#define COMMAND_STALLED_NEAR 14.f
#define COMMAND_STALLED_SECONDS 6
#define COMMAND_STALLED_STEP 0.75f

static real command_squad_walked(
	short squad_index)
{
	struct command_squad const *squad = &command_globals.squads[squad_index];
	short index;
	real most = 0.f;

	if (command_squad_walks[squad_index].order_time != squad->order_time)
	{
		command_squad_walks[squad_index].order_time = squad->order_time;
		csmemset(command_squad_walks[squad_index].walked, 0, sizeof(command_squad_walks[squad_index].walked));
		for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
		{
			if (command_living_unit(squad->units[index]))
				object_get_origin(squad->units[index], &command_squad_walks[squad_index].last[index]);
			command_squad_walks[squad_index].still[index] = command_squad_walks[squad_index].last[index];
			command_squad_walks[squad_index].still_since[index] = game_time_get();
		}
		return 0.f;
	}
	for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
	{
		real_point3d origin;

		if (!command_living_unit(squad->units[index]))
			continue;
		object_get_origin(squad->units[index], &origin);
		command_squad_walks[squad_index].walked[index] += command_distance2d(&origin, &command_squad_walks[squad_index].last[index]);
		command_squad_walks[squad_index].last[index] = origin;
		most = MAX(most, command_squad_walks[squad_index].walked[index]);
		if (command_distance2d(&origin, &command_squad_walks[squad_index].still[index]) > COMMAND_STALLED_STEP)
		{
			command_squad_walks[squad_index].still[index] = origin;
			command_squad_walks[squad_index].still_since[index] = game_time_get();
		}
	}
	return most;
}

static void command_squads_update(
	void)
{
	short squad_index;

	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad *squad = &command_globals.squads[squad_index];
		short index;
		boolean all_there = TRUE;
		short living = 0;
		real walked;

		if (!squad->used)
			continue;
		/* (attacking: the point follows the target while it lives) */
		if (squad->order == _command_order_attack)
		{
			if (command_living_unit(squad->target_index))
			{
				object_get_origin(squad->target_index, &squad->point);
				command_squad_places(squad);
			}
			else if (command_squad_of_unit(squad->target_index, NULL) &&
				command_squad_leader(command_squad_of_unit(squad->target_index, NULL)) != NONE)
			{
				/* (a squad's unit: on to the next of that squad) */
				struct command_squad *enemy = command_squad_of_unit(squad->target_index, NULL);

				squad->target_index = command_squad_leader(enemy);
				platform_log("command: squad %d: its target is dead: on to squad %d's next", squad->number, enemy->number);
			}
			else
			{
				platform_log("command: squad %d: its target is dead: holding", squad->number);
				squad->order = _command_order_hold;
				squad->arrived = TRUE;
				continue;
			}
		}
		/* (a flag run: the point follows the flag, or the way home) */
		if (squad->order == _command_order_flag)
		{
			real_point3d goal;

			if (!command_flag_goal(squad, &goal))
			{
				platform_log("command: squad %d: no flag in play: holding", squad->number);
				squad->order = _command_order_hold;
				squad->arrived = TRUE;
			}
			else if (distance3d(&goal, &squad->point) > 0.5f)
			{
				squad->point = goal;
				command_squad_places(squad);
			}
			continue;
		}
		if (squad->order != _command_order_move && squad->order != _command_order_advance)
			continue;
		walked = command_squad_walked(squad_index);
		for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
		{
			struct unit_datum *unit = command_living_unit(squad->units[index]);
			real_point3d origin;
			real_point3d place;

			if (!unit || unit->unit.player_index != NONE)
				continue;
			living++;
			object_get_origin(squad->units[index], &origin);
			place = squad->places[index];
			if (command_distance2d(&origin, &place) > COMMAND_ARRIVED)
			{
				/* (as near as its way goes: its actor's path ended there,
				a little short of a place on a slope or a rock's foot) */
				struct actor_datum const *actor = unit->unit.actor_index != NONE ? actor_get(unit->unit.actor_index) : NULL;

				if (!actor || !actor->control.path.at_destination || actor->control.moving ||
					command_distance2d(&origin, &place) > COMMAND_ARRIVED * 2.f)
				{
					/* (or as near as any way goes: near, and standing still
					for a while with no way on - a hill's top the AI's
					paths do not reach, a crowd on it) */
					if (command_distance2d(&origin, &place) > COMMAND_STALLED_NEAR ||
						game_time_get() - command_squad_walks[squad_index].still_since[index] < COMMAND_STALLED_SECONDS * TICKS_PER_SECOND)
					{
						all_there = FALSE;
					}
				}
			}
		}
		if (living && all_there)
		{
			squad->order = _command_order_hold;
			squad->arrived = TRUE;
			platform_log("command: squad %d arrived at %.2f %.2f %.2f after %.1f s, walked %.1f wu: holding", squad->number,
				squad->point.x, squad->point.y, squad->point.z,
				(real)(game_time_get() - squad->order_time) / (real)TICKS_PER_SECOND, walked);
		}
	}
}

/* ---- in and out of first person, and the Spartan's AI (the owner, Nov 5
and 6): first person is the commander's own Spartan alone, never an AI's
unit. Above the map, his Spartan fights on as AI: a free actor of a strong
infantry variant (COMMAND_SPARTAN_ACTOR: a veteran marine's behaviour, in
the Spartan's own body, weapons and colours) attached to it, the old
dropping in reversed; it is the squads' list's "Spartan", chosen and ordered
as a squad, and with no order it defends the base (HOLD at its rally point).
The combo takes the player back into it (players_set_local_player_unit, the
cheats' bump possession), its actor let go; leaving attaches a new one. While
he is above the map the player has a placeholder body (an unseen, unhurt
copy of the Spartan's own, ignored by the AI, at his first spawn): the game
has no player without a unit but a dead one, which it respawns. His
Spartan's death as AI is his own (player_died): the game respawns him at the
base (the economy's price on it later), and above the map the new Spartan
is the AI's again */

/* the Spartan's behaviour above the map: a veteran marine's (the campaign's
armoured marine), the actor variant's name's part */
#define COMMAND_SPARTAN_ACTOR "marine_armored assault rifle"
/* with no order it holds this far back from the base's rally point; taken
over by the AI it sees the enemies this near at once */
#define COMMAND_SPARTAN_DEFEND_BACK 5.f
/* its respawn's price (the first spawn free: spec 12b), and how long the
game's respawn takes when it dies as AI */
#define COMMAND_SPARTAN_COST 90
#define COMMAND_SPARTAN_RESPAWN_SECONDS 5
#define COMMAND_SPARTAN_AWARE 25.f

/* (the owner, Nov 7: the Spartan late in the game) each side's Spartan
bought at its HQ - from then on in the field or respawning (each respawn
paid); none at the start */
static long command_spartan_new(short side);
static boolean command_spartan_bought(short side);

static boolean command_rally_point(short side, real_point3d *point);
static boolean command_base_point(short side, real_point3d *point);
static struct command_squad *command_hero_squad(boolean make);

/* a unit and what it holds shown or not */
static void command_unit_set_visible(
	long unit_index,
	boolean visible)
{
	struct object_datum *object = object_try_and_get(unit_index);
	long child_index;

	if (!object)
		return;
	object_set_visibility(unit_index, visible);
	for (child_index = object->object.first_child_object_index; child_index != NONE;
		child_index = object_get(child_index)->object.next_object_index)
	{
		object_set_visibility(child_index, visible);
	}
}

/* a side's Spartan's entry in the squads' list (made if asked and there is
none), NULL if none: the commander's (red's), or the AI commander's */
static struct command_squad *command_side_hero_squad(
	short side,
	boolean make)
{
	short squad_index;
	struct command_squad *free_squad = NULL;

	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad *squad = &command_globals.squads[squad_index];

		if (squad->used && squad->hero && squad->side == side)
			return squad;
		if (!squad->used && !free_squad)
			free_squad = squad;
	}
	if (!make || !free_squad)
		return NULL;
	csmemset(free_squad, 0, sizeof(*free_squad));
	free_squad->ai_goal = NONE;
	for (squad_index = 0; squad_index < COMMAND_SQUAD_SIZE; squad_index++)
	{
		free_squad->units[squad_index] = NONE;
		free_squad->member_variants[squad_index] = NONE;
	}
	free_squad->used = TRUE;
	free_squad->hero = TRUE;
	free_squad->side = side;
	free_squad->number = 0;
	free_squad->actor_variant_index = command_actor_variant_find(COMMAND_SPARTAN_ACTOR);
	free_squad->target_index = NONE;
	free_squad->surface_index = NONE;
	free_squad->order = _command_order_none;
	snprintf(free_squad->name, sizeof(free_squad->name), "Spartan");
	return free_squad;
}

/* the commander's Spartan's entry (his side's) */
static struct command_squad *command_hero_squad(
	boolean make)
{
	return command_side_hero_squad(command_globals.side, make);
}

static void command_spartan_kit_trim(long spartan_index, short side);
static void command_spartan_loadout(long unit_index);

/* (the host) a player's unit set: his side's Spartan, or his seat above the
map. A local game's as the cheats' bump possession; a network game's as the
netcode gives a player the unit the host spawned (network_player_attach_unit):
his machine takes the host's word on his unit (network_distributed.c) */
void network_player_attach_unit(long player_index, long unit_index);

static void command_player_set_unit(
	long player_index,
	long unit_index)
{
	struct player_datum *player = player_get(player_index);
	long old_unit_index = player->unit_index;

	if (!command_network())
	{
		if (player->local_player_index != NONE)
			players_set_local_player_unit(player->local_player_index, unit_index);
		return;
	}
	if (old_unit_index != NONE && object_try_and_get_and_verify_type(old_unit_index, _object_mask_unit))
	{
		struct unit_datum *old = unit_get(old_unit_index);

		if (old->unit.player_index == player_index)
		{
			old->unit.player_index = NONE;
			unit_set_actively_controlled(old_unit_index, FALSE);
		}
	}
	if (unit_index == NONE)
	{
		player->unit_index = NONE;
		if (player->local_player_index != NONE)
			player_control_new_unit(player->local_player_index, NONE);
		return;
	}
	network_player_attach_unit(player_index, unit_index);
}

/* (the host) a commander's seat made ready: unseen, unhurt, the AI's actors
ignore it (each machine hides it: command_seats_hide) */
static void command_seat_prepare(
	long unit_index)
{
	struct unit_datum *seat = command_living_unit(unit_index);

	if (!seat)
		return;
	SET_FLAG(seat->object.damage_flags, _object_cannot_take_damage_bit, TRUE);
	SET_FLAG(seat->unit.flags, _unit_ignored_by_actors_bit, TRUE);
	command_unit_set_visible(unit_index, FALSE);
}

/* a biped for a Spartan or a seat: the players' own (a seat's or a
player's unit), NONE if none */
static long command_player_biped_definition(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	short slot;

	for (slot = 0; slot < COMMAND_PLAYERS; slot++)
	{
		struct unit_datum *seat = command_living_unit(command_seats[slot]);

		if (seat && seat->object.type == _object_type_biped)
			return seat->definition_index;
	}
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct unit_datum *unit = command_living_unit(player->unit_index);

		if (unit && unit->object.type == _object_type_biped)
			return unit->definition_index;
	}
	return NONE;
}

/* (the host) a seat made for a player where he first came in (his seat's
spot: a seat lost - none before -), NONE if it could not */

static long command_seat_new(
	long player_index,
	short side)
{
	short slot = command_player_slot(player_index);
	long definition_index = command_player_biped_definition();
	struct object_placement_data placement;
	long object_index;

	if (slot == NONE || definition_index == NONE)
		return NONE;
	object_placement_data_new(&placement, definition_index, NONE);
	SET_FLAG(placement.flags, _new_object_never_automatically_delete_bit, TRUE);
	if (command_seat_spot_valid[slot])
		placement.position = command_seat_spots[slot];
	else if (!command_base_point(side, &placement.position))
		return NONE;
	placement.position.z += 0.3f;
	object_index = object_new(&placement);
	if (object_index == NONE)
	{
		platform_log("command: player %d's seat could not be made", (int)slot);
		return NONE;
	}
	object_get(object_index)->object.owner_team_index = command_side_team(side);
	command_seat_prepare(object_index);
	command_seats[slot] = object_index;
	platform_log("command: player %d's seat %lx made at %.2f %.2f %.2f", (int)slot, (unsigned long)object_index,
		placement.position.x, placement.position.y, placement.position.z);
	return object_index;
}

/* the player at an absolute index (NONE: none) */
static long command_player_of_slot(
	short slot)
{
	struct data_iterator iterator;
	struct player_datum *player;

	if (slot == NONE)
		return NONE;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (command_player_slot(iterator.datum_index) == slot)
			return iterator.datum_index;
	}
	return NONE;
}

/* (the host) a side's Spartan given its AI: a free actor of
COMMAND_SPARTAN_ACTOR on it, its entry in the squads' list (defending the
base if it has no order) */
static void command_spartan_ai_attach(
	short side)
{
	long spartan_index = command_side_spartan(side);
	struct unit_datum *spartan = command_living_unit(spartan_index);
	struct command_squad *hero;
	short team = command_side_team(side);

	if (!spartan || spartan->unit.player_index != NONE)
		return;
	if (spartan->unit.actor_index == NONE)
	{
		long variant = command_actor_variant_find(COMMAND_SPARTAN_ACTOR);

		if (variant == NONE)
		{
			platform_log("command: Spartan AI: no actor variant named like \"%s\"", COMMAND_SPARTAN_ACTOR);
			return;
		}
		ai_scripting_attach_free(spartan_index, variant);
		spartan = unit_get(spartan_index);
		if (spartan->unit.actor_index != NONE)
		{
			struct actor_datum *actor = actor_get(spartan->unit.actor_index);

			actor->meta.team_index = team;
			actor->meta.force_active = TRUE;
			actor_set_dormant(spartan->unit.actor_index, FALSE);
		}
	}
	spartan->object.owner_player_index = NONE;
	hero = command_side_hero_squad(side, TRUE);
	if (hero)
	{
		hero->units[0] = spartan_index;
		/* (no order: it defends the base, a little back from the rally
		point toward it - the first squad stands on the rally point) */
		if (hero->order == _command_order_none)
		{
			real_point3d rally, base, defend, ground;

			if (command_rally_point(side, &rally) && command_base_point(side, &base))
			{
				real dx = base.x - rally.x, dy = base.y - rally.y;
				real length = (real)sqrt(dx * dx + dy * dy);

				defend = rally;
				if (length > 1.f)
				{
					defend.x += dx / length * COMMAND_SPARTAN_DEFEND_BACK;
					defend.y += dy / length * COMMAND_SPARTAN_DEFEND_BACK;
				}
				if (command_ground(&defend, &ground, NULL))
					defend = ground;
				command_squad_order(hero, _command_order_hold, &defend, NONE, NONE);
			}
		}
		else
			command_squad_places(hero);
	}
	/* (the fight it was in goes on: the enemies near it seen once its new
	actor has had its first update) */
	command_spartans[side].aware_time = game_time_get() + 2;
	ai_update_team_status();
	platform_log("command: Spartan AI: %s's Spartan (%lx) fights as AI (actor %lx)", command_side_names[side],
		(unsigned long)spartan_index, (unsigned long)spartan->unit.actor_index);
}

/* (a few ticks after a Spartan was given its AI) the enemies near it seen
at once, as an attack order's target: the fight it was in goes on (a new
actor knows nothing yet) */
static void command_spartan_aware(
	short side)
{
	long spartan_index = command_side_spartan(side);
	struct unit_datum *spartan = command_living_unit(spartan_index);
	long aware_time = command_spartans[side].aware_time;

	if (aware_time == NONE || game_time_get() < aware_time)
		return;
	if (!spartan || spartan->unit.player_index != NONE || spartan->unit.actor_index == NONE)
	{
		command_spartans[side].aware_time = NONE;
		return;
	}
	/* (a prop is made only for an active actor: a new one is active after
	its first updates - waited for, a few seconds at most) */
	if (!actor_get(spartan->unit.actor_index)->meta.active && game_time_get() < aware_time + 3 * TICKS_PER_SECOND)
		return;
	command_spartans[side].aware_time = NONE;
	{
		real_point3d at;
		short squad_index, member, seen = 0;

		object_get_origin(spartan_index, &at);
		for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
		{
			struct command_squad const *squad = &command_globals.squads[squad_index];

			if (!squad->used || squad->side == side)
				continue;
			for (member = 0; member < COMMAND_SQUAD_SIZE; member++)
			{
				real_point3d origin;
				long prop_index;

				if (!command_living_unit(squad->units[member]))
					continue;
				object_get_origin(squad->units[member], &origin);
				if (distance3d(&at, &origin) > COMMAND_SPARTAN_AWARE)
					continue;
				prop_index = prop_get_base_by_unit_index(spartan->unit.actor_index, squad->units[member], TRUE, FALSE);
				if (prop_index != NONE)
				{
					actor_handle_unit_effect(spartan->unit.actor_index, prop_index, COMMAND_AI_UNIT_EFFECT_MAGIC_SIGHT);
					seen++;
				}
			}
		}
		platform_log("command: Spartan AI: %s's sees the %d enemies within %.0f wu of it", command_side_names[side], seen,
			COMMAND_SPARTAN_AWARE);
	}
}

/* (the host) a commander into his side's Spartan, its actor let go: the
player's unit (his seat kept for his way back). FALSE if he can't (why: a
notice's words) */
static boolean command_possess(
	long player_index,
	char const **why)
{
	short side = command_player_side(player_index);
	short slot = command_player_slot(player_index);
	long spartan_index = command_side_spartan(side);
	struct unit_datum *spartan = command_living_unit(spartan_index);
	struct player_datum *player;

	*why = NULL;
	if (side == NONE || slot == NONE)
		return FALSE;
	player = player_get(player_index);
	if (!spartan)
	{
		*why = command_globals.game_running && !command_spartan_owned[side] ? N_("No Spartan yet: the HQ sells one") :
			N_("Your Spartan is respawning");
		return FALSE;
	}
	if (player->unit_index == spartan_index)
		return TRUE;
	if (command_spartans[side].possessor != NONE || spartan->unit.player_index != NONE)
	{
		*why = N_("Another commander is in your Spartan");
		return FALSE;
	}
	if (!command_living_unit(player->unit_index) || player->unit_index != command_seats[slot])
	{
		*why = N_("Your Spartan is respawning");
		return FALSE;
	}
	if (spartan->unit.actor_index != NONE)
		actor_delete(spartan->unit.actor_index, FALSE);
	command_spartan_kit_trim(spartan_index, side);
	command_player_set_unit(player_index, spartan_index);
	command_spartans[side].possessor = slot;
	command_spartans[side].aware_time = NONE;
	platform_log("command: in: player %d in %s's Spartan (%lx), first person", (int)slot, command_side_names[side],
		(unsigned long)spartan_index);
	return TRUE;
}

/* (the host) a side's Spartan's commander back above the map, in his seat;
the Spartan AI again if it lives */
static void command_unpossess(
	short side,
	char const *why)
{
	short slot = command_spartans[side].possessor;
	long spartan_index = command_side_spartan(side);
	long player_index = command_player_of_slot(slot);

	if (slot == NONE)
		return;
	command_spartans[side].possessor = NONE;
	if (player_index != NONE && player_get(player_index)->unit_index == spartan_index && spartan_index != NONE)
	{
		long seat_index = command_living_unit(command_seats[slot]) ? command_seats[slot] : command_seat_new(player_index, side);

		command_player_set_unit(player_index, seat_index);
	}
	platform_log("command: out (%s): player %d left %s's Spartan, which %s", why, (int)slot, command_side_names[side],
		command_living_unit(spartan_index) ? "fights on as AI" : "is dead");
	if (command_living_unit(spartan_index))
		command_spartan_ai_attach(side);
}

/* (the host, each frame of a Command game) the humans: each one's new unit
(his spawn) his seat, the side's Spartan's commander gone or dead let go,
each side commanded by a human or the AI commander */
static void command_humans_update(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	boolean humans[NUMBER_OF_COMMAND_SIDES] = { FALSE, FALSE };
	short side;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long player_index = iterator.datum_index;
		short slot = command_player_slot(player_index);
		long unit_index = player->unit_index;
		boolean leaving = player->quit_out_of_game || player->quit_out_of_game_time != NONE;

		side = command_player_side(player_index);
		if (slot == NONE || side == NONE)
			continue;
		if (!leaving)
			humans[side] = TRUE;
		/* (leaving the game: his Spartan the AI's before the game kills
		his unit) */
		if (leaving)
		{
			if (command_spartans[side].possessor == slot)
				command_unpossess(side, "the player is leaving");
			continue;
		}
		if (!command_living_unit(unit_index))
		{
			/* (his Spartan dead under him: back in his seat at once, not
			the game's respawn) */
			if (command_spartans[side].possessor == slot)
			{
				command_spartans[side].possessor = NONE;
				if (command_living_unit(command_seats[slot]))
				{
					command_player_set_unit(player_index, command_seats[slot]);
					player->respawn_timer = 0;
				}
				platform_log("command: player %d's Spartan died under him: back above the map", (int)slot);
			}
			continue;
		}
		if (unit_index == command_side_spartan(side) && command_spartans[side].possessor == slot)
			continue;
		if (unit_index == command_seats[slot])
			continue;
		/* (a unit of his the game gave him - his first spawn, or a respawn -:
		his seat now, the last one gone) */
		if (command_living_unit(command_seats[slot]) && command_seats[slot] != command_side_spartan(_command_side_red) &&
			command_seats[slot] != command_side_spartan(_command_side_blue))
		{
			object_delete(command_seats[slot]);
		}
		if (command_spartans[side].possessor == slot)
			command_spartans[side].possessor = NONE;
		command_seats[slot] = unit_index;
		object_get_origin(unit_index, &command_seat_spots[slot]);
		command_seat_spot_valid[slot] = TRUE;
		command_seat_prepare(unit_index);
		platform_log("command: player %d (%s) above the map in his seat %lx", (int)slot, command_side_names[side],
			(unsigned long)unit_index);
	}
	for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
	{
		short slot = command_spartans[side].possessor;

		/* (its commander gone from the game) */
		if (slot != NONE && command_player_of_slot(slot) == NONE)
			command_unpossess(side, "the player is gone");
#ifndef HALO_RELEASE
		/* (tests: "aiside <side> play") */
		if (command_ai_test_play[side])
			humans[side] = FALSE;
#endif
		if (command_ai[side].plays == humans[side])
		{
			command_ai[side].plays = !humans[side];
			command_ai[side].next_think_time = game_time_get() + TICKS_PER_SECOND;
			platform_log("command: %s is commanded by %s", command_side_names[side], humans[side] ? "its human players" :
				"the AI commander");
			if (command_globals.game_running && command_globals.active && side == command_globals.side)
				command_notice("%s", humans[side] ? T("You command your side") : T("The AI commands your side"));
			else if (command_globals.game_running && command_globals.active && !humans[side])
				command_notice("%s", T("The AI commands the other side"));
		}
	}
}

/* (a player leaving the game, game.c, before his unit is killed) his
Spartan the AI's again */
void command_player_quitting(
	long player_index)
{
	short side = command_player_side(player_index);
	short slot = command_player_slot(player_index);

	if (!command_globals.game_running || command_client() || side == NONE || slot == NONE)
		return;
	if (command_spartans[side].possessor == slot)
		command_unpossess(side, "the player quit");
}

/* (each frame, every machine) every commander's seat unseen */
static void command_seats_hide(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct object_datum *object = object_try_and_get(player->unit_index);

		if (object && !TEST_FLAG(object->object.damage_flags, _object_dead_bit) && command_unit_is_seat(player->unit_index))
			command_unit_set_visible(player->unit_index, FALSE);
	}
}

/* the combo above the map: into his Spartan (the host's to do: a client
asks it) */
static void command_net_request_possess(boolean in);

static boolean command_spartan_in(
	void)
{
	char const *why = NULL;

	if (command_client())
	{
		command_net_request_possess(TRUE);
		return TRUE;
	}
	if (!command_possess(command_player_index(), &why))
	{
		platform_log("command: in: %s", why ? why : "not now");
		if (why)
			command_notice("%s", T(why));
		return FALSE;
	}
	return TRUE;
}

/* the combo in first person: back above the map (the host's to do) */
static void command_leave(
	char const *why)
{
	if (command_globals.possessed_unit_index == NONE)
		return;
	if (command_client())
	{
		command_net_request_possess(FALSE);
		return;
	}
	command_unpossess(command_globals.side, why);
}

/* (each frame) the view follows this machine's player's unit: his side's
Spartan in first person, above the map otherwise */
static void command_view_follow(
	void)
{
	long player_index = command_player_index();
	long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;
	boolean in = unit_index != NONE && unit_index == command_my_spartan() && command_living_unit(unit_index);

	if (in && command_globals.possessed_unit_index != unit_index)
	{
		command_globals.possessed_unit_index = unit_index;
		command_globals.possessed_squad = NONE;
		director_forge_camera(command_globals.local_player_index, FALSE, NULL, NULL);
		command_globals.camera_placed = FALSE;
		command_globals.possessed_hint_until = system_milliseconds() + COMMAND_POSSESSED_HINT_MILLISECONDS;
		platform_log("command: in: first person in the Spartan (%lx)", (unsigned long)unit_index);
	}
	else if (!in && command_globals.possessed_unit_index != NONE)
	{
		long was = command_globals.possessed_unit_index;

		command_globals.possessed_unit_index = NONE;
		command_globals.possessed_squad = NONE;
		platform_log("command: out: above the map again (the Spartan %s)", command_living_unit(was) ? "fights on as AI" : "is dead");
		if (command_living_unit(was))
			object_get_origin(was, &command_globals.focus);
		command_camera_place();
	}
}

/* the commander's Spartan's loadout (the spec's 12b, one table: every
spawn's; the armory upgrade swaps the secondary): the game's own items, the
map's tags */
static struct
{
	char const *primary;
	char const *secondary;
	char fragmentation_grenades;
	char plasma_grenades;
	boolean overshield;
} const command_hero_loadout =
{
	"weapons\\assault rifle\\assault rifle", "weapons\\pistol\\pistol", 4, 4, TRUE,
};

static char const *command_spartan_secondary(short side);

static long command_weapon_tag_find(
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

		if (tag_get_group_tag(tag_index) == WEAPON_DEFINITION_TAG && tag_length >= length &&
			!strcmp(tag_name + tag_length - length, name))
		{
			return tag_index;
		}
	}
	return NONE;
}

static void command_spartan_loadout(
	long unit_index)
{
	char const *weapons[2];
	short index;
	struct unit_datum *unit;

	weapons[0] = command_hero_loadout.primary;
	/* (his side's armory's: blue's AI commander's Spartan by his team) */
	weapons[1] = command_spartan_secondary(object_get(unit_index)->object.owner_team_index == command_side_team(_command_side_blue) ?
		_command_side_blue : _command_side_red);
	unit_delete_all_weapons(unit_index);
	/* (the secondary first: the primary is in hand) */
	for (index = 1; index >= 0; index--)
	{
		long tag_index = command_weapon_tag_find(weapons[index]);
		struct object_placement_data placement;
		long weapon_index;

		if (tag_index == NONE)
		{
			platform_log("command: loadout: no %s in the map's tags", weapons[index]);
			continue;
		}
		object_placement_data_new(&placement, tag_index, unit_index);
		weapon_index = object_new(&placement);
		if (weapon_index != NONE && !unit_add_weapon_to_inventory(unit_index, weapon_index, TRUE))
			object_delete(weapon_index);
	}
	unit = unit_get(unit_index);
	unit->unit.grenade_counts[_unit_grenade_human_fragmentation] = command_hero_loadout.fragmentation_grenades;
	unit->unit.grenade_counts[_unit_grenade_covenant_plasma] = command_hero_loadout.plasma_grenades;
	if (command_hero_loadout.overshield)
		object_double_charge_shield(unit_index);
	platform_log("command: loadout: the Spartan with %s and %s, %d frag and %d plasma grenades%s", weapons[0], weapons[1],
		command_hero_loadout.fragmentation_grenades, command_hero_loadout.plasma_grenades,
		command_hero_loadout.overshield ? ", an overshield" : "");
}

/* (C8: the hero kit as a player holds it - two weapons -) what the AI
picked up beside them thrown down, as he is taken back */
void unit_drop_weapon_item(long unit_index, long item_index);

static void command_spartan_kit_trim(
	long spartan_index,
	short side)
{
	{
		short slot, held = 0;

		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			if (unit_inventory_get_weapon(spartan_index, slot) != NONE)
				held++;
		}
		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT && held > 2; slot++)
		{
			long weapon_index = unit_inventory_get_weapon(spartan_index, slot);

			if (weapon_index == NONE || slot == unit_get(spartan_index)->unit.current_weapon_index)
				continue;
			if (command_weapon_tag_find(command_hero_loadout.primary) == object_get(weapon_index)->definition_index ||
				command_weapon_tag_find(command_spartan_secondary(side)) == object_get(weapon_index)->definition_index)
			{
				continue;
			}
			platform_log("command: Spartan AI: %s it picked up thrown down (a player holds two)",
				tag_get_name(object_get(weapon_index)->definition_index));
			unit_drop_weapon_item(spartan_index, weapon_index);
			held--;
		}
	}
}

/* a Spartan in his side's colour, as a team game colours its players
(game_engine_player_get_change_color: red, blue), the colours a function
drives (the shield's glow) left as they are (players_coop_color_unit) */
static void command_unit_team_color(
	long unit_index,
	short side)
{
	struct object_datum *object = object_try_and_get(unit_index);
	struct object_definition *definition;
	real_rgb_color const *color = side == _command_side_red ? global_real_rgb_red : global_real_rgb_blue;
	short index;

	if (!object)
		return;
	definition = object_definition_get(object->definition_index);
	for (index = 0; index < definition->object.change_colors.count && index < NUMBER_OF_OBJECT_CHANGE_COLORS; index++)
	{
		struct object_change_color_definition *change_color = TAG_BLOCK_GET_ELEMENT(
			&definition->object.change_colors, index, struct object_change_color_definition);

		if (change_color->permutations.count > 0 && !change_color->scaled_by && !change_color->darken_by)
		{
			object->object.base_change_colors[index] = *color;
			object->object.outgoing_change_colors[index] = *color;
		}
	}
}

/* a side's Spartan made at its rally point (the players' biped, his side's
colour, the hero loadout): NONE if it could not */
static long command_spartan_new(
	short side)
{
	long definition_index = command_player_biped_definition();
	struct object_placement_data placement;
	real_point3d rally;
	long unit_index;

	if (definition_index == NONE || !command_rally_point(side, &rally))
		return NONE;
	object_placement_data_new(&placement, definition_index, NONE);
	placement.position = rally;
	placement.position.z += 0.6f;
	placement_data_set_change_color(&placement, side == _command_side_red ? global_real_rgb_red : global_real_rgb_blue);
	unit_index = object_new(&placement);
	if (unit_index == NONE)
		return NONE;
	object_get(unit_index)->object.owner_team_index = command_side_team(side);
	command_unit_team_color(unit_index, side);
	command_spartan_loadout(unit_index);
	return unit_index;
}

/* (C9) a side's HQ bought its Spartan: made at its base by
command_side_spartan_update (the first one free), the AI's until a
commander of the side takes it (the combo) */
static boolean command_spartan_bought(
	short side)
{
	command_spartan_owned[side] = TRUE;
	command_spartans[side].paid = FALSE;
	command_spartans[side].respawn_time = game_time_get();
	return TRUE;
}

/* ---- the controls */

static void command_pad_get(
	struct command_pad *pad)
{
	long player_index = command_player_index();
	short button;
	short stick;

	csmemset(pad, 0, sizeof(*pad));
	if (player_index != NONE)
	{
		short gamepad_index = player_get(player_index)->local_player_index;

		if (gamepad_index != NONE && input_has_gamepad(gamepad_index))
		{
			struct gamepad_state const *gamepad = input_get_gamepad_state(gamepad_index);
			float yaw, pitch;

			for (button = 0; button < NUMBER_OF_GAMEPAD_BUTTONS; button++)
				pad->down[button] = gamepad->buttons[button] != 0;
			for (stick = 0; stick < NUMBER_OF_GAMEPAD_STICKS; stick++)
			{
				pad->sticks[stick][0] = (real)gamepad->sticks[stick].x / 32767.f;
				pad->sticks[stick][1] = (real)gamepad->sticks[stick].y / 32767.f;
			}
			if (halo_linux_mouse_look(gamepad_index, &yaw, &pitch))
			{
				pad->look_yaw = yaw;
				pad->look_pitch = pitch;
			}
			if (gamepad_index == 0)
			{
				float wheel = 0.f;

				pad->keys = halo_linux_forge_keys(&wheel);
				pad->wheel = wheel;
			}
		}
	}
	pad->toggle = forge_combo_held || (pad->keys & COMMAND_KEY_TAB) != 0;
	/* (the keyboard's own keys of command mode stand for none of the pad's
	buttons they are bound to in play: Q the flashlight's, E the action's,
	H the grenades') */
	if (pad->keys & COMMAND_KEY_Q)
		pad->down[_gamepad_analog_button_white] = FALSE;
	if (pad->keys & COMMAND_KEY_E)
		pad->down[_gamepad_analog_button_x] = FALSE;
	if (pad->keys & COMMAND_KEY_H)
		pad->down[_gamepad_analog_button_black] = FALSE;
	if (command_globals.test_buttons_frames > 0)
	{
		for (button = 0; button < NUMBER_OF_GAMEPAD_BUTTONS; button++)
			pad->down[button] |= command_globals.test_down[button];
		pad->toggle |= command_globals.test_toggle;
	}
	if (command_globals.test_sticks_frames > 0)
	{
		for (stick = 0; stick < NUMBER_OF_GAMEPAD_STICKS; stick++)
		{
			pad->sticks[stick][0] = command_globals.test_sticks[stick][0];
			pad->sticks[stick][1] = command_globals.test_sticks[stick][1];
		}
	}
	for (stick = 0; stick < NUMBER_OF_GAMEPAD_STICKS; stick++)
	{
		short axis;

		for (axis = 0; axis < 2; axis++)
		{
			real value = pad->sticks[stick][axis];

			if (value > -COMMAND_STICK_DEAD_ZONE && value < COMMAND_STICK_DEAD_ZONE)
				value = 0.f;
			pad->sticks[stick][axis] = PIN(value, -1.f, 1.f);
		}
	}
}

/* ---- the selection: a set of the commander's squads (a bit each, the
table's order), the first of them the one dropped into and logged */

static boolean command_squad_mine(
	struct command_squad const *squad)
{
	return squad->used && squad->side == command_globals.side && !squad->airborne && command_squad_living(squad) > 0;
}

/* (C12, the owner's idea) a squad chosen or ordered by the commander: one
of its members says a line of its own - Halo CE's unit dialogue from the
maps' tags (a Marine's, a Grunt's), the vocalization the game has for it
(COMMAND_SPEAK_*: the first of each list the unit's dialogue has - a
Grunt's has no "sighted friend player" -), not every time: a chosen line
COMMAND_SPEAK_SECONDS after any line, an order's line COMMAND_SPEAK_SECONDS
after the last order's (chosen then ordered at once: both heard, the order's
queued after the chosen one - it was never heard) */
#define COMMAND_SPEAK_SECONDS 4
#define COMMAND_SPEAK_LINES 4
static char const *const command_speak_chosen[COMMAND_SPEAK_LINES] = { "sighted friend player", "alert friend", "taunt", NULL };
static char const *const command_speak_move[COMMAND_SPEAK_LINES] = { "advance", "search start", "uncover start", "alert friend" };
static char const *const command_speak_hold[COMMAND_SPEAK_LINES] = { "cover", "hiding finished", "search abandon", NULL };
#define COMMAND_SPEAK_CHOSEN command_speak_chosen
#define COMMAND_SPEAK_MOVE command_speak_move
#define COMMAND_SPEAK_HOLD command_speak_hold
static long command_speak_last_time = NONE;
static long command_speak_last_order_time = NONE;

static void command_squad_speak(
	struct command_squad const *squad,
	char const *const *vocalizations)
{
	short index;
	short line;
	boolean order = vocalizations != command_speak_chosen;
	long now = game_time_get();

	if (!squad)
		return;
	/* (a new game: its time from 0 again) */
	if (command_speak_last_time != NONE && now < command_speak_last_time)
		command_speak_last_time = command_speak_last_order_time = NONE;
	if (order)
	{
		if (command_speak_last_order_time != NONE && now < command_speak_last_order_time + COMMAND_SPEAK_SECONDS * TICKS_PER_SECOND)
			return;
	}
	else if (command_speak_last_time != NONE && now < command_speak_last_time + COMMAND_SPEAK_SECONDS * TICKS_PER_SECOND)
		return;
	for (line = 0; line < COMMAND_SPEAK_LINES && vocalizations[line]; line++)
	{
		short wanted = dialogue_get_vocalization_type_by_name(vocalizations[line]);

		if (wanted == NONE)
			continue;
		for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
		{
			struct unit_datum *unit = command_living_unit(squad->units[index]);
			short vocalization_type = wanted;
			long sound_definition_index = NONE;
			short speech_type;

			if (!unit || unit->unit.player_index != NONE)
				continue;
			speech_type = unit_test_speech(squad->units[index], _unit_speech_scripted, TRUE, TRUE, NULL, &vocalization_type,
				&sound_definition_index);
			if (speech_type > 0 && sound_definition_index != NONE)
			{
				struct unit_speech_item speech_item;

				csmemset(&speech_item, 0, sizeof(speech_item));
				speech_item.vocalization_type = vocalization_type;
				speech_item.sound_definition_index = sound_definition_index;
				speech_item.priority = _unit_speech_scripted;
				ai_communication_packet_new(&speech_item.ai);
				unit_speak(squad->units[index], speech_type, &speech_item);
				command_speak_last_time = now;
				if (order)
					command_speak_last_order_time = now;
				platform_log("command: squad %d's unit %d says \"%s\" (%s)", squad->number, index + 1, vocalizations[line],
					tag_get_name(sound_definition_index));
				return;
			}
		}
	}
	platform_log("command: squad %d has no line for \"%s\"", squad->number, vocalizations[0]);
}

static void command_selection_set(
	unsigned long mask,
	char const *how)
{
	short index;
	short count = 0;
	char numbers[64];
	int length = 0;

	numbers[0] = 0;
	command_globals.selection = 0;
	command_globals.selected = NONE;
	for (index = 0; index < COMMAND_MAXIMUM_SQUADS; index++)
	{
		if (!(mask & (1UL << index)) || !command_squad_mine(&command_globals.squads[index]))
			continue;
		command_globals.selection |= 1UL << index;
		if (command_globals.selected == NONE)
			command_globals.selected = index;
		count++;
		if (length < (int)sizeof(numbers) - 4)
			length += snprintf(numbers + length, sizeof(numbers) - (size_t)length, "%s%d", length ? " " : "",
				command_globals.squads[index].number);
	}
	platform_log("command: chosen %s: %d squads (%s)", how, count, numbers[0] ? numbers : "none");
	/* (the commander's own choice: the first chosen answers, C12) */
	if (count > 0 && strcmp(how, "as it came") && strncmp(how, "with ", 5))
		command_squad_speak(&command_globals.squads[command_globals.selected], COMMAND_SPEAK_CHOSEN);
	if (count == 1 && command_globals.squads[command_globals.selected].hero)
		command_notice("%s", T("Spartan"));
	else if (count == 1)
		command_notice(T("Squad %d"), command_globals.squads[command_globals.selected].number);
	else if (count > 1)
		command_notice(T("%d squads"), count);
}

static boolean command_squad_is_selected(
	struct command_squad const *squad)
{
	return (command_globals.selection & (1UL << (squad - command_globals.squads))) != 0;
}

/* the commander's squads with a living unit within radius of point */
static unsigned long command_squads_near(
	real_point3d const *point,
	real radius,
	boolean nearest_only)
{
	short squad_index;
	short best = NONE;
	real best_distance = radius;
	unsigned long mask = 0;

	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad *squad = &command_globals.squads[squad_index];
		short index;

		if (!command_squad_mine(squad))
			continue;
		for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
		{
			real_point3d origin;
			real distance;

			if (!command_living_unit(squad->units[index]))
				continue;
			object_get_origin(squad->units[index], &origin);
			distance = command_distance2d(&origin, point);
			if (distance <= radius)
				mask |= 1UL << squad_index;
			if (distance < best_distance)
			{
				best_distance = distance;
				best = squad_index;
			}
		}
	}
	if (nearest_only)
		return best != NONE ? 1UL << best : 0;
	return mask;
}

/* A, a tap: the squad nearest the cursor (within COMMAND_SELECT_RADIUS),
else none */
static void command_select_at_cursor(
	void)
{
	command_selection_set(command_globals.cursor_valid ?
		command_squads_near(&command_globals.cursor, COMMAND_SELECT_RADIUS, TRUE) : 0, "at the cursor");
}

/* the white button: every squad of the commander's */
static void command_select_all(
	void)
{
	command_selection_set(~0UL, "all");
}

/* a squad's part of an order given to several: its point a ring's place
about the one given (the first in the middle), so that they do not crowd
one spot */
static void command_order_point(
	real_point3d const *point,
	short order_index,
	real_point3d *squad_point,
	long *surface_index)
{
	real_point3d center = *point;
	long center_surface_index = *surface_index;

	*squad_point = *point;
	if (order_index > 0)
	{
		real angle = (real)(order_index - 1) * 1.2566371f;
		real ring = 5.f + 2.f * (real)((order_index - 1) / 5);

		squad_point->x += ring * (real)cos(angle);
		squad_point->y += ring * (real)sin(angle);
	}
	/* (on the order's point's ground, as a member's place) */
	if (center_surface_index == NONE)
		command_ground(point, &center, &center_surface_index);
	command_place_ground(&center, center_surface_index, squad_point, COMMAND_PLACE_DROP, squad_point, surface_index);
}

/* (the host) a side's squads (a bit each) ordered at a point: an enemy
unit there attacked, else moved there (a Spartan onto the enemy flag: its
flag run). Only the side's squads that may be ordered (down, not a Shade's
crew) take it; how many did */
static short command_order_squads(
	short side,
	unsigned long mask,
	real_point3d const *point,
	long surface_index,
	long unit_index)
{
	struct unit_datum *target = command_living_unit(unit_index);
	struct command_squad *target_squad = command_squad_of_unit(unit_index, NULL);
	short squad_index;
	short order_index = 0;
	short first = NONE;
	boolean enemy;

	enemy = target && (target_squad ? target_squad->side != side : target->object.owner_team_index != command_side_team(side));
	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad *squad = &command_globals.squads[squad_index];

		if (!(mask & (1UL << squad_index)) || !squad->used || squad->side != side || squad->airborne ||
			squad->hosted || command_squad_living(squad) <= 0)
		{
			continue;
		}
		if (first == NONE)
			first = squad_index;
		if (enemy)
		{
			command_squad_order(squad, _command_order_attack, point, NONE, unit_index);
		}
		/* (a Spartan ordered onto the enemy flag: a flag run) */
		else if (squad->hero && command_flag_at(!side, point))
		{
			real_point3d goal;

			if (command_flag_goal(squad, &goal))
				command_squad_order(squad, _command_order_flag, &goal, NONE, NONE);
		}
		else
		{
			real_point3d squad_point;
			long squad_surface = surface_index;

			if (order_index == 0)
			{
				squad_point = *point;
				if (squad_surface == NONE)
					command_ground(point, &squad_point, &squad_surface);
			}
			else
			{
				command_order_point(point, order_index, &squad_point, &squad_surface);
			}
			command_squad_order(squad, _command_order_move, &squad_point, squad_surface, NONE);
		}
		order_index++;
	}
	/* (one of the first ordered answers, C12) */
	if (first != NONE)
		command_squad_speak(&command_globals.squads[first], COMMAND_SPEAK_MOVE);
	return order_index;
}

/* (the host) a side's squads (a bit each) hold where each stands */
static short command_hold_squads(
	short side,
	unsigned long mask)
{
	short squad_index;
	short count = 0;
	short first = NONE;

	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad *squad = &command_globals.squads[squad_index];
		real_point3d center;
		real_point3d ground;
		long surface_index = NONE;

		if (!(mask & (1UL << squad_index)) || !squad->used || squad->side != side || squad->airborne ||
			squad->hosted || !command_squad_center(squad, &center))
		{
			continue;
		}
		ground = center;
		command_ground(&center, &ground, &surface_index);
		command_squad_order(squad, _command_order_hold, &ground, surface_index, NONE);
		if (first == NONE)
			first = squad_index;
		count++;
	}
	if (first != NONE)
		command_squad_speak(&command_globals.squads[first], COMMAND_SPEAK_HOLD);
	return count;
}

/* (network play, command_net.inc) a client's orders and buys asked of the
host */
static void command_net_request_order(unsigned long mask, real_point3d const *point, long unit_index);
static void command_net_request_hold(unsigned long mask);
static void command_net_request_buy(short row);

/* R: the squads chosen ordered at the cursor (or point) */
static void command_order_at(
	real_point3d const *point,
	long surface_index,
	long unit_index)
{
	if (command_client())
		command_net_request_order(command_globals.selection, point, unit_index);
	else
		command_order_squads(command_globals.side, command_globals.selection, point, surface_index, unit_index);
}

static void command_order_at_cursor(
	void)
{
	if (!command_globals.cursor_valid)
		return;
	command_order_at(&command_globals.cursor, command_globals.cursor_surface_index, command_globals.cursor_unit_index);
}

/* X: the squads chosen hold where each stands */
static void command_hold_here(
	void)
{
	if (command_client())
		command_net_request_hold(command_globals.selection);
	else
		command_hold_squads(command_globals.side, command_globals.selection);
}

/* ---- the mouse's pointer (the desktop: the keyboard and mouse's player,
the button icons the keyboard's; a pad's player has the cursor at the
centre). ui_widget.c hands it here while no menu is up and the view from
above is: a left click chooses the squad under it, a left drag a box of
them, a right click orders the squads chosen there, the screen's edges
move the view */

#define COMMAND_EDGE_PIXELS 6.f
#define COMMAND_DRAG_PIXELS 8.f

static boolean command_pointer_mode(
	void)
{
#ifdef COMMAND_BUTTON_ICONS
	return halo_button_icons_shown() == _halo_button_icons_keyboard;
#else
	return FALSE;
#endif
}

boolean command_pointer_wanted(
	void)
{
	return command_globals.active && command_globals.possessed_unit_index == NONE && command_globals.camera_placed &&
		!game_time_get_paused() && command_pointer_mode();
}

/* the level and the unit under a point of the screen (the pointer's) */
static boolean command_screen_pick(
	real x,
	real y,
	real_point3d *point,
	long *surface_index,
	long *unit_index)
{
	real_point2d screen;
	real_point3d origin;
	real_vector3d direction;
	struct collision_result collision;

	*unit_index = NONE;
	*surface_index = NONE;
	if (!command_globals.view_valid)
		return FALSE;
	/* (render_camera_screen_to_view takes the projection's -1 to 1, as
render_camera_view_to_screen makes them before the viewport's pixels) */
	{
		rectangle2d const *viewport = &command_globals.view_camera.viewport_bounds;
		real width = (real)(viewport->x1 - viewport->x0);
		real height = (real)(viewport->y1 - viewport->y0);

		if (width <= 0.f || height <= 0.f)
			return FALSE;
		screen.x = (x - (real)viewport->x0) / width * 2.f - 1.f;
		screen.y = (y - (real)viewport->y0) / height * 2.f - 1.f;
	}
	render_camera_screen_to_world(&command_globals.view_camera, &command_globals.view_frustum, &screen, &origin,
		&direction);
	normalize3d(&direction);
	direction.i *= COMMAND_PICK_RANGE;
	direction.j *= COMMAND_PICK_RANGE;
	direction.k *= COMMAND_PICK_RANGE;
	if (!collision_test_vector(
			FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
			FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_bipeds_bit) |
			FLAG(_collision_test_objects_vehicles_bit),
			&command_globals.view_camera.position, &direction, command_my_spartan(), &collision))
	{
		return FALSE;
	}
	*point = collision.point;
	if (collision.type == _collision_result_structure)
		*surface_index = collision.surface_index;
	else if (collision.type == _collision_result_object && object_try_and_get(collision.object_index) &&
		command_living_unit(object_get_ultimate_parent(collision.object_index)))
	{
		*unit_index = object_get_ultimate_parent(collision.object_index);
	}
	return TRUE;
}

/* the commander's squads with a living unit inside the screen's box */
static unsigned long command_squads_in_box(
	real x0,
	real y0,
	real x1,
	real y1)
{
	short squad_index;
	unsigned long mask = 0;
	real left = MIN(x0, x1), right = MAX(x0, x1), top = MIN(y0, y1), bottom = MAX(y0, y1);

	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad *squad = &command_globals.squads[squad_index];
		short index;

		if (!command_squad_mine(squad))
			continue;
		for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
		{
			real_point3d origin;
			real_point2d screen;

			if (!command_living_unit(squad->units[index]))
				continue;
			object_get_origin(squad->units[index], &origin);
			origin.z += 0.6f;
			if (command_globals.view_valid &&
				render_camera_world_to_screen(&command_globals.view_camera, &command_globals.view_frustum, &origin, &screen) &&
				screen.x >= left && screen.x <= right && screen.y >= top && screen.y <= bottom)
			{
				mask |= 1UL << squad_index;
			}
		}
	}
	return mask;
}

/* the pointer's motion and clicks since the last frame (present: what
ui_widget.c read), in the menus' 640x480 coordinates */
static void command_pointer_take(struct halo_ui_pointer const *pointer, boolean present, boolean test);

void command_pointer_update(
	struct halo_ui_pointer const *pointer,
	boolean present)
{
	command_pointer_take(pointer, present, FALSE);
}

static void command_pointer_take(
	struct halo_ui_pointer const *pointer,
	boolean present,
	boolean test)
{
	rectangle2d const *viewport = &command_globals.view_camera.viewport_bounds;
	real offset = (real)(viewport->x1 - viewport->x0 - 640) * 0.5f;
	real x, y;

	/* (tests: ui_widget.c's real pointer each frame, not over the test's) */
	if (command_globals.test_pointer_frames > 0 && !test)
	{
		command_globals.test_pointer_frames--;
		return;
	}
	if (!present || !command_pointer_wanted() || !command_globals.view_valid)
	{
		command_globals.pointer_valid = FALSE;
		command_globals.drag_down = FALSE;
		return;
	}
	x = (real)viewport->x0 + offset + (real)pointer->x;
	y = (real)viewport->y0 + (real)pointer->y;
	command_globals.pointer_valid = TRUE;
	command_globals.pointer_x = x;
	command_globals.pointer_y = y;
	/* (a left press: a drag begins where it went down) */
	if (pointer->left_clicks && !command_globals.drag_down)
	{
		command_globals.drag_down = TRUE;
		command_globals.drag_x = (real)viewport->x0 + offset + (real)pointer->click_x;
		command_globals.drag_y = (real)viewport->y0 + (real)pointer->click_y;
	}
	/* (let go: a box if it was dragged, else a click) */
	if (command_globals.drag_down && !pointer->held)
	{
		command_globals.drag_down = FALSE;
		if (fabs(x - command_globals.drag_x) > COMMAND_DRAG_PIXELS || fabs(y - command_globals.drag_y) > COMMAND_DRAG_PIXELS)
		{
			command_selection_set(command_squads_in_box(command_globals.drag_x, command_globals.drag_y, x, y), "by the box");
		}
		else
		{
			real_point3d point;
			long surface_index, unit_index;
			struct command_squad *squad;

			if (command_screen_pick(x, y, &point, &surface_index, &unit_index))
			{
				squad = command_squad_of_unit(unit_index, NULL);
				command_selection_set(squad && command_squad_mine(squad) ? 1UL << (squad - command_globals.squads) :
					command_squads_near(&point, COMMAND_SELECT_RADIUS, TRUE), "by a click");
			}
		}
	}
	if (pointer->right_clicks)
	{
		real_point3d point;
		long surface_index, unit_index;

		if (command_screen_pick(x, y, &point, &surface_index, &unit_index))
			command_order_at(&point, surface_index, unit_index);
	}
}

/* the pointer at the screen's edges moves the view (along its i: forward,
j: left) */
static void command_edge_scroll(
	real *i,
	real *j)
{
	rectangle2d const *window = &command_globals.view_camera.viewport_bounds;

	if (!command_globals.view_valid || !command_globals.pointer_valid || !command_pointer_wanted() || command_globals.drag_down)
		return;
	if (command_globals.pointer_x <= window->x0 + COMMAND_EDGE_PIXELS)
		*j += 1.f;
	else if (command_globals.pointer_x >= window->x1 - 1 - COMMAND_EDGE_PIXELS)
		*j -= 1.f;
	if (command_globals.pointer_y <= window->y0 + COMMAND_EDGE_PIXELS)
		*i += 1.f;
	else if (command_globals.pointer_y >= window->y1 - 1 - COMMAND_EDGE_PIXELS)
		*i -= 1.f;
}

/* ---- the camera's jumps: the base, the last alert */

static boolean command_ground_top(real_point3d const *point, real_point3d *ground);

static void command_camera_jump(
	real_point3d const *point,
	char const *what)
{
	real_point3d ground;

	command_globals.focus = *point;
	if (command_ground_top(point, &ground))
		command_globals.focus.z = ground.z;
	command_globals.camera_jump = TRUE;
	platform_log("command: camera to %s at %.2f %.2f %.2f", what, command_globals.focus.x, command_globals.focus.y,
		command_globals.focus.z);
}

/* the commander's base: his side's flag stand (the map's CTF flag of the
red team - Blood Gulch's bases), else where he began */
static boolean command_base_point(
	short side,
	real_point3d *point)
{
	struct scenario *scenario = global_scenario_try_and_get();
	long index;

	/* (the mode layouts, step 3: the layout's home base of the side - a Base
	marker, OWNER red or blue, HOME BASE yes - before the map's own flags) */
	if (forge_mode_home_base(side == _command_side_red ? 0 : 1, point, NULL))
		return TRUE;
	for (index = 0; scenario && index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index,
			struct scenario_netgame_flag);

		if (flag->type == _netgame_flag_ctf_flag && flag->team_index == (side == _command_side_red ? 0 : 1))
		{
			*point = flag->position;
			return TRUE;
		}
	}
	if (command_globals.start_valid)
	{
		*point = command_globals.start;
		return TRUE;
	}
	return FALSE;
}

/* the black button: the base; twice quickly, the last alert */
static void command_jump_button(
	void)
{
	unsigned long now = system_milliseconds();
	real_point3d point;

	if (now - command_globals.jump_pressed_at < COMMAND_DOUBLE_TAP_MILLISECONDS && command_globals.alert_valid)
	{
		command_globals.jump_pressed_at = 0;
		command_camera_jump(&command_globals.alert, "the last alert");
		command_notice("%s", T("Last alert"));
		return;
	}
	command_globals.jump_pressed_at = now;
	if (command_base_point(command_globals.side, &point))
	{
		command_camera_jump(&point, "the base");
		command_notice("%s", T("Base"));
	}
}

/* (each frame) a squad of the commander's hurt since the last frame is an
alert there: the black button twice takes the camera to it */
static void command_alerts_update(
	void)
{
	short squad_index;

	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad *squad = &command_globals.squads[squad_index];
		real vitality = 0.f;
		short index;

		if (!squad->used)
			continue;
		for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
		{
			struct unit_datum *unit = command_living_unit(squad->units[index]);

			if (unit)
				vitality += unit->object.body_vitality + unit->object.shield_vitality;
		}
		/* (not his Spartan while he is in it: he knows) */
		if (squad->side == command_globals.side && vitality + 0.001f < squad->last_vitality &&
			system_milliseconds() - command_globals.alert_at > 2000 &&
			!(squad->hero && command_globals.possessed_unit_index != NONE))
		{
			long leader = command_squad_leader(squad);

			if (leader != NONE)
			{
				object_get_origin(leader, &command_globals.alert);
				command_globals.alert_valid = TRUE;
				command_globals.alert_at = system_milliseconds();
				platform_log("command: alert: squad %d under attack at %.2f %.2f %.2f", squad->number,
					command_globals.alert.x, command_globals.alert.y, command_globals.alert.z);
				if (squad->hero)
					command_notice("%s", T("Your Spartan is under attack"));
				else
					command_notice(T("Squad %d under attack"), squad->number);
			}
		}
		squad->last_vitality = vitality;
	}
}

static void command_where_scores(void);

static void command_where(
	void)
{
	short squad_index;

	platform_log("command: where: %s, %s, focus %.2f %.2f %.2f yaw %.0f pitch %.0f distance %.1f, cursor %s %.2f %.2f %.2f, %s",
		command_globals.active ? "on" : "off",
		command_globals.possessed_unit_index != NONE ? "in a unit" : "above the map", command_globals.focus.x,
		command_globals.focus.y, command_globals.focus.z, command_globals.yaw * 180.f / _pi,
		command_globals.pitch * 180.f / _pi, command_globals.distance, command_globals.cursor_valid ? "on" : "off",
		command_globals.cursor.x, command_globals.cursor.y, command_globals.cursor.z,
		command_squad_selected() ? "a squad chosen" : "no squad chosen");
	command_where_scores();
	platform_log("command: where: the Shades' gunners hit %ld times (%.0f damage) red's, %ld (%.0f) blue's",
		command_crew_hits[0], command_crew_damage[0], command_crew_hits[1], command_crew_damage[1]);
	platform_log("command: where: the camera at %.2f %.2f %.2f, %.1f over the focus, rise %.2f, view pitch %.0f",
		command_globals.camera_position.x, command_globals.camera_position.y, command_globals.camera_position.z,
		command_globals.camera_position.z - command_globals.focus.z, command_camera_rise,
		command_camera_view_pitch * 180.f / _pi);
	/* (in first person: what the Spartan has - the hero kit, C8) */
	{
		struct unit_datum *in = command_living_unit(command_globals.possessed_unit_index);

		if (in)
		{
			short slot;

			for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
			{
				long weapon_index = unit_inventory_get_weapon(command_globals.possessed_unit_index, slot);

				if (weapon_index != NONE)
					platform_log("command: where:   in hand %d: %s", slot, tag_get_name(object_get(weapon_index)->definition_index));
			}
			platform_log("command: where:   in the Spartan: body %.2f shield %.2f, grenades %d frag %d plasma",
				in->object.body_vitality, in->object.shield_vitality,
				in->unit.grenade_counts[_unit_grenade_human_fragmentation], in->unit.grenade_counts[_unit_grenade_covenant_plasma]);
		}
	}
	/* (the screen: its windows, the cursor where the screen shows it, and
the line through the screen's middle) */
	if (command_globals.cursor_valid)
	{
		real_point2d screen = { 0.f, 0.f };
		real_point2d middle;
		real_point3d origin;
		real_vector3d direction;
		struct render_camera const *camera = &command_globals.view_camera;
		boolean shown = command_globals.view_valid &&
			render_camera_world_to_screen(camera, &command_globals.view_frustum, &command_globals.cursor, &screen);

		middle.x = 0.f;
		middle.y = 0.f;
		render_camera_screen_to_world(camera, &command_globals.view_frustum, &middle, &origin, &direction);
		normalize3d(&direction);
		platform_log("command: where: window %d %d %d %d, viewport %d %d %d %d, the cursor on the screen %s %.1f %.1f, "
			"the middle's line %.2f %.2f %.2f along %.2f %.2f %.2f (the camera's %.2f %.2f %.2f)",
			camera->window_bounds.x0, camera->window_bounds.y0, camera->window_bounds.x1,
			camera->window_bounds.y1, camera->viewport_bounds.x0, camera->viewport_bounds.y0,
			camera->viewport_bounds.x1, camera->viewport_bounds.y1, shown ? "at" : "off", screen.x, screen.y,
			origin.x, origin.y, origin.z, direction.i, direction.j, direction.k, camera->forward.i,
			camera->forward.j, camera->forward.k);
	}
	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad *squad = &command_globals.squads[squad_index];
		short index;

		if (!squad->used)
			continue;
		platform_log("command: where: squad %d%s (%s, %s) %s to %.2f %.2f %.2f, %d alive", squad->number,
			squad_index == command_globals.selected ? "*" : "", squad->name, command_side_names[squad->side],
			command_order_log_names[squad->order], squad->point.x, squad->point.y, squad->point.z,
			command_squad_living(squad));
		for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
		{
			struct unit_datum *unit = command_living_unit(squad->units[index]);
			real_point3d origin;
			real_point3d place;

			if (squad->units[index] == NONE)
				continue;
			if (!unit)
			{
				platform_log("command: where:   unit %d dead", index + 1);
				continue;
			}
			object_get_origin(squad->units[index], &origin);
			command_member_place(squad, index, &place);
			if (squad->hero)
			{
				long weapon_index = unit_inventory_get_weapon(squad->units[index], unit->unit.current_weapon_index);
				struct actor_datum *actor = unit->unit.actor_index != NONE ? actor_get(unit->unit.actor_index) : NULL;

				platform_log("command: where:   the Spartan's target prop %lx, its weapon %s with %d loaded %d more, body %.2f shield %.2f, grenades %d frag %d plasma",
					actor ? (unsigned long)actor->target.target_prop_index : 0xffffffffUL,
					weapon_index != NONE ? tag_get_name(object_get(weapon_index)->definition_index) : "none",
					weapon_index != NONE ? weapon_get(weapon_index)->weapon.magazines[0].rounds_loaded : 0,
					weapon_index != NONE ? weapon_get(weapon_index)->weapon.magazines[0].rounds_total : 0,
					unit->object.body_vitality, unit->object.shield_vitality,
					unit->unit.grenade_counts[_unit_grenade_human_fragmentation], unit->unit.grenade_counts[_unit_grenade_covenant_plasma]);
			}
			if (unit->unit.actor_index != NONE)
			{
				struct actor_datum *actor = actor_get(unit->unit.actor_index);

				platform_log("command: where:   unit %d at %.2f %.2f %.2f, %.1f wu from its place, actor %lx action %d"
					" mode %d destination %d at %d path %d moving %d movement %d throttle %.2f %.2f dormant %d,"
					" team %d, player %ld", index + 1, origin.x, origin.y, origin.z,
					command_distance2d(&origin, &place), (unsigned long)unit->unit.actor_index, actor->state.action,
					actor->state.mode, actor->control.path.destination_orders.destination_type,
					actor->control.path.at_destination, actor->control.path.path.valid, actor->control.moving,
					actor->output.movement_type, actor->output.throttle.i, actor->output.throttle.j, actor->meta.dormant,
					unit->object.owner_team_index, (long)unit->unit.player_index);
				/* (its fight: its target's prop, how well it sees it, its
				combat status, its firing, its body) */
				{
					long prop_index = actor->target.target_prop_index;
					struct prop_datum const *prop = prop_index != NONE ? prop_get(prop_index) : NULL;

					platform_log("command: where:   unit %d fight: target prop %lx (unit %lx) visibility %d line of sight %d,"
						" combat status %d, firing at %d visible %d, body %.2f shield %.2f", index + 1,
						(unsigned long)prop_index, prop ? (unsigned long)prop->unit_index : 0xffffffffUL,
						prop ? prop->visibility : -1, prop ? prop->line_of_sight : -1, actor->state.combat_status,
						actor->control.current_fire_target_type, actor->control.current_fire_target_visible,
						unit->object.body_vitality, unit->object.shield_vitality);
				}
			}
			else
			{
				platform_log("command: where:   unit %d at %.2f %.2f %.2f, %.1f wu from its place, no actor, player %ld",
					index + 1, origin.x, origin.y, origin.z, command_distance2d(&origin, &place),
					(long)unit->unit.player_index);
			}
		}
	}
}

/* ---- the console's words */

static boolean command_word(
	char const **text,
	char *word,
	int size)
{
	int length = 0;

	while (**text == ' ')
		(*text)++;
	while (**text && **text != ' ' && length < size - 1)
		word[length++] = *(*text)++;
	word[length] = 0;
	while (**text && **text != ' ')
		(*text)++;
	return length > 0;
}

static boolean command_point_parse(
	char const *text,
	real_point3d *point)
{
	float x, y, z;

	if (sscanf(text, "%f %f %f", &x, &y, &z) != 3)
		return FALSE;
	point->x = x;
	point->y = y;
	point->z = z;
	return TRUE;
}

static boolean command_buttons_parse(
	char const *text,
	boolean down[NUMBER_OF_GAMEPAD_BUTTONS],
	boolean *toggle)
{
	static struct
	{
		char const *name;
		short button;
	} const names[] =
	{
		{ "a", _gamepad_analog_button_a }, { "b", _gamepad_analog_button_b }, { "x", _gamepad_analog_button_x },
		{ "y", _gamepad_analog_button_y }, { "black", _gamepad_analog_button_black },
		{ "white", _gamepad_analog_button_white }, { "lt", _gamepad_analog_button_left_trigger },
		{ "rt", _gamepad_analog_button_right_trigger }, { "up", _gamepad_binary_button_dpad_up },
		{ "down", _gamepad_binary_button_dpad_down }, { "left", _gamepad_binary_button_dpad_left },
		{ "right", _gamepad_binary_button_dpad_right }, { "back", _gamepad_binary_button_back },
	};
	char copy[64];
	char *part;
	char *saved = NULL;

	csmemset(down, 0, sizeof(boolean) * NUMBER_OF_GAMEPAD_BUTTONS);
	*toggle = FALSE;
	snprintf(copy, sizeof(copy), "%s", text);
	for (part = strtok_r(copy, "+", &saved); part; part = strtok_r(NULL, "+", &saved))
	{
		short index;
		boolean found = FALSE;

		if (!strcmp(part, "combo"))
		{
			*toggle = TRUE;
			continue;
		}
		for (index = 0; index < (short)NUMBEROF(names); index++)
		{
			if (!strcmp(part, names[index].name))
			{
				down[names[index].button] = TRUE;
				found = TRUE;
			}
		}
		if (!found)
			return FALSE;
	}
	return TRUE;
}

/* ---- the Command game: the Forge lobby's START COMMAND on Blood Gulch.
Its map built out as it loads (command_map_loaded: no build in the game,
none of the debug build's lines over it), the commander above the map once
he has spawned; each side's HQ its own base (the map's flag stand of its
team: red the UNSC, blue the Covenant), a squad of each at the start and
another every COMMAND_WAVE_SECONDS while the AI's budget allows; the
Covenant's advance on the UNSC's base until the AI commander plays them */

#define COMMAND_WAVE_SECONDS 60
/* a rally point this far before a base, toward the other */
#define COMMAND_RALLY_DISTANCE 16.f
/* the AI alive, both teams (the owner, Nov 6: 32 on the Vita too), each
team's half its own (the bases' Shade crews in it, the commander's Spartan
not): one side's waves never starve the other's. The Flood and the
Sentinels (later) get a share of their own on top */
#define COMMAND_AI_MAXIMUM 32
#define COMMAND_AI_MAXIMUM_PER_TEAM (COMMAND_AI_MAXIMUM / NUMBER_OF_COMMAND_SIDES)

/* the waves' squads, in turn, the same for both sides (the owner: each
side an army of marines and the Covenant together; the Hunters a base's
upgrade's, never a free wave's): "name:count,..." (command_squad_spawn), the
first named leading */
static char const *const command_waves[] =
{
	"marine assault rifle:4",
	"grunt minor plasma pistol:2,jackal minor plasma pistol:2",
	"elite minor plasma rifle:1,grunt minor plasma pistol:3",
	"marine_armored assault rifle:3,marine_armored sniper rifle:1",
};
#define COMMAND_WAVE_KINDS ((short)NUMBEROF(command_waves))

#define COMMAND_SHADE_TAG "c gun turret"
#define COMMAND_SHADE_CREW "grunt minor plasma pistol"

/* the AI alive: a side's (NONE: both's), the Spartan not counted */
static short command_ai_alive(
	short side)
{
	short squad_index;
	short alive = 0;

	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad const *squad = &command_globals.squads[squad_index];

		if (squad->used && !squad->hero && (side == NONE || squad->side == side))
			alive += command_squad_living(squad);
	}
	return alive;
}

/* squads with no unit living let go: their places for new ones */
static void command_squads_free_dead(
	void)
{
	short squad_index;

	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad *squad = &command_globals.squads[squad_index];

		/* (one in the air waits for its ship: its members without a seat
		come at the drop, a ship shot down lets it go) */
		if (squad->used && !squad->airborne && !squad->hero && !command_squad_living(squad))
		{
			platform_log("command: squad %d (%s) lost", squad->number, command_side_names[squad->side]);
			command_globals.selection &= ~(1UL << squad_index);
			squad->used = FALSE;
		}
	}
	if (command_globals.selected != NONE && !(command_globals.selection & (1UL << command_globals.selected)))
		command_selection_set(command_globals.selection, "with a squad lost");
}

/* the map's bounds for the view: its spawns', flags' and items' places with
some room about them (Blood Gulch's canyon, not its cliffs' tops) */
static void command_bounds_build(
	void)
{
	struct scenario *scenario = global_scenario_try_and_get();
	long index;
	real *b = command_globals.bounds;

	command_globals.bounds_valid = FALSE;
	if (!scenario)
		return;
	b[0] = b[2] = 1e30f;
	b[1] = b[3] = -1e30f;
#define COMMAND_BOUNDS_ADD(point) \
	{ b[0] = MIN(b[0], (point)->x); b[1] = MAX(b[1], (point)->x); b[2] = MIN(b[2], (point)->y); b[3] = MAX(b[3], (point)->y); \
		command_globals.bounds_valid = TRUE; }
	for (index = 0; index < scenario->players.count; index++)
		COMMAND_BOUNDS_ADD(&TAG_BLOCK_GET_ELEMENT(&scenario->players, index, struct encounter_player_starting_location)->position);
	for (index = 0; index < scenario->netgame_flags.count; index++)
		COMMAND_BOUNDS_ADD(&TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag)->position);
	for (index = 0; index < scenario->netgame_equipment.count; index++)
		COMMAND_BOUNDS_ADD(&TAG_BLOCK_GET_ELEMENT(&scenario->netgame_equipment, index, struct scenario_netgame_equipment)->position);
#undef COMMAND_BOUNDS_ADD
	if (command_globals.bounds_valid)
	{
		b[0] -= COMMAND_BOUNDS_MARGIN;
		b[1] += COMMAND_BOUNDS_MARGIN;
		b[2] -= COMMAND_BOUNDS_MARGIN;
		b[3] += COMMAND_BOUNDS_MARGIN;
		platform_log("command: the view's bounds %.1f to %.1f, %.1f to %.1f", b[0], b[1], b[2], b[3]);
	}
}

/* a side's rally point: before its base, toward the other's - where its
squads come and the other's advance to (a base's flag stands inside it,
under its roof, where the AI's paths do not go) */
static boolean command_rally_point(
	short side,
	real_point3d *point)
{
	real_point3d base, other, ground;
	real dx, dy, length;

	if (!command_base_point(side, &base) || !command_base_point(side == _command_side_red ? _command_side_blue :
		_command_side_red, &other))
	{
		return FALSE;
	}
	dx = other.x - base.x;
	dy = other.y - base.y;
	length = (real)sqrt(dx * dx + dy * dy);
	*point = base;
	if (length > 1.f)
	{
		/* (1.1.4, the big team maps: bases near each other - Danger Canyon's
		face each other 24 wu apart across a chasm, a bridge between - the
		rally points are kept 16 wu from each other, each nearer its own
		base, so the two sides' reinforcements do not land in one spot) */
		real distance = MIN(COMMAND_RALLY_DISTANCE, (length - COMMAND_RALLY_DISTANCE) / 2.f);

		distance = MAX(distance, 2.f);
		point->x += dx / length * distance;
		point->y += dy / length * distance;
	}
	if (command_ground(point, &ground, NULL))
		*point = ground;
	return TRUE;
}

/* a wave's squad at its base (made there, or down from its ship): its
first order */
static long command_pelican_cargo(long pelican_index, short side);

static void command_wave_landed(
	struct command_squad *squad)
{
	/* (an AI commander's: its next thought sends it on, command_ai_orders) */
	if (!command_ai[squad->side].plays)
		command_event(squad->side, squad->side, _command_event_arrived, squad->number, 0);
}

static boolean command_flight_send(short side, char const *composition);

/* a side's wave: its squad at its base at the game's start, later by air
(command_flight_send) */
static void command_wave_send(
	short side,
	short kind)
{
	struct command_squad *squad;
	real_point3d base;

	if (!command_rally_point(side, &base))
		return;
	if (command_ai_alive(side) + COMMAND_SQUAD_SIZE > COMMAND_AI_MAXIMUM_PER_TEAM)
	{
		platform_log("command: wave: the %s's squad waits for room (%d of its %d AI alive)", command_side_names[side],
			command_ai_alive(side), COMMAND_AI_MAXIMUM_PER_TEAM);
		return;
	}
	kind = (short)(kind % COMMAND_WAVE_KINDS);
	if (command_globals.side_waves[side] > 0 && command_flight_send(side, command_waves[kind]))
		return;
	squad = command_squad_spawn(side, 1, command_waves[kind], &base);
	if (!squad)
		return;
	command_wave_landed(squad);
}

/* (C1, the owner: the Shades were where they could not shoot) a base's
Shade's spot: on the ground before the base, covering its approaches - the
first two either side of the line toward the other base, the defence's two
on its flanks -, each on flat ground at the base's level with a clear line
of fire outward (COMMAND_SHADE_CLEAR wu); it faces out from the base */
#define COMMAND_SHADE_DISTANCE_NEAREST 10.f
#define COMMAND_SHADE_DISTANCE_FARTHEST 16.f
#define COMMAND_SHADE_CLEAR 25.f
#define COMMAND_SHADE_GUN_HEIGHT 1.4f

static boolean command_shade_spot(
	short side,
	short shade,
	real_point3d *point,
	real_vector3d *forward)
{
	real_point3d base, other, base_ground;
	real dx, dy, length, field;
	/* (the angle off the line toward the other base: the first two its
	front's two sides, the defence's two its flanks) */
	static real const angles[COMMAND_SHADES_PER_BASE] = { 32.f, -32.f, 80.f, -80.f };
	short attempt;

	if (!command_base_point(side, &base) || !command_base_point(side == _command_side_red ? _command_side_blue :
		_command_side_red, &other))
	{
		return FALSE;
	}
	dx = other.x - base.x;
	dy = other.y - base.y;
	length = (real)sqrt(dx * dx + dy * dy);
	if (length < 1.f)
		return FALSE;
	field = (real)atan2(dy, dx);
	if (!command_ground(&base, &base_ground, NULL))
		base_ground = base;
	/* (from the angle wanted, a little either way; from near the base out) */
	for (attempt = 0; attempt < 5 * 7; attempt++)
	{
		static real const turns[5] = { 0.f, 8.f, -8.f, 16.f, -16.f };
		real angle = field + (angles[shade % COMMAND_SHADES_PER_BASE] + turns[attempt / 7]) * _pi / 180.f;
		real distance = COMMAND_SHADE_DISTANCE_NEAREST + (real)(attempt % 7) *
			(COMMAND_SHADE_DISTANCE_FARTHEST - COMMAND_SHADE_DISTANCE_NEAREST) / 6.f;
		real_point3d at, ground;
		short around, level = 0;

		at.x = base.x + (real)cos(angle) * distance;
		at.y = base.y + (real)sin(angle) * distance;
		at.z = base_ground.z + 2.f;
		if (!command_ground(&at, &ground, NULL) || fabs(ground.z - base_ground.z) > 2.5f)
			continue;
		/* (flat all about it: a Shade on a slope tips, and its Grunt gets
		out) */
		for (around = 0; around < 4; around++)
		{
			real_point3d probe = ground;
			real_point3d under;

			probe.x += around == 0 ? 1.2f : around == 1 ? -1.2f : 0.f;
			probe.y += around == 2 ? 1.2f : around == 3 ? -1.2f : 0.f;
			if (command_ground(&probe, &under, NULL) && fabs(under.z - ground.z) < 0.5f)
				level++;
		}
		if (level < 4)
			continue;
		forward->i = (real)cos(angle);
		forward->j = (real)sin(angle);
		forward->k = 0.f;
		/* (a clear line of fire outward, and toward the field, from the gun's
		height) */
		{
			struct collision_result collision;
			real_point3d gun = ground;
			real_vector3d line;
			short look;
			boolean clear = TRUE;

			gun.z += COMMAND_SHADE_GUN_HEIGHT;
			for (look = 0; look < 2 && clear; look++)
			{
				real heading = look ? field : angle;

				line.i = (real)cos(heading) * COMMAND_SHADE_CLEAR;
				line.j = (real)sin(heading) * COMMAND_SHADE_CLEAR;
				line.k = 0.f;
				if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
						&gun, &line, NONE, &collision) && collision.t < 0.6f)
				{
					clear = FALSE;
				}
			}
			if (!clear)
				continue;
		}
		*point = ground;
		return TRUE;
	}
	return FALSE;
}

static short command_shades_wanted(short side);

static long command_vehicle_tag_find(
	char const *name)
{
	struct tag_iterator iterator;
	long tag_index;

	tag_iterator_new(&iterator, VEHICLE_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		char const *tag_name = tag_get_name(tag_index);

		if (tag_get_group_tag(tag_index) == VEHICLE_DEFINITION_TAG && tag_name && strstr(tag_name, name))
			return tag_index;
	}
	return NONE;
}

/* the first of a vehicle's seats a unit may take, NONE if none */
static short command_vehicle_free_seat(
	long unit_index,
	long vehicle_index)
{
	short seat;

	for (seat = 0; seat < 8; seat++)
	{
		long occupant = NONE;

		if (unit_can_enter_seat(unit_index, vehicle_index, seat, &occupant) && occupant == NONE)
			return seat;
	}
	return NONE;
}

/* (the game's start, each wave) every base's Shades there - one destroyed
built again with the base's next wave - and each manned by a Grunt of its
base's side, a crew squad of its own (the commander drops into it as any) */
static void command_shades_update(
	void)
{
	long tag_index = command_vehicle_tag_find(COMMAND_SHADE_TAG);
	short side, shade;

	if (tag_index == NONE)
	{
		platform_log("command: shades: no Shade in the map's tags or the packs");
		return;
	}
	for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
	{
		/* (editor6: a layout with Shades of its own - its Shade turrets placed
		in the editor - has those manned, each its side's (its team, else the
		nearer base's), and no others made; the bases' own Shades only for a
		layout with none) */
		long layout_shades[COMMAND_SHADES_PER_BASE];
		short layout_total = 0;
		short layout_count = 0;
		short wanted;

		{
			short nth;
			long found;
			short team;
			real_point3d bases[NUMBER_OF_COMMAND_SIDES];
			boolean bases_valid = command_base_point(_command_side_red, &bases[0]) &&
				command_base_point(_command_side_blue, &bases[1]);

			for (nth = 0; (found = forge_mode_layout_object(tag_index, nth, &team)) != NONE && nth < 64; nth++)
			{
				short owner = team == 0 ? _command_side_red : team == 1 ? _command_side_blue : NONE;

				layout_total++;
				if (owner == NONE && bases_valid)
				{
					real_point3d at;

					object_get_origin(found, &at);
					owner = command_distance2d(&at, &bases[0]) <= command_distance2d(&at, &bases[1]) ? _command_side_red :
						_command_side_blue;
				}
				if (owner == side && layout_count < COMMAND_SHADES_PER_BASE)
					layout_shades[layout_count++] = found;
			}
		}
		wanted = layout_total > 0 ? layout_count : command_shades_wanted(side);
		for (shade = 0; shade < wanted; shade++)
		{
			long *object_index = &command_globals.shades[side][shade];
			struct object_datum *object = object_try_and_get(*object_index);
			real_point3d spot;
			real_vector3d forward;
			struct command_squad *crew;
			short seat;

			if ((!object || TEST_FLAG(object->object.damage_flags, _object_dead_bit)) && layout_total > 0)
			{
				struct object_datum *placed = object_try_and_get(layout_shades[shade]);

				if (!placed || TEST_FLAG(placed->object.damage_flags, _object_dead_bit))
					continue;
				*object_index = layout_shades[shade];
				placed->object.owner_team_index = command_side_team(side);
				object_get_origin(*object_index, &spot);
				platform_log("command: shades: %s's Shade %d is the layout's (%lx) at %.2f %.2f %.2f", command_side_names[side],
					shade + 1, (unsigned long)*object_index, spot.x, spot.y, spot.z);
				object = placed;
			}
			if (!object || TEST_FLAG(object->object.damage_flags, _object_dead_bit))
			{
				struct object_placement_data placement;


				if (!command_shade_spot(side, shade, &spot, &forward))
				{
					platform_log("command: shades: %s's base has no clear ground for its Shade %d", command_side_names[side], shade + 1);
					continue;
				}
				object_placement_data_new(&placement, tag_index, NONE);
				SET_FLAG(placement.flags, _new_object_never_automatically_delete_bit, TRUE);
				placement.position = spot;
				placement.position.z += 0.2f;
				placement.forward = forward;
				/* (the Shade itself, not what the game type's vehicle set would
				make of it: Forge's exact placing) */
				forge_placing_exact = 1;
				*object_index = object_new(&placement);
				forge_placing_exact = 0;
				if (*object_index == NONE)
				{
					platform_log("command: shades: %s's Shade %d not made at %.2f %.2f %.2f (%s)", command_side_names[side],
						shade + 1, spot.x, spot.y, spot.z, tag_get_name(tag_index));
					continue;
				}
				object_try_and_get(*object_index)->object.owner_team_index = command_side_team(side);
				platform_log("command: shades: %s's Shade %d at %.2f %.2f %.2f", command_side_names[side], shade + 1, spot.x,
					spot.y, spot.z);
			}
			/* (manned: a Grunt of its side in its seat) */
			if (unit_seat_occupant(*object_index, 0) != NONE)
			{
				platform_log("command: shades: %s's Shade %d manned", command_side_names[side], shade + 1);
				continue;
			}
			if (command_ai_alive(side) + 1 > COMMAND_AI_MAXIMUM_PER_TEAM)
				continue;
			object_get_origin(*object_index, &spot);
			spot.z += 0.6f;
			crew = command_squad_spawn(side, 1, COMMAND_SHADE_CREW ":1", &spot);
			if (!crew || crew->units[0] == NONE)
				continue;
			crew->crew = TRUE;
			/* (a crew is not the commander's first squad chosen) */
			if (command_globals.selection == (1UL << (crew - command_globals.squads)))
			{
				command_globals.selection = 0;
				command_globals.selected = NONE;
			}
			seat = command_vehicle_free_seat(crew->units[0], *object_index);
			if (seat != NONE && unit_enter_seat(crew->units[0], *object_index, seat))
			{
				platform_log("command: shades: squad %d's Grunt mans %s's Shade %d (seat %d)", crew->number,
					command_side_names[side], shade + 1, seat);
			}
			else
				platform_log("command: shades: squad %d's Grunt cannot take %s's Shade %d", crew->number,
					command_side_names[side], shade + 1);
		}
	}
}

/* ---- reinforcements by air (the owner's step 3; the spec's section 6): a
wave's squad flies in to its base's landing spot in the ship its species
rides in the campaign - marines in the Pelican, the Covenant in their
dropship, for either side -, along its base's fixed path: in from far behind
the base and high over it, down to a hover over the landing spot, the squad
out of its seats (the campaign's own seats and their exit animations, as its
scripts' vehicle_load_magic and vehicle_unload), then back out the way it
came. The ship is not flown by the AI: command_tick sets it on its path
each tick (its position, facing and velocity), as a recording moves its
unit, its own physics' lift turned off (vehicle_hover). Shot down before
the drop, its squad dies with it, and the wreck falls as the physics has
it.

The path is marker data per base (struct command_path: the landing spot and
the points in), derived from the map's own bases for now - the landing spot
the base's rally point, the cruise height over the highest ground under the
way in - until the Command setup's Forge markers place them (section 3) */

enum
{
	_command_ship_pelican = 0,
	_command_ship_dropship,
	NUMBER_OF_COMMAND_SHIPS
};

static char const *const command_ship_tags[NUMBER_OF_COMMAND_SHIPS] = { "pelican\\pelican", "c_dropship\\c_dropship" };
/* (the riders' seats, as the campaign's scripts name them) */
static char const *const command_ship_seats[NUMBER_OF_COMMAND_SHIPS] = { "rider", "passenger" };
/* (the HUD's notices: whole lines, each ship's own - a language's words
agree with the ship's) */
static char const *const command_ship_inbound[NUMBER_OF_COMMAND_SHIPS] =
{
	N_("Squad %d inbound by Pelican"), N_("Squad %d inbound by dropship"),
};
static char const *const command_ship_lost[NUMBER_OF_COMMAND_SHIPS] =
{
	N_("Squad %d lost: its Pelican was shot down"), N_("Squad %d lost: its dropship was shot down"),
};
static char const *const command_ship_enemy_down[NUMBER_OF_COMMAND_SHIPS] =
{
	N_("Enemy Pelican shot down"), N_("Enemy dropship shot down"),
};
static char const *const command_ship_log_names[NUMBER_OF_COMMAND_SHIPS] = { "Pelican", "Covenant dropship" };
/* the riders' feet this high over the landing spot as they get out (the
Pelican's out of its ramp, the dropship's down from its bays) */
static real const command_ship_drop[NUMBER_OF_COMMAND_SHIPS] = { 0.6f, 1.4f };

enum
{
	_command_flight_none = 0,
	/* along the path's points to over the landing spot */
	_command_flight_in,
	/* down to the hover */
	_command_flight_descend,
	/* the squad getting out */
	_command_flight_hover,
	/* back up over the landing spot, turning for home */
	_command_flight_climb,
	/* back along the path, deleted at its end */
	_command_flight_out,
	/* shot down: the wreck the physics', deleted in a while */
	_command_flight_down,
	NUMBER_OF_COMMAND_FLIGHT_STATES
};

static char const *const command_flight_log_names[NUMBER_OF_COMMAND_FLIGHT_STATES] =
{
	"none", "in", "descend", "hover", "climb", "out", "down",
};

#define COMMAND_FLIGHTS 6
#define COMMAND_PATH_POINTS 3
/* the way in: its average speed, how fast down and up at the landing spot,
how fast it turns */
#define COMMAND_SHIP_SPEED 15.f
#define COMMAND_SHIP_VERTICAL_SPEED 3.5f
#define COMMAND_SHIP_TURN_RATE (80.f * _pi / 180.f)
/* the path: its first point this far behind the base, the descent from this
high over the landing spot, the cruise this far over the highest ground
under the way */
#define COMMAND_PATH_BACK 50.f
#define COMMAND_PATH_APPROACH 12.f
#define COMMAND_PATH_CLEARANCE 10.f
#define COMMAND_PATH_SAMPLES 12
/* the landing spot this far to the side of the base's rally point (where its
first squad and the commander's Spartan stand): flat ground, open sky */
#define COMMAND_LANDING_ASIDE 11.f
/* a squad down walks out this far from under its ship, toward the field */
#define COMMAND_MUSTER_DISTANCE 7.f
/* the hover: at least this long, at most this long (riders still in are put
out), the riders' unload asked again this often */
#define COMMAND_HOVER_MINIMUM_TICKS (3 * TICKS_PER_SECOND)
#define COMMAND_HOVER_MAXIMUM_TICKS (8 * TICKS_PER_SECOND)
#define COMMAND_UNLOAD_RETRY_TICKS (TICKS_PER_SECOND / 2)
/* the hulls (damage as object_cause_damage counts it, before a body's
material and vitality: a rocket's blast on the ship about 310) - the
Pelican down in four rockets, the dropship in five */
#define COMMAND_PELICAN_HULL 1200.f
#define COMMAND_DROPSHIP_HULL 1500.f
/* a wreck deleted this long after it was shot down */
#define COMMAND_WRECK_TICKS (20 * TICKS_PER_SECOND)

/* a base's landing spot and its way in (the spec's markers) */
struct command_path
{
	boolean valid;
	real_point3d landing;
	/* where a squad down goes, out from under its ship: toward the field */
	real_point3d muster;
	/* in: far behind the base, over the base, over the landing spot; out the
	same backwards */
	real_point3d points[COMMAND_PATH_POINTS];
	real length;
	/* (editor6) whether the landing is the layout's drop point */
	boolean layout_landing;
};

struct command_flight
{
	boolean used;
	short side;
	short ship;
	long vehicle_index;
	/* its squad (the table's index), NONE once it is down */
	short squad_index;
	short state;
	long state_time;
	long state_ticks;
	real_point3d hover;
	real yaw;
	/* the yaw last set and its tick (command_tick) */
	long last_tick;
	long last_unload_time;
	/* (a member that took no seat: made under the hover as the others get
	out) */
	long pending_variants[COMMAND_SQUAD_SIZE];
	short number;
	/* the damage it has taken (command_object_damaged), and whether that has
	killed it */
	real damage;
	boolean killed;
	/* (the vehicle bay's) the Warthog under the Pelican, NONE if none */
	long cargo_index;
	/* (shot down) the wreck's fall: its velocity, its roll, on the ground */
	real_vector3d crash_velocity;
	real crash_roll;
	boolean crash_landed;
	/* its way in (its base's, or a drop point's: command_squads.h) */
	struct command_path path;
	/* (command_squads.h) a hosted squad's: its engine team; the player whose
	shot hurt the ship last, his unit (the riders' deaths his kills) */
	boolean hosted;
	short team;
	long attacker_player_index;
	long attacker_unit_index;
};

/* each ship's hull: the damage that shoots it down (its tags give it no
vitality: the campaign's ships cannot be hurt) */
static real const command_ship_hull[NUMBER_OF_COMMAND_SHIPS] = { COMMAND_PELICAN_HULL, COMMAND_DROPSHIP_HULL };

static struct command_path command_paths[NUMBER_OF_COMMAND_SIDES];
static struct command_flight command_flights[COMMAND_FLIGHTS];

/* the highest ground under a point's x y (a cliff's top over the canyon's
floor), FALSE if none */
static boolean command_ground_highest(
	real_point3d const *point,
	real_point3d *ground)
{
	struct collision_result collision;
	real_point3d start = *point;
	real_vector3d down = { 0.f, 0.f, -400.f };

	start.z += 200.f;
	if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
			&start, &down, NONE, &collision))
	{
		*ground = collision.point;
		return TRUE;
	}
	return command_ground_top(point, ground);
}

/* how high the level's sky stands over a point on its ground (the level's
ceiling: the ship kept under it), a long way if nothing is met */
static real command_ceiling(
	real_point3d const *ground)
{
	struct collision_result collision;
	real_point3d start = *ground;
	real_vector3d up = { 0.f, 0.f, 300.f };

	start.z += 1.f;
	if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
			FLAG(_collision_test_front_facing_surfaces_bit), &start, &up, NONE, &collision))
	{
		return collision.point.z;
	}
	return ground->z + 300.f;
}

/* a point along a path's points (Catmull-Rom through them), fraction of its
length along */
static void command_path_at(
	real_point3d const *points,
	short count,
	real fraction,
	real_point3d *out)
{
	real lengths[COMMAND_PATH_POINTS];
	real total = 0.f;
	real along;
	short segment;
	real t;
	real_point3d const *p0, *p1, *p2, *p3;

	for (segment = 0; segment < count - 1; segment++)
	{
		lengths[segment] = distance3d(&points[segment], &points[segment + 1]);
		total += lengths[segment];
	}
	along = PIN(fraction, 0.f, 1.f) * total;
	for (segment = 0; segment < count - 2 && along > lengths[segment]; segment++)
		along -= lengths[segment];
	t = lengths[segment] > 0.f ? PIN(along / lengths[segment], 0.f, 1.f) : 1.f;
	p0 = &points[segment > 0 ? segment - 1 : 0];
	p1 = &points[segment];
	p2 = &points[segment + 1];
	p3 = &points[segment + 2 < count ? segment + 2 : count - 1];
#define COMMAND_CATMULL(c) (0.5f * (2.f * p1->c + (-p0->c + p2->c) * t + (2.f * p0->c - 5.f * p1->c + 4.f * p2->c - p3->c) * t * t + \
	(-p0->c + 3.f * p1->c - 3.f * p2->c + p3->c) * t * t * t))
	out->x = COMMAND_CATMULL(x);
	out->y = COMMAND_CATMULL(y);
	out->z = COMMAND_CATMULL(z);
#undef COMMAND_CATMULL
}

/* a path's heights: its cruise over the highest ground under the way in
(and under the level's sky), the descent's start over the landing spot; its
length. Its points' x y and its landing spot set */
static void command_path_cruise(
	struct command_path *path,
	real *highest_out,
	real *ceiling_out,
	real *cruise_out)
{
	real_point3d ground;
	real highest;
	real ceiling = 1e30f;
	real cruise;
	short point, sample;

	highest = path->landing.z;
	for (point = 0; point < COMMAND_PATH_POINTS - 1; point++)
	{
		for (sample = 0; sample <= COMMAND_PATH_SAMPLES; sample++)
		{
			real f = (real)sample / (real)COMMAND_PATH_SAMPLES;
			real_point3d at;

			at.x = path->points[point].x + (path->points[point + 1].x - path->points[point].x) * f;
			at.y = path->points[point].y + (path->points[point + 1].y - path->points[point].y) * f;
			at.z = path->landing.z;
			if (command_ground_highest(&at, &ground))
			{
				highest = MAX(highest, ground.z);
				ceiling = MIN(ceiling, command_ceiling(&ground));
			}
		}
	}
	cruise = MAX(highest + COMMAND_PATH_CLEARANCE, path->landing.z + COMMAND_PATH_APPROACH);
	if (cruise > ceiling - 4.f)
		cruise = MAX(ceiling - 4.f, path->landing.z + COMMAND_PATH_APPROACH);
	path->points[0].z = cruise;
	path->points[1].z = cruise;
	path->points[2].z = path->landing.z + COMMAND_PATH_APPROACH;
	path->length = 0.f;
	for (point = 0; point < COMMAND_PATH_POINTS - 1; point++)
		path->length += distance3d(&path->points[point], &path->points[point + 1]);
	path->valid = TRUE;
	*highest_out = highest;
	*ceiling_out = ceiling;
	*cruise_out = cruise;
}

/* a base's landing spot (its rally point) and the way in to it: from far
behind the base, over it, to over the landing spot; the cruise over the
highest ground under the way and under the level's sky */
static void command_path_build(
	short side)
{
	struct command_path *path = &command_paths[side];
	real_point3d base, other, ground;
	real dx, dy, length;
	real highest;
	real ceiling = 1e30f;
	real cruise;
	real back;

	path->valid = FALSE;
	if (!command_rally_point(side, &path->landing) || !command_base_point(side, &base) ||
		!command_base_point(side == _command_side_red ? _command_side_blue : _command_side_red, &other))
	{
		platform_log("command: path: %s's base has no landing spot", command_side_names[side]);
		return;
	}
	dx = other.x - base.x;
	dy = other.y - base.y;
	length = (real)sqrt(dx * dx + dy * dy);
	if (length < 1.f)
		return;
	dx /= length;
	dy /= length;
	/* (editor6: the layout's drop point of the side - its own, else the
	nearest of no side - is where its ships land, as placed) */
	if (forge_mode_drop_point(side == _command_side_red ? 0 : 1, &base, &path->landing, NULL))
	{
		if (command_ground(&path->landing, &ground, NULL) && fabs(ground.z - path->landing.z) < 3.f)
			path->landing = ground;
		path->layout_landing = TRUE;
		platform_log("command: path: %s's ships land at the layout's drop point %.2f %.2f %.2f", command_side_names[side],
			path->landing.x, path->landing.y, path->landing.z);
	}
	else
	{
		path->layout_landing = FALSE;
		if (forge_mode_layout_count(NONE) > 0)
			platform_log("command: path: the layout has no drop point for %s: its ships land before its base", command_side_names[side]);
	}
	/* (to one side of the rally point: ground about as high, nothing over
	it) */
	if (!path->layout_landing)
	{
		real_point3d rally = path->landing;
		static real const asides[] = { COMMAND_LANDING_ASIDE, -COMMAND_LANDING_ASIDE, 1.4f * COMMAND_LANDING_ASIDE,
			-1.4f * COMMAND_LANDING_ASIDE };
		short aside;

		for (aside = 0; aside < (short)NUMBEROF(asides); aside++)
		{
			real_point3d at = rally;

			at.x -= dy * asides[aside];
			at.y += dx * asides[aside];
			if (command_ground(&at, &ground, NULL) && fabs(ground.z - rally.z) < 1.5f &&
				command_ceiling(&ground) > ground.z + 2.f * COMMAND_PATH_APPROACH)
			{
				path->landing = ground;
				break;
			}
		}
	}
	path->muster = path->landing;
	path->muster.x += dx * COMMAND_MUSTER_DISTANCE;
	path->muster.y += dy * COMMAND_MUSTER_DISTANCE;
	if (command_ground(&path->muster, &ground, NULL))
		path->muster = ground;
	/* (the first point over the level's ground: nearer the base where the
	map ends sooner) */
	for (back = COMMAND_PATH_BACK; back > 10.f; back -= 10.f)
	{
		path->points[0] = base;
		path->points[0].x -= dx * back;
		path->points[0].y -= dy * back;
		if (command_ground_highest(&path->points[0], &ground))
			break;
	}
	path->points[1] = base;
	path->points[2] = path->landing;
	command_path_cruise(path, &highest, &ceiling, &cruise);
	platform_log("command: path: %s lands at %.2f %.2f %.2f, in from %.2f %.2f %.2f (%.0f wu behind its base) over %.2f %.2f "
		"at %.1f (the highest ground %.1f, the sky %.1f), %.0f wu", command_side_names[side], path->landing.x,
		path->landing.y, path->landing.z, path->points[0].x, path->points[0].y, path->points[0].z, back, path->points[1].x,
		path->points[1].y, cruise, highest, ceiling < 1e29f ? ceiling : -1.f, path->length);
}

/* how long a flight's state lasts (ticks) */
static long command_flight_ticks(
	struct command_flight const *flight,
	short state)
{
	struct command_path const *path = &flight->path;
	real seconds = 0.f;

	switch (state)
	{
	case _command_flight_in:
	case _command_flight_out:
		/* (eased: twice the average speed at the far end) */
		seconds = 1.4f * path->length / COMMAND_SHIP_SPEED;
		break;
	case _command_flight_descend:
	case _command_flight_climb:
		seconds = 0.5f + (real)fabs(path->points[COMMAND_PATH_POINTS - 1].z - flight->hover.z) / COMMAND_SHIP_VERTICAL_SPEED;
		break;
	case _command_flight_down:
		return COMMAND_WRECK_TICKS;
	default:
		return 0x7fffffff;
	}
	return (long)(seconds * (real)TICKS_PER_SECOND) + 1;
}

static void command_flight_state(
	struct command_flight *flight,
	short state)
{
	flight->state = state;
	flight->state_time = game_time_get();
	flight->state_ticks = command_flight_ticks(flight, state);
	platform_log("command: flight %d (%s, %s's squad %d): %s at tick %ld, %.1f s", (int)(flight - command_flights) + 1,
		command_ship_log_names[flight->ship], command_side_names[flight->side], flight->number,
		command_flight_log_names[state], (long)game_time_get(),
		flight->state_ticks < 0x7fffffff ? (real)flight->state_ticks / (real)TICKS_PER_SECOND : -1.f);
}

/* where a flight's ship is, ticks into its state */
static void command_flight_position(
	struct command_flight const *flight,
	real ticks,
	real_point3d *position)
{
	struct command_path const *path = &flight->path;
	real_point3d const *over = &path->points[COMMAND_PATH_POINTS - 1];
	real u = flight->state_ticks > 0 ? PIN(ticks / (real)flight->state_ticks, 0.f, 1.f) : 1.f;
	real smooth = u * u * (3.f - 2.f * u);

	switch (flight->state)
	{
	case _command_flight_in:
		/* (slowing to a stop over the landing spot) */
		command_path_at(path->points, COMMAND_PATH_POINTS, u * (2.f - u), position);
		break;
	case _command_flight_descend:
		*position = *over;
		position->z = over->z + (flight->hover.z - over->z) * smooth;
		break;
	case _command_flight_climb:
		*position = *over;
		position->z = flight->hover.z + (over->z - flight->hover.z) * smooth;
		break;
	case _command_flight_out:
	{
		real_point3d backwards[COMMAND_PATH_POINTS];
		short point;

		for (point = 0; point < COMMAND_PATH_POINTS; point++)
			backwards[point] = path->points[COMMAND_PATH_POINTS - 1 - point];
		/* (from a stop, faster as it goes) */
		command_path_at(backwards, COMMAND_PATH_POINTS, u * u, position);
		break;
	}
	default:
		*position = flight->hover;
		break;
	}
}

/* the ship's riders still in it */
static short command_flight_riders(
	struct command_flight const *flight)
{
	struct command_squad const *squad;
	short index;
	short riders = 0;

	if (flight->squad_index == NONE)
		return 0;
	squad = &command_globals.squads[flight->squad_index];
	for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
	{
		struct unit_datum *unit = command_living_unit(squad->units[index]);

		if (unit && unit->object.parent_object_index == flight->vehicle_index)
			riders++;
	}
	return riders;
}

/* the lowest rider's height under the ship (its seats', as they sit), 0 if
none */
static real command_flight_rider_depth(
	struct command_flight const *flight)
{
	struct command_squad const *squad = &command_globals.squads[flight->squad_index];
	real_point3d ship;
	real depth = 0.f;
	boolean any = FALSE;
	short index;

	object_get_origin(flight->vehicle_index, &ship);
	for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
	{
		struct unit_datum *unit = command_living_unit(squad->units[index]);
		real_point3d origin;

		if (!unit || unit->object.parent_object_index != flight->vehicle_index)
			continue;
		object_get_origin(squad->units[index], &origin);
		if (!any || ship.z - origin.z > depth)
			depth = ship.z - origin.z;
		any = TRUE;
	}
	return depth;
}

/* a squad's ship: marines ride the Pelican, the Covenant their dropship
(by the leader's actor variant: the campaign's species) */
static short command_squad_ship(
	struct command_squad const *squad)
{
	char const *name = tag_get_name(squad->actor_variant_index);

	return name && strstr(name, "marine") ? _command_ship_pelican : _command_ship_dropship;
}

/* a squad sent in by air along a path: its ship (ship NONE: its species')
made far out on the path, the squad made and seated in it; a hosted squad's
on its engine team (team NONE: a Command side's). NULL if it could not fly
(the caller makes the squad on the ground); with no ship made, the squad at
the landing spot (*flying FALSE) */
static struct command_squad *command_flight_send_on(
	short side,
	char const *composition,
	struct command_path const *path,
	short team,
	short ship_wanted,
	boolean *flying)
{
	struct command_flight *flight = NULL;
	struct command_squad *squad;
	struct object_placement_data placement;
	long tag_index;
	long list_index;
	short index;
	short seated;
	short ship;
	real_vector3d forward;

	*flying = FALSE;
	if (!path->valid)
		return NULL;
	for (index = 0; index < COMMAND_FLIGHTS; index++)
	{
		if (!command_flights[index].used)
		{
			flight = &command_flights[index];
			break;
		}
	}
	if (!flight)
	{
		platform_log("command: flight: every ship is flying (%d)", COMMAND_FLIGHTS);
		return NULL;
	}
	/* (the squad made at its landing spot - its places there -, then seated
	at once, unseen) */
	command_spawn_team = team;
	squad = command_squad_spawn(side, 1, composition, &path->landing);
	command_spawn_team = NONE;
	if (!squad)
		return NULL;
	if (team != NONE)
	{
		squad->hosted = TRUE;
		squad->team = team;
	}
	ship = ship_wanted >= 0 && ship_wanted < NUMBER_OF_COMMAND_SHIPS ? ship_wanted : command_squad_ship(squad);
	tag_index = command_vehicle_tag_find(command_ship_tags[ship]);
	csmemset(flight, 0, sizeof(*flight));
	flight->path = *path;
	flight->hosted = team != NONE;
	flight->team = team;
	flight->attacker_player_index = NONE;
	flight->attacker_unit_index = NONE;
	flight->side = side;
	flight->ship = ship;
	flight->squad_index = (short)(squad - command_globals.squads);
	flight->number = squad->number;
	flight->vehicle_index = NONE;
	flight->cargo_index = NONE;
	for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
		flight->pending_variants[index] = NONE;
	if (tag_index != NONE)
	{
		vector_from_points3d(&path->points[0], &path->points[1], &forward);
		forward.k = 0.f;
		if (normalize3d(&forward) == 0.f)
		{
			forward.i = 1.f;
			forward.j = 0.f;
		}
		object_placement_data_new(&placement, tag_index, NONE);
		SET_FLAG(placement.flags, _new_object_never_automatically_delete_bit, TRUE);
		placement.position = path->points[0];
		placement.forward = forward;
		forge_placing_exact = 1;
		flight->vehicle_index = object_new(&placement);
		forge_placing_exact = 0;
	}
	if (flight->vehicle_index == NONE)
	{
		platform_log("command: flight: no %s (%s) made at %.2f %.2f %.2f: squad %d at its base", command_ship_log_names[ship],
			tag_index != NONE ? tag_get_name(tag_index) : "not in the map's tags", path->points[0].x, path->points[0].y,
			path->points[0].z, squad->number);
		if (team == NONE)
			command_wave_landed(squad);
		return squad;
	}
	flight->used = TRUE;
	flight->yaw = (real)atan2(forward.j, forward.i);
	flight->last_tick = NONE;
	object_try_and_get(flight->vehicle_index)->object.owner_team_index = team != NONE ? team : command_side_team(side);
	/* (its lift off: command_tick holds it on its path; no player gets in
	it - its seats are its riders') */
	vehicle_hover(flight->vehicle_index, TRUE);
	unit_set_enterable_by_player(flight->vehicle_index, FALSE);
	if (ship == _command_ship_pelican && team == NONE)
		flight->cargo_index = command_pelican_cargo(flight->vehicle_index, side);
	/* (seated as the campaign's scripts seat a dropship's riders) */
	list_index = object_list_new();
	if (list_index != NONE)
	{
		for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
		{
			if (command_living_unit(squad->units[index]))
				object_list_add(list_index, squad->units[index]);
		}
		vehicle_scripting_load_magic(flight->vehicle_index, command_ship_seats[ship], list_index);
		object_list_delete(list_index);
	}
	seated = 0;
	for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
	{
		struct unit_datum *unit = command_living_unit(squad->units[index]);

		if (!unit)
			continue;
		if (unit->object.parent_object_index == flight->vehicle_index)
		{
			seated++;
			continue;
		}
		/* (no seat for it: made under the hover as the others get out) */
		platform_log("command: flight: squad %d's unit %d (%s) takes no seat of the %s: it comes at the drop",
			squad->number, index + 1, tag_get_name(squad->member_variants[index]), command_ship_log_names[ship]);
		flight->pending_variants[index] = squad->member_variants[index];
		object_delete(squad->units[index]);
		squad->units[index] = NONE;
	}
	squad->airborne = TRUE;
	squad->order = _command_order_none;
	/* (not chosen while it flies) */
	if (command_globals.selection & (1UL << flight->squad_index))
		command_selection_set(command_globals.selection & ~(1UL << flight->squad_index), "with a squad in the air");
	command_flight_state(flight, _command_flight_in);
	platform_log("command: flight %d: a %s (%lx) brings %s's squad %d (%s, %d seated) in from %.2f %.2f %.2f",
		(int)(flight - command_flights) + 1, command_ship_log_names[ship], (unsigned long)flight->vehicle_index,
		command_side_names[side], squad->number, squad->name, seated, path->points[0].x, path->points[0].y,
		path->points[0].z);
	if (team == NONE && !command_ai[side].plays)
		command_event(side, side, _command_event_inbound, squad->number, ship);
	*flying = TRUE;
	return squad;
}

/* a Command wave's squad sent in by air along its base's path. FALSE if it
could not fly (the caller makes the squad at its base) */
static boolean command_flight_send(
	short side,
	char const *composition)
{
	boolean flying;

	return command_flight_send_on(side, composition, &command_paths[side], NONE, NONE, &flying) != NULL;
}

/* the squad down: its members not seated made under the hover, its order */
static void command_flight_release(
	struct command_flight *flight)
{
	struct command_squad *squad;
	short index;

	/* (the Warthog under it set down) */
	if (object_try_and_get(flight->cargo_index))
	{
		object_detach(flight->cargo_index);
		platform_log("command: flight %d: the Warthog set down", (int)(flight - command_flights) + 1);
	}
	flight->cargo_index = NONE;

	if (flight->squad_index == NONE)
		return;
	squad = &command_globals.squads[flight->squad_index];
	flight->squad_index = NONE;
	if (!squad->used)
		return;
	for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
	{
		struct unit_datum *unit;

		if (flight->pending_variants[index] != NONE)
		{
			real_point3d place = squad->places[index];

			place.z += 0.5f;
			command_spawn_team = flight->hosted ? flight->team : (short)NONE;
			squad->units[index] = command_unit_spawn(flight->pending_variants[index], squad->side, &place,
				flight->yaw + _pi);
			command_spawn_team = NONE;
			flight->pending_variants[index] = NONE;
			continue;
		}
		/* (one still in: put out where it sits) */
		unit = command_living_unit(squad->units[index]);
		if (unit && unit->object.parent_object_index == flight->vehicle_index)
		{
			platform_log("command: flight %d: squad %d's unit %d put out (its exit did not play)",
				(int)(flight - command_flights) + 1, squad->number, index + 1);
			unit_exit_seat_end(squad->units[index]);
		}
	}
	ai_update_team_status();
	squad->airborne = FALSE;
	/* (out from under its ship, toward the field; there it holds) */
	if (flight->hosted)
	{
		/* (a mode's: held where it is down, the mode's orders next) */
		command_squad_order(squad, _command_order_hold, &flight->path.muster, NONE, NONE);
		return;
	}
	command_squad_order(squad, _command_order_move, &flight->path.muster, NONE, NONE);
	command_wave_landed(squad);
}

static void command_flight_crash_begin(struct command_flight *flight);

/* (each frame) the flights' states: down, out, back, shot down */
static void command_flights_update(
	void)
{
	short index;

	for (index = 0; index < COMMAND_FLIGHTS; index++)
	{
		struct command_flight *flight = &command_flights[index];
		struct object_datum *ship;
		long elapsed;

		if (!flight->used)
			continue;
		ship = object_try_and_get(flight->vehicle_index);
		elapsed = game_time_get() - flight->state_time;
		/* (shot down: its riders still in die with it) */
		if (flight->state != _command_flight_down && (!ship || TEST_FLAG(ship->object.damage_flags, _object_dead_bit)))
		{
			short riders = command_flight_riders(flight);

			platform_log("command: flight %d: the %s is shot down (%s), %d of squad %d still in it",
				index + 1, command_ship_log_names[flight->ship], ship ? "destroyed" : "gone", riders, flight->number);
			if (flight->squad_index != NONE)
			{
				struct command_squad *squad = &command_globals.squads[flight->squad_index];
				short member;

				for (member = 0; member < COMMAND_SQUAD_SIZE; member++)
				{
					struct unit_datum *unit = command_living_unit(squad->units[member]);

					if (unit && unit->object.parent_object_index == flight->vehicle_index)
					{
						/* (a mode's: the riders' deaths the kills of the player
						who shot the ship down) */
						if (flight->hosted && flight->attacker_player_index != NONE)
						{
							unit->unit.attackers[0].player_index = flight->attacker_player_index;
							unit->unit.attackers[0].object_index = flight->attacker_unit_index;
							unit->unit.attackers[0].game_time_stamp = (unsigned long)game_time_get();
							unit->unit.attackers[0].damage_inflicted = 1.f;
						}
						unit_kill(squad->units[member]);
					}
					flight->pending_variants[member] = NONE;
				}
				squad->airborne = FALSE;
				flight->squad_index = NONE;
				if (!flight->hosted)
					command_event(COMMAND_EVENT_EVERYONE, flight->side, _command_event_ship_lost, flight->number, flight->ship);
			}
			if (!ship)
			{
				flight->used = FALSE;
				continue;
			}
			if (!flight->killed)
				command_flight_crash_begin(flight);
			command_flight_state(flight, _command_flight_down);
			continue;
		}
		switch (flight->state)
		{
		case _command_flight_in:
			if (elapsed >= flight->state_ticks)
			{
				struct command_path const *path = &flight->path;

				/* (the hover: the lowest rider's feet the ship's drop over the
				landing spot) */
				flight->hover = path->points[COMMAND_PATH_POINTS - 1];
				flight->hover.z = path->landing.z + command_ship_drop[flight->ship] + command_flight_rider_depth(flight);
				command_flight_state(flight, _command_flight_descend);
			}
			break;
		case _command_flight_descend:
			if (elapsed >= flight->state_ticks)
			{
				command_flight_state(flight, _command_flight_hover);
				flight->last_unload_time = game_time_get() - COMMAND_UNLOAD_RETRY_TICKS;
			}
			break;
		case _command_flight_hover:
		{
			short riders = command_flight_riders(flight);

			if (riders && game_time_get() - flight->last_unload_time >= COMMAND_UNLOAD_RETRY_TICKS)
			{
				short out = vehicle_scripting_unload(flight->vehicle_index, command_ship_seats[flight->ship]);

				flight->last_unload_time = game_time_get();
				if (out)
					platform_log("command: flight %d: %d of squad %d getting out", index + 1, out, flight->number);
			}
			if ((!riders && elapsed >= COMMAND_HOVER_MINIMUM_TICKS) || elapsed >= COMMAND_HOVER_MAXIMUM_TICKS)
			{
				command_flight_release(flight);
				command_flight_state(flight, _command_flight_climb);
			}
			break;
		}
		case _command_flight_climb:
			if (elapsed >= flight->state_ticks)
				command_flight_state(flight, _command_flight_out);
			break;
		case _command_flight_out:
		case _command_flight_down:
			if (elapsed >= flight->state_ticks)
			{
				platform_log("command: flight %d: the %s %s", index + 1, command_ship_log_names[flight->ship],
					flight->state == _command_flight_out ? "is gone" : "wreck cleared");
				object_delete(flight->vehicle_index);
				flight->used = FALSE;
			}
			break;
		}
	}
}

/* (physics.c: a tick's gravity, wu a tick a tick) */
extern real global_gravity;

/* the wreck's fall shot down begun: on as it was going (the campaign's ships
have no physics of their own against the level: it would fall through it) */
static void command_flight_crash_begin(
	struct command_flight *flight)
{
	struct object_datum *ship = object_try_and_get(flight->vehicle_index);

	flight->killed = TRUE;
	flight->crash_landed = FALSE;
	flight->crash_roll = 0.f;
	flight->crash_velocity = ship ? ship->object.translational_velocity : *global_zero_vector3d;
}

/* (the tick) a wreck falling, rolling, until it meets the level */
static void command_flight_crash_tick(
	struct command_flight *flight)
{
	struct object_datum *ship = object_try_and_get(flight->vehicle_index);
	real seconds = 1.f / (real)TICKS_PER_SECOND;
	real_point3d position, next;
	real_vector3d forward, up, step;
	struct collision_result collision;

	if (!ship)
		return;
	object_get_origin(flight->vehicle_index, &position);
	if (!flight->crash_landed)
	{
		flight->crash_velocity.k -= global_gravity * (real)TICKS_PER_SECOND;
		flight->crash_roll = MIN(flight->crash_roll + 0.9f * seconds, 0.6f);
		step.i = flight->crash_velocity.i * seconds;
		step.j = flight->crash_velocity.j * seconds;
		step.k = flight->crash_velocity.k * seconds - 1.f;
		/* (the hull's belly about a wu under its origin: the ray reaches it) */
		if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
				&position, &step, NONE, &collision))
		{
			next = collision.point;
			next.z += 1.f;
			flight->crash_landed = TRUE;
			flight->crash_velocity = *global_zero_vector3d;
			platform_log("command: flight %d: the %s wreck comes down at %.2f %.2f %.2f", (int)(flight - command_flights) + 1,
				command_ship_log_names[flight->ship], next.x, next.y, next.z);
		}
		else
		{
			next.x = position.x + step.i;
			next.y = position.y + step.j;
			next.z = position.z + step.k + 1.f;
		}
	}
	else
		next = position;
	forward.i = (real)cos(flight->yaw);
	forward.j = (real)sin(flight->yaw);
	forward.k = flight->crash_landed ? -0.15f : -0.35f * flight->crash_roll;
	normalize3d(&forward);
	/* (rolled about its nose) */
	up.i = (real)sin(flight->yaw) * (real)sin(flight->crash_roll);
	up.j = -(real)cos(flight->yaw) * (real)sin(flight->crash_roll);
	up.k = (real)cos(flight->crash_roll);
	{
		real along = dot_product3d(&up, &forward);

		up.i -= forward.i * along;
		up.j -= forward.j * along;
		up.k -= forward.k * along;
		normalize3d(&up);
	}
	object_set_position(flight->vehicle_index, &next, &forward, &up);
	ship = object_get(flight->vehicle_index);
	ship->object.translational_velocity = flight->crash_velocity;
	ship->object.angular_velocity = *global_zero_vector3d;
}

/* (the tick) every ship flying on its path */
static void command_flights_tick(
	void)
{
	short index;

	for (index = 0; index < COMMAND_FLIGHTS; index++)
	{
		struct command_flight *flight = &command_flights[index];
		struct object_datum *ship;
		real ticks;
		real_point3d position, next;
		real_vector3d velocity;
		real_vector3d forward;
		real_vector3d up = { 0.f, 0.f, 1.f };
		real horizontal;
		real target_yaw;
		real turn;

		if (!flight->used)
			continue;
		if (flight->state == _command_flight_down || flight->killed)
		{
			command_flight_crash_tick(flight);
			continue;
		}
		ship = object_try_and_get(flight->vehicle_index);
		if (!ship || TEST_FLAG(ship->object.damage_flags, _object_dead_bit))
			continue;
		ticks = (real)(game_time_get() - flight->state_time);
		command_flight_position(flight, ticks, &position);
		command_flight_position(flight, ticks + 1.f, &next);
		velocity.i = (next.x - position.x) * (real)TICKS_PER_SECOND;
		velocity.j = (next.y - position.y) * (real)TICKS_PER_SECOND;
		velocity.k = (next.z - position.z) * (real)TICKS_PER_SECOND;
		/* (its nose where it goes; still, as it was; climbing out, turning
		for home) */
		target_yaw = flight->yaw;
		horizontal = (real)sqrt(velocity.i * velocity.i + velocity.j * velocity.j);
		if (horizontal > 1.f)
			target_yaw = (real)atan2(velocity.j, velocity.i);
		else if (flight->state == _command_flight_climb)
		{
			struct command_path const *path = &flight->path;

			target_yaw = (real)atan2(path->points[0].y - path->points[COMMAND_PATH_POINTS - 1].y,
				path->points[0].x - path->points[COMMAND_PATH_POINTS - 1].x);
		}
		turn = target_yaw - flight->yaw;
		while (turn > _pi)
			turn -= 2.f * _pi;
		while (turn < -_pi)
			turn += 2.f * _pi;
		turn = PIN(turn, -COMMAND_SHIP_TURN_RATE / (real)TICKS_PER_SECOND, COMMAND_SHIP_TURN_RATE / (real)TICKS_PER_SECOND);
		flight->yaw += turn;
		forward.i = (real)cos(flight->yaw);
		forward.j = (real)sin(flight->yaw);
		forward.k = 0.f;
		object_set_position(flight->vehicle_index, &position, &forward, &up);
		ship = object_get(flight->vehicle_index);
		ship->object.translational_velocity = velocity;
		ship->object.angular_velocity = *global_zero_vector3d;
		flight->last_tick = game_time_get();
	}
}

/* (damage_effect_definitions.h's DAMAGE_EFFECT_DEFINITION_TAG) */
#define COMMAND_DAMAGE_EFFECT_TAG 'jpt!'

/* the console's "command shoot <flight> [hits]": the game's own rocket's
blast on a flight's ship, hits times (tests: a ship shot down) */
static boolean command_flight_shoot(
	short number,
	short hits)
{
	struct tag_iterator iterator;
	long tag_index;
	long blast = NONE;
	struct command_flight *flight;
	short hit;

	if (number < 1 || number > COMMAND_FLIGHTS || !command_flights[number - 1].used)
	{
		platform_log("command: shoot: no flight %d", number);
		return FALSE;
	}
	flight = &command_flights[number - 1];
	tag_iterator_new(&iterator, COMMAND_DAMAGE_EFFECT_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		char const *name = tag_get_name(tag_index);

		if (tag_get_group_tag(tag_index) == COMMAND_DAMAGE_EFFECT_TAG && name && strstr(name, "rocket launcher\\explosion"))
		{
			blast = tag_index;
			break;
		}
	}
	if (blast == NONE)
	{
		platform_log("command: shoot: no rocket blast in the map's tags");
		return FALSE;
	}
	platform_log("command: shoot: flight %d's %s, body %.2f of %.0f, shield %.2f of %.0f", number,
		command_ship_log_names[flight->ship], object_get(flight->vehicle_index)->object.body_vitality,
		object_get_maximum_body_vitality(flight->vehicle_index, TRUE), object_get(flight->vehicle_index)->object.shield_vitality,
		object_get_maximum_shield_vitality(flight->vehicle_index, TRUE));
	for (hit = 0; hit < hits; hit++)
	{
		struct object_datum *ship = object_try_and_get(flight->vehicle_index);
		struct damage_data damage;

		if (!ship || TEST_FLAG(ship->object.damage_flags, _object_dead_bit))
			break;
		damage_data_new(&damage, blast);
		object_get_origin(flight->vehicle_index, &damage.origin);
		damage.epicenter = damage.origin;
		damage.direction.i = 0.f;
		damage.direction.j = 0.f;
		damage.direction.k = 1.f;
		damage.owner_team_index = command_side_team(flight->side == _command_side_red ? _command_side_blue : _command_side_red);
		object_cause_damage(&damage, flight->vehicle_index, NONE, NONE, NONE, NULL);
	}
	{
		struct object_datum *ship = object_try_and_get(flight->vehicle_index);

		platform_log("command: shoot: %d hits: the %s's body %.2f, %s", hit, command_ship_log_names[flight->ship],
			ship ? ship->object.body_vitality : 0.f, ship && TEST_FLAG(ship->object.damage_flags, _object_dead_bit) ?
			"dead" : "flying");
	}
	return TRUE;
}

/* ---- the economy (the owner's step 4; spec sections 2 and 12b): each side
earns resources - a trickle from its base, more for each supply territory
it holds - and the commander spends them on squads (by air: command_flight
_send), on his HQ's upgrades, and on his Spartan's respawn. The supply
territories are the map's own: its king of the hill hills (their markers
grouped by hill, as the king game type groups them). A side takes one by
having more of its living units in it than the other side for a few
seconds (squads, Shade crews, Spartans) */

/* (the owner, Nov 7: no free waves - each side starts with this and buys
its first squads; every unit comes only when bought) */
#define COMMAND_START_RESOURCES 250
#define COMMAND_INCOME_SECONDS 5
/* a payday's: the base's trickle (twice with the supply upgrade), each
territory held */
#define COMMAND_TRICKLE 4
#define COMMAND_TERRITORY_INCOME 5
#define COMMAND_TERRITORIES 12
#define COMMAND_TERRITORY_RADIUS_MINIMUM 5.f
#define COMMAND_TERRITORY_HEIGHT 4.f
#define COMMAND_CAPTURE_SECONDS 5
/* a hill this near a base's flag stand is the base's own */
#define COMMAND_TERRITORY_BASE_DISTANCE 10.f
/* each base's upkeep this often: its Shades manned again, its upgrades'
objects made again where gone (no free squads: the owner's, Nov 7) */
#define COMMAND_UPKEEP_SECONDS 60

struct command_territory
{
	real_point3d center;
	real radius;
	short hill;
	/* who holds it (NONE: no one), who is taking it and since when */
	short owner;
	short taker;
	long taking_since;
	short counts[NUMBER_OF_COMMAND_SIDES];
	/* (the objectives, command_objectives_begin) one of the variant's
	objective territories, its letter */
	boolean objective;
	char letter;
	/* (editor6: a layout's base or objective) its capture time, seconds
	(0: COMMAND_CAPTURE_SECONDS), and whether it is the layout's */
	short capture_seconds;
	boolean layout;
};

static struct command_territory command_territories[COMMAND_TERRITORIES];
static short command_territory_count;

/* what the HQ sells: squads (by air, on the next flight; the Hunters 60 s
after the order), then the upgrades */
enum
{
	_command_buy_squad = 0,
	_command_buy_upgrade,
	/* (no price: a choice the upgrade allows) */
	_command_buy_option,
	/* (the owner, Nov 7: the commander's Spartan late in the game - bought
	once the barracks and the armory stand, dear; each respawn after paid) */
	_command_buy_spartan,
};

enum
{
	_command_upgrade_barracks = 0,
	_command_upgrade_vehicle_bay,
	_command_upgrade_defence,
	_command_upgrade_supply,
	_command_upgrade_armory,
	NUMBER_OF_COMMAND_UPGRADES
};

struct command_buy
{
	short kind;
	char const *key;
	char const *name;
	char const *composition;
	/* an upgrade: the one it builds; an option: the one it needs; a squad:
	the one it needs (NONE: none) */
	short upgrade;
	short cost;
	/* a squad: seconds after the order it leaves; an upgrade: its build time */
	short seconds;
};

static struct command_buy const command_buys[] =
{
	{ _command_buy_squad, "marines", N_("Marines"), "marine assault rifle:4", NONE, 40, 0 },
	{ _command_buy_squad, "grunts", N_("Grunts"), "grunt minor plasma pistol:4", NONE, 40, 0 },
	{ _command_buy_squad, "jackals", N_("Grunts and Jackals"), "grunt minor plasma pistol:2,jackal minor plasma pistol:2", _command_upgrade_barracks, 60, 0 },
	{ _command_buy_squad, "elites", N_("Elite and Grunts"), "elite minor plasma rifle:1,grunt minor plasma pistol:3", _command_upgrade_barracks, 90, 0 },
	{ _command_buy_squad, "hunters", N_("Hunter pair"), "hunter:2", _command_upgrade_barracks, 220, 60 },
	{ _command_buy_upgrade, "barracks", N_("Barracks"), NULL, _command_upgrade_barracks, 150, 20 },
	{ _command_buy_upgrade, "supply", N_("Supply depot"), NULL, _command_upgrade_supply, 100, 15 },
	{ _command_buy_upgrade, "vehiclebay", N_("Vehicle bay"), NULL, _command_upgrade_vehicle_bay, 200, 25 },
	{ _command_buy_upgrade, "defence", N_("Defences"), NULL, _command_upgrade_defence, 120, 20 },
	{ _command_buy_upgrade, "armory", N_("Armory"), NULL, _command_upgrade_armory, 150, 20 },
	{ _command_buy_spartan, "spartan", N_("Spartan"), NULL, _command_upgrade_armory, 350, 0 },
	{ _command_buy_option, "secondary", N_("Spartan's secondary: switch"), NULL, _command_upgrade_armory, 0, 0 },
};
#define COMMAND_BUYS ((short)NUMBEROF(command_buys))

/* (network play, command_net.inc) an event sent to the clients; the
machine a request being handled came from (its refusals go to it alone) */
static void command_net_event_send(short audience, short subject, short code, long a, long b);
static long command_event_machine = NONE;
static void command_net_event_send_to_machine(long machine_index, short subject, short code, long a, long b);

/* an event's notice, in this machine's words (FALSE: none for it) */
static boolean command_event_text(
	short subject,
	short code,
	long a,
	long b,
	char *text,
	size_t size)
{
	boolean mine = subject == command_globals.side;

	text[0] = 0;
	switch (code)
	{
	case _command_event_order:
		if (b < 0 || b >= NUMBER_OF_COMMAND_ORDERS)
			return FALSE;
		if (a == 0)
			snprintf(text, size, T("Spartan: %s"), T(command_order_names[b]));
		else
			snprintf(text, size, T("Squad %d: %s"), (int)a, T(command_order_names[b]));
		return TRUE;
	case _command_event_arrived:
		snprintf(text, size, T("Squad %d has arrived"), (int)a);
		return TRUE;
	case _command_event_inbound:
		if (b < 0 || b >= NUMBER_OF_COMMAND_SHIPS)
			return FALSE;
		snprintf(text, size, T(command_ship_inbound[b]), (int)a);
		return TRUE;
	case _command_event_ship_lost:
		if (b < 0 || b >= NUMBER_OF_COMMAND_SHIPS)
			return FALSE;
		if (mine)
			snprintf(text, size, T(command_ship_lost[b]), (int)a);
		else
			snprintf(text, size, "%s", T(command_ship_enemy_down[b]));
		return TRUE;
	case _command_event_territory:
		if (a >= 'A' && a <= 'Z')
			snprintf(text, size, mine ? T("Territory %c taken") : T("Territory %c lost"), (char)a);
		else
			snprintf(text, size, mine ? T("Supply point %d taken") : T("Supply point %d lost"), (int)b);
		return TRUE;
	case _command_event_built:
		if (a < 0 || a >= COMMAND_BUYS)
			return FALSE;
		snprintf(text, size, T("%s built"), T(command_buys[a].name));
		return TRUE;
	case _command_event_refused:
		if (a <= 0 || a >= COMMAND_REASONS)
			return FALSE;
		snprintf(text, size, "%s", T(command_reasons[a]));
		return TRUE;
	case _command_event_ordered:
		if (a < 0 || a >= COMMAND_BUYS)
			return FALSE;
		snprintf(text, size, T("%s ordered: %d s"), T(command_buys[a].name), (int)b);
		return TRUE;
	case _command_event_building:
		if (a < 0 || a >= COMMAND_BUYS)
			return FALSE;
		snprintf(text, size, T("%s: building"), T(command_buys[a].name));
		return TRUE;
	case _command_event_secondary:
		snprintf(text, size, T("Spartan's secondary: %s"),
			T(a == 2 ? N_("Rocket Launcher") : a == 1 ? N_("Sniper Rifle") : N_("Pistol")));
		return TRUE;
	case _command_event_flag_taken:
		snprintf(text, size, "%s", mine ? T("The enemy has your flag") : T("Your team has the enemy flag"));
		return TRUE;
	case _command_event_flag_scored:
		snprintf(text, size, "%s", mine ? T("The enemy scored your flag") : T("Your team scored the flag"));
		return TRUE;
	case _command_event_flag_dropped:
		snprintf(text, size, "%s", mine ? T("Your flag was dropped") : T("The enemy flag was dropped"));
		return TRUE;
	case _command_event_flag_returned:
		snprintf(text, size, "%s", mine ? T("Your flag was returned") : T("The enemy flag was returned"));
		return TRUE;
	case _command_event_winner:
		snprintf(text, size, "%s", a == NUMBER_OF_COMMAND_SIDES ? T("Draw") : a == _command_side_red ? T("Red team wins") : T("Blue team wins"));
		return TRUE;
	case _command_event_spartan_down:
		snprintf(text, size, "%s", T("Your Spartan is down"));
		return TRUE;
	case _command_event_spartan_here:
		snprintf(text, size, "%s", T("Your Spartan is here"));
		return TRUE;
	default:
		return FALSE;
	}
}

/* an event shown to this machine's commander if it is for him (his side's,
or everyone's) */
static void command_event_show(
	short audience,
	short subject,
	short code,
	long a,
	long b)
{
	char text[COMMAND_LINE_SIZE];

	if (!command_globals.active || (audience != COMMAND_EVENT_EVERYONE && audience != command_globals.side))
		return;
	if (command_event_text(subject, code, a, b, text, sizeof(text)))
		command_notice("%s", text);
}

/* (the host's game) an event: its notice here, and to the clients it is
for (a refusal of a client's request to its machine alone) */
static void command_event(
	short audience,
	short subject,
	short code,
	long a,
	long b)
{
	if (code == _command_event_refused && command_event_machine != NONE)
	{
		command_net_event_send_to_machine(command_event_machine, subject, code, a, b);
		return;
	}
	command_event_show(audience, subject, code, a, b);
	if (command_network() && !command_client())
		command_net_event_send(audience, subject, code, a, b);
}

static char const *const command_upgrade_log_names[NUMBER_OF_COMMAND_UPGRADES] =
{
	"barracks", "vehicle bay", "defence", "supply", "armory",
};

/* each side's upgrades: built (TRUE), or building until a game time */
static boolean command_upgrades[NUMBER_OF_COMMAND_SIDES][NUMBER_OF_COMMAND_UPGRADES];
static long command_upgrade_done_time[NUMBER_OF_COMMAND_SIDES][NUMBER_OF_COMMAND_UPGRADES];

/* squads bought that leave later (the Hunters): their row, side, time */
#define COMMAND_PENDING_BUYS 8
static struct
{
	boolean used;
	short side;
	short buy;
	long time;
} command_pending_buys[COMMAND_PENDING_BUYS];

/* a side's squads bought that have not left yet (the Hunters' wait) */
static short command_net_coming(short side);

static short command_squads_coming(
	short side)
{
	short index, count = 0;

	if (command_client())
		return command_net_coming(side);
	for (index = 0; index < COMMAND_PENDING_BUYS; index++)
	{
		if (command_pending_buys[index].used && command_pending_buys[index].side == side)
			count++;
	}
	return count;
}

/* the commander's pick with the armory: the Spartan's secondary (0: the
hero kit's magnum - the owner's kit, C8 -, 1: the sniper rifle, 2: the
rocket launcher); an AI commander's Spartan takes the rocket launcher */
#define COMMAND_SECONDARIES 3
#define COMMAND_SECONDARY_AI 2
static short command_armory_pick[NUMBER_OF_COMMAND_SIDES] = { 0, 0 };

static boolean command_upgrade_built(
	short side,
	short upgrade)
{
	return command_upgrades[side][upgrade];
}

/* the map's supply territories: its hills (each hill's markers' middle, as
wide as they spread) */
static void command_territories_build(
	void)
{
	struct scenario *scenario = global_scenario_try_and_get();
	long index;

	command_territory_count = 0;
	/* (editor6, the owner's playtest: a layout played - its bases that are
	no side's home are the territories, as the editor shows them, with their
	radius, capture time and owner; the map's hills only when it has none) */
	{
		struct forge_mode_area areas[COMMAND_TERRITORIES];
		short count = forge_mode_command_territories(areas, COMMAND_TERRITORIES);

		if (count > 0)
		{
			for (index = 0; index < count; index++)
			{
				struct command_territory *territory = &command_territories[command_territory_count++];
				real_point3d ground;

				csmemset(territory, 0, sizeof(*territory));
				territory->center = areas[index].center;
				if (command_ground(&territory->center, &ground, NULL) && fabs(ground.z - territory->center.z) < 3.f)
					territory->center = ground;
				territory->radius = areas[index].radius > 0.5f ? areas[index].radius : COMMAND_TERRITORY_RADIUS_MINIMUM;
				/* (a hill number of its own: kept as a supply point, never one of the map's) */
				territory->hill = (short)(1000 + index);
				territory->owner = areas[index].owner == 0 ? _command_side_red : areas[index].owner == 1 ? _command_side_blue :
					NONE;
				territory->taker = NONE;
				territory->capture_seconds = areas[index].seconds;
				territory->layout = TRUE;
				territory->letter = areas[index].letter;
				platform_log("command: territory %d: the layout's Base %c (#%ld) at %.2f %.2f %.2f, %.1f wu across, "
					"taken in %d s, held by %s", (int)index + 1, areas[index].letter, areas[index].id, territory->center.x,
					territory->center.y, territory->center.z, territory->radius,
					territory->capture_seconds > 0 ? territory->capture_seconds : COMMAND_CAPTURE_SECONDS,
					territory->owner == NONE ? "no one" : command_side_names[territory->owner]);
			}
			platform_log("command: %d supply points (the layout's bases)", command_territory_count);
			return;
		}
		if (forge_mode_layout_count(NONE) > 0)
			platform_log("command: the layout has no bases but its two homes: the map's own hills are the territories");
	}
	for (index = 0; scenario && index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);
		short territory;

		if (flag->type != _netgame_flag_hill)
			continue;
		for (territory = 0; territory < command_territory_count; territory++)
		{
			if (command_territories[territory].hill == flag->team_index)
				break;
		}
		if (territory == command_territory_count)
		{
			if (command_territory_count >= COMMAND_TERRITORIES)
				continue;
			csmemset(&command_territories[territory], 0, sizeof(command_territories[territory]));
			command_territories[territory].hill = flag->team_index;
			command_territory_count++;
		}
		/* (summed here, the middle below; the count in counts[0]) */
		command_territories[territory].center.x += flag->position.x;
		command_territories[territory].center.y += flag->position.y;
		command_territories[territory].center.z += flag->position.z;
		command_territories[territory].counts[0]++;
	}
	for (index = 0; index < command_territory_count; index++)
	{
		struct command_territory *territory = &command_territories[index];
		short markers = territory->counts[0];
		long flag_index;
		real_point3d ground;

		territory->center.x /= markers;
		territory->center.y /= markers;
		territory->center.z /= markers;
		territory->radius = COMMAND_TERRITORY_RADIUS_MINIMUM;
		for (flag_index = 0; flag_index < scenario->netgame_flags.count; flag_index++)
		{
			struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, flag_index, struct scenario_netgame_flag);

			if (flag->type == _netgame_flag_hill && flag->team_index == territory->hill)
				territory->radius = MAX(territory->radius, command_distance2d(&flag->position, &territory->center));
		}
		if (command_ground(&territory->center, &ground, NULL))
			territory->center = ground;
		territory->owner = NONE;
		territory->taker = NONE;
		territory->counts[0] = 0;
		platform_log("command: territory %d (hill %d, %d markers) at %.2f %.2f %.2f, %.1f wu across", (int)index + 1,
			territory->hill, markers, territory->center.x, territory->center.y, territory->center.z, territory->radius);
	}
	/* (a hill on a base is not a supply point: the base pays its trickle) */
	{
		short kept = 0;
		short side;

		for (index = 0; index < command_territory_count; index++)
		{
			boolean on_base = FALSE;

			for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
			{
				real_point3d base;

				if (command_base_point(side, &base) &&
					command_distance2d(&base, &command_territories[index].center) < COMMAND_TERRITORY_BASE_DISTANCE)
				{
					on_base = TRUE;
				}
			}
			if (on_base)
			{
				platform_log("command: territory %d is on a base: not a supply point", (int)index + 1);
				continue;
			}
			command_territories[kept++] = command_territories[index];
		}
		command_territory_count = kept;
		platform_log("command: %d supply points", command_territory_count);
	}
	if (!command_territory_count)
		platform_log("command: territories: the map has no hills");
}

/* a unit's side in the game: a squad's, else a player's by his team (the
commander's Spartan red's), NONE for others */
static short command_unit_side(
	long unit_index)
{
	struct command_squad const *squad = command_squad_of_unit(unit_index, NULL);
	struct unit_datum *unit;

	if (squad)
		return squad->side;
	unit = command_living_unit(unit_index);
	if (unit && unit_index == command_side_spartan(_command_side_red))
		return _command_side_red;
	if (unit && unit_index == command_side_spartan(_command_side_blue))
		return _command_side_blue;
	return NONE;
}

/* (each frame) who stands in each territory; a side with more there for
COMMAND_CAPTURE_SECONDS takes it */
static void command_territories_update(
	void)
{
	short index;
	struct object_iterator iterator;
	struct unit_datum *unit;

	/* (who stands in each: one pass over the bipeds, each one's side found
	once - the lag's per-frame cost was territories times bipeds times the
	squads' table) */
	for (index = 0; index < command_territory_count; index++)
		command_territories[index].counts[0] = command_territories[index].counts[1] = 0;
	object_iterator_new(&iterator, _object_mask_biped, 0);
	while ((unit = object_iterator_next(&iterator)) != NULL)
	{
		real_point3d origin;
		short side = NONE;
		boolean side_known = FALSE;

		if (TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			continue;
		object_get_origin(iterator.index, &origin);
		for (index = 0; index < command_territory_count; index++)
		{
			struct command_territory *territory = &command_territories[index];

			if (command_distance2d(&origin, &territory->center) > territory->radius ||
				fabs(origin.z - territory->center.z) > COMMAND_TERRITORY_HEIGHT)
			{
				continue;
			}
			if (!side_known)
			{
				side = command_unit_side(iterator.index);
				side_known = TRUE;
			}
			if (side != NONE)
				territory->counts[side]++;
		}
	}
	for (index = 0; index < command_territory_count; index++)
	{
		struct command_territory *territory = &command_territories[index];
		short leader = NONE;

		if (territory->counts[0] > territory->counts[1])
			leader = _command_side_red;
		else if (territory->counts[1] > territory->counts[0])
			leader = _command_side_blue;
		if (leader == NONE || leader == territory->owner)
		{
			territory->taker = NONE;
			continue;
		}
		if (territory->taker != leader)
		{
			territory->taker = leader;
			territory->taking_since = game_time_get();
		}
		else if (game_time_get() - territory->taking_since >=
			(territory->capture_seconds > 0 ? territory->capture_seconds : COMMAND_CAPTURE_SECONDS) * TICKS_PER_SECOND)
		{
			territory->owner = leader;
			territory->taker = NONE;
			platform_log("command: territory %d taken by %s (%d to %d)", index + 1, command_side_names[leader],
				territory->counts[leader], territory->counts[!leader]);
			command_event(COMMAND_EVENT_EVERYONE, leader, _command_event_territory, territory->letter, index + 1);
		}
	}
}

static void command_upgrade_objects_update(short side);

/* (each frame) paydays; upgrades finished; bought squads that leave now */
static void command_economy_update(
	void)
{
	short side, index;

	if (game_time_get() >= command_globals.next_income_time)
	{
		command_globals.next_income_time = game_time_get() + COMMAND_INCOME_SECONDS * TICKS_PER_SECOND;
		for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
		{
			long income = COMMAND_TRICKLE * (command_upgrade_built(side, _command_upgrade_supply) ? 2 : 1);

			for (index = 0; index < command_territory_count; index++)
			{
				if (command_territories[index].owner == side)
					income += COMMAND_TERRITORY_INCOME;
			}
			command_globals.resources[side] += income;
		}
	}
	for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
	{
		for (index = 0; index < NUMBER_OF_COMMAND_UPGRADES; index++)
		{
			if (!command_upgrades[side][index] && command_upgrade_done_time[side][index] &&
				game_time_get() >= command_upgrade_done_time[side][index])
			{
				command_upgrades[side][index] = TRUE;
				platform_log("command: %s's %s built", command_side_names[side], command_upgrade_log_names[index]);
				command_upgrade_objects_update(side);
				if (index == _command_upgrade_armory && command_living_unit(command_spartans[side].unit_index))
					command_spartan_loadout(command_spartans[side].unit_index);
				if (!command_ai[side].plays)
				{
					short row;

					for (row = 0; row < COMMAND_BUYS; row++)
					{
						if (command_buys[row].kind == _command_buy_upgrade && command_buys[row].upgrade == index)
							command_event(side, side, _command_event_built, row, 0);
					}
				}
			}
		}
	}
	for (index = 0; index < COMMAND_PENDING_BUYS; index++)
	{
		if (command_pending_buys[index].used && game_time_get() >= command_pending_buys[index].time)
		{
			command_pending_buys[index].used = FALSE;
			if (!command_flight_send(command_pending_buys[index].side, command_buys[command_pending_buys[index].buy].composition))
				platform_log("command: buy: %s's %s found no ship (refunded)", command_side_names[command_pending_buys[index].side],
					command_buys[command_pending_buys[index].buy].key);
		}
	}
}

/* whether a side may buy a row now, why not if not (a notice) */
static char const *command_buy_blocked(
	short side,
	short buy_index)
{
	struct command_buy const *buy = &command_buys[buy_index];

	if (buy->kind == _command_buy_squad)
	{
		if (buy->upgrade != NONE && !command_upgrade_built(side, buy->upgrade))
			return N_("Needs the barracks");
		if (command_ai_alive(side) + command_squads_coming(side) * COMMAND_SQUAD_SIZE + COMMAND_SQUAD_SIZE > COMMAND_AI_MAXIMUM_PER_TEAM)
			return N_("No room for more troops");
	}
	else if (buy->kind == _command_buy_spartan)
	{
		if (!command_upgrade_built(side, _command_upgrade_barracks) || !command_upgrade_built(side, _command_upgrade_armory))
			return N_("Needs the barracks and the armory");
		if (command_spartan_owned[side])
			return N_("Already in the field");
	}
	else if (buy->kind == _command_buy_option)
	{
		if (!command_upgrade_built(side, buy->upgrade))
			return N_("Needs the armory");
	}
	else
	{
		if (command_upgrades[side][buy->upgrade])
			return N_("Already built");
		if (command_upgrade_done_time[side][buy->upgrade])
			return N_("Being built");
	}
	if (command_globals.resources[side] < buy->cost)
		return N_("Can't afford");
	return NULL;
}

/* a side buys a row: a squad sent (the Hunters later), an upgrade begun.
FALSE if it could not */
static void command_armory_choose(short side, short pick);

static boolean command_buy(
	short side,
	short buy_index)
{
	struct command_buy const *buy = &command_buys[buy_index];
	char const *blocked = command_buy_blocked(side, buy_index);

	if (blocked)
	{
		platform_log("command: buy: %s's %s: %s (%ld of %d)", command_side_names[side], buy->key, blocked,
			command_globals.resources[side], buy->cost);
		if (!command_ai[side].plays)
			command_event(side, side, _command_event_refused, command_reason_of(blocked), 0);
		return FALSE;
	}
	if (buy->kind == _command_buy_squad)
	{
		if (buy->seconds > 0)
		{
			short index;

			for (index = 0; index < COMMAND_PENDING_BUYS && command_pending_buys[index].used; index++)
				;
			if (index == COMMAND_PENDING_BUYS)
				return FALSE;
			command_pending_buys[index].used = TRUE;
			command_pending_buys[index].side = side;
			command_pending_buys[index].buy = buy_index;
			command_pending_buys[index].time = game_time_get() + buy->seconds * TICKS_PER_SECOND;
			if (!command_ai[side].plays)
				command_event(side, side, _command_event_ordered, buy_index, buy->seconds);
		}
		else if (!command_flight_send(side, buy->composition))
		{
			/* (its troops not in the map's tags nor the packs: said, not
			paid) */
			platform_log("command: buy: %s's %s could not be sent (not paid)", command_side_names[side], buy->key);
			if (!command_ai[side].plays)
				command_event(side, side, _command_event_refused, command_reason_of(N_("Those troops can't come: their files are missing")), 0);
			return FALSE;
		}
	}
	else if (buy->kind == _command_buy_spartan)
	{
		if (!command_spartan_bought(side))
			return FALSE;
	}
	else if (buy->kind == _command_buy_option)
	{
		if (!command_ai[side].plays)
		{
			command_armory_choose(side, (short)((command_armory_pick[side] + 1) % COMMAND_SECONDARIES));
			command_event(side, side, _command_event_secondary, command_armory_pick[side], 0);
		}
	}
	else
	{
		command_upgrade_done_time[side][buy->upgrade] = game_time_get() + buy->seconds * TICKS_PER_SECOND;
		if (!command_ai[side].plays)
			command_event(side, side, _command_event_building, buy_index, 0);
	}
	command_globals.resources[side] -= buy->cost;
	platform_log("command: buy: %s bought %s for %d (%ld left)", command_side_names[side], buy->key, buy->cost,
		command_globals.resources[side]);
	return TRUE;
}

/* the HQ's row bought by this machine's commander (the host's to do: a
client asks it) */
static void command_buy_here(
	short row)
{
	if (command_client())
		command_net_request_buy(row);
	else
		command_buy(command_globals.side, row);
}

/* the HQ panel's buttons: Y (Triangle; F) opens and closes it, the D-pad up
and down or the left stick choose its row (held: again and again, as the
menus' lists), A (Cross) buys it, B (Circle) closes; the keys 1 to 9 buy
their row */
static void command_hq_input(
	struct command_pad const *pad,
	boolean const *pressed,
	unsigned long keys_pressed)
{
	short digit;
	short direction;

	if (pressed[_gamepad_analog_button_y] || (keys_pressed & COMMAND_KEY_F))
	{
		command_globals.hq_open = !command_globals.hq_open;
		platform_log("command: HQ %s", command_globals.hq_open ? "open" : "closed");
	}
	if (!command_globals.hq_open)
	{
		command_hq_held_direction = 0;
		return;
	}
	direction = 0;
	if (pad->down[_gamepad_binary_button_dpad_up] || pad->sticks[_gamepad_stick_left][1] > COMMAND_HQ_STICK_PUSH)
		direction = -1;
	else if (pad->down[_gamepad_binary_button_dpad_down] || pad->sticks[_gamepad_stick_left][1] < -COMMAND_HQ_STICK_PUSH)
		direction = 1;
	if (direction != command_hq_held_direction)
	{
		command_hq_held_direction = direction;
		command_hq_repeat_at = system_milliseconds() + COMMAND_HQ_REPEAT_FIRST_MILLISECONDS;
		if (direction)
			command_globals.hq_row = (short)((command_globals.hq_row + COMMAND_BUYS + direction) % COMMAND_BUYS);
	}
	else if (direction && (long)(system_milliseconds() - command_hq_repeat_at) >= 0)
	{
		command_hq_repeat_at = system_milliseconds() + COMMAND_HQ_REPEAT_MILLISECONDS;
		command_globals.hq_row = (short)((command_globals.hq_row + COMMAND_BUYS + direction) % COMMAND_BUYS);
	}
	if (pressed[_gamepad_analog_button_a])
		command_buy_here(command_globals.hq_row);
	if (pressed[_gamepad_analog_button_b])
		command_globals.hq_open = FALSE;
	for (digit = 0; digit < 9 && digit < COMMAND_BUYS; digit++)
	{
		if (keys_pressed & (1UL << (COMMAND_KEY_DIGIT_FIRST + digit)))
			command_buy_here(digit);
	}
}

/* ---- the upgrades' objects (step 4c; spec section 2): each built upgrade
makes the games' own objects appear on its base, made again with the base's
next wave if destroyed or taken, as the Shades are:
- the vehicle bay: the map's Warthog and Ghost beside the base, and each
  Pelican flies in with a Warthog under it (dropped at the hover);
- the defences: two more Shades on the roof's flanks, Grunt-manned;
- the supply depot: the Covenant's supply containers beside the base (the
  campaign's c_storage; Halo CE has no UNSC crates);
- the armory: the map's sniper rifle and rocket launcher in the base, and
  the commander's Spartan's secondary the one he picks (the spec's 12b).
The spots are the base's own (its flag stand, toward the other base and to
its sides, on the ground there) until the Command setup's upgrade markers */

#define COMMAND_BAY_VEHICLES 2
#define COMMAND_CRATES 3
#define COMMAND_ARMORY_ITEMS 2

static struct
{
	char const *suffix;
	unsigned long group;
	real along;
	real aside;
	real lift;
} const command_bay_objects[COMMAND_BAY_VEHICLES] =
{
	{ "\\warthog\\warthog", 'vehi', -1.f, 11.f, 0.5f },
	{ "\\ghost_mp", 'vehi', 4.f, 11.f, 0.5f },
};
static real const command_crate_spots[COMMAND_CRATES][2] = { { -2.f, -10.f }, { 0.5f, -10.5f }, { -1.f, -12.5f } };
static char const *const command_armory_items[COMMAND_ARMORY_ITEMS] =
{
	"weapons\\sniper rifle\\sniper rifle", "weapons\\rocket launcher\\rocket launcher",
};
#define COMMAND_CRATE_TAG "scenery\\c_storage\\c_storage"
/* the Pelican's cargo: the campaign's Warthog under it, at its marker */
#define COMMAND_PELICAN_CARGO_MARKER "cargo"

static long command_bay[NUMBER_OF_COMMAND_SIDES][COMMAND_BAY_VEHICLES];

/* (the match's lag, Ben's insider.7 report: a 16-minute AI-against-AI
match made 6 -> 18 vehicles, the tick 0.5 -> 1.0 ms on the PC: a bay
vehicle driven off is made again, each Pelican brings a Warthog, and the
wrecks stay) each side's vehicles of Command's making, at most
COMMAND_SIDE_VEHICLES_LIVING living: a new one only in a free place, a
wreck let go (deleted) to make one */
#define COMMAND_SIDE_VEHICLES 8
#define COMMAND_SIDE_VEHICLES_LIVING 4
static long command_side_vehicles[NUMBER_OF_COMMAND_SIDES][COMMAND_SIDE_VEHICLES];

static boolean command_side_vehicle_room(
	short side)
{
	short index, living = 0;

	for (index = 0; index < COMMAND_SIDE_VEHICLES; index++)
	{
		long vehicle_index = command_side_vehicles[side][index];
		struct object_datum *object = vehicle_index != NONE ? object_try_and_get(vehicle_index) : NULL;

		if (!object)
			command_side_vehicles[side][index] = NONE;
		else if (TEST_FLAG(object->object.damage_flags, _object_dead_bit))
		{
			object_delete(vehicle_index);
			command_side_vehicles[side][index] = NONE;
		}
		else
			living++;
	}
	return living < COMMAND_SIDE_VEHICLES_LIVING;
}

static void command_side_vehicle_add(
	short side,
	long vehicle_index)
{
	short index;

	if (vehicle_index == NONE)
		return;
	for (index = 0; index < COMMAND_SIDE_VEHICLES; index++)
	{
		if (command_side_vehicles[side][index] == NONE)
		{
			command_side_vehicles[side][index] = vehicle_index;
			return;
		}
	}
}
static long command_crates[NUMBER_OF_COMMAND_SIDES][COMMAND_CRATES];
static long command_armory[NUMBER_OF_COMMAND_SIDES][COMMAND_ARMORY_ITEMS];

/* the Spartan's secondary: the armory's pick once red has it, else the
loadout's */
static char const *command_spartan_secondary(
	short side)
{
	/* (the commander's pick; an AI commander's the rocket launcher) */
	if (command_upgrade_built(side, _command_upgrade_armory))
	{
		short pick = command_ai[side].plays ? COMMAND_SECONDARY_AI : command_armory_pick[side];

		/* (the magnum: the kit's own) */
		if (pick > 0)
			return command_armory_items[pick - 1];
	}
	return command_hero_loadout.secondary;
}

static short command_shades_wanted(
	short side)
{
	return (short)(COMMAND_SHADES_STANDARD + (command_upgrade_built(side, _command_upgrade_defence) ? 2 : 0));
}

/* a tag of a group whose name ends so, NONE if none */
static long command_tag_find(
	unsigned long group,
	char const *suffix)
{
	struct tag_iterator iterator;
	long tag_index;
	size_t length = strlen(suffix);

	tag_iterator_new(&iterator, group);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		char const *name = tag_get_name(tag_index);
		size_t name_length = name ? strlen(name) : 0;

		if (tag_get_group_tag(tag_index) == group && name_length >= length && !strcmp(name + name_length - length, suffix))
			return tag_index;
	}
	return NONE;
}

/* a spot by a base: along toward the other base, aside to its right, on
the ground there */
static boolean command_base_spot(
	short side,
	real along,
	real aside,
	real_point3d *point,
	real_vector3d *forward)
{
	real_point3d base, other, ground;
	real dx, dy, length;

	if (!command_base_point(side, &base) || !command_base_point(side == _command_side_red ? _command_side_blue :
		_command_side_red, &other))
	{
		return FALSE;
	}
	dx = other.x - base.x;
	dy = other.y - base.y;
	length = (real)sqrt(dx * dx + dy * dy);
	if (length < 1.f)
		return FALSE;
	dx /= length;
	dy /= length;
	*point = base;
	point->x += dx * along + dy * aside;
	point->y += dy * along - dx * aside;
	if (!command_ground(point, &ground, NULL))
		return FALSE;
	*point = ground;
	forward->i = dx;
	forward->j = dy;
	forward->k = 0.f;
	return TRUE;
}

/* one of the games' objects made (the tag itself, not the game type's
set), on the side's team; NONE if it could not be */
static long command_object_place(
	long tag_index,
	real_point3d const *point,
	real_vector3d const *forward,
	short side)
{
	struct object_placement_data placement;
	long object_index;

	if (tag_index == NONE)
		return NONE;
	object_placement_data_new(&placement, tag_index, NONE);
	SET_FLAG(placement.flags, _new_object_never_automatically_delete_bit, TRUE);
	placement.position = *point;
	placement.forward = *forward;
	forge_placing_exact = 1;
	object_index = object_new(&placement);
	forge_placing_exact = 0;
	if (object_index != NONE)
		object_try_and_get(object_index)->object.owner_team_index = command_side_team(side);
	return object_index;
}

/* whether an upgrade's object is still there: not destroyed, not taken (a
weapon picked up, a vehicle driven off further than this) */
#define COMMAND_UPGRADE_OBJECT_LEASH 25.f

static boolean command_upgrade_object_there(
	long object_index,
	real_point3d const *spot)
{
	struct object_datum *object = object_try_and_get(object_index);
	real_point3d origin;

	if (!object || TEST_FLAG(object->object.damage_flags, _object_dead_bit) || object->object.parent_object_index != NONE)
		return FALSE;
	object_get_origin(object_index, &origin);
	return command_distance2d(&origin, spot) < COMMAND_UPGRADE_OBJECT_LEASH;
}

/* (an upgrade built, each wave) a side's upgrades' objects there: made
where gone (a vehicle driven off is left to whoever has it, and another
made) */
static void command_upgrade_objects_update(
	short side)
{
	short index;
	real_point3d spot;
	real_vector3d forward;

	if (command_upgrade_built(side, _command_upgrade_vehicle_bay))
	{
		for (index = 0; index < COMMAND_BAY_VEHICLES; index++)
		{
			if (!command_base_spot(side, command_bay_objects[index].along, command_bay_objects[index].aside, &spot, &forward) ||
				command_upgrade_object_there(command_bay[side][index], &spot))
			{
				continue;
			}
			spot.z += command_bay_objects[index].lift;
			if (!command_side_vehicle_room(side))
			{
				platform_log("command: vehicle bay: %s has %d vehicles out: no %s made", command_side_names[side],
					COMMAND_SIDE_VEHICLES_LIVING, command_bay_objects[index].suffix + 1);
				continue;
			}
			command_bay[side][index] = command_object_place(command_tag_find(command_bay_objects[index].group,
				command_bay_objects[index].suffix), &spot, &forward, side);
			command_side_vehicle_add(side, command_bay[side][index]);
			platform_log("command: vehicle bay: %s's %s %s at %.2f %.2f %.2f", command_side_names[side],
				command_bay_objects[index].suffix + 1, command_bay[side][index] != NONE ? "made" : "NOT made", spot.x, spot.y, spot.z);
		}
	}
	if (command_upgrade_built(side, _command_upgrade_supply))
	{
		for (index = 0; index < COMMAND_CRATES; index++)
		{
			if (!command_base_spot(side, command_crate_spots[index][0], command_crate_spots[index][1], &spot, &forward) ||
				object_try_and_get(command_crates[side][index]))
			{
				continue;
			}
			command_crates[side][index] = command_object_place(command_tag_find('scen', COMMAND_CRATE_TAG), &spot, &forward, side);
			platform_log("command: supply depot: %s's container %d %s at %.2f %.2f %.2f", command_side_names[side], index + 1,
				command_crates[side][index] != NONE ? "made" : "NOT made", spot.x, spot.y, spot.z);
		}
	}
	if (command_upgrade_built(side, _command_upgrade_armory))
	{
		for (index = 0; index < COMMAND_ARMORY_ITEMS; index++)
		{
			real_point3d base;

			if (!command_base_point(side, &base))
				continue;
			spot = base;
			spot.x += index ? 0.8f : -0.8f;
			spot.z += 0.4f;
			if (command_upgrade_object_there(command_armory[side][index], &spot))
				continue;
			forward.i = 1.f;
			forward.j = forward.k = 0.f;
			command_armory[side][index] = command_object_place(command_weapon_tag_find(command_armory_items[index]), &spot,
				&forward, side);
			platform_log("command: armory: %s's %s %s", command_side_names[side], command_armory_items[index],
				command_armory[side][index] != NONE ? "made" : "NOT made");
		}
	}
	if (command_upgrade_built(side, _command_upgrade_defence))
		command_shades_update();
}

/* the Pelican's Warthog (the vehicle bay's): made under it at its cargo
marker as it leaves, dropped at the hover; NONE if none */
static long command_pelican_cargo(
	long pelican_index,
	short side)
{
	struct object_marker marker;
	long tag_index = command_tag_find('vehi', command_bay_objects[0].suffix);
	real_point3d at;
	real_vector3d forward = { 1.f, 0.f, 0.f };
	long warthog;

	if (!command_upgrade_built(side, _command_upgrade_vehicle_bay) || tag_index == NONE)
		return NONE;
	if (!object_get_marker_by_name(pelican_index, COMMAND_PELICAN_CARGO_MARKER, &marker, 1))
	{
		platform_log("command: the Pelican has no \"%s\" marker: no Warthog under it", COMMAND_PELICAN_CARGO_MARKER);
		return NONE;
	}
	if (!command_side_vehicle_room(side))
		return NONE;
	object_get_origin(pelican_index, &at);
	warthog = command_object_place(tag_index, &at, &forward, side);
	if (warthog == NONE)
		return NONE;
	command_side_vehicle_add(side, warthog);
	object_attach_to_marker(pelican_index, COMMAND_PELICAN_CARGO_MARKER, warthog, "");
	platform_log("command: the Pelican carries a Warthog (%lx)", (unsigned long)warthog);
	return warthog;
}

/* the armory's pick: the Spartan's secondary, now and at every spawn */
static void command_armory_choose(
	short side,
	short pick)
{
	command_armory_pick[side] = (short)PIN(pick, 0, COMMAND_SECONDARIES - 1);
	platform_log("command: armory: %s's Spartan's secondary is the %s", command_side_names[side],
		command_spartan_secondary(side));
	if (command_living_unit(command_side_spartan(side)))
		command_spartan_loadout(command_side_spartan(side));
}

/* ---- the objectives (the owner's step 5; spec sections 1 and 2): the
variant's territories, its flag, or both (decision 2). A side scores a point
each second for each objective territory it holds - the supply points
nearest the middle of the map, between the bases, held as any supply point
is (squads, Shade crews, Spartans: the most in it for 5 s) and paying as one
- and COMMAND_CAPTURE_POINTS for each capture of the other side's flag. Only
Spartans carry a flag (a player's unit, or a side's Spartan as AI; never a
squad's member), as Halo CE's CTF plays it: the game's own flag (the map's
multiplayer flag) at each base's flag stand, taken by touching it, scored
by bringing it to the carrier's own stand while his own flag is at home,
dropped where its carrier dies, home again when a Spartan of its side
touches it or after CE's 17 s untouched. The first side to the score limit
wins, or the higher score when the time runs out (the same: a draw); the
game then ends as the game's own do (game_engine_end_game: its game over,
its scores, the carnage report, command_carnage_report) */

/* (C4, the owner: too quick to win - 300 was three territories held for 100 s) */
#define COMMAND_SCORE_LIMIT 1000
#define COMMAND_CAPTURE_POINTS 100
#define COMMAND_TIME_LIMIT_MINUTES 20
#define COMMAND_OBJECTIVE_TERRITORIES 3
/* a flag taken, returned or scored by a Spartan this near (across, up or
down) */
#define COMMAND_FLAG_TOUCH 1.f
#define COMMAND_FLAG_TOUCH_HEIGHT 1.5f
/* (game_engine_ctf.c's CTF_FLAG_RETURN_TIME) */
#define COMMAND_FLAG_RETURN_SECONDS 17
/* a flag run's goal this near a flag stand is in its base: from further
than the approach, the way goes by the base's rally point */
#define COMMAND_FLAG_STAND_NEAR 12.f
#define COMMAND_FLAG_STAND_APPROACH 20.f
#define COMMAND_HURTS 64

enum
{
	_command_objectives_territories = 1 << 0,
	_command_objectives_flag = 1 << 1,
	_command_objectives_both = _command_objectives_territories | _command_objectives_flag,
};

/* the winner when neither side won */
#define COMMAND_DRAW NUMBER_OF_COMMAND_SIDES

/* (game_engine_multiplayer_sounds.c's multiplayer_information_sound: the
CTF announcer's) */
enum
{
	_command_sound_blue_team_has_the_flag = 8,
	_command_sound_blue_team_flag_returned = 9,
	_command_sound_blue_team_score = 10,
	_command_sound_red_team_has_the_flag = 11,
	_command_sound_red_team_flag_returned = 12,
	_command_sound_red_team_score = 13,
};

struct command_flag
{
	boolean valid;
	real_point3d stand;
	long weapon_index;
	/* off its stand; dropped since (NONE: carried, or home); carried by */
	boolean away;
	long dropped_time;
	long carrier_index;
	/* (the log's: said once) */
	boolean blocked_said;
	boolean take_failed_said;
};

static struct
{
	/* the variant (the Forge lobby's OBJECTIVE, the console's) */
	short objectives;
	long score_limit;
	long time_limit_minutes;
	/* the game: each side's score, its captures, its seconds of territory
	held, the units it killed and lost; the commander's Spartan's kills and
	captures */
	long scores[NUMBER_OF_COMMAND_SIDES];
	long captures[NUMBER_OF_COMMAND_SIDES];
	long territory_seconds[NUMBER_OF_COMMAND_SIDES];
	long kills[NUMBER_OF_COMMAND_SIDES];
	long losses[NUMBER_OF_COMMAND_SIDES];
	long spartan_kills;
	long spartan_captures;
	long start_time;
	long next_second_time;
	boolean over;
	short winner;
	struct command_flag flags[NUMBER_OF_COMMAND_SIDES];
	/* who last hurt each unit (the kills' tally) */
	struct
	{
		long unit_index;
		long attacker_index;
		short attacker_side;
		boolean attacker_spartan;
	} hurts[COMMAND_HURTS];
	short next_hurt;
	long spartan_counted_index;
} command_objectives = { _command_objectives_both, COMMAND_SCORE_LIMIT, COMMAND_TIME_LIMIT_MINUTES };

static char const *const command_objectives_log_names[] = { "none", "territories", "flag", "territories and flag" };

/* whether a unit is a Spartan, who alone carry a flag: a player's (not the
commander's placeholder body), or a side's Spartan as AI */
static boolean command_unit_is_spartan(
	long unit_index)
{
	struct unit_datum *unit = command_living_unit(unit_index);
	struct command_squad const *squad;

	if (!unit || command_unit_is_seat(unit_index) || unit->object.type != _object_type_biped)
		return FALSE;
	squad = command_squad_of_unit(unit_index, NULL);
	if (squad)
		return squad->hero;
	return unit->unit.player_index != NONE;
}

/* (the damage hook) who hurt a unit last, for its death's tally */
static void command_hurt_note(
	long unit_index,
	long attacker_index)
{
	short index, slot = NONE;
	short attacker_side;

	if (attacker_index == NONE || attacker_index == unit_index)
		return;
	attacker_side = command_unit_side(attacker_index);
	for (index = 0; index < COMMAND_HURTS; index++)
	{
		if (command_objectives.hurts[index].unit_index == unit_index)
		{
			slot = index;
			break;
		}
	}
	if (slot == NONE)
	{
		slot = command_objectives.next_hurt;
		command_objectives.next_hurt = (short)((command_objectives.next_hurt + 1) % COMMAND_HURTS);
	}
	command_objectives.hurts[slot].unit_index = unit_index;
	command_objectives.hurts[slot].attacker_index = attacker_index;
	command_objectives.hurts[slot].attacker_side = attacker_side;
	command_objectives.hurts[slot].attacker_spartan = attacker_index != NONE &&
		(attacker_index == command_side_spartan(_command_side_red) || attacker_index == command_side_spartan(_command_side_blue));
}

/* a side's unit dead: its loss, and the kill of whoever hurt it last */
static void command_death_count(
	long unit_index,
	short side)
{
	short index;

	command_objectives.losses[side]++;
	for (index = 0; index < COMMAND_HURTS; index++)
	{
		if (command_objectives.hurts[index].unit_index != unit_index)
			continue;
		command_objectives.hurts[index].unit_index = NONE;
		if (command_objectives.hurts[index].attacker_side != NONE && command_objectives.hurts[index].attacker_side != side)
		{
			command_objectives.kills[command_objectives.hurts[index].attacker_side]++;
			if (command_objectives.hurts[index].attacker_spartan)
				command_objectives.spartan_kills++;
		}
		break;
	}
}

/* (each frame) the squads' members and the commander's Spartan dead since
counted */
static void command_losses_update(
	void)
{
	short squad_index, member;

	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad *squad = &command_globals.squads[squad_index];

		if (!squad->used || squad->hero)
			continue;
		for (member = 0; member < COMMAND_SQUAD_SIZE; member++)
		{
			if (squad->units[member] == NONE || squad->counted[member] || command_living_unit(squad->units[member]))
				continue;
			squad->counted[member] = TRUE;
			command_death_count(squad->units[member], squad->side);
		}
	}
}

/* (the game begun) the objective territories: those nearest the middle
between the bases, lettered from red's */
/* (C2, the owner: the objectives in proper spots) the objective
territories are the field's own: its middle between the bases and its two
flanks, across the line between them - A on red's left, B the middle, C on
its right -, each on flat open ground (COMMAND_OBJECTIVE_FLAT within
COMMAND_OBJECTIVE_RADIUS about it, near the middle's height: the canyon's
floor, not a hilltop or a cliff), a clear capture area of their own. A
supply point (a hill) on one is let go */
#define COMMAND_OBJECTIVE_RADIUS 7.f
#define COMMAND_OBJECTIVE_FLAT 1.4f
#define COMMAND_OBJECTIVE_FLOOR 4.f
#define COMMAND_OBJECTIVE_FLANK_FARTHEST 30.f
#define COMMAND_OBJECTIVE_FLANK_NEAREST 14.f
#define COMMAND_OBJECTIVE_SUPPLY_APART 14.f
/* (1.1.4) how far from the spot wanted one of the map's hills may stand in
for it (the big team maps) */
#define COMMAND_OBJECTIVE_HILL_REACH 60.f

/* whether a spot is flat open ground near a height (its ground in *ground) */
static boolean command_objective_spot_flat(
	real_point3d const *point,
	real floor_height,
	real_point3d *ground)
{
	short index;

	if (!command_ground(point, ground, NULL) || fabs(ground->z - floor_height) > COMMAND_OBJECTIVE_FLOOR)
		return FALSE;
	for (index = 0; index < 8; index++)
	{
		real angle = (real)index * _pi / 4.f;
		real_point3d probe = *ground;
		real_point3d probe_ground;

		probe.x += (real)cos(angle) * COMMAND_OBJECTIVE_RADIUS * 0.8f;
		probe.y += (real)sin(angle) * COMMAND_OBJECTIVE_RADIUS * 0.8f;
		if (!command_ground(&probe, &probe_ground, NULL) || fabs(probe_ground.z - ground->z) > COMMAND_OBJECTIVE_FLAT)
			return FALSE;
	}
	return TRUE;
}

static void command_objective_territories_choose(
	void)
{
	real_point3d bases[NUMBER_OF_COMMAND_SIDES];
	real_point3d middle, middle_ground, spots[COMMAND_OBJECTIVE_TERRITORIES];
	boolean placed[COMMAND_OBJECTIVE_TERRITORIES];
	boolean middle_valid;
	real along_x, along_y, across_x, across_y, length;
	short index, spot;

	/* (the objectives of a game before let go: the supply points stay) */
	{
		short kept = 0;

		for (index = 0; index < command_territory_count; index++)
		{
			if (command_territories[index].hill != NONE)
				command_territories[kept++] = command_territories[index];
		}
		command_territory_count = kept;
	}
	if (!(command_objectives.objectives & _command_objectives_territories) ||
		!command_base_point(_command_side_red, &bases[0]) || !command_base_point(_command_side_blue, &bases[1]))
	{
		return;
	}
	/* (editor6: the layout's objectives - its Firefight objectives of a
	zone's kind and its hill points - are the objective territories, as
	placed; chosen here only when it has none) */
	{
		struct forge_mode_area areas[COMMAND_TERRITORIES];
		short count = forge_mode_command_objectives(areas, COMMAND_TERRITORIES);

		if (count > 0)
		{
			for (index = 0; index < count && command_territory_count < COMMAND_TERRITORIES; index++)
			{
				struct command_territory *territory = &command_territories[command_territory_count++];
				real_point3d ground;

				csmemset(territory, 0, sizeof(*territory));
				territory->center = areas[index].center;
				if (command_ground(&territory->center, &ground, NULL) && fabs(ground.z - territory->center.z) < 3.f)
					territory->center = ground;
				territory->radius = areas[index].radius > 0.5f ? areas[index].radius : COMMAND_OBJECTIVE_RADIUS;
				territory->hill = NONE;
				territory->owner = NONE;
				territory->taker = NONE;
				territory->objective = TRUE;
				territory->layout = TRUE;
				territory->letter = areas[index].letter;
				platform_log("command: objective territory %c (territory %d): the layout's objective #%ld at %.2f %.2f %.2f, "
					"%.1f wu across", territory->letter, command_territory_count, areas[index].id, territory->center.x,
					territory->center.y, territory->center.z, territory->radius);
			}
			return;
		}
		if (forge_mode_layout_count(NONE) > 0)
			platform_log("command: the layout has no objectives (Firefight objective zones, hill points): chosen between the bases");
	}
	middle.x = (bases[0].x + bases[1].x) / 2.f;
	middle.y = (bases[0].y + bases[1].y) / 2.f;
	middle.z = (bases[0].z + bases[1].z) / 2.f;
	along_x = bases[1].x - bases[0].x;
	along_y = bases[1].y - bases[0].y;
	length = (real)sqrt(along_x * along_x + along_y * along_y);
	if (length < 1.f)
		return;
	/* (1.1.4, the big team maps: no ground between the bases at all -
	Danger Canyon's bases face each other across a chasm -: the objectives
	are the map's own hills, below) */
	middle_valid = command_ground(&middle, &middle_ground, NULL);
	if (!middle_valid)
	{
		middle_ground = middle;
		platform_log("command: objectives: no ground between the bases (%.2f %.2f): the map's own hills", middle.x, middle.y);
	}
	along_x /= length;
	along_y /= length;
	/* (red's left, looking toward blue) */
	across_x = -along_y;
	across_y = along_x;
	for (spot = 0; spot < COMMAND_OBJECTIVE_TERRITORIES; spot++)
	{
		/* (A: the left flank, B: the middle, C: the right flank) */
		real side = spot == 0 ? 1.f : spot == 2 ? -1.f : 0.f;
		boolean found = FALSE;
		short step;

		placed[spot] = FALSE;
		for (step = 0; step < 24 && !found && middle_valid; step++)
		{
			real_point3d point = middle_ground;
			real offset, shift;

			if (side == 0.f)
			{
				/* (the middle: about it, outward, along and across) */
				offset = (real)((step + 1) / 2) * 3.f * ((step & 1) ? 1.f : -1.f);
				shift = (real)(step / 8) * 3.f * ((step & 2) ? 1.f : -1.f);
			}
			else
			{
				/* (a flank: from the farthest in, a little along the line too) */
				offset = side * (COMMAND_OBJECTIVE_FLANK_FARTHEST - (real)(step % 9) *
					(COMMAND_OBJECTIVE_FLANK_FARTHEST - COMMAND_OBJECTIVE_FLANK_NEAREST) / 8.f);
				shift = (real)(step / 9) * 6.f * ((step / 9) & 1 ? 1.f : -1.f);
			}
			point.x += across_x * offset + along_x * shift;
			point.y += across_y * offset + along_y * shift;
			found = command_objective_spot_flat(&point, middle_ground.z, &spots[spot]);
			/* (1.1.4, the big team maps: flat ground scarce - Death Island's
			A found 5 wu from B, two rings on one spot - an objective is
			apart from the ones chosen before it by the supply points' gap) */
			{
				short other;

				for (other = 0; found && other < spot; other++)
				{
					if (command_distance2d(&spots[spot], &spots[other]) < COMMAND_OBJECTIVE_SUPPLY_APART)
						found = FALSE;
				}
			}
		}
		/* (1.1.4, the big team maps: no flat open ground where the spot was
		wanted - Gephyrophobia's bridge over its chasm, Danger Canyon's chasm
		-: the map's own hill nearest it, as its maker placed them, one not
		taken by a spot before; the old spot near the middle only when the
		map has no hill within reach) */
		if (!found)
		{
			real_point3d wanted = middle_ground;
			real best_distance = COMMAND_OBJECTIVE_HILL_REACH;
			short best = NONE;

			wanted.x += across_x * side * (COMMAND_OBJECTIVE_FLANK_FARTHEST + COMMAND_OBJECTIVE_FLANK_NEAREST) / 2.f;
			wanted.y += across_y * side * (COMMAND_OBJECTIVE_FLANK_FARTHEST + COMMAND_OBJECTIVE_FLANK_NEAREST) / 2.f;
			for (index = 0; index < command_territory_count; index++)
			{
				real distance = command_distance2d(&command_territories[index].center, &wanted);
				short other;

				/* (a hill taken by a spot before stands for no other; hills
				as near each other as the map's maker set them may both be
				objectives: Gephyrophobia's bridge) */
				for (other = 0; other < spot; other++)
				{
					if (placed[other] && command_distance2d(&command_territories[index].center, &spots[other]) < 0.5f)
						distance = COMMAND_OBJECTIVE_HILL_REACH;
				}
				if (distance < best_distance)
				{
					best_distance = distance;
					best = index;
				}
			}
			if (best != NONE)
			{
				spots[spot] = command_territories[best].center;
				found = TRUE;
				platform_log("command: objective territory %c: no flat open ground found: the map's hill %d (%.1f wu from the spot wanted)",
					'A' + spot, command_territories[best].hill, best_distance);
			}
		}
		if (!found)
		{
			platform_log("command: objective territory %c: no flat open ground found%s", 'A' + spot,
				middle_valid ? "" : ", no ground between the bases, no hill in reach");
			if (!middle_valid)
				continue;
			spots[spot] = middle_ground;
			spots[spot].x += across_x * side * COMMAND_OBJECTIVE_FLANK_NEAREST;
			spots[spot].y += across_y * side * COMMAND_OBJECTIVE_FLANK_NEAREST;
			if (!command_ground(&spots[spot], &spots[spot], NULL))
			{
				platform_log("command: objective territory %c: no ground there either: left out", 'A' + spot);
				continue;
			}
		}
		placed[spot] = TRUE;
	}
	/* (a supply point on one let go) */
	{
		short kept = 0;

		for (index = 0; index < command_territory_count; index++)
		{
			boolean near = FALSE;

			for (spot = 0; spot < COMMAND_OBJECTIVE_TERRITORIES; spot++)
			{
				if (placed[spot] && command_distance2d(&command_territories[index].center, &spots[spot]) < COMMAND_OBJECTIVE_SUPPLY_APART)
					near = TRUE;
			}
			if (near)
			{
				platform_log("command: supply point %d is on an objective: let go", index + 1);
				continue;
			}
			command_territories[kept++] = command_territories[index];
		}
		command_territory_count = kept;
	}
	for (spot = 0; spot < COMMAND_OBJECTIVE_TERRITORIES && command_territory_count < COMMAND_TERRITORIES; spot++)
	{
		struct command_territory *territory;

		if (!placed[spot])
			continue;
		territory = &command_territories[command_territory_count++];
		csmemset(territory, 0, sizeof(*territory));
		territory->center = spots[spot];
		territory->radius = COMMAND_OBJECTIVE_RADIUS;
		territory->hill = NONE;
		territory->owner = NONE;
		territory->taker = NONE;
		territory->objective = TRUE;
		territory->letter = (char)('A' + spot);
		platform_log("command: objective territory %c (territory %d) at %.2f %.2f %.2f, %.1f wu across", territory->letter,
			command_territory_count, territory->center.x, territory->center.y, territory->center.z, territory->radius);
	}
}

/* a base's own flag stand (the map's CTF flag of its team) */
static boolean command_flag_stand(
	short side,
	real_point3d *point)
{
	struct scenario *scenario = global_scenario_try_and_get();
	long index;
	short team = side == _command_side_red ? 0 : 1;

	/* (editor6: the layout's own CTF flag of the side, else its home base -
	the flag at the base the editor shows -, the map's stands only for a
	layout with neither) */
	if (forge_mode_layout_flag(team, point))
		return TRUE;
	if (forge_mode_home_base(team, point, NULL))
		return TRUE;
	for (index = 0; scenario && index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index,
			struct scenario_netgame_flag);

		if (flag->type == _netgame_flag_ctf_flag && flag->team_index == team)
		{
			*point = flag->position;
			return TRUE;
		}
	}
	return FALSE;
}

/* where a flag is: on the ground, or its carrier's (a weapon in a unit's
hands has no place of its own) */
static void command_flag_position(
	struct command_flag const *flag,
	real_point3d *at)
{
	struct object_datum *object = object_try_and_get(flag->weapon_index);

	if (!object)
	{
		*at = flag->stand;
		return;
	}
	if (object->object.parent_object_index != NONE && object_try_and_get(object->object.parent_object_index))
		object_get_origin(object->object.parent_object_index, at);
	else
		object_get_origin(flag->weapon_index, at);
}

/* the flag out of a unit's hands, onto the ground where it stands */
static void command_flag_drop_from(
	long unit_index,
	long weapon_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	short slot;

	if (!unit)
		return;
	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		if (unit->unit.weapon_object_indices[slot] == weapon_index)
			break;
	}
	if (slot == MAXIMUM_WEAPONS_PER_UNIT)
		return;
	/* (the flag the one in hand, then dropped as the game drops it:
	ctf_player_drop_flag) */
	if (unit->unit.current_weapon_index != slot)
	{
		object_set_visibility(weapon_index, TRUE);
		unit->unit.current_weapon_index = slot;
	}
	if (!unit_drop_current_weapon(unit_index, TRUE))
		platform_log("command: flag: %lx would not drop the flag", (unsigned long)unit_index);
}

/* a side's flag home on its stand (made again if it is gone) */
static void command_flag_reset(
	short side)
{
	struct command_flag *flag = &command_objectives.flags[side];
	struct object_datum *object = object_try_and_get(flag->weapon_index);

	if (object && object->object.type != _object_type_weapon)
		object = NULL;
	if (object && object->object.parent_object_index != NONE)
		command_flag_drop_from(object->object.parent_object_index, flag->weapon_index);
	object = object_try_and_get(flag->weapon_index);
	if (object && object->object.type == _object_type_weapon)
		game_engine_flag_reset(flag->weapon_index, &flag->stand);
	else
	{
		struct object_placement_data placement;
		long tag_index = get_flag_definition_index();

		flag->weapon_index = NONE;
		if (tag_index == NONE)
		{
			platform_log("command: flag: the map has no multiplayer flag");
			flag->valid = FALSE;
			return;
		}
		object_placement_data_new(&placement, tag_index, NONE);
		placement.position = flag->stand;
		flag->weapon_index = object_new(&placement);
		if (flag->weapon_index == NONE)
		{
			platform_log("command: flag: %s's flag could not be made", command_side_names[side]);
			return;
		}
		object_set_automatic_deactivation(flag->weapon_index, FALSE);
		object_get(flag->weapon_index)->object.owner_team_index = command_side_team(side);
	}
	flag->away = FALSE;
	flag->dropped_time = NONE;
	flag->carrier_index = NONE;
}

static void command_flag_announce(
	long sound_index)
{
	game_engine_play_multiplayer_sound(sound_index);
}

/* a living Spartan within touch of a point (NONE: none); its side */
static long command_spartan_near(
	real_point3d const *point,
	short *side)
{
	struct object_iterator iterator;
	struct unit_datum *unit;

	object_iterator_new(&iterator, _object_mask_biped, 0);
	while ((unit = object_iterator_next(&iterator)) != NULL)
	{
		real_point3d origin;

		if (TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			continue;
		object_get_origin(iterator.index, &origin);
		if (command_distance2d(&origin, point) > COMMAND_FLAG_TOUCH || fabs(origin.z - point->z) > COMMAND_FLAG_TOUCH_HEIGHT)
			continue;
		if (!command_unit_is_spartan(iterator.index))
			continue;
		*side = command_unit_side(iterator.index);
		if (*side != NONE)
			return iterator.index;
	}
	return NONE;
}

/* (each frame) the flags: taken, carried, scored, dropped, home again */
static void command_flags_update(
	void)
{
	long now = game_time_get();
	short side;

	for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
	{
		struct command_flag *flag = &command_objectives.flags[side];
		short other = (short)!side;
		struct object_datum *object;
		long holder;
		real_point3d at;

		if (!flag->valid)
			continue;
		object = object_try_and_get(flag->weapon_index);
		if (!object || object->object.type != _object_type_weapon)
		{
			platform_log("command: flag: %s's flag was gone: made again at its stand", command_side_names[side]);
			command_flag_reset(side);
			continue;
		}
		holder = object->object.parent_object_index;
		if (holder != NONE)
		{
			short holder_side = command_unit_side(holder);

			/* (none but the other side's Spartans carry it: a squad's member
			that picked it up drops it, its own side's Spartan returns it) */
			if (!command_unit_is_spartan(holder) || holder_side != other)
			{
				platform_log("command: flag: %s's flag in the hands of %lx (%s): %s", command_side_names[side], (unsigned long)holder,
					holder_side == NONE ? "no side's" : command_side_names[holder_side],
					holder_side == side ? "home" : "dropped");
				command_flag_drop_from(holder, flag->weapon_index);
				if (holder_side == side)
					command_flag_reset(side);
				continue;
			}
			if (flag->carrier_index != holder)
			{
				flag->carrier_index = holder;
				flag->dropped_time = NONE;
				platform_log("command: flag: %s's flag %s by %s's Spartan %lx at tick %ld", command_side_names[side],
					flag->away ? "picked up" : "taken", command_side_names[other], (unsigned long)holder, now);
				if (!flag->away)
					command_flag_announce(other == _command_side_red ? _command_sound_red_team_has_the_flag :
						_command_sound_blue_team_has_the_flag);
				command_event(COMMAND_EVENT_EVERYONE, side, _command_event_flag_taken, 0, 0);
				flag->away = TRUE;
			}
			/* (scored at the carrier's own stand, his own flag at home) */
			object_get_origin(holder, &at);
			if (command_distance2d(&at, &command_objectives.flags[other].stand) <= COMMAND_FLAG_TOUCH &&
				fabs(at.z - command_objectives.flags[other].stand.z) <= COMMAND_FLAG_TOUCH_HEIGHT)
			{
				if (command_objectives.flags[other].valid && command_objectives.flags[other].away)
				{
					if (!flag->blocked_said)
						platform_log("command: flag: %s's Spartan at its stand, its own flag away: no score", command_side_names[other]);
					flag->blocked_said = TRUE;
					continue;
				}
				command_objectives.captures[other]++;
				command_objectives.scores[other] += COMMAND_CAPTURE_POINTS;
				if (holder == command_side_spartan(other) && command_spartans[other].possessor != NONE)
					command_objectives.spartan_captures++;
				platform_log("command: flag: %s scores %s's flag at tick %ld (%ld captures, score %ld)", command_side_names[other],
					command_side_names[side], now, command_objectives.captures[other], command_objectives.scores[other]);
				command_flag_announce(other == _command_side_red ? _command_sound_red_team_score : _command_sound_blue_team_score);
				command_event(COMMAND_EVENT_EVERYONE, side, _command_event_flag_scored, 0, 0);
				command_flag_reset(side);
			}
			else
				flag->blocked_said = FALSE;
			continue;
		}
		/* (on the ground: dropped, or home) */
		if (flag->carrier_index != NONE)
		{
			platform_log("command: flag: %s's flag dropped at tick %ld", command_side_names[side], now);
			flag->carrier_index = NONE;
			flag->dropped_time = now;
			command_event(COMMAND_EVENT_EVERYONE, side, _command_event_flag_dropped, 0, 0);
		}
		if (flag->away && flag->dropped_time == NONE)
			flag->dropped_time = now;
		if (flag->away && now - flag->dropped_time >= COMMAND_FLAG_RETURN_SECONDS * TICKS_PER_SECOND)
		{
			platform_log("command: flag: %s's flag went home %d s after it was dropped", command_side_names[side],
				COMMAND_FLAG_RETURN_SECONDS);
			command_flag_reset(side);
			command_flag_announce(side == _command_side_red ? _command_sound_red_team_flag_returned :
				_command_sound_blue_team_flag_returned);
			command_event(COMMAND_EVENT_EVERYONE, side, _command_event_flag_returned, 0, 0);
			continue;
		}
		object_get_origin(flag->weapon_index, &at);
		{
			short toucher_side = NONE;
			long toucher = command_spartan_near(&at, &toucher_side);

			if (toucher == NONE)
				continue;
			if (toucher_side == side)
			{
				if (flag->away)
				{
					platform_log("command: flag: %s's flag returned by its Spartan %lx", command_side_names[side], (unsigned long)toucher);
					command_flag_reset(side);
					command_flag_announce(side == _command_side_red ? _command_sound_red_team_flag_returned :
						_command_sound_blue_team_flag_returned);
					command_event(COMMAND_EVENT_EVERYONE, side, _command_event_flag_returned, 0, 0);
				}
				continue;
			}
			/* (taken: in his hands, the one he holds; seen as carried next
			frame) */
			if (unit_add_weapon_to_inventory(toucher, flag->weapon_index, _unit_add_weapon_normal))
			{
				struct unit_datum *unit = unit_get(toucher);
				short slot;

				for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
				{
					if (unit->unit.weapon_object_indices[slot] == flag->weapon_index)
						unit->unit.desired_weapon_index = slot;
				}
			}
			else if (!flag->take_failed_said)
			{
				flag->take_failed_said = TRUE;
				platform_log("command: flag: %lx could not take %s's flag", (unsigned long)toucher, command_side_names[side]);
			}
		}
	}
}

static boolean command_flag_goal(
	struct command_squad const *squad,
	real_point3d *goal)
{
	long unit_index = squad->units[0];
	short side = squad->side;
	struct command_flag const *enemy = &command_objectives.flags[!side];
	struct object_datum *object;

	if (!(command_objectives.objectives & _command_objectives_flag) || !enemy->valid || !command_objectives.flags[side].valid)
		return FALSE;
	object = object_try_and_get(enemy->weapon_index);
	if (!object)
		return FALSE;
	/* (carrying it: home; else to it, wherever it is - with another of his
	side, after it) */
	if (unit_index != NONE && object->object.parent_object_index == unit_index)
		*goal = command_objectives.flags[side].stand;
	else
		command_flag_position(enemy, goal);
	/* (a flag stand is inside its base, which the AI's path searches reach
	only from near: from afar, by way of that base's rally point, outside
	it) */
	if (unit_index != NONE && command_living_unit(unit_index))
	{
		real_point3d origin;
		short base;

		object_get_origin(unit_index, &origin);
		/* (in a base, for somewhere far: out the way in first, by its rally
		point) */
		for (base = 0; base < NUMBER_OF_COMMAND_SIDES; base++)
		{
			real_point3d rally;

			if (command_distance2d(&origin, &command_objectives.flags[base].stand) <= COMMAND_FLAG_STAND_NEAR &&
				command_distance2d(&origin, goal) > COMMAND_FLAG_STAND_APPROACH && command_rally_point(base, &rally) &&
				command_distance2d(&origin, &rally) > COMMAND_ARRIVED)
			{
				*goal = rally;
				return TRUE;
			}
		}
		for (base = 0; base < NUMBER_OF_COMMAND_SIDES; base++)
		{
			real_point3d rally;

			if (command_distance2d(goal, &command_objectives.flags[base].stand) <= COMMAND_FLAG_STAND_NEAR &&
				command_distance2d(&origin, goal) > COMMAND_FLAG_STAND_APPROACH && command_rally_point(base, &rally) &&
				command_distance2d(&origin, &rally) > COMMAND_ARRIVED)
			{
				*goal = rally;
				break;
			}
		}
	}
	return TRUE;
}

static boolean command_flag_at(
	short side,
	real_point3d const *point)
{
	struct command_flag const *flag = &command_objectives.flags[side];
	real_point3d at;

	if (!(command_objectives.objectives & _command_objectives_flag) || !flag->valid || !object_try_and_get(flag->weapon_index))
		return FALSE;
	command_flag_position(flag, &at);
	return command_distance2d(&at, point) <= 4.f && fabs(at.z - point->z) <= 4.f;
}

/* the game over: its winner (COMMAND_DRAW: none), the game's own end */
static void command_game_end(
	short winner,
	char const *why)
{
	if (command_objectives.over)
		return;
	command_objectives.over = TRUE;
	command_objectives.winner = winner;
	command_globals.hq_open = FALSE;
	platform_log("command: game over (%s) at tick %ld: %s, red %ld blue %ld (captures %ld-%ld, territory %ld-%ld s, "
		"kills %ld-%ld, losses %ld-%ld, the Spartan's kills %ld captures %ld)", why, (long)game_time_get(),
		winner == COMMAND_DRAW ? "a draw" : winner == _command_side_red ? "red wins" : "blue wins",
		command_objectives.scores[0], command_objectives.scores[1], command_objectives.captures[0], command_objectives.captures[1],
		command_objectives.territory_seconds[0], command_objectives.territory_seconds[1], command_objectives.kills[0],
		command_objectives.kills[1], command_objectives.losses[0], command_objectives.losses[1], command_objectives.spartan_kills,
		command_objectives.spartan_captures);
	command_event(COMMAND_EVENT_EVERYONE, NONE, _command_event_winner, winner, 0);
	game_engine_end_game();
}

/* (the game begun) the scores, the objective territories, the flags */
static void command_objectives_begin(
	void)
{
	short side;

	csmemset(command_objectives.scores, 0, sizeof(command_objectives.scores));
	csmemset(command_objectives.captures, 0, sizeof(command_objectives.captures));
	csmemset(command_objectives.territory_seconds, 0, sizeof(command_objectives.territory_seconds));
	csmemset(command_objectives.kills, 0, sizeof(command_objectives.kills));
	csmemset(command_objectives.losses, 0, sizeof(command_objectives.losses));
	csmemset(command_objectives.hurts, 0, sizeof(command_objectives.hurts));
	{
		short index;

		for (index = 0; index < COMMAND_HURTS; index++)
			command_objectives.hurts[index].unit_index = NONE;
	}
	command_objectives.next_hurt = 0;
	command_objectives.spartan_kills = 0;
	command_objectives.spartan_captures = 0;
	command_objectives.spartan_counted_index = NONE;
	command_objectives.over = FALSE;
	command_objectives.winner = NONE;
	command_objectives.start_time = game_time_get();
	command_objectives.next_second_time = game_time_get() + TICKS_PER_SECOND;
	command_objective_territories_choose();
	for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
	{
		struct command_flag *flag = &command_objectives.flags[side];

		csmemset(flag, 0, sizeof(*flag));
		flag->weapon_index = NONE;
		flag->carrier_index = NONE;
		flag->dropped_time = NONE;
		if (!(command_objectives.objectives & _command_objectives_flag))
			continue;
		if (!command_flag_stand(side, &flag->stand))
		{
			platform_log("command: flag: the map has no flag stand for %s", command_side_names[side]);
			continue;
		}
		flag->valid = TRUE;
		command_flag_reset(side);
		platform_log("command: flag: %s's flag (%lx) at its stand %.2f %.2f %.2f", command_side_names[side],
			(unsigned long)flag->weapon_index, flag->stand.x, flag->stand.y, flag->stand.z);
	}
	platform_log("command: objectives: %s, to %ld, %ld minutes", command_objectives_log_names[command_objectives.objectives & 3],
		command_objectives.score_limit, command_objectives.time_limit_minutes);
}

/* (each frame) the score: each second's territories, the flags; the end */
static void command_objectives_update(
	void)
{
	long now = game_time_get();
	short index;

	if (command_objectives.over)
		return;
	command_losses_update();
	if (command_objectives.objectives & _command_objectives_flag)
		command_flags_update();
	while (now >= command_objectives.next_second_time)
	{
		command_objectives.next_second_time += TICKS_PER_SECOND;
		if (!(command_objectives.objectives & _command_objectives_territories))
			continue;
		for (index = 0; index < command_territory_count; index++)
		{
			short owner = command_territories[index].owner;

			if (command_territories[index].objective && owner != NONE)
			{
				command_objectives.scores[owner]++;
				command_objectives.territory_seconds[owner]++;
			}
		}
	}
	{
		boolean limit = command_objectives.score_limit > 0 && (command_objectives.scores[0] >= command_objectives.score_limit ||
			command_objectives.scores[1] >= command_objectives.score_limit);
		boolean time = command_objectives.time_limit_minutes > 0 &&
			now - command_objectives.start_time >= command_objectives.time_limit_minutes * 60 * TICKS_PER_SECOND;

		if (limit || time)
		{
			short winner = command_objectives.scores[0] > command_objectives.scores[1] ? _command_side_red :
				command_objectives.scores[1] > command_objectives.scores[0] ? _command_side_blue : COMMAND_DRAW;

			command_game_end(winner, limit ? "the score limit" : "the time limit");
		}
	}
}

/* the time left (ticks; NONE: no limit) */
static long command_time_left(
	void)
{
	long left;

	if (command_objectives.time_limit_minutes <= 0)
		return NONE;
	left = command_objectives.time_limit_minutes * 60 * TICKS_PER_SECOND - (game_time_get() - command_objectives.start_time);
	return left > 0 ? left : 0;
}

/* ---- the AI commander (the owner's step 6; spec section 10, decision 3):
a side no human commands is played by it, through the same buys and orders
as a commander's. Every few seconds it:
- spends: the barracks first, then a squad while it has fewer than two in
  the field, the supply depot, a third squad, then the vehicle bay, the
  defences and the armory, then squads whenever it can (keeping its
  Spartan's respawn when he is down); its squads by what it can afford,
  each kind in turn (an Elite's when it can, the Hunter pair when rich);
- sends each squad with nothing to do to a territory its side does not
  hold - the objective territories first, then the supply points; at most
  two squads to one, the nearest -, leaves one to hold each it has taken,
  and sends the rest on at the other side's base;
- has its Spartan (made at its base as the commander's is, AI from the
  start, in its side's colour, with the hero loadout; respawned at its base
  for the same price) run the enemy flag when the flag is in play, and
  otherwise go with its squads.
The commander's red is a human's in a local game: the AI commander plays
blue (network play gives red's seat too when no one takes it). */



/* a side's squads in the field (or flying in), but its crews and Spartan */
static short command_ai_squad_count(
	short side)
{
	short squad_index, count = 0;

	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad const *squad = &command_globals.squads[squad_index];

		if (squad->used && squad->side == side && !squad->hero && !squad->crew)
			count++;
	}
	return count;
}

static short command_buy_row(
	char const *key)
{
	short row;

	for (row = 0; row < COMMAND_BUYS; row++)
	{
		if (!strcmp(command_buys[row].key, key))
			return row;
	}
	return NONE;
}

/* the AI commander tries a row: TRUE if it bought it */
static boolean command_ai_try(
	short side,
	char const *key)
{
	short row = command_buy_row(key);

	if (row == NONE || command_buy_blocked(side, row))
		return FALSE;
	return command_buy(side, row);
}

/* a squad of what it can afford, each kind in turn (reserved: its level's
reserve kept back) */
static boolean command_ai_buy_squad(
	short side,
	boolean reserved)
{
	static char const *const kinds[] = { "elites", "marines", "jackals", "grunts" };
	short attempt;

	if (reserved && command_globals.resources[side] < command_ai_levels[command_ai_level].reserve + 40)
		return FALSE;
	if (command_globals.resources[side] >= COMMAND_AI_HUNTERS_RICH && command_ai_try(side, "hunters"))
		return TRUE;
	for (attempt = 0; attempt < (short)NUMBEROF(kinds); attempt++)
	{
		short kind = (short)((command_ai[side].next_squad_kind + attempt) % NUMBEROF(kinds));

		if (command_ai_try(side, kinds[kind]))
		{
			command_ai[side].next_squad_kind = (short)(kind + 1);
			return TRUE;
		}
	}
	return FALSE;
}

static boolean command_ai_upgrade_wanted(
	short side,
	char const *key)
{
	short row = command_buy_row(key);

	return row != NONE && !command_upgrades[side][command_buys[row].upgrade] &&
		!command_upgrade_done_time[side][command_buys[row].upgrade];
}

static void command_ai_spend(
	short side)
{
	short squads = command_ai_squad_count(side);
	short index;

	/* (its Spartan's respawn first, when he is down) */
	if (command_spartan_owned[side] && command_spartans[side].unit_index == NONE &&
		command_globals.resources[side] < COMMAND_SPARTAN_COST + 40)
	{
		return;
	}
	/* (its first squads first: no free ones, as the commander's) */
	if (squads < COMMAND_AI_SQUADS_FIRST)
	{
		command_ai_buy_squad(side, FALSE);
		return;
	}
	/* (then its upgrades in its level's order, a squad more between the
	first two) */
	for (index = 0; index < (short)NUMBEROF(command_ai_levels[0].upgrades) && command_ai_levels[command_ai_level].upgrades[index]; index++)
	{
		char const *key = command_ai_levels[command_ai_level].upgrades[index];

		if (index == 2 && squads < COMMAND_AI_SQUADS_SECOND)
		{
			command_ai_buy_squad(side, FALSE);
			return;
		}
		if (command_ai_upgrade_wanted(side, key))
		{
			command_ai_try(side, key);
			return;
		}
	}
	/* (the late game's Spartan) */
	if (command_ai_levels[command_ai_level].buys_spartan && !command_spartan_owned[side] &&
		command_ai_try(side, "spartan"))
	{
		return;
	}
	if (squads < command_ai_levels[command_ai_level].field_squads)
		command_ai_buy_squad(side, TRUE);
}

/* the squads sent to a territory (on their way or holding it) */
static short command_ai_goal_count(
	short side,
	short territory)
{
	short squad_index, count = 0;

	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad const *squad = &command_globals.squads[squad_index];

		if (squad->used && squad->side == side && squad->ai_goal == territory)
			count++;
	}
	return count;
}

/* the territory to send a squad to: one its side does not hold, the
objectives first, with room for it, the nearest; NONE if none */
static short command_ai_goal_choose(
	short side,
	real_point3d const *from)
{
	short pass, index;

	for (pass = 0; pass < 2; pass++)
	{
		short best = NONE;
		real best_distance = 0.f;

		for (index = 0; index < command_territory_count; index++)
		{
			struct command_territory const *territory = &command_territories[index];
			boolean objective = territory->objective && (command_objectives.objectives & _command_objectives_territories);
			real distance;

			if (pass == 0 ? !objective : objective)
				continue;
			if (territory->owner == side || command_ai_goal_count(side, index) >= command_ai_levels[command_ai_level].per_goal)
				continue;
			distance = command_distance2d(from, &territory->center);
			if (best == NONE || distance < best_distance)
			{
				best = index;
				best_distance = distance;
			}
		}
		if (best != NONE)
			return best;
	}
	return NONE;
}

/* (defending) a squad's goal its own base's (not a territory) */
#define COMMAND_AI_GOAL_BASE 100

/* the enemy's living units within radius of a point: how many, the nearest */
static short command_ai_threat(
	short side,
	real_point3d const *point,
	real radius,
	real_point3d *nearest)
{
	struct object_iterator iterator;
	struct unit_datum *unit;
	short count = 0;
	real best = radius;

	object_iterator_new(&iterator, _object_mask_biped, 0);
	while ((unit = object_iterator_next(&iterator)) != NULL)
	{
		real_point3d origin;
		real distance;

		if (TEST_FLAG(unit->object.damage_flags, _object_dead_bit) || command_unit_side(iterator.index) != !side)
			continue;
		object_get_origin(iterator.index, &origin);
		distance = command_distance2d(&origin, point);
		if (distance > radius)
			continue;
		count++;
		if (distance <= best)
		{
			best = distance;
			*nearest = origin;
		}
	}
	return count;
}

/* (its level's defends) a territory it holds being taken, or its base under
attack: the nearest squad free for it sent there (one a threat) */
static void command_ai_defend(
	short side)
{
	short index;
	real_point3d rally, threat;

	for (index = -1; index < command_territory_count; index++)
	{
		real_point3d goal;
		short goal_index;
		short squad_index;
		struct command_squad *best = NULL;
		real best_distance = 0.f;

		if (index < 0)
		{
			if (!command_rally_point(side, &rally) || !command_ai_threat(side, &rally, COMMAND_AI_BASE_THREAT, &threat))
				continue;
			goal = threat;
			goal_index = COMMAND_AI_GOAL_BASE;
		}
		else
		{
			struct command_territory const *territory = &command_territories[index];

			if (territory->owner != side || territory->counts[!side] == 0)
				continue;
			goal = territory->center;
			goal_index = index;
		}
		if (command_ai_goal_count(side, goal_index) > 0)
			continue;
		for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
		{
			struct command_squad *squad = &command_globals.squads[squad_index];
			real_point3d center;
			real distance;

			if (!squad->used || squad->side != side || squad->hero || squad->crew || squad->airborne ||
				!command_squad_center(squad, &center))
			{
				continue;
			}
			/* (one holding a territory alone keeps it; one already
			defending stays) */
			if (squad->ai_goal == COMMAND_AI_GOAL_BASE ||
				(squad->ai_goal != NONE && squad->ai_goal < command_territory_count &&
				command_territories[squad->ai_goal].owner == side && command_ai_goal_count(side, squad->ai_goal) <= 1))
			{
				continue;
			}
			distance = command_distance2d(&center, &goal);
			if (!best || distance < best_distance)
			{
				best = squad;
				best_distance = distance;
			}
		}
		if (best)
		{
			best->ai_goal = goal_index;
			platform_log("command: AI commander (%s): squad %d defends %s", command_side_names[side], best->number,
				goal_index == COMMAND_AI_GOAL_BASE ? "its base" : "a territory");
			command_squad_order(best, _command_order_advance, &goal, NONE, NONE);
		}
	}
}

static void command_ai_orders(
	short side)
{
	short squad_index;
	real_point3d enemy_rally, own_rally;
	boolean enemy_rally_valid = command_rally_point((short)!side, &enemy_rally);
	boolean own_rally_valid = command_rally_point(side, &own_rally);
	short free_squads = 0;

	if (command_ai_levels[command_ai_level].defends)
		command_ai_defend(side);
	/* (the squads with nothing to take: they attack the enemy's base once
	there are enough of them, else wait at their own) */
	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad const *squad = &command_globals.squads[squad_index];

		if (squad->used && squad->side == side && !squad->hero && !squad->crew && !squad->airborne && squad->ai_goal == NONE)
			free_squads++;
	}

	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad *squad = &command_globals.squads[squad_index];
		real_point3d center;
		short goal;

		if (!squad->used || squad->side != side || squad->hero || squad->crew || squad->airborne ||
			!command_squad_center(squad, &center))
		{
			continue;
		}
		/* (one defending its base: there while the enemy is) */
		if (squad->ai_goal == COMMAND_AI_GOAL_BASE)
		{
			real_point3d threat;

			if (own_rally_valid && command_ai_threat(side, &own_rally, COMMAND_AI_BASE_THREAT, &threat))
			{
				if (squad->order == _command_order_hold || squad->order == _command_order_none)
					command_squad_order(squad, _command_order_advance, &threat, NONE, NONE);
				continue;
			}
			squad->ai_goal = NONE;
		}
		/* (one sent to a territory: on while it is not held; there, it holds
		it - the first there; another goes on) */
		if (squad->ai_goal != NONE && squad->ai_goal < command_territory_count)
		{
			struct command_territory const *territory = &command_territories[squad->ai_goal];

			if (territory->owner != side)
			{
				if (squad->order == _command_order_hold || squad->order == _command_order_none)
					command_squad_order(squad, _command_order_advance, &territory->center, NONE, NONE);
				continue;
			}
			if (command_ai_goal_count(side, squad->ai_goal) <= 1)
				continue;
			squad->ai_goal = NONE;
		}
		/* (one still on its way somewhere: left to it) */
		if (squad->order == _command_order_move || squad->order == _command_order_advance || squad->order == _command_order_attack)
			continue;
		goal = command_ai_goal_choose(side, &center);
		if (goal != NONE)
		{
			squad->ai_goal = goal;
			platform_log("command: AI commander (%s): squad %d to %s %d", command_side_names[side], squad->number,
				command_territories[goal].objective ? "objective territory" : "supply point", goal + 1);
			command_squad_order(squad, _command_order_advance, &command_territories[goal].center, NONE, NONE);
		}
		else if (enemy_rally_valid && free_squads >= command_ai_levels[command_ai_level].attack_squads)
		{
			platform_log("command: AI commander (%s): squad %d on at the enemy base", command_side_names[side], squad->number);
			command_squad_order(squad, _command_order_advance, &enemy_rally, NONE, NONE);
		}
		else if (own_rally_valid && squad->order != _command_order_hold &&
			command_distance2d(&center, &own_rally) > COMMAND_HOLD_LEASH * 2.f)
		{
			platform_log("command: AI commander (%s): squad %d waits at its base", command_side_names[side], squad->number);
			command_squad_order(squad, _command_order_hold, &own_rally, NONE, NONE);
		}
	}
	/* (its own flag taken: the squad nearest the carrier after him - his
	death drops it -, as long as he has it) */
	{
		struct command_flag const *own = &command_objectives.flags[side];

		if (command_ai_levels[command_ai_level].hunts_carrier && (command_objectives.objectives & _command_objectives_flag) &&
			own->valid && command_living_unit(own->carrier_index))
		{
			struct command_squad *nearest = NULL;
			real nearest_distance = 0.f;
			real_point3d carrier;
			boolean hunted = FALSE;

			object_get_origin(own->carrier_index, &carrier);
			for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
			{
				struct command_squad *squad = &command_globals.squads[squad_index];
				real_point3d center;
				real distance;

				if (!squad->used || squad->side != side || squad->hero || squad->crew || squad->airborne ||
					!command_squad_center(squad, &center))
				{
					continue;
				}
				if (squad->order == _command_order_attack && squad->target_index == own->carrier_index)
					hunted = TRUE;
				distance = command_distance2d(&center, &carrier);
				if (!nearest || distance < nearest_distance)
				{
					nearest = squad;
					nearest_distance = distance;
				}
			}
			if (!hunted && nearest)
			{
				platform_log("command: AI commander (%s): squad %d after the flag's carrier", command_side_names[side], nearest->number);
				nearest->ai_goal = NONE;
				command_squad_order(nearest, _command_order_attack, &carrier, NONE, own->carrier_index);
			}
		}
	}
	/* (its Spartan: the enemy flag run while the flag is in play; else with
	its squads, at the nearest territory it does not hold) */
	{
		struct command_squad *hero = command_side_hero_squad(side, FALSE);
		real_point3d goal;
		real_point3d center;

		if (hero && command_living_unit(hero->units[0]) && command_squad_center(hero, &center))
		{
			if (command_flag_goal(hero, &goal))
			{
				if (hero->order != _command_order_flag)
					command_squad_order(hero, _command_order_flag, &goal, NONE, NONE);
			}
			else if (hero->order == _command_order_none || hero->order == _command_order_hold)
			{
				short territory = command_ai_goal_choose(side, &center);

				if (territory != NONE)
					command_squad_order(hero, _command_order_advance, &command_territories[territory].center, NONE, NONE);
				else if (enemy_rally_valid)
					command_squad_order(hero, _command_order_advance, &enemy_rally, NONE, NONE);
			}
		}
	}
}

/* (each frame) an AI commander's Spartan: made at its base when it may come
(the first free, each later paid), AI from the start; its death counted */
/* (the host, each frame of a Command game) a side's Spartan: its death
counted, its commander back above the map; made again at its base after
COMMAND_SPARTAN_RESPAWN_SECONDS once its side can pay (the first one free),
the AI's until a commander takes it */
static void command_side_spartan_update(
	short side)
{
	struct command_squad *hero;
	long unit_index = command_spartans[side].unit_index;
	struct object_datum *object = object_try_and_get(unit_index);
	boolean human = !command_ai[side].plays;

	if (unit_index != NONE && (!object || TEST_FLAG(object->object.damage_flags, _object_dead_bit)))
	{
		if (!command_spartans[side].counted)
			command_death_count(unit_index, side);
		platform_log("command: %s's Spartan is down", command_side_names[side]);
		command_spartans[side].unit_index = NONE;
		command_spartans[side].respawn_time = game_time_get() + COMMAND_SPARTAN_RESPAWN_SECONDS * TICKS_PER_SECOND;
		hero = command_side_hero_squad(side, FALSE);
		if (hero)
		{
			hero->units[0] = NONE;
			hero->order = _command_order_none;
		}
		if (human)
			command_event(side, side, _command_event_spartan_down, 0, 0);
		return;
	}
	if (unit_index == NONE)
		command_spartans[side].possessor = NONE;
	if (unit_index != NONE || game_time_get() < command_spartans[side].respawn_time)
		return;
	/* (none until its HQ bought one: C9) */
	if (!command_spartan_owned[side])
		return;
	if (command_spartans[side].paid && command_globals.resources[side] < COMMAND_SPARTAN_COST)
		return;
	{
		real_point3d rally;

		if (command_actor_variant_find(COMMAND_SPARTAN_ACTOR) == NONE || !command_rally_point(side, &rally))
			return;
		unit_index = command_spartan_new(side);
		if (unit_index == NONE)
			return;
		if (command_spartans[side].paid)
		{
			command_globals.resources[side] -= COMMAND_SPARTAN_COST;
			platform_log("command: %s's Spartan's respawn paid: %d (%ld left)", command_side_names[side],
				COMMAND_SPARTAN_COST, command_globals.resources[side]);
		}
		command_spartans[side].paid = TRUE;
		command_spartans[side].unit_index = unit_index;
		command_spartans[side].counted = FALSE;
		command_spartans[side].possessor = NONE;
		hero = command_side_hero_squad(side, TRUE);
		if (hero)
			hero->order = _command_order_none;
		command_spartan_ai_attach(side);
		platform_log("command: %s's Spartan (%lx) at its base at %.2f %.2f %.2f", command_side_names[side],
			(unsigned long)unit_index, rally.x, rally.y, rally.z);
		if (human)
			command_event(side, side, _command_event_spartan_here, 0, 0);
	}
}

static void command_ai_begin(
	void)
{
	short side;

	for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
	{
		csmemset(&command_ai[side], 0, sizeof(command_ai[side]));
		command_ai[side].next_think_time = game_time_get() + TICKS_PER_SECOND +
			command_ai_levels[command_ai_level].first_buy_seconds * TICKS_PER_SECOND;
	}
	platform_log("command: AI commander: %s", command_ai_levels[command_ai_level].log_name);
	/* (red's the human's in a local game; a network game's sides by their
	humans, command_humans_update) */
	command_ai[_command_side_red].plays = command_network();
	command_ai[_command_side_blue].plays = TRUE;
#ifndef HALO_RELEASE
	/* (tests: HALO_TEST_COMMAND_NO_AI=1, no AI commander on either side) */
	command_ai_test_off[0] = command_ai_test_off[1] = getenv("HALO_TEST_COMMAND_NO_AI") != NULL;
#endif
}

static void command_ai_update(
	void)
{
	short side;

	for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
	{
		command_side_spartan_update(side);
		if (!command_ai[side].plays)
			continue;
#ifndef HALO_RELEASE
		if (command_ai_test_off[side])
			continue;
#endif
		if (game_time_get() < command_ai[side].next_think_time)
			continue;
		command_ai[side].next_think_time = game_time_get() + command_ai_levels[command_ai_level].think_seconds * TICKS_PER_SECOND;
		command_ai_spend(side);
		command_ai_orders(side);
	}
}

static void command_routes_build_step(void);
static void command_routes_reset(void);

static void command_game_update(
	void)
{
	if (!command_globals.game_running)
		return;
	command_routes_build_step();
	command_objectives_update();
	if (command_objectives.over)
		return;
	command_territories_update();
	command_economy_update();
	command_flights_update();
	command_squads_free_dead();
	command_ai_update();
	/* (each base's upkeep: its Shades manned, its upgrades' objects made
	again where gone. No free squads: the owner's, Nov 7 - every squad is
	bought, both sides', and comes by air) */
	{
		short side;
		boolean shades = FALSE;

		for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
		{
			if (game_time_get() < command_globals.next_side_wave_time[side])
				continue;
			command_globals.next_side_wave_time[side] = game_time_get() + COMMAND_UPKEEP_SECONDS * TICKS_PER_SECOND;
			if (!shades)
				command_shades_update();
			shades = TRUE;
			command_upgrade_objects_update(side);
		}
	}
}

static boolean command_begin(void);
static void command_build_out(void);

/* the Command game begun: the commander above his base (a network game's
client: the host's game shown, nothing of it run here) */
static void command_net_client_begin(void);
static void command_net_host_begin(void);

static void command_game_begin(
	void)
{
	real_point3d base;
	short side;

	command_globals.side = command_player_side(command_player_index());
	if (command_globals.side == NONE || !command_begin())
		return;
	command_globals.game_pending = FALSE;
	command_globals.game_running = TRUE;
	command_globals.waves = 0;
	command_globals.next_wave_time = game_time_get();
	command_globals.hq_open = FALSE;
	command_globals.hq_row = 0;
	for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
	{
		command_spartans[side].unit_index = NONE;
		command_spartans[side].possessor = NONE;
		command_spartans[side].aware_time = NONE;
		command_spartans[side].paid = FALSE;
		command_spartans[side].counted = FALSE;
		command_armory_pick[side] = 0;
	}
	csmemset(command_spartan_owned, 0, sizeof(command_spartan_owned));
	{
		short slot;

		for (slot = 0; slot < COMMAND_PLAYERS; slot++)
		{
			command_seats[slot] = NONE;
			command_seat_spot_valid[slot] = FALSE;
		}
	}
	csmemset(command_upgrades, 0, sizeof(command_upgrades));
	csmemset(command_upgrade_done_time, 0, sizeof(command_upgrade_done_time));
	csmemset(command_pending_buys, 0, sizeof(command_pending_buys));
	if (command_client())
	{
		command_net_client_begin();
		if (command_base_point(command_globals.side, &base))
			command_camera_jump(&base, "the base");
		platform_log("command: the host's Command game on %s, commanding %s", main_get_multiplayer_map_name() ? main_get_multiplayer_map_name() : "?",
			command_side_names[command_globals.side]);
		return;
	}
	/* (the economy: each side's resources, its territories, no upgrades) */
	{
		for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
		{
			command_globals.resources[side] = COMMAND_START_RESOURCES;
			command_globals.next_side_wave_time[side] = game_time_get();
			command_globals.side_waves[side] = 0;
		}
		command_globals.next_income_time = game_time_get() + COMMAND_INCOME_SECONDS * TICKS_PER_SECOND;
		/* (editor6: what of the layout the game takes, said once) */
		{
			short markers = forge_mode_layout_count(NONE);

			for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
			{
				struct forge_mode_area home;

				if (forge_mode_home_area(side == _command_side_red ? 0 : 1, &home))
				{
					platform_log("command: layout: %s's home base is the layout's Base %c (#%ld) at %.2f %.2f %.2f, %.1f wu across%s",
						command_side_names[side], home.letter, home.id, home.center.x, home.center.y, home.center.z, home.radius,
						home.owner == NONE ? " (no owner set: taken by its letter)" : "");
				}
				else if (markers > 0)
					platform_log("command: layout: no base for %s in the layout: the map's own (its flag stand)", command_side_names[side]);
			}
			if (markers == 0)
				platform_log("command: layout: none (no game mode markers): the map's own bases, hills and landings");
		}
		command_territories_build();
		{
			short index;

			for (index = 0; index < COMMAND_BAY_VEHICLES; index++)
				command_bay[0][index] = command_bay[1][index] = NONE;
			for (index = 0; index < COMMAND_SIDE_VEHICLES; index++)
				command_side_vehicles[0][index] = command_side_vehicles[1][index] = NONE;
			for (index = 0; index < COMMAND_CRATES; index++)
				command_crates[0][index] = command_crates[1][index] = NONE;
			for (index = 0; index < COMMAND_ARMORY_ITEMS; index++)
				command_armory[0][index] = command_armory[1][index] = NONE;
		}
	}
	/* (the objectives: the territories, the flags, the scores; the AI
	commander's side) */
	command_objectives_begin();
	command_ai_begin();
	/* (each base's landing spot and its way in) */
	command_path_build(_command_side_red);
	command_path_build(_command_side_blue);
	/* (no Spartan at the start: bought late in the game, C9 - each human
	above the map in his seat, his spawn) */
	command_humans_update();
	if (command_base_point(command_globals.side, &base))
		command_camera_jump(&base, "the base");
	if (command_network())
		command_net_host_begin();
	platform_log("command: the Command game begun on %s", main_get_map_name() ? main_get_map_name() : "?");
}

/* ---------- public code */

void command_game_request(
	void)
{
	command_globals.game_requested = TRUE;
	platform_log("command: a Command game asked for");
}

/* (1.1.4: the settings panel's Start Command, whose game did not start) the
request forgotten */
void command_game_cancel(
	void)
{
	command_globals.game_requested = FALSE;
}

void command_map_loaded(
	char const *scenario_name,
	boolean forge_game)
{
	/* (every object of the last map is gone, its squads with them) */
	command_globals.active = FALSE;
	command_globals.squads_hosted = FALSE;
	command_combo_dpad = 0;
	command_globals.possessed_unit_index = NONE;
	command_globals.camera_placed = FALSE;
	{
		short side, slot;

		for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
		{
			command_spartans[side].unit_index = NONE;
			command_spartans[side].possessor = NONE;
			command_spartans[side].aware_time = NONE;
			command_spartan_owned[side] = FALSE;
		}
		for (slot = 0; slot < COMMAND_PLAYERS; slot++)
		{
			command_seats[slot] = NONE;
			command_seat_spot_valid[slot] = FALSE;
		}
	}
	command_globals.selection = 0;
	command_globals.selected = NONE;
	command_globals.game_running = FALSE;
	command_globals.game_pending = FALSE;
	command_globals.view_valid = FALSE;
	csmemset(command_globals.squads, 0, sizeof(command_globals.squads));
	command_globals.next_number = 0;
	{
		short side, shade;

		for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
			for (shade = 0; shade < COMMAND_SHADES_PER_BASE; shade++)
				command_globals.shades[side][shade] = NONE;
	}
	command_bounds_build();
	command_routes_reset();
	csmemset(command_flights, 0, sizeof(command_flights));
	csmemset(command_paths, 0, sizeof(command_paths));
#ifndef HALO_RELEASE
	/* (tests: HALO_TEST_COMMAND_GAME=1, the Forge game the harness starts a
	Command game, as START COMMAND asks) */
	if (getenv("HALO_TEST_COMMAND_GAME") && forge_game)
		command_globals.game_requested = TRUE;
#endif
	/* (a network Command game, the host's or a client's of it: its hello
	said so, forge_net.inc) */
	if (forge_game && forge_net_command_session())
		command_globals.game_requested = TRUE;
	if (!command_globals.game_requested)
		return;
	command_globals.game_requested = FALSE;
	/* (1.1.4: Blood Gulch and the PC-exclusive big team maps, forge.c's
	forge_map_command; MCC's copy of a map loads under its own level name) */
	if (!forge_game || !scenario_name || !forge_map_command(forge_map_of_scenario(scenario_name)))
	{
		platform_log("command: a Command game asked for, but %s is not a Forge game of one of Command's maps",
			scenario_name ? scenario_name : "?");
		return;
	}
	/* (the map built out now, as it loads: in memory, nothing on the card) */
	command_build_out();
	command_globals.game_pending = TRUE;
	platform_log("command: %s loaded for a Command game", scenario_name);
}

boolean command_game_running(
	void)
{
	return command_globals.game_running;
}

boolean command_game_playing(
	void)
{
	return command_globals.game_requested || command_globals.game_pending || command_globals.game_running;
}

char const *command_objective_text(
	void)
{
	static char text[64];

	snprintf(text, sizeof(text), "%s: %s", T("OBJECTIVE"),
		command_objectives.objectives == _command_objectives_territories ? T("TERRITORIES") :
		command_objectives.objectives == _command_objectives_flag ? T("FLAG") : T("TERRITORIES AND FLAG"));
	return text;
}

void command_objective_next(
	void)
{
	command_objectives.objectives = command_objectives.objectives == _command_objectives_both ? _command_objectives_territories :
		command_objectives.objectives == _command_objectives_territories ? _command_objectives_flag : _command_objectives_both;
	platform_log("command: objectives: %s", command_objectives_log_names[command_objectives.objectives & 3]);
}

/* (1.1.4: the settings panel's Forge page, Menu style Halo CE's way to
Command's options) the objectives chosen: 0 territories and flag, 1
territories, 2 flag; the AI commander's difficulty, 0 Easy to 3 Legendary */
int command_objective_index(
	void)
{
	return command_objectives.objectives == _command_objectives_territories ? 1 :
		command_objectives.objectives == _command_objectives_flag ? 2 : 0;
}

/* (the console's "where") the scores, the resources, each side's AI */
static void command_where_scores(
	void)
{
	platform_log("command: where: scores red %ld blue %ld (to %ld), resources red %ld blue %ld, AI alive red %d blue %d, squads red %d blue %d",
		command_objectives.scores[0], command_objectives.scores[1], command_objectives.score_limit, command_globals.resources[0],
		command_globals.resources[1], command_ai_alive(_command_side_red), command_ai_alive(_command_side_blue),
		command_ai_squad_count(_command_side_red), command_ai_squad_count(_command_side_blue));
	/* (the match's lag, Ben's report: what piles up - bodies, dropped
	weapons, projectiles - counted by type) */
	{
		struct object_iterator iterator;
		struct object_datum *object;
		long counts[NUMBER_OF_OBJECT_TYPES];
		long dead = 0, total = 0;
		short type;

		csmemset(counts, 0, sizeof(counts));
		object_iterator_new(&iterator, _object_mask_all, 0);
		while ((object = object_iterator_next(&iterator)) != NULL)
		{
			type = object->object.type;
			if (type >= 0 && type < NUMBER_OF_OBJECT_TYPES)
				counts[type]++;
			if (type == _object_type_biped && TEST_FLAG(object->object.damage_flags, _object_dead_bit))
				dead++;
			total++;
		}
		platform_log("command: where: objects %ld: bipeds %ld (%ld dead), vehicles %ld, weapons %ld, equipment %ld, garbage %ld, projectiles %ld, scenery %ld, devices %ld",
			total, counts[_object_type_biped], dead, counts[_object_type_vehicle], counts[_object_type_weapon],
			counts[_object_type_equipment], counts[_object_type_garbage], counts[_object_type_projectile],
			counts[_object_type_scenery], counts[_object_type_machine] + counts[_object_type_control] + counts[_object_type_light_fixture]);
	}
}

/* (the Command lobby's AI COMMANDER row, C13) the AI commander's
difficulty: the row's words, and the next (Easy, Normal, Heroic, Legendary) */
char const *command_ai_level_text(
	void)
{
	static char text[64];

	snprintf(text, sizeof(text), "%s: %s", T("AI COMMANDER"), T(command_ai_levels[command_ai_level].name));
	return text;
}

int command_ai_level_index(
	void)
{
	return command_ai_level;
}

boolean command_legendary_rules(
	void)
{
	return command_globals.game_running;
}

boolean command_ai_damage_owner(
	long owner_object_index)
{
	struct object_datum *object;

	if (!command_globals.game_running || owner_object_index == NONE)
		return FALSE;
	object = object_try_and_get(owner_object_index);
	if (!object || !TEST_FLAG(_object_mask_unit, FLAG(object->object.type)))
		return FALSE;
	return unit_get(owner_object_index)->unit.actor_index != NONE && command_unit_side(owner_object_index) != NONE;
}

void command_ai_level_next(
	void)
{
	command_ai_level = (short)((command_ai_level + 1) % NUMBER_OF_COMMAND_AI_LEVELS);
	platform_log("command: AI commander: %s", command_ai_levels[command_ai_level].log_name);
}

/* ---- PREPARING COMMAND (FIREFIGHT): the modes' own packs. What a Command
or Firefight game brings onto its multiplayer map - the AI characters, the
dropships, the Shades - is built from the player's own Halo CE campaign maps
(the Xbox's, or MCC's own halo1 maps: scene_pack.c, scene_pack_ce.c) into
pack files of the modes' own on the card (t:\packs\<recipe>.pack, as
Forge's), once: a later game loads them as they are, whatever was played
meanwhile, and they are built again only when their recipe, the builder or
their source map changes (scene_packs.c's stamp). Nothing is put in Halo
CE's campaign cache slots (the old way copied a30 and b40 there: six minutes
on a memory card, and every other mission played evicted the copies).
The lobby begins the builds (command_packs_prebuild, paced, on the packs'
worker thread below the game's); START, with a pack not built yet, opens
PREPARING COMMAND: the builds wanted now, their progress and the time left
shown, the game started once they are all ready; backing out leaves them
to go on at the lobby's pace */

static char const *const command_mode_packs[] = { "command_marines", "command_covenant", "command_dropships" };
/* (Firefight's own, whatever its layout: the Covenant waves and the ships;
a layout's other zones' recipes, if any, are built as its map loads) */
static char const *const firefight_mode_packs[] = { "mode_covenant", "command_dropships" };

enum
{
	_command_prepare_idle = 0,
	_command_prepare_building,
	_command_prepare_cancelling,
	_command_prepare_done,
	_command_prepare_failed,
};

static struct
{
	short state;
	boolean firefight;
	real progress;
	/* (the build's start and the progress then: the time left from the
	rate since) */
	unsigned long long started_us;
	long started_progress;
	char note[256];
	/* (the console's "command prepare": the packs alone, no game after) */
	boolean no_game;
	/* (the Forge lobby's own packs - EXTRA OBJECTS, which these games load
	too - still being built when it began: waited for as one more piece) */
	boolean forge_packs;
} command_prepare;

static void command_mode_packs_get(
	boolean firefight,
	char const *const **names,
	short *count)
{
	*names = firefight ? firefight_mode_packs : command_mode_packs;
	*count = firefight ? (short)NUMBEROF(firefight_mode_packs) : (short)NUMBEROF(command_mode_packs);
}

void command_packs_prebuild(
	boolean firefight)
{
	char const *const *names;
	short count, index;

	command_mode_packs_get(firefight, &names, &count);
	for (index = 0; index < count; index++)
	{
		short state = scene_packs_prebuild(names[index]);

		platform_log("command: packs: %s wanted for the %s lobby: %s", names[index], firefight ? "Firefight" : "Command",
			scene_packs_state_name(state));
	}
}

/* (a joiner of a network Command game, in its lobby: forge_net.inc) whether
Command's packs are still being built here, and how far (0 to 100) - the
host's start waits for them, as for a Forge game's packs */
boolean command_packs_building(
	short *percent)
{
	char const *const *names;
	short count, index, building = 0, finished = 0;
	long done_sum = 0;

	command_mode_packs_get(FALSE, &names, &count);
	for (index = 0; index < count; index++)
	{
		long done = 0;
		short state = scene_packs_status(names[index], &done, NULL, 0);

		if (state == _scene_pack_state_building)
		{
			building++;
			done_sum += PIN(done, 0, 100);
		}
		else if (state == _scene_pack_state_ready || state == _scene_pack_state_loaded)
			finished++;
	}
	if (percent)
		*percent = building ? (short)((done_sum + 100L * finished) / (building + finished)) : 100;
	return building != 0;
}

/* whether a pack is not ready to load (not built, being checked or built) */
static boolean command_pack_pending(
	char const *name)
{
	short state = scene_packs_status(name, NULL, NULL, 0);

	return state != _scene_pack_state_ready && state != _scene_pack_state_loaded && state != _scene_pack_state_failed;
}

boolean command_prepare_needed_for(
	boolean firefight)
{
	char const *const *names;
	short count, index;

	command_mode_packs_get(firefight, &names, &count);
	for (index = 0; index < count; index++)
	{
		/* (not asked for yet: asked now, its file checked on the worker) */
		if (scene_packs_status(names[index], NULL, NULL, 0) == _scene_pack_state_none)
			scene_packs_prebuild(names[index]);
		if (command_pack_pending(names[index]))
			return TRUE;
	}
	/* (and the Forge lobby's packs these games load: never built as the
	map loads, a Vita's minute behind the loading screen) */
	return forge_packs_building(NULL, 0, NULL);
}

boolean command_prepare_needed(
	void)
{
	return command_prepare_needed_for(FALSE);
}

boolean command_prepare_begin_for(
	boolean firefight)
{
	if (command_prepare.state == _command_prepare_building)
		return TRUE;
	csmemset(&command_prepare, 0, sizeof(command_prepare));
	command_prepare.firefight = firefight;
	command_packs_prebuild(firefight);
	command_prepare.state = _command_prepare_building;
	command_prepare.started_us = vita_host_time_us();
	command_prepare.started_progress = -1;
	command_prepare.forge_packs = forge_packs_building(NULL, 0, NULL);
	platform_log("command: prepare: the %s packs' builds wanted now", firefight ? "Firefight" : "Command");
	return TRUE;
}

boolean command_prepare_begin(
	void)
{
	return command_prepare_begin_for(FALSE);
}

boolean command_prepare_starts_game(
	void)
{
	return !command_prepare.no_game;
}

boolean command_prepare_firefight(
	void)
{
	return command_prepare.firefight;
}

void command_prepare_cancel(
	void)
{
	if (command_prepare.state == _command_prepare_building)
		platform_log("command: prepare: backed out of (the builds go on at the lobby's pace)");
	command_prepare.state = _command_prepare_idle;
}

short command_prepare_update(
	char *line,
	int line_size,
	real *progress)
{
	char const *const *names;
	short count, index;

	if (line && line_size > 0)
		line[0] = 0;
	command_mode_packs_get(command_prepare.firefight, &names, &count);
	if (command_prepare.state == _command_prepare_building)
	{
		long done = 0;
		short ready = 0;
		char building[96];

		building[0] = 0;
		/* (the player waits: the builds at full pace) */
		scene_packs_prebuilds_wanted_now();
		for (index = 0; index < count && command_prepare.state == _command_prepare_building; index++)
		{
			long pack_progress = 0;
			char why[160];
			short state;

			why[0] = 0;
			state = scene_packs_status(names[index], &pack_progress, why, sizeof(why));
			if (state == _scene_pack_state_none)
				state = scene_packs_prebuild(names[index]);
			if (state == _scene_pack_state_ready || state == _scene_pack_state_loaded)
			{
				ready++;
				done += 100;
			}
			else if (state == _scene_pack_state_failed)
			{
				snprintf(command_prepare.note, sizeof(command_prepare.note), "%s %s",
					T("Your Halo CE campaign maps couldn't be read for this mode:"), why[0] ? why : "?");
				command_prepare.state = _command_prepare_failed;
				platform_log("command: prepare: %s failed: %s", names[index], why[0] ? why : "?");
			}
			else
			{
				done += PIN(pack_progress, 0, 99);
				if (!building[0] || scene_packs_building_now(names[index]))
					snprintf(building, sizeof(building), "%s", names[index]);
			}
		}
		if (command_prepare.state == _command_prepare_building)
		{
			short total = count;

			if (command_prepare.forge_packs)
			{
				long forge_progress = 0;

				total++;
				if (forge_packs_building(NULL, 0, &forge_progress))
					done += PIN(forge_progress, 0, 99);
				else
				{
					ready++;
					done += 100;
				}
			}
			command_prepare.progress = (real)done / (real)(total * 100);
			if (ready == total)
			{
				command_prepare.state = _command_prepare_done;
				command_prepare.progress = 1.f;
				platform_log("command: prepare: the %s packs are ready in %.1f s", command_prepare.firefight ? "Firefight" :
					"Command", (double)(vita_host_time_us() - command_prepare.started_us) / 1000000.0);
			}
			else if (line && line_size > 0)
			{
				long percent = (long)(command_prepare.progress * 100.f);
				unsigned long long now = vita_host_time_us();

				/* (the time left from the rate since the first progress
				seen: the first build's read of its map shows none for a
				while) */
				if (command_prepare.started_progress < 0 && percent > 0)
				{
					command_prepare.started_progress = percent;
					command_prepare.started_us = now;
				}
				if (command_prepare.started_progress >= 0 && percent > command_prepare.started_progress &&
					now - command_prepare.started_us > 3000000ULL)
				{
					double rate = (double)(percent - command_prepare.started_progress) /
						((double)(now - command_prepare.started_us) / 1000000.0);
					long left = (long)((double)(100 - percent) / rate + 0.5);

					if (left >= 90)
						snprintf(line, (size_t)line_size, T("%ld%% - about %ld minutes left"), percent, (left + 30) / 60);
					else
						snprintf(line, (size_t)line_size, T("%ld%% - about %ld seconds left"), percent, left < 5 ? 5L : left);
				}
				else
				{
					snprintf(line, (size_t)line_size, T("%ld%% - working out the time left..."), percent);
				}
			}
		}
	}
	if (line && line_size > 0)
	{
		if (command_prepare.state == _command_prepare_failed)
			snprintf(line, (size_t)line_size, "%s", command_prepare.note);
	}
	if (progress)
		*progress = command_prepare.state == _command_prepare_done ? 1.f :
			command_prepare.state == _command_prepare_building ? command_prepare.progress : 0.f;
	return command_prepare.state;
}

boolean command_game_asked(
	void)
{
#ifndef HALO_RELEASE
	if (getenv("HALO_TEST_COMMAND_GAME"))
		return TRUE;
#endif
	return command_globals.game_requested || forge_net_command_session();
}

/* (carnage_report.c) the report's rows as the Command game's sides: each a
team row (its score, the enemy units it killed, its units lost and its
Spartans' deaths, its captures, its seconds of territory), its players under
it (the commander: his Spartan's kills and captures, as AI or played), and
under blue, with no player, its AI commander; the winner first */
boolean command_carnage_report(
	struct carnage_report *report,
	boolean in_game)
{
	static struct carnage_report_row players[CARNAGE_REPORT_MAXIMUM_ROWS];
	static short const colors[NUMBER_OF_COMMAND_SIDES] = { 2, 3 };
	long player_count = 0;
	short order[NUMBER_OF_COMMAND_SIDES];
	long index, row = 0;
	short rank, side;

	if (!command_globals.game_running)
		return FALSE;
	for (index = 0; index < report->row_count && player_count < CARNAGE_REPORT_MAXIMUM_ROWS; index++)
	{
		if (!report->rows[index].team_row)
			players[player_count++] = report->rows[index];
	}
	/* (the winner first; still playing, the higher score) */
	order[0] = _command_side_red;
	order[1] = _command_side_blue;
	if ((!in_game && command_objectives.over && command_objectives.winner == _command_side_blue) ||
		((in_game || !command_objectives.over || command_objectives.winner == COMMAND_DRAW) &&
			command_objectives.scores[1] > command_objectives.scores[0]))
	{
		order[0] = _command_side_blue;
		order[1] = _command_side_red;
	}
	report->type = CARNAGE_REPORT_TYPE_COMMAND;
	report->teams = TRUE;
	report->team_count = NUMBER_OF_COMMAND_SIDES;
	report->winning_row = NONE;
	report->winning_team = !in_game && command_objectives.over && command_objectives.winner != COMMAND_DRAW ?
		command_objectives.winner : NONE;
	for (rank = 0; rank < NUMBER_OF_COMMAND_SIDES; rank++)
	{
		struct carnage_report_row *team;
		boolean tied = command_objectives.scores[0] == command_objectives.scores[1] &&
			(in_game || !command_objectives.over || command_objectives.winner == COMMAND_DRAW);
		long place = tied ? 0 : rank;
		long members = 0;

		side = order[rank];
		if (row >= CARNAGE_REPORT_MAXIMUM_ROWS)
			break;
		team = &report->rows[row++];
		csmemset(team, 0, sizeof(*team));
		team->team_row = TRUE;
		team->team = side;
		team->color = colors[side];
		team->place = place;
		team->tied = tied;
		team->won = report->winning_team == side;
		team->player_index = NONE;
		team->machine = NONE;
		team->local_player_index = NONE;
		snprintf(team->name, sizeof(team->name), "%s", side == _command_side_red ? T("RED TEAM") : T("BLUE TEAM"));
		snprintf(team->score, sizeof(team->score), "%ld", command_objectives.scores[side]);
		team->score_value = command_objectives.scores[side];
		team->kills = command_objectives.kills[side];
		team->deaths = command_objectives.losses[side];
		team->objective[0] = command_objectives.captures[side];
		team->objective[1] = command_objectives.territory_seconds[side];
		for (index = 0; index < player_count && row < CARNAGE_REPORT_MAXIMUM_ROWS; index++)
		{
			struct carnage_report_row *player = &report->rows[row];
			struct player_datum *datum = players[index].player_index != NONE ?
				player_try_and_get(players[index].player_index) : NULL;
			short player_side = datum && (short)datum->team_index == command_side_team(_command_side_blue) ?
				_command_side_blue : _command_side_red;

			if (player_side != side)
				continue;
			*player = players[index];
			player->team = side;
			player->color = colors[side];
			player->place = place;
			player->tied = tied;
			player->won = team->won;
			/* (the commander's: his Spartan's tally, as AI or played - the
			game counts only players killed -, its deaths the game's own) */
			if (datum && players[index].player_index == command_player_index())
			{
				/* (a local game's player has no name of the game's: his
				profile's, as his nameplate shows it) */
				if (!player->name[0])
					menus_player_name(player->name, (int)sizeof(player->name));
				player->kills = command_objectives.spartan_kills;
				player->objective[0] = command_objectives.spartan_captures;
				player->objective[1] = 0;
				player->score_value = command_objectives.spartan_captures * COMMAND_CAPTURE_POINTS;
				snprintf(player->score, sizeof(player->score), "%ld", player->score_value);
			}
			team->assists += player->assists;
			team->suicides += player->suicides;
			team->betrayals += player->betrayals;
			team->local |= player->local;
			row++;
			members++;
		}
		/* (a side with no player: its AI commander) */
		if (!members && row < CARNAGE_REPORT_MAXIMUM_ROWS)
		{
			struct carnage_report_row *commander = &report->rows[row++];

			csmemset(commander, 0, sizeof(*commander));
			commander->team = side;
			commander->color = colors[side];
			commander->place = place;
			commander->tied = tied;
			commander->won = team->won;
			commander->player_index = NONE;
			commander->machine = NONE;
			commander->local_player_index = NONE;
			snprintf(commander->name, sizeof(commander->name), "%s", T("AI Commander"));
			snprintf(commander->score, sizeof(commander->score), "%ld", command_objectives.scores[side]);
			commander->score_value = command_objectives.scores[side];
			commander->objective[0] = command_objectives.captures[side];
		}
	}
	report->row_count = row;
	return TRUE;
}

void command_object_damaged(
	long object_index,
	real damage,
	long owner_object_index)
{
	short index;

	if ((!command_globals.game_running && !command_globals.squads_hosted) || damage <= 0.f || object_index == NONE ||
		command_client())
	{
		return;
	}
	if (command_globals.game_running)
		command_hurt_note(object_index, owner_object_index);
	if (command_globals.game_running && owner_object_index != NONE)
	{
		struct command_squad const *owner = command_squad_of_unit(owner_object_index, NULL);
		short side, shade;

		if (owner && owner->crew)
		{
			command_crew_hits[owner->side]++;
			command_crew_damage[owner->side] += damage;
		}
		else
		{
			for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
			{
				for (shade = 0; shade < COMMAND_SHADES_PER_BASE; shade++)
				{
					if (command_globals.shades[side][shade] == owner_object_index)
					{
						command_crew_hits[side]++;
						command_crew_damage[side] += damage;
					}
				}
			}
		}
	}
	for (index = 0; index < COMMAND_FLIGHTS; index++)
	{
		struct command_flight *flight = &command_flights[index];

		if (!flight->used || flight->vehicle_index != object_index || flight->killed || flight->state == _command_flight_down)
			continue;
		flight->damage += damage;
		if (flight->hosted)
		{
			struct object_datum *owner = owner_object_index != NONE ? object_try_and_get(owner_object_index) : NULL;

			if (owner && (FLAG(owner->object.type) & _object_mask_unit) && ((struct unit_datum *)owner)->unit.player_index != NONE)
			{
				flight->attacker_player_index = ((struct unit_datum *)owner)->unit.player_index;
				flight->attacker_unit_index = owner_object_index;
			}
		}
		if (flight->damage >= command_ship_hull[flight->ship])
		{
			/* (dead as the game kills a unit: its own death and its explosion;
			then its wreck's fall) */
			command_flight_crash_begin(flight);
			unit_kill(object_index);
			platform_log("command: flight %d: the %s takes %.0f (%.0f of its hull %.0f): shot down", index + 1,
				command_ship_log_names[flight->ship], damage, flight->damage, command_ship_hull[flight->ship]);
		}
		else
		{
			platform_log("command: flight %d: the %s takes %.0f (%.0f of its hull %.0f)", index + 1,
				command_ship_log_names[flight->ship], damage, flight->damage, command_ship_hull[flight->ship]);
		}
		return;
	}
}

void command_tick(
	void)
{
	/* (a network game's client: the host flies them, the netcode moves them) */
	if ((!command_globals.game_running && !command_globals.squads_hosted) || command_client())
		return;
	command_flights_tick();
}

/* ---------- public code (the rest) */

boolean command_active(
	void)
{
	return command_globals.active;
}

void command_toggle_request(
	void)
{
	__atomic_store_n(&command_globals.toggle_requested, 1, __ATOMIC_RELEASE);
	platform_log("command: %s asked from the Forge menu", command_globals.active ? "off" : "on");
}

boolean command_command(
	char const *arguments)
{
	char word[32];
	char const *text = arguments;

	if (!command_word(&text, word, sizeof(word)))
	{
		if (command_globals.active)
			command_end();
		else
			command_begin();
		return TRUE;
	}
	while (*text == ' ')
		text++;
	/* (a network game's client: its commander's view, his requests to the
	host and the logs alone - the game is the host's) */
	if (command_client())
	{
		static char const *const client_words[] =
		{
			"where", "net", "select", "move", "attack", "hold", "buy", "possess", "leave", "camera", "press", "stick", "mouse",
			"lclick", "rclick", "drag", "flag", "colours",
		};
		short index;

		for (index = 0; index < (short)NUMBEROF(client_words); index++)
		{
			if (!strcmp(word, client_words[index]))
				break;
		}
		if (index == (short)NUMBEROF(client_words))
		{
			platform_log("command: %s: the host's to do (a network game's client)", word);
			return FALSE;
		}
	}
	if (!strcmp(word, "on"))
		return command_begin();
	if (!strcmp(word, "off"))
	{
		command_end();
		return TRUE;
	}
	/* (C13) Halo CE's difficulty table, each level's (the soldiers on
Legendary's), and the AI commander's level */
	if (!strcmp(word, "rules"))
	{
		extern real game_difficulty_value_at(short value_type, short difficulty);
		short value;

		for (value = 0; value <= _game_difficulty_value_grenade_timer_scale; value++)
		{
			platform_log("command: rules: value %d: easy %.3f normal %.3f heroic %.3f legendary %.3f, now %.3f", value,
				game_difficulty_value_at(value, 0), game_difficulty_value_at(value, 1), game_difficulty_value_at(value, 2),
				game_difficulty_value_at(value, 3), game_difficulty_get_value(value));
		}
		return TRUE;
	}
	if (!strcmp(word, "ai"))
	{
		short level;

		for (level = 0; level < NUMBER_OF_COMMAND_AI_LEVELS; level++)
		{
			if (!strcmp(text, command_ai_levels[level].log_name))
			{
				command_ai_level = level;
				platform_log("command: AI commander: %s", command_ai_levels[level].log_name);
				return TRUE;
			}
		}
		return FALSE;
	}
#ifndef HALO_RELEASE
	/* (tests) "aiside red|blue on|off": that side's AI commander playing or
	not; "order <n> x y z": squad n (either side's) moved there, as its
	commander orders it */
	if (!strcmp(word, "aiside"))
	{
		char side_word[16] = "", state[8] = "";
		short side;

		if (sscanf(text, "%15s %7s", side_word, state) != 2)
			return FALSE;
		side = !strcmp(side_word, "blue") ? _command_side_blue : _command_side_red;
		command_ai_test_off[side] = strcmp(state, "on") != 0 && strcmp(state, "play") != 0;
		/* ("play": the AI commander plays the side even with its human
		commander above it - an AI against AI match, the lag measures) */
		command_ai_test_play[side] = !strcmp(state, "play");
		command_ai[side].next_think_time = game_time_get();
		platform_log("command: AI commander of %s %s (test)", command_side_names[side], command_ai_test_off[side] ? "off" : "on");
		return TRUE;
	}
	if (!strcmp(word, "order"))
	{
		char numbers[64] = "";
		char *next = NULL;
		char *part;
		real_point3d point;
		real_point3d ground;
		long surface_index = NONE;
		unsigned long mask = 0;
		short side = NONE;

		/* ("order 5,6,7 x y z": those squads together, as a box's) */
		if (sscanf(text, "%63s %f %f %f", numbers, &point.x, &point.y, &point.z) != 4)
			return FALSE;
		for (part = strtok_r(numbers, ",", &next); part; part = strtok_r(NULL, ",", &next))
		{
			short squad_index;

			for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
			{
				struct command_squad *squad = &command_globals.squads[squad_index];

				if (squad->used && squad->number == atoi(part))
				{
					squad->ai_goal = NONE;
					side = squad->side;
					mask |= 1UL << squad_index;
				}
			}
		}
		if (!mask)
			return FALSE;
		ground = point;
		command_ground(&point, &ground, &surface_index);
		command_order_squads(side, mask, &ground, surface_index, NONE);
		return TRUE;
	}
#endif
	if (!strcmp(word, "where"))
	{
		short index;

		command_where();
		for (index = 0; index < COMMAND_FLIGHTS; index++)
		{
			struct command_flight const *flight = &command_flights[index];
			struct object_datum *ship = flight->used ? object_try_and_get(flight->vehicle_index) : NULL;
			real_point3d origin = { 0.f, 0.f, 0.f };

			if (!flight->used)
				continue;
			if (ship)
				object_get_origin(flight->vehicle_index, &origin);
			platform_log("command: where: flight %d: a %s, %s's squad %d, %s %.1f of %.1f s, at %.2f %.2f %.2f yaw %.0f, %d riders,"
				" hull %.0f of %.0f", index + 1, command_ship_log_names[flight->ship], command_side_names[flight->side], flight->number,
				command_flight_log_names[flight->state], (real)(game_time_get() - flight->state_time) / (real)TICKS_PER_SECOND,
				flight->state_ticks < 0x7fffffff ? (real)flight->state_ticks / (real)TICKS_PER_SECOND : -1.f, origin.x, origin.y,
				origin.z, flight->yaw * 180.f / _pi, command_flight_riders(flight), command_ship_hull[flight->ship] - flight->damage,
				command_ship_hull[flight->ship]);
		}
		return TRUE;
	}
	/* (the team-colour check, spec 12b: what each species' unit and actor
	variant give the change colours - report, nothing changed) */
	if (!strcmp(word, "colours"))
	{
		static char const *const variants[] =
		{
			"marine assault rifle", "marine_armored assault rifle", "grunt minor plasma pistol", "grunt major needler",
			"jackal minor plasma pistol", "elite minor plasma rifle", "elite major needler", "hunter",
		};
		short index;

		for (index = 0; index < (short)NUMBEROF(variants); index++)
		{
			long variant_index = command_actor_variant_find(variants[index]);
			struct actor_variant_definition *variant;
			struct object_definition *unit;
			short slot;

			if (variant_index == NONE)
			{
				platform_log("command: colours: %s: not in the map's tags", variants[index]);
				continue;
			}
			variant = actor_variant_definition_get(variant_index);
			if (variant->unit_reference.index == NONE)
				continue;
			unit = object_definition_get(variant->unit_reference.index);
			platform_log("command: colours: %s (unit %s): the actor variant's change colours %ld; the unit's %ld", variants[index],
				tag_get_name(variant->unit_reference.index), (long)variant->change_colors.count, (long)unit->object.change_colors.count);
			for (slot = 0; slot < unit->object.change_colors.count && slot < NUMBER_OF_OBJECT_CHANGE_COLORS; slot++)
			{
				struct object_change_color_definition *color = TAG_BLOCK_GET_ELEMENT(&unit->object.change_colors, slot,
					struct object_change_color_definition);

				platform_log("command: colours:   slot %c: darken by %d, scaled by %d, %ld permutations, %.2f %.2f %.2f to %.2f %.2f %.2f",
					'A' + slot, color->darken_by, color->scaled_by, (long)color->permutations.count, color->color_lower_bound.red,
					color->color_lower_bound.green, color->color_lower_bound.blue, color->color_upper_bound.red,
					color->color_upper_bound.green, color->color_upper_bound.blue);
			}
		}
		return TRUE;
	}
	/* (tests: the mode's packs built now, as PREPARING COMMAND builds
	them, no game after; "command prepare firefight": Firefight's) */
	if (!strcmp(word, "prepare"))
	{
		boolean firefight = !strncmp(text, "firefight", 9);

		platform_log("command: prepare: %s", command_prepare_needed_for(firefight) ? "packs to build" : "nothing to build");
		if (!command_prepare_begin_for(firefight))
			return FALSE;
		command_prepare.no_game = TRUE;
		return TRUE;
	}
	/* (the variant: its objectives, its score limit, its time limit) */
	if (!strcmp(word, "objective"))
	{
		if (!strncmp(text, "territories", 11))
			command_objectives.objectives = _command_objectives_territories;
		else if (!strncmp(text, "flag", 4))
			command_objectives.objectives = _command_objectives_flag;
		else if (!strncmp(text, "both", 4))
			command_objectives.objectives = _command_objectives_both;
		else
			return FALSE;
		platform_log("command: objectives: %s", command_objectives_log_names[command_objectives.objectives & 3]);
		return TRUE;
	}
#ifndef HALO_RELEASE
	/* (tests) "list": every unit, weapon and device of the level with its
	place (a mode's spots on a new map) */
	if (!strcmp(word, "list"))
	{
		struct object_iterator iterator;
		struct object_datum *object;

		object_iterator_new(&iterator, _object_mask_unit | _object_mask_weapon | _object_mask_device, 0);
		while ((object = object_iterator_next(&iterator)) != NULL)
		{
			real_point3d origin;

			object_get_origin(iterator.index, &origin);
			platform_log("command: list: %s at %.2f %.2f %.2f%s", tag_get_name(object->definition_index), origin.x, origin.y,
				origin.z, TEST_FLAG(object->object.damage_flags, _object_dead_bit) ? " (dead)" : "");
		}
		return TRUE;
	}
#endif
	if (!strcmp(word, "score") || !strcmp(word, "time"))
	{
		long value = atol(text);

		if (value < 0)
			return FALSE;
		if (!strcmp(word, "score"))
			command_objectives.score_limit = value;
		else
			command_objectives.time_limit_minutes = value;
		platform_log("command: the %s limit %ld", word, value);
		return TRUE;
	}
	if (!command_globals.active)
	{
		platform_log("command: %s: not in command mode (command on)", word);
		return FALSE;
	}
	/* (tests) the game ended now; the flags in the log; the Spartan (or
	the unit he is in) put somewhere */
	if (!strcmp(word, "end"))
	{
		if (!command_globals.game_running)
			return FALSE;
		command_game_end(!strncmp(text, "red", 3) ? _command_side_red : !strncmp(text, "blue", 4) ? _command_side_blue :
			COMMAND_DRAW, "the console");
		return TRUE;
	}
	if (!strcmp(word, "flag"))
	{
		short side;

		for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
		{
			struct command_flag const *flag = &command_objectives.flags[side];
			real_point3d at = { 0.f, 0.f, 0.f };

			if (object_try_and_get(flag->weapon_index))
				command_flag_position(flag, &at);
			platform_log("command: flag: %s's: %s, %s, carrier %lx, at %.2f %.2f %.2f; score %ld (%ld captures, %ld s of territory)",
				command_side_names[side], flag->valid ? "in play" : "not in play", flag->away ? "away" : "home",
				(unsigned long)flag->carrier_index, at.x, at.y, at.z, command_objectives.scores[side], command_objectives.captures[side],
				command_objectives.territory_seconds[side]);
		}
		return TRUE;
	}
	if (!strcmp(word, "put"))
	{
		real_point3d at;
		long unit_index = command_globals.possessed_unit_index != NONE ? command_globals.possessed_unit_index :
			command_my_spartan();

		if (!command_point_parse(text, &at) || !command_living_unit(unit_index))
			return FALSE;
		object_set_position(unit_index, &at, NULL, NULL);
		object_get(unit_index)->object.translational_velocity = *global_zero_vector3d;
		platform_log("command: put: %lx at %.2f %.2f %.2f", (unsigned long)unit_index, at.x, at.y, at.z);
		return TRUE;
	}
	if (!strcmp(word, "spawn"))
	{
		char side_word[16];
		char count_word[8];
		char name[96];
		real_point3d at;
		short side;
		char const *rest;

		if (!command_word(&text, side_word, sizeof(side_word)) || !command_word(&text, count_word, sizeof(count_word)))
			return FALSE;
		side = !strcmp(side_word, "blue") || !strcmp(side_word, "covenant") ? _command_side_blue : _command_side_red;
		while (*text == ' ')
			text++;
		/* (the name, then "at x y z" if given) */
		snprintf(name, sizeof(name), "%s", text);
		rest = strstr(text, " at ");
		if (rest)
		{
			name[rest - text] = 0;
			if (!command_point_parse(rest + 4, &at))
				return FALSE;
		}
		else if (command_globals.cursor_valid)
			at = command_globals.cursor;
		else
			at = command_globals.focus;
		{
			struct command_squad *squad = command_squad_spawn(side, (short)atoi(count_word), name, &at);

			/* (blue's - no one commands it yet - advances on red, as its waves) */
			if (squad && side == _command_side_blue)
				command_wave_landed(squad);
			return squad != NULL;
		}
	}
	if (!strcmp(word, "select"))
	{
		short squad_index;
		short number = (short)atoi(text);

		/* ("select first": the commander's first squad down; "select all") */
		if (!strncmp(text, "first", 5) || !strncmp(text, "all", 3))
		{
			for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
			{
				struct command_squad const *squad = &command_globals.squads[squad_index];

				if (command_squad_mine(squad) && !squad->hero && !squad->crew)
				{
					command_selection_set(text[0] == 'a' ? ~0UL : 1UL << squad_index, "by the console");
					return TRUE;
				}
			}
			platform_log("command: select: no squad of the commander's");
			return FALSE;
		}

		for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
		{
			if (command_globals.squads[squad_index].used && command_globals.squads[squad_index].number == number)
			{
				command_selection_set(1UL << squad_index, "by the console");
				return TRUE;
			}
		}
		platform_log("command: select: no squad %d", number);
		return FALSE;
	}
	if (!strcmp(word, "move"))
	{
		struct command_squad *squad = command_squad_selected();
		real_point3d point;
		real_point3d ground;
		long surface_index = NONE;

		if (!squad)
			return FALSE;
		if (!*text)
		{
			command_order_at_cursor();
			return TRUE;
		}
		if (!command_point_parse(text, &point))
			return FALSE;
		ground = point;
		command_ground(&point, &ground, &surface_index);
		command_order_at(&ground, surface_index, NONE);
		return TRUE;
	}
	if (!strcmp(word, "attack"))
	{
		struct command_squad *squad = command_squad_selected();
		short squad_index;
		short number = (short)atoi(text);

		/* ("attack <n>": squad n's first living unit, as the cursor on it) */
		if (squad && number > 0)
		{
			for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
			{
				struct command_squad *target = &command_globals.squads[squad_index];
				long leader = target->used && target->number == number ? command_squad_leader(target) : NONE;
				real_point3d at;

				if (leader != NONE && target->side != squad->side)
				{
					object_get_origin(leader, &at);
					command_order_at(&at, NONE, leader);
					return TRUE;
				}
			}
			platform_log("command: attack: no enemy squad %d", number);
			return FALSE;
		}
		command_order_at_cursor();
		return TRUE;
	}
	if (!strcmp(word, "hold"))
	{
		command_hold_here();
		return TRUE;
	}
	/* (tests) "wave [red|blue] [kind]": a side's (both's) next wave now, of
	that kind (the waves' table's, from 0); "shoot <flight> [hits]" */
	if (!strcmp(word, "wave"))
	{
		char side_word[16] = "";
		int kind = command_globals.waves;
		short side;

		if (!command_globals.game_running)
			return FALSE;
		sscanf(text, "%15s %d", side_word, &kind);
		if (!command_globals.waves)
			command_globals.waves = 1;
		for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
		{
			if (!side_word[0] || !strcmp(side_word, command_side_names[side]))
				command_wave_send(side, (short)kind);
		}
		return TRUE;
	}
	/* (tests) "buy <row's key> [blue]", "money <red|blue> <amount>",
	"territories": the economy's */
	if (!strcmp(word, "buy"))
	{
		char key[32] = "", side_word[16] = "";
		short row;

		if (sscanf(text, "%31s %15s", key, side_word) < 1 || !command_globals.game_running)
			return FALSE;
		for (row = 0; row < COMMAND_BUYS; row++)
		{
			/* (a network game's: this machine's commander's, its side his;
			a client's asked of the host) */
			if (!strcmp(command_buys[row].key, key) && command_network() && !side_word[0])
			{
				command_buy_here(row);
				return TRUE;
			}
			if (!strcmp(command_buys[row].key, key))
				return command_buy(!strcmp(side_word, "blue") ? _command_side_blue : _command_side_red, row);
		}
		platform_log("command: buy: no row \"%s\"", key);
		return FALSE;
	}
	if (!strcmp(word, "money"))
	{
		char side_word[16];
		long amount;

		if (sscanf(text, "%15s %ld", side_word, &amount) < 2 || command_client())
			return FALSE;
		command_globals.resources[!strcmp(side_word, "blue") ? _command_side_blue : _command_side_red] = amount;
		platform_log("command: money: %s has %ld", side_word, amount);
		return TRUE;
	}
	if (!strcmp(word, "territories"))
	{
		short index;

		platform_log("command: resources: red %ld, blue %ld", command_globals.resources[0], command_globals.resources[1]);
		for (index = 0; index < command_territory_count; index++)
		{
			struct command_territory const *territory = &command_territories[index];

			platform_log("command: territory %d: %s, red %d blue %d inside%s", index + 1,
				territory->owner == NONE ? "no one's" : command_side_names[territory->owner], territory->counts[0],
				territory->counts[1], territory->taker != NONE ? ", being taken" : "");
		}
		return TRUE;
	}
	/* (tests) "spartan kill": the commander's Spartan killed where it stands */
	if (!strcmp(word, "spartan"))
	{
		if (strncmp(text, "kill", 4) || !command_living_unit(command_my_spartan()))
			return FALSE;
		platform_log("command: spartan kill: the Spartan (%lx) killed", (unsigned long)command_my_spartan());
		unit_kill(command_my_spartan());
		return TRUE;
	}
	if (!strcmp(word, "shoot"))
	{
		int number = 0, hits = 1;

		if (sscanf(text, "%d %d", &number, &hits) < 1)
			return FALSE;
		return command_flight_shoot((short)number, (short)PIN(hits, 1, 100));
	}
	/* (tests) "net junk": a client's volley of bad requests (command_net.inc);
	"net": the network game's counts */
	if (!strcmp(word, "net"))
		return command_net_command(text);
	if (!strcmp(word, "possess"))
	{
		if (*text)
		{
			short squad_index;
			short number = (short)atoi(text);

			for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
			{
				if (command_globals.squads[squad_index].used && command_globals.squads[squad_index].number == number)
					command_selection_set(1UL << squad_index, "by the console");
			}
		}
		return command_spartan_in();
	}
	if (!strcmp(word, "leave"))
	{
		command_leave("asked");
		return TRUE;
	}
	if (!strcmp(word, "camera"))
	{
		float x, y, yaw, distance, pitch = COMMAND_PITCH_DEFAULT * 180.f / _pi;
		real_point3d ground;
		real_point3d point;

		if (sscanf(text, "%f %f %f %f %f", &x, &y, &yaw, &distance, &pitch) < 4)
			return FALSE;
		point.x = x;
		point.y = y;
		point.z = command_globals.focus.z;
		command_globals.focus = point;
		if (command_ground(&point, &ground, NULL))
			command_globals.focus.z = ground.z;
		command_globals.yaw = yaw * _pi / 180.f;
		command_globals.distance = PIN(distance, COMMAND_DISTANCE_MINIMUM, COMMAND_DISTANCE_MAXIMUM);
		command_globals.pitch = PIN(pitch * _pi / 180.f, COMMAND_PITCH_MINIMUM, COMMAND_PITCH_MAXIMUM);
		command_globals.camera_jump = TRUE;
		platform_log("command: camera over %.2f %.2f %.2f, yaw %.0f, distance %.1f, pitch %.0f", command_globals.focus.x,
			command_globals.focus.y, command_globals.focus.z, yaw, command_globals.distance,
			command_globals.pitch * 180.f / _pi);
		return TRUE;
	}
	/* (tests: the mouse's pointer, as ui_widget.c hands it: "mouse x y",
	"lclick x y", "rclick x y", "drag x0 y0 x1 y1", the menus' 640x480) */
	if (!strcmp(word, "mouse") || !strcmp(word, "lclick") || !strcmp(word, "rclick") || !strcmp(word, "drag"))
	{
		struct halo_ui_pointer pointer;
		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		int read = sscanf(text, "%d %d %d %d", &x0, &y0, &x1, &y1);

		if (read < 2 || (!strcmp(word, "drag") && read < 4))
			return FALSE;
		/* ("mouse x y frames": held there that many frames) */
		command_globals.test_pointer_frames = (short)(!strcmp(word, "mouse") && read >= 3 ? PIN(x1, 1, 900) : 2);
		csmemset(&pointer, 0, sizeof(pointer));
		pointer.x = pointer.click_x = (short)x0;
		pointer.y = pointer.click_y = (short)y0;
		pointer.moved = TRUE;
		if (!strcmp(word, "rclick"))
			pointer.right_clicks = 1;
		if (!strcmp(word, "lclick") || !strcmp(word, "drag"))
		{
			pointer.left_clicks = 1;
			pointer.held = TRUE;
			command_pointer_take(&pointer, TRUE, TRUE);
			pointer.left_clicks = 0;
			pointer.held = FALSE;
			if (!strcmp(word, "drag"))
			{
				pointer.x = (short)x1;
				pointer.y = (short)y1;
			}
		}
		command_pointer_take(&pointer, TRUE, TRUE);
		platform_log("command: %s at %d %d%s, the pointer at %.0f %.0f of the screen (%s)", word, x0, y0,
			!strcmp(word, "drag") ? " to the second point" : "", command_globals.pointer_x, command_globals.pointer_y,
			command_globals.pointer_valid ? "taken" : "not taken: not the keyboard's view");
		return TRUE;
	}
	if (!strcmp(word, "press"))
	{
		char buttons[64];
		int frames = 2;

		if (!command_word(&text, buttons, sizeof(buttons)))
			return FALSE;
		if (*text)
			frames = atoi(text);
		if (!command_buttons_parse(buttons, command_globals.test_down, &command_globals.test_toggle))
			return FALSE;
		command_globals.test_buttons_frames = (short)PIN(frames, 1, 600);
		platform_log("command: press %s for %d frames", buttons, command_globals.test_buttons_frames);
		return TRUE;
	}
	if (!strcmp(word, "stick"))
	{
		float lx, ly, rx, ry;
		int frames = 2;

		if (sscanf(text, "%f %f %f %f %d", &lx, &ly, &rx, &ry, &frames) < 4)
			return FALSE;
		command_globals.test_sticks[_gamepad_stick_left][0] = lx;
		command_globals.test_sticks[_gamepad_stick_left][1] = ly;
		command_globals.test_sticks[_gamepad_stick_right][0] = rx;
		command_globals.test_sticks[_gamepad_stick_right][1] = ry;
		command_globals.test_sticks_frames = (short)PIN(frames, 1, 600);
		platform_log("command: sticks %.2f %.2f %.2f %.2f for %d frames", lx, ly, rx, ry, command_globals.test_sticks_frames);
		return TRUE;
	}
	platform_log("command: %s: not a command mode command", word);
	return FALSE;
}

boolean command_camera_input(
	short local_player_index,
	real seconds,
	struct forge_camera_input *input)
{
	struct command_pad pad;
	real speed;
	real pan_i;
	real pan_j;
	real turn;
	real_vector3d forward;
	real_point3d position;
	real_point3d ground;
	real dx, dy;
	real yaw_cos, yaw_sin;

	if (!command_globals.active || local_player_index != command_globals.local_player_index ||
		command_globals.possessed_unit_index != NONE || !command_globals.camera_placed)
	{
		return FALSE;
	}
	csmemset(input, 0, sizeof(*input));
	command_pad_get(&pad);
	/* (the left stick: the focus along the ground, as fast as the camera is
	high; the screen's edges under the pointer too) */
	speed = command_globals.distance * COMMAND_PAN_RATE * seconds;
	if (pad.down[_gamepad_analog_button_left_trigger] || (pad.keys & COMMAND_KEY_SHIFT))
		speed *= COMMAND_PAN_FAST;
	pan_i = pad.sticks[_gamepad_stick_left][1];
	pan_j = -pad.sticks[_gamepad_stick_left][0];
	/* (the HQ panel open: the left stick its rows') */
	if (command_globals.hq_open)
		pan_i = pan_j = 0.f;
	command_edge_scroll(&pan_i, &pan_j);
	pan_i = PIN(pan_i, -1.f, 1.f) * speed;
	pan_j = PIN(pan_j, -1.f, 1.f) * speed;
	yaw_cos = (real)cos(command_globals.yaw);
	yaw_sin = (real)sin(command_globals.yaw);
	command_globals.focus.x += yaw_cos * pan_i - yaw_sin * pan_j;
	command_globals.focus.y += yaw_sin * pan_i + yaw_cos * pan_j;
	/* (within the map's bounds: the canyon, not its cliffs' tops) */
	if (command_globals.bounds_valid)
	{
		command_globals.focus.x = PIN(command_globals.focus.x, command_globals.bounds[0], command_globals.bounds[1]);
		command_globals.focus.y = PIN(command_globals.focus.y, command_globals.bounds[2], command_globals.bounds[3]);
	}
	/* (the right stick: across turns the camera about the focus, up and
	down brings it nearer and further; Q and E turn, the wheel zooms) */
	turn = -pad.sticks[_gamepad_stick_right][0];
	if (pad.keys & COMMAND_KEY_Q)
		turn += 1.f;
	if (pad.keys & COMMAND_KEY_E)
		turn -= 1.f;
	command_globals.yaw += PIN(turn, -1.f, 1.f) * COMMAND_TURN_RATE * seconds + pad.look_yaw;
	command_globals.distance *= 1.f - pad.sticks[_gamepad_stick_right][1] * COMMAND_ZOOM_RATE * seconds;
	if (pad.keys & COMMAND_KEY_WHEEL)
		command_globals.distance *= 1.f - pad.wheel * COMMAND_WHEEL_ZOOM;
	command_globals.distance = PIN(command_globals.distance, COMMAND_DISTANCE_MINIMUM, COMMAND_DISTANCE_MAXIMUM);
	/* (the focus at the ground's smoothed height about it - the tops there,
	a base's roof over its floor -, eased slowly: the camera keeps a steady
	height over the ground, C3) */
	{
		real height;

		if (command_ground_smooth(&command_globals.focus, &height))
		{
			real ease = command_globals.camera_jump ? 1.f : PIN(seconds * COMMAND_GROUND_SMOOTH_RATE, 0.f, 1.f);

			command_globals.focus.z += (height - command_globals.focus.z) * ease;
		}
	}
	(void)ground;
	command_camera_pose(seconds, &position, &forward);
	if (command_globals.camera_jump)
	{
		command_globals.camera_jump = FALSE;
		command_camera_place();
		return TRUE;
	}
	/* (the flying camera given what takes it from where it was put last to
	the pose wanted: its yaw and pitch added, its move along its new yaw) */
	yaw_cos = (real)cos(command_globals.yaw);
	yaw_sin = (real)sin(command_globals.yaw);
	input->facing_delta.yaw = command_globals.yaw - command_globals.camera_yaw;
	input->facing_delta.pitch = command_camera_view_pitch - command_globals.camera_pitch;
	dx = position.x - command_globals.camera_position.x;
	dy = position.y - command_globals.camera_position.y;
	input->translation.i = yaw_cos * dx + yaw_sin * dy;
	input->translation.j = -yaw_sin * dx + yaw_cos * dy;
	input->translation.k = position.z - command_globals.camera_position.z;
	command_globals.camera_position = position;
	command_globals.camera_yaw = command_globals.yaw;
	command_globals.camera_pitch = command_camera_view_pitch;

	return TRUE;
}

void command_update(
	void)
{
	struct command_pad pad;
	boolean pressed[NUMBER_OF_GAMEPAD_BUTTONS];
	boolean toggle_pressed;
	unsigned long keys_pressed;
	short button;
	long player_index;
	boolean host = !command_client();

	command_menu_pad = 0;
	if (__atomic_exchange_n(&command_globals.toggle_requested, 0, __ATOMIC_ACQ_REL))
	{
		if (command_globals.active)
			command_end();
		else
			command_begin();
	}
	/* (a Command game's map loaded: begun once the player has spawned) */
	if (command_globals.game_pending && !command_globals.active)
	{
		long pending_player = command_player_index();

		if (pending_player != NONE && command_living_unit(player_get(pending_player)->unit_index) &&
			!game_time_get_paused())
		{
			command_game_begin();
		}
	}
	/* (the host's game: its humans, in their seats or their Spartans, each
	side commanded by them or the AI; the Spartans' new actors) */
	if (host && (command_globals.game_running || command_globals.active))
	{
		short side;

		command_humans_update();
		for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
			command_spartan_aware(side);
	}
	if (command_globals.game_running)
		command_seats_hide();
	if (!command_globals.active)
	{
		/* (command_squads.h: another mode's squads, their moves followed) */
		if (command_globals.squads_hosted)
		{
			command_squads_update();
			command_flights_update();
		}
		/* (the host's game goes on with its commander out of command mode) */
		else if (host && command_globals.game_running)
		{
			command_squads_update();
			command_game_update();
		}
		command_net_frame();
		return;
	}
	player_index = command_player_index();
	if (player_index == NONE || (game_connection() != _game_connection_local && !command_globals.game_running))
	{
		command_globals.active = FALSE;
		command_combo_dpad = 0;
		command_globals.possessed_unit_index = NONE;
		command_globals.camera_placed = FALSE;
		csmemset(command_globals.squads, 0, sizeof(command_globals.squads));
		platform_log("command: gone (a new map, or no player)");
		return;
	}
	command_pad_get(&pad);
	if (command_globals.test_buttons_frames > 0)
		command_globals.test_buttons_frames--;
	if (command_globals.test_sticks_frames > 0)
		command_globals.test_sticks_frames--;
	for (button = 0; button < NUMBER_OF_GAMEPAD_BUTTONS; button++)
	{
		pressed[button] = pad.down[button] && !command_globals.was_down[button];
		command_globals.was_down[button] = pad.down[button];
	}
	toggle_pressed = pad.toggle && !command_globals.toggle_was_down;
	command_globals.toggle_was_down = pad.toggle;
	keys_pressed = pad.keys & ~command_globals.keys_was_down;
	command_globals.keys_was_down = pad.keys;

	/* (the view follows his unit: his Spartan in first person, his seat
	above the map) */
	command_view_follow();
	/* (in first person: the combo back above the map) */
	if (command_globals.possessed_unit_index != NONE)
	{
		if (toggle_pressed)
			command_leave("the combo");
		if (host)
		{
			command_squads_update();
			command_game_update();
		}
		command_alerts_update();
		command_net_frame();
		return;
	}
	if (!command_globals.camera_placed)
		command_view_begin();
	/* (the view from above again where the game took it back - his
	Spartan's respawn chooses the game's perspective -, where it was) */
	else if (!director_forge_camera_flying(command_globals.local_player_index))
	{
		platform_log("command: the view from above put back (the game's camera had taken it)");
		command_camera_place();
	}
	command_cursor_update();
	/* (the HQ panel: open, its buttons are its own) */
	if (command_globals.game_running)
		command_hq_input(&pad, pressed, keys_pressed);
	if (command_globals.hq_open)
	{
		pressed[_gamepad_analog_button_a] = FALSE;
		pad.down[_gamepad_analog_button_a] = FALSE;
		/* (the panel's: B closes it, X is no hold order meanwhile) */
		pressed[_gamepad_analog_button_x] = FALSE;
	}
	command_menu_pad = command_globals.hq_open && command_globals.game_running;
	/* (A: a tap chooses the squad at the cursor; held, a circle grows about
	the cursor and every squad in it is chosen as it is let go) */
	if (pressed[_gamepad_analog_button_a])
	{
		command_globals.sweep_down = TRUE;
		command_globals.sweep_since = system_milliseconds();
		command_globals.sweep_radius = 0.f;
	}
	if (command_globals.sweep_down)
	{
		unsigned long held = system_milliseconds() - command_globals.sweep_since;

		command_globals.sweep_radius = held < COMMAND_SWEEP_MILLISECONDS ? 0.f :
			MIN(COMMAND_SWEEP_RADIUS_MAXIMUM, COMMAND_SWEEP_RADIUS_MINIMUM +
				COMMAND_SWEEP_RADIUS_RATE * (real)(held - COMMAND_SWEEP_MILLISECONDS) / 1000.f);
		if (!pad.down[_gamepad_analog_button_a])
		{
			command_globals.sweep_down = FALSE;
			if (command_globals.sweep_radius > 0.f && command_globals.cursor_valid)
			{
				platform_log("command: sweep of %.1f wu about %.2f %.2f", command_globals.sweep_radius, command_globals.cursor.x,
					command_globals.cursor.y);
				command_selection_set(command_squads_near(&command_globals.cursor, command_globals.sweep_radius, FALSE),
					"by the sweep");
			}
			else
				command_select_at_cursor();
			command_globals.sweep_radius = 0.f;
		}
	}
	if (pressed[_gamepad_analog_button_white] || (keys_pressed & COMMAND_KEY_CTRL_A))
		command_select_all();
	if (pressed[_gamepad_analog_button_black] || (keys_pressed & COMMAND_KEY_B))
		command_jump_button();
	if (keys_pressed & COMMAND_KEY_H)
		command_hold_here();
	if (pressed[_gamepad_analog_button_right_trigger])
		command_order_at_cursor();
	if (pressed[_gamepad_analog_button_x])
		command_hold_here();
	/* (the combo, as Forge's flip: into his Spartan, wherever it stands) */
	if (toggle_pressed)
		command_spartan_in();
	if (host)
	{
		command_squads_update();
		command_game_update();
	}
	command_alerts_update();
	command_net_frame();
}

/* how near an attacker closes before it fights as it will: its variant's
combat range's upper bound, held within reason */
static real command_attack_range(
	struct actor_datum const *actor)
{
	real range = 12.f;

	if (actor->meta.variant_definition_index != NONE)
	{
		struct actor_variant_definition const *variant = actor_variant_definition_get(actor->meta.variant_definition_index);

		if (variant->ranged_combat.combat_range_upper_bound > 0.f)
			range = variant->ranged_combat.combat_range_upper_bound;
	}
	return PIN(range, 5.f, 25.f) * 0.8f;
}

/* ---- the route map (the host's): the AI's own path searches give up
past a few dozen world units (path.c's PATH_NODE_LIST_SIZE nodes), and a
Command order crosses a whole multiplayer map. So the map's ground is laid
out at the game's start as a grid of points (COMMAND_ROUTE_SPACING apart,
each on the ground at its top), two neighbours joined where the AI's path
search finds a way from one to the other within a short bound (their
ground walked, a hill gone round, a cliff not climbed) - a few each frame
on the main thread, between ticks. A member with no way found to its place
is sent through it: the way's length to the order's point from each point
of the grid (from those about the point, on its level), and the member
steps to the grid point near it that its own search reaches with the
shortest way left; there, it searches again - the last part is short */

#define COMMAND_ROUTE_SPACING 10.f
#define COMMAND_ROUTE_COLUMNS 32
#define COMMAND_ROUTE_ROWS 32
#define COMMAND_ROUTE_NODES (COMMAND_ROUTE_COLUMNS * COMMAND_ROUTE_ROWS)
#define COMMAND_ROUTE_DIRECTIONS 8
/* (a soldier's own: an Elite's, wider than a marine's) */
#define COMMAND_ROUTE_RADIUS 0.4f
/* how far a link's search may go about the two (a way round a rock, not
the map's) */
#define COMMAND_ROUTE_SEARCH_BOUND (COMMAND_ROUTE_SPACING * 2.5f)
/* the time a frame's links get (microseconds) */
#define COMMAND_ROUTE_BUILD_MICROSECONDS 2500
/* the grid points about the order's point (within this, on its level) the
way's lengths start from, and the grid points about a member it may step to */
#define COMMAND_ROUTE_GOAL_REACH (COMMAND_ROUTE_SPACING * 1.6f)
#define COMMAND_ROUTE_LEVEL 1.5f
#define COMMAND_ROUTE_STEP_REACH (COMMAND_ROUTE_SPACING * 2.3f)
#define COMMAND_ROUTE_FAR 1e9f

static short const command_route_steps[COMMAND_ROUTE_DIRECTIONS][2] =
{
	{ 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 },
};

static struct
{
	/* the grid laid out, its links searched up to next_link */
	boolean laid_out;
	boolean built;
	long next_link;
	short columns, rows;
	real x0, y0;
	short points, links;
	boolean valid[COMMAND_ROUTE_NODES];
	real_point3d point[COMMAND_ROUTE_NODES];
	long surface[COMMAND_ROUTE_NODES];
	/* a bit a direction: a way from this point to that neighbour */
	unsigned char link[COMMAND_ROUTE_NODES];
} command_routes;

/* each squad's ways' lengths to its order's point, for the order (its time)
they were reckoned for (the tick's: command_actor_orders) */
static struct
{
	long order_time;
	real_point3d point;
	real left[COMMAND_ROUTE_NODES];
} command_route_ways[COMMAND_MAXIMUM_SQUADS];

static void command_routes_reset(
	void)
{
	csmemset(&command_routes, 0, sizeof(command_routes));
	{
		short index;

		for (index = 0; index < COMMAND_MAXIMUM_SQUADS; index++)
			command_route_ways[index].order_time = NONE;
	}
}

static void command_routes_lay_out(
	void)
{
	real const *b = command_globals.bounds;
	short column, row;

	command_routes.laid_out = TRUE;
	if (!command_globals.bounds_valid)
		return;
	command_routes.columns = (short)PIN((long)((b[1] - b[0]) / COMMAND_ROUTE_SPACING) + 1, 1, COMMAND_ROUTE_COLUMNS);
	command_routes.rows = (short)PIN((long)((b[3] - b[2]) / COMMAND_ROUTE_SPACING) + 1, 1, COMMAND_ROUTE_ROWS);
	command_routes.x0 = (b[0] + b[1]) / 2.f - (real)(command_routes.columns - 1) * COMMAND_ROUTE_SPACING / 2.f;
	command_routes.y0 = (b[2] + b[3]) / 2.f - (real)(command_routes.rows - 1) * COMMAND_ROUTE_SPACING / 2.f;
	for (row = 0; row < command_routes.rows; row++)
	{
		for (column = 0; column < command_routes.columns; column++)
		{
			short node = (short)(row * COMMAND_ROUTE_COLUMNS + column);
			real_point3d at;
			struct collision_result collision;
			real_vector3d down = { 0.f, 0.f, -400.f };

			at.x = command_routes.x0 + (real)column * COMMAND_ROUTE_SPACING;
			at.y = command_routes.y0 + (real)row * COMMAND_ROUTE_SPACING;
			at.z = 200.f;
			if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
					&at, &down, NONE, &collision) &&
				collision.surface_index != NONE)
			{
				command_routes.valid[node] = TRUE;
				command_routes.point[node] = collision.point;
				command_routes.surface[node] = collision.surface_index;
				command_routes.points++;
			}
		}
	}
}

/* whether the AI's path search finds a way from one grid point to another,
not far round */
static boolean command_route_search(
	short from,
	short to)
{
	static struct path_state state;
	struct path_input input;

	path_input_new(&input, COMMAND_ROUTE_RADIUS, FALSE, NONE);
	path_input_set_start(&input, &command_routes.point[from], command_routes.surface[from]);
	path_input_set_search_bounds(&input, COMMAND_ROUTE_SEARCH_BOUND);
	path_state_new(&input, &state, NULL);
	path_state_destination(&state, &command_routes.point[to], command_routes.surface[to], 0.f);
	/* (the search alone: its path's steps, and the way about the objects in
	it, are a unit's - path_state_build_path wants one) */
	return path_state_find(&state);
}

/* (the main thread, a Command game's host, each frame: command_game_update)
the route map's next links, for a little time */
static void command_routes_build_step(
	void)
{
	unsigned long long started;

	if (command_routes.built || command_client())
		return;
	if (!command_routes.laid_out)
	{
		command_routes_lay_out();
		return;
	}
	started = vita_host_time_us();
	while (command_routes.next_link < (long)COMMAND_ROUTE_NODES * COMMAND_ROUTE_DIRECTIONS)
	{
		short node = (short)(command_routes.next_link / COMMAND_ROUTE_DIRECTIONS);
		short direction = (short)(command_routes.next_link % COMMAND_ROUTE_DIRECTIONS);
		short column = (short)(node % COMMAND_ROUTE_COLUMNS) + command_route_steps[direction][0];
		short row = (short)(node / COMMAND_ROUTE_COLUMNS) + command_route_steps[direction][1];

		command_routes.next_link++;
		if (!command_routes.valid[node] || column < 0 || row < 0 || column >= command_routes.columns ||
			row >= command_routes.rows || !command_routes.valid[row * COMMAND_ROUTE_COLUMNS + column])
		{
			continue;
		}
		if (command_route_search(node, (short)(row * COMMAND_ROUTE_COLUMNS + column)))
		{
			command_routes.link[node] |= (unsigned char)(1 << direction);
			command_routes.links++;
		}
		if (vita_host_time_us() - started > COMMAND_ROUTE_BUILD_MICROSECONDS)
			return;
	}
	command_routes.built = TRUE;
	platform_log("command: route map: %d grid points (%d by %d, %.0f wu apart), %d links, built by tick %ld",
		command_routes.points, command_routes.columns, command_routes.rows, COMMAND_ROUTE_SPACING, command_routes.links,
		(long)game_time_get());
}

/* (the tick) a squad's ways' lengths to its order's point from each grid
point (the links walked backward from the grid points about the point, on
its level), reckoned once an order; FALSE if none reaches it */
static boolean command_route_ways_reckon(
	short squad_index,
	real_point3d const *goal)
{
	real *left = command_route_ways[squad_index].left;
	static boolean done[COMMAND_ROUTE_NODES];
	short node;
	boolean any = FALSE;

	if (command_route_ways[squad_index].order_time == command_globals.squads[squad_index].order_time &&
		distance3d(&command_route_ways[squad_index].point, goal) < 0.5f)
	{
		return TRUE;
	}
	command_route_ways[squad_index].order_time = command_globals.squads[squad_index].order_time;
	command_route_ways[squad_index].point = *goal;
	for (node = 0; node < COMMAND_ROUTE_NODES; node++)
	{
		left[node] = COMMAND_ROUTE_FAR;
		done[node] = FALSE;
		if (command_routes.valid[node] && command_distance2d(&command_routes.point[node], goal) < COMMAND_ROUTE_GOAL_REACH &&
			fabs(command_routes.point[node].z - goal->z) < COMMAND_ROUTE_LEVEL)
		{
			left[node] = distance3d(&command_routes.point[node], goal);
			any = TRUE;
		}
	}
	if (!any)
		return FALSE;
	/* (the shortest ways: the nearest point not done each time - a few
	hundred points) */
	for (;;)
	{
		short best = NONE;
		short direction;

		for (node = 0; node < COMMAND_ROUTE_NODES; node++)
		{
			if (!done[node] && left[node] < COMMAND_ROUTE_FAR && (best == NONE || left[node] < left[best]))
				best = node;
		}
		if (best == NONE)
			break;
		done[best] = TRUE;
		/* (each neighbour with a link to this one) */
		for (direction = 0; direction < COMMAND_ROUTE_DIRECTIONS; direction++)
		{
			short column = (short)(best % COMMAND_ROUTE_COLUMNS) + command_route_steps[direction][0];
			short row = (short)(best / COMMAND_ROUTE_COLUMNS) + command_route_steps[direction][1];
			short other;
			short back = (short)((direction + COMMAND_ROUTE_DIRECTIONS / 2) % COMMAND_ROUTE_DIRECTIONS);
			real length;

			if (column < 0 || row < 0 || column >= command_routes.columns || row >= command_routes.rows)
				continue;
			other = (short)(row * COMMAND_ROUTE_COLUMNS + column);
			if (done[other] || !(command_routes.link[other] & (1 << back)))
				continue;
			length = left[best] + distance3d(&command_routes.point[best], &command_routes.point[other]);
			if (length < left[other])
				left[other] = length;
		}
	}
	return TRUE;
}

/* (the tick) a member with no way found to goal: a step through the route
map - the grid point about it its own search reaches with the shortest way
left (and shorter than from where it stands). TRUE if it is on its way */
static boolean command_route_step(
	long actor_index,
	short squad_index,
	real_point3d const *origin,
	real_point3d const *goal)
{
	real *left = command_route_ways[squad_index].left;
	short tried[3];
	short tries;
	real here = COMMAND_ROUTE_FAR;
	short node;

	if (!command_routes.built || !command_route_ways_reckon(squad_index, goal))
		return FALSE;
	/* (the way left from where it stands: its nearest grid point's on its
	level) */
	for (node = 0; node < COMMAND_ROUTE_NODES; node++)
	{
		if (command_routes.valid[node] && left[node] < COMMAND_ROUTE_FAR &&
			command_distance2d(&command_routes.point[node], origin) < COMMAND_ROUTE_SPACING &&
			fabs(command_routes.point[node].z - origin->z) < COMMAND_ROUTE_LEVEL * 2.f)
		{
			here = MIN(here, left[node] + distance3d(&command_routes.point[node], origin));
		}
	}
	for (tries = 0; tries < (short)NUMBEROF(tried); tries++)
	{
		short best = NONE;
		real best_score = here;

		for (node = 0; node < COMMAND_ROUTE_NODES; node++)
		{
			real reach;
			real score;
			short previous;

			if (!command_routes.valid[node] || left[node] >= COMMAND_ROUTE_FAR)
				continue;
			reach = distance3d(&command_routes.point[node], origin);
			if (reach > COMMAND_ROUTE_STEP_REACH || reach < 2.f)
				continue;
			for (previous = 0; previous < tries && tried[previous] != node; previous++)
				;
			if (previous < tries)
				continue;
			score = left[node] + reach;
			if (best == NONE ? score < best_score + COMMAND_ROUTE_SPACING : score < best_score)
			{
				best = node;
				best_score = score;
			}
		}
		if (best == NONE)
			return FALSE;
		tried[tries] = best;
		if (actor_move_to_point(actor_index, &command_routes.point[best], command_routes.surface[best], NONE))
		{
#ifndef HALO_RELEASE
			/* (tests: HALO_TEST_COMMAND_ROUTE_LOG=1, each step said) */
			if (getenv("HALO_TEST_COMMAND_ROUTE_LOG"))
				platform_log("command: route: squad %d actor %lx at %.1f %.1f %.1f (%.1f left) to grid %d at %.1f %.1f %.1f (%.1f left), try %d, tick %ld",
					command_globals.squads[squad_index].number, (unsigned long)actor_index, origin->x, origin->y, origin->z, here,
					best, command_routes.point[best].x, command_routes.point[best].y, command_routes.point[best].z, left[best], tries,
					(long)game_time_get());
#endif
			return TRUE;
		}
	}
	return FALSE;
}

/* (a member with no way to its place nor the order's point) the wait
before it searches again */
#define COMMAND_NO_PATH_RETRY_SECONDS 3
/* (no way found to a far point) the step toward it walked meanwhile, and
how long before the whole way is searched again */
#define COMMAND_NO_PATH_STEP 16.f
#define COMMAND_NO_PATH_STEP_SECONDS 2
/* (no way found) the steps tried a tick, and the one tried first next time
(each squad's members') */
#define COMMAND_NO_PATH_STEP_TRIES 3
static short command_step_next[COMMAND_MAXIMUM_SQUADS][COMMAND_SQUAD_SIZE];

void command_actor_orders(
	long actor_index)
{
	struct actor_datum *actor;
	struct command_squad *squad;
	struct unit_datum *unit;
	short member = NONE;
	real_point3d origin;
	real_point3d place;
	real distance;
	boolean moving = FALSE;
	boolean found;

	/* (a Command game's squads follow their orders whether or not this
	machine's own commander is above the map: a network game's are its
	clients' too) */
	if (!command_globals.active && !command_globals.squads_hosted && !command_globals.game_running)
		return;
	actor = actor_get(actor_index);
	unit = command_living_unit(actor->meta.unit_index);
	/* (none for a player's unit, nor one in a seat - a Shade's gunner holds
	it -) */
	if (!unit || unit->unit.player_index != NONE || unit->object.parent_object_index != NONE)
		return;
	squad = command_squad_of_unit(actor->meta.unit_index, &member);
	if (!squad || squad->order == _command_order_none)
		return;
	object_get_origin(actor->meta.unit_index, &origin);
	place = squad->places[member];
	/* (one whose place has no way to it goes to the order's point) */
	if (squad->path_order_time == squad->order_time && (squad->place_unreachable & (1UL << member)))
		place = squad->point;
	distance = command_distance2d(&origin, &place);
	switch (squad->order)
	{
	case _command_order_move:
		/* (a move is a move: on its way under fire too, shooting as it goes) */
		moving = distance > COMMAND_ARRIVED * 0.5f;
		break;
	case _command_order_attack:
		/* (on at the target until within its weapon's combat range - the
		variant's own, as its fighting keeps -, then the actor's own fight:
		magically seeing the target from afar it stood guarding, out of
		range) */
		moving = distance > command_attack_range(actor);
		break;
	case _command_order_hold:
		moving = distance > COMMAND_HOLD_LEASH;
		break;
	case _command_order_advance:
		moving = actor->target.target_prop_index == NONE && distance > COMMAND_ARRIVED;
		break;
	case _command_order_flag:
		/* (onto the flag, or the stand, itself: it is taken or scored by
		touch) */
		moving = distance > COMMAND_FLAG_RUN_ARRIVED;
		break;
	}
	if (!moving)
		return;
	/* (a new order: every member's place tried again) */
	if (squad->path_order_time != squad->order_time)
	{
		squad->path_order_time = squad->order_time;
		squad->place_unreachable = 0;
		csmemset(squad->path_retry_time, 0, sizeof(squad->path_retry_time));
	}
	/* (no way at all last time: not searched again until its wait is over -
	a failed search walks the whole map's graph) */
	if (game_time_get() < squad->path_retry_time[member])
		return;
	if (!(squad->place_unreachable & (1UL << member)))
	{
		found = actor_move_to_point(actor_index, &place, squad->place_surfaces[member], NONE);
		/* (its place on the ring has no way to it - on a rock, past a wall -:
		the order's own point from now on, this order) */
		if (!found && member > 0)
			squad->place_unreachable |= 1UL << member;
	}
	else
		found = FALSE;
	if (squad->place_unreachable & (1UL << member))
		found = actor_move_to_point(actor_index, &squad->point, squad->surface_index, NONE);
	if (!found)
	{
		/* (no way found - the long searches give up across a big map -: a
		step toward it, searched again from there after a while: straight on,
		else turned a little, then more, either way, nearer - a hill, a rock or
		a base in the way -, each on the ground at about the unit's own height
		(not a hill's top over it). A few tried a tick, the next ones next
		time: a failed search walks the whole graph) */
		real_point3d goal = (squad->place_unreachable & (1UL << member)) ? squad->point : place;
		real dx = goal.x - origin.x, dy = goal.y - origin.y;
		real length = (real)sqrt(dx * dx + dy * dy);
		short squad_index = (short)(squad - command_globals.squads);
		short *next = &command_step_next[squad_index][member];
		short tries;

		squad->path_retry_time[member] = game_time_get() + COMMAND_NO_PATH_RETRY_SECONDS * TICKS_PER_SECOND;
		/* (through the route map first) */
		if (length > COMMAND_NO_PATH_STEP * 0.5f && command_route_step(actor_index, squad_index, &origin, &goal))
		{
			squad->path_retry_time[member] = game_time_get() + COMMAND_NO_PATH_STEP_SECONDS * TICKS_PER_SECOND;
			if (actor->orders.move.override_movement_type == NONE && actor->state.mode != COMMAND_ACTOR_MODE_COMBAT)
				actor->orders.move.override_movement_type = _actor_movement_type_combat;
			return;
		}
		if (length > COMMAND_NO_PATH_STEP)
		{
			for (tries = 0; tries < COMMAND_NO_PATH_STEP_TRIES; tries++)
			{
				static real const turns[] = { 0.f, 25.f, -25.f, 50.f, -50.f, 80.f, -80.f };
				short candidate = (short)(*next % (2 * (short)NUMBEROF(turns)));
				real turn = turns[candidate % NUMBEROF(turns)] * _pi / 180.f;
				real step_length = candidate < (short)NUMBEROF(turns) ? COMMAND_NO_PATH_STEP : COMMAND_NO_PATH_STEP * 0.55f;
				real rise = step_length * 0.5f;
				real ci = (real)cos(turn), si = (real)sin(turn);
				real_point3d start = origin;
				real_vector3d down = { 0.f, 0.f, 0.f };
				struct collision_result collision;

				start.x += (dx * ci - dy * si) / length * step_length;
				start.y += (dx * si + dy * ci) / length * step_length;
				start.z += rise;
				down.k = -2.f * rise - 1.f;
				if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
						&start, &down, NONE, &collision) &&
					actor_move_to_point(actor_index, &collision.point, collision.surface_index, NONE))
				{
					squad->path_retry_time[member] = game_time_get() + COMMAND_NO_PATH_STEP_SECONDS * TICKS_PER_SECOND;
					if (actor->orders.move.override_movement_type == NONE && actor->state.mode != COMMAND_ACTOR_MODE_COMBAT)
						actor->orders.move.override_movement_type = _actor_movement_type_combat;
					/* (the same way first next time) */
					*next = candidate;
					return;
				}
				*next = (short)(candidate + 1);
			}
			/* (the rest soon) */
			squad->path_retry_time[member] = game_time_get() + TICKS_PER_SECOND / 2;
		}
	}
	/* (an actor asleep stands still - actor_move_update stops a move in the
	asleep stance - and one alert walks its relaxed walk (a marine 0.35 wu a
	second): on an order it goes in its combat stance, as it runs to a
	firing position in a fight) */
	if (actor->orders.move.override_movement_type == NONE && actor->state.mode != COMMAND_ACTOR_MODE_COMBAT)
		actor->orders.move.override_movement_type = _actor_movement_type_combat;
	/* (once an order: a member with no path to its place said) */
	if (!found && squad->no_path_logged_time != squad->order_time)
	{
		squad->no_path_logged_time = squad->order_time;
		platform_log("command: squad %d: unit %d finds no path to %.2f %.2f %.2f", squad->number, member + 1,
			place.x, place.y, place.z);
	}
}

/* (the Forge menu's CONTROLS in command mode) the commander's controls, as
the player's buttons are named: the Vita's; on the desktop by the button
icons shown (display.button_icons, HALO_BUTTON_ICONS: an Xbox pad's, a
PlayStation's, the keyboard's), as the menus name them */
static char const *command_controls_text_view(
	void)
{
#ifdef HALO_VITA
	return T("Into your Spartan and back: Select + D-pad up.\nMove the view: left stick (faster: hold L).\n"
		"Turn: right stick across. Zoom: up and down.\nChoose the squad at the cursor: Cross.\n"
		"Hold Cross: a circle of squads.\nAll your squads: D-pad right.\nYour base: D-pad left (twice: the last alert).\n"
		"Order there (move, or attack): R.\nHold where they stand: Square.");
#elif defined(COMMAND_BUTTON_ICONS)
	switch (halo_button_icons_shown())
	{
	case _halo_button_icons_playstation:
		return T("Into your Spartan and back: Share + D-pad up.\nMove the view: left stick (faster: hold L2).\n"
			"Turn: right stick across. Zoom: up and down.\nChoose the squad at the cursor: Cross.\n"
			"Hold Cross: a circle of squads.\nAll your squads: L1.\nYour base: R1 (twice: the last alert).\n"
			"Order there (move, or attack): R2.\nHold where they stand: Square.");
	case _halo_button_icons_keyboard:
		return T("Into your Spartan and back: Tab.\nMove the view: W A S D, or the screen's edges.\n"
			"Faster: Shift. Turn: Q, E. Zoom: the wheel.\nChoose: left click, or drag a box.\n"
			"All your squads: Ctrl + A.\nYour base: B (twice: the last alert).\n"
			"Order there (move, or attack): right click.\nHold where they stand: H.");
	default:
		break;
	}
#endif
	return T("Into your Spartan and back: Back + D-pad up.\nMove the view: left stick (faster: hold LT).\n"
		"Turn: right stick across. Zoom: up and down.\nChoose the squad at the cursor: A.\n"
		"Hold A: a circle of squads.\nAll your squads: LB.\nYour base: RB (twice: the last alert).\n"
		"Order there (move, or attack): RT.\nHold where they stand: X.");
}

/* the HQ's line, as the player's buttons are named */
static char const *command_controls_hq(
	void)
{
#ifdef HALO_VITA
	return T("The HQ (squads, upgrades): Triangle; D-pad up and down, Cross buys.");
#elif defined(COMMAND_BUTTON_ICONS)
	switch (halo_button_icons_shown())
	{
	case _halo_button_icons_playstation:
		return T("The HQ (squads, upgrades): Triangle; D-pad up and down, Cross buys.");
	case _halo_button_icons_keyboard:
		return T("The HQ (squads, upgrades): F; 1 to 9 buy its rows.");
	default:
		break;
	}
#endif
	return T("The HQ (squads, upgrades): Y; D-pad up and down, A buys.");
}

char const *command_controls_text(
	void)
{
	static char text[1024];

	snprintf(text, sizeof(text), "%s\n%s", command_controls_text_view(), command_controls_hq());
	return text;
}

/* ---- the rings on the ground (render.c, each window, after the objects:
command_render_world): under each unit of the commander's squads (the
chosen ones bright), the enemy's, the cursor, the sweep's circle, the
squads chosen's orders' points; flat, blended, a little over the ground,
as Forge's markers are drawn */

#define COMMAND_RING_SIDES 16
#define COMMAND_RING_VERTICES 9000
#define COMMAND_RING_LIFT 0.1f

struct command_ring_vertex
{
	real_point3d point;
	unsigned long color;
};

static unsigned long command_color(
	real red,
	real green,
	real blue,
	real alpha)
{
	return ((unsigned long)(PIN(alpha, 0.f, 1.f) * 255.f + 0.5f) << 24) |
		((unsigned long)(PIN(red, 0.f, 1.f) * 255.f + 0.5f) << 16) |
		((unsigned long)(PIN(green, 0.f, 1.f) * 255.f + 0.5f) << 8) |
		(unsigned long)(PIN(blue, 0.f, 1.f) * 255.f + 0.5f);
}

/* a ring's point's height: on the ground under it, as a slope or a step
has it (not the centre's: a flat ring on a hill was half in it), within
reach of the centre's height */
static real command_ring_height(
	real_point3d const *center,
	real_point3d const *point,
	real radius)
{
	struct collision_result collision;
	real_point3d start = *point;
	real reach = 1.5f + radius * 0.6f;
	real_vector3d down = { 0.f, 0.f, 0.f };

	start.z = center->z + reach;
	down.k = -2.f * reach;
	if (radius > 0.f && collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
			&start, &down, NONE, &collision))
	{
		return collision.point.z;
	}
	return center->z;
}

static void command_ring(
	struct command_ring_vertex *vertices,
	long *count,
	real_point3d const *center,
	real inner,
	real outer,
	unsigned long color)
{
	short side;

	for (side = 0; side < COMMAND_RING_SIDES && *count + 6 <= COMMAND_RING_VERTICES; side++)
	{
		real angle0 = (real)side * (real)(2.0 * 3.14159265358979) / (real)COMMAND_RING_SIDES;
		real angle1 = (real)(side + 1) * (real)(2.0 * 3.14159265358979) / (real)COMMAND_RING_SIDES;
		real_point3d corners[4];
		short corner;
		static short const order[6] = { 0, 2, 3, 0, 3, 1 };

		for (corner = 0; corner < 4; corner++)
		{
			real angle = corner & 1 ? angle1 : angle0;
			real radius = corner & 2 ? outer : inner;

			corners[corner] = *center;
			corners[corner].x += (real)cos(angle) * radius;
			corners[corner].y += (real)sin(angle) * radius;
			corners[corner].z = command_ring_height(center, &corners[corner], radius) + COMMAND_RING_LIFT;
		}
		for (corner = 0; corner < 6; corner++)
		{
			vertices[*count].point = corners[order[corner]];
			vertices[(*count)++].color = color;
		}
	}
}

/* the triangles drawn, blended, as Forge's markers are */
static void command_render_vertices(
	struct command_ring_vertex const *vertices,
	long count)
{
	long buffer;

	if (count < 3)
		return;
	buffer = rasterizer_dynamic_vertices_new(_rasterizer_vertex_type_debug, count);
	if (buffer == NONE)
		return;
	{
		void *locked;

		rasterizer_globals.current_lock_operation = _rasterizer_lock_debug;
		locked = rasterizer_dynamic_vertices_lock(buffer);
		if (locked)
		{
			csmemcpy(locked, vertices, (size_t)count * sizeof(vertices[0]));
			rasterizer_dynamic_vertices_unlock(buffer);
			rasterizer_debug_drawing_begin(FALSE);
			rasterizer_draw_dynamic_vertices(0, count / 3, buffer, 3);
			rasterizer_debug_drawing_end();
		}
		rasterizer_globals.current_lock_operation = _rasterizer_lock_none;
	}
	rasterizer_dynamic_vertices_delete(buffer);
}

/* a team-coloured marker over each living AI of his side (the commander's
red), a small point-down triangle facing the view over the head, as the
multiplayer game marks teammates; within COMMAND_TEAMMATE_RANGE */
#define COMMAND_TEAMMATE_RANGE 60.f
#define COMMAND_TEAMMATE_HEIGHT 0.78f
static void command_render_teammates(
	struct command_ring_vertex *vertices,
	long *count)
{
	short squad_index, member;
	real_vector3d right, up;
	unsigned long color = command_color(0.95f, 0.36f, 0.32f, 0.85f);

	/* (the view's right and up, from its forward) */
	right.i = render.camera.forward.j;
	right.j = -render.camera.forward.i;
	right.k = 0.f;
	if (normalize3d(&right) == 0.f)
	{
		right.i = 1.f;
		right.j = 0.f;
	}
	cross_product3d(&right, &render.camera.forward, &up);
	if (up.k < 0.f)
	{
		up.i = -up.i;
		up.j = -up.j;
		up.k = -up.k;
	}
	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad const *squad = &command_globals.squads[squad_index];

		if (!squad->used || squad->side != command_globals.side || squad->airborne)
			continue;
		for (member = 0; member < COMMAND_SQUAD_SIZE && *count + 3 <= COMMAND_RING_VERTICES; member++)
		{
			long unit_index = squad->units[member];
			real_point3d head, corners[3];
			real size;
			short corner;

			if (unit_index == command_globals.possessed_unit_index || !command_living_unit(unit_index))
				continue;
			object_get_origin(unit_index, &head);
			if (distance3d(&head, &render.camera.position) > COMMAND_TEAMMATE_RANGE)
				continue;
			/* (over the head: a standing marine's, a Spartan's, are 0.7 wu) */
			head.z += COMMAND_TEAMMATE_HEIGHT;
			size = 0.03f + distance3d(&head, &render.camera.position) * 0.011f;
			for (corner = 0; corner < 3; corner++)
				corners[corner] = head;
			corners[0].x -= right.i * size;
			corners[0].y -= right.j * size;
			corners[1].x += right.i * size;
			corners[1].y += right.j * size;
			corners[2].x -= up.i * size * 1.2f;
			corners[2].y -= up.j * size * 1.2f;
			corners[2].z -= up.k * size * 1.2f;
			for (corner = 0; corner < 3; corner++)
			{
				vertices[*count].point = corners[corner];
				vertices[(*count)++].color = color;
			}
		}
	}
}

void command_render_world(
	void)
{
	static struct command_ring_vertex vertices[COMMAND_RING_VERTICES];
	long count = 0;
	short squad_index;

	if (!command_globals.active)
		return;
	/* (in his Spartan: a marker over each of his side's AI, as the game's
	teammates are marked) */
	if (command_globals.possessed_unit_index != NONE)
	{
		command_render_teammates(vertices, &count);
		command_render_vertices(vertices, count);
		return;
	}
	if (!command_globals.camera_placed)
		return;
	/* (the view drawn, kept for the pointer's picks between frames: the
	render's camera is another outside its drawing) */
	command_globals.view_camera = render.camera;
	command_globals.view_frustum = render.frustum;
	command_globals.view_valid = TRUE;
	for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
	{
		struct command_squad const *squad = &command_globals.squads[squad_index];
		boolean mine = squad->used && squad->side == command_globals.side;
		boolean chosen = mine && command_squad_is_selected(squad);
		unsigned long color = !mine ? command_color(0.95f, 0.30f, 0.25f, 0.55f) :
			chosen ? command_color(0.55f, 1.f, 0.65f, 0.95f) : command_color(0.45f, 0.75f, 1.f, 0.5f);
		short index;

		/* (none under a squad in the air: its ship carries its number) */
		if (!squad->used || squad->airborne)
			continue;
		for (index = 0; index < COMMAND_SQUAD_SIZE; index++)
		{
			real_point3d origin;

			if (!command_living_unit(squad->units[index]))
				continue;
			object_get_origin(squad->units[index], &origin);
			command_ring(vertices, &count, &origin, chosen ? 0.55f : 0.6f, 0.75f, color);
		}
		/* (a chosen squad's order's point: a small ring) */
		if (chosen && (squad->order == _command_order_move || squad->order == _command_order_attack))
		{
			command_ring(vertices, &count, &squad->point, 0.9f, 1.1f, squad->order == _command_order_attack ?
				command_color(1.f, 0.45f, 0.3f, 0.85f) : command_color(0.55f, 1.f, 0.65f, 0.85f));
		}
	}
	/* (the supply territories: held by red blue-green, by blue red, by no one
	white) */
	if (command_globals.game_running)
	{
		short index;

		for (index = 0; index < command_territory_count; index++)
		{
			struct command_territory const *territory = &command_territories[index];
			unsigned long color = territory->owner == command_globals.side ? command_color(0.45f, 0.9f, 1.f, 0.6f) :
				territory->owner != NONE ? command_color(0.95f, 0.30f, 0.25f, 0.6f) :
				command_color(1.f, 1.f, 1.f, 0.45f);

			command_ring(vertices, &count, &territory->center, territory->radius - 0.3f, territory->radius, color);
			/* (an objective's: a second ring inside) */
			if (territory->objective)
				command_ring(vertices, &count, &territory->center, territory->radius - 0.9f, territory->radius - 0.6f, color);
		}
	}
	/* (the cursor, and the sweep's circle about it) */
	if (command_globals.cursor_valid && !command_globals.pointer_valid)
	{
		command_ring(vertices, &count, &command_globals.cursor, 1.0f, 1.25f, command_color(1.f, 1.f, 1.f, 0.85f));
		command_ring(vertices, &count, &command_globals.cursor, 0.f, 0.18f, command_color(1.f, 1.f, 1.f, 0.85f));
	}
	if (command_globals.sweep_radius > 0.f && command_globals.cursor_valid)
	{
		command_ring(vertices, &count, &command_globals.cursor, command_globals.sweep_radius - 0.2f, command_globals.sweep_radius,
			command_color(0.55f, 1.f, 0.65f, 0.8f));
		command_ring(vertices, &count, &command_globals.cursor, 0.f, command_globals.sweep_radius - 0.2f,
			command_color(0.55f, 1.f, 0.65f, 0.12f));
	}
	command_render_vertices(vertices, count);
}

static void command_draw_line(
	long font_index,
	rectangle2d const *bounds,
	short justification,
	real red,
	real green,
	real blue,
	char const *line)
{
	real_argb_color color;
	unsigned short text[COMMAND_LINE_SIZE];

	color.alpha = 1.f;
	color.red = red;
	color.green = green;
	color.blue = blue;
	draw_string_set_draw_mode(font_index, NONE, justification, 0, &color);
	halo_menus_utf16(line, text, NUMBEROF(text));
	rasterizer_draw_unicode_string(bounds, NULL, NULL, 0, (wchar_t const *)text);
}

/* ---- the HUD (the owner's step 6): drawn in the look of the Halo 3 menus
(the halo3 folder's XML: their panels, their orange row, their rules and their team
bands, their fonts, their button icons), laid out in the menus' units (a
640x480 screen's, as tall as the view: wider views have more across) and
kept clear of the game's own HUD messages (the pickups' and the respawn's
words, at the left under the top and in the middle):
- top left, the score: COMMAND and the time left, each side's band filled
  to its score of the limit, the objective territories' letters in their
  holders' colours and the flags;
- top right, the resources and the supply points held (and the income);
- at the foot, a card for each of the commander's squads (the Spartan
  first): its number, its kind, a pip for each member alive, its order -
  the squads chosen lit as the menus' rows are -, and under them the
  controls, each with its button's icon (the player's: the Vita's, the
  pad's, the keyboard's and mouse's);
- the HQ as a menu on the right: its title, the resources, a row for each
  squad and upgrade (its price, or why it can't be bought, an upgrade's
  build shown filling), the row chosen lit, its buttons at its foot;
- notices under the top's middle; the result at the game's end.
In first person, his Spartan's view: the game's own HUD, the score at the
top's middle, and a team-coloured marker over each of his side's AI
(command_render_world). */

/* (menu_tags.c, hud_draw.c) */
boolean menu_tags_png_texture(char const *png, long width, long height, long smallest, struct bitmap_data *bitmap);

/* (rasterizer_text.c's: a font's letter, its width) */
struct command_font_character
{
	word character;
	short character_width;
};

enum
{
	_command_art_panel = 0,
	_command_art_item_on,
	_command_art_rule,
	_command_art_strip,
	_command_art_band_red,
	_command_art_band_blue,
	NUMBER_OF_COMMAND_ART
};

static struct
{
	char const *png;
	short width;
	short height;
} const command_art_files[NUMBER_OF_COMMAND_ART] =
{
	{ "halo3/panel.png", 64, 64 },
	{ "halo3/item_on.png", 128, 16 },
	{ "halo3/rule.png", 128, 2 },
	{ "halo3/strip.png", 64, 16 },
	{ "halo3/scoreboard_band_0.png", 128, 16 },
	{ "halo3/scoreboard_band_1.png", 128, 16 },
};

static struct
{
	/* 0 not tried, 1 made, -1 not to be had */
	short state[NUMBER_OF_COMMAND_ART];
	struct bitmap_data bitmaps[NUMBER_OF_COMMAND_ART];
} command_art;

/* the HUD's frame: the view's window, a menu unit's pixels, the fonts */
static struct
{
	rectangle2d window;
	real unit;
	real width;
	long font_item;
	long font_small;
	long font_title;
	short height_item;
	short height_small;
	short height_title;
	/* the panels' rectangles this frame (the menus' units), which the world's
	words over units, flags and territories keep out of */
	short panel_count;
	real panels[8][4];
} command_hud;

/* the menus' button kinds the HUD names */
enum
{
	_command_input_combo = 0,
	_command_input_choose,
	_command_input_order,
	_command_input_hold,
	_command_input_hq,
	_command_input_all,
	_command_input_base,
	_command_input_buy,
	_command_input_close,
	_command_input_rows,
	NUMBER_OF_COMMAND_INPUTS
};

/* (the menus' colours: command_hud.h's) */

static struct bitmap_data *command_art_get(
	short art)
{
	if (!command_art.state[art])
	{
		command_art.state[art] = menu_tags_png_texture(command_art_files[art].png, command_art_files[art].width,
			command_art_files[art].height, command_art_files[art].width, &command_art.bitmaps[art]) ? 1 : -1;
		if (command_art.state[art] < 0)
			platform_log("command: HUD: %s could not be read", command_art_files[art].png);
	}
	return command_art.state[art] > 0 ? &command_art.bitmaps[art] : NULL;
}

/* the game's white (the virtual keyboard's caret, as the button icons
draw their strokes) */
static struct bitmap_data *command_white(
	void)
{
	long tag_index = tag_loaded(BITMAP_GROUP_TAG, "ui\\shell\\bitmaps\\white");
	struct bitmap_data *white;

	if (tag_index == NONE)
		return NULL;
	white = bitmap_group_get_bitmap_from_sequence(tag_index, 0, 0);
	if (!white || !_texture_cache_bitmap_get_hardware_format(white, FALSE, TRUE))
		return NULL;
	return white;
}

static pixel32 command_argb(
	unsigned long color,
	real alpha)
{
	unsigned long a = (unsigned long)(PIN(alpha, 0.f, 1.f) * (real)(color >> 24) + 0.5f);

	return (pixel32)((a << 24) | (color & 0x00FFFFFFUL));
}

/* a panel's rectangle (the menus' units): kept clear of the world's words */
static void command_hud_panel(
	real x0,
	real y0,
	real x1,
	real y1)
{
	if (command_hud.panel_count >= (short)NUMBEROF(command_hud.panels))
		return;
	command_hud.panels[command_hud.panel_count][0] = x0;
	command_hud.panels[command_hud.panel_count][1] = y0;
	command_hud.panels[command_hud.panel_count][2] = x1;
	command_hud.panels[command_hud.panel_count][3] = y1;
	command_hud.panel_count++;
}

/* whether a rectangle of the screen's pixels meets a panel */
static boolean command_hud_over_panel(
	rectangle2d const *bounds)
{
	short index;

	for (index = 0; index < command_hud.panel_count; index++)
	{
		real x0 = (real)command_hud.window.x0 + command_hud.panels[index][0] * command_hud.unit;
		real y0 = (real)command_hud.window.y0 + command_hud.panels[index][1] * command_hud.unit;
		real x1 = (real)command_hud.window.x0 + command_hud.panels[index][2] * command_hud.unit;
		real y1 = (real)command_hud.window.y0 + command_hud.panels[index][3] * command_hud.unit;

		if ((real)bounds->x1 > x0 && (real)bounds->x0 < x1 && (real)bounds->y1 > y0 && (real)bounds->y0 < y1)
			return TRUE;
	}
	return FALSE;
}

/* a picture over a rectangle of the menus' units */
static void command_hud_quad(
	struct bitmap_data *bitmap,
	real x0,
	real y0,
	real x1,
	real y1,
	pixel32 color)
{
	struct dynamic_screen_vertex vertices[4];
	struct rasterizer_dynamic_screen_geometry_parameters parameters;
	short k;

	if (!bitmap || x1 <= x0 || y1 <= y0)
		return;
	for (k = 0; k < 4; k++)
	{
		boolean right = ((k + 1) & 2) != 0, bottom = k > 1;

		vertices[k].position.x = (real)command_hud.window.x0 + (right ? x1 : x0) * command_hud.unit;
		vertices[k].position.y = (real)command_hud.window.y0 + (bottom ? y1 : y0) * command_hud.unit;
		vertices[k].texture_coordinates.x = right ? 1.f : 0.f;
		vertices[k].texture_coordinates.y = bottom ? 1.f : 0.f;
		vertices[k].color = color;
	}
	csmemset(&parameters, 0, sizeof(parameters));
	parameters.map_texture_scale[0].i = parameters.map_texture_scale[0].j = 1.f;
	parameters.map_scale[0].i = parameters.map_scale[0].j = 1.f;
	parameters.framebuffer_blend_function = 0;
	parameters.map[0] = bitmap;
	rasterizer_psuedo_dynamic_screen_quad_draw(&parameters, vertices);
}

static void command_hud_art(
	short art,
	real x0,
	real y0,
	real x1,
	real y1,
	real alpha)
{
	command_hud_quad(command_art_get(art), x0, y0, x1, y1, command_argb(0xFFFFFFFFUL, alpha));
}

/* a flat colour (the game's white, tinted) */
static void command_hud_fill(
	real x0,
	real y0,
	real x1,
	real y1,
	unsigned long color)
{
	struct bitmap_data *white = command_white();

	if (white)
		command_hud_quad(white, x0, y0, x1, y1, (pixel32)color);
	else
		command_hud_quad(command_art_get(_command_art_strip), x0, y0, x1, y1, (pixel32)color);
}

/* words in a box of the menus' units: justification 0 left, 1 right, 2
the middle; the box's top is where the line's top is */
static void command_hud_text(
	long font_index,
	real x0,
	real y0,
	real x1,
	short justification,
	unsigned long color,
	char const *text)
{
	rectangle2d bounds;
	real_argb_color argb;
	unsigned short wide[COMMAND_LINE_SIZE];
	short height = font_index == command_hud.font_title ? command_hud.height_title :
		font_index == command_hud.font_small ? command_hud.height_small : command_hud.height_item;

	if (font_index == NONE || !text || !text[0])
		return;
	bounds.x0 = (short)(command_hud.window.x0 + x0 * command_hud.unit);
	bounds.x1 = (short)(command_hud.window.x0 + x1 * command_hud.unit);
	bounds.y0 = (short)(command_hud.window.y0 + y0 * command_hud.unit);
	bounds.y1 = (short)(bounds.y0 + height + 2);
	argb.alpha = (real)(color >> 24) / 255.f;
	argb.red = (real)((color >> 16) & 0xFF) / 255.f;
	argb.green = (real)((color >> 8) & 0xFF) / 255.f;
	argb.blue = (real)(color & 0xFF) / 255.f;
	draw_string_set_draw_mode(font_index, NONE, justification, 0, &argb);
	halo_menus_utf16(text, wide, NUMBEROF(wide));
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, (wchar_t const *)wide);
}

/* a text's width in the menus' units (as its font draws it) */
static real command_hud_text_width(
	long font_index,
	char const *text)
{
	struct font_header *font;
	real width = 0.f;
	unsigned char const *c;

	if (font_index == NONE || !text)
		return 0.f;
	font = font_definition_get(font_index);
	for (c = (unsigned char const *)text; *c; c++)
	{
		struct command_font_character *character;

		/* (UTF-8's continuation bytes: a letter's width once) */
		if ((*c & 0xC0) == 0x80)
			continue;
		character = (struct command_font_character *)font_get_character_by_ascii_code(font, (word)(*c < 0x80 ? *c : 'e'));
		if (character)
			width += character->character_width;
	}
	return width / command_hud.unit;
}

/* a font of the menus', the HUD's own if they are not loaded */
static long command_hud_font(
	char const *name,
	short *height)
{
	char path[64];
	long font_index;

	snprintf(path, sizeof(path), "pc\\h3\\%s", name);
	font_index = tag_loaded(FONT_GROUP_TAG, path);
	if (font_index == NONE)
		font_index = hud_get_font_index();
	if (font_index != NONE)
	{
		struct font_header *font = font_definition_get(font_index);

		*height = (short)(font->ascending_height + font->descending_height);
	}
	return font_index;
}

/* the glyphs a button kind is drawn with (two for the combo, the HQ's rows:
the D-pad's up and down), the player's: the Vita's, the pad's as its icons
show, the keyboard's and mouse's keys */
static short command_input_glyphs(
	short input,
	short *glyphs)
{
#ifndef HALO_BUTTON_GLYPHS
	return 0;
#elif defined(HALO_VITA)
	switch (input)
	{
	case _command_input_combo: glyphs[0] = VITA_GLYPH_SELECT; glyphs[1] = VITA_GLYPH_UP; return 2;
	case _command_input_choose: case _command_input_buy: glyphs[0] = VITA_GLYPH_CROSS; return 1;
	case _command_input_order: glyphs[0] = VITA_GLYPH_R; return 1;
	case _command_input_hold: glyphs[0] = VITA_GLYPH_SQUARE; return 1;
	case _command_input_hq: glyphs[0] = VITA_GLYPH_TRIANGLE; return 1;
	case _command_input_all: glyphs[0] = VITA_GLYPH_RIGHT; return 1;
	case _command_input_base: glyphs[0] = VITA_GLYPH_LEFT; return 1;
	case _command_input_close: glyphs[0] = VITA_GLYPH_CIRCLE; return 1;
	case _command_input_rows: glyphs[0] = VITA_GLYPH_UP; glyphs[1] = VITA_GLYPH_DOWN; return 2;
	}
	return 0;
#else
#ifdef COMMAND_BUTTON_ICONS
	if (halo_button_icons_shown() == _halo_button_icons_keyboard)
	{
		switch (input)
		{
		case _command_input_combo: glyphs[0] = HALO_GLYPH_KEY_FIRST + HALO_KEY_TAB; return 1;
		case _command_input_choose: glyphs[0] = HALO_GLYPH_KEY_FIRST + HALO_KEY_LMB; return 1;
		case _command_input_order: glyphs[0] = HALO_GLYPH_KEY_FIRST + HALO_KEY_RMB; return 1;
		case _command_input_hold: glyphs[0] = HALO_GLYPH_KEY_FIRST + HALO_KEY_H; return 1;
		case _command_input_hq: case _command_input_close: glyphs[0] = HALO_GLYPH_KEY_FIRST + HALO_KEY_F; return 1;
		case _command_input_all: glyphs[0] = HALO_GLYPH_KEY_FIRST + HALO_KEY_CTRL; glyphs[1] = HALO_GLYPH_KEY_FIRST + HALO_KEY_A; return 2;
		case _command_input_base: glyphs[0] = HALO_GLYPH_KEY_FIRST + HALO_KEY_B; return 1;
		case _command_input_buy: glyphs[0] = HALO_GLYPH_KEY_FIRST + HALO_KEY_N1; return 1;
		case _command_input_rows: glyphs[0] = HALO_GLYPH_KEY_FIRST + HALO_KEY_UP; glyphs[1] = HALO_GLYPH_KEY_FIRST + HALO_KEY_DOWN; return 2;
		}
		return 0;
	}
#endif
	switch (input)
	{
	case _command_input_combo:
		glyphs[0] = hud_vita_button_glyph(_gamepad_binary_button_back);
		glyphs[1] = hud_vita_button_glyph(_gamepad_binary_button_dpad_up);
		if (!glyphs[1])
			glyphs[1] = VITA_GLYPH_UP;
		return 2;
	case _command_input_choose: case _command_input_buy: glyphs[0] = hud_vita_button_glyph(_gamepad_analog_button_a); return 1;
	case _command_input_order: glyphs[0] = hud_vita_button_glyph(_gamepad_analog_button_right_trigger); return 1;
	case _command_input_hold: glyphs[0] = hud_vita_button_glyph(_gamepad_analog_button_x); return 1;
	case _command_input_hq: glyphs[0] = hud_vita_button_glyph(_gamepad_analog_button_y); return 1;
	case _command_input_all: glyphs[0] = hud_vita_button_glyph(_gamepad_analog_button_white); return 1;
	case _command_input_base: glyphs[0] = hud_vita_button_glyph(_gamepad_analog_button_black); return 1;
	case _command_input_close: glyphs[0] = hud_vita_button_glyph(_gamepad_analog_button_b); return 1;
	case _command_input_rows:
		glyphs[0] = hud_vita_button_glyph(_gamepad_binary_button_dpad_up);
		glyphs[1] = hud_vita_button_glyph(_gamepad_binary_button_dpad_down);
		/* (the D-pad has no icon of the pad's: the menus' round one) */
		if (!glyphs[0])
			glyphs[0] = VITA_GLYPH_UP;
		if (!glyphs[1])
			glyphs[1] = VITA_GLYPH_DOWN;
		return 2;
	}
	return 0;
#endif
}

/* a button's icons and its word, from x; where the next goes */
static real command_hud_hint(
	real x,
	real y,
	short input,
	char const *label)
{
	short glyphs[2];
	short count = command_input_glyphs(input, glyphs);
	short index;
	real size = 14.f;

	for (index = 0; index < count; index++)
	{
		real_rectangle2d rectangle;

		rectangle.x0 = (real)command_hud.window.x0 + x * command_hud.unit;
		rectangle.x1 = rectangle.x0 + size * command_hud.unit;
		rectangle.y0 = (real)command_hud.window.y0 + y * command_hud.unit;
		rectangle.y1 = rectangle.y0 + size * command_hud.unit;
#ifdef HALO_BUTTON_GLYPHS
		if (glyphs[index] != 0)
			hud_vita_glyph_draw_in_rect(glyphs[index], &rectangle, (pixel32)0xFFFFFFFFUL);
#endif
		x += size + 1.f;
	}
	x += 2.f;
	command_hud_text(command_hud.font_item, x, y - 1.f, x + 200.f, 0, COMMAND_HUD_LIGHT, label);
	return x + command_hud_text_width(command_hud.font_item, label) + 12.f;
}

/* a squad's kind, as the card names it */
static char const *command_squad_kind(
	struct command_squad const *squad)
{
	if (squad->hero)
		return T("SPARTAN");
	if (squad->crew)
		return T("SHADE");
	if (strstr(squad->name, "hunter"))
		return T("HUNTERS");
	if (strstr(squad->name, "elite"))
		return T("ELITES");
	if (strstr(squad->name, "jackal"))
		return T("JACKALS");
	if (strstr(squad->name, "grunt"))
		return T("GRUNTS");
	return T("MARINES");
}

/* ---- command_hud.h: the HUD's drawing for the other modes */

boolean command_hud_frame_begin(
	real *width)
{
	command_hud.window = render.camera.window_bounds;
	command_hud.unit = (real)(command_hud.window.y1 - command_hud.window.y0) / 480.f;
	if (command_hud.unit <= 0.f)
		return FALSE;
	command_hud.width = (real)(command_hud.window.x1 - command_hud.window.x0) / command_hud.unit;
	command_hud.font_item = command_hud_font("font_item", &command_hud.height_item);
	command_hud.font_small = command_hud_font("font_small", &command_hud.height_small);
	command_hud.font_title = command_hud_font("font_title", &command_hud.height_title);
	*width = command_hud.width;
	return TRUE;
}

static long command_hud_font_of(
	short font)
{
	return font == COMMAND_HUD_FONT_TITLE ? command_hud.font_title : font == COMMAND_HUD_FONT_SMALL ? command_hud.font_small :
		command_hud.font_item;
}

void command_hud_draw_art(
	short art,
	real x0,
	real y0,
	real x1,
	real y1,
	real alpha)
{
	if (art >= 0 && art < NUMBER_OF_COMMAND_ART)
		command_hud_art(art, x0, y0, x1, y1, alpha);
}

void command_hud_draw_fill(
	real x0,
	real y0,
	real x1,
	real y1,
	unsigned long color)
{
	command_hud_fill(x0, y0, x1, y1, color);
}

void command_hud_draw_text(
	short font,
	real x0,
	real y0,
	real x1,
	short justification,
	unsigned long color,
	char const *text)
{
	command_hud_text(command_hud_font_of(font), x0, y0, x1, justification, color, text);
}

real command_hud_measure_text(
	short font,
	char const *text)
{
	return command_hud_text_width(command_hud_font_of(font), text);
}

/* the score box (top left) */
static void command_hud_score(
	real right,
	real y)
{
	real width = 220.f;
	real x = right - width;
	char text[COMMAND_LINE_SIZE];
	long left = command_time_left();
	short side, index;

	command_hud_art(_command_art_strip, x, y, x + width, y + 18.f, 0.85f);
	command_hud_text(command_hud.font_item, x + 6.f, y + 2.f, x + width, 0, COMMAND_HUD_LIGHT, T(COMMAND_MODE_NAME));
	{
		char limit[32];

		snprintf(limit, sizeof(limit), T("TO %ld"), command_objectives.score_limit);
		if (left != NONE)
			snprintf(text, sizeof(text), "%s   %ld:%02ld", limit, left / (TICKS_PER_SECOND * 60), (left / TICKS_PER_SECOND) % 60);
		else
			snprintf(text, sizeof(text), "%s", limit);
		command_hud_text(command_hud.font_item, x, y + 2.f, x + width - 6.f, 1, COMMAND_HUD_DIM, text);
	}
	y += 19.f;
	for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
	{
		real fill = command_objectives.score_limit > 0 ?
			(real)PIN(command_objectives.scores[side], 0, command_objectives.score_limit) / (real)command_objectives.score_limit : 0.f;

		command_hud_art(_command_art_panel, x, y, x + width, y + 16.f, 0.8f);
		command_hud_art(side == _command_side_red ? _command_art_band_red : _command_art_band_blue, x, y,
			x + MAX(4.f, width * fill), y + 16.f, 0.95f);
		command_hud_text(command_hud.font_item, x + 6.f, y + 1.f, x + width, 0, COMMAND_HUD_LIGHT,
			side == _command_side_red ? T("RED TEAM") : T("BLUE TEAM"));
		snprintf(text, sizeof(text), "%ld", command_objectives.scores[side]);
		command_hud_text(command_hud.font_item, x, y + 1.f, x + width - 6.f, 1, COMMAND_HUD_LIGHT, text);
		y += 17.f;
	}
	/* (the objectives: each territory's letter in its holder's colour; the
	flags) */
	command_hud_art(_command_art_strip, x, y, x + width, y + 16.f, 0.85f);
	{
		real cx = x + 6.f;

		char next;

		for (next = 'A'; next < 'A' + COMMAND_TERRITORIES; next++)
		for (index = 0; index < command_territory_count; index++)
		{
			struct command_territory const *territory = &command_territories[index];
			char letter[2] = { territory->letter, 0 };

			if (!territory->objective || territory->letter != next)
				continue;
			if (territory->owner != NONE)
				command_hud_art(territory->owner == _command_side_red ? _command_art_band_red : _command_art_band_blue,
					cx, y + 2.f, cx + 14.f, y + 14.f, 0.95f);
			else
				command_hud_fill(cx, y + 2.f, cx + 14.f, y + 14.f, 0x60A0B4CDUL);
			command_hud_text(command_hud.font_small, cx, y + 2.f, cx + 14.f, 2, COMMAND_HUD_LIGHT, letter);
			cx += 17.f;
		}
		if (command_objectives.objectives & _command_objectives_flag)
		{
			/* (each side's flag: a band of its colour, at home dim, its state's
			word) */
			for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
			{
				struct command_flag const *flag = &command_objectives.flags[side];
				char const *state = !flag->away ? T("HOME") : flag->carrier_index != NONE ? T("TAKEN") : T("DROPPED");
				real end = side == _command_side_red ? x + width - 62.f : x + width - 2.f;
				real start = end - 58.f;

				if (start < cx)
					start = cx;
				command_hud_art(side == _command_side_red ? _command_art_band_red : _command_art_band_blue, start, y + 2.f, end,
					y + 14.f, flag->away ? 0.95f : 0.35f);
				command_hud_text(command_hud.font_small, start, y + 2.f, end, 2, COMMAND_HUD_LIGHT, state);
			}
		}
	}
}

/* the resources box (top right) */
static void command_hud_resources(
	real x,
	real y)
{
	real width = 200.f;
	real right = x + width;
	char text[COMMAND_LINE_SIZE];
	short index, held = 0;
	long income = COMMAND_TRICKLE * (command_upgrade_built(command_globals.side, _command_upgrade_supply) ? 2 : 1);

	for (index = 0; index < command_territory_count; index++)
	{
		if (command_territories[index].owner == command_globals.side)
		{
			held++;
			income += COMMAND_TERRITORY_INCOME;
		}
	}
	command_hud_art(_command_art_strip, x, y, right, y + 18.f, 0.85f);
	command_hud_text(command_hud.font_item, x + 6.f, y + 2.f, right, 0, COMMAND_HUD_ITEM, T("RESOURCES"));
	snprintf(text, sizeof(text), "%ld", command_globals.resources[command_globals.side]);
	command_hud_text(command_hud.font_item, x, y + 2.f, right - 6.f, 1, COMMAND_HUD_ORANGE, text);
	y += 19.f;
	command_hud_art(_command_art_panel, x, y, right, y + 16.f, 0.8f);
	snprintf(text, sizeof(text), T("SUPPLY %d/%d"), held, command_territory_count);
	command_hud_text(command_hud.font_small, x + 6.f, y + 2.f, right, 0, COMMAND_HUD_ITEM, text);
	snprintf(text, sizeof(text), T("+%ld EVERY %d S"), income, COMMAND_INCOME_SECONDS);
	command_hud_text(command_hud.font_small, x, y + 2.f, right - 6.f, 1, COMMAND_HUD_DIM, text);
}

/* the HQ as a menu (right) */
static void command_hud_hq(
	real right,
	real y)
{
	real width = 270.f;
	real x = right - width;
	real row_height = 17.f;
	real height = 30.f + row_height * COMMAND_BUYS + 44.f;
	char text[COMMAND_LINE_SIZE];
	short row;
	real hint_x;

	command_hud_art(_command_art_panel, x, y, right, y + height, 0.92f);
	command_hud_text(command_hud.font_title, x + 8.f, y + 4.f, right, 0, COMMAND_HUD_LIGHT, T("HQ"));
	snprintf(text, sizeof(text), T("RESOURCES %ld"), command_globals.resources[command_globals.side]);
	command_hud_text(command_hud.font_item, x, y + 7.f, right - 8.f, 1, COMMAND_HUD_ORANGE, text);
	command_hud_art(_command_art_rule, x + 6.f, y + 27.f, right - 6.f, y + 29.f, 1.f);
	y += 30.f;
	for (row = 0; row < COMMAND_BUYS; row++)
	{
		struct command_buy const *buy = &command_buys[row];
		char const *blocked = command_buy_blocked(command_globals.side, row);
		boolean building = buy->kind == _command_buy_upgrade && command_upgrade_done_time[command_globals.side][buy->upgrade] &&
			!command_upgrades[command_globals.side][buy->upgrade];
		boolean built = buy->kind == _command_buy_upgrade && command_upgrades[command_globals.side][buy->upgrade];
		boolean chosen = row == command_globals.hq_row;
		unsigned long color = chosen ? COMMAND_HUD_LIGHT : blocked && !built ? COMMAND_HUD_GREY : COMMAND_HUD_ITEM;

		if (chosen)
			command_hud_art(_command_art_item_on, x + 4.f, y, right - 4.f, y + row_height - 1.f, 1.f);
		command_hud_text(command_hud.font_item, x + 10.f, y + 1.f, right, 0, color, T(buy->name));
		if (building)
		{
			long done = command_upgrade_done_time[command_globals.side][buy->upgrade];
			real share = buy->seconds > 0 ? 1.f - (real)(done - game_time_get()) / (real)(buy->seconds * TICKS_PER_SECOND) : 1.f;
			real bar_x0 = right - 74.f, bar_x1 = right - 10.f;

			snprintf(text, sizeof(text), "%ld s", (done - game_time_get()) / TICKS_PER_SECOND + 1);
			command_hud_text(command_hud.font_small, x, y + 2.f, bar_x0 - 4.f, 1, COMMAND_HUD_ORANGE, text);
			command_hud_fill(bar_x0, y + 5.f, bar_x1, y + 11.f, 0x80202838UL);
			command_hud_fill(bar_x0, y + 5.f, bar_x0 + (bar_x1 - bar_x0) * PIN(share, 0.f, 1.f), y + 11.f, 0xFFF0B070UL);
		}
		else if (built)
			command_hud_text(command_hud.font_small, x, y + 2.f, right - 10.f, 1, COMMAND_HUD_DIM, T("BUILT"));
		else if (buy->cost > 0)
		{
			snprintf(text, sizeof(text), "%d", buy->cost);
			command_hud_text(command_hud.font_item, x, y + 1.f, right - 10.f, 1,
				blocked && !strcmp(blocked, N_("Can't afford")) ? COMMAND_HUD_RED : blocked ? COMMAND_HUD_GREY :
				chosen ? COMMAND_HUD_LIGHT : COMMAND_HUD_ORANGE, text);
		}
		y += row_height;
	}
	/* (the row chosen: why it can't be bought) */
	command_hud_art(_command_art_rule, x + 6.f, y + 2.f, right - 6.f, y + 4.f, 1.f);
	if (command_globals.hq_row >= 0 && command_globals.hq_row < COMMAND_BUYS)
	{
		char const *blocked = command_buy_blocked(command_globals.side, command_globals.hq_row);
		struct command_buy const *buy = &command_buys[command_globals.hq_row];

		if (buy->kind == _command_buy_upgrade && command_upgrades[command_globals.side][buy->upgrade])
			blocked = NULL;
		if (blocked)
			command_hud_text(command_hud.font_small, x + 10.f, y + 6.f, right - 8.f, 0, COMMAND_HUD_RED, T(blocked));
		else if (buy->kind == _command_buy_squad && buy->seconds > 0)
		{
			snprintf(text, sizeof(text), T("Leaves %d s after the order"), buy->seconds);
			command_hud_text(command_hud.font_small, x + 10.f, y + 6.f, right - 8.f, 0, COMMAND_HUD_DIM, text);
		}
		else if (buy->kind == _command_buy_upgrade && !command_upgrade_done_time[command_globals.side][buy->upgrade])
		{
			snprintf(text, sizeof(text), T("Built in %d s"), buy->seconds);
			command_hud_text(command_hud.font_small, x + 10.f, y + 6.f, right - 8.f, 0, COMMAND_HUD_DIM, text);
		}
	}
	y += 20.f;
	hint_x = x + 8.f;
	hint_x = command_hud_hint(hint_x, y + 7.f, _command_input_buy, T("Buy"));
	hint_x = command_hud_hint(hint_x, y + 7.f, _command_input_rows, T("Choose"));
	command_hud_hint(hint_x, y + 7.f, _command_input_close, T("Close"));
}

/* a squad's card at the foot; how wide */
static real command_hud_card(
	struct command_squad const *squad,
	real x,
	real y)
{
	real width = 86.f, height = 40.f;
	boolean chosen = command_squad_is_selected(squad);
	char text[COMMAND_LINE_SIZE];
	short member, members = 0, living = command_squad_living(squad);

	command_hud_art(_command_art_panel, x, y, x + width, y + height, 0.85f);
	if (chosen)
		command_hud_art(_command_art_item_on, x, y, x + width, y + 15.f, 1.f);
	if (squad->hero)
		snprintf(text, sizeof(text), "%s", command_squad_kind(squad));
	else
		snprintf(text, sizeof(text), "%d %s", squad->number, command_squad_kind(squad));
	command_hud_text(command_hud.font_small, x + 4.f, y + 1.f, x + width, 0, chosen ? COMMAND_HUD_LIGHT : COMMAND_HUD_ITEM, text);
	for (member = 0; member < COMMAND_SQUAD_SIZE; member++)
		members += squad->units[member] != NONE || squad->airborne;
	if (squad->hero)
		members = 1;
	/* (a pip for each member: alive lit) */
	for (member = 0; member < MIN(members, COMMAND_SQUAD_SIZE); member++)
	{
		real px = x + 5.f + member * 9.f;

		command_hud_fill(px, y + 18.f, px + 7.f, y + 24.f, member < living ? 0xFFA8E07CUL : 0x80404858UL);
	}
	if (squad->hero && !living && command_globals.game_running && command_globals.resources[command_globals.side] < COMMAND_SPARTAN_COST)
		snprintf(text, sizeof(text), T("%d TO RESPAWN"), COMMAND_SPARTAN_COST);
	else if (squad->hero && command_globals.possessed_unit_index != NONE)
		snprintf(text, sizeof(text), "%s", T("YOU"));
	else if (!living && squad->hero)
		snprintf(text, sizeof(text), "%s", T("RESPAWNING"));
	else if (squad->airborne)
		snprintf(text, sizeof(text), "%s", T("INBOUND"));
	else
		snprintf(text, sizeof(text), "%s", T(command_order_names[squad->order]));
	command_hud_text(command_hud.font_small, x + 4.f, y + 26.f, x + width, 0,
		squad->hero && !living && command_globals.resources[command_globals.side] < COMMAND_SPARTAN_COST ? COMMAND_HUD_RED :
		COMMAND_HUD_DIM, text);
	return width + 4.f;
}

/* a word over a point of the world, where the view shows it */
static void command_draw_marker(
	long font_index,
	short height,
	real_point3d const *position,
	char const *text,
	real red,
	real green,
	real blue)
{
	real_point2d screen;
	real_vector3d to;
	rectangle2d bounds;

	to.i = position->x - render.camera.position.x;
	to.j = position->y - render.camera.position.y;
	to.k = position->z - render.camera.position.z;
	if (dot_product3d(&to, &render.camera.forward) <= 0.1f ||
		!render_camera_world_to_screen(&render.camera, &render.frustum, (real_point3d *)position, &screen))
	{
		return;
	}
	bounds.x0 = (short)(screen.x - render.camera.viewport_bounds.x0 - 80);
	bounds.x1 = (short)(bounds.x0 + 160);
	bounds.y1 = (short)(screen.y - render.camera.viewport_bounds.y0);
	bounds.y0 = (short)(bounds.y1 - height);
	/* (a word under one of the HUD's panels is not drawn: the panel's own
	words stay clear; the words' own width, not the box's) */
	{
		rectangle2d words = bounds;
		real half = command_hud.unit > 0.f ? command_hud_text_width(font_index, text) * command_hud.unit * 0.5f + 2.f : 80.f;
		short middle = (short)((bounds.x0 + bounds.x1) / 2);

		words.x0 = (short)(middle - half);
		words.x1 = (short)(middle + half);
		if (command_hud_over_panel(&words))
			return;
	}
	command_draw_line(font_index, &bounds, 2, red, green, blue, text);
}

void command_draw(
	short local_player_index)
{
	long font_index;
	short height;
	real right, foot;
	char line[COMMAND_LINE_SIZE];
	struct command_squad *selected;

	/* (none under a menu that pauses the game: Forge's menu, the pause
	screen) */
	if (!command_globals.active || local_player_index != command_globals.local_player_index || game_time_get_paused())
		return;
	if (!command_hud_frame_begin(&right))
		return;
	font_index = command_hud.font_item;
	height = command_hud.height_item;
	if (font_index == NONE)
		return;
	right = command_hud.width - 12.f;
	foot = 480.f - 12.f;
	selected = command_squad_selected();
	/* (the game over: its result in the middle, until the scores) */
	if (command_globals.game_running && command_objectives.over)
	{
		char const *result = command_objectives.winner == COMMAND_DRAW ? T("DRAW") :
			command_objectives.winner == _command_side_red ? T("RED TEAM WINS") : T("BLUE TEAM WINS");

		command_hud_art(_command_art_strip, command_hud.width * 0.5f - 130.f, 170.f, command_hud.width * 0.5f + 130.f, 200.f, 0.85f);
		command_hud_text(command_hud.font_title, 0.f, 175.f, command_hud.width, 2, COMMAND_HUD_ORANGE, result);
		return;
	}
	/* (in his Spartan: the game's own HUD; the way back its first seconds,
	the score and the notice at the top's middle) */
	if (command_globals.possessed_unit_index != NONE)
	{
		real y = 10.f;

		if (system_milliseconds() < command_globals.possessed_hint_until)
		{
			real width = 0.f;
			short glyphs[2];
			short count = command_input_glyphs(_command_input_combo, glyphs);

			width = count * 15.f + 2.f + command_hud_text_width(command_hud.font_item, T("Back to command"));
			command_hud_hint(command_hud.width * 0.5f - width * 0.5f, y, _command_input_combo, T("Back to command"));
			y += 18.f;
		}
		if (command_globals.game_running)
		{
			snprintf(line, sizeof(line), T("RED %ld   BLUE %ld   TO %ld"), command_objectives.scores[0], command_objectives.scores[1],
				command_objectives.score_limit);
			command_hud_text(command_hud.font_item, 0.f, y, command_hud.width, 2, COMMAND_HUD_LIGHT, line);
			y += 16.f;
		}
		if (command_globals.notice[0] && system_milliseconds() < command_globals.notice_until)
			command_hud_text(command_hud.font_item, 0.f, y, command_hud.width, 2, COMMAND_HUD_ORANGE, command_globals.notice);
		return;
	}
	/* (the panels' places first: the world's words keep out of them) */
	command_hud.panel_count = 0;
	command_hud_panel(0.f, foot - 17.f, command_hud.width, 480.f);
	if (command_globals.game_running)
	{
		command_hud_panel(right - 220.f, 12.f, right, 12.f + 19.f + 17.f * 2.f + 16.f);
		command_hud_panel(12.f, 12.f, 212.f, 12.f + 19.f + 16.f);
		if (command_globals.hq_open)
			command_hud_panel(right - 270.f, 90.f, right, 90.f + 30.f + 17.f * COMMAND_BUYS + 44.f);
	}
	else
		command_hud_panel(12.f, 12.f, 202.f, 30.f);
	{
		short squad_index, cards = 0;
		real rows;

		for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
			cards += command_globals.squads[squad_index].used && command_globals.squads[squad_index].side == command_globals.side;
		rows = (real)(cards ? (cards * 90 + (short)(right - 12.f) - 1) / (short)(right - 12.f) : 0);
		if (cards)
			command_hud_panel(12.f, foot - 18.f - 42.f - 44.f * (rows - 1.f), 12.f + 90.f * MIN(cards, (short)((right - 12.f) / 90.f)),
				foot - 18.f);
	}
	if (command_globals.game_running)
	{
		command_hud_score(right, 12.f);
		command_hud_resources(12.f, 12.f);
	}
	else
	{
		command_hud_art(_command_art_strip, 12.f, 12.f, 202.f, 30.f, 0.85f);
		command_hud_text(command_hud.font_item, 18.f, 14.f, 202.f, 0, COMMAND_HUD_LIGHT, T(COMMAND_MODE_NAME));
	}
	/* the notice, under the top's middle */
	if (command_globals.notice[0] && system_milliseconds() < command_globals.notice_until)
		command_hud_text(command_hud.font_item, 0.f, 16.f, command_hud.width, 2, COMMAND_HUD_ORANGE, command_globals.notice);
	/* each squad's number over its first unit, the chosen one's orange, the
	enemy's red */
	{
		short squad_index;

		for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
		{
			struct command_squad *squad = &command_globals.squads[squad_index];
			long leader = squad->used ? command_squad_leader(squad) : NONE;
			real_point3d position;

			if (leader == NONE)
				continue;
			object_get_origin(leader, &position);
			position.z += 1.4f;
			if (squad->hero)
				snprintf(line, sizeof(line), "%s", T("Spartan"));
			else
				snprintf(line, sizeof(line), "%d", squad->number);
			if (squad->side != command_globals.side)
				command_draw_marker(font_index, height, &position, squad->hero ? line : "", 0.95f, 0.35f, 0.30f);
			else if (command_squad_is_selected(squad))
				command_draw_marker(font_index, height, &position, line, 0.95f, 0.62f, 0.30f);
			else
				command_draw_marker(font_index, height, &position, line, 0.47f, 0.84f, 1.f);
		}
	}
	/* the objective territories' letters, the flags where they are */
	if (command_globals.game_running)
	{
		short index;

		for (index = 0; index < command_territory_count; index++)
		{
			struct command_territory const *territory = &command_territories[index];
			real_point3d position = territory->center;
			char letter[2] = { territory->letter, 0 };

			if (!territory->objective)
				continue;
			position.z += 1.5f;
			command_draw_marker(command_hud.font_title, command_hud.height_title, &position, letter,
				territory->owner == _command_side_red ? 0.95f : territory->owner == _command_side_blue ? 0.42f : 0.95f,
				territory->owner == _command_side_red ? 0.38f : territory->owner == _command_side_blue ? 0.55f : 0.95f,
				territory->owner == _command_side_red ? 0.34f : territory->owner == _command_side_blue ? 0.94f : 0.95f);
		}
		for (index = 0; index < NUMBER_OF_COMMAND_SIDES; index++)
		{
			struct command_flag const *flag = &command_objectives.flags[index];
			real_point3d position;

			if (!flag->valid || !object_try_and_get(flag->weapon_index))
				continue;
			command_flag_position(flag, &position);
			position.z += flag->carrier_index != NONE ? 2.2f : 1.2f;
			command_draw_marker(font_index, height, &position, index == command_globals.side ? T("Your flag") : T("Enemy flag"),
				index == _command_side_red ? 0.95f : 0.42f, index == _command_side_red ? 0.38f : 0.55f,
				index == _command_side_red ? 0.34f : 0.94f);
		}
	}
	/* the order's point of the squad chosen */
	if (selected && selected->order != _command_order_none)
	{
		real_point3d position = selected->point;

		position.z += 0.3f;
		command_draw_marker(font_index, height / 2 + height, &position, "x", 0.95f, 0.62f, 0.30f);
	}
	/* (the pointer's drag: its box's corners) */
	if (command_globals.drag_down && command_globals.pointer_valid &&
		(fabs(command_globals.pointer_x - command_globals.drag_x) > COMMAND_DRAG_PIXELS ||
			fabs(command_globals.pointer_y - command_globals.drag_y) > COMMAND_DRAG_PIXELS))
	{
		real xs[2] = { command_globals.drag_x, command_globals.pointer_x };
		real ys[2] = { command_globals.drag_y, command_globals.pointer_y };
		short corner;

		for (corner = 0; corner < 4; corner++)
		{
			rectangle2d bounds;
			short x = (short)(xs[corner & 1] - render.camera.viewport_bounds.x0);
			short y = (short)(ys[corner >> 1] - render.camera.viewport_bounds.y0);

			bounds.x0 = (short)(x - 20);
			bounds.x1 = (short)(x + 20);
			bounds.y0 = (short)(y - height / 2);
			bounds.y1 = (short)(bounds.y0 + height);
			command_draw_line(font_index, &bounds, 2, 0.55f, 1.f, 0.65f, "+");
		}
	}
	/* the HQ, a menu on the right */
	if (command_globals.hq_open && command_globals.game_running)
		command_hud_hq(right, 90.f);
	/* the squads' cards at the foot, the Spartan's first; the controls
	under them */
	{
		real x = 12.f;
		real y = foot - 18.f - 42.f;
		short pass, squad_index;

		for (pass = 0; pass < 2; pass++)
		{
			for (squad_index = 0; squad_index < COMMAND_MAXIMUM_SQUADS; squad_index++)
			{
				struct command_squad const *squad = &command_globals.squads[squad_index];

				if (!squad->used || squad->side != command_globals.side || (pass == 0) != squad->hero)
					continue;
				if (x + 86.f > right)
				{
					x = 12.f;
					y -= 44.f;
				}
				x += command_hud_card(squad, x, y);
			}
		}
	}
	{
		real x = 12.f;
		real y = foot - 14.f;

		command_hud_art(_command_art_strip, 0.f, y - 3.f, command_hud.width, 480.f, 0.6f);
		x = command_hud_hint(x, y, _command_input_combo, T("Spartan"));
		x = command_hud_hint(x, y, _command_input_choose, T("Choose"));
		x = command_hud_hint(x, y, _command_input_order, T("Order"));
		x = command_hud_hint(x, y, _command_input_hold, T("Hold"));
		x = command_hud_hint(x, y, _command_input_all, T("All"));
		x = command_hud_hint(x, y, _command_input_base, T("Base"));
		if (command_globals.game_running)
			command_hud_hint(x, y, _command_input_hq, T("HQ"));
	}
}

/* ---------- the squads for the other game modes (command_squads.h) */

typedef char command_squads_members_match[COMMAND_SQUADS_MEMBERS == COMMAND_SQUAD_SIZE ? 1 : -1];

/* a handle's squad: its table index and its number (16 bits each) */
static long command_squad_handle(
	struct command_squad const *squad)
{
	return (long)((squad - command_globals.squads) | ((unsigned long)(unsigned short)squad->number << 16));
}

static struct command_squad *command_squad_of_handle(
	long handle)
{
	long index = handle & 0xFFFF;
	struct command_squad *squad;

	if (handle == NONE || index >= COMMAND_MAXIMUM_SQUADS)
		return NULL;
	squad = &command_globals.squads[index];
	if (!squad->used || !squad->hosted || (unsigned short)squad->number != (unsigned short)((unsigned long)handle >> 16))
		return NULL;
	return squad;
}

void command_squads_host(
	boolean host)
{
	short index;

	if (command_globals.squads_hosted == host)
		return;
	command_globals.squads_hosted = host;
	/* (no longer hosted: every hosted squad let go, its units left; a
	ship still flying one gone, with its riders) */
	if (!host)
	{
		for (index = 0; index < COMMAND_FLIGHTS; index++)
		{
			if (command_flights[index].used && command_flights[index].hosted)
			{
				object_delete(command_flights[index].vehicle_index);
				command_flights[index].used = FALSE;
			}
		}
		for (index = 0; index < COMMAND_MAXIMUM_SQUADS; index++)
		{
			if (command_globals.squads[index].used && command_globals.squads[index].hosted)
				command_globals.squads[index].used = FALSE;
		}
	}
	platform_log("command: squads %s by a game mode", host ? "hosted" : "no longer hosted");
}

boolean command_squads_hosted(
	void)
{
	return command_globals.squads_hosted;
}

long command_squad_spawn_team(
	short team,
	char const *composition,
	real_point3d const *at)
{
	struct command_squad *squad;

	if (!command_globals.squads_hosted || !composition || !at || command_globals.active)
		return NONE;
	command_spawn_team = team;
	squad = command_squad_spawn(_command_side_blue, 1, composition, at);
	command_spawn_team = NONE;
	if (!squad)
		return NONE;
	squad->hosted = TRUE;
	squad->team = team;
	return command_squad_handle(squad);
}

short command_squad_composition_missing(
	char const *composition,
	char *missing,
	int size)
{
	char text[256];
	char *part;
	char *next = NULL;
	short count = 0;

	if (size > 0)
		missing[0] = 0;
	if (!composition)
		return 0;
	snprintf(text, sizeof(text), "%s", composition);
	for (part = strtok_r(text, ",", &next); part; part = strtok_r(NULL, ",", &next))
	{
		char *colon = strchr(part, ':');

		if (colon)
			*colon = 0;
		while (*part == ' ')
			part++;
		if (!*part || command_actor_variant_find(part) != NONE)
			continue;
		if (size > 0)
		{
			size_t length = strlen(missing);

			snprintf(missing + length, (size_t)size - length, "%s%s", length ? ", " : "", part);
		}
		count++;
	}
	return count;
}

boolean command_squad_order_hosted(
	long handle,
	short order,
	real_point3d const *point,
	long target_unit_index)
{
	struct command_squad *squad = command_squad_of_handle(handle);
	real_point3d here;

	/* (none while it flies in: its orders once it is down) */
	if (!squad || squad->airborne)
		return FALSE;
	switch (order)
	{
	case _command_squad_hold:
		if (!point)
		{
			if (!command_squad_center(squad, &here))
				return FALSE;
			point = &here;
		}
		command_squad_order(squad, _command_order_hold, point, NONE, NONE);
		return TRUE;
	case _command_squad_move:
	case _command_squad_advance:
		if (!point)
			return FALSE;
		command_squad_order(squad, order == _command_squad_move ? _command_order_move : _command_order_advance, point, NONE, NONE);
		return TRUE;
	case _command_squad_attack:
		if (!command_living_unit(target_unit_index))
			return FALSE;
		object_get_origin(target_unit_index, &here);
		command_squad_order(squad, _command_order_attack, &here, NONE, target_unit_index);
		return TRUE;
	}
	return FALSE;
}

/* a drop point's way in: from far out toward yaw (where the ship comes
from), at the cruise over the highest ground under it, down over the landing
spot (on the ground under the point). The farthest start whose cruise clears
the ground under the way (under the level's sky); none, from the other side,
then from either flank. FALSE if the sky over the drop is too low or no way
clears the ground */
static boolean command_path_to_point(
	struct command_path *path,
	real_point3d const *at,
	real yaw)
{
	static real const turns[] = { 0.f, _pi, 0.5f * _pi, -0.5f * _pi };
	real_point3d ground;
	real highest, ceiling, cruise, back;
	short turn;

	csmemset(path, 0, sizeof(*path));
	if (!command_ground(at, &path->landing, NULL))
		return FALSE;
	if (command_ceiling(&path->landing) < path->landing.z + 2.f * COMMAND_PATH_APPROACH)
	{
		platform_log("command: path: no open sky over the drop at %.2f %.2f %.2f", path->landing.x, path->landing.y,
			path->landing.z);
		return FALSE;
	}
	path->muster = path->landing;
	for (turn = 0; turn < (short)NUMBEROF(turns); turn++)
	{
		real dx = (real)cos(yaw + turns[turn]), dy = (real)sin(yaw + turns[turn]);

		for (back = COMMAND_PATH_BACK + 30.f; back >= 30.f; back -= 10.f)
		{
			/* (the first point over the level's ground) */
			path->points[0] = path->landing;
			path->points[0].x += dx * back;
			path->points[0].y += dy * back;
			if (!command_ground_highest(&path->points[0], &ground))
				continue;
			path->points[1] = path->landing;
			path->points[1].x += dx * 20.f;
			path->points[1].y += dy * 20.f;
			path->points[2] = path->landing;
			command_path_cruise(path, &highest, &ceiling, &cruise);
			if (cruise < highest + 3.f)
				continue;
			platform_log("command: path: a drop at %.2f %.2f %.2f, in from %.2f %.2f at %.1f (the highest ground %.1f, the sky %.1f), "
				"%.0f wu%s", path->landing.x, path->landing.y, path->landing.z, path->points[0].x, path->points[0].y, cruise, highest,
				ceiling < 1e29f ? ceiling : -1.f, path->length, turn ? " (not from the side asked: its way does not clear the ground)" : "");
			return TRUE;
		}
	}
	platform_log("command: path: no way in to the drop at %.2f %.2f %.2f clears the ground under the sky", path->landing.x,
		path->landing.y, path->landing.z);
	csmemset(path, 0, sizeof(*path));
	return FALSE;
}

long command_squad_send_by_air(
	short team,
	char const *composition,
	real_point3d const *at,
	real yaw,
	short ship)
{
	struct command_path path;
	struct command_squad *squad;
	boolean flying = FALSE;

	if (!command_globals.squads_hosted || !composition || !at || command_globals.active || team == NONE)
		return NONE;
	if (!command_path_to_point(&path, at, yaw))
		return NONE;
	squad = command_flight_send_on(_command_side_blue, composition, &path, team,
		ship == COMMAND_SQUADS_SHIP_PELICAN ? _command_ship_pelican : ship == COMMAND_SQUADS_SHIP_DROPSHIP ? _command_ship_dropship :
		(short)NONE, &flying);
	return squad ? command_squad_handle(squad) : NONE;
}

boolean command_squad_airborne(
	long handle)
{
	struct command_squad *squad = command_squad_of_handle(handle);

	return squad && squad->airborne;
}

short command_squad_living_count(
	long handle)
{
	struct command_squad *squad = command_squad_of_handle(handle);

	return squad ? command_squad_living(squad) : 0;
}

short command_squad_units(
	long handle,
	long units[COMMAND_SQUADS_MEMBERS])
{
	struct command_squad *squad = command_squad_of_handle(handle);
	short index;
	short count = 0;

	for (index = 0; index < COMMAND_SQUADS_MEMBERS; index++)
	{
		units[index] = squad ? squad->units[index] : NONE;
		count += units[index] != NONE;
	}
	return count;
}

boolean command_squad_middle(
	long handle,
	real_point3d *middle)
{
	struct command_squad *squad = command_squad_of_handle(handle);

	return squad && command_squad_center(squad, middle);
}

void command_squad_release(
	long handle)
{
	struct command_squad *squad = command_squad_of_handle(handle);

	if (squad)
		squad->used = FALSE;
}

short command_squads_alive_on_team(
	short team)
{
	short index;
	short alive = 0;

	for (index = 0; index < COMMAND_MAXIMUM_SQUADS; index++)
	{
		struct command_squad const *squad = &command_globals.squads[index];

		if (squad->used && squad->hosted && (team == NONE || squad->team == team))
			alive += command_squad_living(squad);
	}
	return alive;
}

/* ---------- network play */

#include "command_net.inc"
