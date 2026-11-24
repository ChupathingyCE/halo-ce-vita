/*
MIXED_COLLISION_PROTOCOL.C

Mixed play's flag stand and teleporter base collision (mixed_collision_protocol.h): the stream, its pieces, and a
model's bytes written from the game's memory and read back, checked, into a tag the game can use. Plain C.
*/

#include "mixed_collision_protocol.h"
#include "mixed_coop_protocol.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MC_NONE 0xFFFFFFFFu

/* (the game's pointers are 32 bits: the Vita's, the 32-bit PC builds') */
typedef char mc_pointer_size_assert[sizeof(void *) == 4 ? 1 : -1];

/* ---------- the models */

char const *mixed_collision_model_path(int model)
{
	switch (model)
	{
	case MIXED_COLLISION_MODEL_FLAG_STAND: return "scenery\\flag_base\\flag_base";
	case MIXED_COLLISION_MODEL_TELEPORTER_BASE: return "scenery\\teleporter_base\\teleporter_base";
	default: return NULL;
	}
}

/* ---------- little-endian reads and writes */

static uint32_t mc_u32(uint8_t const *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint16_t mc_u16(uint8_t const *bytes)
{
	return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

static void mc_put_u32(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

static int mc_finite(uint8_t const *bytes)
{
	/* (an exponent of all ones: an infinity or not a number) */
	return (mc_u32(bytes) & 0x7F800000u) != 0x7F800000u;
}

static void mc_why(char *why, int why_size, char const *format, long a, long b)
{
	if (why && why_size > 0)
		snprintf(why, (size_t)why_size, format, a, b);
}

/* ---------- the layout: each element's size, its blocks, references, strings and floats */

enum
{
	MC_ROOT,
	MC_MATERIAL,
	MC_REGION,
	MC_PERMUTATION,
	MC_MODIFIER,
	MC_SPHERE,
	MC_NODE,
	MC_BSP,
	MC_BSP3D_NODE,
	MC_PLANE,
	MC_LEAF,
	MC_BSP2D_REFERENCE,
	MC_BSP2D_NODE,
	MC_SURFACE,
	MC_EDGE,
	MC_VERTEX,
	NUMBER_OF_MC_KINDS
};

struct mc_block
{
	uint32_t offset;
	uint8_t kind;
	uint32_t maximum;
};

struct mc_layout
{
	uint32_t size;
	uint8_t block_count;
	struct mc_block blocks[8];
	uint8_t reference_count;
	uint32_t references[8];
	uint8_t string_count;
	uint32_t strings[1];
	/* the float fields: runs of { offset, count } */
	uint8_t float_run_count;
	uint32_t float_runs[12][2];
};

static struct mc_layout const mc_layouts[NUMBER_OF_MC_KINDS] =
{
	/* (struct collision_model: damage_resistance's eight effects, its floats, the pathfinding box) */
	[MC_ROOT] = { MIXED_COLLISION_ROOT_BYTES, 5,
		{ { MIXED_COLLISION_ROOT_MATERIALS, MC_MATERIAL, MIXED_COLLISION_MAXIMUM_MATERIALS },
		  { MIXED_COLLISION_ROOT_REGIONS, MC_REGION, MIXED_COLLISION_MAXIMUM_REGIONS },
		  { MIXED_COLLISION_ROOT_MODIFIERS, MC_MODIFIER, 0 },
		  { MIXED_COLLISION_ROOT_SPHERES, MC_SPHERE, MIXED_COLLISION_MAXIMUM_SPHERES },
		  { MIXED_COLLISION_ROOT_NODES, MC_NODE, MIXED_COLLISION_MAXIMUM_NODES } },
		8, { 0x70, 0x84, 0x98, 0xA8, 0xBC, 0x188, 0x198, 0x1A8 },
		0, { 0 },
		11, { { 0x08, 2 }, { 0x44, 1 }, { 0x80, 1 }, { 0x94, 1 }, { 0xB8, 1 }, { 0xCC, 1 }, { 0xF0, 2 }, { 0x108, 3 },
			{ 0x184, 1 }, { 0x1C0, 1 }, { 0x268, 6 } } },
	[MC_MATERIAL] = { MIXED_COLLISION_MATERIAL_BYTES, 0, { { 0 } }, 0, { 0 }, 1, { 0 }, 2, { { 0x28, 2 }, { 0x3C, 1 } } },
	[MC_REGION] = { MIXED_COLLISION_REGION_BYTES, 1,
		{ { MIXED_COLLISION_REGION_PERMUTATIONS, MC_PERMUTATION, MIXED_COLLISION_MAXIMUM_PERMUTATIONS } },
		1, { 0x38 }, 1, { 0 }, 1, { { 0x28, 1 } } },
	[MC_PERMUTATION] = { MIXED_COLLISION_PERMUTATION_BYTES, 0, { { 0 } }, 0, { 0 }, 1, { 0 }, 0, { { 0 } } },
	/* (none taken: the block is the game's unused one) */
	[MC_MODIFIER] = { 4, 0, { { 0 } }, 0, { 0 }, 0, { 0 }, 0, { { 0 } } },
	[MC_SPHERE] = { MIXED_COLLISION_SPHERE_BYTES, 0, { { 0 } }, 0, { 0 }, 0, { 0 }, 1, { { 0x10, 4 } } },
	[MC_NODE] = { MIXED_COLLISION_NODE_BYTES, 1, { { MIXED_COLLISION_NODE_BSPS, MC_BSP, MIXED_COLLISION_MAXIMUM_BSPS } },
		0, { 0 }, 1, { 0 }, 0, { { 0 } } },
	[MC_BSP] = { MIXED_COLLISION_BSP_BYTES, 8,
		{ { MIXED_COLLISION_BSP_BSP3D_NODES, MC_BSP3D_NODE, 0x20000 },
		  { MIXED_COLLISION_BSP_PLANES, MC_PLANE, 0x10000 },
		  { MIXED_COLLISION_BSP_LEAVES, MC_LEAF, 0x10000 },
		  { MIXED_COLLISION_BSP_BSP2D_REFERENCES, MC_BSP2D_REFERENCE, 131072 },
		  { MIXED_COLLISION_BSP_BSP2D_NODES, MC_BSP2D_NODE, 0xFFFF },
		  { MIXED_COLLISION_BSP_SURFACES, MC_SURFACE, 131072 },
		  { MIXED_COLLISION_BSP_EDGES, MC_EDGE, 262144 },
		  { MIXED_COLLISION_BSP_VERTICES, MC_VERTEX, 131072 } },
		0, { 0 }, 0, { 0 }, 0, { { 0 } } },
	[MC_BSP3D_NODE] = { MIXED_COLLISION_BSP3D_NODE_BYTES, 0, { { 0 } }, 0, { 0 }, 0, { 0 }, 0, { { 0 } } },
	[MC_PLANE] = { MIXED_COLLISION_PLANE_BYTES, 0, { { 0 } }, 0, { 0 }, 0, { 0 }, 1, { { 0, 4 } } },
	[MC_LEAF] = { MIXED_COLLISION_LEAF_BYTES, 0, { { 0 } }, 0, { 0 }, 0, { 0 }, 0, { { 0 } } },
	[MC_BSP2D_REFERENCE] = { MIXED_COLLISION_BSP2D_REFERENCE_BYTES, 0, { { 0 } }, 0, { 0 }, 0, { 0 }, 0, { { 0 } } },
	[MC_BSP2D_NODE] = { MIXED_COLLISION_BSP2D_NODE_BYTES, 0, { { 0 } }, 0, { 0 }, 0, { 0 }, 1, { { 0, 3 } } },
	[MC_SURFACE] = { MIXED_COLLISION_SURFACE_BYTES, 0, { { 0 } }, 0, { 0 }, 0, { 0 }, 0, { { 0 } } },
	[MC_EDGE] = { MIXED_COLLISION_EDGE_BYTES, 0, { { 0 } }, 0, { 0 }, 0, { 0 }, 0, { { 0 } } },
	[MC_VERTEX] = { MIXED_COLLISION_VERTEX_BYTES, 0, { { 0 } }, 0, { 0 }, 0, { 0 }, 1, { { 0, 3 } } },
};

/* the most elements a model may hold in all (its bytes bound them too) */
#define MC_MAXIMUM_ELEMENTS 16384
/* a tag string's bytes (TAG_STRING_LENGTH + 1) */
#define MC_STRING_BYTES 32

/* ---------- writing (the joiner: its own map's model, in memory) */

struct mc_write
{
	uint8_t *out;
	uint32_t size;
	uint32_t length;
	long elements;
	char *why;
	int why_size;
	int failed;
};

static void mc_write_elements(struct mc_write *write, int kind, uint8_t const *elements, uint32_t count);

static void mc_write_element_blocks(struct mc_write *write, int kind, uint8_t const *element)
{
	struct mc_layout const *layout = &mc_layouts[kind];
	uint8_t index;

	for (index = 0; index < layout->block_count && !write->failed; index++)
	{
		struct mc_block const *block = &layout->blocks[index];
		uint32_t count = mc_u32(element + block->offset);
		uintptr_t address = (uintptr_t)mc_u32(element + block->offset + 4);

		if (count == 0)
			continue;
		if (count > block->maximum || !address)
		{
			mc_why(write->why, write->why_size, "a block of %ld elements (the game's most %ld)", (long)count,
				(long)block->maximum);
			write->failed = 1;
			return;
		}
		mc_write_elements(write, block->kind, (uint8_t const *)address, count);
	}
}

static void mc_write_elements(struct mc_write *write, int kind, uint8_t const *elements, uint32_t count)
{
	struct mc_layout const *layout = &mc_layouts[kind];
	uint32_t index;
	uint8_t at;

	if (write->failed)
		return;
	if (count > MC_MAXIMUM_ELEMENTS || (write->elements += count) > MC_MAXIMUM_ELEMENTS ||
		(uint64_t)count * layout->size > (uint64_t)(write->size - write->length))
	{
		mc_why(write->why, write->why_size, "more than %ld bytes (%ld elements)", (long)write->size, write->elements);
		write->failed = 1;
		return;
	}
	for (index = 0; index < count; index++)
	{
		uint8_t *element = write->out + write->length;

		memcpy(element, elements + (size_t)index * layout->size, layout->size);
		for (at = 0; at < layout->block_count; at++)
		{
			mc_put_u32(element + layout->blocks[at].offset + 4, 0);
			mc_put_u32(element + layout->blocks[at].offset + 8, 0);
		}
		for (at = 0; at < layout->reference_count; at++)
		{
			if (mc_u32(element + layout->references[at] + 12) != MC_NONE)
			{
				mc_why(write->why, write->why_size, "it names another tag (a reference at %ld of kind %ld)",
					(long)layout->references[at], (long)kind);
				write->failed = 1;
				return;
			}
			mc_put_u32(element + layout->references[at] + 4, 0);
			mc_put_u32(element + layout->references[at] + 8, 0);
		}
		write->length += layout->size;
	}
	for (index = 0; index < count && !write->failed; index++)
		mc_write_element_blocks(write, kind, elements + (size_t)index * layout->size);
}

uint32_t mixed_collision_model_write(void const *root, uint8_t *out, uint32_t size, char *why, int why_size)
{
	struct mc_write write;

	memset(&write, 0, sizeof(write));
	write.out = out;
	write.size = size;
	write.why = why;
	write.why_size = why_size;
	if (!root || !out)
	{
		mc_why(why, why_size, "no model%s", 0, 0);
		return 0;
	}
	mc_write_elements(&write, MC_ROOT, (uint8_t const *)root, 1);
	return write.failed ? 0 : write.length;
}

/* ---------- reading: the bytes walked twice, first to check and size them, then to lay them out */

struct mc_read
{
	uint8_t const *bytes;
	uint32_t length;
	uint32_t at;
	long elements;
	/* (the second walk) */
	uint8_t *region;
	uint32_t region_used;
	uint32_t region_size;
	uint32_t empty_name;
	char *why;
	int why_size;
	int failed;
};

static uint32_t mc_align(uint32_t value)
{
	return (value + 7u) & ~7u;
}

static int mc_check_element(struct mc_read *read, int kind, uint8_t const *element)
{
	struct mc_layout const *layout = &mc_layouts[kind];
	uint8_t index;

	for (index = 0; index < layout->reference_count; index++)
	{
		uint8_t const *reference = element + layout->references[index];
		uint32_t group = mc_u32(reference);

		if (mc_u32(reference + 4) || mc_u32(reference + 8) || mc_u32(reference + 12) != MC_NONE ||
			(group != MC_NONE && group != 0x65666665u /* 'effe' */))
		{
			mc_why(read->why, read->why_size, "a reference that names a tag (kind %ld at %ld)", kind,
				(long)layout->references[index]);
			return 0;
		}
	}
	for (index = 0; index < layout->block_count; index++)
	{
		if (mc_u32(element + layout->blocks[index].offset + 4) || mc_u32(element + layout->blocks[index].offset + 8))
		{
			mc_why(read->why, read->why_size, "a block with an address (kind %ld at %ld)", kind,
				(long)layout->blocks[index].offset);
			return 0;
		}
	}
	for (index = 0; index < layout->string_count; index++)
	{
		if (!memchr(element + layout->strings[index], 0, MC_STRING_BYTES))
		{
			mc_why(read->why, read->why_size, "a name not ended (kind %ld at %ld)", kind, (long)layout->strings[index]);
			return 0;
		}
	}
	for (index = 0; index < layout->float_run_count; index++)
	{
		uint32_t value;

		for (value = 0; value < layout->float_runs[index][1]; value++)
		{
			if (!mc_finite(element + layout->float_runs[index][0] + value * 4))
			{
				mc_why(read->why, read->why_size, "a value not a number (kind %ld at %ld)", kind,
					(long)(layout->float_runs[index][0] + value * 4));
				return 0;
			}
		}
	}
	return 1;
}

/* the elements of one block, at read->at, checked (first walk) or laid out (second walk): their offset in the
region (second walk) */
static uint32_t mc_read_elements(struct mc_read *read, int kind, uint32_t count);

static void mc_read_element_blocks(struct mc_read *read, int kind, uint8_t const *element, uint8_t *laid)
{
	struct mc_layout const *layout = &mc_layouts[kind];
	uint8_t index;

	for (index = 0; index < layout->block_count && !read->failed; index++)
	{
		struct mc_block const *block = &layout->blocks[index];
		uint32_t count = mc_u32(element + block->offset);
		uint32_t offset;

		if (count > block->maximum)
		{
			mc_why(read->why, read->why_size, "a block of %ld elements (the game's most %ld)", (long)count,
				(long)block->maximum);
			read->failed = 1;
			return;
		}
		if (!count)
			continue;
		offset = mc_read_elements(read, block->kind, count);
		if (laid && !read->failed)
			mc_put_u32(laid + block->offset + 4, (uint32_t)(uintptr_t)(read->region + offset));
	}
}

static uint32_t mc_read_elements(struct mc_read *read, int kind, uint32_t count)
{
	struct mc_layout const *layout = &mc_layouts[kind];
	uint32_t index, start = read->at, offset = 0;
	uint8_t at;

	if (read->failed)
		return 0;
	if (count > MC_MAXIMUM_ELEMENTS || (read->elements += count) > MC_MAXIMUM_ELEMENTS ||
		(uint64_t)count * layout->size > (uint64_t)(read->length - read->at))
	{
		mc_why(read->why, read->why_size, "a block of %ld elements past the model's bytes (%ld)", (long)count,
			(long)read->length);
		read->failed = 1;
		return 0;
	}
	read->at += count * layout->size;
	if (read->region)
	{
		offset = mc_align(read->region_used);
		if (offset > read->region_size || (uint64_t)count * layout->size > (uint64_t)(read->region_size - offset))
		{
			mc_why(read->why, read->why_size, "a layout past its room%s", 0, 0);
			read->failed = 1;
			return 0;
		}
		memcpy(read->region + offset, read->bytes + start, (size_t)count * layout->size);
		read->region_used = offset + count * layout->size;
		/* (each reference's name the region's empty string) */
		for (index = 0; index < count; index++)
		{
			for (at = 0; at < layout->reference_count; at++)
				mc_put_u32(read->region + offset + index * layout->size + layout->references[at] + 4,
					(uint32_t)(uintptr_t)(read->region + read->empty_name));
		}
	}
	else
	{
		for (index = 0; index < count; index++)
		{
			if (!mc_check_element(read, kind, read->bytes + start + index * layout->size))
			{
				read->failed = 1;
				return 0;
			}
		}
		read->region_used = mc_align(read->region_used) + count * layout->size;
	}
	for (index = 0; index < count && !read->failed; index++)
	{
		mc_read_element_blocks(read, kind, read->bytes + start + index * layout->size,
			read->region ? read->region + offset + index * layout->size : NULL);
	}
	return offset;
}

/* a signed index into a block of `count`, or none where taken */
static int mc_index_ok(uint32_t index, uint32_t count, int none_taken)
{
	if (index == MC_NONE)
		return none_taken;
	return index < count;
}

/* the same for a 16-bit index */
static int mc_short_ok(uint16_t index, uint32_t count, int none_taken)
{
	if (index == 0xFFFFu)
		return none_taken;
	return index < count;
}

#define MC_BLOCK_COUNT(element, offset) mc_u32((element) + (offset))
#define MC_BLOCK_ADDRESS(element, offset) ((uint8_t const *)(uintptr_t)mc_u32((element) + (offset) + 4))

/* a bsp3d or bsp2d child: a node after this one, a leaf (bsp3d) or surface (bsp2d) by its index with the top bit, or
none */
static int mc_child_ok(uint32_t child, uint32_t self, uint32_t nodes, uint32_t others)
{
	if (child == MC_NONE)
		return 1;
	if (child & 0x80000000u)
		return (child & 0x7FFFFFFFu) < others;
	return child > self && child < nodes;
}

/* a laid out bsp's every index (the second walk's, its addresses the region's) */
static int mc_bsp_check(struct mc_read *read, uint8_t const *bsp, uint32_t materials)
{
	uint32_t bsp3d_nodes = MC_BLOCK_COUNT(bsp, MIXED_COLLISION_BSP_BSP3D_NODES);
	uint32_t planes = MC_BLOCK_COUNT(bsp, MIXED_COLLISION_BSP_PLANES);
	uint32_t leaves = MC_BLOCK_COUNT(bsp, MIXED_COLLISION_BSP_LEAVES);
	uint32_t references = MC_BLOCK_COUNT(bsp, MIXED_COLLISION_BSP_BSP2D_REFERENCES);
	uint32_t bsp2d_nodes = MC_BLOCK_COUNT(bsp, MIXED_COLLISION_BSP_BSP2D_NODES);
	uint32_t surfaces = MC_BLOCK_COUNT(bsp, MIXED_COLLISION_BSP_SURFACES);
	uint32_t edges = MC_BLOCK_COUNT(bsp, MIXED_COLLISION_BSP_EDGES);
	uint32_t vertices = MC_BLOCK_COUNT(bsp, MIXED_COLLISION_BSP_VERTICES);
	uint8_t const *element;
	uint32_t index;

	if (!planes && (bsp3d_nodes || surfaces || references))
	{
		mc_why(read->why, read->why_size, "a bsp of %ld nodes and %ld surfaces with no planes", (long)bsp3d_nodes,
			(long)surfaces);
		return 0;
	}
	for (index = 0; index < bsp3d_nodes; index++)
	{
		element = MC_BLOCK_ADDRESS(bsp, MIXED_COLLISION_BSP_BSP3D_NODES) + index * MIXED_COLLISION_BSP3D_NODE_BYTES;
		if ((mc_u32(element) & 0x7FFFFFFFu) >= planes || !mc_child_ok(mc_u32(element + 4), index, bsp3d_nodes, leaves) ||
			!mc_child_ok(mc_u32(element + 8), index, bsp3d_nodes, leaves))
		{
			mc_why(read->why, read->why_size, "bsp3d node %ld of %ld names what is not there", (long)index, (long)bsp3d_nodes);
			return 0;
		}
	}
	for (index = 0; index < leaves; index++)
	{
		uint16_t count;
		uint32_t first;

		element = MC_BLOCK_ADDRESS(bsp, MIXED_COLLISION_BSP_LEAVES) + index * MIXED_COLLISION_LEAF_BYTES;
		count = mc_u16(element + 2);
		first = mc_u32(element + 4);
		if ((count & 0x8000u) || (count && (first >= references || count > references - first)))
		{
			mc_why(read->why, read->why_size, "leaf %ld's bsp2d references are not the bsp's (%ld)", (long)index,
				(long)references);
			return 0;
		}
	}
	for (index = 0; index < references; index++)
	{
		uint32_t root;

		element = MC_BLOCK_ADDRESS(bsp, MIXED_COLLISION_BSP_BSP2D_REFERENCES) + index * MIXED_COLLISION_BSP2D_REFERENCE_BYTES;
		root = mc_u32(element + 4);
		if ((mc_u32(element) & 0x7FFFFFFFu) >= planes || root == MC_NONE ||
			((root & 0x80000000u) ? (root & 0x7FFFFFFFu) >= surfaces : root >= bsp2d_nodes))
		{
			mc_why(read->why, read->why_size, "bsp2d reference %ld of %ld names what is not there", (long)index,
				(long)references);
			return 0;
		}
	}
	for (index = 0; index < bsp2d_nodes; index++)
	{
		element = MC_BLOCK_ADDRESS(bsp, MIXED_COLLISION_BSP_BSP2D_NODES) + index * MIXED_COLLISION_BSP2D_NODE_BYTES;
		if (mc_u32(element + 12) == MC_NONE || mc_u32(element + 16) == MC_NONE ||
			!mc_child_ok(mc_u32(element + 12), index, bsp2d_nodes, surfaces) ||
			!mc_child_ok(mc_u32(element + 16), index, bsp2d_nodes, surfaces))
		{
			mc_why(read->why, read->why_size, "bsp2d node %ld of %ld names what is not there", (long)index,
				(long)bsp2d_nodes);
			return 0;
		}
	}
	for (index = 0; index < vertices; index++)
	{
		element = MC_BLOCK_ADDRESS(bsp, MIXED_COLLISION_BSP_VERTICES) + index * MIXED_COLLISION_VERTEX_BYTES;
		if (!mc_index_ok(mc_u32(element + 12), edges, 0))
		{
			mc_why(read->why, read->why_size, "vertex %ld's edge is not the bsp's (%ld)", (long)index, (long)edges);
			return 0;
		}
	}
	for (index = 0; index < edges; index++)
	{
		element = MC_BLOCK_ADDRESS(bsp, MIXED_COLLISION_BSP_EDGES) + index * MIXED_COLLISION_EDGE_BYTES;
		if (!mc_index_ok(mc_u32(element), vertices, 0) || !mc_index_ok(mc_u32(element + 4), vertices, 0) ||
			!mc_index_ok(mc_u32(element + 8), edges, 0) || !mc_index_ok(mc_u32(element + 12), edges, 0) ||
			!mc_index_ok(mc_u32(element + 16), surfaces, 1) || !mc_index_ok(mc_u32(element + 20), surfaces, 1))
		{
			mc_why(read->why, read->why_size, "edge %ld of %ld names what is not there", (long)index, (long)edges);
			return 0;
		}
	}
	for (index = 0; index < surfaces; index++)
	{
		uint32_t first, edge, steps;

		element = MC_BLOCK_ADDRESS(bsp, MIXED_COLLISION_BSP_SURFACES) + index * MIXED_COLLISION_SURFACE_BYTES;
		first = mc_u32(element + 4);
		if ((mc_u32(element) & 0x7FFFFFFFu) >= planes || !mc_index_ok(first, edges, 0) ||
			!mc_short_ok(mc_u16(element + 10), materials, 1))
		{
			mc_why(read->why, read->why_size, "surface %ld of %ld names what is not there", (long)index, (long)surfaces);
			return 0;
		}
		/* (its edge ring: around the surface back to its first edge) */
		for (edge = first, steps = 0; ; steps++)
		{
			uint8_t const *at = MC_BLOCK_ADDRESS(bsp, MIXED_COLLISION_BSP_EDGES) + edge * MIXED_COLLISION_EDGE_BYTES;

			if (steps >= MIXED_COLLISION_MAXIMUM_RING)
			{
				mc_why(read->why, read->why_size, "surface %ld's edge ring is not closed in %ld edges", (long)index,
					MIXED_COLLISION_MAXIMUM_RING);
				return 0;
			}
			if (mc_u32(at + 16) == index)
				edge = mc_u32(at + 8);
			else if (mc_u32(at + 20) == index)
				edge = mc_u32(at + 12);
			else
			{
				mc_why(read->why, read->why_size, "surface %ld's edge ring leaves it at edge %ld", (long)index, (long)edge);
				return 0;
			}
			if (edge == first)
				break;
		}
	}
	return 1;
}

/* the laid out model's indices across its blocks */
static int mc_model_check(struct mc_read *read, uint8_t const *root)
{
	uint32_t materials = MC_BLOCK_COUNT(root, MIXED_COLLISION_ROOT_MATERIALS);
	uint32_t regions = MC_BLOCK_COUNT(root, MIXED_COLLISION_ROOT_REGIONS);
	uint32_t nodes = MC_BLOCK_COUNT(root, MIXED_COLLISION_ROOT_NODES);
	uint32_t spheres = MC_BLOCK_COUNT(root, MIXED_COLLISION_ROOT_SPHERES);
	uint32_t index, bsp;

	if (!mc_short_ok(mc_u16(root + 4), materials, 1))
	{
		mc_why(read->why, read->why_size, "its indirect damage material %ld is not one of its %ld", (long)mc_u16(root + 4),
			(long)materials);
		return 0;
	}
	if (mc_u16(root + 0xD2) >= MIXED_COLLISION_MAXIMUM_MATERIAL_TYPES)
	{
		mc_why(read->why, read->why_size, "its shield material type %ld%s", (long)mc_u16(root + 0xD2), 0);
		return 0;
	}
	for (index = 0; index < materials; index++)
	{
		uint8_t const *material = MC_BLOCK_ADDRESS(root, MIXED_COLLISION_ROOT_MATERIALS) + index * MIXED_COLLISION_MATERIAL_BYTES;

		if (mc_u16(material + 0x24) >= MIXED_COLLISION_MAXIMUM_MATERIAL_TYPES)
		{
			mc_why(read->why, read->why_size, "material %ld's type %ld", (long)index, (long)mc_u16(material + 0x24));
			return 0;
		}
	}
	for (index = 0; index < spheres; index++)
	{
		uint8_t const *sphere = MC_BLOCK_ADDRESS(root, MIXED_COLLISION_ROOT_SPHERES) + index * MIXED_COLLISION_SPHERE_BYTES;

		if (!mc_short_ok(mc_u16(sphere), MIXED_COLLISION_MAXIMUM_NODES, 1))
		{
			mc_why(read->why, read->why_size, "pathfinding sphere %ld's node %ld", (long)index, (long)mc_u16(sphere));
			return 0;
		}
	}
	for (index = 0; index < nodes; index++)
	{
		uint8_t const *node = MC_BLOCK_ADDRESS(root, MIXED_COLLISION_ROOT_NODES) + index * MIXED_COLLISION_NODE_BYTES;

		if (!mc_short_ok(mc_u16(node + 0x20), regions, 1) || !mc_short_ok(mc_u16(node + 0x22), nodes, 1) ||
			!mc_short_ok(mc_u16(node + 0x24), nodes, 1) || !mc_short_ok(mc_u16(node + 0x26), nodes, 1))
		{
			mc_why(read->why, read->why_size, "node %ld of %ld names what is not there", (long)index, (long)nodes);
			return 0;
		}
		for (bsp = 0; bsp < MC_BLOCK_COUNT(node, MIXED_COLLISION_NODE_BSPS); bsp++)
		{
			if (!mc_bsp_check(read, MC_BLOCK_ADDRESS(node, MIXED_COLLISION_NODE_BSPS) + bsp * MIXED_COLLISION_BSP_BYTES,
				materials))
			{
				return 0;
			}
		}
	}
	return 1;
}

int mixed_collision_model_rebuild(uint8_t const *bytes, uint32_t length, uint32_t reserve, uint8_t **region,
	uint32_t *region_bytes, uint32_t *root_offset, char *why, int why_size)
{
	struct mc_read read;
	uint32_t size, root;

	*region = NULL;
	*region_bytes = 0;
	*root_offset = 0;
	if (!bytes || length < MIXED_COLLISION_ROOT_BYTES || length > MIXED_COLLISION_MAXIMUM_MODEL_BYTES ||
		reserve > 4096)
	{
		mc_why(why, why_size, "a model of %ld bytes%s", (long)length, 0);
		return 0;
	}
	/* (the first walk: every element checked, the region's size worked out) */
	memset(&read, 0, sizeof(read));
	read.bytes = bytes;
	read.length = length;
	read.why = why;
	read.why_size = why_size;
	read.region_used = mc_align(reserve) + 8 /* the empty name */;
	root = mc_align(read.region_used);
	mc_read_elements(&read, MC_ROOT, 1);
	if (read.failed)
		return 0;
	if (read.at != length)
	{
		mc_why(why, why_size, "%ld bytes past the model's (%ld)", (long)(length - read.at), (long)read.at);
		return 0;
	}
	size = mc_align(read.region_used);
	/* (the second walk: laid out in the region, its addresses the region's) */
	read.region = calloc(1, size);
	if (!read.region)
	{
		mc_why(why, why_size, "no memory for %ld bytes%s", (long)size, 0);
		return 0;
	}
	read.region_size = size;
	read.at = 0;
	read.elements = 0;
	read.empty_name = mc_align(reserve);
	read.region_used = read.empty_name + 8;
	if (mc_read_elements(&read, MC_ROOT, 1) != root || read.failed || !mc_model_check(&read, read.region + root))
	{
		if (!read.failed && why && why_size > 0 && !why[0])
			mc_why(why, why_size, "its root not where it was laid out%s", 0, 0);
		free(read.region);
		return 0;
	}
	*region = read.region;
	*region_bytes = size;
	*root_offset = root;
	return 1;
}

/* ---------- the stream */

void mixed_collision_writer_begin(struct mixed_collision_writer *writer, void *bytes, uint32_t size, uint8_t level)
{
	memset(writer, 0, sizeof(*writer));
	writer->bytes = (uint8_t *)bytes;
	writer->size = size;
	if (size < 8 + 4)
	{
		writer->overflow = 1;
		return;
	}
	mc_put_u32(writer->bytes, MIXED_COLLISION_MAGIC);
	writer->bytes[4] = (uint8_t)MIXED_COLLISION_VERSION;
	writer->bytes[5] = 0;
	writer->bytes[6] = level;
	writer->bytes[7] = 0;
	writer->length = 8;
}

void mixed_collision_writer_model(struct mixed_collision_writer *writer, uint8_t model, uint8_t const *data,
	uint32_t length)
{
	if (writer->overflow || writer->model_count >= NUMBER_OF_MIXED_COLLISION_MODELS ||
		length > MIXED_COLLISION_MAXIMUM_MODEL_BYTES || writer->size - writer->length < 8 + length + 4)
	{
		writer->overflow = 1;
		return;
	}
	writer->bytes[writer->length] = model;
	writer->bytes[writer->length + 1] = 0;
	writer->bytes[writer->length + 2] = 0;
	writer->bytes[writer->length + 3] = 0;
	mc_put_u32(writer->bytes + writer->length + 4, length);
	memcpy(writer->bytes + writer->length + 8, data, length);
	writer->length += 8 + length;
	writer->model_count++;
	writer->bytes[7] = writer->model_count;
}

uint32_t mixed_collision_writer_end(struct mixed_collision_writer *writer)
{
	if (writer->overflow || !writer->model_count || writer->size - writer->length < 4)
		return 0;
	mc_put_u32(writer->bytes + writer->length, mixed_coop_fnv(MIXED_COOP_FNV_BASIS, writer->bytes, writer->length));
	writer->length += 4;
	return writer->length;
}

int mixed_collision_stream_read(uint8_t const *bytes, uint32_t length, struct mixed_collision_stream *stream,
	char *why, int why_size)
{
	uint32_t at = 8, model_bits = 0;
	uint8_t index;

	memset(stream, 0, sizeof(*stream));
	if (!bytes || length < 8 + 8 + 4 || length > MIXED_COLLISION_MAXIMUM_STREAM_BYTES)
	{
		mc_why(why, why_size, "a stream of %ld bytes%s", (long)length, 0);
		return 0;
	}
	if (mc_u32(bytes) != MIXED_COLLISION_MAGIC || mc_u16(bytes + 4) != MIXED_COLLISION_VERSION)
	{
		mc_why(why, why_size, "not a version %ld stream%s", MIXED_COLLISION_VERSION, 0);
		return 0;
	}
	if (mc_u32(bytes + length - 4) != mixed_coop_fnv(MIXED_COOP_FNV_BASIS, bytes, length - 4))
	{
		mc_why(why, why_size, "its check does not match%s%s", 0, 0);
		return 0;
	}
	stream->level = bytes[6];
	stream->model_count = bytes[7];
	if (stream->level >= MIXED_COOP_LEVELS || stream->level < MIXED_COOP_CAMPAIGN_LEVELS || !stream->model_count ||
		stream->model_count > NUMBER_OF_MIXED_COLLISION_MODELS)
	{
		mc_why(why, why_size, "level %ld, %ld models", (long)stream->level, (long)stream->model_count);
		return 0;
	}
	for (index = 0; index < stream->model_count; index++)
	{
		uint8_t model;
		uint32_t bytes_count;

		if (length - 4 - at < 8)
		{
			mc_why(why, why_size, "model %ld past the stream's end%s", (long)index, 0);
			return 0;
		}
		model = bytes[at];
		bytes_count = mc_u32(bytes + at + 4);
		if (model >= NUMBER_OF_MIXED_COLLISION_MODELS || (model_bits & (1u << model)) || bytes[at + 1] || bytes[at + 2] ||
			bytes[at + 3] || bytes_count < MIXED_COLLISION_ROOT_BYTES || bytes_count > MIXED_COLLISION_MAXIMUM_MODEL_BYTES ||
			bytes_count > length - 4 - at - 8)
		{
			mc_why(why, why_size, "model %ld (%ld bytes) not one taken", (long)model, (long)bytes_count);
			return 0;
		}
		model_bits |= 1u << model;
		stream->models[index].model = model;
		stream->models[index].bytes = bytes_count;
		stream->models[index].data = bytes + at + 8;
		at += 8 + bytes_count;
	}
	if (at != length - 4)
	{
		mc_why(why, why_size, "%ld bytes past its models%s", (long)(length - 4 - at), 0);
		return 0;
	}
	return 1;
}

/* ---------- reassembly */

void mixed_collision_reassembly_begin(struct mixed_collision_reassembly *reassembly, uint32_t session)
{
	reassembly->session = session;
	reassembly->total_bytes = 0;
	reassembly->count = 0;
	reassembly->received = 0;
	memset(reassembly->have, 0, sizeof(reassembly->have));
}

int mixed_collision_reassembly_take(struct mixed_collision_reassembly *reassembly,
	struct mixed_coop_fragment const *fragment, uint32_t fragment_bytes)
{
	uint32_t expected_count, offset;

	if (!fragment || fragment_bytes < offsetof(struct mixed_coop_fragment, data) ||
		fragment->session != reassembly->session || fragment->pad != 0 ||
		fragment->total_bytes == 0 || fragment->total_bytes > MIXED_COLLISION_MAXIMUM_STREAM_BYTES)
	{
		return _mixed_coop_fragment_refused;
	}
	expected_count = (fragment->total_bytes + MIXED_COLLISION_FRAGMENT_BYTES - 1) / MIXED_COLLISION_FRAGMENT_BYTES;
	if (fragment->count != expected_count || fragment->index >= fragment->count ||
		fragment->count > MIXED_COLLISION_MAXIMUM_FRAGMENTS)
	{
		return _mixed_coop_fragment_refused;
	}
	if (reassembly->count == 0)
	{
		reassembly->total_bytes = fragment->total_bytes;
		reassembly->count = fragment->count;
	}
	else if (reassembly->total_bytes != fragment->total_bytes || reassembly->count != fragment->count)
	{
		return _mixed_coop_fragment_refused;
	}
	offset = (uint32_t)fragment->index * MIXED_COLLISION_FRAGMENT_BYTES;
	if (fragment->bytes != (fragment->index + 1u == fragment->count ? fragment->total_bytes - offset :
		MIXED_COLLISION_FRAGMENT_BYTES) || fragment->bytes > fragment_bytes - offsetof(struct mixed_coop_fragment, data))
	{
		return _mixed_coop_fragment_refused;
	}
	if (reassembly->have[fragment->index])
		return _mixed_coop_fragment_refused;
	reassembly->have[fragment->index] = 1;
	memcpy(reassembly->bytes + offset, fragment->data, fragment->bytes);
	reassembly->received++;
	return reassembly->received == reassembly->count ? _mixed_coop_fragment_complete : _mixed_coop_fragment_taken;
}
