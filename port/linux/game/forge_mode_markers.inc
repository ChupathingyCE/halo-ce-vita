/*
FORGE_MODE_MARKERS.INC

The game modes' markers (included by forge.c after forge_spawns.inc, for its
statics): the Forge objects a map maker places for FIREFIGHT and WARZONE (the
design: docs/superpowers/specs/2026-11-05-forge-warzone-firefight-design.md,
section 2) - an AI SPAWN ZONE (a squad of Halo CE's own characters, how
many, its side, its radius, its rounds and waves or its Warzone timing, the
base it defends, its delivery), a BOSS (its kind, a name of our own list,
its points, its toughness, its side, its round or its minute, its return,
its delivery), a BASE (its letter, its owner at the start, a team's home
base, its radius, its capture time), a CORE (its team, its hull), a DROP
POINT (its side, its ship, its facing), a FIREFIGHT OBJECTIVE (its kind,
its round, its radius, its time, a defended object's hull) and a BONUS ROUND
(Firefight round 3: where the bonus round's rush comes from - its squad, how
many, its radius, its delivery - and the bonus round's own settings, its
time and its points a kill, or the bonus round off). The OBJECTIVES browser
offers no BOSS now (the owner, Nov 5: "Firefight didn't really have bosses,
it had bonus rounds"): a boss is Warzone's, a layout's own still read,
drawn, edited and saved for its later game type, and Firefight ignores it.

Each is no object of the map's: a marker in the player spawn points' table
(forge_globals.spawns, its kind past theirs: forge.h _forge_mode_marker_*),
so it is drawn (forge_markers.inc: the see-through marker in its side's
colour, a ring on the ground at its radius), taken, carried, let go, turned
and deleted as a spawn point is, its properties opened from it (Square while
holding it: each kind's rows, forge_mode_marker_properties_get), for the
monitor only, never for the Spartan. The OBJECTIVES category of the object
browser places them. At most FORGE_MAXIMUM_MODE_MARKERS, apart from the spawn
points' FORGE_MAXIMUM_SPAWNS, in the objects' budget; a few kinds fewer
(forge_files.h: five bases, eight drop points, one core a team).

Saved in the map variant as version 5's mode marker records (forge_files.h),
with the modes' settings (forge_globals.mode_settings: the map maker's
defaults); read back as the map loads.

Network Forge (until the game modes' network step): a network game's host
keeps its markers to itself - they are never sent to a joiner as spawn
entries (a joiner would take one for a respawn point: forge_spawn_add) nor
in the layout a joiner is given, and a joiner's request naming one (move,
delete, properties) is refused; a joiner cannot place one (its browser says
why).
*/

/* ---------- constants */

/* (the OBJECTIVES category's game modes' items' names: forge.c's forge_mode_marker_names) */
static char const *const forge_mode_marker_log_names[NUMBER_OF_FORGE_MODE_MARKERS] =
{
	"AI spawn zone", "boss", "base", "core", "drop point", "Firefight objective", "bonus round", "boundary", "lobby camera",
};

/* an AI spawn zone's squads (forge_files.h _forge_mode_squad_*' order) */
static char const *const forge_mode_squad_names[NUMBER_OF_FORGE_MODE_SQUADS] =
{
	N_("Marines"), N_("Armored marines"), N_("Grunts"), N_("Grunts and Jackals"), N_("Elite and Grunts"), N_("Elites"),
	N_("Stealth Elites"), N_("Hunter pair"), N_("Flood combat forms"), N_("Flood pod"), N_("Sentinels"), N_("Shade crew"),
	N_("Elite commanders"),
};

/* a boss's kinds (_forge_mode_boss_*) */
static char const *const forge_mode_boss_kind_names[NUMBER_OF_FORGE_MODE_BOSSES] =
{
	N_("Elite commander"), N_("Elite major"), N_("Stealth Elite"), N_("Hunter"), N_("Flood carrier"), N_("Sentinel major"),
};

/* the bosses' names: our own (the design: never another game's, never typed
text, so a shared variant carries none) */
static char const *const forge_mode_boss_names[FORGE_FILE_MODE_BOSS_NAMES] =
{
	N_("Ironjaw"), N_("the Red Reaver"), N_("Old Thunder"), N_("Ashkeeper"), N_("Nightglass"), N_("the Barrow Warden"),
	N_("Stonehide"), N_("Emberclaw"), N_("the Hollow King"), N_("Duskfang"), N_("Rustmaw"), N_("the Pale Herald"),
	N_("Shardback"), N_("Cinderveil"), N_("the Last Watch"), N_("Thornspire"),
};

/* a drop point's ships (_forge_mode_ship_*) */
static char const *const forge_mode_ship_names[NUMBER_OF_FORGE_MODE_SHIPS] =
{
	N_("BY SPECIES"), N_("PELICAN"), N_("COVENANT DROPSHIP"),
};

/* a Firefight objective's kinds (_forge_mode_objective_*) */
static char const *const forge_mode_objective_names[NUMBER_OF_FORGE_MODE_OBJECTIVES] =
{
	N_("Defend zone"), N_("Hold zone"), N_("Defend object"), N_("Kill the boss"),
};

/* the choices the rows step through */
static unsigned short const forge_mode_wave_seconds[] = { 0, 30, 60, 90, 120, 180, 240, 300 };
#define FORGE_MODE_BOSS_RETURN_STEP 60
#define FORGE_MODE_POINTS_STEP 25
#define FORGE_MODE_HULL_STEP 500
#define FORGE_MODE_BONUS_SECONDS_STEP 15
/* (a bonus round's: Reach's and ODST's minute) */
#define FORGE_MODE_BONUS_SECONDS 60
#define FORGE_MODE_BONUS_POINTS 100

/* ---------- private code */

static boolean forge_kind_is_mode_marker(
	short kind)
{
	return kind >= FIRST_FORGE_MODE_MARKER && kind < FORGE_MODE_MARKER_KINDS_END;
}

static short forge_mode_markers_count(
	void)
{
	short index;
	short count = 0;

	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
		count += forge_kind_is_mode_marker(forge_globals.spawns[index].kind) ? 1 : 0;
	return count;
}

static short forge_mode_markers_of_kind(
	short kind)
{
	short index;
	short count = 0;

	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
		count += forge_globals.spawns[index].kind == kind ? 1 : 0;
	return count;
}

/* (a base's letter: each its own) the letters the layout's bases take, a bit
each, but one's (its own) */
static unsigned short forge_mode_base_letters(
	struct forge_spawn const *but)
{
	short index;
	unsigned short letters = 0;

	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];

		if (spawn->kind == _forge_mode_marker_base && spawn != but)
			letters |= (unsigned short)(1 << (spawn->mode.index % FORGE_FILE_MAXIMUM_BASES));
	}
	return letters;
}

/* (command.c, step 3: Command on a layout; editor6, Nov 10: the owner's
bases had no owner set) the layout's home base marker of a team (0 red, 1
blue): its Base marker with HOME BASE yes and that owner, else any base of
that owner; else, when no base has that owner, the bases with no owner by
their letters - red the first (Base A), blue the next (Base B, or Base A
when red has a base of its own) - so that a layout's Base A and Base B are
the two sides' homes as the editor's first two bases are (*by_letter set).
NULL when the layout has none */
static struct forge_spawn const *forge_mode_home_spawn(
	short team,
	boolean *by_letter)
{
	short index;
	struct forge_spawn const *best = NULL;
	struct forge_spawn const *unowned[FORGE_FILE_MAXIMUM_BASES];
	short unowned_count = 0;
	boolean other_owned = FALSE;
	short slot;

	if (by_letter)
		*by_letter = FALSE;
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];

		if (spawn->kind != _forge_mode_marker_base)
			continue;
		if (spawn->team == team)
		{
			if (!best || ((spawn->mode.flags & FORGE_MODE_FLAG_HOME) && !(best->mode.flags & FORGE_MODE_FLAG_HOME)))
				best = spawn;
		}
		else if (spawn->team == (team == 0 ? 1 : 0))
			other_owned = TRUE;
		else if (spawn->team < 0 && unowned_count < FORGE_FILE_MAXIMUM_BASES)
		{
			/* (in their letters' order) */
			short at = unowned_count++;

			while (at > 0 && unowned[at - 1]->mode.index > spawn->mode.index)
			{
				unowned[at] = unowned[at - 1];
				at--;
			}
			unowned[at] = spawn;
		}
	}
	if (best)
		return best;
	slot = team == 0 || other_owned ? 0 : 1;
	if (slot >= unowned_count)
		return NULL;
	if (by_letter)
		*by_letter = TRUE;
	return unowned[slot];
}

boolean forge_mode_home_base(
	short team,
	real_point3d *point,
	real *radius)
{
	struct forge_spawn const *best;

	if (!forge_globals.game_active)
		return FALSE;
	best = forge_mode_home_spawn(team, NULL);
	if (!best)
		return FALSE;
	*point = best->home.position;
	if (radius)
		*radius = forge_mode_marker_radius(best);
	return TRUE;
}

/* (editor6, command.c: Command played on the layout as the editor shows it)
how many of a kind the session's layout has (NONE: its game mode markers
of any kind) */
short forge_mode_layout_count(
	short kind)
{
	if (!forge_globals.game_active)
		return 0;
	return kind == NONE ? forge_mode_markers_count() : forge_mode_markers_of_kind(kind);
}

/* the layout's home base of a team as a whole: its letter ('A'...), its
owner as the layout has it (NONE: taken by its letter), its point, radius
and capture time */
boolean forge_mode_home_area(
	short team,
	struct forge_mode_area *area)
{
	boolean by_letter;
	struct forge_spawn const *spawn;

	if (!forge_globals.game_active || !(spawn = forge_mode_home_spawn(team, &by_letter)))
		return FALSE;
	csmemset(area, 0, sizeof(*area));
	area->center = spawn->home.position;
	area->radius = forge_mode_marker_radius(spawn);
	area->seconds = spawn->mode.seconds;
	area->owner = by_letter ? NONE : spawn->team;
	area->letter = (char)('A' + spawn->mode.index % FORGE_FILE_MAXIMUM_BASES);
	area->id = spawn->id;
	return TRUE;
}

/* the layout's bases that are not a side's home (Command's territories, the
supply points), in their letters' order: each one's point, radius, capture
time, owner at the start (NONE neutral), letter; how many */
short forge_mode_command_territories(
	struct forge_mode_area *areas,
	short capacity)
{
	struct forge_spawn const *homes[2];
	short index;
	short count = 0;

	if (!forge_globals.game_active)
		return 0;
	homes[0] = forge_mode_home_spawn(0, NULL);
	homes[1] = forge_mode_home_spawn(1, NULL);
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE && count < capacity; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];
		struct forge_mode_area *area;
		short at;

		if (spawn->kind != _forge_mode_marker_base || spawn == homes[0] || spawn == homes[1])
			continue;
		at = count++;
		while (at > 0 && areas[at - 1].letter > (char)('A' + spawn->mode.index % FORGE_FILE_MAXIMUM_BASES))
		{
			areas[at] = areas[at - 1];
			at--;
		}
		area = &areas[at];
		csmemset(area, 0, sizeof(*area));
		area->center = spawn->home.position;
		area->radius = forge_mode_marker_radius(spawn);
		area->seconds = spawn->mode.seconds;
		area->owner = spawn->team;
		area->letter = (char)('A' + spawn->mode.index % FORGE_FILE_MAXIMUM_BASES);
		area->id = spawn->id;
	}
	return count;
}

/* the layout's objectives for Command (its objective territories): its
Firefight objectives of a zone's kind (DEFEND ZONE, HOLD ZONE) and its King
of the Hill hill points, in the layout's order, lettered A, B, C...; each
one's point, radius (a hill point's the objectives' own: 0) and time; how
many */
short forge_mode_command_objectives(
	struct forge_mode_area *areas,
	short capacity)
{
	short index;
	short count = 0;

	if (!forge_globals.game_active)
		return 0;
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE && count < capacity; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];
		struct forge_mode_area *area;

		if (!(spawn->kind == _forge_mode_marker_ff_objective && (spawn->variant == _forge_mode_objective_defend_zone ||
			spawn->variant == _forge_mode_objective_hold_zone)) && spawn->kind != _forge_spawn_hill)
		{
			continue;
		}
		area = &areas[count];
		csmemset(area, 0, sizeof(*area));
		area->center = spawn->home.position;
		area->radius = spawn->kind == _forge_spawn_hill ? 0.f : forge_mode_marker_radius(spawn);
		area->seconds = spawn->kind == _forge_spawn_hill ? 0 : spawn->mode.seconds;
		area->owner = NONE;
		area->letter = (char)('A' + count);
		area->id = spawn->id;
		count++;
	}
	return count;
}

/* the layout's drop point for a side's ships: one of its side, else of the
ones of no side the nearest its base of those nearer its base than the
other side's home (each side its own half), else the nearest of no side;
FALSE when the layout has none for it */
boolean forge_mode_drop_point(
	short team,
	real_point3d const *base,
	real_point3d *point,
	real *yaw)
{
	short index;
	struct forge_spawn const *best = NULL;
	short best_rank = 0;
	real best_distance = 0.f;
	struct forge_spawn const *other = forge_mode_home_spawn(team == 0 ? 1 : 0, NULL);

	if (!forge_globals.game_active)
		return FALSE;
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];
		short rank;
		real dx, dy, distance;

		if (spawn->kind != _forge_mode_marker_drop_point || (spawn->team >= 0 && spawn->team != team))
			continue;
		dx = base ? spawn->home.position.x - base->x : 0.f;
		dy = base ? spawn->home.position.y - base->y : 0.f;
		distance = dx * dx + dy * dy;
		/* (2 its side's, 1 of no side on its half, 0 of no side) */
		if (spawn->team == team)
			rank = 2;
		else
		{
			real ox = other ? spawn->home.position.x - other->home.position.x : 0.f;
			real oy = other ? spawn->home.position.y - other->home.position.y : 0.f;

			rank = !other || distance <= ox * ox + oy * oy ? 1 : 0;
		}
		if (!best || rank > best_rank || (rank == best_rank && distance < best_distance))
		{
			best = spawn;
			best_rank = rank;
			best_distance = distance;
		}
	}
	if (!best)
		return FALSE;
	*point = best->home.position;
	if (yaw)
		*yaw = (real)atan2(best->home.forward.j, best->home.forward.i);
	return TRUE;
}

/* the layout's CTF flag spot of a team (a game type object of the
OBJECTIVES category); FALSE when the layout has none */
boolean forge_mode_layout_flag(
	short team,
	real_point3d *point)
{
	short index;

	if (!forge_globals.game_active)
		return FALSE;
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];

		if (spawn->kind == _forge_spawn_flag && spawn->channel == team)
		{
			*point = spawn->home.position;
			return TRUE;
		}
	}
	return FALSE;
}

/* the layout's placed objects of a tag (a Shade turret's: command.c): the
nth one alive, its object index (NONE past them) and its team (NONE none) */
long forge_mode_layout_object(
	long tag_index,
	short nth,
	short *team)
{
	short index;

	if (!forge_globals.game_active || tag_index == NONE)
		return NONE;
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed const *placed = &forge_globals.placed[index];

		if (placed->tag_index != tag_index || !object_try_and_get(placed->object_index))
			continue;
		if (nth-- > 0)
			continue;
		if (team)
			*team = placed->team;
		return placed->object_index;
	}
	return NONE;
}

/* (a core's team: one a team) whether the team has a core but one's */
static boolean forge_mode_core_of_team(
	signed char team,
	struct forge_spawn const *but)
{
	short index;

	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];

		if (spawn->kind == _forge_mode_marker_core && spawn != but && spawn->team == team)
			return TRUE;
	}
	return FALSE;
}

/* the most of a kind (forge_files.h's), and whether another may be placed */
static short forge_mode_marker_most(
	short kind)
{
	switch (kind)
	{
	case _forge_mode_marker_base: return FORGE_FILE_MAXIMUM_BASES;
	case _forge_mode_marker_core: return 2;
	case _forge_mode_marker_drop_point: return FORGE_FILE_MAXIMUM_DROP_POINTS;
	case _forge_mode_marker_boundary: return FORGE_FILE_MAXIMUM_BOUNDARIES;
	case _forge_mode_marker_camera: return FORGE_FILE_MAXIMUM_CAMERAS;
	}
	return FORGE_MAXIMUM_MODE_MARKERS;
}

/* a marker's radius on the ground (world units; 0: none drawn; a
boundary's none: its box) */
static real forge_mode_marker_radius(
	struct forge_spawn const *spawn)
{
	if (spawn->kind == _forge_mode_marker_boundary || spawn->kind == _forge_mode_marker_camera)
		return 0.f;
	return (real)spawn->mode.radius / (real)FORGE_FILE_MODE_RADIUS_UNITS;
}

/* (the mode layouts) a boundary's half extents, world units: along its
facing, across it, up */
static void forge_mode_boundary_extents(
	struct forge_spawn const *spawn,
	real *along,
	real *across,
	real *up)
{
	*along = (real)spawn->mode.points / (real)FORGE_FILE_MODE_RADIUS_UNITS;
	*across = (real)spawn->mode.seconds / (real)FORGE_FILE_MODE_RADIUS_UNITS;
	*up = (real)spawn->mode.radius / (real)FORGE_FILE_MODE_RADIUS_UNITS;
}

/* (the mode layouts) the lobby camera's pitch, degrees up from level */
static real forge_mode_camera_pitch(
	struct forge_spawn const *spawn)
{
	return (real)((int)spawn->mode.radius - FORGE_FILE_CAMERA_PITCH_OFFSET);
}

/* a kind's defaults (the design's) */
static void forge_mode_marker_defaults(
	struct forge_spawn *spawn)
{
	spawn->variant = 0;
	spawn->count = 0;
	spawn->respawn_seconds = 0;
	spawn->game_types = 0;
	spawn->team = NONE;
	csmemset(&spawn->mode, 0, sizeof(spawn->mode));
	switch (spawn->kind)
	{
	case _forge_mode_marker_ai_zone:
		spawn->variant = _forge_mode_squad_grunts;
		spawn->count = 4;
		spawn->mode.radius = 4 * FORGE_FILE_MODE_RADIUS_UNITS;
		break;
	case _forge_mode_marker_boss:
		spawn->variant = _forge_mode_boss_elite_commander;
		spawn->mode.points = 150;
		spawn->mode.toughness = 3;
		break;
	case _forge_mode_marker_base:
	{
		unsigned short letters = forge_mode_base_letters(spawn);
		unsigned char letter;

		for (letter = 0; letter < FORGE_FILE_MAXIMUM_BASES - 1 && (letters & (1 << letter)); letter++)
			;
		spawn->mode.index = letter;
		/* (editor6: Base A red's home, Base B blue's - Command's two sides,
		as the owner places them - the others neutral territories) */
		if (letter < 2)
		{
			spawn->team = (signed char)letter;
			spawn->mode.flags |= FORGE_MODE_FLAG_HOME;
		}
		spawn->mode.radius = 8 * FORGE_FILE_MODE_RADIUS_UNITS;
		spawn->mode.seconds = 10;
		break;
	}
	case _forge_mode_marker_core:
		spawn->team = forge_mode_core_of_team(0, spawn) ? 1 : 0;
		spawn->mode.points = 3000;
		break;
	case _forge_mode_marker_ff_objective:
		spawn->variant = _forge_mode_objective_hold_zone;
		spawn->mode.radius = 5 * FORGE_FILE_MODE_RADIUS_UNITS;
		spawn->mode.seconds = 60;
		break;
	case _forge_mode_marker_bonus_round:
		spawn->variant = _forge_mode_squad_grunts;
		spawn->count = 4;
		spawn->mode.radius = 6 * FORGE_FILE_MODE_RADIUS_UNITS;
		spawn->mode.seconds = FORGE_MODE_BONUS_SECONDS;
		spawn->mode.points = FORGE_MODE_BONUS_POINTS;
		break;
	case _forge_mode_marker_boundary:
		/* (a wall 8 by 8 by 4 world units) */
		spawn->variant = FORGE_FILE_BOUNDARY_WALL;
		spawn->mode.points = 4 * FORGE_FILE_MODE_RADIUS_UNITS;
		spawn->mode.seconds = 4 * FORGE_FILE_MODE_RADIUS_UNITS;
		spawn->mode.radius = 2 * FORGE_FILE_MODE_RADIUS_UNITS;
		break;
	case _forge_mode_marker_camera:
		/* (level) */
		spawn->mode.radius = FORGE_FILE_CAMERA_PITCH_OFFSET;
		break;
	}
}

static char const *forge_mode_side_name(
	signed char team)
{
	return team == 0 ? T("RED") : team == 1 ? T("BLUE") : T("HOSTILE");
}

static char const *forge_mode_side_log_name(
	signed char team)
{
	return team == 0 ? "red" : team == 1 ? "blue" : team == FORGE_MODE_TEAM_HOSTILE ? "hostile" : "none";
}

/* a marker's readable name ("Base B", "Core, Red", "Boss: Ironjaw"),
translated */
static void forge_mode_marker_name(
	struct forge_spawn const *spawn,
	char *name,
	int size)
{
	char const *kind = T(forge_mode_marker_names[(spawn->kind - FIRST_FORGE_MODE_MARKER) % NUMBER_OF_FORGE_MODE_MARKERS]);

	switch (spawn->kind)
	{
	case _forge_mode_marker_ai_zone:
		snprintf(name, (size_t)size, "%s: %s", kind, T(forge_mode_squad_names[spawn->variant % NUMBER_OF_FORGE_MODE_SQUADS]));
		break;
	case _forge_mode_marker_boss:
		snprintf(name, (size_t)size, "%s: %s", kind, T(forge_mode_boss_names[spawn->mode.index % FORGE_FILE_MODE_BOSS_NAMES]));
		break;
	case _forge_mode_marker_base:
		snprintf(name, (size_t)size, "%s %c", kind, 'A' + spawn->mode.index % FORGE_FILE_MAXIMUM_BASES);
		break;
	case _forge_mode_marker_core:
		snprintf(name, (size_t)size, spawn->team == 0 ? T("%s, Red") : T("%s, Blue"), kind);
		break;
	case _forge_mode_marker_ff_objective:
		snprintf(name, (size_t)size, "%s", T(forge_mode_objective_names[spawn->variant % NUMBER_OF_FORGE_MODE_OBJECTIVES]));
		break;
	case _forge_mode_marker_bonus_round:
		snprintf(name, (size_t)size, "%s: %s", kind, T(forge_mode_squad_names[spawn->variant % NUMBER_OF_FORGE_MODE_SQUADS]));
		break;
	case _forge_mode_marker_boundary:
		snprintf(name, (size_t)size, "%s: %s", kind, spawn->variant == FORGE_FILE_BOUNDARY_KILL ? T("Kill zone") : T("Wall"));
		break;
	default:
		snprintf(name, (size_t)size, "%s", kind);
		break;
	}
}

static void forge_mode_marker_log(
	char const *what,
	struct forge_spawn const *spawn)
{
	platform_log("forge: %s the %s #%ld (side %s, variant %d, count %d, modes %d, radius %.2f, rounds %d, waves %d, "
		"points %d, toughness %d, seconds %d, minute %d, index %d, flags %d, respawn %d) at %.2f %.2f %.2f facing %.2f %.2f",
		what, forge_mode_marker_log_names[(spawn->kind - FIRST_FORGE_MODE_MARKER) % NUMBER_OF_FORGE_MODE_MARKERS], spawn->id,
		forge_mode_side_log_name(spawn->team), spawn->variant, spawn->count, spawn->game_types, forge_mode_marker_radius(spawn),
		spawn->mode.rounds, spawn->mode.waves, spawn->mode.points, spawn->mode.toughness, spawn->mode.seconds,
		spawn->mode.start_minute, spawn->mode.index, spawn->mode.flags, spawn->respawn_seconds, spawn->home.position.x,
		spawn->home.position.y, spawn->home.position.z, spawn->home.forward.i, spawn->home.forward.j);
}

/* a marker added to the layout (id 0: a new id), its kind's defaults, or
its record's (NULL: none read yet); NULL when there are the most */
static struct forge_spawn *forge_mode_marker_add(
	long id,
	short kind,
	struct forge_transform const *home,
	struct forge_file_record const *record)
{
	struct forge_spawn *spawn;

	if (!forge_kind_is_mode_marker(kind) || forge_globals.spawn_count < 0 || forge_globals.spawn_count >= FORGE_SPAWN_TABLE ||
		forge_mode_markers_count() >= FORGE_MAXIMUM_MODE_MARKERS || forge_mode_markers_of_kind(kind) >= forge_mode_marker_most(kind))
	{
		return NULL;
	}
	spawn = &forge_globals.spawns[forge_globals.spawn_count++];
	csmemset(spawn, 0, sizeof(*spawn));
	spawn->id = id ? id : ++forge_globals.next_id;
	spawn->kind = kind;
	if (record)
	{
		spawn->team = record->team;
		spawn->variant = record->variant;
		spawn->count = record->count;
		spawn->respawn_seconds = record->respawn_seconds;
		spawn->game_types = record->game_types;
		spawn->mode = record->mode;
	}
	else
		forge_mode_marker_defaults(spawn);
	spawn->home = *home;
	spawn->home.forward.k = 0.f;
	if (normalize3d(&spawn->home.forward) < 0.0001f)
		spawn->home.forward = *global_forward3d;
	spawn->home.up = *global_up3d;
	return spawn;
}

/* (forge_markers.inc) a marker's colour: its side's (hostile: purple; none
or any: white) */
static void forge_mode_marker_color(
	struct forge_spawn const *spawn,
	real *red,
	real *green,
	real *blue)
{
	/* (the lobby camera: cyan) */
	if (spawn->kind == _forge_mode_marker_camera)
	{
		*red = 0.25f;
		*green = 0.9f;
		*blue = 1.f;
		return;
	}
	/* (a boundary: a wall amber, a kill zone red) */
	if (spawn->kind == _forge_mode_marker_boundary)
	{
		*red = 1.f;
		*green = spawn->variant == FORGE_FILE_BOUNDARY_KILL ? 0.2f : 0.65f;
		*blue = spawn->variant == FORGE_FILE_BOUNDARY_KILL ? 0.15f : 0.1f;
		return;
	}
	if (spawn->team == 0)
	{
		*red = 1.f;
		*green = 0.28f;
		*blue = 0.22f;
	}
	else if (spawn->team == 1)
	{
		*red = 0.28f;
		*green = 0.55f;
		*blue = 1.f;
	}
	else if (spawn->kind == _forge_mode_marker_ai_zone || spawn->kind == _forge_mode_marker_boss ||
		spawn->team == FORGE_MODE_TEAM_HOSTILE)
	{
		*red = 0.75f;
		*green = 0.40f;
		*blue = 1.f;
	}
	else
	{
		*red = 0.92f;
		*green = 0.92f;
		*blue = 0.85f;
	}
	/* (a boss's brighter, a Firefight objective's gold) */
	if (spawn->kind == _forge_mode_marker_boss)
	{
		*red = MIN(1.f, *red + 0.2f);
		*green = MIN(1.f, *green + 0.1f);
	}
	else if (spawn->kind == _forge_mode_marker_ff_objective)
	{
		*red = 1.f;
		*green = 0.82f;
		*blue = 0.3f;
	}
	/* (a bonus round's orange) */
	else if (spawn->kind == _forge_mode_marker_bonus_round)
	{
		*red = 1.f;
		*green = 0.55f;
		*blue = 0.15f;
	}
}

/* a modes value's words: both, Firefight's, Warzone's */
static char const *forge_mode_modes_name(
	unsigned short game_types)
{
	return game_types == FORGE_FILE_GAME_TYPE_FIREFIGHT ? T(FORGE_FIREFIGHT_NAME) :
		game_types == FORGE_FILE_GAME_TYPE_WARZONE ? T(FORGE_WARZONE_NAME) : T("BOTH");
}

/* a mask's one bit's number from 1 (0: none, NONE: several) */
static short forge_mode_mask_single(
	unsigned char mask)
{
	short bit;

	if (!mask)
		return 0;
	for (bit = 0; bit < 8; bit++)
	{
		if (mask == (1 << bit))
			return (short)(bit + 1);
	}
	return NONE;
}

/* a mask stepped through all (0) and each one bit to count */
static unsigned char forge_mode_mask_step(
	unsigned char mask,
	short count,
	short step)
{
	short at = forge_mode_mask_single(mask);

	/* (several, from a file: all first) */
	at = at == NONE ? (short)(step > 0 ? 0 : count) : (short)((at + step + count + 1) % (count + 1));
	return at ? (unsigned char)(1 << (at - 1)) : 0;
}

static void forge_mode_round_text(
	char *text,
	int size,
	char const *label,
	unsigned char rounds,
	char const *none)
{
	short round = forge_mode_mask_single(rounds);

	if (round == 0)
		snprintf(text, (size_t)size, "%s: %s", label, none);
	else if (round == NONE)
		snprintf(text, (size_t)size, "%s: %s", label, T("SEVERAL"));
	else
		snprintf(text, (size_t)size, "%s: %d", label, round);
}

/* (forge_properties_get) each kind's rows, in its own order (the property
index a row's: forge_mode_marker_property_apply) */
static void forge_mode_marker_properties_get(
	struct forge_spawn const *spawn,
	struct forge_properties *properties)
{
	real_vector3d const *forward = spawn->id == forge_globals.held_marker_id ? &forge_globals.held_forward :
		&spawn->home.forward;
	char (*row)[48] = properties->marker_row;
	int size = (int)sizeof(properties->marker_row[0]);
	boolean warzone = spawn->game_types == FORGE_FILE_GAME_TYPE_WARZONE;
	short rows = 0;

	forge_mode_marker_name(spawn, properties->name, sizeof(properties->name));
	properties->marker = TRUE;
	properties->team = spawn->team;
	properties->yaw = (short)forge_round(RADIANS_TO_DEGREES(arctangent(forward->j, forward->i)));
	switch (spawn->kind)
	{
	case _forge_mode_marker_ai_zone:
		snprintf(row[rows++], size, "%s: %s", T("SQUAD"), T(forge_mode_squad_names[spawn->variant % NUMBER_OF_FORGE_MODE_SQUADS]));
		snprintf(row[rows++], size, "%s: %d", T("COUNT"), spawn->count);
		snprintf(row[rows++], size, "%s: %s", T("TEAM"), forge_mode_side_name(spawn->team));
		snprintf(row[rows++], size, "%s: %d %s", T("RADIUS"), (int)forge_mode_marker_radius(spawn), T("WU"));
		snprintf(row[rows++], size, "%s: %s", T("MODE"), forge_mode_modes_name(spawn->game_types));
		if (warzone)
		{
			snprintf(row[rows++], size, T("STARTS: MINUTE %d"), spawn->mode.start_minute);
			if (spawn->respawn_seconds)
				snprintf(row[rows++], size, T("EVERY: %d S"), spawn->respawn_seconds);
			else
				snprintf(row[rows++], size, "%s: %s", T("EVERY"), T("THE GAME'S"));
		}
		else
		{
			forge_mode_round_text(row[rows++], size, T("ROUND"), spawn->mode.rounds, T("ALL"));
			if (spawn->mode.waves == FORGE_FILE_MODE_HEAVY_WAVE)
				snprintf(row[rows++], size, "%s: %s", T("WAVE"), T("HEAVY WAVE ONLY"));
			else
				forge_mode_round_text(row[rows++], size, T("WAVE"), spawn->mode.waves, T("ALL"));
		}
		if (spawn->mode.index)
			snprintf(row[rows++], size, T("DEFENDS: BASE %c"), 'A' + (spawn->mode.index - 1) % FORGE_FILE_MAXIMUM_BASES);
		else
			snprintf(row[rows++], size, "%s: %s", T("DEFENDS"), T("NOTHING"));
		snprintf(row[rows++], size, "%s: %s", T("DELIVERY"), (spawn->mode.flags & FORGE_MODE_FLAG_DROPSHIP) ? T("DROPSHIP") :
			T("ON THE GROUND"));
		break;
	case _forge_mode_marker_boss:
		snprintf(row[rows++], size, "%s: %s", T("KIND"), T(forge_mode_boss_kind_names[spawn->variant % NUMBER_OF_FORGE_MODE_BOSSES]));
		snprintf(row[rows++], size, "%s: %s", T("NAME"), T(forge_mode_boss_names[spawn->mode.index % FORGE_FILE_MODE_BOSS_NAMES]));
		snprintf(row[rows++], size, "%s: %d", T("POINTS"), spawn->mode.points);
		snprintf(row[rows++], size, "%s: x%d", T("TOUGHNESS"), spawn->mode.toughness);
		snprintf(row[rows++], size, "%s: %s", T("TEAM"), forge_mode_side_name(spawn->team));
		snprintf(row[rows++], size, "%s: %s", T("MODE"), forge_mode_modes_name(spawn->game_types));
		if (warzone)
			snprintf(row[rows++], size, T("APPEARS: MINUTE %d"), spawn->mode.start_minute);
		else
			forge_mode_round_text(row[rows++], size, T("ROUND"), spawn->mode.rounds, T("THE LAST"));
		if (spawn->mode.flags & FORGE_MODE_FLAG_RETURNS)
			snprintf(row[rows++], size, T("RETURNS: AFTER %d S"), spawn->respawn_seconds);
		else
			snprintf(row[rows++], size, "%s: %s", T("RETURNS"), T("NEVER"));
		snprintf(row[rows++], size, "%s: %s", T("DELIVERY"), (spawn->mode.flags & FORGE_MODE_FLAG_DROPSHIP) ? T("DROPSHIP") :
			T("ON THE GROUND"));
		break;
	case _forge_mode_marker_base:
		snprintf(row[rows++], size, "%s: %c", T("LETTER"), 'A' + spawn->mode.index % FORGE_FILE_MAXIMUM_BASES);
		snprintf(row[rows++], size, "%s: %s", T("OWNER"), spawn->team == 0 ? T("RED") : spawn->team == 1 ? T("BLUE") :
			T("NONE"));
		snprintf(row[rows++], size, "%s: %s", T("HOME BASE"), (spawn->mode.flags & FORGE_MODE_FLAG_HOME) ? T("YES") : T("NO"));
		snprintf(row[rows++], size, "%s: %d %s", T("RADIUS"), (int)forge_mode_marker_radius(spawn), T("WU"));
		snprintf(row[rows++], size, T("CAPTURE TIME: %d S"), spawn->mode.seconds);
		break;
	case _forge_mode_marker_core:
		snprintf(row[rows++], size, "%s: %s", T("TEAM"), spawn->team == 0 ? T("RED") : T("BLUE"));
		snprintf(row[rows++], size, "%s: %d", T("HULL"), spawn->mode.points);
		break;
	case _forge_mode_marker_drop_point:
		snprintf(row[rows++], size, "%s: %s", T("TEAM"), spawn->team == 0 ? T("RED") : spawn->team == 1 ? T("BLUE") :
			spawn->team == FORGE_MODE_TEAM_HOSTILE ? T("HOSTILE") : T("ANY"));
		snprintf(row[rows++], size, "%s: %s", T("SHIP"), T(forge_mode_ship_names[spawn->variant % NUMBER_OF_FORGE_MODE_SHIPS]));
		snprintf(row[rows++], size, "%s: %d\xc2\xb0", T("YAW"), properties->yaw);
		break;
	case _forge_mode_marker_ff_objective:
		snprintf(row[rows++], size, "%s: %s", T("KIND"),
			T(forge_mode_objective_names[spawn->variant % NUMBER_OF_FORGE_MODE_OBJECTIVES]));
		forge_mode_round_text(row[rows++], size, T("ROUND"), spawn->mode.rounds, T("ALL"));
		if (spawn->variant != _forge_mode_objective_kill_boss)
		{
			snprintf(row[rows++], size, "%s: %d %s", T("RADIUS"), (int)forge_mode_marker_radius(spawn), T("WU"));
			snprintf(row[rows++], size, T("TIME: %d S"), spawn->mode.seconds);
		}
		if (spawn->variant == _forge_mode_objective_defend_object)
			snprintf(row[rows++], size, "%s: %d", T("HULL"), spawn->mode.points);
		break;
	case _forge_mode_marker_bonus_round:
		snprintf(row[rows++], size, "%s: %s", T("BONUS ROUND"), (spawn->mode.flags & FORGE_MODE_FLAG_OFF) ? T("OFF") : T("ON"));
		snprintf(row[rows++], size, "%s: %s", T("SQUAD"), T(forge_mode_squad_names[spawn->variant % NUMBER_OF_FORGE_MODE_SQUADS]));
		snprintf(row[rows++], size, "%s: %d", T("COUNT"), spawn->count);
		snprintf(row[rows++], size, "%s: %d %s", T("RADIUS"), (int)forge_mode_marker_radius(spawn), T("WU"));
		snprintf(row[rows++], size, T("TIME: %d S"), spawn->mode.seconds);
		snprintf(row[rows++], size, T("POINTS A KILL: %d"), spawn->mode.points);
		snprintf(row[rows++], size, "%s: %s", T("DELIVERY"), (spawn->mode.flags & FORGE_MODE_FLAG_DROPSHIP) ? T("DROPSHIP") :
			T("ON THE GROUND"));
		break;
	case _forge_mode_marker_boundary:
	{
		real along, across, up;

		forge_mode_boundary_extents(spawn, &along, &across, &up);
		snprintf(row[rows++], size, "%s: %s", T("KIND"), spawn->variant == FORGE_FILE_BOUNDARY_KILL ? T("KILL ZONE") : T("WALL"));
		snprintf(row[rows++], size, "%s: %.1f %s", T("LENGTH"), along * 2.f, T("WU"));
		snprintf(row[rows++], size, "%s: %.1f %s", T("WIDTH"), across * 2.f, T("WU"));
		snprintf(row[rows++], size, "%s: %.1f %s", T("HEIGHT"), up * 2.f, T("WU"));
		break;
	}
	case _forge_mode_marker_camera:
		snprintf(row[rows++], size, "%s: %d", T("PITCH"), (int)forge_mode_camera_pitch(spawn));
		break;
	}
	properties->marker_rows = rows;
}

/* a value stepped within minimum to maximum by step, wrapping */
static int forge_mode_step_within(
	int value,
	int minimum,
	int maximum,
	int by,
	short step)
{
	value += step > 0 ? by : -by;
	if (value > maximum)
		value = minimum;
	if (value < minimum)
		value = maximum;
	return value;
}

/* a radius in quarter units stepped a whole unit within its kind's range */
static unsigned char forge_mode_radius_step(
	unsigned char radius,
	int minimum,
	int maximum,
	short step)
{
	int units = forge_mode_step_within(radius / FORGE_FILE_MODE_RADIUS_UNITS, minimum, maximum, 1, step);

	return (unsigned char)(units * FORGE_FILE_MODE_RADIUS_UNITS);
}

/* the yaw stepped as a spawn point's is (forge_spawns.inc) */
static void forge_mode_marker_yaw_step(
	struct forge_spawn *spawn,
	short step,
	boolean held)
{
	forge_spawn_yaw_step(spawn, _forge_property_yaw, step, held);
	forge_mode_marker_log("properties of", spawn);
}

/* a marker's row stepped (its own rows: forge_mode_marker_properties_get):
the host's and a local game's */
static void forge_mode_marker_property_apply(
	struct forge_spawn *spawn,
	short property,
	short step,
	boolean held)
{
	boolean warzone = spawn->game_types == FORGE_FILE_GAME_TYPE_WARZONE;

	switch (spawn->kind)
	{
	case _forge_mode_marker_ai_zone:
		switch (property)
		{
		case 0: spawn->variant = (unsigned char)forge_mode_step_within(spawn->variant, 0, NUMBER_OF_FORGE_MODE_SQUADS - 1, 1, step); break;
		case 1: spawn->count = (unsigned char)forge_mode_step_within(spawn->count, 1, 8, 1, step); break;
		case 2: spawn->team = (signed char)forge_mode_step_within(spawn->team, -1, 1, 1, step); break;
		case 3: spawn->mode.radius = forge_mode_radius_step(spawn->mode.radius, 2, 16, step); break;
		case 4:
		{
			static unsigned short const modes[] = { 0, FORGE_FILE_GAME_TYPE_FIREFIGHT, FORGE_FILE_GAME_TYPE_WARZONE };
			short at = spawn->game_types == modes[1] ? 1 : spawn->game_types == modes[2] ? 2 : 0;

			spawn->game_types = modes[forge_mode_step_within(at, 0, 2, 1, step)];
			break;
		}
		case 5:
			if (warzone)
				spawn->mode.start_minute = (unsigned char)forge_mode_step_within(spawn->mode.start_minute, 0, 20, 1, step);
			else
				spawn->mode.rounds = forge_mode_mask_step(spawn->mode.rounds, 5, step);
			break;
		case 6:
			if (warzone)
			{
				short at = 0;
				short count = (short)NUMBEROF(forge_mode_wave_seconds);

				while (at < count && forge_mode_wave_seconds[at] != spawn->respawn_seconds)
					at++;
				at = at >= count ? 0 : (short)forge_mode_step_within(at, 0, count - 1, 1, step);
				spawn->respawn_seconds = forge_mode_wave_seconds[at];
			}
			else
				spawn->mode.waves = forge_mode_mask_step(spawn->mode.waves, 6, step);
			break;
		case 7: spawn->mode.index = (unsigned char)forge_mode_step_within(spawn->mode.index, 0, FORGE_FILE_MAXIMUM_BASES, 1, step); break;
		case 8: spawn->mode.flags ^= FORGE_MODE_FLAG_DROPSHIP; break;
		default: return;
		}
		break;
	case _forge_mode_marker_boss:
		switch (property)
		{
		case 0: spawn->variant = (unsigned char)forge_mode_step_within(spawn->variant, 0, NUMBER_OF_FORGE_MODE_BOSSES - 1, 1, step); break;
		case 1: spawn->mode.index = (unsigned char)forge_mode_step_within(spawn->mode.index, 0, FORGE_FILE_MODE_BOSS_NAMES - 1, 1, step); break;
		case 2: spawn->mode.points = (unsigned short)forge_mode_step_within(spawn->mode.points, 25, 500, FORGE_MODE_POINTS_STEP, step); break;
		case 3: spawn->mode.toughness = (unsigned char)forge_mode_step_within(spawn->mode.toughness, 1, 8, 1, step); break;
		case 4: spawn->team = (signed char)forge_mode_step_within(spawn->team, -1, 1, 1, step); break;
		case 5:
		{
			static unsigned short const modes[] = { 0, FORGE_FILE_GAME_TYPE_FIREFIGHT, FORGE_FILE_GAME_TYPE_WARZONE };
			short at = spawn->game_types == modes[1] ? 1 : spawn->game_types == modes[2] ? 2 : 0;

			spawn->game_types = modes[forge_mode_step_within(at, 0, 2, 1, step)];
			break;
		}
		case 6:
			if (warzone)
				spawn->mode.start_minute = (unsigned char)forge_mode_step_within(spawn->mode.start_minute, 0, 20, 1, step);
			else
				spawn->mode.rounds = forge_mode_mask_step(spawn->mode.rounds, 5, step);
			break;
		case 7:
		{
			/* (never, then after 60 to 600 s) */
			int seconds = (spawn->mode.flags & FORGE_MODE_FLAG_RETURNS) ? spawn->respawn_seconds : 0;

			seconds = forge_mode_step_within(seconds, 0, 600, FORGE_MODE_BOSS_RETURN_STEP, step);
			spawn->respawn_seconds = (unsigned short)seconds;
			spawn->mode.flags = (unsigned char)(seconds ? spawn->mode.flags | FORGE_MODE_FLAG_RETURNS :
				spawn->mode.flags & ~FORGE_MODE_FLAG_RETURNS);
			break;
		}
		case 8: spawn->mode.flags ^= FORGE_MODE_FLAG_DROPSHIP; break;
		default: return;
		}
		break;
	case _forge_mode_marker_base:
		switch (property)
		{
		case 0:
		{
			/* (the next letter no other base has) */
			unsigned short taken = forge_mode_base_letters(spawn);
			short tries;
			int letter = spawn->mode.index;

			for (tries = 0; tries < FORGE_FILE_MAXIMUM_BASES; tries++)
			{
				letter = forge_mode_step_within(letter, 0, FORGE_FILE_MAXIMUM_BASES - 1, 1, step);
				if (!(taken & (1 << letter)))
					break;
			}
			if (!(taken & (1 << letter)))
				spawn->mode.index = (unsigned char)letter;
			break;
		}
		case 1:
			spawn->team = (signed char)forge_mode_step_within(spawn->team, -1, 1, 1, step);
			/* (nobody's base is nobody's home) */
			if (spawn->team == NONE)
				spawn->mode.flags &= (unsigned char)~FORGE_MODE_FLAG_HOME;
			break;
		case 2:
			if (spawn->team == 0 || spawn->team == 1)
				spawn->mode.flags ^= FORGE_MODE_FLAG_HOME;
			else
				forge_notice("%s", T("A home base needs an owner: red or blue."));
			break;
		case 3: spawn->mode.radius = forge_mode_radius_step(spawn->mode.radius, 3, 20, step); break;
		case 4: spawn->mode.seconds = (unsigned short)forge_mode_step_within(spawn->mode.seconds, 5, 30, 5, step); break;
		default: return;
		}
		break;
	case _forge_mode_marker_core:
		switch (property)
		{
		case 0:
		{
			signed char other = (signed char)(spawn->team == 0 ? 1 : 0);

			if (forge_mode_core_of_team(other, spawn))
				forge_notice("%s", T("That team has its core already."));
			else
				spawn->team = other;
			break;
		}
		case 1: spawn->mode.points = (unsigned short)forge_mode_step_within(spawn->mode.points, 1000, 10000, FORGE_MODE_HULL_STEP, step); break;
		default: return;
		}
		break;
	case _forge_mode_marker_drop_point:
		switch (property)
		{
		case 0: spawn->team = (signed char)forge_mode_step_within(spawn->team, -1, FORGE_MODE_TEAM_HOSTILE, 1, step); break;
		case 1: spawn->variant = (unsigned char)forge_mode_step_within(spawn->variant, 0, NUMBER_OF_FORGE_MODE_SHIPS - 1, 1, step); break;
		case 2: forge_mode_marker_yaw_step(spawn, step, held); return;
		default: return;
		}
		break;
	case _forge_mode_marker_ff_objective:
		switch (property)
		{
		case 0:
			/* (KILL THE BOSS no more: Firefight has no bosses - Firefight
			round 3; a file's own still read) */
			spawn->variant = (unsigned char)forge_mode_step_within(spawn->variant, 0, NUMBER_OF_FORGE_MODE_OBJECTIVES - 1, 1, step);
			if (spawn->variant == _forge_mode_objective_kill_boss)
				spawn->variant = (unsigned char)forge_mode_step_within(spawn->variant, 0, NUMBER_OF_FORGE_MODE_OBJECTIVES - 1, 1, step);
			/* (each kind's own fields, as forge_files.h checks them) */
			spawn->mode.radius = spawn->variant == _forge_mode_objective_kill_boss ? 0 :
				spawn->mode.radius ? spawn->mode.radius : 5 * FORGE_FILE_MODE_RADIUS_UNITS;
			spawn->mode.seconds = spawn->variant == _forge_mode_objective_kill_boss ? 0 :
				spawn->mode.seconds ? spawn->mode.seconds : 60;
			spawn->mode.points = spawn->variant == _forge_mode_objective_defend_object ? 2000 : 0;
			break;
		case 1: spawn->mode.rounds = forge_mode_mask_step(spawn->mode.rounds, 5, step); break;
		case 2:
			if (spawn->variant != _forge_mode_objective_kill_boss)
				spawn->mode.radius = forge_mode_radius_step(spawn->mode.radius, 2, 20, step);
			break;
		case 3:
			if (spawn->variant != _forge_mode_objective_kill_boss)
				spawn->mode.seconds = (unsigned short)forge_mode_step_within(spawn->mode.seconds, 30, 300, 30, step);
			break;
		case 4:
			if (spawn->variant == _forge_mode_objective_defend_object)
				spawn->mode.points = (unsigned short)forge_mode_step_within(spawn->mode.points, 500, 10000, FORGE_MODE_HULL_STEP, step);
			break;
		default: return;
		}
		/* (a boss kill's rows: kind, round only) */
		if (spawn->variant == _forge_mode_objective_kill_boss)
		{
			spawn->mode.radius = 0;
			spawn->mode.seconds = 0;
			spawn->mode.points = 0;
		}
		break;
	case _forge_mode_marker_bonus_round:
		switch (property)
		{
		case 0: spawn->mode.flags ^= FORGE_MODE_FLAG_OFF; break;
		case 1: spawn->variant = (unsigned char)forge_mode_step_within(spawn->variant, 0, NUMBER_OF_FORGE_MODE_SQUADS - 1, 1, step); break;
		case 2: spawn->count = (unsigned char)forge_mode_step_within(spawn->count, 1, 8, 1, step); break;
		case 3: spawn->mode.radius = forge_mode_radius_step(spawn->mode.radius, 2, 16, step); break;
		case 4:
			spawn->mode.seconds = (unsigned short)forge_mode_step_within(spawn->mode.seconds, FORGE_FILE_BONUS_MINIMUM_SECONDS,
				FORGE_FILE_BONUS_MAXIMUM_SECONDS, FORGE_MODE_BONUS_SECONDS_STEP, step);
			break;
		case 5: spawn->mode.points = (unsigned short)forge_mode_step_within(spawn->mode.points, 25, 500, FORGE_MODE_POINTS_STEP, step); break;
		case 6: spawn->mode.flags ^= FORGE_MODE_FLAG_DROPSHIP; break;
		default: return;
		}
		break;
	case _forge_mode_marker_boundary:
		/* (its extents step a world unit, 1/2 to 500 along and across, 1/2
		to 63 up: quarter units in the fields) */
		switch (property)
		{
		case 0: spawn->variant = (unsigned char)forge_mode_step_within(spawn->variant, 0, NUMBER_OF_FORGE_FILE_BOUNDARY_KINDS - 1, 1, step); break;
		case 1: spawn->mode.points = (unsigned short)PIN((int)spawn->mode.points + step * FORGE_FILE_MODE_RADIUS_UNITS, 2, FORGE_FILE_BOUNDARY_MAXIMUM_HALF_UNITS); break;
		case 2: spawn->mode.seconds = (unsigned short)PIN((int)spawn->mode.seconds + step * FORGE_FILE_MODE_RADIUS_UNITS, 2, FORGE_FILE_BOUNDARY_MAXIMUM_HALF_UNITS); break;
		case 3: spawn->mode.radius = (unsigned char)PIN((int)spawn->mode.radius + step * FORGE_FILE_MODE_RADIUS_UNITS, 2, 252); break;
		default: return;
		}
		break;
	case _forge_mode_marker_camera:
		/* (its pitch, 5 degrees a step, straight down to straight up) */
		if (property != 0)
			return;
		spawn->mode.radius = (unsigned char)PIN((int)spawn->mode.radius + step * 5, 0, 2 * FORGE_FILE_CAMERA_PITCH_OFFSET);
		break;
	default:
		return;
	}
	forge_mode_marker_log("properties of", spawn);
}

/* (the browser's OBJECTIVES, after its game type objects) a marker of the kind placed in front of the
camera, facing the way it looks, and held; a network game's client places
none (the host keeps them: until the modes' network step) */
static void forge_mode_marker_choose(
	short kind)
{
	real_point3d position;
	real_vector3d forward;
	struct forge_transform home;
	struct forge_spawn *spawn;
	real distance;

	if (forge_net_client())
	{
		forge_notice("%s", T("Only the host places objectives in a network game."));
		return;
	}
	if (!forge_camera_get(&position, &forward))
		return;
	if (forge_objects_counted() >= FORGE_MAXIMUM_OBJECTS)
	{
		forge_notice(T("Budget full: %d objects. Delete one to place another."), FORGE_MAXIMUM_OBJECTS);
		return;
	}
	if (forge_mode_markers_count() >= FORGE_MAXIMUM_MODE_MARKERS || forge_mode_markers_of_kind(kind) >= forge_mode_marker_most(kind))
	{
		char name[FORGE_NAME_SIZE];

		snprintf(name, sizeof(name), "%s", T(forge_mode_marker_names[(kind - FIRST_FORGE_MODE_MARKER) % NUMBER_OF_FORGE_MODE_MARKERS]));
		forge_notice(T("No room for another %s: %d at most."), name,
			forge_mode_markers_count() >= FORGE_MAXIMUM_MODE_MARKERS ? FORGE_MAXIMUM_MODE_MARKERS : forge_mode_marker_most(kind));
		return;
	}
	distance = MIN(4.f, forge_structure_distance(&position, &forward) - 1.f);
	distance = MAX(distance, FORGE_MINIMUM_HOLD_DISTANCE);
	home.position.x = position.x + forward.i * distance;
	home.position.y = position.y + forward.j * distance;
	home.position.z = position.z + forward.k * distance;
	forge_spawn_grounded(&home.position);
	home.forward = forward;
	home.forward.k = 0.f;
	if (normalize3d(&home.forward) < 0.0001f)
		home.forward = *global_forward3d;
	home.up = *global_up3d;
	spawn = forge_mode_marker_add(0, kind, &home, NULL);
	if (!spawn)
		return;
	forge_mode_marker_log("placed", spawn);
	forge_globals.held_marker_id = spawn->id;
	forge_globals.held_marker_position = spawn->home.position;
	forge_globals.held_forward = spawn->home.forward;
	forge_globals.held_up = *global_up3d;
	forge_globals.held_distance = distance;
	{
		char name[FORGE_NAME_SIZE];

		forge_mode_marker_name(spawn, name, sizeof(name));
		forge_notice(T("Placed %s"), name);
	}
}

/* (forge_variant_capture, a save) the markers as version 5's records after
the spawn points', and the modes' settings; how many could not be (the file
full) */
static short forge_mode_markers_capture(
	struct forge_file_layout *layout)
{
	short index;
	short skipped = 0;
	short taken = 0;

	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];
		struct forge_file_record *record;
		float angles[3];
		short axis;

		if (!forge_kind_is_mode_marker(spawn->kind))
			continue;
		for (axis = 0; axis < 3; axis++)
		{
			real coordinate = axis == 0 ? spawn->home.position.x : axis == 1 ? spawn->home.position.y : spawn->home.position.z;

			if (!(coordinate > -FORGE_FILE_MAXIMUM_COORDINATE + 1.f && coordinate < FORGE_FILE_MAXIMUM_COORDINATE - 1.f))
				break;
		}
		if (axis < 3 || layout->record_count >= FORGE_FILE_MAXIMUM_RECORDS)
		{
			skipped++;
			continue;
		}
		record = &layout->records[layout->record_count];
		csmemset(record, 0, sizeof(*record));
		record->path_index = FORGE_FILE_NO_PATH;
		record->kind = (unsigned char)(_forge_record_ai_zone + (spawn->kind - FIRST_FORGE_MODE_MARKER));
		record->team = spawn->team;
		record->variant = spawn->variant;
		record->count = spawn->count;
		record->respawn_seconds = spawn->respawn_seconds;
		record->game_types = spawn->game_types;
		record->mode = spawn->mode;
		record->position[0] = spawn->home.position.x;
		record->position[1] = spawn->home.position.y;
		record->position[2] = spawn->home.position.z;
		forge_angles_from_facing(&spawn->home.forward, global_up3d, angles);
		record->angles[0] = angles[0];
		layout->record_count++;
		layout->flags |= FORGE_FILE_FLAG_NETGAME_SPAWNS;
		taken++;
	}
	if (taken)
	{
		layout->modes = forge_globals.mode_settings;
		layout->flags |= FORGE_FILE_FLAG_MODE_SETTINGS;
	}
	return skipped;
}

/* (the map loaded for a Forge game or a map variant played, after the
spawn points) the layout's markers and settings: a version 5 file's */
static void forge_mode_markers_from_layout(
	struct forge_file_layout const *layout)
{
	short index;
	short taken = 0;

	if (forge_file_layout_version(layout) < 5)
		return;
	if (layout->flags & FORGE_FILE_FLAG_MODE_SETTINGS)
		forge_globals.mode_settings = layout->modes;
	for (index = 0; index < layout->record_count && index < FORGE_FILE_MAXIMUM_RECORDS; index++)
	{
		struct forge_file_record const *record = &layout->records[index];
		struct forge_transform home;
		float angles[3];

		if (!forge_file_kind_is_mode_marker(record->kind))
			continue;
		home.position.x = record->position[0];
		home.position.y = record->position[1];
		home.position.z = record->position[2];
		angles[0] = record->angles[0];
		angles[1] = angles[2] = 0.f;
		forge_facing_from_angles(angles, &home.forward, &home.up);
		if (forge_mode_marker_add(0, (short)(FIRST_FORGE_MODE_MARKER + (record->kind - _forge_record_ai_zone)), &home, record))
			taken++;
	}
	if (taken)
		platform_log("forge: map variant \"%s\": %d game mode markers", layout->title, taken);
}
