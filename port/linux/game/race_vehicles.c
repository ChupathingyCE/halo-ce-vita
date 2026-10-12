/*
RACE_VEHICLES.C

A race's players start in a vehicle, on a dedicated server (sv_race_spawn_vehicle,
dedicated_server.c; on by default there, off on every other host).

The Xbox game gives a race its vehicles once, two ticks into the game: one
for each player in the game then, at the race vehicle flag nearest them
(game_engine_race.c, spawn_race_vehicles). A dedicated server's players come
into a game after it starts (a joiner loads, or first downloads, the map),
so they got none, and none was ever made again. With this on, each player's
unit is put in the driver's seat of a vehicle as it spawns, on the host: an
empty one within a few units of the spawn point (a map's own, or one left
there; not a turret), or else one made there of the race's type (the variant's vehicle
set, as spawn_race_vehicles chooses its first: the globals' multiplayer
Warthog, which a Custom Edition map makes its own race vehicle). The clients
are told of it as of any of the host's vehicles, and of the seat as of any
player's (network_objects.c, network_distributed.c): nothing of theirs
changes, so the betas see it too.

The vehicles made so are taken away once nobody rides them: at once if the
player they were made for is dead, has spawned again or has left, and after
ABANDONED_SECONDS otherwise; at the end of the game the game engine takes
every vehicle away. Nothing is made or taken on a client, nor outside the
race game engine (race, team race, rally).
*/

/* ---------- headers */

#include "cseries.h"
#include "math/real_math.h"
#include "tag_files/tag_groups.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_globals.h"
#include "game/players.h"
#include "networking/network_game_globals.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "units/vehicle_definitions.h"

/* ---------- constants */

enum
{
	/* the vehicles made for players the host keeps track of at once */
	MAXIMUM_SPAWNED_VEHICLES = 64,
	/* a vehicle made for a player, nobody riding it, is taken away after */
	ABANDONED_SECONDS = 10,
	/* (game_engine.c's vehicle sets, as the variant names them) */
	VEHICLE_SET_NONE = 1,
	VEHICLE_SET_GHOST = 3,
	VEHICLE_SET_TANK = 4,
	/* (vehicles.c's vehicle types: a turret, which stays where it is) */
	_vehicle_type_turret = 6,
};

/* an empty vehicle this near the spawn point is taken rather than another
made beside it (world units) */
#define REUSE_DISTANCE 3.0f

/* ---------- structures */

/* the vehicle tag's type, as network_objects.c reads it (its layout is not
the public header's) */
struct race_vehicle_definition
{
	byte __unknown0[0x2F4];
	short vehicle_type;
};

typedef char race_vehicles_vehicle_type_offset_assert[
	offsetof(struct race_vehicle_definition, vehicle_type) == 0x2F4 ? 1 : -1];

struct race_spawned_vehicle
{
	long vehicle_index;
	long player_index;
	long unit_index;
	long empty_since;
};

/* ---------- globals */

static boolean race_vehicles_enabled = FALSE;
static struct race_spawned_vehicle race_spawned_vehicles[MAXIMUM_SPAWNED_VEHICLES];
static long race_vehicles_count = 0;
static long race_vehicles_reported_definition = NONE - 1;

/* ---------- prototypes */

void platform_log(char const *format, ...);
boolean tag_index_is_group(long tag_index, long group_tag);

/* ---------- private code */

static boolean race_vehicles_active(
	void)
{
	return race_vehicles_enabled && game_engine_running() &&
		game_engine_get_variant()->game_engine_index == game_engine_race &&
		!network_game_distributed_client();
}

/* a seat of the vehicle definition's that is a driver's, or NONE */
static short race_vehicles_driver_seat(
	long definition_index)
{
	struct unit_definition *definition = vehicle_definition_get(definition_index);
	short seat_index;

	for (seat_index = 0; seat_index < definition->unit.seats.count; seat_index++)
	{
		struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, seat_index, struct unit_seat);

		if (TEST_FLAG(seat->flags, _unit_seat_driver_bit))
			return seat_index;
	}
	return NONE;
}

/* the vehicle a player starts in: the variant's set's first, as
spawn_race_vehicles makes it (default and Warthogs: the globals' first
multiplayer vehicle; Ghosts, Scorpions: the second, the third), one with a
driver's seat; NONE for none (a set of no vehicles) */
static long race_vehicles_definition(
	void)
{
	struct game_globals *game_globals = scenario_get_game_globals();
	struct game_globals_multiplayer_information *information;
	long vehicle_set = game_engine_get_variant()->universal_variant.vehicle_set;
	short index = vehicle_set == VEHICLE_SET_GHOST ? 1 : vehicle_set == VEHICLE_SET_TANK ? 2 : 0;
	long definition_index = NONE;

	if (vehicle_set != VEHICLE_SET_NONE && game_globals->multiplayer_information.count > 0)
	{
		information = TAG_BLOCK_GET_ELEMENT(&game_globals->multiplayer_information, 0,
			struct game_globals_multiplayer_information);
		if (index < information->vehicles.count)
			definition_index = TAG_BLOCK_GET_ELEMENT(&information->vehicles, index, struct game_globals_vehicle)->vehicle.index;
	}
	if (definition_index != NONE &&
		(!tag_index_is_group(definition_index, VEHICLE_DEFINITION_TAG) ||
			race_vehicles_driver_seat(definition_index) == NONE))
	{
		definition_index = NONE;
	}
	if (definition_index != race_vehicles_reported_definition)
	{
		race_vehicles_reported_definition = definition_index;
		platform_log("race: players start in %s", definition_index != NONE ? tag_get_name(definition_index) :
			"no vehicle (the variant has none, or the map's has no driver's seat)");
	}
	return definition_index;
}

/* whether anyone rides the vehicle */
static boolean race_vehicles_ridden(
	long vehicle_index)
{
	long child_index;

	for (child_index = object_get(vehicle_index)->object.first_child_object_index; child_index != NONE;
		child_index = object_get(child_index)->object.next_object_index)
	{
		struct object_datum *child = object_get(child_index);

		if (TEST_FLAG(_object_mask_unit, child->object.type) && unit_get(child_index)->unit.parent_seat_index != NONE)
			return TRUE;
	}
	return FALSE;
}

static long race_vehicles_find(
	long vehicle_index)
{
	long index;

	for (index = 0; index < race_vehicles_count; index++)
		if (race_spawned_vehicles[index].vehicle_index == vehicle_index)
			return index;
	return NONE;
}

static void race_vehicles_forget(
	long index)
{
	race_spawned_vehicles[index] = race_spawned_vehicles[--race_vehicles_count];
}

/* an empty, upright vehicle with a driver's seat near the point (one that
moves: not a turret), or NONE */
static long race_vehicles_nearby_empty(
	real_point3d const *point)
{
	struct object_iterator iterator;
	long best_index = NONE;
	real best_distance = REUSE_DISTANCE * REUSE_DISTANCE;

	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while (object_iterator_next(&iterator))
	{
		struct object_datum *vehicle = object_get(iterator.index);
		real distance = distance_squared3d(point, &vehicle->object.position);

		if (distance < best_distance && vehicle->object.up.k >= 0.5f &&
			!TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) &&
			!race_vehicles_ridden(iterator.index) &&
			((struct race_vehicle_definition *)tag_get(VEHICLE_DEFINITION_TAG, vehicle->definition_index))->vehicle_type !=
				_vehicle_type_turret &&
			race_vehicles_driver_seat(vehicle->definition_index) != NONE)
		{
			best_index = iterator.index;
			best_distance = distance;
		}
	}
	return best_index;
}

/* ---------- public code */

void race_vehicles_set_spawn_in_vehicle(
	boolean enabled)
{
	race_vehicles_enabled = enabled;
}

boolean race_vehicles_spawn_in_vehicle(
	void)
{
	return race_vehicles_enabled;
}

/* a new game: none made yet */
void race_vehicles_new_game(
	void)
{
	race_vehicles_count = 0;
}

/* whether the vehicle was made for a player (the Xbox game's start of a
race leaves those, game_engine_race.c) */
boolean race_vehicles_spawned(
	long vehicle_index)
{
	return race_vehicles_find(vehicle_index) != NONE;
}

/* the player's unit has just spawned (players.c, player_spawn): into a
vehicle's driver's seat */
void race_vehicles_player_spawned(
	long player_index)
{
	struct player_datum *player;
	struct unit_datum *unit;
	struct object_placement_data placement;
	long vehicle_index;
	long definition_index;
	short seat_index;
	long index;

	if (!race_vehicles_active())
		return;
	player = player_get(player_index);
	if (player->unit_index == NONE || !(unit = unit_try_and_get(player->unit_index)) ||
		unit->object.parent_object_index != NONE || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
	{
		return;
	}
	/* (the vehicle of the player's last life, if nobody rides it: gone) */
	for (index = race_vehicles_count - 1; index >= 0; index--)
	{
		struct race_spawned_vehicle *spawned = &race_spawned_vehicles[index];

		if (spawned->player_index == player_index && object_try_and_get(spawned->vehicle_index) &&
			!race_vehicles_ridden(spawned->vehicle_index))
		{
			object_delete(spawned->vehicle_index);
			race_vehicles_forget(index);
		}
	}
	vehicle_index = race_vehicles_nearby_empty(&unit->object.position);
	if (vehicle_index == NONE)
	{
		definition_index = race_vehicles_definition();
		if (definition_index == NONE)
			return;
		object_placement_data_new(&placement, definition_index, NONE);
		placement.position = unit->object.position;
		placement.forward = unit->object.forward;
		placement.forward.k = 0.0f;
		if (normalize3d(&placement.forward) == 0.0f)
			placement.forward = *global_forward3d;
		placement.up = *global_up3d;
		vehicle_index = object_new(&placement);
		if (vehicle_index == NONE)
			return;
		if (race_vehicles_count < MAXIMUM_SPAWNED_VEHICLES)
		{
			struct race_spawned_vehicle *spawned = &race_spawned_vehicles[race_vehicles_count++];

			spawned->vehicle_index = vehicle_index;
			spawned->empty_since = NONE;
		}
	}
	index = race_vehicles_find(vehicle_index);
	if (index != NONE)
	{
		race_spawned_vehicles[index].player_index = player_index;
		race_spawned_vehicles[index].unit_index = player->unit_index;
		race_spawned_vehicles[index].empty_since = NONE;
	}
	for (seat_index = 0; seat_index < unit_definition_get(object_get(vehicle_index)->definition_index)->unit.seats.count;
		seat_index++)
	{
		if (unit_seat_is_driver(vehicle_index, seat_index) && unit_enter_seat(player->unit_index, vehicle_index, seat_index))
			return;
	}
	platform_log("race: player %ld could not get into %s", (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index),
		tag_get_name(object_get(vehicle_index)->definition_index));
}

/* every tick of a race, on the host: the vehicles made for players that
nobody rides any more, taken away */
void race_vehicles_update(
	void)
{
	long now = game_time_get();
	long index;

	if (!race_vehicles_active())
		return;
	for (index = race_vehicles_count - 1; index >= 0; index--)
	{
		struct race_spawned_vehicle *spawned = &race_spawned_vehicles[index];
		struct object_datum *vehicle = object_try_and_get(spawned->vehicle_index);
		struct player_datum *player;
		boolean life_over;

		if (!vehicle || vehicle->object.type != _object_type_vehicle)
		{
			race_vehicles_forget(index);
			continue;
		}
		if (race_vehicles_ridden(spawned->vehicle_index))
		{
			spawned->empty_since = NONE;
			continue;
		}
		if (spawned->empty_since == NONE)
			spawned->empty_since = now;
		player = player_try_and_get(spawned->player_index);
		life_over = !player || player->unit_index != spawned->unit_index || !unit_try_and_get(spawned->unit_index) ||
			TEST_FLAG(object_get(spawned->unit_index)->object.damage_flags, _object_dead_bit);
		if (life_over || now - spawned->empty_since >= ABANDONED_SECONDS * TICKS_PER_SECOND)
		{
			object_delete(spawned->vehicle_index);
			race_vehicles_forget(index);
		}
	}
}
