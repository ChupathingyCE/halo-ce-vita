/*
NETWORK_EPOCH.C

OpenCE's network version 42's distributed layout (network_epoch.h). Ported
from ChupathingyCE's port/linux/game/network_rules.c (GPL-3.0-or-later,
Milenko: network_rules_header_expand, _header_strip, _epoch_newer,
_boundary_taken, _unit_part_bytes), with the layout fixed to OpenCE's 42 to
44 rather than a table's facts.
*/

#include "network_epoch.h"

#include <string.h>

static unsigned int epoch_word(const uint8_t *bytes)
{
	return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8);
}

static void epoch_put_word(uint8_t *bytes, unsigned int value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static uint32_t epoch_long(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void epoch_put_long(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

/* the message header's size (this machine's order, as the transport gives
it: the type in the low 4 bits) set */
static void epoch_header_size(uint8_t *message, int size)
{
	uint16_t header;

	memcpy(&header, message, sizeof(header));
	header = (uint16_t)((header & 0xF) | ((unsigned int)size << 4));
	memcpy(message, &header, sizeof(header));
}

int network_epoch_header_expand(const uint8_t *message, int size, uint8_t *out, int room, uint32_t epoch)
{
	const int extra = NETWORK_EPOCH_HEADER_EXTRA;
	int in = NETWORK_EPOCH_HEADER_SIZE, at;

	if (!message || !out || size < NETWORK_EPOCH_HEADER_SIZE || size + extra > room)
		return 0;
	memcpy(out, message, NETWORK_EPOCH_HEADER_SIZE);
	epoch_put_long(out + NETWORK_EPOCH_HEADER_SIZE, epoch);
	at = NETWORK_EPOCH_HEADER_SIZE + extra;
	if (message[2] != NETWORK_EPOCH_BATCH_KIND)
	{
		memcpy(out + at, message + in, (size_t)(size - in));
		at += size - in;
	}
	else
	{
		/* (each message: its size word, its header past the message header
		(6 bytes), the further bytes, its entries) */
		while (in < size)
		{
			int length;

			if (size - in < 2)
				return 0;
			length = (int)epoch_word(message + in);
			if (length < NETWORK_EPOCH_HEADER_SIZE - 2 || length > size - in - 2 || at + 2 + length + extra > room)
				return 0;
			epoch_put_word(out + at, (unsigned int)(length + extra));
			memcpy(out + at + 2, message + in + 2, NETWORK_EPOCH_HEADER_SIZE - 2);
			epoch_put_long(out + at + NETWORK_EPOCH_HEADER_SIZE, epoch);
			memcpy(out + at + NETWORK_EPOCH_HEADER_SIZE + extra, message + in + NETWORK_EPOCH_HEADER_SIZE,
				(size_t)(length - (NETWORK_EPOCH_HEADER_SIZE - 2)));
			at += 2 + length + extra;
			in += 2 + length;
		}
	}
	if (at > NETWORK_EPOCH_MESSAGE_MAXIMUM)
		return 0;
	epoch_header_size(out, at);
	return at;
}

int network_epoch_header_strip(const uint8_t *message, int size, uint8_t *out, int room, uint32_t want,
	int keep_kind, uint32_t *epoch)
{
	const int extra = NETWORK_EPOCH_HEADER_EXTRA;
	int in, at = NETWORK_EPOCH_HEADER_SIZE;

	if (epoch)
		*epoch = 0;
	if (!message || !out || !epoch || size < NETWORK_EPOCH_HEADER_SIZE + extra || room < size)
		return 0;
	*epoch = epoch_long(message + NETWORK_EPOCH_HEADER_SIZE);
	memcpy(out, message, NETWORK_EPOCH_HEADER_SIZE);
	in = NETWORK_EPOCH_HEADER_SIZE + extra;
	if (message[2] != NETWORK_EPOCH_BATCH_KIND)
	{
		memcpy(out + at, message + in, (size_t)(size - in));
		at += size - in;
	}
	else
	{
		while (in < size)
		{
			int length;
			uint32_t inner;

			/* (reading stops at a size word past the end, or shorter than a
			message's header and its further bytes: the messages before it
			are kept) */
			if (size - in < 2)
				break;
			length = (int)epoch_word(message + in);
			if (length < NETWORK_EPOCH_HEADER_SIZE - 2 + extra || length > size - in - 2)
				break;
			inner = epoch_long(message + in + NETWORK_EPOCH_HEADER_SIZE);
			if (inner == want || (keep_kind && message[in + 2] == keep_kind))
			{
				epoch_put_word(out + at, (unsigned int)(length - extra));
				memcpy(out + at + 2, message + in + 2, NETWORK_EPOCH_HEADER_SIZE - 2);
				memcpy(out + at + NETWORK_EPOCH_HEADER_SIZE, message + in + NETWORK_EPOCH_HEADER_SIZE + extra,
					(size_t)(length - extra - (NETWORK_EPOCH_HEADER_SIZE - 2)));
				at += 2 + length - extra;
			}
			in += 2 + length;
		}
	}
	epoch_header_size(out, at);
	return at;
}

int network_epoch_newer(uint32_t epoch, uint32_t current)
{
	return (int32_t)(epoch - current) > 0;
}

int network_epoch_boundary_taken(int size, int count, int from_stream, int in_batch, uint32_t epoch,
	uint32_t current)
{
	return size == NETWORK_EPOCH_HEADER_SIZE && count == 0 && from_stream && !in_batch && epoch &&
		network_epoch_newer(epoch, current);
}

int network_epoch_unit_part_bytes(unsigned int version, unsigned int parts, int where, int alive, int placed)
{
	int bit, bytes = 0;

	for (bit = 0; bit < 8; bit++)
	{
		int size, at;

		if (!(parts & (1u << bit)) || (NETWORK_EPOCH_PARTS_BUILT_IN & (1u << bit)))
			continue;
		if (bit == NETWORK_EPOCH_POSE_PART_BIT && version >= 42)
		{
			size = NETWORK_EPOCH_POSE_PART_SIZE;
			at = NETWORK_EPOCH_PART_LAST;
		}
		else if (bit == NETWORK_EPOCH_DECK_PART_BIT && version >= 43)
		{
			size = NETWORK_EPOCH_DECK_PART_SIZE;
			at = NETWORK_EPOCH_PART_AFTER_UP;
		}
		else
			return -1;
		if (at != where)
			continue;
		if (!alive || (where == NETWORK_EPOCH_PART_AFTER_UP && !placed))
			return -1;
		bytes += size;
	}
	return bytes;
}
