/*
NET_FUZZ_EPOCH.C

A fuzz target for OpenCE 42's distributed layout as a client reads what a
host of 42 or more sends (port/linux/src/network_epoch.c, used by
network_distributed.c): every distributed message's header 4 bytes longer
with the host's checkpoint epoch, a batch's messages each with their own,
the boundary's checks, and a unit state's further parts.

An input is a kind byte, then the bytes:
  kind & 3 == 0  a message (or batch) as a host of 42 sends it: its size word
                 set to the input's length, stripped with the epoch the next
                 4 bytes say; a stripped message must be no longer than the
                 input, its header's size its length, and (not a batch) widen
                 again to the input exactly
  kind & 3 == 1  the same bytes as this build makes them: widened, then
                 stripped with the epoch given, which must give them back
                 (any batch whose sizes do not add up is refused, not sent)
  kind & 3 == 2  a unit state's head (player, flags, parts) for a host of
                 version 40 + (kind >> 2) % 8: the further parts' bytes, which
                 must be -1 or a sum of 28 and 30 that the state can have
  kind & 3 == 3  the boundary's checks on the input's fields
Built as the platform layer's tests are, with AddressSanitizer and UBSan, by
run_net_fuzz_test.sh.
*/

#include "../../linux/src/network_epoch.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint8_t input[0x1100], wide[0x1100], narrow[0x1100], again[0x1100];

static void set_size(uint8_t *message, int size)
{
	uint16_t header = (uint16_t)(2 | (size << 4));

	memcpy(message, &header, 2);
}

static int get_size(const uint8_t *message)
{
	uint16_t header;

	memcpy(&header, message, 2);
	return header >> 4;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	uint8_t kind;
	uint32_t want = 0, epoch = 0;
	int length, stripped, widened;

	if (size < 1)
		return 0;
	kind = data[0];
	data++;
	size--;
	if (size >= 4)
	{
		memcpy(&want, data, 4);
		data += 4;
		size -= 4;
	}
	if (size > NETWORK_EPOCH_MESSAGE_MAXIMUM)
		size = NETWORK_EPOCH_MESSAGE_MAXIMUM;
	length = (int)size;
	memset(input, 0, sizeof(input));
	memcpy(input, data, size);
	switch (kind & 3)
	{
	case 0:
		if (length >= 2)
			set_size(input, length);
		stripped = network_epoch_header_strip(input, length, narrow, length, want, (kind & 4) ? 9 : 0, &epoch);
		if (!stripped)
			break;
		if (stripped > length || stripped < NETWORK_EPOCH_HEADER_SIZE || get_size(narrow) != stripped)
			abort();
		if (input[2] != NETWORK_EPOCH_BATCH_KIND)
		{
			widened = network_epoch_header_expand(narrow, stripped, wide, (int)sizeof(wide), epoch);
			if (widened != length || memcmp(wide + 2, input + 2, (size_t)length - 2))
				abort();
		}
		else
		{
			/* (a batch: every message kept is whole, of the epoch wanted or
			the kind kept, and widens again to no more than came) */
			widened = network_epoch_header_expand(narrow, stripped, wide, (int)sizeof(wide), want);
			if (widened > length)
				abort();
		}
		break;
	case 1:
		if (length < NETWORK_EPOCH_HEADER_SIZE)
			break;
		set_size(input, length);
		widened = network_epoch_header_expand(input, length, wide, (kind & 4) ? length + 4 : (int)sizeof(wide), want);
		if (!widened)
			break;
		if (widened < length + 4 || widened > NETWORK_EPOCH_MESSAGE_MAXIMUM || get_size(wide) != widened)
			abort();
		stripped = network_epoch_header_strip(wide, widened, again, (int)sizeof(again), want, 0, &epoch);
		if (stripped != length || epoch != want || memcmp(again + 2, input + 2, (size_t)length - 2))
			abort();
		break;
	case 2:
		if (length >= 3)
		{
			unsigned int version = 40 + (unsigned int)((kind >> 2) % 8);
			int alive = input[1] & 1, placed = (input[1] >> 1) & 1;
			int after = network_epoch_unit_part_bytes(version, input[2], NETWORK_EPOCH_PART_AFTER_UP, alive, placed);
			int last = network_epoch_unit_part_bytes(version, input[2], NETWORK_EPOCH_PART_LAST, alive, placed);

			if ((after != -1 && after != 0 && after != NETWORK_EPOCH_DECK_PART_SIZE) ||
				(last != -1 && last != 0 && last != NETWORK_EPOCH_POSE_PART_SIZE))
			{
				abort();
			}
			if (after > 0 && (version < 43 || !alive || !placed))
				abort();
			if (last > 0 && (version < 42 || !alive))
				abort();
			if ((input[2] & 0x40) && version < 42 && last != -1)
				abort();
		}
		break;
	case 3:
		if (length >= 4)
		{
			int taken = network_epoch_boundary_taken(input[0] | (input[1] & 0x0F) << 8, input[2] & 3, input[3] & 1,
				(input[3] >> 1) & 1, want, 0x80000000u);

			if (taken && (!want || !network_epoch_newer(want, 0x80000000u) || (input[3] & 3) != 1 ||
				(input[2] & 3) != 0))
			{
				abort();
			}
		}
		break;
	}
	return 0;
}
