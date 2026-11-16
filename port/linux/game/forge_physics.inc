/*
FORGE_PHYSICS.INC

An object's physics (included by forge.c, after forge_netgame.inc, for its
statics): Halo 3's Physics property - NORMAL, FIXED, PHASED - in the
properties window's PHYSICS row, for any placed object but a character.

- NORMAL: as the game has it (it falls, it is pushed, it is driven off).
- FIXED: it never moves: the game's host (or a local game) puts it back where
  the editor let it go each frame it was moved, its velocity none - a
  vehicle is not driven off, a weapon not knocked about (one picked up is
  carried as ever, and back at its place once let go). The editor still
  moves it: held, it goes where the monitor takes it, and stays where it is
  let go (it does not fall).
- PHASED: no collision, by the engine's own switch (_object_no_collisions_bit,
  the one the scripts' object_set_collideable sets): players, vehicles and
  other objects pass through it, so that pieces can overlap to make shapes.
  Only its own collision goes: a floor of NORMAL or FIXED pieces under it
  holds a player as ever, and the map's structure is never touched. (A
  projectile's line still meets it: the engine's vector test does not read
  the switch, as for a scripted object; so a PHASED object also carries
  _object_forge_phased_bit, which the vector tests of collisions.c read: shots
  pass it, a scripted object's behaviour as it was. The monitor's crosshair
  asks for it, _collision_test_forge_phased_objects_bit: it is still grabbed.)

Every machine of a network Forge game sets the switch on its own copy (a
joiner's movement is its own until the host corrects it), from the entry's
physics (forge_net_protocol.h: the entry's step); FIXED is the host's alone.
Saved in the map variant as version 7's flags (forge_files.h
FORGE_RECORD_PHYSICS_*). Nothing of a game that is not Forge or a map variant
played is touched.
*/

/* ---------- constants */

/* a fixed object's place kept to within this (world units) */
#define FORGE_PHYSICS_FIXED_TOLERANCE 0.005f

/* ---------- private code */

static short forge_physics_mode(
	struct forge_placed const *placed)
{
	if (placed->flags & FORGE_RECORD_PHYSICS_PHASED)
		return _forge_physics_phased;
	if (placed->flags & FORGE_RECORD_PHYSICS_FIXED)
		return _forge_physics_fixed;
	return _forge_physics_normal;
}

static unsigned char forge_physics_flags(
	short mode)
{
	return (unsigned char)(mode == _forge_physics_phased ? FORGE_RECORD_PHYSICS_PHASED :
		mode == _forge_physics_fixed ? FORGE_RECORD_PHYSICS_FIXED : 0);
}

/* the object's collision switch as its physics has it: set for a phased
one, cleared for another only where this set it */
static void forge_physics_apply(
	struct forge_placed *placed)
{
	struct object_datum *object;
	boolean phased = forge_physics_mode(placed) == _forge_physics_phased && !placed->ai;

	if (!forge_object_alive(placed->object_index))
	{
		placed->phased_applied = FALSE;
		return;
	}
	object = object_get(placed->object_index);
	if (phased)
	{
		if (!TEST_FLAG(object->object.flags, _object_no_collisions_bit))
			SET_FLAG(object->object.flags, _object_no_collisions_bit, TRUE);
		SET_FLAG(object->object.flags, _object_forge_phased_bit, TRUE);
		placed->phased_applied = TRUE;
	}
	else if (placed->phased_applied)
	{
		SET_FLAG(object->object.flags, _object_no_collisions_bit, FALSE);
		SET_FLAG(object->object.flags, _object_forge_phased_bit, FALSE);
		placed->phased_applied = FALSE;
	}
}

/* (each frame, a Forge game or a map variant played, before the tick) every
placed object's collision switch; a fixed one put back where it was let go
(the host's, a local game's) */
static void forge_physics_update(
	void)
{
	short index;
	boolean holds = !forge_net_client();

	if (!forge_globals.game_active)
		return;
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed *placed = &forge_globals.placed[index];
		struct object_datum *object;

		forge_physics_apply(placed);
		if (!holds || placed->ai || !(placed->flags & FORGE_RECORD_PHYSICS_FIXED) || !placed->has_home ||
			!forge_object_alive(placed->object_index) || placed->object_index == forge_globals.held_object_index ||
			forge_net_held_by_other(placed->object_index))
		{
			continue;
		}
		object = object_get(placed->object_index);
		if (object->object.parent_object_index != NONE || TEST_FLAG(object->object.damage_flags, _object_dead_bit))
			continue;
		if (distance_squared3d(&object->object.position, &placed->home.position) >
			FORGE_PHYSICS_FIXED_TOLERANCE * FORGE_PHYSICS_FIXED_TOLERANCE ||
			magnitude_squared3d(&object->object.translational_velocity) > 0.f ||
			magnitude_squared3d(&object->object.angular_velocity) > 0.f)
		{
			object_set_position(placed->object_index, &placed->home.position, &placed->home.forward, &placed->home.up);
			object = object_get(placed->object_index);
			object->object.translational_velocity = *global_zero_vector3d;
			object->object.angular_velocity = *global_zero_vector3d;
			SET_FLAG(object->object.flags, _object_at_rest_bit, TRUE);
		}
	}
}

/* (forge_drop, an object turned or set, an undo's move: wherever the editor
lets an object go to fall) a weapon or a piece of equipment let go with its
origin inside a solid object - a building block, a crate - put on that
object's top. An item falls by a line from where it is to where it moves
(items.c item_update), and a line inside an object's collision model crosses
none of its surfaces: one let go even a centimetre into a block fell through
it (1.1.2: "weapons fall through every block"; one held to look as if it lay
on a block's top has its origin a little in it). The map's structure needs
nothing: an item in the ground stays there (its location must stay valid),
and players and vehicles are pushed out of what they are in. Only items, only
the objects an item rests on (scenery, machines, controls: items.c's rule).
1.1.4: at any depth - 1.1.3 lifted only one whose origin was within its
bounding radius (a powerup's, a grenade's: 0.1) of the top, and one held a
little deeper, or let go inside a block from its side, still fell through to
the ground; the top is looked for FORGE_ITEM_UNBURY_HEIGHT up (the tallest
block is 2 high), and one under an object, not in it, meets its underside
first and is left alone. A ramp's 45 degree face is a top too */
#define FORGE_ITEM_UNBURY_HEIGHT 4.f
#define FORGE_ITEM_UNBURY_TOP 0.6f
static void forge_item_unburied(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	unsigned long solid = FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_objects_machines_bit) | FLAG(_collision_test_objects_controls_bit);
	struct collision_result entered;
	struct collision_result left;
	real_point3d position;
	real_point3d over;
	real height;
	boolean enters;

	if (!object || !(FLAG(object->object.type) & _object_mask_item) || object->object.parent_object_index != NONE)
		return;
	position = object->object.position;
	height = MAX(object->object.bounding_sphere_radius, FORGE_ITEM_UNBURY_HEIGHT);
	over = position;
	over.z += height;
	/* (inside: going up, a surface left - a back face - before any entered) */
	if (!collision_test_line(solid | FLAG(_collision_test_back_facing_surfaces_bit), &position, &over, object_index, &left) ||
		left.type != _collision_result_object || left.plane.n.k < FORGE_ITEM_UNBURY_TOP)
	{
		return;
	}
	enters = collision_test_line(solid | FLAG(_collision_test_front_facing_surfaces_bit), &position, &over, object_index,
		&entered);
	if (enters && entered.t <= left.t)
		return;
	/* (just over the top: it falls onto it and rests there) */
	over = left.point;
	over.z += 0.05f;
	object_set_position(object_index, &over, NULL, NULL);
	object->object.translational_velocity = *global_zero_vector3d;
	platform_log("forge: %s let go %.2f into %s: put on its top at %.2f %.2f %.2f", forge_tag_leaf(object->definition_index),
		left.point.z - position.z, forge_tag_leaf(object_get(left.object_index)->definition_index), over.x, over.y, over.z);
}
