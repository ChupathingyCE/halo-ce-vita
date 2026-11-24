/*
MIXED_COOP_DELTA.C

Mixed play with a ChupathingyCE host over its Delta Peer (mixed_coop_delta.h):
the wire, then the client. Written from ChupathingyCE's specification
(docs/delta/peer.md); none of its code.

With MIXED_COOP_DELTA_WIRE_ONLY (the fuzz target) only the wire is built.
*/

#include "mixed_coop_delta.h"

#include <string.h>

/* ---------- the wire */

enum
{
	DELTA_HEADER_BYTES = 12,
	DELTA_MAXIMUM_DATAGRAM = 1200,
	DELTA_MAJOR = 1,
	DELTA_HELLO = 1,
	DELTA_WELCOME = 2,
	DELTA_LEGACY = 3,
	DELTA_BYE = 5,
	DELTA_MIXED_HELLO = 42,
	DELTA_MIXED_ANSWER = 43,
	DELTA_MIXED_REQUEST = 44,
	DELTA_MIXED_PIECE = 45,
	DELTA_MIXED_BSP = 46,
	DELTA_WELCOME_BYTES = 24,
	DELTA_MIXED_LOBBY_BYTES = 16,
	DELTA_MIXED_REQUEST_BYTES = 28,
	DELTA_MIXED_PIECE_HEAD_BYTES = 20,
	DELTA_MIXED_BSP_BYTES = 16,
	/* (a build string's longest) */
	DELTA_BUILD_BYTES = 31,
};

static uint16_t delta_u16(uint8_t const *bytes)
{
	return (uint16_t)(bytes[0] | bytes[1] << 8);
}

static uint32_t delta_u32(uint8_t const *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static void delta_put16(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static void delta_put32(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

static void delta_header(uint8_t *data, int type, int length, uint32_t session)
{
	data[0] = 'D';
	data[1] = 'P';
	data[2] = DELTA_MAJOR;
	data[3] = (uint8_t)type;
	delta_put16(data + 4, (uint32_t)length);
	delta_put16(data + 6, 0);
	delta_put32(data + 8, session);
}

int mixed_coop_delta_read_header(uint8_t const *data, int size, int *type, uint32_t *session, uint8_t const **payload,
	int *payload_size)
{
	if (!data || size < DELTA_HEADER_BYTES || size > DELTA_MAXIMUM_DATAGRAM || data[0] != 'D' || data[1] != 'P')
		return 0;
	/* (the payload's length, exactly what came) */
	if (delta_u16(data + 4) != size - DELTA_HEADER_BYTES)
		return 0;
	*type = data[3];
	*session = delta_u32(data + 8);
	*payload = data + DELTA_HEADER_BYTES;
	*payload_size = size - DELTA_HEADER_BYTES;
	/* (another major says LEGACY alone; its header is the same) */
	return data[2] == DELTA_MAJOR || data[3] == DELTA_LEGACY;
}

int mixed_coop_delta_read_welcome(uint8_t const *payload, int size, uint32_t *agreed)
{
	int length;

	if (!payload || size < DELTA_WELCOME_BYTES)
		return 0;
	length = payload[11];
	if (length > DELTA_BUILD_BYTES || size < DELTA_WELCOME_BYTES + length)
		return 0;
	*agreed = delta_u32(payload + 4);
	return 1;
}

static void delta_lobby_read(uint8_t const *payload, struct mixed_coop_lobby_message *message)
{
	memset(message, 0, sizeof(*message));
	message->command = delta_u16(payload);
	message->version = delta_u16(payload + 2);
	message->family = payload[4];
	message->level = payload[5];
	message->flags = delta_u16(payload + 6);
	message->session = delta_u32(payload + 8);
	message->reserved = delta_u32(payload + 12);
}

int mixed_coop_delta_read_hello(uint8_t const *payload, int size, struct mixed_coop_lobby_message *hello)
{
	if (!payload || size < DELTA_MIXED_LOBBY_BYTES)
		return 0;
	delta_lobby_read(payload, hello);
	return hello->command == _mixed_coop_lobby_hello && hello->session && mixed_coop_lobby_valid(hello);
}

int mixed_coop_delta_read_bsp(uint8_t const *payload, int size, struct mixed_coop_bsp_digest *bsp)
{
	if (!payload || size < DELTA_MIXED_BSP_BYTES)
		return 0;
	memset(bsp, 0, sizeof(*bsp));
	bsp->session = delta_u32(payload);
	bsp->bsp_index = delta_u16(payload + 4);
	bsp->pad = delta_u16(payload + 6);
	bsp->digest = delta_u32(payload + 8);
	bsp->digest_offset = delta_u32(payload + 12);
	return bsp->session && bsp->bsp_index < MIXED_COOP_MAXIMUM_BSPS && !bsp->pad;
}

void mixed_coop_delta_pieces_begin(struct mixed_coop_delta_pieces *pieces, uint8_t stream, uint32_t session, uint8_t level,
	uint8_t *bytes, uint32_t maximum_bytes)
{
	memset(pieces, 0, sizeof(*pieces));
	pieces->stream = stream;
	pieces->session = session;
	pieces->level = level;
	pieces->bytes = bytes;
	pieces->maximum_bytes = maximum_bytes;
}

int mixed_coop_delta_take_piece(struct mixed_coop_delta_pieces *pieces, uint8_t const *payload, int size)
{
	uint32_t session, total, offset, expected;
	uint16_t index, count, bytes;

	if (!pieces || !pieces->bytes || !payload || size < DELTA_MIXED_PIECE_HEAD_BYTES)
		return _mixed_coop_fragment_refused;
	session = delta_u32(payload + 4);
	total = delta_u32(payload + 8);
	index = delta_u16(payload + 12);
	count = delta_u16(payload + 14);
	bytes = delta_u16(payload + 16);
	if (payload[0] != pieces->stream || payload[1] != pieces->level || session != pieces->session || delta_u16(payload + 2) ||
		delta_u16(payload + 18))
	{
		return _mixed_coop_fragment_refused;
	}
	if (!total || total > pieces->maximum_bytes)
		return _mixed_coop_fragment_refused;
	expected = (total + MIXED_COOP_DELTA_PIECE_BYTES - 1) / MIXED_COOP_DELTA_PIECE_BYTES;
	if (count != expected || index >= count || count > MIXED_COOP_DELTA_DICTIONARY_WANT_BITS)
		return _mixed_coop_fragment_refused;
	/* (every piece full but the last, which has the rest; and all of it came) */
	offset = (uint32_t)index * MIXED_COOP_DELTA_PIECE_BYTES;
	if (bytes != (index + 1u < count ? (uint32_t)MIXED_COOP_DELTA_PIECE_BYTES : total - offset) ||
		size < DELTA_MIXED_PIECE_HEAD_BYTES + bytes)
	{
		return _mixed_coop_fragment_refused;
	}
	if (pieces->received == 0)
	{
		pieces->total_bytes = total;
		pieces->count = count;
	}
	else if (pieces->total_bytes != total || pieces->count != count)
	{
		return _mixed_coop_fragment_refused;
	}
	/* (a piece again: a want crossed it on the way; nothing new) */
	if (pieces->have[index / 8] & (1u << (index % 8)))
		return pieces->received == pieces->count ? _mixed_coop_fragment_complete : _mixed_coop_fragment_taken;
	memcpy(pieces->bytes + offset, payload + DELTA_MIXED_PIECE_HEAD_BYTES, bytes);
	pieces->have[index / 8] |= (uint8_t)(1u << (index % 8));
	pieces->received++;
	return pieces->received == pieces->count ? _mixed_coop_fragment_complete : _mixed_coop_fragment_taken;
}

void mixed_coop_delta_pieces_wanted(struct mixed_coop_delta_pieces const *pieces, uint16_t *first, uint8_t *bits,
	int bit_count)
{
	int bit;

	*first = 0;
	memset(bits, 0, (size_t)(bit_count / 8));
	for (bit = 0; bit < bit_count; bit++)
	{
		/* (before the first piece: every piece the stream can have) */
		if (pieces->received && bit >= pieces->count)
			break;
		if (!(pieces->have[bit / 8] & (1u << (bit % 8))))
			bits[bit / 8] |= (uint8_t)(1u << (bit % 8));
	}
}

#ifndef MIXED_COOP_DELTA_WIRE_ONLY

/* ---------- the client */

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "../../vita/include/vita_version.h"

#include <stdlib.h>

void platform_log(char const *format, ...);

/* the handshake: HELLO every HELLO_MILLISECONDS until WELCOME or LEGACY, at
most GIVE_UP_MILLISECONDS (then no Delta: mixed_coop.c's wait refuses the
game); datagrams read a frame, and taken from the host a second */
#define MIXED_COOP_DELTA_HELLO_MILLISECONDS 500
#define MIXED_COOP_DELTA_GIVE_UP_MILLISECONDS 4000
#define MIXED_COOP_DELTA_READS_A_FRAME 64
#define MIXED_COOP_DELTA_TAKEN_A_SECOND 200

enum
{
	_delta_client_off = 0,
	_delta_client_hello,
	_delta_client_agreed,
	_delta_client_legacy,
};

static struct
{
	int state;
	SOCKET socket;
	struct sockaddr_in host;
	uint32_t session;
	uint32_t agreed;
	int machine_index;
	unsigned int legacy_version;
	unsigned long started;
	unsigned long hello_time;
	unsigned long second_began;
	int taken_this_second;
	long dropped;
} mixed_coop_delta = { 0, INVALID_SOCKET };

static void mixed_coop_delta_send(uint8_t const *data, int size)
{
	if (mixed_coop_delta.socket == INVALID_SOCKET)
		return;
	sendto(mixed_coop_delta.socket, (char const *)data, size, 0, (struct sockaddr const *)&mixed_coop_delta.host,
		sizeof(mixed_coop_delta.host));
}

/* this machine's platform key (peer.md's Platform keys): its number in
ChupathingyCE's registry (11 a Vita, 1 Windows, 3 Linux), version 1, the
Vitas' sixteen players, its memory's log2 of megabytes */
static void mixed_coop_delta_key(uint8_t *key)
{
	memset(key, 0, 8);
#if defined(HALO_VITA) || defined(__vita__)
	key[0] = 11;
	key[6] = 9;
#elif defined(_WIN32)
	key[0] = 1;
	key[6] = 12;
#else
	key[0] = 3;
	key[6] = 12;
#endif
	key[1] = 1;
	key[3] = 16;
	key[4] = 16;
	key[5] = 16;
}

static void mixed_coop_delta_hello(unsigned long now)
{
	uint8_t data[DELTA_HEADER_BYTES + 20 + DELTA_BUILD_BYTES];
	uint8_t *payload = data + DELTA_HEADER_BYTES;
	char build[DELTA_BUILD_BYTES + 1];
	int length;

	snprintf(build, sizeof(build), "Halo CE Vita %s", HALO_VITA_VERSION);
	length = (int)strlen(build);
	memset(payload, 0, 20);
	delta_put32(payload, (1u << MIXED_COOP_DELTA_CAPABILITY_PLATFORM) | (1u << MIXED_COOP_DELTA_CAPABILITY_MIXED_MAPS));
	delta_put16(payload + 4, mixed_coop_delta.legacy_version);
	payload[6] = (uint8_t)mixed_coop_delta.machine_index;
	payload[7] = (uint8_t)length;
	/* (no signed legacy tables taken) */
	delta_put32(payload + 8, 0xFFFFFFFFu);
	mixed_coop_delta_key(payload + 12);
	memcpy(payload + 20, build, (size_t)length);
	delta_header(data, DELTA_HELLO, 20 + length, mixed_coop_delta.session);
	mixed_coop_delta_send(data, DELTA_HEADER_BYTES + 20 + length);
	mixed_coop_delta.hello_time = now ? now : 1;
}

void mixed_coop_delta_begin(unsigned long ipv4, int machine_index, unsigned int legacy_version)
{
	struct sockaddr_in local;
	u_long nonblocking = 1;

	mixed_coop_delta_end();
	if (machine_index < 0 || machine_index > 0xFF || !ipv4)
		return;
	mixed_coop_delta.socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (mixed_coop_delta.socket == INVALID_SOCKET)
	{
		platform_log("mixed play: Delta Peer: no socket");
		return;
	}
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	local.sin_port = 0;
	if (bind(mixed_coop_delta.socket, (struct sockaddr const *)&local, sizeof(local)) != 0 ||
		ioctlsocket(mixed_coop_delta.socket, FIONBIO, &nonblocking) != 0)
	{
		platform_log("mixed play: Delta Peer: the socket could not be bound");
		closesocket(mixed_coop_delta.socket);
		mixed_coop_delta.socket = INVALID_SOCKET;
		return;
	}
	memset(&mixed_coop_delta.host, 0, sizeof(mixed_coop_delta.host));
	mixed_coop_delta.host.sin_family = AF_INET;
	mixed_coop_delta.host.sin_addr.s_addr = htonl(ipv4);
	mixed_coop_delta.host.sin_port = htons(MIXED_COOP_DELTA_PORT);
	mixed_coop_delta.machine_index = machine_index;
	mixed_coop_delta.legacy_version = legacy_version;
	mixed_coop_delta.agreed = 0;
	mixed_coop_delta.dropped = 0;
	do
	{
		mixed_coop_delta.session = (uint32_t)rand() << 17 ^ (uint32_t)rand() << 3 ^ (uint32_t)system_milliseconds();
	} while (!mixed_coop_delta.session);
	mixed_coop_delta.state = _delta_client_hello;
	mixed_coop_delta.started = system_milliseconds();
	platform_log("mixed play: Delta Peer: HELLO to the host (machine %d, network version %u), offering mixed_maps",
		machine_index, legacy_version);
	mixed_coop_delta_hello(mixed_coop_delta.started);
}

void mixed_coop_delta_end(void)
{
	if (mixed_coop_delta.socket != INVALID_SOCKET)
	{
		if (mixed_coop_delta.state == _delta_client_agreed)
		{
			uint8_t data[DELTA_HEADER_BYTES];

			delta_header(data, DELTA_BYE, 0, mixed_coop_delta.session);
			mixed_coop_delta_send(data, DELTA_HEADER_BYTES);
		}
		closesocket(mixed_coop_delta.socket);
		mixed_coop_delta.socket = INVALID_SOCKET;
		platform_log("mixed play: Delta Peer: done (%ld datagrams dropped)", mixed_coop_delta.dropped);
	}
	mixed_coop_delta.state = _delta_client_off;
	mixed_coop_delta.agreed = 0;
}

int mixed_coop_delta_active(void)
{
	return mixed_coop_delta.state == _delta_client_hello || mixed_coop_delta.state == _delta_client_agreed;
}

int mixed_coop_delta_said_no(void)
{
	/* (LEGACY, BYE, no answer in time, or a WELCOME without mixed_maps) */
	return mixed_coop_delta.state == _delta_client_legacy ||
		(mixed_coop_delta.state == _delta_client_agreed && !mixed_coop_delta_agreed());
}

int mixed_coop_delta_agreed(void)
{
	return mixed_coop_delta.state == _delta_client_agreed &&
		(mixed_coop_delta.agreed & (1u << MIXED_COOP_DELTA_CAPABILITY_MIXED_MAPS)) != 0;
}

static void mixed_coop_delta_receive(uint8_t const *data, int size)
{
	uint8_t const *payload;
	int type, payload_size;
	uint32_t session;

	if (!mixed_coop_delta_read_header(data, size, &type, &session, &payload, &payload_size) ||
		session != mixed_coop_delta.session)
	{
		mixed_coop_delta.dropped++;
		return;
	}
	switch (type)
	{
	case DELTA_WELCOME:
	{
		uint32_t agreed;

		if (mixed_coop_delta.state != _delta_client_hello || !mixed_coop_delta_read_welcome(payload, payload_size, &agreed))
		{
			mixed_coop_delta.dropped++;
			return;
		}
		/* (only what this machine offered) */
		mixed_coop_delta.agreed = agreed &
			((1u << MIXED_COOP_DELTA_CAPABILITY_PLATFORM) | (1u << MIXED_COOP_DELTA_CAPABILITY_MIXED_MAPS));
		mixed_coop_delta.state = _delta_client_agreed;
		platform_log("mixed play: Delta Peer: WELCOME, the host %s mixed play",
			mixed_coop_delta_agreed() ? "agreed to" : "does not offer");
		return;
	}
	case DELTA_LEGACY:
		if (mixed_coop_delta.state == _delta_client_hello)
		{
			mixed_coop_delta.state = _delta_client_legacy;
			platform_log("mixed play: Delta Peer: the host answered LEGACY (another major)");
		}
		return;
	case DELTA_BYE:
		if (mixed_coop_delta.state == _delta_client_agreed)
		{
			mixed_coop_delta.state = _delta_client_legacy;
			platform_log("mixed play: Delta Peer: the host said BYE");
		}
		return;
	case DELTA_MIXED_HELLO:
	{
		struct mixed_coop_lobby_message hello;

		if (!mixed_coop_delta_agreed() || !mixed_coop_delta_read_hello(payload, payload_size, &hello))
		{
			mixed_coop_delta.dropped++;
			return;
		}
		mixed_coop_delta_heard_hello(&hello);
		return;
	}
	case DELTA_MIXED_PIECE:
		if (!mixed_coop_delta_agreed())
		{
			mixed_coop_delta.dropped++;
			return;
		}
		mixed_coop_delta_heard_piece(payload, payload_size);
		return;
	case DELTA_MIXED_BSP:
	{
		struct mixed_coop_bsp_digest bsp;

		if (!mixed_coop_delta_agreed() || !mixed_coop_delta_read_bsp(payload, payload_size, &bsp))
		{
			mixed_coop_delta.dropped++;
			return;
		}
		mixed_coop_delta_heard_bsp(&bsp);
		return;
	}
	default:
		/* (ROSTER and the rest: not this client's) */
		return;
	}
}

void mixed_coop_delta_frame(void)
{
	unsigned long now = system_milliseconds();
	short reads;

	if (mixed_coop_delta.socket == INVALID_SOCKET)
		return;
	if (mixed_coop_delta.state == _delta_client_hello)
	{
		if (now - mixed_coop_delta.started >= MIXED_COOP_DELTA_GIVE_UP_MILLISECONDS)
		{
			mixed_coop_delta.state = _delta_client_legacy;
			platform_log("mixed play: Delta Peer: no answer from the host in %d s", MIXED_COOP_DELTA_GIVE_UP_MILLISECONDS / 1000);
		}
		else if (now - mixed_coop_delta.hello_time >= MIXED_COOP_DELTA_HELLO_MILLISECONDS)
		{
			mixed_coop_delta_hello(now);
		}
	}
	if (now - mixed_coop_delta.second_began >= 1000)
	{
		mixed_coop_delta.second_began = now;
		mixed_coop_delta.taken_this_second = 0;
	}
	for (reads = 0; reads < MIXED_COOP_DELTA_READS_A_FRAME; reads++)
	{
		static uint8_t data[DELTA_MAXIMUM_DATAGRAM + 1];
		struct sockaddr_in from;
		int from_length = sizeof(from);
		int size = recvfrom(mixed_coop_delta.socket, (char *)data, sizeof(data), 0, (struct sockaddr *)&from, &from_length);

		if (size < 0)
			break;
		/* (the host's address and port alone, at most so many a second) */
		if (from_length < (int)sizeof(from) || from.sin_addr.s_addr != mixed_coop_delta.host.sin_addr.s_addr ||
			from.sin_port != mixed_coop_delta.host.sin_port ||
			mixed_coop_delta.taken_this_second >= MIXED_COOP_DELTA_TAKEN_A_SECOND)
		{
			mixed_coop_delta.dropped++;
			continue;
		}
		mixed_coop_delta.taken_this_second++;
		mixed_coop_delta_receive(data, size);
		if (mixed_coop_delta.socket == INVALID_SOCKET)
			break;
	}
}

static void delta_lobby_write(uint8_t *payload, struct mixed_coop_lobby_message const *message)
{
	delta_put16(payload, message->command);
	delta_put16(payload + 2, message->version);
	payload[4] = message->family;
	payload[5] = message->level;
	delta_put16(payload + 6, 0);
	delta_put32(payload + 8, message->session);
	delta_put32(payload + 12, 0);
}

int mixed_coop_delta_send_answer(struct mixed_coop_lobby_message const *answer, uint16_t want_first,
	uint8_t const *want_bits)
{
	uint8_t data[DELTA_HEADER_BYTES + DELTA_MIXED_LOBBY_BYTES + 4 + MIXED_COOP_DELTA_LAYOUT_WANT_BITS / 8];
	uint8_t *payload = data + DELTA_HEADER_BYTES;
	int length = DELTA_MIXED_LOBBY_BYTES;

	if (!mixed_coop_delta_agreed())
		return 0;
	memset(payload, 0, sizeof(data) - DELTA_HEADER_BYTES);
	delta_lobby_write(payload, answer);
	if (answer->command == _mixed_coop_lobby_layout_request)
	{
		delta_put16(payload + length, want_first);
		if (want_bits)
			memcpy(payload + length + 4, want_bits, MIXED_COOP_DELTA_LAYOUT_WANT_BITS / 8);
		else
			memset(payload + length + 4, 0xFF, MIXED_COOP_DELTA_LAYOUT_WANT_BITS / 8);
		length += 4 + MIXED_COOP_DELTA_LAYOUT_WANT_BITS / 8;
	}
	delta_header(data, DELTA_MIXED_ANSWER, length, mixed_coop_delta.session);
	mixed_coop_delta_send(data, DELTA_HEADER_BYTES + length);
	return 1;
}

int mixed_coop_delta_send_request(struct mixed_coop_request const *request, uint16_t want_first, uint8_t const *want_bits)
{
	uint8_t data[DELTA_HEADER_BYTES + DELTA_MIXED_REQUEST_BYTES + 4 + MIXED_COOP_DELTA_DICTIONARY_WANT_BITS / 8];
	uint8_t *payload = data + DELTA_HEADER_BYTES;
	int length = DELTA_MIXED_REQUEST_BYTES + 4 + MIXED_COOP_DELTA_DICTIONARY_WANT_BITS / 8;

	if (!mixed_coop_delta_agreed())
		return 0;
	memset(payload, 0, (size_t)length);
	delta_put16(payload, request->version);
	payload[2] = request->family;
	payload[3] = request->level;
	delta_put32(payload + 4, request->session);
	delta_put32(payload + 8, request->digests.layout);
	delta_put32(payload + 12, request->digests.gameplay);
	delta_put32(payload + 16, request->digests.host_only);
	delta_put32(payload + 20, request->digests.tag_count);
	delta_put32(payload + 24, request->digests.layout_offset);
	delta_put16(payload + 28, want_first);
	if (want_bits)
		memcpy(payload + 32, want_bits, MIXED_COOP_DELTA_DICTIONARY_WANT_BITS / 8);
	else
		memset(payload + 32, 0xFF, MIXED_COOP_DELTA_DICTIONARY_WANT_BITS / 8);
	delta_header(data, DELTA_MIXED_REQUEST, length, mixed_coop_delta.session);
	mixed_coop_delta_send(data, DELTA_HEADER_BYTES + length);
	return 1;
}

#endif
