/*
NET_FUZZ_MIXED_COLLISION.C

A fuzz target for mixed play's flag stand and teleporter base collision (port/linux/game/
mixed_collision_protocol.c): what an Xbox-files machine sends an MCC-files host, as the host reads it.

An input's first byte chooses what the rest is:
  0  a model's bytes: rebuilt (mixed_collision_model_rebuild); one taken is walked again here - every block in
     the region, every index in its blocks - and written back (mixed_collision_model_write), which must give the
     input's bytes exactly
  1  a stream: read (mixed_collision_stream_read); each model in it as 0
  2  pieces of a stream: each a 16-byte head (struct mixed_coop_fragment's) and as many bytes of data as the
     input has left (at most MIXED_COLLISION_FRAGMENT_BYTES), taken in order into one reassembly; a whole one read
     as 1
  3  lobby messages (16 bytes each): mixed_coop_lobby_valid's take of the collision ask and offer
Whatever the input: nothing read or written out of bounds (AddressSanitizer, UBSan), and the checks above.
net_fuzz_checks: a small valid model is taken and written back the same; each damage of it - a count past the
game's limit, a block past the bytes, bytes left over, an index past its block, a child before its parent, a
surface's edge ring left open, a reference to a tag, a float not a number, a name not ended, an address - is
refused; a stream with a model twice, of no model, of a bad check, is refused; a piece twice is refused.
Built as the platform layer's tests are, with AddressSanitizer and UBSan, by run_net_fuzz_test.sh.
*/

#include "../../linux/game/mixed_collision_protocol.h"
#include "../../linux/game/mixed_coop_protocol.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESERVE 0xA4

static uint32_t u32(uint8_t const *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

static void put_u16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

/* a block of the region: its elements all in it */
static void check_block(uint8_t const *region, uint32_t size, uint8_t const *element, uint32_t offset, uint32_t element_bytes)
{
	uint32_t count = u32(element + offset);
	uintptr_t address = (uintptr_t)u32(element + offset + 4);

	if (!count)
		return;
	if (address < (uintptr_t)region || address > (uintptr_t)region + size ||
		(uint64_t)count * element_bytes > (uint64_t)((uintptr_t)region + size - address))
	{
		abort();
	}
}

static void check_bsp(uint8_t const *region, uint32_t size, uint8_t const *bsp)
{
	check_block(region, size, bsp, MIXED_COLLISION_BSP_BSP3D_NODES, MIXED_COLLISION_BSP3D_NODE_BYTES);
	check_block(region, size, bsp, MIXED_COLLISION_BSP_PLANES, MIXED_COLLISION_PLANE_BYTES);
	check_block(region, size, bsp, MIXED_COLLISION_BSP_LEAVES, MIXED_COLLISION_LEAF_BYTES);
	check_block(region, size, bsp, MIXED_COLLISION_BSP_BSP2D_REFERENCES, MIXED_COLLISION_BSP2D_REFERENCE_BYTES);
	check_block(region, size, bsp, MIXED_COLLISION_BSP_BSP2D_NODES, MIXED_COLLISION_BSP2D_NODE_BYTES);
	check_block(region, size, bsp, MIXED_COLLISION_BSP_SURFACES, MIXED_COLLISION_SURFACE_BYTES);
	check_block(region, size, bsp, MIXED_COLLISION_BSP_EDGES, MIXED_COLLISION_EDGE_BYTES);
	check_block(region, size, bsp, MIXED_COLLISION_BSP_VERTICES, MIXED_COLLISION_VERTEX_BYTES);
}

/* a rebuilt model walked again, then written back: the input's bytes */
static int model(uint8_t const *data, uint32_t size)
{
	static uint8_t written[MIXED_COLLISION_MAXIMUM_MODEL_BYTES];
	uint8_t *region;
	uint32_t region_bytes, root_offset, index, bsp, length;
	uint8_t const *root;
	char why[160];

	if (!mixed_collision_model_rebuild(data, size, RESERVE, &region, &region_bytes, &root_offset, why, sizeof(why)))
	{
		if (region || !why[0])
			abort();
		return 0;
	}
	if (!region || root_offset < RESERVE || root_offset + MIXED_COLLISION_ROOT_BYTES > region_bytes)
		abort();
	root = region + root_offset;
	check_block(region, region_bytes, root, MIXED_COLLISION_ROOT_MATERIALS, MIXED_COLLISION_MATERIAL_BYTES);
	check_block(region, region_bytes, root, MIXED_COLLISION_ROOT_REGIONS, MIXED_COLLISION_REGION_BYTES);
	check_block(region, region_bytes, root, MIXED_COLLISION_ROOT_SPHERES, MIXED_COLLISION_SPHERE_BYTES);
	check_block(region, region_bytes, root, MIXED_COLLISION_ROOT_NODES, MIXED_COLLISION_NODE_BYTES);
	if (u32(root + MIXED_COLLISION_ROOT_MODIFIERS))
		abort();
	for (index = 0; index < u32(root + MIXED_COLLISION_ROOT_REGIONS); index++)
		check_block(region, region_bytes, (uint8_t const *)(uintptr_t)u32(root + MIXED_COLLISION_ROOT_REGIONS + 4) +
			index * MIXED_COLLISION_REGION_BYTES, MIXED_COLLISION_REGION_PERMUTATIONS, MIXED_COLLISION_PERMUTATION_BYTES);
	for (index = 0; index < u32(root + MIXED_COLLISION_ROOT_NODES); index++)
	{
		uint8_t const *node = (uint8_t const *)(uintptr_t)u32(root + MIXED_COLLISION_ROOT_NODES + 4) +
			index * MIXED_COLLISION_NODE_BYTES;

		check_block(region, region_bytes, node, MIXED_COLLISION_NODE_BSPS, MIXED_COLLISION_BSP_BYTES);
		for (bsp = 0; bsp < u32(node + MIXED_COLLISION_NODE_BSPS); bsp++)
			check_bsp(region, region_bytes, (uint8_t const *)(uintptr_t)u32(node + MIXED_COLLISION_NODE_BSPS + 4) +
				bsp * MIXED_COLLISION_BSP_BYTES);
	}
	length = mixed_collision_model_write(root, written, sizeof(written), why, sizeof(why));
	if (length != size || memcmp(written, data, size))
		abort();
	free(region);
	return 1;
}

static void stream(uint8_t const *data, uint32_t size)
{
	struct mixed_collision_stream read;
	char why[160];
	uint8_t index;

	if (!mixed_collision_stream_read(data, size, &read, why, sizeof(why)))
		return;
	if (!read.model_count || read.model_count > NUMBER_OF_MIXED_COLLISION_MODELS)
		abort();
	for (index = 0; index < read.model_count; index++)
	{
		if (read.models[index].data < data || read.models[index].data + read.models[index].bytes > data + size ||
			read.models[index].model >= NUMBER_OF_MIXED_COLLISION_MODELS)
		{
			abort();
		}
		model(read.models[index].data, read.models[index].bytes);
	}
}

static void pieces(uint8_t const *data, size_t size)
{
	static struct mixed_collision_reassembly reassembly;
	static struct mixed_coop_fragment fragment;
	size_t at = 4;
	uint32_t session;
	int result = _mixed_coop_fragment_refused;

	if (size < 4)
		return;
	session = u32(data);
	mixed_collision_reassembly_begin(&reassembly, session);
	while (at + 16 <= size)
	{
		size_t piece = size - at < 16 + MIXED_COLLISION_FRAGMENT_BYTES ? size - at : 16 + MIXED_COLLISION_FRAGMENT_BYTES;
		uint16_t received = reassembly.received;

		memset(&fragment, 0, sizeof(fragment));
		memcpy(&fragment, data + at, piece);
		result = mixed_collision_reassembly_take(&reassembly, &fragment, (uint32_t)piece);
		if (reassembly.total_bytes > MIXED_COLLISION_MAXIMUM_STREAM_BYTES || reassembly.received > reassembly.count ||
			reassembly.count > MIXED_COLLISION_MAXIMUM_FRAGMENTS ||
			(result == _mixed_coop_fragment_refused && reassembly.received != received))
		{
			abort();
		}
		at += piece;
		if (result == _mixed_coop_fragment_complete)
		{
			if (reassembly.received != reassembly.count)
				abort();
			stream(reassembly.bytes, reassembly.total_bytes);
			break;
		}
	}
}

static void lobby(uint8_t const *data, size_t size)
{
	size_t at;

	for (at = 0; at + sizeof(struct mixed_coop_lobby_message) <= size; at += sizeof(struct mixed_coop_lobby_message))
	{
		struct mixed_coop_lobby_message message;

		memcpy(&message, data + at, sizeof(message));
		if (!mixed_coop_lobby_valid(&message))
			continue;
		if (message.command == _mixed_coop_lobby_collision_ask || message.command == _mixed_coop_lobby_collision_offer)
		{
			if ((message.flags & ~MIXED_COLLISION_ALL_BITS) || message.level < MIXED_COOP_CAMPAIGN_LEVELS ||
				message.level >= MIXED_COOP_LEVELS || (message.command == _mixed_coop_lobby_collision_ask && !message.flags))
			{
				abort();
			}
		}
		else if (message.flags)
			abort();
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < 1 || size > (1 << 20))
		return 0;
	switch (data[0] & 3)
	{
	case 0: model(data + 1, (uint32_t)(size - 1)); break;
	case 1: stream(data + 1, (uint32_t)(size - 1)); break;
	case 2: pieces(data + 1, size - 1); break;
	case 3: lobby(data + 1, size - 1); break;
	}
	return 0;
}

/* ---------- the checks */

/* a small valid model: a material, a region of one permutation, a node of one bsp - a triangle: three vertices and
edges, one surface, one plane, a bsp3d node of one leaf, its bsp2d reference to the surface */
static uint32_t valid_model(uint8_t *out)
{
	uint8_t *at = out;
	uint8_t *root, *region, *node, *bsp, *element;
	int index;

	memset(out, 0, 4096);
	root = at; at += MIXED_COLLISION_ROOT_BYTES;
	put_u16(root + 4, 0xFFFF);
	{
		static uint32_t const references[] = { 0x70, 0x84, 0x98, 0xA8, 0xBC, 0x188, 0x198, 0x1A8 };

		for (index = 0; index < 8; index++)
		{
			put_u32(root + references[index], 0x65666665u);
			put_u32(root + references[index] + 12, 0xFFFFFFFFu);
		}
	}
	put_u32(root + MIXED_COLLISION_ROOT_MATERIALS, 1);
	put_u32(root + MIXED_COLLISION_ROOT_REGIONS, 1);
	put_u32(root + MIXED_COLLISION_ROOT_NODES, 1);
	/* the material */
	memcpy(at, "default", 7); at += MIXED_COLLISION_MATERIAL_BYTES;
	/* the region, its permutation */
	region = at; at += MIXED_COLLISION_REGION_BYTES;
	memcpy(region, "base", 4);
	put_u32(region + 0x38, 0x65666665u);
	put_u32(region + 0x38 + 12, 0xFFFFFFFFu);
	put_u32(region + MIXED_COLLISION_REGION_PERMUTATIONS, 1);
	memcpy(at, "__base", 6); at += MIXED_COLLISION_PERMUTATION_BYTES;
	/* the node */
	node = at; at += MIXED_COLLISION_NODE_BYTES;
	memcpy(node, "frame", 5);
	put_u16(node + 0x20, 0);
	put_u16(node + 0x22, 0xFFFF);
	put_u16(node + 0x24, 0xFFFF);
	put_u16(node + 0x26, 0xFFFF);
	put_u32(node + MIXED_COLLISION_NODE_BSPS, 1);
	/* its bsp */
	bsp = at; at += MIXED_COLLISION_BSP_BYTES;
	put_u32(bsp + MIXED_COLLISION_BSP_BSP3D_NODES, 1);
	put_u32(bsp + MIXED_COLLISION_BSP_PLANES, 1);
	put_u32(bsp + MIXED_COLLISION_BSP_LEAVES, 1);
	put_u32(bsp + MIXED_COLLISION_BSP_BSP2D_REFERENCES, 1);
	put_u32(bsp + MIXED_COLLISION_BSP_SURFACES, 1);
	put_u32(bsp + MIXED_COLLISION_BSP_EDGES, 3);
	put_u32(bsp + MIXED_COLLISION_BSP_VERTICES, 3);
	/* bsp3d node: plane 0, front the leaf, back none */
	element = at; at += MIXED_COLLISION_BSP3D_NODE_BYTES;
	put_u32(element + 4, 0x80000000u);
	put_u32(element + 8, 0xFFFFFFFFu);
	/* plane z = 0 */
	element = at; at += MIXED_COLLISION_PLANE_BYTES;
	put_u32(element + 8, 0x3F800000u);
	/* leaf: one reference */
	element = at; at += MIXED_COLLISION_LEAF_BYTES;
	put_u16(element + 2, 1);
	/* bsp2d reference: plane 0, the surface */
	element = at; at += MIXED_COLLISION_BSP2D_REFERENCE_BYTES;
	put_u32(element + 4, 0x80000000u);
	/* surface: plane 0, edge 0, material 0 */
	element = at; at += MIXED_COLLISION_SURFACE_BYTES;
	/* edges: a ring 0 -> 1 -> 2 -> 0 on the surface's left */
	for (index = 0; index < 3; index++)
	{
		element = at; at += MIXED_COLLISION_EDGE_BYTES;
		put_u32(element, (uint32_t)index);
		put_u32(element + 4, (uint32_t)((index + 1) % 3));
		put_u32(element + 8, (uint32_t)((index + 1) % 3));
		put_u32(element + 12, (uint32_t)((index + 2) % 3));
		put_u32(element + 16, 0);
		put_u32(element + 20, 0xFFFFFFFFu);
	}
	/* vertices */
	for (index = 0; index < 3; index++)
	{
		element = at; at += MIXED_COLLISION_VERTEX_BYTES;
		put_u32(element, index == 1 ? 0x3F800000u : 0);
		put_u32(element + 4, index == 2 ? 0x3F800000u : 0);
		put_u32(element + 12, (uint32_t)index);
	}
	return (uint32_t)(at - out);
}

static int failures;

static void expect(int ok, char const *what)
{
	if (!ok)
	{
		fprintf(stderr, "FAIL mixed collision: %s\n", what);
		failures++;
	}
}

/* the valid model damaged at offset (a u32 put there): refused */
static void expect_refused(uint8_t const *good, uint32_t length, uint32_t offset, uint32_t value, char const *what)
{
	static uint8_t bad[4096];
	uint8_t *region;
	uint32_t region_bytes, root_offset;
	char why[160];

	memcpy(bad, good, length);
	put_u32(bad + offset, value);
	why[0] = 0;
	if (mixed_collision_model_rebuild(bad, length, RESERVE, &region, &region_bytes, &root_offset, why, sizeof(why)))
	{
		free(region);
		expect(0, what);
	}
	if (getenv("NET_FUZZ_LOG"))
		fprintf(stderr, "  [refused] %s: %s\n", what, why);
}

int net_fuzz_checks(void)
{
	static uint8_t good[4096], stream_bytes[16384];
	uint32_t length = valid_model(good);
	/* (where the valid model's parts are: valid_model's order) */
	uint32_t material = MIXED_COLLISION_ROOT_BYTES, region = material + MIXED_COLLISION_MATERIAL_BYTES;
	uint32_t node = region + MIXED_COLLISION_REGION_BYTES + MIXED_COLLISION_PERMUTATION_BYTES;
	uint32_t bsp = node + MIXED_COLLISION_NODE_BYTES, bsp3d = bsp + MIXED_COLLISION_BSP_BYTES;
	uint32_t plane = bsp3d + MIXED_COLLISION_BSP3D_NODE_BYTES, leaf = plane + MIXED_COLLISION_PLANE_BYTES;
	uint32_t reference = leaf + MIXED_COLLISION_LEAF_BYTES, surface = reference + MIXED_COLLISION_BSP2D_REFERENCE_BYTES;
	uint32_t edge = surface + MIXED_COLLISION_SURFACE_BYTES, vertex = edge + 3 * MIXED_COLLISION_EDGE_BYTES;
	struct mixed_collision_writer writer;
	struct mixed_collision_stream read;
	uint32_t stream_length;
	char why[160];

	expect(model(good, length), "the valid model was not taken back the same");
	expect_refused(good, length, MIXED_COLLISION_ROOT_NODES, 65, "65 nodes");
	expect_refused(good, length, MIXED_COLLISION_ROOT_NODES, 2, "a block past the bytes");
	expect_refused(good, length, MIXED_COLLISION_ROOT_MODIFIERS, 1, "a modifier");
	expect_refused(good, length, MIXED_COLLISION_ROOT_NODES + 4, 0x1000, "an address");
	expect_refused(good, length, 0x70 + 12, 5, "a reference to a tag");
	expect_refused(good, length, 0x08, 0x7FC00000u, "a vitality not a number");
	{
		static uint8_t named[4096];
		uint8_t *region_out;
		uint32_t region_bytes, root_offset;

		memcpy(named, good, length);
		memset(named + material, 'A', 32);
		expect(!mixed_collision_model_rebuild(named, length, RESERVE, &region_out, &region_bytes, &root_offset, why,
			sizeof(why)), "a name not ended");
	}
	expect_refused(good, length, node + 0x20, 0xFFFF0003u, "a node of region 3 of 1");
	expect_refused(good, length, bsp3d, 1, "a bsp3d node of plane 1 of 1");
	expect_refused(good, length, bsp3d + 4, 0, "a bsp3d node its own child");
	expect_refused(good, length, bsp3d + 4, 0x80000001u, "a bsp3d node of leaf 1 of 1");
	expect_refused(good, length, leaf, 0x00020000u, "a leaf of two references of one");
	expect_refused(good, length, reference + 4, 0x80000001u, "a reference to surface 1 of 1");
	expect_refused(good, length, surface + 4, 3, "a surface of edge 3 of 3");
	expect_refused(good, length, surface + 8, 0x00050000u, "a surface of material 5 of 1");
	expect_refused(good, length, edge + MIXED_COLLISION_EDGE_BYTES + 8, 1, "an edge ring left open");
	expect_refused(good, length, edge + 16, 1, "an edge ring leaving its surface");
	expect_refused(good, length, edge, 3, "an edge of vertex 3 of 3");
	expect_refused(good, length, vertex + 12, 7, "a vertex of edge 7 of 3");
	expect_refused(good, length, vertex, 0x7F800000u, "a vertex at infinity");
	expect_refused(good, length, plane + 12, 0xFFC00000u, "a plane not a number");
	{
		uint8_t *region_out;
		uint32_t region_bytes, root_offset;

		expect(!mixed_collision_model_rebuild(good, length - 1, RESERVE, &region_out, &region_bytes, &root_offset, why,
			sizeof(why)), "a model a byte short");
		memcpy(stream_bytes, good, length);
		stream_bytes[length] = 0;
		expect(!mixed_collision_model_rebuild(stream_bytes, length + 4, RESERVE, &region_out, &region_bytes, &root_offset,
			why, sizeof(why)), "a model with bytes left over");
	}
	/* the stream */
	mixed_collision_writer_begin(&writer, stream_bytes, sizeof(stream_bytes), 12);
	mixed_collision_writer_model(&writer, MIXED_COLLISION_MODEL_FLAG_STAND, good, length);
	stream_length = mixed_collision_writer_end(&writer);
	expect(stream_length && mixed_collision_stream_read(stream_bytes, stream_length, &read, why, sizeof(why)) &&
		read.model_count == 1 && read.models[0].bytes == length, "a stream of one model");
	if (getenv("NET_FUZZ_SEEDS"))
	{
		extern void net_fuzz_write_seed(const char *folder, const char *name, const void *data, unsigned long size);
		static uint8_t seed[16384];

		seed[0] = 0;
		memcpy(seed + 1, good, length);
		net_fuzz_write_seed(getenv("NET_FUZZ_SEEDS"), "model", seed, length + 1);
		seed[0] = 1;
		memcpy(seed + 1, stream_bytes, stream_length);
		net_fuzz_write_seed(getenv("NET_FUZZ_SEEDS"), "stream", seed, stream_length + 1);
		/* (the stream in one piece) */
		seed[0] = 2;
		put_u32(seed + 1, 0x1234u);
		memset(seed + 5, 0, 16);
		put_u32(seed + 5, 0x1234u);
		put_u32(seed + 9, stream_length);
		put_u16(seed + 13, 0);
		put_u16(seed + 15, 1);
		put_u16(seed + 17, (uint16_t)stream_length);
		memcpy(seed + 21, stream_bytes, stream_length);
		net_fuzz_write_seed(getenv("NET_FUZZ_SEEDS"), "pieces", seed, 21 + stream_length);
	}
	stream_bytes[stream_length - 1] ^= 1;
	expect(!mixed_collision_stream_read(stream_bytes, stream_length, &read, why, sizeof(why)), "a stream of a bad check");
	mixed_collision_writer_begin(&writer, stream_bytes, sizeof(stream_bytes), 12);
	mixed_collision_writer_model(&writer, MIXED_COLLISION_MODEL_FLAG_STAND, good, length);
	mixed_collision_writer_model(&writer, MIXED_COLLISION_MODEL_FLAG_STAND, good, length);
	stream_length = mixed_collision_writer_end(&writer);
	expect(stream_length && !mixed_collision_stream_read(stream_bytes, stream_length, &read, why, sizeof(why)),
		"a stream of one model twice");
	mixed_collision_writer_begin(&writer, stream_bytes, sizeof(stream_bytes), 12);
	expect(!mixed_collision_writer_end(&writer), "a stream of no model");
	mixed_collision_writer_begin(&writer, stream_bytes, sizeof(stream_bytes), 3);
	mixed_collision_writer_model(&writer, MIXED_COLLISION_MODEL_FLAG_STAND, good, length);
	stream_length = mixed_collision_writer_end(&writer);
	expect(!mixed_collision_stream_read(stream_bytes, stream_length, &read, why, sizeof(why)), "a stream of a campaign level");
	/* pieces */
	{
		static struct mixed_collision_reassembly reassembly;
		static struct mixed_coop_fragment fragment;

		mixed_collision_writer_begin(&writer, stream_bytes, sizeof(stream_bytes), 12);
		mixed_collision_writer_model(&writer, MIXED_COLLISION_MODEL_FLAG_STAND, good, length);
		stream_length = mixed_collision_writer_end(&writer);
		mixed_collision_reassembly_begin(&reassembly, 7);
		memset(&fragment, 0, sizeof(fragment));
		fragment.session = 7;
		fragment.total_bytes = stream_length;
		fragment.count = 1;
		fragment.bytes = (uint16_t)stream_length;
		memcpy(fragment.data, stream_bytes, stream_length);
		expect(mixed_collision_reassembly_take(&reassembly, &fragment, sizeof(fragment)) == _mixed_coop_fragment_complete,
			"a stream in one piece");
		expect(mixed_collision_reassembly_take(&reassembly, &fragment, sizeof(fragment)) == _mixed_coop_fragment_refused,
			"a piece twice");
		mixed_collision_reassembly_begin(&reassembly, 8);
		expect(mixed_collision_reassembly_take(&reassembly, &fragment, sizeof(fragment)) == _mixed_coop_fragment_refused,
			"a piece of another session");
		mixed_collision_reassembly_begin(&reassembly, 7);
		expect(mixed_collision_reassembly_take(&reassembly, &fragment, 20) == _mixed_coop_fragment_refused,
			"a piece shorter than it says");
	}
	/* the lobby */
	{
		struct mixed_coop_lobby_message message;

		memset(&message, 0, sizeof(message));
		message.command = _mixed_coop_lobby_collision_ask;
		message.version = MIXED_COOP_VERSION;
		message.family = _mixed_coop_family_mcc;
		message.level = 12;
		message.flags = 3;
		message.session = 1;
		expect(mixed_coop_lobby_valid(&message), "an MCC host's ask");
		message.flags = 4;
		expect(!mixed_coop_lobby_valid(&message), "an ask of a model past the two");
		message.flags = 0;
		expect(!mixed_coop_lobby_valid(&message), "an ask of nothing");
		message.flags = 1;
		message.family = _mixed_coop_family_xbox;
		expect(!mixed_coop_lobby_valid(&message), "an ask by an Xbox host");
		message.command = _mixed_coop_lobby_collision_offer;
		message.flags = 0;
		expect(mixed_coop_lobby_valid(&message), "an Xbox machine's offer of nothing");
		message.level = 3;
		message.version = MIXED_COOP_VERSION_CAMPAIGN;
		expect(!mixed_coop_lobby_valid(&message), "an offer on a campaign level");
		message.command = _mixed_coop_lobby_hello;
		message.level = 12;
		message.version = MIXED_COOP_VERSION;
		message.flags = 1;
		expect(!mixed_coop_lobby_valid(&message), "a hello with flags");
	}
	return failures;
}
