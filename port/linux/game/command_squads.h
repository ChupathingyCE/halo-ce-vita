/*
COMMAND_SQUADS.H

Command mode's AI squads (command.c) for the other game modes that use them
(forge_modes.c's FIREFIGHT and WARZONE: the design,
docs/superpowers/specs/2026-11-05-forge-warzone-firefight-design.md,
section 7): a squad of up to four campaign characters, each a unit with a
free actor kept active, on any engine team, ordered as Command's squads are
(MOVE, ATTACK, HOLD, ADVANCE: command_actor_orders steering the engine's
actors over the level's own path searches). A thin layer over command.c's
own table and code: no copy of it.

A mode hosts the squads (command_squads_host) while it runs: their orders
are then carried out with no commander above the map, and nothing of
Command's own game (its sides' lists, selection, HUD, economy, AI commander)
sees them. Nothing changes for Command mode or for Forge while no mode hosts
them, and a new map lets them go (command_map_loaded).

A squad is named by a handle (its table index and its number: one let go
and made again is another), NONE for none. The caller lets a squad go
(command_squad_release) when it no longer wants it: its units, dead or not,
are left as they are.

Threads: as command.c's. Every call on the main thread (forge_update's,
command_update's time), never from the tick.
*/

#ifndef __COMMAND_SQUADS_H
#define __COMMAND_SQUADS_H

#include "cseries.h"
#include "real_math.h"

/* ---------- constants */

enum
{
	/* the most members of a squad (command.c COMMAND_SQUAD_SIZE) */
	COMMAND_SQUADS_MEMBERS = 4,
};

/* a hosted squad's orders */
enum
{
	/* the actors' own behaviour where they are, walked back to their places */
	_command_squad_hold = 0,
	/* to the point, fighting on the way, then HOLD there */
	_command_squad_move,
	/* at a unit (seen by the squad at once), followed while it lives */
	_command_squad_attack,
	/* on to the point, each member fighting what it meets on the way */
	_command_squad_advance,
	NUMBER_OF_COMMAND_SQUAD_ORDERS
};

/* ---------- prototypes/COMMAND.C */

/* (a mode, as its game begins and ends) the squads hosted or not */
void command_squads_host(
	boolean host);

boolean command_squads_hosted(
	void);

/* a squad on an engine team (game.h _game_team_*: a multiplayer team's
index for a side's allies, an AI team for the hostile) at a point, its kinds
"name:count,name:count" (the first named leads; each the first actor variant
with that name in its tag's name: the map's, the packs' and the in-memory
build-out's); its handle, NONE if it could not be made (no such variant,
the table full, the squads not hosted) */
long command_squad_spawn_team(
	short team,
	char const *composition,
	real_point3d const *at);

/* (the layout editor's checks) the actor variants a composition names that
the loaded map (its packs and build-out counted) does not have: their names
in missing, comma separated; how many (0: it can be made) */
short command_squad_composition_missing(
	char const *composition,
	char *missing,
	int size);

/* a squad's ship (command_squad_send_by_air) */
enum
{
	/* (its species': marines the Pelican, the others the Covenant's dropship) */
	COMMAND_SQUADS_SHIP_SPECIES = 0,
	COMMAND_SQUADS_SHIP_PELICAN,
	COMMAND_SQUADS_SHIP_DROPSHIP,
};

/* a squad as command_squad_spawn_team makes it, brought in by air as
Command's waves are (its ship flown in from far out toward yaw - radians,
where the ship comes from -, down to a hover over the ground under at, the
squad out of its seats; shot down - its hull as Command's ships' -, its riders
die with it, the kills of the player who shot it last); its handle, NONE if
it could not fly (no open sky over at, every ship flying, ...: the caller
makes it on the ground). While it flies it takes no orders
(command_squad_airborne) */
long command_squad_send_by_air(
	short team,
	char const *composition,
	real_point3d const *at,
	real yaw,
	short ship);

boolean command_squad_airborne(
	long squad);

/* a hosted squad ordered (_command_squad_*): to the point (MOVE, ADVANCE,
HOLD there; NULL for HOLD where it stands), or at the unit (ATTACK) */
boolean command_squad_order_hosted(
	long squad,
	short order,
	real_point3d const *point,
	long target_unit_index);

/* a squad's living members (0 for a handle no longer good); its units
(NONE for an empty place) */
short command_squad_living_count(
	long squad);
short command_squad_units(
	long squad,
	long units[COMMAND_SQUADS_MEMBERS]);

/* the squad's living members' middle; FALSE with none */
boolean command_squad_middle(
	long squad,
	real_point3d *middle);

/* a squad let go: its place for a new one */
void command_squad_release(
	long squad);

/* the hosted squads' living members on an engine team (NONE: every team) */
short command_squads_alive_on_team(
	short team);

#endif
