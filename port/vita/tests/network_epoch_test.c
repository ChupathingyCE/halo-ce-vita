/*
NETWORK_EPOCH_TEST.C

OpenCE 42's distributed layout as this build reads and writes it
(port/linux/src/network_epoch.c): the header's further bytes and epoch on a
message and in a batch, a batch's members of another epoch left out, the
boundary's checks, a unit state's further parts by version, and malformed
input refused. Run by run_network_wire_test.sh.
*/

#include "../../linux/src/network_epoch.c"

#include <stdio.h>
#include <string.h>

static int failures, checks;

static void check(int good, const char *what)
{
	checks++;
	failures += !good;
	printf("%s %s\n", good ? "PASS" : "FAIL", what);
}

/* a message header word as this machine keeps it: type 2, the size */
static void header_word(uint8_t *message, int size)
{
	uint16_t word = (uint16_t)(2 | (size << 4));

	memcpy(message, &word, 2);
}

static int header_size(const uint8_t *message)
{
	uint16_t word;

	memcpy(&word, message, 2);
	return word >> 4;
}

/* a message of kind with count entries of entry bytes (each byte its
index + seed) */
static int make_message(uint8_t *out, int kind, int count, int entry, int seed)
{
	int size = NETWORK_EPOCH_HEADER_SIZE + count * entry, index;

	header_word(out, size);
	out[2] = (uint8_t)kind;
	out[3] = (uint8_t)count;
	out[4] = 0x10;
	out[5] = 0x20;
	out[6] = 0x30;
	out[7] = 0x00;
	for (index = NETWORK_EPOCH_HEADER_SIZE; index < size; index++)
		out[index] = (uint8_t)(index + seed);
	return size;
}

/* a batch (as this build makes it) of the messages */
static int make_batch(uint8_t *out, const uint8_t *const *messages, const int *sizes, int count)
{
	int at = NETWORK_EPOCH_HEADER_SIZE, index;

	out[2] = NETWORK_EPOCH_BATCH_KIND;
	out[3] = 0;
	memset(out + 4, 0x44, 4);
	for (index = 0; index < count; index++)
	{
		int length = sizes[index] - 2;

		out[at] = (uint8_t)length;
		out[at + 1] = (uint8_t)(length >> 8);
		memcpy(out + at + 2, messages[index] + 2, (size_t)length);
		at += 2 + length;
	}
	header_word(out, at);
	return at;
}

int main(void)
{
	static uint8_t one[512], two[512], batch[1200], wide[1400], narrow[1400], again[1400];
	int one_size = make_message(one, 5, 3, 7, 1);
	int two_size = make_message(two, 9, 0, 0, 2);
	uint32_t epoch;
	int size, narrow_size;

	/* one message */
	size = network_epoch_header_expand(one, one_size, wide, (int)sizeof(wide), 0x01020304);
	check(size == one_size + 4 && header_size(wide) == size && !memcmp(wide + 2, one + 2, 6) && wide[8] == 4 &&
		wide[9] == 3 && wide[10] == 2 && wide[11] == 1 && !memcmp(wide + 12, one + 8, (size_t)(one_size - 8)),
		"a message: its 8 bytes, the epoch little-endian, its entries; the size 4 more");
	narrow_size = network_epoch_header_strip(wide, size, narrow, (int)sizeof(narrow), 7, 0, &epoch);
	check(narrow_size == one_size && epoch == 0x01020304 && !memcmp(narrow, one, (size_t)one_size),
		"... and back: the message as it was, its epoch told (any epoch: the caller checks)");
	check(!network_epoch_header_expand(one, one_size, wide, one_size + 3, 1), "no room for the further bytes: 0");
	check(!network_epoch_header_strip(wide, 11, narrow, (int)sizeof(narrow), 0, 0, &epoch) && epoch == 0,
		"a message shorter than the 12 bytes: 0");
	check(!network_epoch_header_expand(one, 7, wide, (int)sizeof(wide), 1), "a message shorter than a header: 0");
	check(!network_epoch_header_strip(wide, size, narrow, size - 1, 0, 0, &epoch), "a strip with less room than the message: 0");

	/* a batch */
	{
		const uint8_t *messages[2] = { one, two };
		int sizes[2] = { one_size, two_size };
		int batch_size = make_batch(batch, messages, sizes, 2);

		size = network_epoch_header_expand(batch, batch_size, wide, (int)sizeof(wide), 9);
		check(size == batch_size + 12 && header_size(wide) == size,
			"a batch: 4 bytes more for itself and each of its 2 messages");
		narrow_size = network_epoch_header_strip(wide, size, narrow, (int)sizeof(narrow), 9, 0, &epoch);
		check(narrow_size == batch_size && epoch == 9 && !memcmp(narrow + 2, batch + 2, (size_t)batch_size - 2) &&
			header_size(narrow) == batch_size, "... and back, its messages of the epoch wanted kept");
		narrow_size = network_epoch_header_strip(wide, size, narrow, (int)sizeof(narrow), 8, 0, &epoch);
		check(narrow_size == NETWORK_EPOCH_HEADER_SIZE && epoch == 9,
			"a batch's messages of another epoch than wanted left out (the batch's own epoch told)");
		narrow_size = network_epoch_header_strip(wide, size, narrow, (int)sizeof(narrow), 8, 9, &epoch);
		check(narrow_size == NETWORK_EPOCH_HEADER_SIZE + 2 + two_size - 2 && narrow[NETWORK_EPOCH_HEADER_SIZE + 2] == 9,
			"... but a kind kept whatever its epoch (a host's ready)");
		/* (one message of the batch of another epoch) */
		memcpy(again, wide, (size_t)size);
		again[NETWORK_EPOCH_HEADER_SIZE + 4 + 2 + 6] ^= 1;
		narrow_size = network_epoch_header_strip(again, size, narrow, (int)sizeof(narrow), 9, 0, &epoch);
		check(narrow_size == NETWORK_EPOCH_HEADER_SIZE + 2 + two_size - 2,
			"a batch of two epochs: the other's message alone left out");
		/* (a size word past the end: the messages before it kept) */
		memcpy(again, wide, (size_t)size);
		again[NETWORK_EPOCH_HEADER_SIZE + 4 + 2 + one_size - 2 + 4] = 0xFF;
		again[NETWORK_EPOCH_HEADER_SIZE + 4 + 2 + one_size - 2 + 4 + 1] = 0x0F;
		narrow_size = network_epoch_header_strip(again, size, narrow, (int)sizeof(narrow), 9, 0, &epoch);
		check(narrow_size == NETWORK_EPOCH_HEADER_SIZE + 2 + one_size - 2,
			"a size word past the batch's end: read stops, the messages before it kept");
		/* (a size word too short for a header and the epoch) */
		again[NETWORK_EPOCH_HEADER_SIZE + 4] = 9;
		again[NETWORK_EPOCH_HEADER_SIZE + 5] = 0;
		narrow_size = network_epoch_header_strip(again, size, narrow, (int)sizeof(narrow), 9, 0, &epoch);
		check(narrow_size == NETWORK_EPOCH_HEADER_SIZE, "a message's size shorter than its header and epoch: none kept");
		/* (expand refuses a malformed batch of ours) */
		batch[NETWORK_EPOCH_HEADER_SIZE] = 0xFF;
		check(!network_epoch_header_expand(batch, batch_size, wide, (int)sizeof(wide), 1), "a malformed batch: not sent");
	}
	/* a batch past the 12-bit size once widened */
	{
		static uint8_t big[0x1000];
		int at = NETWORK_EPOCH_HEADER_SIZE, count = 0;

		big[2] = NETWORK_EPOCH_BATCH_KIND;
		while (at + 8 <= 0xFF8)
		{
			big[at] = 6;
			big[at + 1] = 0;
			big[at + 2] = 9;
			at += 8;
			count++;
		}
		header_word(big, at);
		check(!network_epoch_header_expand(big, at, wide, (int)sizeof(wide), 1) &&
			!network_epoch_header_expand(big, at, big, 0, 1), "a batch that no longer fits a header's size: 0");
		(void)count;
	}

	/* the boundary */
	check(network_epoch_boundary_taken(8, 0, 1, 0, 5, 4), "a boundary: the header alone, count 0, the stream, newer");
	check(!network_epoch_boundary_taken(8, 0, 1, 0, 4, 4) && !network_epoch_boundary_taken(8, 0, 1, 0, 3, 4) &&
		!network_epoch_boundary_taken(8, 0, 1, 0, 0, 0), "not the same, an older or epoch 0");
	check(!network_epoch_boundary_taken(9, 0, 1, 0, 5, 4) && !network_epoch_boundary_taken(8, 1, 1, 0, 5, 4) &&
		!network_epoch_boundary_taken(8, 0, 0, 0, 5, 4) && !network_epoch_boundary_taken(8, 0, 1, 1, 5, 4),
		"not with entries, a count, from a datagram, or in a batch");
	check(network_epoch_newer(1, 0xFFFFFFFFu) && !network_epoch_newer(0xFFFFFFFFu, 1),
		"newer by a signed difference (past the wrap)");

	/* a unit state's further parts */
	check(network_epoch_unit_part_bytes(41, 0x40, NETWORK_EPOCH_PART_LAST, 1, 1) == -1 &&
		network_epoch_unit_part_bytes(42, 0x80, NETWORK_EPOCH_PART_AFTER_UP, 1, 1) == -1,
		"bit 6 before 42 and bit 7 before 43: unreadable");
	check(network_epoch_unit_part_bytes(42, 0x40, NETWORK_EPOCH_PART_LAST, 1, 0) == 30 &&
		network_epoch_unit_part_bytes(42, 0x40, NETWORK_EPOCH_PART_AFTER_UP, 1, 0) == 0,
		"42's pose: 30 bytes last, none after the up");
	check(network_epoch_unit_part_bytes(44, 0xC0, NETWORK_EPOCH_PART_AFTER_UP, 1, 1) == 28 &&
		network_epoch_unit_part_bytes(44, 0xC0, NETWORK_EPOCH_PART_LAST, 1, 1) == 30,
		"43 and 44's deck: 28 after the up, the pose 30 last");
	check(network_epoch_unit_part_bytes(44, 0x80, NETWORK_EPOCH_PART_AFTER_UP, 1, 0) == -1 &&
		network_epoch_unit_part_bytes(44, 0x40, NETWORK_EPOCH_PART_LAST, 0, 0) == -1,
		"a deck on a unit not placed, a pose on a dead one: unreadable");
	check(network_epoch_unit_part_bytes(44, 0x3F, NETWORK_EPOCH_PART_LAST, 1, 1) == 0,
		"the built-in parts: no further bytes");

	printf("%s (%d of %d checks failed)\n", failures ? "FAIL" : "PASS", failures, checks);
	return failures != 0;
}
