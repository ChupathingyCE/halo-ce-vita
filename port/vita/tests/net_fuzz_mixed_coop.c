/*
NET_FUZZ_MIXED_COOP.C

A fuzz target for mixed co-op's messages (port/linux/game/
mixed_coop_protocol.c): what another machine sends in a mixed co-op game -
the lobby's hello and answer, a client's request, the host's dictionary in
pieces and a structure BSP's digest - as this machine takes them.

An input's first byte chooses what the rest is:
  0  a lobby message (16 bytes, zeros past the input's end)
  1  a request (28 bytes)
  2  pieces of a dictionary: each a fragment's 16-byte header and as many
     bytes of data as the input has left (at most its bytes), taken in
     order into one reassembly; a stream completed is read
  3  a dictionary stream as it is (its check left as the input has it)
  4  a dictionary stream with its check made right (so the reader goes past
     the check into every count and length)
Whatever the input:
- a lobby message or request taken has a command, version, family and
  level there are;
- a reassembly never takes more bytes than a stream may have, nor a piece
  twice, and a stream it completes has exactly the bytes its pieces said;
- a dictionary read has its tags and graphs within its bytes: every tag
  read, every graph walked (as many as it says, each animation's hash within
  the bytes), never past its end.
net_fuzz_checks: a dictionary written is read back as written; one cut
short, one with a byte changed (its check) or a count past its limit is
refused; a stream sent in pieces in any order is the stream.
Built as net_fuzz_map_share.c is, by run_net_fuzz_test.sh.
*/

#include "../../linux/game/mixed_coop_protocol.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct mixed_coop_reassembly reassembly;
static uint8_t stream[MIXED_COOP_DICTIONARY_MAXIMUM_BYTES];

static void check_dictionary(uint8_t const *bytes, uint32_t length)
{
	struct mixed_coop_dictionary dictionary;
	char why[96];
	uint32_t index, offset = 0, graphs = 0;
	uint16_t host_tag, count;
	uint8_t const *hashes;

	if (!mixed_coop_dictionary_read(bytes, length, &dictionary, why, (int)sizeof(why)))
	{
		if (!why[0] || strlen(why) >= sizeof(why))
			abort();
		return;
	}
	if (dictionary.tag_count == 0 || dictionary.tag_count > MIXED_COOP_MAXIMUM_TAGS ||
		dictionary.bsp_count > MIXED_COOP_MAXIMUM_BSPS || dictionary.graph_count > MIXED_COOP_MAXIMUM_GRAPHS ||
		dictionary.level >= MIXED_COOP_LEVELS ||
		(dictionary.family != _mixed_coop_family_xbox && dictionary.family != _mixed_coop_family_mcc))
	{
		abort();
	}
	if (dictionary.tags < bytes || dictionary.tags + (size_t)dictionary.tag_count * 12 > bytes + length)
		abort();
	for (index = 0; index < dictionary.tag_count; index++)
	{
		struct mixed_coop_dictionary_tag tag;
		volatile uint32_t sink;

		mixed_coop_dictionary_tag_get(&dictionary, index, &tag);
		sink = tag.path_hash ^ tag.digest ^ tag.datum;
		(void)sink;
	}
	while (mixed_coop_dictionary_graph_next(&dictionary, &offset, &host_tag, &count, &hashes))
	{
		uint16_t animation;

		if (host_tag >= dictionary.tag_count || count > MIXED_COOP_MAXIMUM_ANIMATIONS)
			abort();
		if (hashes < bytes || hashes + (size_t)count * 4 > bytes + length)
			abort();
		for (animation = 0; animation < count; animation++)
		{
			volatile uint32_t sink = mixed_coop_read_u32(hashes + animation * 4);

			(void)sink;
		}
		if (++graphs > dictionary.graph_count)
			abort();
	}
	if (graphs != dictionary.graph_count)
		abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	uint8_t mode;

	if (size < 1)
		return 0;
	mode = data[0];
	data++;
	size--;
	switch (mode % 5)
	{
	case 0:
	{
		struct mixed_coop_lobby_message message;

		memset(&message, 0, sizeof(message));
		memcpy(&message, data, size < sizeof(message) ? size : sizeof(message));
		if (mixed_coop_lobby_valid(&message) &&
			(message.command == 0 || message.command >= NUMBER_OF_MIXED_COOP_LOBBY_COMMANDS ||
				message.version != MIXED_COOP_VERSION || message.family >= NUMBER_OF_MIXED_COOP_FAMILIES ||
				message.level >= MIXED_COOP_LEVELS || message.reserved || message.flags))
		{
			abort();
		}
		break;
	}
	case 1:
	{
		struct mixed_coop_request request;

		memset(&request, 0, sizeof(request));
		memcpy(&request, data, size < sizeof(request) ? size : sizeof(request));
		if (mixed_coop_request_valid(&request) &&
			(request.version != MIXED_COOP_VERSION || request.level >= MIXED_COOP_LEVELS ||
				(request.family != _mixed_coop_family_xbox && request.family != _mixed_coop_family_mcc)))
		{
			abort();
		}
		break;
	}
	case 2:
	{
		static struct mixed_coop_fragment fragment;
		size_t at = 0;
		uint32_t session = 0;

		if (size >= 4)
			memcpy(&session, data, 4);
		mixed_coop_reassembly_begin(&reassembly, session);
		while (at + 16 <= size)
		{
			size_t take;
			int result;

			memset(&fragment, 0, sizeof(fragment));
			memcpy(&fragment, data + at, 16);
			at += 16;
			take = size - at;
			if (take > fragment.bytes)
				take = fragment.bytes;
			if (take > MIXED_COOP_FRAGMENT_BYTES)
				take = MIXED_COOP_FRAGMENT_BYTES;
			memcpy(fragment.data, data + at, take);
			at += take;
			result = mixed_coop_reassembly_take(&reassembly, &fragment);
			if (reassembly.received > reassembly.count || reassembly.total_bytes > MIXED_COOP_DICTIONARY_MAXIMUM_BYTES)
				abort();
			if (result == _mixed_coop_fragment_complete)
			{
				if (reassembly.received != reassembly.count || !reassembly.total_bytes)
					abort();
				check_dictionary(reassembly.bytes, reassembly.total_bytes);
				break;
			}
		}
		break;
	}
	case 3:
		if (size <= sizeof(stream))
		{
			memcpy(stream, data, size);
			check_dictionary(stream, (uint32_t)size);
		}
		break;
	case 4:
		if (size >= 4 && size <= sizeof(stream))
		{
			uint32_t check;

			memcpy(stream, data, size);
			check = mixed_coop_fnv(MIXED_COOP_FNV_BASIS, stream, (uint32_t)size - 4);
			stream[size - 4] = (uint8_t)check;
			stream[size - 3] = (uint8_t)(check >> 8);
			stream[size - 2] = (uint8_t)(check >> 16);
			stream[size - 1] = (uint8_t)(check >> 24);
			check_dictionary(stream, (uint32_t)size);
		}
		break;
	}
	return 0;
}

/* a small dictionary: 3 tags, 2 BSPs, 2 graphs */
static uint32_t write_sample(uint8_t *bytes, uint32_t size, uint32_t tag_count, uint32_t graph_animations)
{
	struct mixed_coop_dictionary_writer writer;
	uint32_t index;

	mixed_coop_writer_begin(&writer, bytes, size);
	mixed_coop_writer_u32(&writer, MIXED_COOP_DICTIONARY_MAGIC);
	mixed_coop_writer_u16(&writer, MIXED_COOP_VERSION);
	mixed_coop_writer_u8(&writer, _mixed_coop_family_mcc);
	mixed_coop_writer_u8(&writer, 1);
	mixed_coop_writer_u32(&writer, tag_count);
	mixed_coop_writer_u32(&writer, 0x11111111u);
	mixed_coop_writer_u32(&writer, 0x22222222u);
	mixed_coop_writer_u32(&writer, 0x33333333u);
	mixed_coop_writer_u32(&writer, 0x44444444u);
	mixed_coop_writer_u16(&writer, 2);
	mixed_coop_writer_u16(&writer, 0);
	for (index = 0; index < 4; index++)
		mixed_coop_writer_u32(&writer, 0xB0000000u + index);
	for (index = 0; index < tag_count; index++)
	{
		mixed_coop_writer_u32(&writer, mixed_coop_path_hash(0x77656170u /* 'weap' */, index ? "weapons\\pistol\\pistol" : "a"));
		mixed_coop_writer_u32(&writer, index * 7u);
		mixed_coop_writer_u32(&writer, 0xE1740000u + (index << 16) + index);
	}
	mixed_coop_writer_u32(&writer, 2);
	for (index = 0; index < 2; index++)
	{
		uint32_t animation;

		mixed_coop_writer_u16(&writer, (uint16_t)(index % tag_count));
		mixed_coop_writer_u16(&writer, (uint16_t)graph_animations);
		for (animation = 0; animation < graph_animations; animation++)
			mixed_coop_writer_u32(&writer, mixed_coop_name_hash("stand rifle idle", 16) + animation);
	}
	return mixed_coop_writer_end(&writer);
}

int net_fuzz_checks(void)
{
	static uint8_t bytes[MIXED_COOP_DICTIONARY_MAXIMUM_BYTES];
	static struct mixed_coop_fragment fragment;
	struct mixed_coop_dictionary dictionary;
	struct mixed_coop_dictionary_tag tag;
	char why[96];
	int failures = 0;
	uint32_t length = write_sample(bytes, sizeof(bytes), 3, 5), cut, offset, index;
	uint16_t host_tag, count;
	uint8_t const *hashes;
	char *seeds = getenv("NET_FUZZ_SEEDS");

	if (!length || !mixed_coop_dictionary_read(bytes, length, &dictionary, why, (int)sizeof(why)))
	{
		printf("FAIL mixed_coop: the sample dictionary does not read back (%s)\n", why);
		return 1;
	}
	mixed_coop_dictionary_tag_get(&dictionary, 1, &tag);
	if (dictionary.tag_count != 3 || dictionary.bsp_count != 2 || dictionary.bsp_digests[1][1] != 0xB0000003u ||
		dictionary.digests.layout_offset != 0x44444444u || tag.digest != 7 || tag.datum != 0xE1750001u ||
		tag.path_hash != mixed_coop_path_hash(0x77656170u, "WEAPONS/pistol/pistol") || dictionary.graph_count != 2)
	{
		printf("FAIL mixed_coop: the sample dictionary reads back otherwise than written\n");
		failures++;
	}
	offset = 0;
	index = 0;
	while (mixed_coop_dictionary_graph_next(&dictionary, &offset, &host_tag, &count, &hashes))
		index += count;
	if (index != 10)
	{
		printf("FAIL mixed_coop: the sample's graphs walk %u animations, not 10\n", (unsigned)index);
		failures++;
	}
	/* (cut short anywhere: refused) */
	for (cut = 0; cut < length; cut++)
	{
		if (mixed_coop_dictionary_read(bytes, cut, &dictionary, why, (int)sizeof(why)))
		{
			printf("FAIL mixed_coop: a dictionary cut to %u of its %u bytes was taken\n", (unsigned)cut, (unsigned)length);
			failures++;
			break;
		}
	}
	/* (a byte changed: its check refuses it) */
	for (index = 0; index < length; index++)
	{
		bytes[index] ^= 0x40;
		if (mixed_coop_dictionary_read(bytes, length, &dictionary, why, (int)sizeof(why)))
		{
			printf("FAIL mixed_coop: a dictionary with byte %u changed was taken\n", (unsigned)index);
			failures++;
			bytes[index] ^= 0x40;
			break;
		}
		bytes[index] ^= 0x40;
	}
	/* (a graph's animations past the limit, its check right: refused) */
	length = write_sample(bytes, sizeof(bytes), 3, MIXED_COOP_MAXIMUM_ANIMATIONS + 1);
	if (length && mixed_coop_dictionary_read(bytes, length, &dictionary, why, (int)sizeof(why)))
	{
		printf("FAIL mixed_coop: a graph of %d animations was taken\n", MIXED_COOP_MAXIMUM_ANIMATIONS + 1);
		failures++;
	}
	/* (a stream of three pieces sent last first: the stream) */
	length = write_sample(bytes, sizeof(bytes), 700, 3);
	mixed_coop_reassembly_begin(&reassembly, 99);
	{
		uint16_t pieces = (uint16_t)((length + MIXED_COOP_FRAGMENT_BYTES - 1) / MIXED_COOP_FRAGMENT_BYTES);
		int piece, result = _mixed_coop_fragment_refused;

		for (piece = pieces - 1; piece >= 0; piece--)
		{
			memset(&fragment, 0, sizeof(fragment));
			fragment.session = 99;
			fragment.total_bytes = length;
			fragment.index = (uint16_t)piece;
			fragment.count = pieces;
			fragment.bytes = (uint16_t)(piece + 1 < pieces ? MIXED_COOP_FRAGMENT_BYTES :
				length - (uint32_t)piece * MIXED_COOP_FRAGMENT_BYTES);
			memcpy(fragment.data, bytes + piece * MIXED_COOP_FRAGMENT_BYTES, fragment.bytes);
			result = mixed_coop_reassembly_take(&reassembly, &fragment);
			if (piece && mixed_coop_reassembly_take(&reassembly, &fragment) != _mixed_coop_fragment_refused)
			{
				printf("FAIL mixed_coop: a piece taken twice\n");
				failures++;
			}
		}
		if (result != _mixed_coop_fragment_complete || reassembly.total_bytes != length ||
			memcmp(reassembly.bytes, bytes, length) ||
			!mixed_coop_dictionary_read(reassembly.bytes, length, &dictionary, why, (int)sizeof(why)) ||
			dictionary.tag_count != 700)
		{
			printf("FAIL mixed_coop: a dictionary in %u pieces, last first, is not the dictionary\n", (unsigned)pieces);
			failures++;
		}
	}
	/* (seeds for a fuzzing run: NET_FUZZ_SEEDS=<folder>) */
	if (seeds)
	{
		void net_fuzz_write_seed(const char *folder, const char *name, const void *data, unsigned long size);
		static uint8_t seed[1 + MIXED_COOP_DICTIONARY_MAXIMUM_BYTES];
		struct mixed_coop_lobby_message hello = { _mixed_coop_lobby_hello, MIXED_COOP_VERSION, _mixed_coop_family_mcc, 0, 0,
			0x12345678u, 0 };
		struct mixed_coop_request request;

		seed[0] = 0;
		memcpy(seed + 1, &hello, sizeof(hello));
		net_fuzz_write_seed(seeds, "lobby_hello", seed, 1 + sizeof(hello));
		memset(&request, 0, sizeof(request));
		request.version = MIXED_COOP_VERSION;
		request.family = _mixed_coop_family_xbox;
		request.level = 9;
		request.digests.tag_count = 3000;
		seed[0] = 1;
		memcpy(seed + 1, &request, sizeof(request));
		net_fuzz_write_seed(seeds, "request", seed, 1 + sizeof(request));
		length = write_sample(bytes, sizeof(bytes), 3, 5);
		seed[0] = 3;
		memcpy(seed + 1, bytes, length);
		net_fuzz_write_seed(seeds, "dictionary", seed, 1 + length);
		seed[0] = 4;
		net_fuzz_write_seed(seeds, "dictionary_checked", seed, 1 + length);
		seed[0] = 2;
		memset(&fragment, 0, sizeof(fragment));
		fragment.session = 0;
		fragment.total_bytes = length;
		fragment.count = 1;
		fragment.bytes = (uint16_t)length;
		memcpy(seed + 1, &fragment, 16);
		memcpy(seed + 17, bytes, length);
		net_fuzz_write_seed(seeds, "fragments", seed, 17 + length);
	}
	return failures;
}
