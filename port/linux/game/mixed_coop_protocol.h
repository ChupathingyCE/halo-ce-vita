/*
MIXED_COOP_PROTOCOL.H

Mixed co-op's messages (mixed_coop.h): a campaign co-op game where some
machines play a level from the Xbox's maps and others the same level from
Halo: The Master Chief Collection's files (the Halo content folder). Plain C,
no game headers, for the fuzz target (port/vita/tests/net_fuzz_mixed_coop.c).

Three things go on the wire, none of them map data:

- In the lobby (the Bungie messages _message_server_mixed_coop and
  _message_client_mixed_coop, past the flaming helmet's): the host of a co-op
  game on a stock campaign level says hello to each machine that joins - its
  family (the files it plays the level from) and the level -, and a machine
  that plays mixed co-op answers with its own family for that level. A
  machine that never answers is an older build, which can only play the
  host's own family (it refuses the other by itself: see mixed_coop.h).
- Once a machine has loaded the level, before it says it is ready (the
  distributed kinds _distributed_message_mixed_request, _mixed_dictionary and
  _mixed_bsp): its request with its own digests; the host's tag dictionary,
  a byte stream in fragments (MIXED_COOP_DICTIONARY_*): the level's digests
  (layout, gameplay, the scripts and AI the host runs, each structure BSP's
  collision as the host has loaded it), then every tag of the host's by a
  hash of its group and path and the digest of what the game reads in it,
  then each animation graph's animations by a hash of their names; and a
  structure BSP's collision digest as the host first loads it.

Version 2 (mixed multiplayer): the same on the multiplayer maps whose files
are one game, and the host's netgame flags at the dictionary's end.

Everything read here is another machine's: every count and length is
checked against the bytes there are, before anything is taken.
*/

#ifndef HALO_MIXED_COOP_PROTOCOL_H
#define HALO_MIXED_COOP_PROTOCOL_H

#include <stdint.h>

/* the protocol's versions. 1: the campaign's ten levels (co-op). 2: the
multiplayer maps too (MIXED_COOP_LEVELS). A message carries the lowest version
that can say its level (mixed_coop_version_for_level): about a campaign level
it is version 1, byte for byte what the first builds send and take; about a
multiplayer map, version 2, which a version 1 build refuses at its version
check before it reads the level (never misparsed: it then plays as it always
did, and its own family check refuses the other family's map). A message of
a version not listed here, or a level its version cannot name, is refused. */
#define MIXED_COOP_VERSION_CAMPAIGN 1
#define MIXED_COOP_VERSION 2

/* the files a machine plays a level from */
enum mixed_coop_family
{
	_mixed_coop_family_none = 0,
	/* the Xbox's maps folder (levels\a10\a10 from a10.map) */
	_mixed_coop_family_xbox,
	/* MCC's halo1\maps (Halo: CE Anniversary caches) */
	_mixed_coop_family_mcc,
	NUMBER_OF_MIXED_COOP_FAMILIES
};

/* the levels by index: the campaign's ten (a10 ... d40, as main.c names
them, 0 to 9: version 1), then the multiplayer maps whose MCC and Xbox files
the research found to have the same collision (10 ...: version 2,
mixed_coop.c's mixed_coop_level_stems), Damnation (17), and two Halo PC
rebuilt, Derelict and Hang 'Em High (18, 19: an Xbox-files host's game alone,
mixed_coop_rebuilt_maps) */
#define MIXED_COOP_CAMPAIGN_LEVELS 10
#define MIXED_COOP_MULTIPLAYER_LEVELS 10
#define MIXED_COOP_LEVELS (MIXED_COOP_CAMPAIGN_LEVELS + MIXED_COOP_MULTIPLAYER_LEVELS)
#define MIXED_COOP_NO_LEVEL 0xFF

/* ---------- the lobby */

enum
{
	_mixed_coop_lobby_hello = 1,
	_mixed_coop_lobby_answer,
	NUMBER_OF_MIXED_COOP_LOBBY_COMMANDS
};

/* both ways (16 bytes) */
struct mixed_coop_lobby_message
{
	uint16_t command;
	uint16_t version;
	/* the sender's family for the level (a joiner without the level: none) */
	uint8_t family;
	/* the level (0 to 9; version 2: 0 to MIXED_COOP_LEVELS - 1) */
	uint8_t level;
	uint16_t flags;
	/* the host's hosting (the hello's, echoed by the answer) */
	uint32_t session;
	uint32_t reserved;
};

typedef char mixed_coop_lobby_message_size_assert[sizeof(struct mixed_coop_lobby_message) == 16 ? 1 : -1];

/* a lobby message checked: its command, version, family and level, no
reserved bits; 1 when it can be taken */
int mixed_coop_lobby_valid(struct mixed_coop_lobby_message const *message);
/* the version a message about `level` carries (MIXED_COOP_VERSION_CAMPAIGN
for a campaign level), and whether `version` can name `level` (1: a known
version and a level it names) */
int mixed_coop_version_for_level(unsigned level);
int mixed_coop_version_names_level(unsigned version, unsigned level);

/* ---------- after the load */

/* digests of a level, as one machine computed them */
struct mixed_coop_digests
{
	/* the scenario's placements, palettes, names, device groups, trigger
	volumes and starts (the MCC-only extras left out) */
	uint32_t layout;
	/* every gameplay tag's digest, by path, in path-hash order */
	uint32_t gameplay;
	/* what the host alone runs: the scripts and the AI's encounters (told,
	not refused) */
	uint32_t host_only;
	uint32_t tag_count;
	/* the layout on the second grid (positions half a step over:
	mixed_coop.c) */
	uint32_t layout_offset;
};

/* a client's request for the host's dictionary (one entry, reliable) */
struct mixed_coop_request
{
	uint16_t version;
	uint8_t family;
	uint8_t level;
	uint32_t session;
	struct mixed_coop_digests digests;
};

typedef char mixed_coop_request_size_assert[sizeof(struct mixed_coop_request) == 28 ? 1 : -1];

int mixed_coop_request_valid(struct mixed_coop_request const *request);

/* a piece of the host's dictionary stream (one entry, reliable) */
#define MIXED_COOP_FRAGMENT_BYTES 3200
/* the longest stream taken (the host's tags and animations: a10's is about
40 KB) */
#define MIXED_COOP_DICTIONARY_MAXIMUM_BYTES (256 * 1024)
#define MIXED_COOP_DICTIONARY_MAXIMUM_FRAGMENTS \
	((MIXED_COOP_DICTIONARY_MAXIMUM_BYTES + MIXED_COOP_FRAGMENT_BYTES - 1) / MIXED_COOP_FRAGMENT_BYTES)

struct mixed_coop_fragment
{
	uint32_t session;
	uint32_t total_bytes;
	uint16_t index;
	uint16_t count;
	uint16_t bytes;
	uint16_t pad;
	uint8_t data[MIXED_COOP_FRAGMENT_BYTES];
};

typedef char mixed_coop_fragment_size_assert[sizeof(struct mixed_coop_fragment) == 16 + MIXED_COOP_FRAGMENT_BYTES ? 1 : -1];

/* a structure BSP's collision digest as the host loaded it (one entry,
reliable, to every mixed client) */
struct mixed_coop_bsp_digest
{
	uint32_t session;
	uint16_t bsp_index;
	uint16_t pad;
	/* the collision's digest on two grids half a step apart (one of them
	takes any difference smaller than half a step: mixed_coop.c) */
	uint32_t digest;
	uint32_t digest_offset;
};

typedef char mixed_coop_bsp_digest_size_assert[sizeof(struct mixed_coop_bsp_digest) == 16 ? 1 : -1];

/* the stream's reassembly: the fragments of one session, in any order, each
once */
struct mixed_coop_reassembly
{
	uint32_t session;
	uint32_t total_bytes;
	uint16_t count;
	uint16_t received;
	uint8_t have[MIXED_COOP_DICTIONARY_MAXIMUM_FRAGMENTS];
	uint8_t bytes[MIXED_COOP_DICTIONARY_MAXIMUM_BYTES];
};

enum
{
	_mixed_coop_fragment_refused = -1,
	_mixed_coop_fragment_taken = 0,
	_mixed_coop_fragment_complete = 1,
};

/* starts a reassembly of the session's stream */
void mixed_coop_reassembly_begin(struct mixed_coop_reassembly *reassembly, uint32_t session);
/* takes a fragment: refused (another session, sizes that do not add up, a
piece twice), taken, or the stream complete (its bytes in reassembly) */
int mixed_coop_reassembly_take(struct mixed_coop_reassembly *reassembly, struct mixed_coop_fragment const *fragment);

/* ---------- the dictionary stream

	u32 magic 'MXD1', u16 version, u8 family, u8 level
	u32 tag count, digests (layout, gameplay, host_only, layout on the second
	    grid)
	u16 structure BSP count, u16 0, then two u32 collision digests each (its
	    two grids; 0: not loaded on the host yet)
	tag count x { u32 path hash, u32 digest (0: not a gameplay tag), u32 the
	    host's tag index (its datum: the index with its salt, which a host
	    checks a client's whole) }
	u32 graph count, then each { u16 host tag index, u16 animation count,
	    animation count x u32 name hash }
	(version 2, a multiplayer map's: u16 netgame flag count, u16 0, then
	    each { 4 x u32 float bits: x, y, z, facing; u16 type, u16 team } -
	    the host's flags, whose places a client takes: Halo PC moved
	    teleporters, race checkpoints and hills a little; a client runs the
	    teleporters too)
	u32 check (FNV-1a of every byte before it)
*/

#define MIXED_COOP_DICTIONARY_MAGIC 0x3144584Du /* 'MXD1' little-endian */
#define MIXED_COOP_MAXIMUM_TAGS 0x8000
#define MIXED_COOP_MAXIMUM_BSPS 32
#define MIXED_COOP_MAXIMUM_GRAPHS 1024
#define MIXED_COOP_MAXIMUM_ANIMATIONS 2048
#define MIXED_COOP_MAXIMUM_NETGAME_FLAGS 512
#define MIXED_COOP_NETGAME_FLAG_BYTES 20
/* the farthest a flag's coordinate is taken (world units) */
#define MIXED_COOP_NETGAME_FLAG_LIMIT 100000.0f

struct mixed_coop_netgame_flag
{
	float position[3];
	float facing;
	uint16_t type;
	uint16_t team;
};

struct mixed_coop_dictionary_tag
{
	uint32_t path_hash;
	uint32_t digest;
	uint32_t datum;
};

/* a stream read: views into its bytes (which must outlive it) */
struct mixed_coop_dictionary
{
	uint8_t family;
	uint8_t level;
	uint32_t tag_count;
	struct mixed_coop_digests digests;
	uint16_t bsp_count;
	uint32_t bsp_digests[MIXED_COOP_MAXIMUM_BSPS][2];
	/* tag_count entries, unaligned: read with mixed_coop_dictionary_tag_get */
	uint8_t const *tags;
	uint32_t graph_count;
	/* the graphs' part, unaligned: walked with mixed_coop_dictionary_graph_next */
	uint8_t const *graphs;
	uint32_t graphs_bytes;
	/* (version 2) the host's netgame flags, unaligned: read with
	mixed_coop_dictionary_netgame_flag_get */
	uint16_t version;
	uint32_t netgame_flag_count;
	uint8_t const *netgame_flags;
};

/* the host's stream written into bytes (at most size): its length, or 0
when it does not fit */
struct mixed_coop_dictionary_writer
{
	uint8_t *bytes;
	uint32_t size;
	uint32_t length;
	int overflow;
};

void mixed_coop_writer_begin(struct mixed_coop_dictionary_writer *writer, void *bytes, uint32_t size);
void mixed_coop_writer_u8(struct mixed_coop_dictionary_writer *writer, uint8_t value);
void mixed_coop_writer_u16(struct mixed_coop_dictionary_writer *writer, uint16_t value);
void mixed_coop_writer_u32(struct mixed_coop_dictionary_writer *writer, uint32_t value);
/* the check appended: the stream's length, 0 when it overflowed */
uint32_t mixed_coop_writer_end(struct mixed_coop_dictionary_writer *writer);

/* reads a stream: 1 and the views, or 0 and why (at most why_size bytes) */
int mixed_coop_dictionary_read(uint8_t const *bytes, uint32_t length, struct mixed_coop_dictionary *dictionary,
	char *why, int why_size);
void mixed_coop_dictionary_tag_get(struct mixed_coop_dictionary const *dictionary, uint32_t index,
	struct mixed_coop_dictionary_tag *tag);
/* the next graph from *offset (0 first): 1 and its host tag index, its
animations' count and their name hashes (unaligned, count x u32), or 0 at
the end */
int mixed_coop_dictionary_graph_next(struct mixed_coop_dictionary const *dictionary, uint32_t *offset,
	uint16_t *host_tag_index, uint16_t *animation_count, uint8_t const **name_hashes);
/* the host's netgame flag `index` (0: past the count) */
int mixed_coop_dictionary_netgame_flag_get(struct mixed_coop_dictionary const *dictionary, uint32_t index,
	struct mixed_coop_netgame_flag *flag);
uint32_t mixed_coop_read_u32(uint8_t const *bytes);

/* ---------- hashes */

/* FNV-1a over bytes, carried on from hash (MIXED_COOP_FNV_BASIS to begin) */
#define MIXED_COOP_FNV_BASIS 0x811C9DC5u
uint32_t mixed_coop_fnv(uint32_t hash, void const *bytes, uint32_t size);
/* a tag's path hash: its group (models as one: 'mod2' as 'mode') and its
path in lower case, '/' as '\' */
uint32_t mixed_coop_path_hash(uint32_t group_tag, char const *path);
/* an animation's name hash (lower case) */
uint32_t mixed_coop_name_hash(char const *name, int length);

/* the family's name for the log and the player ("Xbox", "MCC") */
char const *mixed_coop_family_name(int family);

#endif
