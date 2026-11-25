/*
RAGDOLLS.C

Ragdolls (display.ragdolls, HALO_RAGDOLLS; the Vita's settings panel:
Graphics > Detail > Ragdolls): a character killed goes limp and falls as a
body falls - knocked by the blow that killed it, its arms and legs thrown
apart by a grenade's blast - and comes to rest on the ground, the steps or
the rocks it lands on, instead of its death animation's last pose laid onto
the ground (the game's own limp body, biped_limp_noodle.c).

Only the picture. A ragdoll is a pose drawn for a dead body: the body
itself - its place, its node matrices, its hit boxes and markers, what
projectiles meet - is the game's as ever, and nothing here is game state:
nothing goes over the network, into a checkpoint or a saved game, nothing
draws from the game's random numbers or its memory. Each machine of a
network game (co-op, multiplayer, crossplay) simulates its own. The tick
hash proves it: a run with ragdolls and one without keep the same game
state, tick for tick (the commit's message).

The idea is jojoxdk's "Halo 1 MCC Box3D Physics Mod" (MIT), which gives
MCC's Halo ragdolls with Erin Catto's Box3D: its joint types and limits come
from the animation graph's limp body nodes (ball and socket, hinge, no
movement, and their ranges), knees and elbows bend as far as its hinges let
them (150 and 145 degrees), hips 45 degrees, a body thrown by the blow that
killed it. Nothing of its code or its rig files is used (its rigs are taken
from the game's tags; these are made here, at run time, from each model's
own nodes). The solver is not Box3D but a small position-based one written
for the Vita: Box3D is some 60,000 lines of C, its SIMD for 64-bit ARM only
(the Vita's 32-bit A9 gets its scalar path), and would need the structure
BSP turned into a mesh of its own (the mod rebuilds one at every BSP switch
and breakable surface); a ragdoll here is a few dozen particles and
constraints, and meets the world through the game's own collision tests.

Which bodies: those the game itself lays limp, by their biped's own flag
(uses limp body physics): the marines, crewmen, the Master Chief, the
Covenant, the Flood's combat forms; not the cinematics' characters,
Cortana, the Monitor, sentinels, infection forms or the scripts' wounded
marines. A Custom Edition map's bipeds as their maker set them.

The rig, made when a body dies from its model's nodes (no game file or
data is shipped):
- a particle at each node that is not a finger, a gun, an eye or the like,
  and lies at least 2.5 cm (0.025 world units, a world unit 10 feet) from
  its nearest particle ancestor: the pelvis, spine, neck, head, clavicles,
  upper arms, forearms, hands, thighs, calves, feet of the game's
  characters (19 on the Master Chief and the marines, 21 on an Elite);
- the particles of a node with two or more particle children (the pelvis
  with the spine and thighs, the chest with the neck and clavicles) and its
  parent's are held rigid together by shape matching (the shape of the body
  at its death, found again every step with the rotation that best fits it:
  Mueller et al., "Meshless deformations based on shape matching" 2005,
  "A robust method to extract the rotational part of deformations" 2016);
- every other particle is held at its distance from its parent particle;
- every joint is limited as the mod limits it: the bone below a joint stays
  within a cone about its direction in the model's default pose, measured
  from the rigid part above it (the distances to that part's particles
  kept between those the cone allows), its half-angle the animation graph
  node's range (halved, 6 to 90 degrees) or the mod's for that joint;
- the knees, an Elite's and a Jackal's horselinks and the elbows are
  hinges: the bone below kept within 10 degrees of the plane square to the
  joint node's up axis (as it stood at the death, turned with the bone
  above) and bent only the way it bends - every character's dying pose
  bends a knee or an elbow toward its node's -up, a horselink toward +up -,
  straight at most (5 degrees over);
- a node's pose is its pose at death turned as its part has turned since
  (the rigid parts by their fitted rotation, a bone by the turn that takes
  its direction then to its direction now, carried from its parent's), and
  the nodes without a particle carried rigidly by their parents.

Each game tick (30 a second) a ragdoll is stepped twice, four rounds of
its constraints each (Verlet integration, the game's gravity), and its
particles meet the structure BSP: a particle that moved is tested with one
collision vector from where it stood at the tick's start to where it ends
(its radius further), and stopped at the surface with Coulomb friction; the
planes it met are kept to every step without a test while it touches them;
a particle put out of one surface into another (a corner) goes back to
where it stood. The particles of a rigid part near its centre (the hips by
the pelvis, the clavicles and neck by the chest) get a vector only once a
point test finds them in solid structure: the centre's sphere stands for
them.
The particles' radius is the animation graph's limp body node radius (3 cm
by default), one and a half that for the pelvis, chest and head.

The death, as Halo 2 and Halo 3 die: the body plays its own death
animation first (the one the game picks for the blow's direction and the
part hit, as ever) and goes limp partway through it. unit_died (not a
feigned one) names the body, and each tick after that its animation is
watched (ragdoll_watch) for the moment the ragdoll takes over:
- a body that a big blow (an explosion, a melee blow, a vehicle's hit or a
  shot that pushes harder than 4 m/s) throws into the air (rising faster
  than 1.5 m/s, or off the ground), or that dies in the air, flies on the
  game's own path playing the game's airborne death ("airborne-dead"), and
  the ragdoll takes over as it meets the ground or a wall (any joint of it
  about to pass into the level the next tick), or when the game lands it
  (in place of "landing-dead");
- at once, the tick after the death, for a big blow that leaves the body on
  its feet and for one with no death animation; a body thrown that has no
  airborne death (its biped's "has no dying airborne", or the game never
  plays one within eight ticks) goes limp at once too;
- else at the animation's key frame where the animation has one; when its
  trunk (root to head) leans 30 degrees further from upright than at the
  death (the body falling, not one sinking to its knees); when its head,
  spine or arms would pass into the level the next tick (a fall into a wall
  or onto the ground); four fifths through the animation at the latest.
A ragdoll that takes over from an animation starts from the pose the body
had the tick before, each particle moving as the animation moved its node
(after a landing, the whole body moving as it last moved), so the tick's
two steps land it on the pose the game has now (no pop), and for its first
eight ticks it is pulled toward the animation's pose as that goes on - half
the way at first, less each tick; a particle a wall or the ground stopped
well short of it is left alone - then falls limp (ragdoll_step's follow).
One that starts at once starts from its pose then, moving as the body moves
(the game has already thrown it with the blow's acceleration) but no faster
than 6 m/s - Halo's blasts throw a body at 15 m/s and more, 20 m and
further, a ragdoll 3 or 4 m; a local player's own, which the camera
follows, as the game threw it -, and the blow that killed it
(object_damage_aftermath) adds to that: an explosion throws each particle
away from its centre, the nearest the most; a shot or a blow pushes the
particle nearest its line (at least 3.5 m/s) and the one that kills knocks
the whole body back (3.5 to 6 m/s); a melee blow (the damage category
melee: a punch, a rifle butt, a plasma sword) throws the whole body along it
at 12 m/s or more, a little upward, the part nearest the line hardest.
The watch reads the body's animation state, flags, node matrices and the
level's collision (point tests), and writes only its own copy of the pose.
Ported from the same behaviour in ChupathingyCE's Jolt ragdolls (the
death-animation hand-off and the airborne landing), here on this file's
own solver. Where the game
has the body (thrown further) does not bound the ragdoll: the render finds a
ragdoll in the cluster it lies in (render_ragdolls_find), not only in the
clusters the game has its body in; past 150 m from it, it is let go.

The budget, for the Vita's tick: four ragdolls awake at most
(HALO_RAGDOLLS_ACTIVE), twelve kept (asleep, their pose drawn as they came
to rest). A ragdoll falls asleep when no particle has moved more than 1.5
mm a tick for half a second, or after ten seconds awake, and costs nothing
more but the copy of its pose; a new death past four awake puts the one
awake longest to sleep; past twelve kept, the one not drawn for longest is
let go (its body drawn as the game lays it again). A blow that strikes a
sleeping ragdoll wakes it.

The threads: the ragdolls are stepped on the tick's thread after each game
tick (game_time.c), read only the game's state and write only their own.
The render draws them through the tick's poses (render_interpolation.c):
the tick copies each ragdoll's pose over its body's as it copies the poses
the next frame draws, so the frame drawn while the next tick runs draws
the pose of the tick it shows, with or without frame interpolation; a
frame drawn with no tick running (no thread steps the ragdolls then) reads
the pose here. A checkpoint revert, a saved game or a new map lets every
ragdoll go (halo_map_generation, ragdolls_reset).

HALO_RAGDOLLS_LOG=1: a line each time a ragdoll starts, sleeps, wakes or is
let go and why, and its cost every ten seconds while any is kept; =2 each
ragdoll's rig too; =3 every tenth tick of each awake one, its particle that
moved most. main.c's "@ragdoll ..." test commands
(port/vita/tests/run_ragdolls_test.sh): "boom [count] [range]" a grenade's
explosion beside the nearest living characters, "punch [count] [range]" a
melee blow from the player that kills each, "shoot [count] [range]" an
assault rifle bullet from the player that kills each, "kill [count] [range]"
kills them outright, "stats" and "check" log what the ragdolls are and cost,
"where" where each is.
*/

#include "cseries.h"
#include "real_math.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "game/game_globals.h"
#include "objects/damage.h"
#include "objects/damage_effect_definitions.h"
#include "models/model_definitions.h"
#include "models/model_animation_definitions.h"
#include "units/units.h"
#include "units/bipeds.h"
#include "units/biped_definitions.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "game/players.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "cache/cache_files.h"
#include "render_epoch.h"
#include "camera/director.h"

#include "ragdolls.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* port/linux/src/port_config.c */
int config_boolean(const char *name);
extern volatile unsigned long halo_settings_generation;
void platform_log(const char *format, ...);
/* the host's clock, microseconds */
unsigned long long vita_host_time_us(void);
/* physics.c: world units a tick, each tick */
extern real global_gravity;
/* models.c */
void model_get_node_matrices(struct model const *model, real_matrix4x3 *node_matrices, real_point3d const *origin,
	real_vector3d const *forward, real_vector3d const *up);
/* damage.c's */
void damage_kill_object_for_player(long object_index, long player_index);
/* (C99's, which the game's gnu89 headers leave undeclared) */
float sqrtf(float x);
float sinf(float x);
float cosf(float x);
float acosf(float x);
float fabsf(float x);

/* ---------- constants */

enum
{
	/* bodies whose pose is a ragdoll's, awake or asleep */
	RAGDOLL_SLOTS = 12,
	/* awake at once, by default (HALO_RAGDOLLS_ACTIVE) */
	RAGDOLL_ACTIVE_DEFAULT = 4,
	RAGDOLL_MAXIMUM_NODES = MAXIMUM_NODES_PER_MODEL,
	RAGDOLL_MAXIMUM_PARTICLES = 24,
	RAGDOLL_MAXIMUM_CONSTRAINTS = 112,
	RAGDOLL_MAXIMUM_CLUSTERS = 6,
	RAGDOLL_CLUSTER_MEMBERS = 6,
	/* the particles of the rigid part above a joint its cone is kept from */
	RAGDOLL_MAXIMUM_SHELL = 4,
	RAGDOLL_PLANES = 2,
	RAGDOLL_PENDING = 16,
	RAGDOLL_DAMAGE_NOTES = 16,
	RAGDOLL_SUBSTEPS = 2,
	RAGDOLL_ITERATIONS = 4,
	/* the fewest particles a rig may have (a sentinel's, an infection
	form's skeleton is no body) */
	RAGDOLL_MINIMUM_PARTICLES = 8,

	/* a body dead in a vehicle's seat waits this many ticks to fall out, or
	for room among the awake (from when its ragdoll is due) */
	RAGDOLL_PENDING_TICKS = 90,
	/* the death animation awaited this many ticks after the death (the
	biped's update picks it the tick after the blow, or the same tick) */
	RAGDOLL_ANIMATION_WAIT_TICKS = 3,
	/* the joints watched for a fall into the level (the upper body's; a body
	thrown, all of them) */
	RAGDOLL_IMPACT_NODES = 24,
	/* a body thrown: this long for the game to play its airborne death (it
	does after three ticks in the air), and at most this long in the air */
	RAGDOLL_LIFT_TICKS = 8,
	RAGDOLL_FLIGHT_TICKS_MOST = 300,
	/* a blow is the death's if it came this many ticks before it */
	RAGDOLL_NOTE_TICKS = 3,
	/* still this long, asleep (after at least the second) */
	RAGDOLL_SLEEP_TICKS = 15,
	RAGDOLL_SLEEP_AGE = 20,
	/* awake this long at most */
	RAGDOLL_AWAKE_TICKS = 300,
	/* the game's body still thrown about this long after the death: its
	moves are not the end of a ragdoll asleep */
	RAGDOLL_FLIGHT_TICKS = 150,
	/* ragdolls started a tick at most */
	RAGDOLL_STARTS_PER_TICK = 2,
	/* awake this long at least before put to sleep for another, and still
	this long */
	RAGDOLL_ROOM_TICKS = 30,
	RAGDOLL_ROOM_STILL_TICKS = 3,
	/* a ragdoll that takes over from a death animation follows (is pulled
	toward) its pose this many ticks */
	RAGDOLL_FOLLOW_TICKS = 8,
	/* a remembered plane, kept this many ticks without touching it */
	RAGDOLL_PLANE_TICKS = 20,
	/* kept, not drawn for this many frames: let go first */
	RAGDOLL_UNSEEN_FRAMES = 30,
};

enum
{
	_ragdoll_free = 0,
	_ragdoll_awake,
	_ragdoll_asleep,
};

/* why a ragdoll was let go (the log's words) */
enum
{
	_ragdoll_released_gone,
	_ragdoll_released_alive,
	_ragdoll_released_attached,
	_ragdoll_released_changed,
	_ragdoll_released_setting,
	_ragdoll_released_map,
	_ragdoll_released_not_a_number,
	_ragdoll_released_far,
	_ragdoll_released_replaced,
	_ragdoll_released_body_moved,
	NUMBER_OF_RAGDOLL_RELEASE_REASONS
};

static char const *const ragdoll_release_names[NUMBER_OF_RAGDOLL_RELEASE_REASONS] =
{
	"gone", "alive again", "attached", "its model changed", "the setting off", "the game state replaced",
	"not a number", "too far from its body", "replaced", "its body moved",
};

/* animation graph nodes' joint flags (biped_limp_noodle.c's) */
enum
{
	_ragdoll_joint_ball_and_socket_bit = 0,
	_ragdoll_joint_hinge_bit,
	_ragdoll_joint_no_movement_bit,
};

/* the shortest bone a particle stands for (world units: 2.5 cm) */
#define RAGDOLL_MINIMUM_BONE 0.025f
/* the limp body node radius when the graph gives none, and its bounds */
#define RAGDOLL_DEFAULT_RADIUS 0.03f
#define RAGDOLL_MAXIMUM_RADIUS 0.07f
/* air drag a step (of 1: none; the game's bodies fly without any) */
#define RAGDOLL_DAMPING 0.999f
/* a hinge's child out of its plane by more than this share of its bone
(sin 10 degrees), and bent the wrong way past this share (sin 5) */
#define RAGDOLL_HINGE_SLACK 0.17f
#define RAGDOLL_HINGE_OVER 0.087f
/* the most a body's own motion starts its ragdoll with (6 m/s: thrown 3
or 4 m, the length of a body or two; Halo's blasts throw its bodies at 15
m/s and more, 20 m and further), world units a tick; a local player's
own body, which the camera follows, as the game threw it */
#define RAGDOLL_LAUNCH_SPEED (6.0f / 3.048f / TICKS_PER_SECOND)
/* friction against the world, static and kinetic (a body sticks on slopes
up to about 40 degrees) */
#define RAGDOLL_STATIC_FRICTION 0.85f
#define RAGDOLL_KINETIC_FRICTION 0.6f
/* moved less than this in a tick (1.5 mm): still */
#define RAGDOLL_STILL 0.0005f
/* moved less than this, against a plane met: not tested again */
#define RAGDOLL_RESTING 0.0008f
/* nearer a plane than its radius and this, a particle touches it */
#define RAGDOLL_TOUCHING 0.002f
/* a plane is forgotten once the particle is this far from where it met it */
#define RAGDOLL_PLANE_REACH 0.12f
/* how hard a ragdoll that took over from a death animation follows it at
first (this share of the way each of a tick's two steps: about half the way
a tick, less each tick after), and how far from the
animation's node a particle may be pulled from (world units: 0.6 m; one
further off, stopped by a wall or the ground, is left alone) */
#define RAGDOLL_FOLLOW_STRENGTH 0.3f
#define RAGDOLL_FOLLOW_REACH 0.2f
/* the death animation: the ragdoll takes over this far through it at the
latest, or when the trunk leans this much further from upright than at the
death (radians: 30 degrees) */
#define RAGDOLL_HANDOFF_FRACTION 0.8f
#define RAGDOLL_HANDOFF_LEAN 0.5236f
/* a blow this hard (4 m/s), world units a tick: no death animation */
#define RAGDOLL_BIG_BLOW (4.0f / 3.048f / TICKS_PER_SECOND)
/* a blow throws the body into the air when it leaves it rising this fast
(1.5 m/s), world units a tick, or off the ground */
#define RAGDOLL_LIFT (1.5f / 3.048f / TICKS_PER_SECOND)
/* the most a particle moves as the animation moved its node when the
ragdoll takes over (10 m/s: a hard kill's animation throws a marine back
that fast), world units a tick */
#define RAGDOLL_ANIMATED_SPEED (10.0f / 3.048f / TICKS_PER_SECOND)
/* a watched joint that moved less than this in a tick (1 mm) is not tested
against the level (it cannot pass into it) */
#define RAGDOLL_WATCH_MOVED 0.0003f
/* a shot's least push (3.5 m/s; the mod's 1.7 barely moved a limb), world
units a tick */
#define RAGDOLL_SHOT_SPEED (3.5f / 3.048f / TICKS_PER_SECOND)
/* the shot that kills knocks the whole body back along it, a little
upward: at least 3.5 m/s, at most 6 (a shotgun's or a sniper's own push) */
#define RAGDOLL_SHOT_KNOCKBACK (3.5f / 3.048f / TICKS_PER_SECOND)
#define RAGDOLL_SHOT_KNOCKBACK_SCALE 0.5f
/* a melee blow's least push (12 m/s: a Spartan's punch throws a grunt
some way), world units a tick: the whole body thrown along it (the damage
category melee, ai.c's _damage_category_melee) */
#define RAGDOLL_MELEE_SPEED (12.0f / 3.048f / TICKS_PER_SECOND)
#define RAGDOLL_DAMAGE_CATEGORY_MELEE 6
/* the most an impulse adds to a particle (8 m/s; the mod's 12 throws the
whole body, which the game has already thrown here); a melee blow's whole
body throw, 14 */
#define RAGDOLL_MAXIMUM_IMPULSE (8.0f / 3.048f / TICKS_PER_SECOND)
#define RAGDOLL_MELEE_MAXIMUM (14.0f / 3.048f / TICKS_PER_SECOND)
/* a ragdoll this far from its body's root node is let go (world units:
150 m; a blast throws the game's body 40 m and more, its ragdoll a few:
only a body put elsewhere) */
#define RAGDOLL_TETHER 50.0f
/* asleep, its body moved this far by the game: let go (world units: 6 m) */
#define RAGDOLL_BODY_MOVED 2.0f
/* the drawn sphere: around the particles, this much more */
#define RAGDOLL_SPHERE_MARGIN 0.12f

/* the vector tests' surfaces: the structure, seen from the front, not its
invisible ones (player clipping) */
#define RAGDOLL_COLLISION_FLAGS \
	(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) | \
	FLAG(_collision_test_ignore_invisible_surfaces_bit))
#define RAGDOLL_SOLID_FLAGS FLAG(_collision_test_structure_bit)

#define RAGDOLL_DEGREES(d) ((d) * 0.017453292f)

/* ---------- structures */

/* biped_limp_noodle.c's: an animation graph's node, its joint's type and
range */
struct ragdoll_graph_node
{
	char name[TAG_STRING_LENGTH + 1];
	short next_sibling_node_index;
	short first_child_node_index;
	short parent_node_index;
	word pad;
	unsigned long flags;
	real_vector3d base_vector;
	real range;
	long pad1;
};

/* a rotation: the images of the three axes */
struct ragdoll_rotation
{
	real_vector3d axes[3];
};

/* a surface a particle met, kept to without a test a while */
struct ragdoll_plane
{
	real_vector3d normal;
	real d;
	real_point3d at;
	short ticks;
};

struct ragdoll_particle
{
	real_point3d position;
	real_point3d previous;
	/* where it stood at the tick's start (the collision test's start) */
	real_point3d start;
	/* (following the death animation) where the animation had its node
	at the tick's start */
	real_point3d animated;
	real inverse_mass;
	real radius;
	short node_index;
	/* its parent particle (NONE: the root), its one child (a bone: NONE
	when it has none or more than one), how many it has, the cluster it is
	the centre of (NONE: none) */
	short parent;
	short child;
	short child_count;
	short cluster;
	/* a knee or an elbow: it bends one way only, about this axis (as it
	stood at the death, carried by its parent's turn), its child on the
	side cross(parent to it, it to its child) . axis > 0 */
	boolean hinge;
	real_vector3d hinge_axis;
	/* how far the step's rounds pushed it out of the planes it touches
	(its friction's measure, ragdoll_plane_keep) */
	real pushed;
	/* its turn is wanted while stepping (a hinge's parent, and theirs up
	to a rigid part's centre) */
	boolean turned;
	/* it meets the world with a vector each tick it moves (one near the
	centre of its rigid part only once its centre has gone into solid
	structure: the centre's sphere stands for it) */
	boolean collides;
	/* toward its child at the death (a bone) */
	real_vector3d handoff_direction;
	/* the turn since the death */
	struct ragdoll_rotation delta;
	struct ragdoll_plane planes[RAGDOLL_PLANES];
};

/* two particles' distance kept between minimum and maximum */
struct ragdoll_constraint
{
	unsigned char a, b;
	real minimum, maximum;
};

/* particles held rigid in their shape at the death */
struct ragdoll_cluster
{
	short count;
	short centre;
	unsigned char members[RAGDOLL_CLUSTER_MEMBERS];
	real_vector3d rest[RAGDOLL_CLUSTER_MEMBERS];
	/* the rotation since the death (w, i, j, k), kept from step to step */
	real quaternion[4];
	boolean warm;
};

struct ragdoll
{
	long object_index;
	unsigned long map_generation;
	short state;
	short node_count, particle_count, constraint_count, cluster_count, hinge_count;
	short still_ticks;
	short follow_ticks;
	long age;
	long awake_ticks;
	/* the order it started in, and was last woken in (the oldest is put to
	sleep, the least seen let go) */
	unsigned long started;
	unsigned long woken;
	struct ragdoll_particle particles[RAGDOLL_MAXIMUM_PARTICLES];
	struct ragdoll_constraint constraints[RAGDOLL_MAXIMUM_CONSTRAINTS];
	struct ragdoll_cluster clusters[RAGDOLL_MAXIMUM_CLUSTERS];
	/* nodes, parents before children */
	short node_order[RAGDOLL_MAXIMUM_NODES];
	short node_parent[RAGDOLL_MAXIMUM_NODES];
	signed char node_particle[RAGDOLL_MAXIMUM_NODES];
	/* a particle's node: its matrix at the death; any other: its matrix in
	its parent's */
	real_matrix4x3 local[RAGDOLL_MAXIMUM_NODES];
	/* the pose drawn */
	real_matrix4x3 pose[RAGDOLL_MAXIMUM_NODES];
	real_point3d center;
	real radius;
	/* where its body was the last tick it was awake (or, the first five
	seconds, the last tick: the game's body thrown further than its own) */
	real_point3d body;
	/* the tick it started on (ragdoll_ticks) */
	unsigned long started_tick;
	/* the cluster its middle is in (NONE: none), the render's: it is
	drawn when that cluster is (ragdolls_drawn) */
	short drawn_cluster;
};

/* why a ragdoll takes over from the body (the log's words) */
enum
{
	_ragdoll_handoff_explosion,
	_ragdoll_handoff_melee,
	_ragdoll_handoff_vehicle,
	_ragdoll_handoff_blow,
	_ragdoll_handoff_airborne,
	_ragdoll_handoff_seat,
	_ragdoll_handoff_no_animation,
	_ragdoll_handoff_no_airborne_animation,
	_ragdoll_handoff_animation_over,
	_ragdoll_handoff_key_frame,
	_ragdoll_handoff_fallen,
	_ragdoll_handoff_impact,
	_ragdoll_handoff_landed,
	_ragdoll_handoff_time,
	NUMBER_OF_RAGDOLL_HANDOFFS
};

static char const *const ragdoll_handoff_names[NUMBER_OF_RAGDOLL_HANDOFFS] =
{
	"an explosion", "a melee blow", "a vehicle's hit", "a big blow", "dying in the air", "in a seat",
	"no death animation", "thrown, with no airborne death", "the death animation over",
	"the animation's key frame", "falling", "about to meet the level", "landed",
	"partway through the animation",
};

/* the hand-offs that follow the death animation: started from its pose the
tick before and pulled toward it a moment */
#define RAGDOLL_HANDOFF_ANIMATED(handoff) ((handoff) >= _ragdoll_handoff_key_frame)

/* a body that died: its death animation playing until its ragdoll is due */
struct ragdoll_pending
{
	long object_index;
	/* the tick it died on, and the tick its ragdoll became due (its wait for
	room counts from then) */
	unsigned long tick;
	unsigned long due_tick;
	/* why its ragdoll is due (_ragdoll_handoff_...); NONE: not yet */
	short handoff;
	/* the joints watched for the level (NONE: not found yet) */
	short impact_count;
	short impact_nodes[RAGDOLL_IMPACT_NODES];
	/* its pose the tick before, while it played its death animation (the
	ragdoll starts from it), and that animation */
	boolean have_previous;
	short previous_animation;
	short node_count;
	/* thrown into the air (or dead in it): flying on the game's path, its
	airborne death playing, since flight_tick; flew: the game played its
	airborne death; blow: why it was thrown (_ragdoll_handoff_...) */
	boolean flying, flew;
	short blow;
	unsigned long flight_tick;
	/* how its root node moved the last tick kept (world units a tick) */
	real_vector3d motion;
	/* its head's node (NONE: none), and how far its trunk leaned from
	upright at the death (radians) */
	short head_node;
	real lean;
	real_matrix4x3 previous[RAGDOLL_MAXIMUM_NODES];
};

struct ragdoll_damage_note
{
	long object_index;
	unsigned long tick;
	boolean explosion;
	/* a melee blow (a punch, a rifle butt, a sword): the whole body thrown */
	boolean melee;
	/* a vehicle's hit (the game globals' vehicle collision damage) */
	boolean vehicle;
	real_point3d origin;
	real_vector3d direction;
	/* world units a tick */
	real speed;
};

/* the game globals' falling damage block (a copy, as objects/damage.c,
units/bipeds.c and physics/physics.c each keep one): what a vehicle's hit
on a biped does (physics_compute_biped_vehicle_collision) */
struct ragdoll_falling_damage
{
	long unused0[2];
	real falling_distance_lower_bound;
	real falling_distance_upper_bound;
	struct tag_reference falling_damage;
	long terminal_velocity_unused[2];
	real maximum_distance;
	struct tag_reference maximum_distance_damage;
	struct tag_reference vehicle_hit_environment_damage_effect;
	struct tag_reference vehicle_killed_unit_damage_effect;
	struct tag_reference vehicle_collision_damage;
	struct tag_reference flaming_death_damage;
	long unused7c[4];
	real runtime_maximum_falling_velocity;
	real runtime_minimum_damage_velocity;
	real runtime_maximum_damage_velocity;
};

typedef char ragdoll_falling_damage_size_assert[sizeof(struct ragdoll_falling_damage) == 0x98 ? 1 : -1];

/* ---------- globals */

static struct ragdoll ragdolls[RAGDOLL_SLOTS];
/* by an object's absolute index, its ragdoll's slot plus one (0: none) */
static unsigned char ragdoll_slot_by_object[MAXIMUM_OBJECTS_PER_MAP];
static struct ragdoll_pending ragdoll_pending[RAGDOLL_PENDING];
static short ragdoll_pending_count;
static struct ragdoll_damage_note ragdoll_notes[RAGDOLL_DAMAGE_NOTES];
static short ragdoll_next_note;
/* the ragdolls' own count of game ticks, and of ragdolls started */
static unsigned long ragdoll_ticks;
static unsigned long ragdoll_sequence;
static unsigned long ragdoll_map_generation;
/* (the rig building's) the model's default pose */
static real_matrix4x3 ragdoll_bind[RAGDOLL_MAXIMUM_NODES];
/* (the rig building's) the pose a rig is made from when not the body's own
now: the tick before's, of a ragdoll that takes over from a death animation */
static real_matrix4x3 const *ragdoll_start_pose;
/* (the tests' camera) the last body to die, and the tests' last target */
static long ragdoll_last_death = NONE;
static long ragdoll_test_target = NONE;

static struct
{
	unsigned long started, rejected, slept, woken, released[NUMBER_OF_RAGDOLL_RELEASE_REASONS];
	/* why each ragdoll took over; the death animations watched (a body a
	tick), their time and point tests */
	unsigned long handoffs[NUMBER_OF_RAGDOLL_HANDOFFS];
	unsigned long watched, watch_points;
	unsigned long long watch_us, watch_most_us;
	unsigned long start_fixed, solid_reverts, not_a_number;
	/* the ticks with a ragdoll awake, their time, the most a tick, the
	collision vectors and point tests */
	unsigned long busy_ticks;
	unsigned long long busy_us, most_us;
	/* ragdolls stepped (one a tick each), and the starts' time */
	unsigned long steps;
	unsigned long long start_us;
	unsigned long rays, point_tests;
	/* the poses the render was given: copied over the body's with the
	tick's poses, and read with no tick running (racy counts, the render's
	and the tick's threads) */
	unsigned long copied, read_live;
	short most_awake, most_kept;
	/* (the log's window) */
	unsigned long window_ticks;
	unsigned long long window_us, window_most_us;
	unsigned long window_rays;
} ragdoll_stats;

/* ---------- the setting */

static boolean ragdolls_setting_read(
	char const *variable,
	char const *config_name)
{
	char const *setting = getenv(variable);

	/* (as port_config.c reads it: "0", "false", "no" and "off" are false) */
	if (setting && *setting)
	{
		return strcmp(setting, "0") && strcasecmp(setting, "false") && strcasecmp(setting, "no") &&
			strcasecmp(setting, "off");
	}
	return config_name ? config_boolean(config_name) != 0 : FALSE;
}

static int ragdolls_enabled_value = -1;
static short ragdolls_active_cap_value = RAGDOLL_ACTIVE_DEFAULT;

static void ragdolls_settings_update(
	void)
{
	static unsigned long settings_seen;

	/* (the environment first: the Vita's settings panel sets the variable,
	read again when it changes something - halo_settings_generation - so
	the row is live) */
	if (ragdolls_enabled_value < 0 || settings_seen != halo_settings_generation)
	{
		char const *active = getenv("HALO_RAGDOLLS_ACTIVE");

		settings_seen = halo_settings_generation;
#ifdef HALO_DEDICATED_SERVER
		ragdolls_enabled_value = FALSE;
#else
		ragdolls_enabled_value = ragdolls_setting_read("HALO_RAGDOLLS", "display.ragdolls");
#endif
		ragdolls_active_cap_value = RAGDOLL_ACTIVE_DEFAULT;
		if (active && *active)
		{
			int value = atoi(active);

			ragdolls_active_cap_value = (short)(value < 1 ? 1 : value > RAGDOLL_SLOTS ? RAGDOLL_SLOTS : value);
		}
	}
}

static boolean ragdolls_enabled(
	void)
{
	ragdolls_settings_update();
	return ragdolls_enabled_value != 0;
}

/* ---------- the log */

static int ragdolls_log_level(
	void)
{
	static int level = -1;

	if (level < 0)
	{
		char const *setting = getenv("HALO_RAGDOLLS_LOG");

		level = setting ? atoi(setting) : 0;
	}
	return level;
}

static char const *ragdoll_object_name(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);

	return object ? tag_name_strip_path(tag_get_name(object->definition_index)) : "none";
}

/* ---------- vectors */

static real ragdoll_dot(real_vector3d const *a, real_vector3d const *b)
{
	return a->i * b->i + a->j * b->j + a->k * b->k;
}

static void ragdoll_cross(real_vector3d const *a, real_vector3d const *b, real_vector3d *result)
{
	real_vector3d c;

	c.i = a->j * b->k - a->k * b->j;
	c.j = a->k * b->i - a->i * b->k;
	c.k = a->i * b->j - a->j * b->i;
	*result = c;
}

static void ragdoll_between(real_point3d const *from, real_point3d const *to, real_vector3d *result)
{
	result->i = to->x - from->x;
	result->j = to->y - from->y;
	result->k = to->z - from->z;
}

static real ragdoll_length(real_vector3d const *v)
{
	return sqrtf(ragdoll_dot(v, v));
}

static real ragdoll_distance(real_point3d const *a, real_point3d const *b)
{
	real_vector3d v;

	ragdoll_between(a, b, &v);
	return ragdoll_length(&v);
}

static real ragdoll_normalize(real_vector3d *v)
{
	real length = ragdoll_length(v);

	if (length > 1e-12f)
	{
		v->i /= length;
		v->j /= length;
		v->k /= length;
	}
	return length;
}

static void ragdoll_move(real_point3d *point, real_vector3d const *v, real scale)
{
	point->x += v->i * scale;
	point->y += v->j * scale;
	point->z += v->k * scale;
}

/* (neither infinite nor not a number: x - x is 0 only then) */
static boolean ragdoll_finite(real_point3d const *p)
{
	volatile real x = p->x, y = p->y, z = p->z;

	return x - x == 0.f && y - y == 0.f && z - z == 0.f;
}

/* ---------- rotations */

static void ragdoll_rotation_identity(
	struct ragdoll_rotation *r)
{
	memset(r, 0, sizeof(*r));
	r->axes[0].i = r->axes[1].j = r->axes[2].k = 1.f;
}

static void ragdoll_rotate(struct ragdoll_rotation const *r, real_vector3d const *v, real_vector3d *result)
{
	real_vector3d c;

	c.i = r->axes[0].i * v->i + r->axes[1].i * v->j + r->axes[2].i * v->k;
	c.j = r->axes[0].j * v->i + r->axes[1].j * v->j + r->axes[2].j * v->k;
	c.k = r->axes[0].k * v->i + r->axes[1].k * v->j + r->axes[2].k * v->k;
	*result = c;
}

/* a then b (b applied after a) */
static void ragdoll_rotation_multiply(
	struct ragdoll_rotation const *b,
	struct ragdoll_rotation const *a,
	struct ragdoll_rotation *result)
{
	struct ragdoll_rotation c;
	short axis;

	for (axis = 0; axis < 3; axis++)
		ragdoll_rotate(b, &a->axes[axis], &c.axes[axis]);
	*result = c;
}

/* the shortest turn taking unit vector from to unit vector to */
static void ragdoll_rotation_between(
	real_vector3d const *from,
	real_vector3d const *to,
	struct ragdoll_rotation *r)
{
	real c = ragdoll_dot(from, to);
	real_vector3d k;
	short axis;

	if (c < -0.9999f)
	{
		/* (half a turn about any axis square to from) */
		real_vector3d other = { 0.f, 0.f, 0.f };

		if (fabsf(from->i) < 0.9f)
			other.i = 1.f;
		else
			other.j = 1.f;
		ragdoll_cross(from, &other, &k);
		ragdoll_normalize(&k);
		for (axis = 0; axis < 3; axis++)
		{
			real_vector3d e = { 0.f, 0.f, 0.f };

			e.n[axis] = 1.f;
			/* (2 k k^T - I) e */
			r->axes[axis].i = 2.f * k.i * k.n[axis] - e.i;
			r->axes[axis].j = 2.f * k.j * k.n[axis] - e.j;
			r->axes[axis].k = 2.f * k.k * k.n[axis] - e.k;
		}
		return;
	}
	ragdoll_cross(from, to, &k);
	for (axis = 0; axis < 3; axis++)
	{
		real_vector3d e = { 0.f, 0.f, 0.f };
		real_vector3d k_cross_e;
		real scale = k.n[axis] / (1.f + c);

		e.n[axis] = 1.f;
		ragdoll_cross(&k, &e, &k_cross_e);
		/* c e + k x e + k (k . e) / (1 + c) */
		r->axes[axis].i = c * e.i + k_cross_e.i + k.i * scale;
		r->axes[axis].j = c * e.j + k_cross_e.j + k.j * scale;
		r->axes[axis].k = c * e.k + k_cross_e.k + k.k * scale;
	}
}

static void ragdoll_rotation_from_quaternion(
	real const q[4],
	struct ragdoll_rotation *r)
{
	real w = q[0], x = q[1], y = q[2], z = q[3];

	r->axes[0].i = 1.f - 2.f * (y * y + z * z);
	r->axes[0].j = 2.f * (x * y + w * z);
	r->axes[0].k = 2.f * (x * z - w * y);
	r->axes[1].i = 2.f * (x * y - w * z);
	r->axes[1].j = 1.f - 2.f * (x * x + z * z);
	r->axes[1].k = 2.f * (y * z + w * x);
	r->axes[2].i = 2.f * (x * z + w * y);
	r->axes[2].j = 2.f * (y * z - w * x);
	r->axes[2].k = 1.f - 2.f * (x * x + y * y);
}

/* the rotational part of a (its columns: the axes' images), from q on,
into q (Mueller, Bender, Chentanez, Macklin 2016) */
static void ragdoll_extract_rotation(
	real_vector3d const a[3],
	real q[4],
	short iterations)
{
	short iteration;

	for (iteration = 0; iteration < iterations; iteration++)
	{
		struct ragdoll_rotation r;
		real_vector3d omega = { 0.f, 0.f, 0.f };
		real denominator = 0.f;
		real angle;
		short axis;

		ragdoll_rotation_from_quaternion(q, &r);
		for (axis = 0; axis < 3; axis++)
		{
			real_vector3d c;

			ragdoll_cross(&r.axes[axis], &a[axis], &c);
			omega.i += c.i;
			omega.j += c.j;
			omega.k += c.k;
			denominator += ragdoll_dot(&r.axes[axis], &a[axis]);
		}
		denominator = fabsf(denominator) + 1e-9f;
		omega.i /= denominator;
		omega.j /= denominator;
		omega.k /= denominator;
		angle = ragdoll_length(&omega);
		if (angle < 1e-6f)
			break;
		{
			real half = 0.5f * angle;
			real s = sinf(half) / angle;
			real dq[4] = { cosf(half), omega.i * s, omega.j * s, omega.k * s };
			real n[4];
			real length;

			/* q = dq q */
			n[0] = dq[0] * q[0] - dq[1] * q[1] - dq[2] * q[2] - dq[3] * q[3];
			n[1] = dq[0] * q[1] + dq[1] * q[0] + dq[2] * q[3] - dq[3] * q[2];
			n[2] = dq[0] * q[2] - dq[1] * q[3] + dq[2] * q[0] + dq[3] * q[1];
			n[3] = dq[0] * q[3] + dq[1] * q[2] - dq[2] * q[1] + dq[3] * q[0];
			length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2] + n[3] * n[3]);
			if (!(length > 1e-9f))
				break;
			q[0] = n[0] / length;
			q[1] = n[1] / length;
			q[2] = n[2] / length;
			q[3] = n[3] / length;
		}
	}
}

/* ---------- the rig */

/* whether a node's name has part in it, whatever its case (the game's
characters name theirs "bip01 l thigh", "Bip01 Pelvis") */
static boolean ragdoll_name_has(
	char const *name,
	char const *part)
{
	char lower[TAG_STRING_LENGTH + 1];
	short index;

	for (index = 0; index < TAG_STRING_LENGTH && name[index]; index++)
		lower[index] = (char)(name[index] >= 'A' && name[index] <= 'Z' ? name[index] - 'A' + 'a' : name[index]);
	lower[index] = 0;
	return strstr(lower, part) != NULL;
}

/* a node no particle stands for: carried by its parent */
static boolean ragdoll_node_excluded(
	char const *name)
{
	static char const *const parts[] =
	{
		"finger", "thumb", "gun", "frame", "eye", "jaw", "mouth", "lid", "brow", "tongue", "toe", "nub",
		"dummy", "ponytail", "antenna", "mandible",
	};
	short index;

	for (index = 0; index < (short)NUMBEROF(parts); index++)
	{
		if (ragdoll_name_has(name, parts[index]))
			return TRUE;
	}
	return FALSE;
}

/* a joint of the body, a particle wherever it is */
static boolean ragdoll_node_major(
	char const *name)
{
	static char const *const parts[] =
	{
		"pelvis", "spine", "neck", "head", "clavicle", "upperarm", "forearm", "hand", "thigh", "calf", "horselink",
		"foot",
	};
	short index;

	for (index = 0; index < (short)NUMBEROF(parts); index++)
	{
		if (ragdoll_name_has(name, parts[index]))
			return TRUE;
	}
	return FALSE;
}

/* the half-angle of the cone the bone below a joint stays in (the joint's
node), and whether it is a hinge (the mod's limits, its graph's range) */
static real ragdoll_joint_cone(
	char const *name,
	struct ragdoll_graph_node const *graph_node)
{
	real cone;

	/* (the hinges: the mod's knee, Elite's lower leg and elbow, bent
	either way here: a distance does not know which) */
	if (ragdoll_name_has(name, "calf") || ragdoll_name_has(name, "horselink"))
		return RAGDOLL_DEGREES(150.f);
	if (ragdoll_name_has(name, "forearm"))
		return RAGDOLL_DEGREES(145.f);
	if (graph_node && TEST_FLAG(graph_node->flags, _ragdoll_joint_no_movement_bit))
		return RAGDOLL_DEGREES(6.f);
	if (ragdoll_name_has(name, "thigh"))
		cone = RAGDOLL_DEGREES(45.f);
	else if (ragdoll_name_has(name, "upperarm"))
		cone = RAGDOLL_DEGREES(90.f);
	else if (ragdoll_name_has(name, "clavicle"))
		cone = RAGDOLL_DEGREES(15.f);
	else if (ragdoll_name_has(name, "spine") || ragdoll_name_has(name, "pelvis"))
		cone = RAGDOLL_DEGREES(12.f);
	else if (ragdoll_name_has(name, "neck") || ragdoll_name_has(name, "head"))
		cone = RAGDOLL_DEGREES(35.f);
	else if (ragdoll_name_has(name, "hand"))
		cone = RAGDOLL_DEGREES(50.f);
	else if (ragdoll_name_has(name, "foot"))
		cone = RAGDOLL_DEGREES(35.f);
	else if (graph_node && graph_node->range > 0.f && graph_node->range < 6.3f)
		cone = 0.5f * graph_node->range;
	else
		cone = RAGDOLL_DEGREES(30.f);
	if (cone < RAGDOLL_DEGREES(6.f))
		cone = RAGDOLL_DEGREES(6.f);
	if (cone > RAGDOLL_DEGREES(90.f))
		cone = RAGDOLL_DEGREES(90.f);
	return cone;
}

static struct ragdoll_graph_node const *ragdoll_graph_node_get(
	struct animation_graph const *graph,
	short node_index)
{
	if (!graph || !VALID_INDEX(node_index, graph->nodes.count))
		return NULL;
	return TAG_BLOCK_GET_ELEMENT(&graph->nodes, node_index, struct ragdoll_graph_node);
}

static boolean ragdoll_in_cluster_together(
	struct ragdoll const *ragdoll,
	short a,
	short b)
{
	short cluster_index;

	for (cluster_index = 0; cluster_index < ragdoll->cluster_count; cluster_index++)
	{
		struct ragdoll_cluster const *cluster = &ragdoll->clusters[cluster_index];
		boolean has_a = FALSE, has_b = FALSE;
		short member;

		for (member = 0; member < cluster->count; member++)
		{
			has_a |= cluster->members[member] == a;
			has_b |= cluster->members[member] == b;
		}
		if (has_a && has_b)
			return TRUE;
	}
	return FALSE;
}

static boolean ragdoll_constraint_add(
	struct ragdoll *ragdoll,
	short a,
	short b,
	real minimum,
	real maximum)
{
	struct ragdoll_constraint *constraint;

	if (ragdoll->constraint_count >= RAGDOLL_MAXIMUM_CONSTRAINTS || a == b)
		return FALSE;
	constraint = &ragdoll->constraints[ragdoll->constraint_count++];
	constraint->a = (unsigned char)a;
	constraint->b = (unsigned char)b;
	constraint->minimum = minimum;
	constraint->maximum = maximum;
	return TRUE;
}

/* the joint at particle j: the bone from j to its child c kept within the
cone of half-angle cone about its default direction, measured from the
particles h of the rigid part above it (each distance c-h kept within
what the cone allows, its default angles from the default pose, its
lengths from the pose at the death; widened to take that pose) */
static void ragdoll_joint_add(
	struct ragdoll *ragdoll,
	short j,
	short c,
	short const *shell,
	short shell_count,
	real cone)
{
	struct ragdoll_particle const *joint = &ragdoll->particles[j];
	struct ragdoll_particle const *child = &ragdoll->particles[c];
	real_vector3d axis;
	real length = ragdoll_distance(&joint->position, &child->position);
	short index;

	ragdoll_between(&ragdoll_bind[joint->node_index].position, &ragdoll_bind[child->node_index].position, &axis);
	if (ragdoll_normalize(&axis) < 1e-4f || length < 1e-4f)
		return;
	for (index = 0; index < shell_count; index++)
	{
		struct ragdoll_particle const *h = &ragdoll->particles[shell[index]];
		real_vector3d w;
		real w_length, beta, low, high, minimum, maximum, now;

		ragdoll_between(&ragdoll_bind[joint->node_index].position, &ragdoll_bind[h->node_index].position, &w);
		if (ragdoll_normalize(&w) < 1e-4f)
			continue;
		w_length = ragdoll_distance(&joint->position, &h->position);
		if (w_length < 0.005f)
			continue;
		beta = acosf(PIN(ragdoll_dot(&w, &axis), -1.f, 1.f));
		low = MAX(0.f, beta - cone);
		high = MIN(3.14159265f, beta + cone);
		minimum = sqrtf(MAX(0.f, w_length * w_length + length * length - 2.f * w_length * length * cosf(low)));
		maximum = sqrtf(MAX(0.f, w_length * w_length + length * length - 2.f * w_length * length * cosf(high)));
		now = ragdoll_distance(&child->position, &h->position);
		if (now < minimum)
			minimum = now * 0.98f;
		if (now > maximum)
			maximum = now * 1.02f;
		ragdoll_constraint_add(ragdoll, shell[index], c, minimum, maximum);
	}
}

/* the particles a node's subtree starts from: its parent particle's
other children (the rigid part above a joint), its parent */
static short ragdoll_shell_find(
	struct ragdoll const *ragdoll,
	short j,
	short *shell,
	real const *cones)
{
	short p = ragdoll->particles[j].parent;
	short count = 0;
	short index;

	if (p == NONE)
		return 0;
	shell[count++] = p;
	for (index = 0; index < ragdoll->particle_count && count < RAGDOLL_MAXIMUM_SHELL; index++)
	{
		if (index != j && ragdoll->particles[index].parent == p)
			shell[count++] = index;
	}
	/* (a bone below a joint that hardly moves - the clavicle - is measured
	from the part above that too: the shoulder from the chest) */
	if (count < 3 && cones[p] <= RAGDOLL_DEGREES(25.f) && ragdoll->particles[p].parent != NONE)
	{
		short g = ragdoll->particles[p].parent;

		shell[count++] = g;
		for (index = 0; index < ragdoll->particle_count && count < RAGDOLL_MAXIMUM_SHELL; index++)
		{
			if (index != p && ragdoll->particles[index].parent == g)
				shell[count++] = index;
		}
	}
	return count;
}

static void ragdoll_log_rig(
	struct ragdoll const *ragdoll,
	struct model const *model,
	real const *cones)
{
	char line[1024];
	int length = 0;
	short index;

	for (index = 0; index < ragdoll->particle_count && length < (int)sizeof(line) - 64; index++)
	{
		struct ragdoll_particle const *particle = &ragdoll->particles[index];
		struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, particle->node_index, struct model_node);

		length += snprintf(line + length, sizeof(line) - length, "%s%s(%d<%d %.0f r%.3f)", index ? ", " : "",
			node->name, (int)index, (int)particle->parent, (double)(cones[index] * 57.29578f),
			(double)particle->radius);
	}
	platform_log("ragdolls: rig of %s: %d nodes, %d particles, %d constraints, %d clusters: %s",
		ragdoll_object_name(ragdoll->object_index), (int)ragdoll->node_count, (int)ragdoll->particle_count,
		(int)ragdoll->constraint_count, (int)ragdoll->cluster_count, line);
}

/* the rig of a dead body, from its model's nodes and its pose now (FALSE:
no body a ragdoll can be made of) */
static boolean ragdoll_build(
	struct ragdoll *ragdoll,
	long object_index,
	struct object_datum *object)
{
	struct object_definition *definition = object_definition_get(object->definition_index);
	struct model const *model;
	struct animation_graph const *graph = NULL;
	real_matrix4x3 const *live;
	short node_count;
	short depth[RAGDOLL_MAXIMUM_NODES];
	short particle_of_ancestor[RAGDOLL_MAXIMUM_NODES];
	real cones[RAGDOLL_MAXIMUM_PARTICLES];
	real limp_radius = RAGDOLL_DEFAULT_RADIUS;
	short index, count, level;

	if (!definition || definition->object.model.index == NONE)
		return FALSE;
	/* (a body the game itself lays limp on the ground, its biped's own
	flag: the soldiers, Covenant, crewmen, Flood combat forms and the
	Master Chief; not the cinematics' characters, Cortana, the Monitor,
	sentinels, infection forms or the scripts' wounded) */
	if (object->object.type != _object_type_biped ||
		!TEST_FLAG(biped_definition_get(object->definition_index)->biped.flags, _biped_uses_limp_body_physics_bit))
	{
		return FALSE;
	}
	model = model_definition_get(definition->object.model.index);
	node_count = (short)(object->object.node_matrices.size / (short)sizeof(real_matrix4x3));
	if (!model || node_count < RAGDOLL_MINIMUM_PARTICLES || node_count > RAGDOLL_MAXIMUM_NODES ||
		model->nodes.count < node_count)
	{
		return FALSE;
	}
	if (definition->object.animation_graph.index != NONE)
	{
		graph = animation_graph_definition_get(definition->object.animation_graph.index);
		if (graph && graph->nodes.count < node_count)
			graph = NULL;
		if (graph && graph->limp_body_node_collision_radius > 0.f &&
			graph->limp_body_node_collision_radius <= RAGDOLL_MAXIMUM_RADIUS)
		{
			limp_radius = graph->limp_body_node_collision_radius;
		}
	}
	live = ragdoll_start_pose ? ragdoll_start_pose :
		(real_matrix4x3 const *)object_header_block_get(object_index, &object->object.node_matrices);
	model_get_node_matrices(model, ragdoll_bind, NULL, NULL, NULL);

	memset(ragdoll, 0, sizeof(*ragdoll));
	ragdoll->object_index = object_index;
	ragdoll->node_count = node_count;

	/* (the nodes by depth: parents before children) */
	for (index = 0; index < node_count; index++)
	{
		struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node);
		short parent = node->parent_node_index;
		short guard = 0;
		short at = index;

		ragdoll->node_parent[index] = VALID_INDEX(parent, node_count) && parent != index ? parent : NONE;
		if (index == 0)
			ragdoll->node_parent[index] = NONE;
		depth[index] = 0;
		while (at != 0 && guard++ < RAGDOLL_MAXIMUM_NODES)
		{
			short up = TAG_BLOCK_GET_ELEMENT(&model->nodes, at, struct model_node)->parent_node_index;

			if (!VALID_INDEX(up, node_count) || up == at)
				break;
			at = up;
			depth[index]++;
		}
		if (index != 0 && ragdoll->node_parent[index] == NONE)
			return FALSE;
	}
	count = 0;
	for (level = 0; level < RAGDOLL_MAXIMUM_NODES && count < node_count; level++)
	{
		for (index = 0; index < node_count; index++)
		{
			if (depth[index] == level)
				ragdoll->node_order[count++] = index;
		}
	}
	if (count != node_count)
		return FALSE;
	/* (a map's nodes whose parents run in a ring: no body) */
	for (count = 0; count < node_count; count++)
	{
		short parent = ragdoll->node_parent[ragdoll->node_order[count]];

		if (parent != NONE && depth[parent] != depth[ragdoll->node_order[count]] - 1)
			return FALSE;
	}

	/* (the particles: the nodes far enough from their particle ancestor) */
	for (count = 0; count < node_count; count++)
	{
		short node_index = ragdoll->node_order[count];
		struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);
		short parent = ragdoll->node_parent[node_index];
		short ancestor = parent == NONE ? NONE : (ragdoll->node_particle[parent] >= 0 ?
			ragdoll->node_particle[parent] : particle_of_ancestor[parent]);
		boolean wanted;

		particle_of_ancestor[node_index] = ancestor;
		ragdoll->node_particle[node_index] = -1;
		if (!ragdoll_finite(&live[node_index].position))
			return FALSE;
		if (parent == NONE)
			wanted = TRUE;
		else
		{
			/* (a joint of the body by its name - a grunt's hip is where its
			pelvis is -, any other node far enough from the one above) */
			wanted = ancestor != NONE && !ragdoll_node_excluded(node->name) &&
				(ragdoll_node_major(node->name) || ragdoll_distance(&live[node_index].position,
					&ragdoll->particles[ancestor].position) >= RAGDOLL_MINIMUM_BONE);
		}
		if (wanted && ragdoll->particle_count < RAGDOLL_MAXIMUM_PARTICLES)
		{
			short p = ragdoll->particle_count++;
			struct ragdoll_particle *particle = &ragdoll->particles[p];

			particle->node_index = node_index;
			particle->parent = ancestor;
			particle->child = NONE;
			particle->cluster = NONE;
			particle->position = live[node_index].position;
			particle->inverse_mass = 1.f;
			particle->radius = limp_radius;
			ragdoll->node_particle[node_index] = (signed char)p;
			cones[p] = ragdoll_joint_cone(node->name, ragdoll_graph_node_get(graph, node_index));
			if (ancestor != NONE)
			{
				struct ragdoll_particle *above = &ragdoll->particles[ancestor];

				above->child = above->child_count++ == 0 ? p : NONE;
			}
			if (ragdoll_name_has(node->name, "head"))
				particle->radius = limp_radius * 1.5f;
		}
	}
	if (ragdoll->particle_count < RAGDOLL_MINIMUM_PARTICLES)
		return FALSE;

	/* (the rigid parts: a particle with two or more particle children, its
	children and its parent; heavier, and fatter) */
	for (index = 0; index < ragdoll->particle_count && ragdoll->cluster_count < RAGDOLL_MAXIMUM_CLUSTERS; index++)
	{
		struct ragdoll_particle *particle = &ragdoll->particles[index];
		struct ragdoll_cluster *cluster;
		real_point3d centroid = { 0.f, 0.f, 0.f };
		short member;

		if (particle->child_count < 2)
			continue;
		cluster = &ragdoll->clusters[ragdoll->cluster_count];
		cluster->count = 0;
		cluster->centre = index;
		cluster->members[cluster->count++] = (unsigned char)index;
		if (particle->parent != NONE)
			cluster->members[cluster->count++] = (unsigned char)particle->parent;
		for (member = 0; member < ragdoll->particle_count && cluster->count < RAGDOLL_CLUSTER_MEMBERS; member++)
		{
			if (ragdoll->particles[member].parent == index)
				cluster->members[cluster->count++] = (unsigned char)member;
		}
		for (member = 0; member < cluster->count; member++)
		{
			struct ragdoll_particle *m = &ragdoll->particles[cluster->members[member]];

			m->inverse_mass = 0.5f;
			if (m->radius < limp_radius * 1.5f)
				m->radius = limp_radius * 1.5f;
			centroid.x += m->position.x;
			centroid.y += m->position.y;
			centroid.z += m->position.z;
		}
		centroid.x /= cluster->count;
		centroid.y /= cluster->count;
		centroid.z /= cluster->count;
		for (member = 0; member < cluster->count; member++)
			ragdoll_between(&centroid, &ragdoll->particles[cluster->members[member]].position, &cluster->rest[member]);
		cluster->quaternion[0] = 1.f;
		particle->cluster = ragdoll->cluster_count++;
	}

	/* (the particles that meet the world with a vector each tick: all but
	those of a rigid part within two and a half radii of its centre - the
	hips by the pelvis, the clavicles and neck by the chest -, which its
	sphere stands for: those only once a point test finds them in solid
	structure) */
	for (index = 0; index < ragdoll->particle_count; index++)
		ragdoll->particles[index].collides = TRUE;
	for (index = 0; index < ragdoll->cluster_count; index++)
	{
		struct ragdoll_cluster const *cluster = &ragdoll->clusters[index];
		struct ragdoll_particle const *centre = &ragdoll->particles[cluster->centre];
		short member;

		for (member = 0; member < cluster->count; member++)
		{
			struct ragdoll_particle *particle = &ragdoll->particles[cluster->members[member]];

			if (cluster->members[member] != cluster->centre && particle->child_count > 0 &&
				ragdoll_distance(&particle->position, &centre->position) < 2.5f * centre->radius)
			{
				particle->collides = FALSE;
			}
		}
	}

	/* (the bones: each particle at its distance from its parent, unless a
	rigid part holds both) */
	for (index = 0; index < ragdoll->particle_count; index++)
	{
		struct ragdoll_particle *particle = &ragdoll->particles[index];
		real length;

		if (particle->parent == NONE || ragdoll_in_cluster_together(ragdoll, index, particle->parent))
			continue;
		length = ragdoll_distance(&particle->position, &ragdoll->particles[particle->parent].position);
		ragdoll_constraint_add(ragdoll, particle->parent, index, length, length);
	}

	/* (the joints: the bone from each particle j to its child c, kept in
	its cone, unless a rigid part holds both) */
	for (index = 0; index < ragdoll->particle_count; index++)
	{
		short c;

		for (c = 0; c < ragdoll->particle_count; c++)
		{
			short shell[RAGDOLL_MAXIMUM_SHELL];
			short shell_count;

			if (ragdoll->particles[c].parent != index || ragdoll_in_cluster_together(ragdoll, index, c))
				continue;
			shell_count = ragdoll_shell_find(ragdoll, index, shell, cones);
			ragdoll_joint_add(ragdoll, index, c, shell, shell_count, cones[index]);
		}
	}

	/* (the hinges: a knee, a horselink, an elbow - a bone with a bone
	above and one below - bends about its node's up axis, as every
	character's dying pose has it: a knee or an elbow its child toward
	-up's side, an Elite's or Jackal's horselink toward +up's; another
	hinge of the animation graph's the way it was bent at the death, if it
	was bent 10 degrees) */
	for (index = 0; index < ragdoll->particle_count; index++)
	{
		struct ragdoll_particle *joint = &ragdoll->particles[index];
		struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, joint->node_index, struct model_node);
		struct ragdoll_graph_node const *graph_node = ragdoll_graph_node_get(graph, joint->node_index);
		real_vector3d above, below, bend;
		real sign = 0.f;
		short up;

		if (joint->parent == NONE || joint->child == NONE ||
			ragdoll_in_cluster_together(ragdoll, index, joint->child) ||
			ragdoll_in_cluster_together(ragdoll, index, joint->parent))
		{
			continue;
		}
		ragdoll_between(&ragdoll->particles[joint->parent].position, &joint->position, &above);
		ragdoll_between(&joint->position, &ragdoll->particles[joint->child].position, &below);
		if (ragdoll_normalize(&above) < 1e-4f || ragdoll_normalize(&below) < 1e-4f)
			continue;
		ragdoll_cross(&above, &below, &bend);
		if (ragdoll_name_has(node->name, "horselink"))
			sign = 1.f;
		else if (ragdoll_name_has(node->name, "calf") || ragdoll_name_has(node->name, "forearm"))
			sign = -1.f;
		else if (graph_node && TEST_FLAG(graph_node->flags, _ragdoll_joint_hinge_bit) &&
			ragdoll_length(&bend) > RAGDOLL_HINGE_SLACK)
		{
			sign = ragdoll_dot(&bend, &live[joint->node_index].up) > 0.f ? 1.f : -1.f;
		}
		if (sign == 0.f)
			continue;
		joint->hinge = TRUE;
		joint->hinge_axis.i = live[joint->node_index].up.i * sign;
		joint->hinge_axis.j = live[joint->node_index].up.j * sign;
		joint->hinge_axis.k = live[joint->node_index].up.k * sign;
		if (ragdoll_normalize(&joint->hinge_axis) < 1e-4f)
		{
			joint->hinge = FALSE;
			continue;
		}
		ragdoll->hinge_count++;
		/* (its parent's turn carries the axis: that particle's and its
		ancestors' up to a rigid part's centre are found as it steps) */
		for (up = joint->parent; up != NONE; up = ragdoll->particles[up].parent)
		{
			ragdoll->particles[up].turned = TRUE;
			if (ragdoll->particles[up].cluster != NONE)
				break;
		}
	}

	/* (each node's matrix at the death: a particle's in the world, any
	other's in its parent's) */
	for (index = 0; index < node_count; index++)
	{
		short parent = ragdoll->node_parent[index];

		if (ragdoll->node_particle[index] >= 0 || parent == NONE)
			ragdoll->local[index] = live[index];
		else
		{
			real_matrix4x3 inverse;

			matrix4x3_inverse(&live[parent], &inverse);
			matrix4x3_multiply(&inverse, &live[index], &ragdoll->local[index]);
		}
		ragdoll->pose[index] = live[index];
	}
	for (index = 0; index < ragdoll->particle_count; index++)
	{
		struct ragdoll_particle *particle = &ragdoll->particles[index];

		ragdoll_rotation_identity(&particle->delta);
		if (particle->child != NONE)
		{
			ragdoll_between(&particle->position, &ragdoll->particles[particle->child].position,
				&particle->handoff_direction);
			ragdoll_normalize(&particle->handoff_direction);
		}
		particle->previous = particle->position;
		particle->start = particle->position;
		particle->animated = particle->position;
	}
	if (ragdolls_log_level() >= 2)
		ragdoll_log_rig(ragdoll, model, cones);
	return TRUE;
}

/* ---------- impulses */

static struct ragdoll_damage_note const *ragdoll_note_find(
	long object_index)
{
	struct ragdoll_damage_note const *found = NULL;
	short index;

	for (index = 0; index < RAGDOLL_DAMAGE_NOTES; index++)
	{
		struct ragdoll_damage_note const *note = &ragdoll_notes[index];

		if (note->object_index == object_index && ragdoll_ticks - note->tick <= RAGDOLL_NOTE_TICKS &&
			(!found || note->tick >= found->tick))
		{
			found = note;
		}
	}
	return found;
}

static void ragdoll_push(
	struct ragdoll_particle *particle,
	real_vector3d const *direction,
	real speed)
{
	/* (the step's motion is the tick's split in substeps) */
	real per_step = MIN(speed, RAGDOLL_MELEE_MAXIMUM) / RAGDOLL_SUBSTEPS;

	particle->previous.x -= direction->i * per_step;
	particle->previous.y -= direction->j * per_step;
	particle->previous.z -= direction->k * per_step;
}

/* a blow: an explosion throws each particle from its centre, the nearest
the most, a little upward; a melee blow the whole body along it; a shot the
particle nearest its line, and its neighbours half as much, and the shot
that kills (killing) the whole body back too */
static void ragdoll_apply_note(
	struct ragdoll *ragdoll,
	struct ragdoll_damage_note const *note,
	boolean killing)
{
	short index;

	if (note->explosion)
	{
		real_vector3d pushes[RAGDOLL_MAXIMUM_PARTICLES];
		real_vector3d mean = { 0.f, 0.f, 0.f };
		real nearest = REAL_MAX, farthest = 0.f;

		for (index = 0; index < ragdoll->particle_count; index++)
		{
			real distance = ragdoll_distance(&note->origin, &ragdoll->particles[index].position);

			nearest = MIN(nearest, distance);
			farthest = MAX(farthest, distance);
		}
		for (index = 0; index < ragdoll->particle_count; index++)
		{
			struct ragdoll_particle *particle = &ragdoll->particles[index];
			real_vector3d *push = &pushes[index];
			real distance = ragdoll_distance(&note->origin, &particle->position);
			real near = farthest > nearest ? 1.f - (distance - nearest) / (farthest - nearest) : 1.f;
			real speed = MIN(note->speed, RAGDOLL_MAXIMUM_IMPULSE) * (0.3f + 0.7f * near);

			ragdoll_between(&note->origin, &particle->position, push);
			if (ragdoll_normalize(push) < 1e-5f)
				*push = note->direction;
			push->k += 0.25f;
			ragdoll_normalize(push);
			push->i *= speed;
			push->j *= speed;
			push->k *= speed;
			mean.i += push->i / ragdoll->particle_count;
			mean.j += push->j / ragdoll->particle_count;
			mean.k += push->k / ragdoll->particle_count;
		}
		/* (the blast spreads the limbs, the nearest the most: the whole body
		is thrown as the game threw it, which its motion already has) */
		for (index = 0; index < ragdoll->particle_count; index++)
		{
			real_vector3d spread;
			real speed;

			spread.i = pushes[index].i - mean.i;
			spread.j = pushes[index].j - mean.j;
			spread.k = pushes[index].k - mean.k;
			speed = ragdoll_normalize(&spread);
			ragdoll_push(&ragdoll->particles[index], &spread, speed);
		}
	}
	else
	{
		short nearest = NONE;
		real nearest_distance = REAL_MAX;
		real speed = note->melee ? MAX(note->speed, RAGDOLL_MELEE_SPEED) :
			MIN(MAX(note->speed, RAGDOLL_SHOT_SPEED), RAGDOLL_MAXIMUM_IMPULSE);

		for (index = 0; index < ragdoll->particle_count; index++)
		{
			real_vector3d to;
			real along, distance_squared;

			ragdoll_between(&note->origin, &ragdoll->particles[index].position, &to);
			along = ragdoll_dot(&to, &note->direction);
			distance_squared = ragdoll_dot(&to, &to) - along * along;
			if (distance_squared < nearest_distance)
			{
				nearest_distance = distance_squared;
				nearest = index;
			}
		}
		if (nearest == NONE)
			return;
		if (note->melee)
		{
			/* (a melee blow: the whole body thrown along it, a little
			upward, the part nearest its line a quarter harder, so it turns
			as it goes) */
			real_vector3d throw_direction = note->direction;

			throw_direction.k += 0.3f;
			ragdoll_normalize(&throw_direction);
			for (index = 0; index < ragdoll->particle_count; index++)
				ragdoll_push(&ragdoll->particles[index], &throw_direction, speed * 0.85f);
			ragdoll_push(&ragdoll->particles[nearest], &throw_direction, speed * 0.25f);
			return;
		}
		ragdoll_push(&ragdoll->particles[nearest], &note->direction, speed);
		for (index = 0; index < ragdoll->particle_count; index++)
		{
			if (ragdoll->particles[index].parent == nearest || ragdoll->particles[nearest].parent == index)
				ragdoll_push(&ragdoll->particles[index], &note->direction, speed * 0.5f);
		}
		if (killing)
		{
			real_vector3d back = note->direction;
			real knockback = PIN(note->speed * RAGDOLL_SHOT_KNOCKBACK_SCALE, RAGDOLL_SHOT_KNOCKBACK,
				RAGDOLL_LAUNCH_SPEED);

			back.k += 0.25f;
			ragdoll_normalize(&back);
			for (index = 0; index < ragdoll->particle_count; index++)
				ragdoll_push(&ragdoll->particles[index], &back, knockback);
		}
	}
}

/* ---------- slots */

static void ragdoll_release(
	short slot,
	short reason)
{
	struct ragdoll *ragdoll = &ragdolls[slot];
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(ragdoll->object_index);

	if (ragdoll->state == _ragdoll_free)
		return;
	if (ragdolls_log_level())
	{
		platform_log("ragdolls: %08lx (%s) let go: %s (%ld ticks, %s)", (unsigned long)ragdoll->object_index,
			reason == _ragdoll_released_map ? "-" : ragdoll_object_name(ragdoll->object_index),
			ragdoll_release_names[reason], ragdoll->age, ragdoll->state == _ragdoll_awake ? "awake" : "asleep");
	}
	if (VALID_INDEX(absolute_index, MAXIMUM_OBJECTS_PER_MAP) && ragdoll_slot_by_object[absolute_index] == slot + 1)
		ragdoll_slot_by_object[absolute_index] = 0;
	ragdoll->state = _ragdoll_free;
	ragdoll->object_index = NONE;
	ragdoll_stats.released[reason]++;
}

static void ragdolls_release_all(
	short reason)
{
	short slot;

	for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
		ragdoll_release(slot, reason);
	ragdoll_pending_count = 0;
	memset(ragdoll_slot_by_object, 0, sizeof(ragdoll_slot_by_object));
}

static void ragdoll_sleep(
	struct ragdoll *ragdoll,
	char const *why)
{
	ragdoll->state = _ragdoll_asleep;
	ragdoll_stats.slept++;
	if (ragdolls_log_level())
	{
		platform_log("ragdolls: %08lx (%s) asleep: %s (%ld ticks)", (unsigned long)ragdoll->object_index,
			ragdoll_object_name(ragdoll->object_index), why, ragdoll->age);
	}
}

static struct ragdoll *ragdoll_of_object(
	long object_index)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	short slot;

	if (object_index == NONE || !VALID_INDEX(absolute_index, MAXIMUM_OBJECTS_PER_MAP))
		return NULL;
	slot = ragdoll_slot_by_object[absolute_index];
	if (!slot)
		return NULL;
	if (ragdolls[slot - 1].object_index != object_index || ragdolls[slot - 1].state == _ragdoll_free ||
		ragdolls[slot - 1].map_generation != halo_map_generation)
	{
		return NULL;
	}
	return &ragdolls[slot - 1];
}

/* a slot for a new ragdoll: a free one, else the one asleep and not drawn
for longest (the oldest of them), else the oldest asleep, else the oldest */
static short ragdoll_slot_new(
	void)
{
	short slot, chosen = NONE;
	short pass;

	for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
	{
		if (ragdolls[slot].state == _ragdoll_free)
			return slot;
	}
	for (pass = 0; pass < 3 && chosen == NONE; pass++)
	{
		for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
		{
			struct ragdoll *ragdoll = &ragdolls[slot];

			if (pass < 2 && ragdoll->state != _ragdoll_asleep)
				continue;
			if (pass == 0 && object_drawn_within(ragdoll->object_index, RAGDOLL_UNSEEN_FRAMES))
				continue;
			if (chosen == NONE || ragdoll->started < ragdolls[chosen].started)
				chosen = slot;
		}
	}
	if (chosen != NONE)
		ragdoll_release(chosen, _ragdoll_released_replaced);
	return chosen;
}

/* the particles of a new ragdoll that its pose left in solid structure
(an arm through a wall it died against) brought out the way their parent
stands: FALSE when its root is in solid */
static boolean ragdoll_start_outside(
	struct ragdoll *ragdoll)
{
	short index;

	for (index = 0; index < ragdoll->particle_count; index++)
	{
		struct ragdoll_particle *particle = &ragdoll->particles[index];
		struct collision_result collision;
		real_vector3d vector;

		ragdoll_stats.point_tests++;
		if (!collision_test_point(RAGDOLL_SOLID_FLAGS, &particle->position, NONE))
			continue;
		if (particle->parent == NONE)
			return FALSE;
		ragdoll_between(&ragdoll->particles[particle->parent].position, &particle->position, &vector);
		ragdoll_stats.rays++;
		if (collision_test_vector(RAGDOLL_COLLISION_FLAGS, &ragdoll->particles[particle->parent].position, &vector,
			ragdoll->object_index, &collision))
		{
			particle->position = collision.point;
			ragdoll_move(&particle->position, &collision.plane.n, particle->radius);
		}
		else
			particle->position = ragdoll->particles[particle->parent].position;
		ragdoll_stats.point_tests++;
		if (collision_test_point(RAGDOLL_SOLID_FLAGS, &particle->position, NONE))
			particle->position = ragdoll->particles[particle->parent].position;
		particle->previous = particle->position;
		particle->start = particle->position;
		ragdoll_stats.start_fixed++;
	}
	return TRUE;
}

/* the body is a local player's own unit (its dead camera follows where
the game has it) */
static boolean ragdoll_local_player_body(
	long object_index)
{
	short local_player_index;

	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		long player_index = local_player_get_player_index(local_player_index);

		struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;

		/* (player_died has it as its dead unit) */
		if (player && (player->unit_index == object_index || player->dead_unit_index == object_index))
			return TRUE;
	}
	return FALSE;
}

/* the cluster a point is in (NONE: none, outside the structure) */
static short ragdoll_cluster_of(
	real_point3d const *point)
{
	struct location location;

	scenario_location_from_point(&location, point);
	return location.cluster_index;
}

/* a body whose pose is to be a ragdoll's from now (FALSE: none made): from
its pose now, moving as the game threw it, the blow that killed it adding
to that; or, taking over from a death animation that played a tick at
least (from), from its pose the tick before, each particle moving as the
animation moved its node, so that this tick's steps land it on the pose
the game has now */
static boolean ragdoll_start(
	long object_index,
	struct object_datum *object,
	struct ragdoll_pending const *from)
{
	struct ragdoll_damage_note const *note = ragdoll_note_find(object_index);
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	real_matrix4x3 const *live = (real_matrix4x3 const *)object_header_block_get(object_index,
		&object->object.node_matrices);
	short node_count = (short)(object->object.node_matrices.size / (short)sizeof(real_matrix4x3));
	boolean animated = from && from->have_previous && from->node_count == node_count;
	boolean same_animation = animated && from->previous_animation == object->object.animation.state.index;
	struct ragdoll *ragdoll;
	short slot;
	short index;
	real_vector3d velocity = object->object.translational_velocity;
	unsigned long long started_us;
	boolean made;

	if (!VALID_INDEX(absolute_index, MAXIMUM_OBJECTS_PER_MAP) || ragdoll_of_object(object_index))
		return FALSE;
	started_us = vita_host_time_us();
	slot = ragdoll_slot_new();
	if (slot == NONE)
		return FALSE;
	ragdoll = &ragdolls[slot];
	/* (one that took over from the death animation: the blow that killed
	it was long before, and its throw is in the animation) */
	if (from && RAGDOLL_HANDOFF_ANIMATED(from->handoff))
		note = NULL;
	ragdoll_start_pose = animated ? from->previous : NULL;
	made = ragdoll_build(ragdoll, object_index, object) && ragdoll_start_outside(ragdoll);
	ragdoll_start_pose = NULL;
	if (!made)
	{
		memset(ragdoll, 0, sizeof(*ragdoll));
		ragdoll->state = _ragdoll_free;
		ragdoll->object_index = NONE;
		ragdoll_stats.rejected++;
		if (ragdolls_log_level())
			platform_log("ragdolls: %08lx (%s) no ragdoll: no body to make one of", (unsigned long)object_index,
				ragdoll_object_name(object_index));
		return FALSE;
	}
	ragdoll->map_generation = halo_map_generation;
	ragdoll->state = _ragdoll_awake;
	ragdoll->started = ragdoll->woken = ++ragdoll_sequence;
	/* (the body's own motion, as the game threw it, a tick's in substeps;
	no faster than a blast plausibly throws a body, but a local player's
	own, which the camera follows) */
	if (!ragdoll_finite((real_point3d const *)&velocity))
		velocity = *global_zero_vector3d;
	if (!ragdoll_local_player_body(object_index))
	{
		real speed = ragdoll_length(&velocity);

		if (speed > RAGDOLL_LAUNCH_SPEED)
		{
			velocity.i *= RAGDOLL_LAUNCH_SPEED / speed;
			velocity.j *= RAGDOLL_LAUNCH_SPEED / speed;
			velocity.k *= RAGDOLL_LAUNCH_SPEED / speed;
		}
	}
	/* (from the tick before but another animation now - landed, its
	airborne death over -: the whole body moving as it last moved) */
	if (animated && !same_animation)
	{
		real speed = ragdoll_length(&from->motion);

		velocity = from->motion;
		if (!(speed <= RAGDOLL_ANIMATED_SPEED))
		{
			real scale = speed > RAGDOLL_ANIMATED_SPEED ? RAGDOLL_ANIMATED_SPEED / speed : 0.f;

			velocity.i *= scale;
			velocity.j *= scale;
			velocity.k *= scale;
		}
	}
	for (index = 0; index < ragdoll->particle_count; index++)
	{
		struct ragdoll_particle *particle = &ragdoll->particles[index];
		real_vector3d moved = velocity;

		/* (from the death animation: each particle as its node moved from
		the tick before to now) */
		if (same_animation)
		{
			short node_index = particle->node_index;
			real speed;

			ragdoll_between(&from->previous[node_index].position, &live[node_index].position, &moved);
			speed = ragdoll_length(&moved);
			if (!(speed <= RAGDOLL_ANIMATED_SPEED))
			{
				real scale = speed > RAGDOLL_ANIMATED_SPEED ? RAGDOLL_ANIMATED_SPEED / speed : 0.f;

				moved.i *= scale;
				moved.j *= scale;
				moved.k *= scale;
			}
		}
		particle->previous.x = particle->position.x - moved.i / RAGDOLL_SUBSTEPS;
		particle->previous.y = particle->position.y - moved.j / RAGDOLL_SUBSTEPS;
		particle->previous.z = particle->position.z - moved.k / RAGDOLL_SUBSTEPS;
	}
	if (note)
		ragdoll_apply_note(ragdoll, note, TRUE);
	/* (taken over from a death animation: pulled toward it a moment, from
	where it had each node the tick before; a big blow's: thrown at once) */
	ragdoll->follow_ticks = animated && RAGDOLL_HANDOFF_ANIMATED(from->handoff) ? RAGDOLL_FOLLOW_TICKS : 0;
	ragdoll->center = object->object.bounding_sphere_center;
	ragdoll->radius = object->object.bounding_sphere_radius;
	ragdoll->body = object->object.position;
	ragdoll->started_tick = ragdoll_ticks;
	ragdoll->drawn_cluster = ragdoll_cluster_of(&ragdoll->center);
	ragdoll_slot_by_object[absolute_index] = (unsigned char)(slot + 1);
	ragdoll_stats.started++;
	ragdoll_stats.start_us += vita_host_time_us() - started_us;
	if (ragdolls_log_level())
	{
		platform_log("ragdolls: %08lx (%s) started in slot %d: %d particles, %d constraints, %d rigid parts, %d hinges; "
			"%s %.3f wu/tick, follows its death %d ticks; due %lu ticks after its death: %s; %s",
			(unsigned long)object_index, ragdoll_object_name(object_index),
			(int)slot, (int)ragdoll->particle_count, (int)ragdoll->constraint_count, (int)ragdoll->cluster_count,
			(int)ragdoll->hinge_count,
			note ? (note->vehicle ? "a vehicle's hit" : note->explosion ? "an explosion" : note->melee ? "a melee blow" :
			"a blow") : "no blow", note ? (double)note->speed : 0.0, (int)ragdoll->follow_ticks,
			from ? from->due_tick - from->tick : 0UL,
			from && VALID_INDEX(from->handoff, NUMBER_OF_RAGDOLL_HANDOFFS) ? ragdoll_handoff_names[from->handoff] : "-",
			!animated ? "from its pose now" : same_animation ? "from its death animation's last tick, moving as it moved" :
			"from its pose the tick before, moving as it last moved");
	}
	return TRUE;
}

/* ---------- the death animation */

/* the death animation the body plays (NULL: none, or not that now); in_the_air:
its airborne death too */
static struct animation const *ragdoll_death_animation(
	struct object_datum *object,
	boolean in_the_air)
{
	struct unit_datum const *unit = (struct unit_datum const *)object;
	struct animation_graph const *graph;
	short index = object->object.animation.state.index;

	if ((unit->unit.animation.state != _unit_state_dying &&
		(!in_the_air || unit->unit.animation.state != _unit_state_dying_airborne)) ||
		object->object.animation.animation_graph_index == NONE)
	{
		return NULL;
	}
	graph = animation_graph_definition_get(object->object.animation.animation_graph_index);
	if (!graph || !VALID_INDEX(index, graph->animations.count))
		return NULL;
	return TAG_BLOCK_GET_ELEMENT(&graph->animations, index, struct animation);
}

/* the joints of a body's upper body (its head, neck, spine and arms; with
whole_body its pelvis and legs too), at most RAGDOLL_IMPACT_NODES */
static short ragdoll_impact_nodes(
	struct object_datum *object,
	short node_count,
	short *nodes,
	boolean whole_body)
{
	static char const *const parts[] = { "head", "neck", "spine", "clavicle", "upperarm", "forearm", "hand",
		"pelvis", "thigh", "calf", "horselink", "foot" };
	short part_count = whole_body ? (short)NUMBEROF(parts) : 7;
	struct object_definition *definition = object_definition_get(object->definition_index);
	struct model const *model = definition && definition->object.model.index != NONE ?
		model_definition_get(definition->object.model.index) : NULL;
	short count = 0, node, part;

	if (!model)
		return 0;
	for (node = 0; node < node_count && node < model->nodes.count && count < RAGDOLL_IMPACT_NODES; node++)
	{
		char const *name = TAG_BLOCK_GET_ELEMENT(&model->nodes, node, struct model_node)->name;

		if (ragdoll_node_excluded(name))
			continue;
		for (part = 0; part < part_count; part++)
		{
			if (ragdoll_name_has(name, parts[part]))
			{
				nodes[count++] = node;
				break;
			}
		}
	}
	return count;
}

/* how far a pose's trunk (its root node to its head) leans from upright,
radians (0: no head) */
static real ragdoll_lean(
	real_matrix4x3 const *pose,
	short head_node)
{
	real_vector3d trunk;
	real length;

	if (head_node == NONE)
		return 0.f;
	ragdoll_between(&pose[0].position, &pose[head_node].position, &trunk);
	length = ragdoll_length(&trunk);
	if (!(length > 1e-4f))
		return 0.f;
	return acosf(PIN(trunk.k / length, -1.f, 1.f));
}

/* the body's pose now kept as the tick before's (its death animation
playing) */
static void ragdoll_keep_previous(
	struct ragdoll_pending *pending,
	struct object_datum *object,
	real_matrix4x3 const *live,
	short node_count)
{
	short index;

	if (!pending->have_previous)
	{
		struct object_definition *definition = object_definition_get(object->definition_index);
		struct model const *model = definition && definition->object.model.index != NONE ?
			model_definition_get(definition->object.model.index) : NULL;

		pending->head_node = NONE;
		for (index = 1; model && index < node_count && index < model->nodes.count; index++)
		{
			if (ragdoll_name_has(TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node)->name, "head"))
			{
				pending->head_node = index;
				break;
			}
		}
		pending->lean = ragdoll_lean(live, pending->head_node);
	}
	if (pending->have_previous && pending->node_count == node_count)
		ragdoll_between(&pending->previous[0].position, &live[0].position, &pending->motion);
	else
		pending->motion = *global_zero_vector3d;
	memcpy(pending->previous, live, node_count * sizeof(real_matrix4x3));
	pending->node_count = node_count;
	pending->previous_animation = object->object.animation.state.index;
	pending->have_previous = TRUE;
}

/* whether any of the watched joints (pending->impact_nodes, found the first
time) would pass into the level the next tick, moving on as it moved since
the tick before (one that barely moved is not tested: it cannot) */
static boolean ragdoll_meets_level(
	struct ragdoll_pending *pending,
	struct object_datum *object,
	real_matrix4x3 const *live,
	short node_count)
{
	short index;

	if (!pending->have_previous || pending->node_count != node_count)
		return FALSE;
	if (pending->impact_count == NONE)
		pending->impact_count = ragdoll_impact_nodes(object, node_count, pending->impact_nodes, pending->flying);
	for (index = 0; index < pending->impact_count; index++)
	{
		short node = pending->impact_nodes[index];
		real_point3d ahead = live[node].position;
		real_vector3d moved;

		ragdoll_between(&pending->previous[node].position, &live[node].position, &moved);
		if (!(ragdoll_length(&moved) >= RAGDOLL_WATCH_MOVED))
			continue;
		ragdoll_move(&ahead, &moved, 1.f);
		if (!ragdoll_finite(&ahead))
			continue;
		/* (ahead first: the one usually clear) */
		ragdoll_stats.watch_points++;
		if (!collision_test_point(RAGDOLL_SOLID_FLAGS, &ahead, NONE))
			continue;
		ragdoll_stats.watch_points++;
		if (!collision_test_point(RAGDOLL_SOLID_FLAGS, &live[node].position, NONE))
			return TRUE;
	}
	return FALSE;
}

/* whether a body thrown (or dead in the air) may fly on the game's path with
its airborne death playing: its biped has one, and it is in the air or
leaving the ground */
static boolean ragdoll_may_fly(
	struct object_datum *object)
{
	struct unit_datum const *unit = (struct unit_datum const *)object;
	struct biped_datum const *biped = (struct biped_datum const *)object;

	if (object->object.parent_object_index != NONE ||
		TEST_FLAG(biped_definition_get(object->definition_index)->biped.flags, _biped_has_no_dying_airborne_bit))
	{
		return FALSE;
	}
	return unit->unit.animation.state == _unit_state_dying_airborne ||
		TEST_FLAG(biped->biped.flags, _biped_airborne_bit) ||
		object->object.translational_velocity.k > RAGDOLL_LIFT;
}

/* a tick of a body flying (ragdoll_may_fly): its ragdoll due as it meets the
level in the air, when the game lands it, or, the game playing no airborne
death a few ticks on, as the blow's (NONE: not yet) */
static short ragdoll_flight(
	struct ragdoll_pending *pending,
	struct object_datum *object,
	real_matrix4x3 const *live,
	short node_count)
{
	struct unit_datum const *unit = (struct unit_datum const *)object;
	struct biped_datum const *biped = (struct biped_datum const *)object;
	char state = unit->unit.animation.state;

	if (TEST_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit))
		return _ragdoll_handoff_animation_over;
	if (state == _unit_state_dying_airborne)
	{
		pending->flew = TRUE;
		if (ragdoll_ticks - pending->flight_tick >= RAGDOLL_FLIGHT_TICKS_MOST)
			return _ragdoll_handoff_time;
		/* (any joint about to meet the ground or a wall) */
		if (pending->previous_animation == object->object.animation.state.index &&
			ragdoll_meets_level(pending, object, live, node_count))
		{
			return _ragdoll_handoff_impact;
		}
		return NONE;
	}
	/* (the game landed it: its "landing-dead" would play now) */
	if (pending->flew)
		return _ragdoll_handoff_landed;
	if (state != _unit_state_dying)
		return _ragdoll_handoff_animation_over;
	/* (the game plays the airborne death after three ticks in the air: none
	by now, none at all, or the body never left the ground) */
	if (ragdoll_ticks - pending->flight_tick >= RAGDOLL_LIFT_TICKS)
	{
		return TEST_FLAG(biped->biped.flags, _biped_airborne_bit) ? _ragdoll_handoff_no_airborne_animation :
			pending->blow;
	}
	return NONE;
}

/* a tick of a body that died and has no ragdoll yet: whether its ragdoll is
due now and why (pending->handoff), else its pose kept for the start. Reads
the game's state and the level's collision only */
static void ragdoll_watch(
	struct ragdoll_pending *pending,
	struct object_datum *object)
{
	struct unit_datum const *unit = (struct unit_datum const *)object;
	struct biped_datum const *biped = (struct biped_datum const *)object;
	struct ragdoll_damage_note const *note = ragdoll_note_find(pending->object_index);
	real_matrix4x3 const *live = (real_matrix4x3 const *)object_header_block_get(pending->object_index,
		&object->object.node_matrices);
	short node_count = (short)(object->object.node_matrices.size / (short)sizeof(real_matrix4x3));
	struct animation const *animation = ragdoll_death_animation(object, pending->flying);
	short frame = object->object.animation.state.frame_index;
	short handoff = NONE;
	short blow = !note ? NONE : note->vehicle ? _ragdoll_handoff_vehicle : note->explosion ? _ragdoll_handoff_explosion :
		note->melee ? _ragdoll_handoff_melee : note->speed > RAGDOLL_BIG_BLOW ? _ragdoll_handoff_blow : NONE;

	if (node_count < 1 || node_count > RAGDOLL_MAXIMUM_NODES)
		handoff = _ragdoll_handoff_no_animation;
	else if (pending->flying)
		handoff = ragdoll_flight(pending, object, live, node_count);
	/* (a big blow, or a death in the air: flying, its airborne death
	playing, where the body is thrown into the air and has one; else limp
	at once) */
	else if (blow != NONE || unit->unit.animation.state == _unit_state_dying_airborne)
	{
		if (ragdoll_may_fly(object))
		{
			pending->flying = TRUE;
			pending->flew = FALSE;
			pending->blow = blow != NONE ? blow : _ragdoll_handoff_airborne;
			pending->flight_tick = ragdoll_ticks;
			/* (all its joints watched for the ground or a wall) */
			pending->impact_count = NONE;
			if (ragdolls_log_level())
			{
				platform_log("ragdolls: %08lx (%s) thrown (%s, rising %.1f m/s): flying, its airborne death playing",
					(unsigned long)pending->object_index, ragdoll_object_name(pending->object_index),
					ragdoll_handoff_names[pending->blow], (double)(object->object.translational_velocity.k *
					3.048f * TICKS_PER_SECOND));
			}
			animation = ragdoll_death_animation(object, TRUE);
		}
		else
			handoff = blow != NONE ? blow : _ragdoll_handoff_airborne;
	}
	else if (object->object.parent_object_index != NONE)
		handoff = _ragdoll_handoff_seat;
	else if (TEST_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit))
		handoff = _ragdoll_handoff_animation_over;
	else if (!animation || animation->frame_count < 2)
	{
		/* (its death animation over, or none picked a few ticks on) */
		if (pending->have_previous)
			handoff = _ragdoll_handoff_animation_over;
		else if (ragdoll_ticks - pending->tick >= RAGDOLL_ANIMATION_WAIT_TICKS)
			handoff = _ragdoll_handoff_no_animation;
	}
	else if (pending->have_previous)
	{
		short key = animation->private_key_frame_index;

		if (frame >= animation->frame_count - 1)
			handoff = _ragdoll_handoff_animation_over;
		else if (key > 0 && key < animation->frame_count && frame >= key)
			handoff = _ragdoll_handoff_key_frame;
		else if (ragdoll_lean(live, pending->head_node) - pending->lean > RAGDOLL_HANDOFF_LEAN)
			handoff = _ragdoll_handoff_fallen;
		else if (frame >= (short)(animation->frame_count * RAGDOLL_HANDOFF_FRACTION))
			handoff = _ragdoll_handoff_time;
		/* (its head, spine or arms about to pass into the level: a fall into
		a wall, or onto the ground) */
		else if (ragdoll_meets_level(pending, object, live, node_count))
			handoff = _ragdoll_handoff_impact;
	}
	if (handoff == NONE)
	{
		if (animation && animation->frame_count >= 2)
			ragdoll_keep_previous(pending, object, live, node_count);
		return;
	}
	pending->handoff = handoff;
	pending->due_tick = ragdoll_ticks;
	ragdoll_stats.handoffs[handoff]++;
	if (ragdolls_log_level())
	{
		platform_log("ragdolls: %08lx (%s) due %lu ticks after its death: %s (%s, frame %d of %d, key frame %d; "
			"moving %.1f m/s)", (unsigned long)pending->object_index, ragdoll_object_name(pending->object_index),
			ragdoll_ticks - pending->tick, ragdoll_handoff_names[handoff],
			animation ? animation->name : "no death animation", (int)frame, animation ? (int)animation->frame_count : 0,
			animation ? (int)animation->private_key_frame_index : 0,
			(double)(ragdoll_length(&object->object.translational_velocity) * 3.048f * TICKS_PER_SECOND));
	}
}

/* ---------- the step */

static void ragdoll_solve_constraints(
	struct ragdoll *ragdoll)
{
	short index;

	for (index = 0; index < ragdoll->constraint_count; index++)
	{
		struct ragdoll_constraint const *constraint = &ragdoll->constraints[index];
		struct ragdoll_particle *a = &ragdoll->particles[constraint->a];
		struct ragdoll_particle *b = &ragdoll->particles[constraint->b];
		real_vector3d delta;
		real distance, target, weights, correction;

		ragdoll_between(&a->position, &b->position, &delta);
		distance = ragdoll_length(&delta);
		if (distance < 1e-6f)
			continue;
		if (distance < constraint->minimum)
			target = constraint->minimum;
		else if (distance > constraint->maximum)
			target = constraint->maximum;
		else
			continue;
		weights = a->inverse_mass + b->inverse_mass;
		correction = (distance - target) / (distance * weights);
		ragdoll_move(&a->position, &delta, a->inverse_mass * correction);
		ragdoll_move(&b->position, &delta, -b->inverse_mass * correction);
	}
}

static void ragdoll_solve_clusters(
	struct ragdoll *ragdoll)
{
	short index;

	for (index = 0; index < ragdoll->cluster_count; index++)
	{
		struct ragdoll_cluster *cluster = &ragdoll->clusters[index];
		real_point3d centre = { 0.f, 0.f, 0.f };
		real_vector3d a[3];
		struct ragdoll_rotation rotation;
		real mass = 0.f;
		short member;

		for (member = 0; member < cluster->count; member++)
		{
			struct ragdoll_particle const *particle = &ragdoll->particles[cluster->members[member]];
			real m = 1.f / particle->inverse_mass;

			centre.x += particle->position.x * m;
			centre.y += particle->position.y * m;
			centre.z += particle->position.z * m;
			mass += m;
		}
		centre.x /= mass;
		centre.y /= mass;
		centre.z /= mass;
		memset(a, 0, sizeof(a));
		for (member = 0; member < cluster->count; member++)
		{
			struct ragdoll_particle const *particle = &ragdoll->particles[cluster->members[member]];
			real_vector3d p;
			short axis;

			ragdoll_between(&centre, &particle->position, &p);
			for (axis = 0; axis < 3; axis++)
			{
				a[axis].i += p.i * cluster->rest[member].n[axis];
				a[axis].j += p.j * cluster->rest[member].n[axis];
				a[axis].k += p.k * cluster->rest[member].n[axis];
			}
		}
		ragdoll_extract_rotation(a, cluster->quaternion, cluster->warm ? 2 : 12);
		cluster->warm = TRUE;
		ragdoll_rotation_from_quaternion(cluster->quaternion, &rotation);
		for (member = 0; member < cluster->count; member++)
		{
			struct ragdoll_particle *particle = &ragdoll->particles[cluster->members[member]];
			real_vector3d offset;

			ragdoll_rotate(&rotation, &cluster->rest[member], &offset);
			particle->position = centre;
			ragdoll_move(&particle->position, &offset, 1.f);
		}
	}
}

/* a particle against a plane it met: out to its radius, no motion left
into it, and along it Coulomb's friction as position-based dynamics has it
(Macklin et al. 2014: a slide shorter than the static share of how far it
was pushed out is undone, a longer one shortened by the kinetic share);
TRUE: touching it */
static boolean ragdoll_plane_keep(
	struct ragdoll_particle *particle,
	real_vector3d const *normal,
	real d)
{
	real distance = ragdoll_dot(normal, (real_vector3d const *)&particle->position) - d;
	real_vector3d motion, along;
	real depth, away, into, slide;

	if (distance >= particle->radius + RAGDOLL_TOUCHING)
		return FALSE;
	ragdoll_between(&particle->previous, &particle->position, &motion);
	/* (the step's rounds' pushes out of it are no motion away) */
	away = MAX(0.f, ragdoll_dot(&motion, normal) - particle->pushed);
	depth = MAX(0.f, particle->radius - distance);
	ragdoll_move(&particle->position, normal, depth);
	/* (and the step's rounds' pushes out of it) */
	depth += particle->pushed;
	if (depth <= 0.f)
		return TRUE;
	ragdoll_between(&particle->previous, &particle->position, &motion);
	into = ragdoll_dot(&motion, normal);
	along.i = motion.i - normal->i * into;
	along.j = motion.j - normal->j * into;
	along.k = motion.k - normal->k * into;
	slide = ragdoll_length(&along);
	if (slide <= RAGDOLL_STATIC_FRICTION * depth)
	{
		/* (it sticks) */
		ragdoll_move(&particle->position, &along, -1.f);
		along = *global_zero_vector3d;
	}
	else
	{
		real kept = 1.f - MIN(1.f, RAGDOLL_KINETIC_FRICTION * depth / slide);

		ragdoll_move(&particle->position, &along, kept - 1.f);
		along.i *= kept;
		along.j *= kept;
		along.k *= kept;
	}
	/* (what moved it away from the plane before it was pushed out, kept) */
	particle->previous.x = particle->position.x - along.i - normal->i * away;
	particle->previous.y = particle->position.y - along.j - normal->j * away;
	particle->previous.z = particle->position.z - along.k - normal->k * away;
	return TRUE;
}

static void ragdoll_plane_remember(
	struct ragdoll_particle *particle,
	real_vector3d const *normal,
	real_point3d const *point)
{
	short index, chosen = 0;

	for (index = 0; index < RAGDOLL_PLANES; index++)
	{
		struct ragdoll_plane *plane = &particle->planes[index];

		/* (the same surface met again, or a free place, or the oldest) */
		if (plane->ticks > 0 && ragdoll_dot(&plane->normal, normal) > 0.99f)
		{
			chosen = index;
			break;
		}
		if (plane->ticks <= 0 || plane->ticks < particle->planes[chosen].ticks)
			chosen = index;
	}
	particle->planes[chosen].normal = *normal;
	particle->planes[chosen].d = ragdoll_dot(normal, (real_vector3d const *)point);
	particle->planes[chosen].at = particle->position;
	particle->planes[chosen].ticks = RAGDOLL_PLANE_TICKS;
}

/* the particles moved this tick met with the structure: one vector from
where each stood to where it ends, its radius further */
static void ragdoll_collide(
	struct ragdoll *ragdoll)
{
	short index;

	for (index = 0; index < ragdoll->particle_count; index++)
	{
		struct ragdoll_particle *particle = &ragdoll->particles[index];
		struct collision_result collision;
		real_vector3d move, vector;
		real length;
		boolean resting = FALSE;
		short plane;

		for (plane = 0; plane < RAGDOLL_PLANES; plane++)
		{
			if (particle->planes[plane].ticks > 0)
			{
				resting = TRUE;
				if (--particle->planes[plane].ticks <= 0 ||
					ragdoll_distance(&particle->planes[plane].at, &particle->position) > RAGDOLL_PLANE_REACH)
				{
					particle->planes[plane].ticks = 0;
				}
			}
		}
		ragdoll_between(&particle->start, &particle->position, &move);
		length = ragdoll_length(&move);
		if (resting && length < RAGDOLL_RESTING)
			continue;
		/* (one its rigid part's centre stands for: only a point test, the
		vector only when it went into solid structure all the same) */
		if (!particle->collides)
		{
			ragdoll_stats.point_tests++;
			if (!collision_test_point(RAGDOLL_SOLID_FLAGS, &particle->position, NONE))
				continue;
		}
		if (length > 1e-6f)
		{
			vector = move;
			ragdoll_move((real_point3d *)&vector, &move, particle->radius / length);
		}
		else
		{
			vector.i = vector.j = 0.f;
			vector.k = -particle->radius - 0.01f;
		}
		ragdoll_stats.rays++;
		if (collision_test_vector(RAGDOLL_COLLISION_FLAGS, &particle->start, &vector, ragdoll->object_index,
			&collision))
		{
			real_vector3d normal = collision.plane.n;

			if (ragdoll_dot(&normal, &vector) > 0.f)
			{
				normal.i = -normal.i;
				normal.j = -normal.j;
				normal.k = -normal.k;
			}
			ragdoll_plane_remember(particle, &normal, &collision.point);
			ragdoll_plane_keep(particle, &normal, ragdoll_dot(&normal, (real_vector3d const *)&collision.point));
			/* (never left in solid structure - put out of one surface into
			another, in a corner -: back where it stood; a vector that met
			nothing ends where it started, in the open) */
			ragdoll_stats.point_tests++;
			if (collision_test_point(RAGDOLL_SOLID_FLAGS, &particle->position, NONE))
			{
				particle->position = particle->start;
				particle->previous = particle->start;
				ragdoll_stats.solid_reverts++;
			}
		}
	}
}

/* the turn of a particle's part since the death (its parent's found
first): a rigid part's centre's by its fitted rotation, a bone's by the
turn taking its direction then, carried by its parent's, to its direction
now, any other's its parent's */
static void ragdoll_turn(
	struct ragdoll *ragdoll,
	short index)
{
	struct ragdoll_particle *particle = &ragdoll->particles[index];
	struct ragdoll_rotation parent_delta;

	if (particle->parent != NONE)
		parent_delta = ragdoll->particles[particle->parent].delta;
	else
		ragdoll_rotation_identity(&parent_delta);
	if (particle->cluster != NONE)
		ragdoll_rotation_from_quaternion(ragdoll->clusters[particle->cluster].quaternion, &particle->delta);
	else if (particle->child != NONE)
	{
		real_vector3d expected, now;
		struct ragdoll_rotation turn;

		ragdoll_rotate(&parent_delta, &particle->handoff_direction, &expected);
		ragdoll_between(&particle->position, &ragdoll->particles[particle->child].position, &now);
		if (ragdoll_normalize(&now) < 1e-6f || ragdoll_normalize(&expected) < 1e-6f)
			particle->delta = parent_delta;
		else
		{
			ragdoll_rotation_between(&expected, &now, &turn);
			ragdoll_rotation_multiply(&turn, &parent_delta, &particle->delta);
		}
	}
	else
		particle->delta = parent_delta;
}

/* the knees and elbows bent the way they bend: the child kept in the
plane square to the hinge's axis (within 10 degrees) and on its side of
the bone above (straight at most, 5 degrees over); its distance from the
joint as it was */
static void ragdoll_solve_hinges(
	struct ragdoll *ragdoll)
{
	static int enabled = -1;
	short index;

	/* (debug) HALO_RAGDOLLS_HINGES=0: knees and elbows bend either way, as
	before the hinges (their angle alone limited) */
	if (enabled < 0)
		enabled = !getenv("HALO_RAGDOLLS_HINGES") || atoi(getenv("HALO_RAGDOLLS_HINGES")) != 0;
	if (!ragdoll->hinge_count || !enabled)
		return;
	for (index = 0; index < ragdoll->particle_count; index++)
	{
		if (ragdoll->particles[index].turned)
			ragdoll_turn(ragdoll, index);
	}
	for (index = 0; index < ragdoll->particle_count; index++)
	{
		struct ragdoll_particle const *joint = &ragdoll->particles[index];
		struct ragdoll_particle *child;
		real_vector3d axis, above, below, side;
		real length, off, limit, along, bent;


		if (!joint->hinge)
			continue;
		child = &ragdoll->particles[joint->child];
		ragdoll_rotate(&ragdoll->particles[joint->parent].delta, &joint->hinge_axis, &axis);
		ragdoll_between(&ragdoll->particles[joint->parent].position, &joint->position, &above);
		ragdoll_between(&joint->position, &child->position, &below);
		length = ragdoll_length(&below);
		if (ragdoll_normalize(&above) < 1e-5f || length < 1e-5f)
			continue;
		/* (out of the plane: back to within 10 degrees of it) */
		off = ragdoll_dot(&below, &axis);
		limit = RAGDOLL_HINGE_SLACK * length;
		if (off > limit || off < -limit)
			ragdoll_move((real_point3d *)&below, &axis, (off > 0.f ? limit : -limit) - off);
		/* (bent the wrong way: straightened, 5 degrees over at most) */
		ragdoll_cross(&above, &below, &side);
		bent = ragdoll_dot(&side, &axis);
		if (bent < -RAGDOLL_HINGE_OVER * length)
		{
			real_vector3d across;

			along = ragdoll_dot(&below, &above);
			ragdoll_cross(&axis, &above, &across);
			/* (along the bone above, a little over its line) */
			off = ragdoll_dot(&below, &axis);
			below.i = above.i * MAX(along, 0.f) + axis.i * off - across.i * RAGDOLL_HINGE_OVER * length;
			below.j = above.j * MAX(along, 0.f) + axis.j * off - across.j * RAGDOLL_HINGE_OVER * length;
			below.k = above.k * MAX(along, 0.f) + axis.k * off - across.k * RAGDOLL_HINGE_OVER * length;
		}
		if (ragdoll_normalize(&below) < 1e-6f)
			continue;
		/* (the child and the joint moved toward it by their masses: a foot
		on the ground straightens a knee by the knee) */
		{
			real_point3d target = joint->position;
			real_vector3d correction;
			real weights = joint->inverse_mass + child->inverse_mass;
			struct ragdoll_particle *bent_joint = &ragdoll->particles[index];

			ragdoll_move(&target, &below, length);
			ragdoll_between(&child->position, &target, &correction);
			ragdoll_move(&child->position, &correction, child->inverse_mass / weights);
			ragdoll_move(&bent_joint->position, &correction, -bent_joint->inverse_mass / weights);
		}
	}
}

/* the planes a particle touches, kept to within a step's rounds: out to its
radius (its motion and friction the step's end's, ragdoll_plane_keep) */
static void ragdoll_solve_planes(
	struct ragdoll *ragdoll)
{
	short index, plane;

	for (index = 0; index < ragdoll->particle_count; index++)
	{
		struct ragdoll_particle *particle = &ragdoll->particles[index];

		for (plane = 0; plane < RAGDOLL_PLANES; plane++)
		{
			struct ragdoll_plane const *touched = &particle->planes[plane];
			real distance;

			if (touched->ticks <= 0)
				continue;
			distance = ragdoll_dot(&touched->normal, (real_vector3d const *)&particle->position) - touched->d;
			if (distance < particle->radius)
			{
				ragdoll_move(&particle->position, &touched->normal, particle->radius - distance);
				particle->pushed += particle->radius - distance;
			}
		}
	}
}

/* the turn of each particle's part since the death, and the pose drawn:
a particle's node at the particle, turned so; any other carried by its
parent */
static void ragdoll_pose(
	struct ragdoll *ragdoll)
{
	real_point3d low, high;
	short index;

	for (index = 0; index < ragdoll->particle_count; index++)
		ragdoll_turn(ragdoll, index);
	for (index = 0; index < ragdoll->node_count; index++)
	{
		short node_index = ragdoll->node_order[index];
		short p = ragdoll->node_particle[node_index];
		real_matrix4x3 *pose = &ragdoll->pose[node_index];

		if (p >= 0)
		{
			struct ragdoll_particle const *particle = &ragdoll->particles[p];
			real_matrix4x3 const *death = &ragdoll->local[node_index];

			pose->scale = death->scale;
			ragdoll_rotate(&particle->delta, &death->forward, &pose->forward);
			ragdoll_rotate(&particle->delta, &death->left, &pose->left);
			ragdoll_rotate(&particle->delta, &death->up, &pose->up);
			pose->position = particle->position;
		}
		else if (ragdoll->node_parent[node_index] != NONE)
			matrix4x3_multiply(&ragdoll->pose[ragdoll->node_parent[node_index]], &ragdoll->local[node_index], pose);
	}
	low = high = ragdoll->particles[0].position;
	for (index = 1; index < ragdoll->particle_count; index++)
	{
		real_point3d const *p = &ragdoll->particles[index].position;

		low.x = MIN(low.x, p->x);
		low.y = MIN(low.y, p->y);
		low.z = MIN(low.z, p->z);
		high.x = MAX(high.x, p->x);
		high.y = MAX(high.y, p->y);
		high.z = MAX(high.z, p->z);
	}
	ragdoll->center.x = 0.5f * (low.x + high.x);
	ragdoll->center.y = 0.5f * (low.y + high.y);
	ragdoll->center.z = 0.5f * (low.z + high.z);
	ragdoll->radius = 0.5f * ragdoll_distance(&low, &high) + RAGDOLL_SPHERE_MARGIN;
	ragdoll->drawn_cluster = ragdoll_cluster_of(&ragdoll->center);
}

/* a tick of an awake ragdoll; FALSE: let go (why in reason) */
static boolean ragdoll_step(
	struct ragdoll *ragdoll,
	struct object_datum *object,
	short *reason)
{
	real_matrix4x3 const *live = (real_matrix4x3 const *)object_header_block_get(ragdoll->object_index,
		&object->object.node_matrices);
	real step_gravity = global_gravity / (RAGDOLL_SUBSTEPS * RAGDOLL_SUBSTEPS);
	real most = 0.f;
	short most_index = 0;
	short substep, iteration, index;

	/* (the animation over, or the game's limp body on: limp now) */
	if (ragdoll->follow_ticks > 0 && !ragdoll_death_animation(object, TRUE))
		ragdoll->follow_ticks = 0;
	for (index = 0; index < ragdoll->particle_count; index++)
		ragdoll->particles[index].start = ragdoll->particles[index].position;
	for (substep = 0; substep < RAGDOLL_SUBSTEPS; substep++)
	{
		for (index = 0; index < ragdoll->particle_count; index++)
		{
			struct ragdoll_particle *particle = &ragdoll->particles[index];
			real_vector3d motion;

			ragdoll_between(&particle->previous, &particle->position, &motion);
			particle->previous = particle->position;
			ragdoll_move(&particle->position, &motion, RAGDOLL_DAMPING);
			particle->position.z -= step_gravity;
			particle->pushed = 0.f;
		}
		/* (one that took over from its death animation follows it at
		first, let go of a little more each tick: drawn toward where the
		animation has each node and how it moves it, nothing added) */
		if (ragdoll->follow_ticks > 0)
		{
			real strength = RAGDOLL_FOLLOW_STRENGTH * ragdoll->follow_ticks / RAGDOLL_FOLLOW_TICKS;
			real along = (real)(substep + 1) / RAGDOLL_SUBSTEPS;

			for (index = 0; index < ragdoll->particle_count; index++)
			{
				struct ragdoll_particle *particle = &ragdoll->particles[index];
				real_point3d const *now = &live[particle->node_index].position;
				real_point3d target;
				real_vector3d toward, motion, animated;

				ragdoll_between(&particle->animated, now, &animated);
				target = particle->animated;
				ragdoll_move(&target, &animated, along);
				ragdoll_between(&particle->position, &target, &toward);
				if (!(ragdoll_length(&toward) <= RAGDOLL_FOLLOW_REACH))
					continue;
				ragdoll_between(&particle->previous, &particle->position, &motion);
				ragdoll_move(&particle->position, &toward, strength);
				/* (the step's motion toward the animation's) */
				motion.i += (animated.i / RAGDOLL_SUBSTEPS - motion.i) * strength;
				motion.j += (animated.j / RAGDOLL_SUBSTEPS - motion.j) * strength;
				motion.k += (animated.k / RAGDOLL_SUBSTEPS - motion.k) * strength;
				particle->previous.x = particle->position.x - motion.i;
				particle->previous.y = particle->position.y - motion.j;
				particle->previous.z = particle->position.z - motion.k;
			}
		}
		for (iteration = 0; iteration < RAGDOLL_ITERATIONS; iteration++)
		{
			ragdoll_solve_constraints(ragdoll);
			ragdoll_solve_clusters(ragdoll);
			/* (the hinges and the planes touched every second round: a
			foot on the ground straightens its knee by moving the knee,
			then the bones and the rigid parts take it up) */
			if (iteration & 1)
			{
				ragdoll_solve_hinges(ragdoll);
				ragdoll_solve_planes(ragdoll);
			}
		}
		for (index = 0; index < ragdoll->particle_count; index++)
		{
			struct ragdoll_particle *particle = &ragdoll->particles[index];
			short plane;

			/* (a plane still touched is kept on: a body at rest stays on
			the ground without a test) */
			for (plane = 0; plane < RAGDOLL_PLANES; plane++)
			{
				if (particle->planes[plane].ticks > 0 &&
					ragdoll_plane_keep(particle, &particle->planes[plane].normal, particle->planes[plane].d))
				{
					particle->planes[plane].ticks = RAGDOLL_PLANE_TICKS;
				}
			}
		}
	}
	if (ragdoll->follow_ticks > 0)
	{
		ragdoll->follow_ticks--;
		for (index = 0; index < ragdoll->particle_count; index++)
			ragdoll->particles[index].animated = live[ragdoll->particles[index].node_index].position;
	}
	ragdoll_collide(ragdoll);

	for (index = 0; index < ragdoll->particle_count; index++)
	{
		struct ragdoll_particle const *particle = &ragdoll->particles[index];

		if (!ragdoll_finite(&particle->position) || !ragdoll_finite(&particle->previous))
		{
			ragdoll_stats.not_a_number++;
			*reason = _ragdoll_released_not_a_number;
			return FALSE;
		}
		if (ragdoll_distance(&particle->start, &particle->position) > most)
		{
			most = ragdoll_distance(&particle->start, &particle->position);
			most_index = index;
		}
	}
	if (ragdolls_log_level() >= 3 && ragdoll->age % 10 == 0)
	{
		struct ragdoll_particle const *particle = &ragdoll->particles[most_index];

		platform_log("ragdolls: %08lx age %ld: most moved %.5f, particle %d (node %d) at %.3f %.3f %.3f planes %d %d",
			(unsigned long)ragdoll->object_index, ragdoll->age, (double)most, (int)most_index, (int)particle->node_index,
			(double)particle->position.x, (double)particle->position.y, (double)particle->position.z,
			(int)particle->planes[0].ticks, (int)particle->planes[1].ticks);
	}
	if (ragdoll_distance(&ragdoll->particles[0].position, &live[ragdoll->particles[0].node_index].position) >
		RAGDOLL_TETHER)
	{
		if (ragdolls_log_level())
			platform_log("ragdolls: %08lx root at %.3f %.3f %.3f, its body's at %.3f %.3f %.3f", (unsigned long)ragdoll->object_index,
				(double)ragdoll->particles[0].position.x, (double)ragdoll->particles[0].position.y,
				(double)ragdoll->particles[0].position.z, (double)live[ragdoll->particles[0].node_index].position.x,
				(double)live[ragdoll->particles[0].node_index].position.y, (double)live[ragdoll->particles[0].node_index].position.z);
		*reason = _ragdoll_released_far;
		return FALSE;
	}
	ragdoll_pose(ragdoll);
	ragdoll->body = object->object.position;

	ragdoll->age++;
	ragdoll->awake_ticks++;
	ragdoll->still_ticks = most < RAGDOLL_STILL ? ragdoll->still_ticks + 1 : 0;
	if (ragdoll->still_ticks >= RAGDOLL_SLEEP_TICKS && ragdoll->age >= RAGDOLL_SLEEP_AGE)
		ragdoll_sleep(ragdoll, "at rest");
	else if (ragdoll->awake_ticks >= RAGDOLL_AWAKE_TICKS)
		ragdoll_sleep(ragdoll, "awake too long");
	return TRUE;
}

/* the body a ragdoll is of, while it is still a dead body (NULL: let go,
why in reason) */
static struct object_datum *ragdoll_body(
	struct ragdoll const *ragdoll,
	short *reason)
{
	struct object_datum *object = object_try_and_get(ragdoll->object_index);
	struct unit_datum const *unit = (struct unit_datum const *)object;

	if (!object || object->object.type != _object_type_biped)
	{
		*reason = _ragdoll_released_gone;
		return NULL;
	}
	if (!TEST_FLAG(object->object.damage_flags, _object_dead_bit) || unit->unit.feign_death_timer > 0)
	{
		*reason = _ragdoll_released_alive;
		return NULL;
	}
	if (object->object.parent_object_index != NONE)
	{
		*reason = _ragdoll_released_attached;
		return NULL;
	}
	if (object->object.node_matrices.size / (short)sizeof(real_matrix4x3) != ragdoll->node_count)
	{
		*reason = _ragdoll_released_changed;
		return NULL;
	}
	return object;
}

/* an awake ragdoll that may be put to sleep for a new one: one awake a
second at least and nearly at rest (one still falling, frozen, hung in the
air), the stillest of them (the longest still, then the one awake
longest); NONE: none */
static short ragdoll_room(
	void)
{
	short slot, chosen = NONE;

	for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
	{
		struct ragdoll const *ragdoll = &ragdolls[slot];

		/* (not one still falling: frozen in the air while its body flies
		on) */
		if (ragdoll->state != _ragdoll_awake || ragdoll->awake_ticks < RAGDOLL_ROOM_TICKS ||
			ragdoll->still_ticks < RAGDOLL_ROOM_STILL_TICKS)
		{
			continue;
		}
		if (chosen == NONE || ragdoll->still_ticks > ragdolls[chosen].still_ticks ||
			(ragdoll->still_ticks == ragdolls[chosen].still_ticks && ragdoll->woken < ragdolls[chosen].woken))
		{
			chosen = slot;
		}
	}
	return chosen;
}

/* ---------- public code */

void ragdolls_reset(
	void)
{
	short slot;

	for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
	{
		if (ragdolls[slot].state != _ragdoll_free)
			ragdoll_stats.released[_ragdoll_released_map]++;
		ragdolls[slot].state = _ragdoll_free;
		ragdolls[slot].object_index = NONE;
	}
	ragdoll_pending_count = 0;
	ragdoll_last_death = ragdoll_test_target = NONE;
	memset(ragdoll_notes, 0, sizeof(ragdoll_notes));
	memset(ragdoll_slot_by_object, 0, sizeof(ragdoll_slot_by_object));
	ragdoll_map_generation = halo_map_generation;
}

void ragdolls_unit_died(
	long unit_index)
{
	struct object_datum *object;

	if (!ragdolls_enabled())
		return;
	object = object_try_and_get(unit_index);
	/* (a body the game lays limp: ragdoll_build) */
	if (!object || object->object.type != _object_type_biped ||
		!TEST_FLAG(biped_definition_get(object->definition_index)->biped.flags, _biped_uses_limp_body_physics_bit))
	{
		return;
	}
	if (ragdoll_pending_count >= RAGDOLL_PENDING)
	{
		/* (the oldest forgotten) */
		memmove(ragdoll_pending, ragdoll_pending + 1, sizeof(ragdoll_pending[0]) * (RAGDOLL_PENDING - 1));
		ragdoll_pending_count--;
	}
	{
		struct ragdoll_pending *pending = &ragdoll_pending[ragdoll_pending_count];

		pending->object_index = unit_index;
		pending->tick = pending->due_tick = ragdoll_ticks;
		pending->handoff = NONE;
		pending->impact_count = NONE;
		pending->have_previous = FALSE;
		pending->previous_animation = NONE;
		pending->node_count = 0;
		pending->flying = pending->flew = FALSE;
		pending->blow = NONE;
		pending->flight_tick = ragdoll_ticks;
		pending->motion = *global_zero_vector3d;
		pending->head_node = NONE;
		pending->lean = 0.f;
	}
	ragdoll_pending_count++;
	ragdoll_last_death = unit_index;
}

void ragdolls_note_damage(
	long object_index,
	struct damage_data const *damage)
{
	struct object_datum *object;
	struct object_definition *definition;
	struct damage_effect_definition *effect;
	struct ragdoll_damage_note *note;
	struct ragdoll *ragdoll;
	short index;

	if (!damage || damage->definition_index == NONE || !ragdolls_enabled())
		return;
	object = object_try_and_get(object_index);
	if (!object || object->object.type != _object_type_biped)
		return;
	definition = object_definition_get(object->definition_index);
	effect = damage_effect_definition_get(damage->definition_index);
	if (!definition || !effect)
		return;
	note = &ragdoll_notes[ragdoll_next_note];
	ragdoll_next_note = (short)((ragdoll_next_note + 1) % RAGDOLL_DAMAGE_NOTES);
	note->object_index = object_index;
	note->tick = ragdoll_ticks;
	/* (a melee blow first: the weapons' melee effects have a small radius
	of their own, the assault rifle's for one, but are not explosions) */
	note->melee = effect->damage.category == RAGDOLL_DAMAGE_CATEGORY_MELEE;
	note->explosion = !note->melee && effect->cutoff_radius > 0.f;
	note->vehicle = FALSE;
	{
		struct game_globals *globals = scenario_get_game_globals();

		if (globals && globals->falling_damage.count > 0)
		{
			struct ragdoll_falling_damage const *falling = TAG_BLOCK_GET_ELEMENT(&globals->falling_damage, 0,
				struct ragdoll_falling_damage);

			note->vehicle = falling->vehicle_collision_damage.index != NONE &&
				falling->vehicle_collision_damage.index == damage->definition_index;
		}
	}
	note->origin = damage->origin;
	note->direction = damage->direction;
	if (ragdoll_normalize(&note->direction) < 1e-5f)
	{
		note->direction = *global_zero_vector3d;
		note->direction.k = 1.f;
	}
	/* (object_damage_aftermath's acceleration, world units a tick; an
	explosion's by how near) */
	note->speed = MAX(0.f, effect->damage.instantaneous_acceleration) * definition->object.acceleration_scale /
		TICKS_PER_SECOND;
	if (note->explosion)
		note->speed *= PIN(damage->scale, 0.f, 1.f);
	if (!(note->speed >= 0.f) || !ragdoll_finite(&note->origin))
	{
		note->object_index = NONE;
		return;
	}
	/* (a ragdoll struck: thrown again, awake) */
	ragdoll = ragdoll_of_object(object_index);
	if (ragdoll && (note->explosion || note->melee || note->speed > 0.f))
	{
		if (ragdoll->state == _ragdoll_asleep)
		{
			ragdoll->state = _ragdoll_awake;
			ragdoll->still_ticks = 0;
			ragdoll->awake_ticks = 0;
			ragdoll->woken = ++ragdoll_sequence;
			ragdoll_stats.woken++;
			for (index = 0; index < ragdoll->particle_count; index++)
				ragdoll->particles[index].previous = ragdoll->particles[index].position;
			if (ragdolls_log_level())
				platform_log("ragdolls: %08lx (%s) woken by a blow", (unsigned long)object_index,
					ragdoll_object_name(object_index));
		}
		ragdoll_apply_note(ragdoll, note, FALSE);
		/* (the whole body thrown as object_damage_aftermath throws it -
		a little upward -, no faster than a body's launch) */
		{
			real_vector3d direction = note->direction;
			real speed = MIN(note->speed, RAGDOLL_LAUNCH_SPEED);

			direction.k += 0.45f;
			if (speed > 0.f && ragdoll_normalize(&direction) > 1e-5f)
			{
				for (index = 0; index < ragdoll->particle_count; index++)
				{
					ragdoll->particles[index].previous.x -= direction.i * speed / RAGDOLL_SUBSTEPS;
					ragdoll->particles[index].previous.y -= direction.j * speed / RAGDOLL_SUBSTEPS;
					ragdoll->particles[index].previous.z -= direction.k * speed / RAGDOLL_SUBSTEPS;
				}
			}
		}
	}
}

void ragdolls_tick(
	void)
{
	unsigned long long started;
	short awake = 0, kept = 0;
	short slot, index, starts;
	short cap;

	ragdoll_ticks++;
	if (!ragdolls_enabled())
	{
		/* (set off: every body as the game lays it again) */
		for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
			ragdoll_release(slot, _ragdoll_released_setting);
		ragdoll_pending_count = 0;
		return;
	}
	/* (a revert, a saved game, a new map: the bodies are not these) */
	if (ragdoll_map_generation != halo_map_generation)
	{
		ragdolls_release_all(_ragdoll_released_map);
		ragdoll_map_generation = halo_map_generation;
	}
	/* (nothing to do: not even the clock read, a system call on the Vita) */
	for (slot = 0; slot < RAGDOLL_SLOTS && ragdolls[slot].state == _ragdoll_free; slot++)
		;
	if (slot == RAGDOLL_SLOTS && !ragdoll_pending_count)
		return;
	started = vita_host_time_us();
	cap = ragdolls_active_cap_value;

	/* (the cap lowered by the setting: the ones awake longest put to
	sleep) */
	for (;;)
	{
		short oldest = NONE;

		awake = 0;
		for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
		{
			if (ragdolls[slot].state != _ragdoll_awake)
				continue;
			awake++;
			if (oldest == NONE || ragdolls[slot].woken < ragdolls[oldest].woken)
				oldest = slot;
		}
		if (awake <= cap || oldest == NONE)
			break;
		ragdoll_sleep(&ragdolls[oldest], "too many awake");
	}

	/* (the bodies that died, in the order they died: each started once on
	its own, out of a seat, with room among the awake - made by putting to
	sleep one that has fallen a while, the stillest -, else it waits a
	while, its death animation playing, and is started from where that has
	it then) */
	for (index = 0, starts = 0; index < ragdoll_pending_count;)
	{
		struct ragdoll_pending *pending = &ragdoll_pending[index];
		struct object_datum *object = object_try_and_get(pending->object_index);
		struct unit_datum const *unit = (struct unit_datum const *)object;
		boolean done = TRUE;

		if (object && object->object.type == _object_type_biped &&
			TEST_FLAG(object->object.damage_flags, _object_dead_bit) && unit->unit.feign_death_timer <= 0)
		{
			short room = awake < cap ? NONE : ragdoll_room();

			/* (its death animation playing until its ragdoll is due) */
			if (pending->handoff == NONE)
			{
				unsigned long long watching = vita_host_time_us(), spent;

				ragdoll_watch(pending, object);
				spent = vita_host_time_us() - watching;
				ragdoll_stats.watched++;
				ragdoll_stats.watch_us += spent;
				ragdoll_stats.watch_most_us = MAX(ragdoll_stats.watch_most_us, spent);
			}
			if (pending->handoff == NONE)
				done = FALSE;
			/* (two a tick at most: a grenade's four deaths at once start
			over two ticks, the tick's cost spread) */
			else if (starts >= RAGDOLL_STARTS_PER_TICK)
				done = FALSE;
			else if (object->object.parent_object_index == NONE && (awake < cap || room != NONE))
			{
				starts++;
				if (room != NONE)
				{
					ragdoll_sleep(&ragdolls[room], "made room");
					awake--;
				}
				awake += ragdoll_start(pending->object_index, object, pending);
			}
			else if (ragdoll_ticks - pending->due_tick < RAGDOLL_PENDING_TICKS)
				done = FALSE;
			/* (due but waiting: started from where its animation has it then;
			its pose now kept, or, its animation over, none) */
			if (!done && pending->handoff != NONE)
			{
				short node_count = (short)(object->object.node_matrices.size / (short)sizeof(real_matrix4x3));

				if (ragdoll_death_animation(object, pending->flying) && node_count >= 1 &&
					node_count <= RAGDOLL_MAXIMUM_NODES)
				{
					ragdoll_keep_previous(pending, object, (real_matrix4x3 const *)object_header_block_get(
						pending->object_index, &object->object.node_matrices), node_count);
				}
				else
					pending->have_previous = FALSE;
			}
		}
		if (done)
		{
			memmove(pending, pending + 1, sizeof(*pending) * (ragdoll_pending_count - index - 1));
			ragdoll_pending_count--;
		}
		else
			index++;
	}

	awake = 0;
	for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
	{
		struct ragdoll *ragdoll = &ragdolls[slot];
		struct object_datum *object;
		short reason = _ragdoll_released_gone;

		if (ragdoll->state == _ragdoll_free)
			continue;
		object = ragdoll_body(ragdoll, &reason);
		if (!object)
		{
			ragdoll_release(slot, reason);
			continue;
		}
		/* (asleep, its body moved far by the game - pushed by a vehicle,
		put elsewhere by a script; a blast that strikes it wakes it, as it
		throws the body -: drawn as the game has it again; not while the
		game's body is still flying from the death, thrown further than the
		ragdoll) */
		if (ragdoll->state == _ragdoll_asleep && ragdoll_ticks - ragdoll->started_tick < RAGDOLL_FLIGHT_TICKS)
			ragdoll->body = object->object.position;
		else if (ragdoll->state == _ragdoll_asleep &&
			ragdoll_distance(&ragdoll->body, &object->object.position) > RAGDOLL_BODY_MOVED)
		{
			ragdoll_release(slot, _ragdoll_released_body_moved);
			continue;
		}
		kept++;
		if (ragdoll->state != _ragdoll_awake)
			continue;
		awake++;
		ragdoll_stats.steps++;
		if (!ragdoll_step(ragdoll, object, &reason))
		{
			ragdoll_release(slot, reason);
			kept--;
		}
	}
	ragdoll_stats.most_awake = MAX(ragdoll_stats.most_awake, awake);
	ragdoll_stats.most_kept = MAX(ragdoll_stats.most_kept, kept);
	if (awake)
	{
		unsigned long long elapsed = vita_host_time_us() - started;

		ragdoll_stats.busy_ticks++;
		ragdoll_stats.busy_us += elapsed;
		ragdoll_stats.most_us = MAX(ragdoll_stats.most_us, elapsed);
		ragdoll_stats.window_ticks++;
		ragdoll_stats.window_us += elapsed;
		ragdoll_stats.window_most_us = MAX(ragdoll_stats.window_most_us, elapsed);
	}
	/* (HALO_RAGDOLLS_LOG: the cost, every ten seconds while any is kept) */
	if (ragdolls_log_level() && ragdoll_ticks % (10 * TICKS_PER_SECOND) == 0 && (kept || ragdoll_stats.window_ticks))
	{
		platform_log("ragdolls: %d kept, %d awake; %lu ticks busy in 10 s, %.3f ms a tick on them, at most %.3f; "
			"%lu vectors", (int)kept, (int)awake, ragdoll_stats.window_ticks,
			ragdoll_stats.window_ticks ? (double)ragdoll_stats.window_us / ragdoll_stats.window_ticks / 1000.0 : 0.0,
			(double)ragdoll_stats.window_most_us / 1000.0, ragdoll_stats.rays - ragdoll_stats.window_rays);
		ragdoll_stats.window_ticks = 0;
		ragdoll_stats.window_us = ragdoll_stats.window_most_us = 0;
		ragdoll_stats.window_rays = ragdoll_stats.rays;
	}
}

/* ---------- the render's poses */

boolean ragdolls_object_has_pose(
	long object_index)
{
	return ragdoll_of_object(object_index) != NULL;
}

boolean ragdolls_pose_override(
	long object_index,
	real_matrix4x3 *matrices,
	short node_count,
	real_point3d *center,
	real *radius)
{
	struct ragdoll const *ragdoll = ragdoll_of_object(object_index);

	if (!ragdoll || ragdoll->node_count != node_count)
		return FALSE;
	ragdoll_stats.copied++;
	memcpy(matrices, ragdoll->pose, node_count * sizeof(real_matrix4x3));
	/* (the detail level, the lighting's and fog's centroid: the drawn
	body's) */
	*center = ragdoll->center;
	*radius = ragdoll->radius;
	return TRUE;
}

real_matrix4x3 *ragdolls_live_node_matrices(
	long object_index)
{
	struct ragdoll *ragdoll = ragdoll_of_object(object_index);

	if (!ragdoll)
		return NULL;
	ragdoll_stats.read_live++;
	return ragdoll->pose;
}

boolean ragdolls_live_bounding_sphere(
	long object_index,
	real_point3d *center,
	real *radius)
{
	struct ragdoll const *ragdoll = ragdoll_of_object(object_index);

	if (!ragdoll)
		return FALSE;
	*center = ragdoll->center;
	*radius = ragdoll->radius;
	return TRUE;
}

/* the ragdolls kept, their spheres and the clusters their middles are in
(the tick's thread copying the poses, or the render with no tick
running): the render draws one in a cluster it draws, wherever the game
has its body (render_interpolation.c) */
short ragdolls_drawn_list(
	long *objects,
	real_point3d *centers,
	real *radii,
	short *clusters,
	short maximum)
{
	short slot, count = 0;

	for (slot = 0; slot < RAGDOLL_SLOTS && count < maximum; slot++)
	{
		struct ragdoll const *ragdoll = &ragdolls[slot];

		if (ragdoll->state == _ragdoll_free || ragdoll->map_generation != halo_map_generation ||
			ragdoll->drawn_cluster == NONE)
		{
			continue;
		}
		objects[count] = ragdoll->object_index;
		centers[count] = ragdoll->center;
		radii[count] = ragdoll->radius;
		clusters[count] = ragdoll->drawn_cluster;
		count++;
	}
	return count;
}

/* ---------- tests */

/* the living characters nearest the first local player's unit, not a
player's, within range: at most count, nearest first */
static short ragdoll_test_targets(
	long *targets,
	short count,
	real range,
	char const *name_filter,
	long *player_index_reference)
{
	long player_index = local_player_get_player_index(0);
	struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
	struct object_datum *unit = player && player->unit_index != NONE ? object_try_and_get(player->unit_index) : NULL;
	long candidates[64];
	real distances[64];
	struct object_iterator iterator;
	struct object_datum *object;
	short candidate_count = 0;
	short found;

	*player_index_reference = player_index;
	if (!unit || count <= 0)
		return 0;
	object_iterator_new(&iterator, _object_mask_biped, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL &&
		candidate_count < (short)NUMBEROF(candidates))
	{
		struct unit_datum const *other = (struct unit_datum const *)object;
		real distance = ragdoll_distance(&object->object.position, &unit->object.position);

		if (iterator.index == player->unit_index || other->unit.player_index != NONE ||
			TEST_FLAG(object->object.damage_flags, _object_dead_bit) || object->object.parent_object_index != NONE ||
			distance > range || (name_filter && name_filter[0] && !strstr(ragdoll_object_name(iterator.index), name_filter)))
		{
			continue;
		}
		candidates[candidate_count] = iterator.index;
		distances[candidate_count] = distance;
		candidate_count++;
	}
	/* (the nearest first) */
	for (found = 0; found < count && found < candidate_count; found++)
	{
		short nearest = found, index;

		for (index = (short)(found + 1); index < candidate_count; index++)
		{
			if (distances[index] < distances[nearest])
				nearest = index;
		}
		targets[found] = candidates[nearest];
		candidates[nearest] = candidates[found];
		distances[nearest] = distances[found];
	}
	return found;
}

static void ragdoll_test_check(
	void)
{
	short slot, index;
	short kept = 0, awake = 0, particles = 0, outside = 0, not_a_number = 0, hinges = 0, wrong = 0;
	real farthest = 0.f;

	for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
	{
		struct ragdoll const *ragdoll = &ragdolls[slot];
		struct object_datum *object;

		if (ragdoll->state == _ragdoll_free || !ragdoll_of_object(ragdoll->object_index))
			continue;
		kept++;
		awake += ragdoll->state == _ragdoll_awake;
		object = object_try_and_get(ragdoll->object_index);
		for (index = 0; index < ragdoll->particle_count; index++)
		{
			real_point3d const *p = &ragdoll->particles[index].position;

			particles++;
			if (!ragdoll_finite(p))
			{
				not_a_number++;
				continue;
			}
			if (collision_test_point(RAGDOLL_SOLID_FLAGS, p, NONE))
			{
				outside++;
				platform_log("ragdolls check: solid: %08lx (%s) %s, %ld ticks: particle %d (node %d, %s) at %.3f %.3f %.3f",
					(unsigned long)ragdoll->object_index, ragdoll_object_name(ragdoll->object_index),
					ragdoll->state == _ragdoll_awake ? "awake" : "asleep", ragdoll->age, (int)index,
					(int)ragdoll->particles[index].node_index, ragdoll->particles[index].collides ? "tested" : "near its centre",
					(double)p->x, (double)p->y, (double)p->z);
			}
		}
		for (index = 0; index < ragdoll->node_count; index++)
		{
			if (!ragdoll_finite(&ragdoll->pose[index].position) || !ragdoll_finite((real_point3d const *)&ragdoll->pose[index].forward))
				not_a_number++;
		}
		/* (a hinge bent the wrong way more than 10 degrees, or 20 out of
		its plane) */
		for (index = 0; index < ragdoll->particle_count; index++)
		{
			struct ragdoll_particle const *joint = &ragdoll->particles[index];
			real_vector3d axis, above, below, side;

			if (!joint->hinge)
				continue;
			hinges++;
			ragdoll_rotate(&ragdoll->particles[joint->parent].delta, &joint->hinge_axis, &axis);
			ragdoll_between(&ragdoll->particles[joint->parent].position, &joint->position, &above);
			ragdoll_between(&joint->position, &ragdoll->particles[joint->child].position, &below);
			ragdoll_normalize(&above);
			ragdoll_normalize(&below);
			ragdoll_cross(&above, &below, &side);
			if (ragdoll_dot(&side, &axis) < -0.174f || ragdoll_dot(&below, &axis) > 0.342f ||
				ragdoll_dot(&below, &axis) < -0.342f)
			{
				wrong++;
				platform_log("ragdolls check: hinge: %08lx (%s) %s, %ld ticks: particle %d (node %d) bent %.3f, out of its plane %.3f",
					(unsigned long)ragdoll->object_index, ragdoll_object_name(ragdoll->object_index),
					ragdoll->state == _ragdoll_awake ? "awake" : "asleep", ragdoll->age, (int)index, (int)joint->node_index,
					(double)ragdoll_dot(&side, &axis), (double)ragdoll_dot(&below, &axis));
			}
		}
		if (object)
		{
			real_matrix4x3 const *live = (real_matrix4x3 const *)object_header_block_get(ragdoll->object_index,
				&object->object.node_matrices);
			real distance = ragdoll_distance(&ragdoll->particles[0].position, &live[ragdoll->particles[0].node_index].position);

			farthest = MAX(farthest, distance);
			if (distance > RAGDOLL_TETHER)
			{
				platform_log("ragdolls check: far: %08lx (%s) %s, %ld ticks: its root at %.3f %.3f %.3f, its body at %.3f %.3f %.3f",
					(unsigned long)ragdoll->object_index, ragdoll_object_name(ragdoll->object_index),
					ragdoll->state == _ragdoll_awake ? "awake" : "asleep", ragdoll->age,
					(double)ragdoll->particles[0].position.x, (double)ragdoll->particles[0].position.y,
					(double)ragdoll->particles[0].position.z, (double)object->object.position.x,
					(double)object->object.position.y, (double)object->object.position.z);
			}
		}
	}
	platform_log("ragdolls check: %d kept, %d awake, %d particles, %d in solid structure, %d not a number, "
		"farthest from its body's root %.3f, %d of %d hinges bent the wrong way", (int)kept, (int)awake, (int)particles,
		(int)outside, (int)not_a_number, (double)farthest, (int)wrong, (int)hinges);
}

static void ragdoll_test_camera(
	char const *arguments)
{
	float distance = 2.5f, yaw = 90.f, pitch = 25.f;
	long shown = ragdoll_test_target != NONE && object_try_and_get(ragdoll_test_target) ? ragdoll_test_target :
		ragdoll_last_death;
	struct object_datum *object = shown != NONE ? object_try_and_get(shown) : NULL;
	struct ragdoll const *ragdoll = object ? ragdoll_of_object(shown) : NULL;
	real_point3d center, position;
	real_vector3d forward;
	real facing, up;
	FILE *file;

	sscanf(arguments, " %*s %f %f %f", &distance, &yaw, &pitch);
	if (!object)
	{
		platform_log("ragdolls cam: no body");
		return;
	}
	center = ragdoll ? ragdoll->center : object->object.bounding_sphere_center;
	facing = (real)atan2(object->object.forward.j, object->object.forward.i) + RAGDOLL_DEGREES(yaw);
	up = RAGDOLL_DEGREES(pitch);
	position.x = center.x + distance * cosf(facing) * cosf(up);
	position.y = center.y + distance * sinf(facing) * cosf(up);
	position.z = center.z + distance * sinf(up);
	ragdoll_between(&position, &center, &forward);
	ragdoll_normalize(&forward);
	/* (as main.c's @camera: director_load_camera's file) */
	file = fopen("d:\\camera.txt", "w");
	if (!file)
		return;
	fprintf(file, "%f %f %f\n%f %f %f\n%f %f %f\n%f\n", (double)position.x, (double)position.y, (double)position.z,
		(double)forward.i, (double)forward.j, (double)forward.k, 0.0, 0.0, 1.0, (double)RAGDOLL_DEGREES(70.f));
	fclose(file);
	director_load_camera();
	platform_log("ragdolls cam: %08lx (%s, %s) from %.2f %.2f %.2f", (unsigned long)shown,
		ragdoll_object_name(shown), ragdoll ? "its ragdoll" : "its own pose", (double)position.x,
		(double)position.y, (double)position.z);
}

void ragdolls_test_command(
	char const *arguments)
{
	char word[16] = "", filter[32] = "";
	int count = 3;
	float range = 10.f;

	if (!arguments)
		return;
	sscanf(arguments, " %15s %d %f %31s", word, &count, &range, filter);
	if (!strcmp(word, "stats"))
	{
		short slot, kept = 0, awake = 0;
		unsigned long released = 0;

		for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
		{
			if (ragdolls[slot].state == _ragdoll_free || !ragdoll_of_object(ragdolls[slot].object_index))
				continue;
			kept++;
			awake += ragdolls[slot].state == _ragdoll_awake;
		}
		for (slot = 0; slot < NUMBER_OF_RAGDOLL_RELEASE_REASONS; slot++)
			released += ragdoll_stats.released[slot];
		platform_log("ragdolls stats: %d kept, %d awake (most %d kept, %d awake, cap %d); %lu started, %lu not made, "
			"%lu asleep, %lu woken, %lu let go (%lu replaced, %lu game state replaced, %lu gone, %lu alive, "
			"%lu setting); %lu busy ticks, %.3f ms a tick, at most %.3f; %lu vectors, %lu point tests; "
			"%lu brought out of solid at the start, %lu back from solid, %lu not a number; "
			"poses drawn: %lu copied with the tick's, %lu read; %lu steps, %.3f ms a start",
			(int)kept, (int)awake, (int)ragdoll_stats.most_kept, (int)ragdoll_stats.most_awake,
			(int)ragdolls_active_cap_value, ragdoll_stats.started, ragdoll_stats.rejected, ragdoll_stats.slept,
			ragdoll_stats.woken, released, ragdoll_stats.released[_ragdoll_released_replaced],
			ragdoll_stats.released[_ragdoll_released_map], ragdoll_stats.released[_ragdoll_released_gone],
			ragdoll_stats.released[_ragdoll_released_alive], ragdoll_stats.released[_ragdoll_released_setting],
			ragdoll_stats.busy_ticks,
			ragdoll_stats.busy_ticks ? (double)ragdoll_stats.busy_us / ragdoll_stats.busy_ticks / 1000.0 : 0.0,
			(double)ragdoll_stats.most_us / 1000.0, ragdoll_stats.rays, ragdoll_stats.point_tests,
			ragdoll_stats.start_fixed, ragdoll_stats.solid_reverts, ragdoll_stats.not_a_number, ragdoll_stats.copied,
			ragdoll_stats.read_live, ragdoll_stats.steps,
			ragdoll_stats.started ? (double)ragdoll_stats.start_us / ragdoll_stats.started / 1000.0 : 0.0);
		platform_log("ragdolls handoffs: death animations watched %lu ticks, %.4f ms a tick, at most %.3f, %lu point "
			"tests; %d waiting; due: %lu explosion, %lu melee, %lu vehicle, %lu big blow, %lu in the air, %lu seat, "
			"%lu no animation, %lu no airborne death, %lu animation over, %lu key frame, %lu falling, "
			"%lu meeting the level, %lu landed, %lu partway", ragdoll_stats.watched,
			ragdoll_stats.watched ? (double)ragdoll_stats.watch_us / ragdoll_stats.watched / 1000.0 : 0.0,
			(double)ragdoll_stats.watch_most_us / 1000.0, ragdoll_stats.watch_points, (int)ragdoll_pending_count,
			ragdoll_stats.handoffs[_ragdoll_handoff_explosion], ragdoll_stats.handoffs[_ragdoll_handoff_melee],
			ragdoll_stats.handoffs[_ragdoll_handoff_vehicle], ragdoll_stats.handoffs[_ragdoll_handoff_blow],
			ragdoll_stats.handoffs[_ragdoll_handoff_airborne], ragdoll_stats.handoffs[_ragdoll_handoff_seat],
			ragdoll_stats.handoffs[_ragdoll_handoff_no_animation],
			ragdoll_stats.handoffs[_ragdoll_handoff_no_airborne_animation],
			ragdoll_stats.handoffs[_ragdoll_handoff_animation_over], ragdoll_stats.handoffs[_ragdoll_handoff_key_frame],
			ragdoll_stats.handoffs[_ragdoll_handoff_fallen], ragdoll_stats.handoffs[_ragdoll_handoff_impact],
			ragdoll_stats.handoffs[_ragdoll_handoff_landed], ragdoll_stats.handoffs[_ragdoll_handoff_time]);
	}
	else if (!strcmp(word, "check"))
		ragdoll_test_check();
	/* (the debug camera put to look at the last body to die - its ragdoll's
	middle, or, its death animation still playing, its own - from distance
	world units, yaw degrees about it from its facing and pitch degrees up:
	"cam distance yaw pitch", for the screenshots) */
	else if (!strcmp(word, "cam"))
		ragdoll_test_camera(arguments);
	/* (a ragdoll's pose, node by node, and its particles: slot count) */
	else if (!strcmp(word, "nodes") && VALID_INDEX(count, RAGDOLL_SLOTS) && ragdolls[count].state != _ragdoll_free)
	{
		struct ragdoll const *ragdoll = &ragdolls[count];
		short index;

		for (index = 0; index < ragdoll->node_count; index++)
		{
			real_matrix4x3 const *m = &ragdoll->pose[index];

			platform_log("ragdolls nodes: %08lx node %d (particle %d) at %.3f %.3f %.3f scale %.3f forward %.3f %.3f %.3f up %.3f %.3f %.3f",
				(unsigned long)ragdoll->object_index, (int)index, (int)ragdoll->node_particle[index], (double)m->position.x,
				(double)m->position.y, (double)m->position.z, (double)m->scale, (double)m->forward.i, (double)m->forward.j,
				(double)m->forward.k, (double)m->up.i, (double)m->up.j, (double)m->up.k);
		}
	}
	/* (where each is: a camera put to look at one, @camera) */
	else if (!strcmp(word, "where"))
	{
		short slot;

		for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
		{
			struct ragdoll const *ragdoll = &ragdolls[slot];
			struct object_datum *object;

			if (ragdoll->state == _ragdoll_free || !ragdoll_of_object(ragdoll->object_index))
				continue;
			object = object_try_and_get(ragdoll->object_index);
			platform_log("ragdolls where: %08lx (%s) %s at %.3f %.3f %.3f, its body at %.3f %.3f %.3f (%s, %s)",
				(unsigned long)ragdoll->object_index, ragdoll_object_name(ragdoll->object_index),
				ragdoll->state == _ragdoll_awake ? "awake" : "asleep", (double)ragdoll->center.x,
				(double)ragdoll->center.y, (double)ragdoll->center.z, object ? (double)object->object.position.x : 0.0,
				object ? (double)object->object.position.y : 0.0, object ? (double)object->object.position.z : 0.0,
				TEST_FLAG(object_header_get(ragdoll->object_index)->flags, _object_header_active_bit) ? "active" : "inactive",
				object && TEST_FLAG(object->object.flags, _object_invisible_bit) ? "invisible" : "visible");
		}
	}
	else if (!strcmp(word, "boom") || !strcmp(word, "kill") || !strcmp(word, "punch") || !strcmp(word, "shoot"))
	{
		long targets[RAGDOLL_SLOTS * 2];
		long player_index;
		short found = ragdoll_test_targets(targets, (short)PIN(count, 0, (int)NUMBEROF(targets)), range, filter,
			&player_index);
		long effect = NONE;
		short index;

		if (!strcmp(word, "punch"))
		{
			struct tag_iterator tags;
			long tag_index;

			/* (a melee damage effect: one named so first) */
			tag_iterator_new(&tags, DAMAGE_EFFECT_DEFINITION_TAG);
			while ((tag_index = tag_iterator_next(&tags)) != NONE)
			{
				if (damage_effect_definition_get(tag_index)->damage.category != RAGDOLL_DAMAGE_CATEGORY_MELEE)
					continue;
				if (strstr(tag_get_name(tag_index), "melee"))
				{
					effect = tag_index;
					break;
				}
				if (effect == NONE)
					effect = tag_index;
			}
		}
		else if (!strcmp(word, "shoot"))
		{
			struct tag_iterator tags;
			long tag_index;

			/* (an assault rifle bullet, else any bullet) */
			tag_iterator_new(&tags, DAMAGE_EFFECT_DEFINITION_TAG);
			while ((tag_index = tag_iterator_next(&tags)) != NONE)
			{
				char const *name = tag_get_name(tag_index);
				struct damage_effect_definition const *definition = damage_effect_definition_get(tag_index);

				if (definition->damage.category == RAGDOLL_DAMAGE_CATEGORY_MELEE || definition->cutoff_radius > 0.f ||
					!strstr(name, "bullet"))
				{
					continue;
				}
				if (strstr(name, "assault rifle"))
				{
					effect = tag_index;
					break;
				}
				if (effect == NONE)
					effect = tag_index;
			}
		}
		else if (!strcmp(word, "boom"))
		{
			struct tag_iterator tags;
			long tag_index;

			/* (a frag grenade's explosion, else any explosion) */
			tag_iterator_new(&tags, DAMAGE_EFFECT_DEFINITION_TAG);
			while ((tag_index = tag_iterator_next(&tags)) != NONE)
			{
				char const *name = tag_get_name(tag_index);

				if (strstr(name, "frag grenade\\explosion"))
				{
					effect = tag_index;
					break;
				}
				if (effect == NONE && strstr(name, "explosion") &&
					damage_effect_definition_get(tag_index)->cutoff_radius > 0.f)
				{
					effect = tag_index;
				}
			}
		}
		for (index = 0; index < found; index++)
		{
			struct object_datum *target = object_try_and_get(targets[index]);

			if (!target)
				continue;
			ragdoll_test_target = targets[index];
			if (effect != NONE && (!strcmp(word, "punch") || !strcmp(word, "shoot")))
			{
				struct damage_data damage;
				struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
				struct object_datum *puncher = player && player->unit_index != NONE ?
					object_try_and_get(player->unit_index) : NULL;

				damage_data_new(&damage, effect);
				/* (at its chest, from the player: lethal) */
				damage.origin = target->object.position;
				damage.origin.z += 0.2f;
				damage.epicenter = damage.origin;
				if (puncher)
				{
					damage.direction.i = target->object.position.x - puncher->object.position.x;
					damage.direction.j = target->object.position.y - puncher->object.position.y;
					damage.direction.k = 0.f;
					damage.owner_player_index = player_index;
					damage.owner_object_index = player->unit_index;
					damage.owner_team_index = (short)player->team_index;
				}
				else
				{
					damage.direction = *global_forward3d;
				}
				ragdoll_normalize(&damage.direction);
				scenario_location_from_point(&damage.location, &damage.origin);
				damage.scale = 1.f;
				damage.multiplier = 100.f;
				platform_log("ragdolls test: %s %s %08lx (%s)", tag_get_name(effect),
					!strcmp(word, "punch") ? "punches" : "shoots", (unsigned long)targets[index],
					ragdoll_object_name(targets[index]));
				object_cause_damage(&damage, targets[index], NONE, NONE, NONE, NULL);
			}
			else if (effect != NONE)
			{
				struct damage_data damage;
				struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;

				damage_data_new(&damage, effect);
				/* (at its feet, a little beside it) */
				damage.origin = target->object.position;
				damage.origin.x += 0.15f;
				damage.origin.y += 0.1f;
				damage.origin.z += 0.05f;
				damage.epicenter = damage.origin;
				scenario_location_from_point(&damage.location, &damage.origin);
				damage.scale = 1.f;
				if (player)
				{
					damage.owner_player_index = player_index;
					damage.owner_object_index = player->unit_index;
					damage.owner_team_index = (short)player->team_index;
				}
				platform_log("ragdolls test: %s at %.2f %.2f %.2f beside %08lx (%s)", tag_get_name(effect),
					(double)damage.origin.x, (double)damage.origin.y, (double)damage.origin.z,
					(unsigned long)targets[index], ragdoll_object_name(targets[index]));
				area_of_effect_cause_damage(&damage, NONE);
			}
			else
			{
				platform_log("ragdolls test: %08lx (%s) killed", (unsigned long)targets[index],
					ragdoll_object_name(targets[index]));
				damage_kill_object_for_player(targets[index], player_index);
			}
		}
		platform_log("ragdolls test: %s, %d of %d within %.1f", word, (int)found, count, (double)range);
	}
	else
		platform_log("ragdolls test: unknown \"%s\" (boom, punch, shoot, kill, stats, check, where, cam)", arguments);
}
