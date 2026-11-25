/*
MIXED_COOP_TABLES.H

(1.1.3) Mixed play with a host that knows nothing of it: the built-in tables.

A host of an older build, an OpenCE host or a ChupathingyCE host plays the
stock maps from the Xbox's files and never says it plays mixed, so a machine
with only MCC's copy of the level gets no dictionary from it. Every stock host
plays the same Xbox files, so the dictionary such a host would send is known
in advance: for each of the 23 mixed levels this build carries it
(mixed_coop_tables.inc, made by tools/mixed_tables.py from the Xbox's maps
with this build's own digests). It is what a host's dictionary carries and no
more: each tag's group and path hash, its digest (one-way) and its datum,
each animation graph's animations' name hashes, the level's digests; the
netgame flags' types and teams without their places. No tag data, no
geometry, no coordinates.

For Blood Gulch, Battle Creek and Sidewinder (Halo PC placed other scenery and
vehicles there) a table also carries the Xbox's placements' structure: its
scenery and vehicle palettes by path hash and its placements with their
places left out, each naming the placement of this machine's MCC file whose
place it takes (matched by tag and nearest place when the table was made),
or one of MCC's own objects to stand in, at its place, for an Xbox
placement MCC's file has nothing near (so that every object after it has the
Xbox's index on every machine).

Both are this build's own data, read with the same checked readers as a
host's streams (mixed_coop_dictionary_read, mixed_coop_layout_read).
*/

#ifndef HALO_MIXED_COOP_TABLES_H
#define HALO_MIXED_COOP_TABLES_H

#include <stdint.h>

/* a placement's source in a layout table (after the MXL1 stream, one per placement of its blocks in order) */
enum
{
	/* the Xbox's tag at the MCC placement's place */
	_mixed_coop_table_source_matched = 0,
	/* the MCC placement itself, its own tag at its place (an Xbox placement MCC's file has nothing near) */
	_mixed_coop_table_source_stand_in = 1,
};

#define MIXED_COOP_TABLE_LAYOUT_MAGIC 0x31534C4Du /* 'MLS1' little-endian */

/* whether the level (mixed_coop_level_index's) has a built-in table */
int mixed_coop_table_present(int level);

/* the level's dictionary (an MXD1 stream, as a host sends it), inflated into
a new allocation (*bytes, let go with mixed_coop_table_free): 1, or 0 for none */
int mixed_coop_table_dictionary(int level, uint8_t **bytes, uint32_t *length);

/* the level's layout table (Blood Gulch, Battle Creek, Sidewinder):

	u32 magic 'MLS1', u32 the MXL1 stream's length, the MXL1 stream (its
	    placements' places zero, no netgame flags), then for each placement
	    of its blocks in order { u16 the MCC placement's index in this
	    machine's block, u8 source (_mixed_coop_table_source_*), u8 0 }

inflated into a new allocation: 1, or 0 for none */
int mixed_coop_table_layout(int level, uint8_t **bytes, uint32_t *length);

/* a table's allocation let go (the C library's own free: the game's allocator never made it) */
void mixed_coop_table_free(void *bytes);

#endif
