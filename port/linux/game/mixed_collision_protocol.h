/*
MIXED_COLLISION_PROTOCOL.H

(1.1.3, the owner's flag stand decision, Nov 24 2026) Mixed play's last gap: an MCC-files host's multiplayer game
with an Xbox-files machine in it. MCC's files have no collision model for the flag stand and the teleporter base
(scenery\flag_base\flag_base, scenery\teleporter_base\teleporter_base: both placed in every game type), which the
Xbox's have. The Xbox machine joining sends the host the collision models the host says it lacks, read from that
machine's own files as it loaded the map; the host keeps them in memory alone (never written to a disk, never sent
on to another machine), checks them strictly, and then plays with them as the Xbox plays: its collision decides.
Nothing of the game is shipped: the collision comes from the joining player's own files, at runtime.

Plain C, no game headers (the fuzz target port/vita/tests/net_fuzz_mixed_collision.c takes all of it).

The wire (mixed_coop_protocol.h's lobby and distributed kinds, never the crossplay wire):

- In the lobby, after a machine of the Xbox's files answered the host's hello: the host's
  _mixed_coop_lobby_collision_ask, its flags the models it lacks (MIXED_COLLISION_*_BIT); the machine's
  _mixed_coop_lobby_collision_offer, its flags those it will send (none: the host waits for nothing). An older
  build drops either command (a command it does not know): it plays, or is refused, as before.
- Once the machine has loaded the map, before its dictionary request: the stream below in pieces (the
  distributed kind DISTRIBUTED_MESSAGE_MIXED_COLLISION, a struct mixed_coop_fragment each, reliable, to the host
  alone). The host answers the machine's dictionary request only once the stream is whole and taken, or refused.

The stream:

	u32 magic 'MXC1', u16 version (1), u8 level, u8 model count (1 or 2)
	model count x { u8 model (MIXED_COLLISION_MODEL_*), u8 0, u16 0, u32 bytes, bytes: the model }
	u32 check (FNV-1a of every byte before it)

A model: its collision model tag as the game lays it out in memory (32-bit), depth first: the root's bytes
(MIXED_COLLISION_ROOT_BYTES), then each of its blocks in the order below - the block's elements' bytes, then
each element's own blocks, likewise. In every element: a block's address and definition are 0 (its count is
the count of elements that follow), a tag reference's name 0 and its length 0 and its index none (a reference
to a tag is never sent: such a model is not sent). Nothing in it is a pointer.

What the host takes (mixed_collision_model_rebuild): every count no more than the game's limit and the model's
bytes all of them, every index into its own blocks (or none where the game takes none), every float finite,
every string terminated in its field, the bsps' trees each child after its parent (no loop), each surface's edge
ring closed within MIXED_COLLISION_MAXIMUM_RING edges; then (mixed_coop.c) the game's own tag check of it
(tag_validate.c, as a scene pack's: no correction allowed) and its digest (BLAKE2b-256 of the model's bytes) one
of the Xbox's files' (mixed_collision_known_digest). Anything else: not taken, the machine refused as before.
*/

#ifndef HALO_MIXED_COLLISION_PROTOCOL_H
#define HALO_MIXED_COLLISION_PROTOCOL_H

#include <stdint.h>

#define MIXED_COLLISION_MAGIC 0x3143584Du /* 'MXC1' little-endian */
#define MIXED_COLLISION_VERSION 1

enum
{
	MIXED_COLLISION_MODEL_FLAG_STAND = 0,
	MIXED_COLLISION_MODEL_TELEPORTER_BASE,
	NUMBER_OF_MIXED_COLLISION_MODELS
};

/* (the ask's and the offer's flags) */
#define MIXED_COLLISION_FLAG_STAND_BIT (1u << MIXED_COLLISION_MODEL_FLAG_STAND)
#define MIXED_COLLISION_TELEPORTER_BASE_BIT (1u << MIXED_COLLISION_MODEL_TELEPORTER_BASE)
#define MIXED_COLLISION_ALL_BITS (MIXED_COLLISION_FLAG_STAND_BIT | MIXED_COLLISION_TELEPORTER_BASE_BIT)

/* the scenery and its collision model's path, by model */
char const *mixed_collision_model_path(int model);

/* a model's most bytes, and the stream's */
#define MIXED_COLLISION_MAXIMUM_MODEL_BYTES (64 * 1024)
#define MIXED_COLLISION_MAXIMUM_STREAM_BYTES (4 + 2 + 1 + 1 + NUMBER_OF_MIXED_COLLISION_MODELS * (8 + MIXED_COLLISION_MAXIMUM_MODEL_BYTES) + 4)
/* the stream's pieces (struct mixed_coop_fragment's data, MIXED_COOP_FRAGMENT_BYTES each) */
#define MIXED_COLLISION_FRAGMENT_BYTES 3200
#define MIXED_COLLISION_MAXIMUM_FRAGMENTS \
	((MIXED_COLLISION_MAXIMUM_STREAM_BYTES + MIXED_COLLISION_FRAGMENT_BYTES - 1) / MIXED_COLLISION_FRAGMENT_BYTES)
/* a surface's edge ring's most edges taken */
#define MIXED_COLLISION_MAXIMUM_RING 64
/* a digest's bytes (BLAKE2b-256) */
#define MIXED_COLLISION_DIGEST_BYTES 32

/* the game's layout (32-bit pointers; mixed_coop.c checks these against the game's structures) */
#define MIXED_COLLISION_ROOT_BYTES 0x298
#define MIXED_COLLISION_MATERIAL_BYTES 0x48
#define MIXED_COLLISION_REGION_BYTES 0x54
#define MIXED_COLLISION_PERMUTATION_BYTES 0x20
#define MIXED_COLLISION_SPHERE_BYTES 0x20
#define MIXED_COLLISION_NODE_BYTES 0x40
#define MIXED_COLLISION_BSP_BYTES 0x60
#define MIXED_COLLISION_BSP3D_NODE_BYTES 0xC
#define MIXED_COLLISION_PLANE_BYTES 0x10
#define MIXED_COLLISION_LEAF_BYTES 0x8
#define MIXED_COLLISION_BSP2D_REFERENCE_BYTES 0x8
#define MIXED_COLLISION_BSP2D_NODE_BYTES 0x14
#define MIXED_COLLISION_SURFACE_BYTES 0xC
#define MIXED_COLLISION_EDGE_BYTES 0x18
#define MIXED_COLLISION_VERTEX_BYTES 0x10
/* the root's blocks (struct collision_model: resistance.materials, .regions, .modifiers, pathfinding_spheres,
nodes) */
#define MIXED_COLLISION_ROOT_MATERIALS 0x234
#define MIXED_COLLISION_ROOT_REGIONS 0x240
#define MIXED_COLLISION_ROOT_MODIFIERS 0x24C
#define MIXED_COLLISION_ROOT_SPHERES 0x280
#define MIXED_COLLISION_ROOT_NODES 0x28C
/* a region's permutations, a node's bsps */
#define MIXED_COLLISION_REGION_PERMUTATIONS 0x48
#define MIXED_COLLISION_NODE_BSPS 0x34
/* a bsp's blocks (struct collision_bsp) */
#define MIXED_COLLISION_BSP_BSP3D_NODES 0x00
#define MIXED_COLLISION_BSP_PLANES 0x0C
#define MIXED_COLLISION_BSP_LEAVES 0x18
#define MIXED_COLLISION_BSP_BSP2D_REFERENCES 0x24
#define MIXED_COLLISION_BSP_BSP2D_NODES 0x30
#define MIXED_COLLISION_BSP_SURFACES 0x3C
#define MIXED_COLLISION_BSP_EDGES 0x48
#define MIXED_COLLISION_BSP_VERTICES 0x54

/* the game's limits (tag_schema_collision.c's) */
#define MIXED_COLLISION_MAXIMUM_MATERIALS 32
#define MIXED_COLLISION_MAXIMUM_REGIONS 8
#define MIXED_COLLISION_MAXIMUM_PERMUTATIONS 32
#define MIXED_COLLISION_MAXIMUM_SPHERES 32
#define MIXED_COLLISION_MAXIMUM_NODES 64
#define MIXED_COLLISION_MAXIMUM_BSPS 32
#define MIXED_COLLISION_MAXIMUM_MATERIAL_TYPES 40

/* ---------- the stream */

struct mixed_collision_stream
{
	uint8_t level;
	uint8_t model_count;
	struct
	{
		uint8_t model;
		uint32_t bytes;
		uint8_t const *data;
	} models[NUMBER_OF_MIXED_COLLISION_MODELS];
};

/* reads a stream (its envelope: magic, version, sizes, each model once, the check): 1 and views into bytes, or 0
and why */
int mixed_collision_stream_read(uint8_t const *bytes, uint32_t length, struct mixed_collision_stream *stream,
	char *why, int why_size);

/* the stream's reassembly, one session's, each piece once (the joiner's pieces on the host) */
struct mixed_collision_reassembly
{
	uint32_t session;
	uint32_t total_bytes;
	uint16_t count;
	uint16_t received;
	uint8_t have[MIXED_COLLISION_MAXIMUM_FRAGMENTS];
	uint8_t bytes[MIXED_COLLISION_MAXIMUM_STREAM_BYTES];
};

struct mixed_coop_fragment;

void mixed_collision_reassembly_begin(struct mixed_collision_reassembly *reassembly, uint32_t session);
/* _mixed_coop_fragment_refused, _taken or _complete (mixed_coop_protocol.h) */
int mixed_collision_reassembly_take(struct mixed_collision_reassembly *reassembly,
	struct mixed_coop_fragment const *fragment, uint32_t fragment_bytes);

/* ---------- a model */

/* writes the collision model at root (the game's, in memory: its blocks' addresses this machine's pointers) as
the stream's model bytes into out (at most size): its length, or 0 and why (a model too large, a reference to a
tag, a count past the game's limit) */
uint32_t mixed_collision_model_write(void const *root, uint8_t *out, uint32_t size, char *why, int why_size);

/* a model's bytes checked and laid out as the game's collision model tag: a region (malloc'd: the caller frees it)
of `reserve` bytes first (the caller's: a tag header), then the root at *root_offset and its blocks after it, every
address in it the region's own (it must not move), each reference's name the region's empty string. 1, or 0 and
why (nothing allocated) */
int mixed_collision_model_rebuild(uint8_t const *bytes, uint32_t length, uint32_t reserve, uint8_t **region,
	uint32_t *region_bytes, uint32_t *root_offset, char *why, int why_size);

/* the stream's writer: a model's bytes appended */
struct mixed_collision_writer
{
	uint8_t *bytes;
	uint32_t size;
	uint32_t length;
	uint8_t model_count;
	int overflow;
};

void mixed_collision_writer_begin(struct mixed_collision_writer *writer, void *bytes, uint32_t size, uint8_t level);
void mixed_collision_writer_model(struct mixed_collision_writer *writer, uint8_t model, uint8_t const *data,
	uint32_t length);
/* the check appended: the stream's length, 0 if it overflowed or has no model */
uint32_t mixed_collision_writer_end(struct mixed_collision_writer *writer);

#endif
