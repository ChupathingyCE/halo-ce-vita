/*
NET_FUZZ_MIXED_COOP_DELTA.C

A fuzz target for mixed play over a ChupathingyCE host's Delta Peer
(port/linux/game/mixed_coop_delta.c, its wire alone): what such a host sends
this machine, as it reads it.

An input's first byte chooses what the rest is:
  0  a datagram: its header read, then its payload as WELCOME, MIXED_HELLO
     and MIXED_BSP
  1  pieces of the host's dictionary: each a MIXED_PIECE payload of the
     piece's 20-byte head and as many bytes of data as the input has left (at
     most 1024), taken in order into one stream; the pieces still wanted
     worked out after each
  2  the same for a layout (its 64 KB)
Whatever the input:
- a datagram read has its payload within it, of the size its header says;
- a hello read is a lobby message mixed_coop_lobby_valid takes, its command
  the hello's and its session not 0; a BSP's index under 32;
- a stream never takes more bytes than its kind may have, nor a piece of
  another stream, session or level; the pieces it has are within its count;
  a whole stream has received as many pieces as its count, and wants none.
net_fuzz_checks: a three-piece stream taken in any order is whole and the
same bytes; a piece again changes nothing; a piece of the wrong length, past
the count, of another session or stream, or whose count is not the stream's
is refused; a hello with reserved bits or no family is refused.
Built as the platform layer's tests are, with AddressSanitizer and UBSan, by
run_net_fuzz_test.sh.
*/

#include "../../linux/game/mixed_coop_delta.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t dictionary_bytes[MIXED_COOP_DICTIONARY_MAXIMUM_BYTES];
static uint8_t layout_bytes[MIXED_COOP_LAYOUT_MAXIMUM_BYTES];

static void datagram(const uint8_t *data, int size)
{
	const uint8_t *payload;
	int type, payload_size;
	uint32_t session, agreed;
	struct mixed_coop_lobby_message hello;
	struct mixed_coop_bsp_digest bsp;

	if (!mixed_coop_delta_read_header(data, size, &type, &session, &payload, &payload_size))
		return;
	if (payload < data || payload_size < 0 || payload + payload_size != data + size)
		abort();
	mixed_coop_delta_read_welcome(payload, payload_size, &agreed);
	if (mixed_coop_delta_read_hello(payload, payload_size, &hello) &&
		(hello.command != _mixed_coop_lobby_hello || !hello.session || !mixed_coop_lobby_valid(&hello)))
	{
		abort();
	}
	if (mixed_coop_delta_read_bsp(payload, payload_size, &bsp) && (bsp.bsp_index >= MIXED_COOP_MAXIMUM_BSPS || !bsp.session))
		abort();
}

static void stream(uint8_t kind, const uint8_t *data, size_t size)
{
	static struct mixed_coop_delta_pieces pieces;
	uint8_t bits[MIXED_COOP_DELTA_DICTIONARY_WANT_BITS / 8];
	uint16_t first;
	uint32_t session;
	size_t at = 0;

	if (size < 6)
		return;
	session = (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
	mixed_coop_delta_pieces_begin(&pieces, kind, session, data[4], kind == _mixed_coop_delta_stream_dictionary ?
		dictionary_bytes : layout_bytes, kind == _mixed_coop_delta_stream_dictionary ? MIXED_COOP_DICTIONARY_MAXIMUM_BYTES :
		MIXED_COOP_LAYOUT_MAXIMUM_BYTES);
	at = 5;
	while (at + 20 <= size)
	{
		int piece = (int)(size - at < 20 + 1024 ? size - at : 20 + 1024);
		int result = mixed_coop_delta_take_piece(&pieces, data + at, piece);
		int index;

		if (pieces.total_bytes > pieces.maximum_bytes || pieces.received > pieces.count ||
			pieces.count > MIXED_COOP_DELTA_DICTIONARY_WANT_BITS)
		{
			abort();
		}
		for (index = pieces.count; index < MIXED_COOP_DELTA_DICTIONARY_WANT_BITS && pieces.received; index++)
		{
			if (pieces.have[index / 8] & (1u << (index % 8)))
				abort();
		}
		mixed_coop_delta_pieces_wanted(&pieces, &first, bits, MIXED_COOP_DELTA_DICTIONARY_WANT_BITS);
		if (result == _mixed_coop_fragment_complete)
		{
			if (pieces.received != pieces.count)
				abort();
			for (index = 0; index < (int)sizeof(bits); index++)
				if (bits[index])
					abort();
		}
		at += (size_t)piece;
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < 1)
		return 0;
	switch (data[0] % 3)
	{
	case 0:
		datagram(data + 1, (int)(size - 1 > 1200 ? 1200 : size - 1));
		break;
	case 1:
		stream(_mixed_coop_delta_stream_dictionary, data + 1, size - 1);
		break;
	default:
		stream(_mixed_coop_delta_stream_layout, data + 1, size - 1);
		break;
	}
	return 0;
}

/* a piece's 20-byte head and its bytes, into out: its length */
static int piece_make(uint8_t *out, uint8_t stream_kind, uint8_t level, uint32_t session, uint32_t total, uint16_t index,
	uint16_t count, uint16_t bytes, uint8_t fill)
{
	memset(out, 0, 20);
	out[0] = stream_kind;
	out[1] = level;
	out[4] = (uint8_t)session; out[5] = (uint8_t)(session >> 8); out[6] = (uint8_t)(session >> 16); out[7] = (uint8_t)(session >> 24);
	out[8] = (uint8_t)total; out[9] = (uint8_t)(total >> 8); out[10] = (uint8_t)(total >> 16); out[11] = (uint8_t)(total >> 24);
	out[12] = (uint8_t)index; out[13] = (uint8_t)(index >> 8);
	out[14] = (uint8_t)count; out[15] = (uint8_t)(count >> 8);
	out[16] = (uint8_t)bytes; out[17] = (uint8_t)(bytes >> 8);
	memset(out + 20, fill, bytes);
	return 20 + bytes;
}

int net_fuzz_checks(void)
{
	static uint8_t piece[20 + 1024];
	struct mixed_coop_delta_pieces pieces;
	struct mixed_coop_lobby_message hello;
	uint8_t bits[MIXED_COOP_DELTA_DICTIONARY_WANT_BITS / 8];
	uint8_t hello_payload[16] = { 1, 0, 2, 0, 1, 11, 0, 0, 0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0 };
	uint16_t first;
	int failures = 0, size, order[3] = { 2, 0, 1 }, step, index;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "net_fuzz_checks: %s (line %d)\n", #condition, __LINE__); \
	failures++; } } while (0)
	/* a stream of 2,500 bytes in three pieces, any order: whole, the same bytes */
	mixed_coop_delta_pieces_begin(&pieces, 1, 77, 4, dictionary_bytes, MIXED_COOP_DICTIONARY_MAXIMUM_BYTES);
	for (step = 0; step < 3; step++)
	{
		index = order[step];
		size = piece_make(piece, 1, 4, 77, 2500, (uint16_t)index, 3, index == 2 ? 452 : 1024, (uint8_t)(0x10 + index));
		CHECK(mixed_coop_delta_take_piece(&pieces, piece, size) == (step == 2 ? _mixed_coop_fragment_complete :
			_mixed_coop_fragment_taken));
		mixed_coop_delta_pieces_wanted(&pieces, &first, bits, MIXED_COOP_DELTA_DICTIONARY_WANT_BITS);
		CHECK(first == 0 && ((bits[0] >> index) & 1) == 0);
	}
	CHECK(pieces.total_bytes == 2500 && dictionary_bytes[0] == 0x10 && dictionary_bytes[1024] == 0x11 &&
		dictionary_bytes[2048] == 0x12 && dictionary_bytes[2499] == 0x12);
	mixed_coop_delta_pieces_wanted(&pieces, &first, bits, MIXED_COOP_DELTA_DICTIONARY_WANT_BITS);
	CHECK(!bits[0]);
	/* (again: nothing new) */
	CHECK(mixed_coop_delta_take_piece(&pieces, piece, size) == _mixed_coop_fragment_complete && pieces.received == 3);
	/* refused: the wrong length, past the count, another session, stream or level, a count not the stream's, past
	the stream's most */
	mixed_coop_delta_pieces_begin(&pieces, 1, 77, 4, dictionary_bytes, MIXED_COOP_DICTIONARY_MAXIMUM_BYTES);
	size = piece_make(piece, 1, 4, 77, 2500, 0, 3, 1000, 0);
	CHECK(mixed_coop_delta_take_piece(&pieces, piece, size) == _mixed_coop_fragment_refused);
	size = piece_make(piece, 1, 4, 77, 2500, 3, 3, 452, 0);
	CHECK(mixed_coop_delta_take_piece(&pieces, piece, size) == _mixed_coop_fragment_refused);
	size = piece_make(piece, 1, 4, 78, 2500, 0, 3, 1024, 0);
	CHECK(mixed_coop_delta_take_piece(&pieces, piece, size) == _mixed_coop_fragment_refused);
	size = piece_make(piece, 2, 4, 77, 2500, 0, 3, 1024, 0);
	CHECK(mixed_coop_delta_take_piece(&pieces, piece, size) == _mixed_coop_fragment_refused);
	size = piece_make(piece, 1, 5, 77, 2500, 0, 3, 1024, 0);
	CHECK(mixed_coop_delta_take_piece(&pieces, piece, size) == _mixed_coop_fragment_refused);
	size = piece_make(piece, 1, 4, 77, 2500, 0, 4, 1024, 0);
	CHECK(mixed_coop_delta_take_piece(&pieces, piece, size) == _mixed_coop_fragment_refused);
	size = piece_make(piece, 1, 4, 77, MIXED_COOP_DICTIONARY_MAXIMUM_BYTES + 1, 0, 257, 1024, 0);
	CHECK(mixed_coop_delta_take_piece(&pieces, piece, size) == _mixed_coop_fragment_refused);
	/* (cut short: its bytes not all there) */
	size = piece_make(piece, 1, 4, 77, 2500, 0, 3, 1024, 0);
	CHECK(mixed_coop_delta_take_piece(&pieces, piece, size - 1) == _mixed_coop_fragment_refused);
	CHECK(pieces.received == 0);
	/* a hello: taken; with reserved bits, no family, or a session of 0: refused */
	CHECK(mixed_coop_delta_read_hello(hello_payload, 16, &hello) && hello.level == 11 && hello.session == 0x12345678);
	hello_payload[14] = 1;
	CHECK(!mixed_coop_delta_read_hello(hello_payload, 16, &hello));
	hello_payload[14] = 0;
	hello_payload[4] = 0;
	CHECK(!mixed_coop_delta_read_hello(hello_payload, 16, &hello));
	hello_payload[4] = 1;
	memset(hello_payload + 8, 0, 4);
	CHECK(!mixed_coop_delta_read_hello(hello_payload, 16, &hello));
	CHECK(!mixed_coop_delta_read_hello(hello_payload, 15, &hello));
#undef CHECK
	return failures;
}
