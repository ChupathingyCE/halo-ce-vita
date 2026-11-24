/*
MIXED_COOP_DELTA.H

Mixed play with a ChupathingyCE host (the crossplay wire): the few parts of
ChupathingyCE's Delta Peer (its docs/delta/peer.md) that mixed play needs,
spoken by a client of this build that plays the host's level from MCC's
files alone, and by no other.

A ChupathingyCE host with network.mixed_maps on says it plays mixed over
Delta Peer, not over the game's own messages (the crossplay wire is OpenCE's
and stays exactly as it is). So, on the crossplay wire, a joiner whose only
copy of the host's level is MCC's says Delta's HELLO to the host's port 5160,
offering the mixed_maps capability (and platform), and waits as it does for a
Vita host's hello (mixed_coop.c, 8 s). A host that answers with MIXED_HELLO
is played with exactly as a Vita host of mixed play is: the same lobby
answer, the same dictionary and layout (in Delta's pieces, asked for again
until whole), the same checks and translation. A host that does not - an
OpenCE host, a ChupathingyCE host without it or with the setting off -
refuses the game before the map loads, as before.

The messages (MIXED_HELLO 42, MIXED_ANSWER 43, MIXED_REQUEST 44, MIXED_PIECE
45, MIXED_BSP 46; capability bit 27) carry mixed_coop_protocol.h's
structures, little-endian. Everything read here is another machine's: every
size is checked before anything is taken, and only the host's address, port
5160 and this session's datagrams are read.
*/

#ifndef HALO_MIXED_COOP_DELTA_H
#define HALO_MIXED_COOP_DELTA_H

#include "mixed_coop_protocol.h"

/* Delta Peer's numbers this client uses */
#define MIXED_COOP_DELTA_PORT 5160
#define MIXED_COOP_DELTA_CAPABILITY_PLATFORM 0
#define MIXED_COOP_DELTA_CAPABILITY_MIXED_MAPS 27
/* the pieces of a stream (MIXED_PIECE's bytes), and how many a want names */
#define MIXED_COOP_DELTA_PIECE_BYTES 1024
#define MIXED_COOP_DELTA_DICTIONARY_WANT_BITS 256
#define MIXED_COOP_DELTA_LAYOUT_WANT_BITS 64

enum
{
	_mixed_coop_delta_stream_dictionary = 1,
	_mixed_coop_delta_stream_layout = 2,
};

/* a stream as its pieces come (any order, each once) */
struct mixed_coop_delta_pieces
{
	uint32_t session;
	uint8_t stream;
	uint8_t level;
	uint32_t total_bytes;
	uint16_t count;
	uint16_t received;
	uint32_t maximum_bytes;
	uint8_t have[MIXED_COOP_DELTA_DICTIONARY_WANT_BITS / 8];
	uint8_t *bytes;
};

/* ---------- the wire (plain, for the fuzz target) */

/* a datagram read: 1 and its type, session and payload (in data), or 0 */
int mixed_coop_delta_read_header(uint8_t const *data, int size, int *type, uint32_t *session, uint8_t const **payload,
	int *payload_size);
/* WELCOME: 1 and the host's agreed capabilities, or 0 */
int mixed_coop_delta_read_welcome(uint8_t const *payload, int size, uint32_t *agreed);
/* MIXED_HELLO: 1 and the hello as a lobby message (checked as the Bungie
message's: mixed_coop_lobby_valid), or 0 */
int mixed_coop_delta_read_hello(uint8_t const *payload, int size, struct mixed_coop_lobby_message *hello);
/* MIXED_BSP: 1 and the digest, or 0 */
int mixed_coop_delta_read_bsp(uint8_t const *payload, int size, struct mixed_coop_bsp_digest *bsp);
/* MIXED_PIECE taken into pieces (its stream, session and level must be
pieces'): refused, taken, or the stream whole (_mixed_coop_fragment_*) */
int mixed_coop_delta_take_piece(struct mixed_coop_delta_pieces *pieces, uint8_t const *payload, int size);
void mixed_coop_delta_pieces_begin(struct mixed_coop_delta_pieces *pieces, uint8_t stream, uint32_t session, uint8_t level,
	uint8_t *bytes, uint32_t maximum_bytes);
/* the pieces still wanted, as a want's first piece and bits (bit_count bits) */
void mixed_coop_delta_pieces_wanted(struct mixed_coop_delta_pieces const *pieces, uint16_t *first, uint8_t *bits,
	int bit_count);

/* ---------- the client (mixed_coop.c's) */

/* starts the handshake with the host at ipv4 (host byte order): this
machine's machine index in the game and the network version it plays it at */
void mixed_coop_delta_begin(unsigned long ipv4, int machine_index, unsigned int legacy_version);
/* each frame: HELLO again until answered (4 s), the host's datagrams read and
handed to mixed_coop.c (mixed_coop_delta_heard_*) */
void mixed_coop_delta_frame(void);
/* BYE, the socket closed */
void mixed_coop_delta_end(void);
/* whether the handshake is under way or done, and whether the host agreed
to mixed_maps */
int mixed_coop_delta_active(void);
int mixed_coop_delta_agreed(void);
/* whether the host answered that it plays no mixed game, or never answered */
int mixed_coop_delta_said_no(void);
/* the messages to the host (1 sent) */
int mixed_coop_delta_send_answer(struct mixed_coop_lobby_message const *answer, uint16_t want_first,
	uint8_t const *want_bits);
int mixed_coop_delta_send_request(struct mixed_coop_request const *request, uint16_t want_first, uint8_t const *want_bits);

/* (mixed_coop.c's) what the host said */
void mixed_coop_delta_heard_hello(struct mixed_coop_lobby_message const *hello);
void mixed_coop_delta_heard_piece(uint8_t const *payload, int size);
void mixed_coop_delta_heard_bsp(struct mixed_coop_bsp_digest const *bsp);

#endif
