/*
NET_FUZZ_WORKSHOP.C

A fuzz target for what a joiner makes of a host's map that may be a Steam
Workshop item's (port/linux/game/workshop_maps.c): the level name a host's
game settings record carries (0x80 bytes, which need not be ended), its
map.version, and this machine's copy's identity. The game's side (the file
looked at: custom_edition_mods.c's lookup by the name made here) is not here;
it is only ever asked of a name workshop_map_name_parse took.

An input is the host's identity (4 bytes), this machine's (4 bytes), a stem
length and an id (8 bytes, for the names made), then the name's bytes (at
most 0x80, as the record's field). Whatever the input:
- a level name taken is levels\test\<name>\<name>, ended within the bytes
  given; its name is at most 25 characters of [a-z0-9_-] after "ws" and the
  item's number (1 to 12 digits, no leading zero), with no separator, drive,
  climb or byte outside them; workshop_map_name_parse takes that name again
  and gives the same number;
- the verdict is one there is; a name not taken is NOT_WORKSHOP whatever the
  identities; a taken one is PLAYABLE only with both identities, equal;
- a name made of any id and stem (workshop_map_name_make) fits its room, is
  taken back by the parser with that id, and nothing is written past it.
Built as net_fuzz_crossplay_maps.c is, by run_net_fuzz_test.sh.
*/

#include "../../linux/game/workshop_maps.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RECORD_NAME_BYTES 0x80

static uint32_t read32(const uint8_t *data)
{
	return (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static void check_name(const char *name, uint64_t id)
{
	size_t length = strnlen(name, WORKSHOP_MAP_NAME_BYTES), index;
	uint64_t again = 0;

	if (length == 0 || length > WORKSHOP_MAP_NAME_MAXIMUM)
		abort();
	if (name[0] != 'w' || name[1] != 's' || name[2] < '1' || name[2] > '9')
		abort();
	for (index = 0; index < length; index++)
	{
		char c = name[index];

		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
			abort();
	}
	if (strstr(name, ".."))
		abort();
	if (!workshop_map_name_parse(name, length + 1, &again) || again != id || !id)
		abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *level_name;
	char name[WORKSHOP_MAP_NAME_BYTES + 8];
	char made[WORKSHOP_MAP_NAME_BYTES + 8];
	char stem[WORKSHOP_MAP_STEM_BYTES + 8];
	unsigned long host_identity, local_identity;
	uint64_t id = 0, verdict_id = 0, made_id;
	size_t name_size, index, stem_length;
	int taken;
	enum workshop_map_join verdict;

	if (size < 16)
		return 0;
	host_identity = read32(data);
	local_identity = read32(data + 4);
	stem_length = data[8] % (WORKSHOP_MAP_STEM_BYTES + 4);
	made_id = (uint64_t)read32(data + 9) | (uint64_t)(data[13] & 0x7F) << 32;
	(void)data[14];
	(void)data[15];
	name_size = size - 16 < RECORD_NAME_BYTES ? size - 16 : RECORD_NAME_BYTES;
	/* (exactly the bytes given, on the heap: AddressSanitizer sees a read past them) */
	level_name = malloc(name_size ? name_size : 1);
	if (!level_name)
		return 0;
	memcpy(level_name, data + 16, name_size);
	memset(name, 0x5A, sizeof(name));

	taken = workshop_map_level_name_parse(level_name, name_size, name, &id);
	for (index = WORKSHOP_MAP_NAME_BYTES; index < sizeof(name); index++)
		if (name[index] != 0x5A)
			abort();
	if (taken)
	{
		size_t length = strlen(name);

		check_name(name, id);
		if (!memchr(level_name, 0, name_size))
			abort();
		/* (levels\test\<name>\<name>, in any case, the name as given back in lower case) */
		if (strlen(level_name) != strlen(WORKSHOP_MAP_LEVEL_PREFIX) + 2 * length + 1)
			abort();
	}
	else if (name[0] || id)
		abort();

	verdict = workshop_map_join_verdict(level_name, name_size, host_identity, local_identity, &verdict_id);
	if ((int)verdict < WORKSHOP_MAP_JOIN_NOT_WORKSHOP || (int)verdict > WORKSHOP_MAP_JOIN_UNIDENTIFIED)
		abort();
	if (!taken && (verdict != WORKSHOP_MAP_JOIN_NOT_WORKSHOP || verdict_id))
		abort();
	if (taken && (verdict == WORKSHOP_MAP_JOIN_NOT_WORKSHOP || verdict_id != id))
		abort();
	if (verdict == WORKSHOP_MAP_JOIN_PLAYABLE && (!host_identity || host_identity != local_identity))
		abort();
	if (taken && host_identity && local_identity && host_identity == local_identity && verdict != WORKSHOP_MAP_JOIN_PLAYABLE)
		abort();
	free(level_name);

	/* (a name made of the input's id and stem: its bytes the name's own when they are the stem's characters) */
	for (index = 0; index < stem_length && index < WORKSHOP_MAP_STEM_BYTES + 3; index++)
		stem[index] = (char)data[(16 + index) % size];
	stem[stem_length < sizeof(stem) ? stem_length : sizeof(stem) - 1] = 0;
	memset(made, 0x5A, sizeof(made));
	if (workshop_map_name_make(made_id, stem, made))
		check_name(made, made_id);
	else if (made[0])
		abort();
	for (index = WORKSHOP_MAP_NAME_BYTES; index < sizeof(made); index++)
		if (made[index] != 0x5A)
			abort();
	return 0;
}
