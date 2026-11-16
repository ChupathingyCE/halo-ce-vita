/*
WORKSHOP_MAPS.C

(port) Steam Workshop multiplayer maps' names, and a joiner's verdict on a
host's map (workshop_maps.h). Plain C: every byte of a host's level name is
read within the size given, and nothing is written past the room given.
*/

#include "workshop_maps.h"

#include <stdio.h>
#include <string.h>

/* ---------- private code */

static int stem_character(char character)
{
	return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' ||
		character == '-';
}

static char lower(char character)
{
	return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

/* FNV-1a of the stem: the shortened name's four hex digits */
static uint32_t stem_hash(const char *stem)
{
	uint32_t hash = 2166136261u;

	for (; *stem; stem++)
		hash = (hash ^ (unsigned char)*stem) * 16777619u;
	return hash;
}

/* ---------- names */

int workshop_map_id_text(uint64_t id, char *text, size_t size)
{
	char digits[24];
	int length;

	if (!text || !size)
		return 0;
	text[0] = 0;
	length = snprintf(digits, sizeof(digits), "%llu", (unsigned long long)id);
	if (!id || length <= 0 || length > WORKSHOP_MAP_ID_DIGITS || (size_t)length >= size)
		return 0;
	memcpy(text, digits, (size_t)length + 1);
	return 1;
}

int workshop_map_id_parse(const char *text, uint64_t *id)
{
	uint64_t value = 0;
	int length;

	if (!text || text[0] < '1' || text[0] > '9')
		return 0;
	for (length = 0; text[length]; length++)
	{
		if (length >= WORKSHOP_MAP_ID_DIGITS || text[length] < '0' || text[length] > '9')
			return 0;
		value = value * 10u + (uint64_t)(text[length] - '0');
	}
	if (id)
		*id = value;
	return 1;
}

int workshop_map_name_make(uint64_t id, const char *stem, char *name)
{
	char digits[24];
	size_t stem_length, head, room;
	int written;

	if (!name)
		return 0;
	name[0] = 0;
	if (!stem || !workshop_map_id_text(id, digits, sizeof(digits)))
		return 0;
	for (stem_length = 0; stem[stem_length]; stem_length++)
		if (stem_length >= WORKSHOP_MAP_STEM_BYTES - 1 || !stem_character(stem[stem_length]))
			return 0;
	if (!stem_length)
		return 0;
	head = strlen(WORKSHOP_MAP_PREFIX) + strlen(digits) + 1;
	room = WORKSHOP_MAP_NAME_MAXIMUM - head;
	if (stem_length <= room)
	{
		written = snprintf(name, WORKSHOP_MAP_NAME_BYTES, "%s%s_%s", WORKSHOP_MAP_PREFIX, digits, stem);
	}
	else
	{
		/* (its first characters, '-' and four hex digits of the whole: at
		least one of its own) */
		if (room < 6)
			return 0;
		written = snprintf(name, WORKSHOP_MAP_NAME_BYTES, "%s%s_%.*s-%04x", WORKSHOP_MAP_PREFIX, digits, (int)(room - 5), stem,
			(unsigned int)(stem_hash(stem) & 0xFFFFu));
	}
	if (written <= 0 || written > WORKSHOP_MAP_NAME_MAXIMUM)
	{
		name[0] = 0;
		return 0;
	}
	return 1;
}

int workshop_map_name_parse(const char *name, size_t size, uint64_t *id)
{
	size_t prefix = strlen(WORKSHOP_MAP_PREFIX), index, digits_end, length;
	uint64_t value = 0;

	if (id)
		*id = 0;
	if (!name || !size)
		return 0;
	/* (ended within size, and no longer than a map's name) */
	for (length = 0; length < size && name[length]; length++)
		if (length > WORKSHOP_MAP_NAME_MAXIMUM)
			return 0;
	if (length >= size || length > WORKSHOP_MAP_NAME_MAXIMUM)
		return 0;
	for (index = 0; index < prefix; index++)
		if (lower(name[index]) != WORKSHOP_MAP_PREFIX[index])
			return 0;
	if (name[prefix] < '1' || name[prefix] > '9')
		return 0;
	for (digits_end = prefix; name[digits_end] >= '0' && name[digits_end] <= '9'; digits_end++)
	{
		if (digits_end - prefix >= WORKSHOP_MAP_ID_DIGITS)
			return 0;
		value = value * 10u + (uint64_t)(name[digits_end] - '0');
	}
	if (name[digits_end] != '_' || !name[digits_end + 1])
		return 0;
	for (index = digits_end + 1; name[index]; index++)
		if (!stem_character(lower(name[index])))
			return 0;
	if (id)
		*id = value;
	return 1;
}

int workshop_map_level_name_parse(const char *level_name, size_t size, char *name, uint64_t *id)
{
	size_t prefix = strlen(WORKSHOP_MAP_LEVEL_PREFIX), length, first, index;
	const char *separator;

	if (name)
		name[0] = 0;
	if (id)
		*id = 0;
	if (!level_name || !size)
		return 0;
	if (size > WORKSHOP_MAP_LEVEL_NAME_BYTES)
		size = WORKSHOP_MAP_LEVEL_NAME_BYTES;
	for (length = 0; length < size && level_name[length]; length++)
	{
	}
	if (length >= size || length <= prefix)
		return 0;
	for (index = 0; index < prefix; index++)
		if (lower(level_name[index]) != WORKSHOP_MAP_LEVEL_PREFIX[index])
			return 0;
	/* (levels\test\<name>\<name>: the two parts the same) */
	separator = memchr(level_name + prefix, '\\', length - prefix);
	if (!separator)
		return 0;
	first = (size_t)(separator - (level_name + prefix));
	if (!first || first > WORKSHOP_MAP_NAME_MAXIMUM || length - prefix - first - 1 != first)
		return 0;
	for (index = 0; index < first; index++)
		if (lower(level_name[prefix + index]) != lower(separator[1 + index]))
			return 0;
	{
		char part[WORKSHOP_MAP_NAME_BYTES];

		for (index = 0; index < first; index++)
			part[index] = lower(level_name[prefix + index]);
		part[first] = 0;
		if (!workshop_map_name_parse(part, sizeof(part), id))
			return 0;
		if (name)
			memcpy(name, part, first + 1);
	}
	return 1;
}

/* ---------- a joiner's verdict */

enum workshop_map_join workshop_map_join_verdict(const char *level_name, size_t size, unsigned long host_identity,
	unsigned long local_identity, uint64_t *id)
{
	uint64_t item = 0;

	if (id)
		*id = 0;
	if (!workshop_map_level_name_parse(level_name, size, NULL, &item))
		return WORKSHOP_MAP_JOIN_NOT_WORKSHOP;
	if (id)
		*id = item;
	if (!(host_identity & 0xFFFFFFFFUL))
		return WORKSHOP_MAP_JOIN_UNIDENTIFIED;
	if (!(local_identity & 0xFFFFFFFFUL))
		return WORKSHOP_MAP_JOIN_MISSING;
	if ((local_identity & 0xFFFFFFFFUL) != (host_identity & 0xFFFFFFFFUL))
		return WORKSHOP_MAP_JOIN_DIFFERENT;
	return WORKSHOP_MAP_JOIN_PLAYABLE;
}
