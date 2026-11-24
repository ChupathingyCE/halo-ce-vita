/*
MIXED_COOP_PROTOCOL.C

Mixed co-op's messages read and written (mixed_coop_protocol.h). Plain C.
*/

#include "mixed_coop_protocol.h"

#include <stdio.h>
#include <string.h>

/* ---------- hashes */

uint32_t mixed_coop_fnv(uint32_t hash, void const *bytes, uint32_t size)
{
	uint8_t const *cursor = (uint8_t const *)bytes;
	uint32_t index;

	for (index = 0; index < size; index++)
	{
		hash ^= cursor[index];
		hash *= 0x01000193u;
	}
	return hash;
}

static uint8_t mixed_coop_fold(char character)
{
	if (character >= 'A' && character <= 'Z')
		return (uint8_t)(character - 'A' + 'a');
	if (character == '/')
		return '\\';
	return (uint8_t)character;
}

uint32_t mixed_coop_path_hash(uint32_t group_tag, char const *path)
{
	uint32_t hash = MIXED_COOP_FNV_BASIS;
	uint8_t group[4];
	int index;

	/* (a Custom Edition map's gbxmodels are the game's models: one group) */
	if (group_tag == 0x6D6F6432u /* 'mod2' */)
		group_tag = 0x6D6F6465u; /* 'mode' */
	for (index = 0; index < 4; index++)
		group[index] = (uint8_t)(group_tag >> (8 * (3 - index)));
	hash = mixed_coop_fnv(hash, group, 4);
	for (index = 0; path && path[index] && index < 256; index++)
	{
		uint8_t folded = mixed_coop_fold(path[index]);

		hash = mixed_coop_fnv(hash, &folded, 1);
	}
	return hash;
}

uint32_t mixed_coop_name_hash(char const *name, int length)
{
	uint32_t hash = MIXED_COOP_FNV_BASIS;
	int index;

	for (index = 0; name && index < length && name[index]; index++)
	{
		uint8_t folded = mixed_coop_fold(name[index]);

		hash = mixed_coop_fnv(hash, &folded, 1);
	}
	return hash;
}

char const *mixed_coop_family_name(int family)
{
	switch (family)
	{
	case _mixed_coop_family_xbox: return "Xbox";
	case _mixed_coop_family_mcc: return "MCC";
	default: return "none";
	}
}

/* ---------- messages */

int mixed_coop_lobby_valid(struct mixed_coop_lobby_message const *message)
{
	if (!message)
		return 0;
	if (message->command == 0 || message->command >= NUMBER_OF_MIXED_COOP_LOBBY_COMMANDS)
		return 0;
	if (message->reserved != 0)
		return 0;
	/* (1.1.3: the collision ask, an MCC host's, and the offer, an Xbox machine's, of a multiplayer map: their flags
	the flag stand and teleporter base models - mixed_collision_protocol.h's bits; no other message has flags) */
	if (message->command == _mixed_coop_lobby_collision_ask || message->command == _mixed_coop_lobby_collision_offer)
	{
		if ((message->flags & ~0x3u) != 0 || message->level < MIXED_COOP_CAMPAIGN_LEVELS ||
			message->family != (message->command == _mixed_coop_lobby_collision_ask ? _mixed_coop_family_mcc :
				_mixed_coop_family_xbox) ||
			(message->command == _mixed_coop_lobby_collision_ask && message->flags == 0))
		{
			return 0;
		}
	}
	else if (message->flags != 0)
		return 0;
	if (message->family >= NUMBER_OF_MIXED_COOP_FAMILIES)
		return 0;
	/* (the host plays the level from its own files: never none) */
	if (message->command == _mixed_coop_lobby_hello && message->family == _mixed_coop_family_none)
		return 0;
	/* (a layout is asked for by an MCC machine alone, of a multiplayer map) */
	if (message->command == _mixed_coop_lobby_layout_request &&
		(message->family != _mixed_coop_family_mcc || message->level < MIXED_COOP_CAMPAIGN_LEVELS))
	{
		return 0;
	}
	return mixed_coop_version_names_level(message->version, message->level);
}

int mixed_coop_version_for_level(unsigned level)
{
	return level < MIXED_COOP_CAMPAIGN_LEVELS ? MIXED_COOP_VERSION_CAMPAIGN : MIXED_COOP_VERSION;
}

int mixed_coop_version_names_level(unsigned version, unsigned level)
{
	if (version == MIXED_COOP_VERSION_CAMPAIGN)
		return level < MIXED_COOP_CAMPAIGN_LEVELS;
	if (version == MIXED_COOP_VERSION)
		return level < MIXED_COOP_LEVELS;
	return 0;
}

int mixed_coop_request_valid(struct mixed_coop_request const *request)
{
	return request && mixed_coop_version_names_level(request->version, request->level) &&
		(request->family == _mixed_coop_family_xbox || request->family == _mixed_coop_family_mcc) &&
		request->digests.tag_count <= MIXED_COOP_MAXIMUM_TAGS;
}

/* ---------- reassembly */

void mixed_coop_reassembly_begin(struct mixed_coop_reassembly *reassembly, uint32_t session)
{
	reassembly->session = session;
	reassembly->total_bytes = 0;
	reassembly->count = 0;
	reassembly->received = 0;
	memset(reassembly->have, 0, sizeof(reassembly->have));
}

int mixed_coop_reassembly_take(struct mixed_coop_reassembly *reassembly, struct mixed_coop_fragment const *fragment)
{
	uint32_t expected_count, offset;

	if (!fragment || fragment->session != reassembly->session || fragment->pad != 0)
		return _mixed_coop_fragment_refused;
	if (fragment->total_bytes == 0 || fragment->total_bytes > MIXED_COOP_DICTIONARY_MAXIMUM_BYTES)
		return _mixed_coop_fragment_refused;
	expected_count = (fragment->total_bytes + MIXED_COOP_FRAGMENT_BYTES - 1) / MIXED_COOP_FRAGMENT_BYTES;
	if (fragment->count != expected_count || fragment->index >= fragment->count)
		return _mixed_coop_fragment_refused;
	/* (every piece full but the last, which has the rest) */
	offset = (uint32_t)fragment->index * MIXED_COOP_FRAGMENT_BYTES;
	if (fragment->bytes != (fragment->index + 1u < fragment->count ? MIXED_COOP_FRAGMENT_BYTES :
		fragment->total_bytes - offset))
	{
		return _mixed_coop_fragment_refused;
	}
	if (reassembly->received == 0)
	{
		reassembly->total_bytes = fragment->total_bytes;
		reassembly->count = fragment->count;
	}
	else if (reassembly->total_bytes != fragment->total_bytes || reassembly->count != fragment->count)
	{
		return _mixed_coop_fragment_refused;
	}
	if (reassembly->have[fragment->index])
		return _mixed_coop_fragment_refused;
	memcpy(reassembly->bytes + offset, fragment->data, fragment->bytes);
	reassembly->have[fragment->index] = 1;
	reassembly->received++;
	return reassembly->received == reassembly->count ? _mixed_coop_fragment_complete : _mixed_coop_fragment_taken;
}

/* ---------- the stream */

void mixed_coop_writer_begin(struct mixed_coop_dictionary_writer *writer, void *bytes, uint32_t size)
{
	writer->bytes = (uint8_t *)bytes;
	writer->size = size;
	writer->length = 0;
	writer->overflow = 0;
}

static void mixed_coop_writer_bytes(struct mixed_coop_dictionary_writer *writer, void const *bytes, uint32_t size)
{
	if (writer->overflow || size > writer->size - writer->length)
	{
		writer->overflow = 1;
		return;
	}
	memcpy(writer->bytes + writer->length, bytes, size);
	writer->length += size;
}

void mixed_coop_writer_u8(struct mixed_coop_dictionary_writer *writer, uint8_t value)
{
	mixed_coop_writer_bytes(writer, &value, 1);
}

void mixed_coop_writer_u16(struct mixed_coop_dictionary_writer *writer, uint16_t value)
{
	uint8_t bytes[2] = { (uint8_t)value, (uint8_t)(value >> 8) };

	mixed_coop_writer_bytes(writer, bytes, 2);
}

void mixed_coop_writer_u32(struct mixed_coop_dictionary_writer *writer, uint32_t value)
{
	uint8_t bytes[4] = { (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24) };

	mixed_coop_writer_bytes(writer, bytes, 4);
}

uint32_t mixed_coop_writer_end(struct mixed_coop_dictionary_writer *writer)
{
	uint32_t check;

	if (writer->overflow)
		return 0;
	check = mixed_coop_fnv(MIXED_COOP_FNV_BASIS, writer->bytes, writer->length);
	mixed_coop_writer_u32(writer, check);
	return writer->overflow ? 0 : writer->length;
}

uint32_t mixed_coop_read_u32(uint8_t const *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint16_t mixed_coop_read_u16(uint8_t const *bytes)
{
	return (uint16_t)(bytes[0] | bytes[1] << 8);
}

static int mixed_coop_refuse(char *why, int why_size, char const *text)
{
	if (why && why_size > 0)
		snprintf(why, (size_t)why_size, "%s", text);
	return 0;
}

int mixed_coop_dictionary_read(uint8_t const *bytes, uint32_t length, struct mixed_coop_dictionary *dictionary,
	char *why, int why_size)
{
	uint32_t at = 0, index, graphs_begin;

	memset(dictionary, 0, sizeof(*dictionary));
	if (!bytes || length < 4 + 4 + 4 + 16 + 4 + 4 + 4)
		return mixed_coop_refuse(why, why_size, "too short");
	if (length > MIXED_COOP_DICTIONARY_MAXIMUM_BYTES)
		return mixed_coop_refuse(why, why_size, "too long");
	if (mixed_coop_fnv(MIXED_COOP_FNV_BASIS, bytes, length - 4) != mixed_coop_read_u32(bytes + length - 4))
		return mixed_coop_refuse(why, why_size, "its check does not match");
	length -= 4;
	if (mixed_coop_read_u32(bytes) != MIXED_COOP_DICTIONARY_MAGIC)
		return mixed_coop_refuse(why, why_size, "not a dictionary");
	dictionary->family = bytes[6];
	dictionary->level = bytes[7];
	if (mixed_coop_read_u16(bytes + 4) != MIXED_COOP_VERSION_CAMPAIGN && mixed_coop_read_u16(bytes + 4) != MIXED_COOP_VERSION)
		return mixed_coop_refuse(why, why_size, "another version");
	if ((dictionary->family != _mixed_coop_family_xbox && dictionary->family != _mixed_coop_family_mcc) ||
		!mixed_coop_version_names_level(mixed_coop_read_u16(bytes + 4), dictionary->level))
	{
		return mixed_coop_refuse(why, why_size, "a family or level not known");
	}
	at = 8;
	dictionary->tag_count = mixed_coop_read_u32(bytes + at); at += 4;
	dictionary->digests.layout = mixed_coop_read_u32(bytes + at); at += 4;
	dictionary->digests.gameplay = mixed_coop_read_u32(bytes + at); at += 4;
	dictionary->digests.host_only = mixed_coop_read_u32(bytes + at); at += 4;
	dictionary->digests.layout_offset = mixed_coop_read_u32(bytes + at); at += 4;
	dictionary->digests.tag_count = dictionary->tag_count;
	if (dictionary->tag_count == 0 || dictionary->tag_count > MIXED_COOP_MAXIMUM_TAGS)
		return mixed_coop_refuse(why, why_size, "a tag count past the limit");
	if (length - at < 4)
		return mixed_coop_refuse(why, why_size, "cut short (BSPs)");
	dictionary->bsp_count = mixed_coop_read_u16(bytes + at);
	if (mixed_coop_read_u16(bytes + at + 2) != 0)
		return mixed_coop_refuse(why, why_size, "reserved bits");
	at += 4;
	if (dictionary->bsp_count > MIXED_COOP_MAXIMUM_BSPS)
		return mixed_coop_refuse(why, why_size, "a BSP count past the limit");
	if ((uint32_t)dictionary->bsp_count * 8 > length - at)
		return mixed_coop_refuse(why, why_size, "cut short (BSP digests)");
	for (index = 0; index < dictionary->bsp_count; index++, at += 8)
	{
		dictionary->bsp_digests[index][0] = mixed_coop_read_u32(bytes + at);
		dictionary->bsp_digests[index][1] = mixed_coop_read_u32(bytes + at + 4);
	}
	if (dictionary->tag_count > (length - at) / 12)
		return mixed_coop_refuse(why, why_size, "cut short (tags)");
	dictionary->tags = bytes + at;
	at += dictionary->tag_count * 12;
	if (length - at < 4)
		return mixed_coop_refuse(why, why_size, "cut short (graphs)");
	dictionary->graph_count = mixed_coop_read_u32(bytes + at);
	at += 4;
	if (dictionary->graph_count > MIXED_COOP_MAXIMUM_GRAPHS)
		return mixed_coop_refuse(why, why_size, "a graph count past the limit");
	graphs_begin = at;
	/* (every graph walked once here, so a reader's walk later never leaves
	the bytes) */
	for (index = 0; index < dictionary->graph_count; index++)
	{
		uint16_t host_tag_index, count;

		if (length - at < 4)
			return mixed_coop_refuse(why, why_size, "cut short (a graph)");
		host_tag_index = mixed_coop_read_u16(bytes + at);
		count = mixed_coop_read_u16(bytes + at + 2);
		at += 4;
		if (host_tag_index >= dictionary->tag_count || count > MIXED_COOP_MAXIMUM_ANIMATIONS)
			return mixed_coop_refuse(why, why_size, "a graph past the limits");
		if ((uint32_t)count * 4 > length - at)
			return mixed_coop_refuse(why, why_size, "cut short (a graph's animations)");
		at += (uint32_t)count * 4;
	}
	dictionary->graphs = bytes + graphs_begin;
	dictionary->graphs_bytes = at - graphs_begin;
	dictionary->version = mixed_coop_read_u16(bytes + 4);
	dictionary->netgame_flag_count = 0;
	dictionary->netgame_flags = NULL;
	if (dictionary->version == MIXED_COOP_VERSION)
	{
		if (length - at < 4)
			return mixed_coop_refuse(why, why_size, "cut short (netgame flags)");
		dictionary->netgame_flag_count = mixed_coop_read_u16(bytes + at);
		if (mixed_coop_read_u16(bytes + at + 2) != 0)
			return mixed_coop_refuse(why, why_size, "reserved bits (netgame flags)");
		at += 4;
		if (dictionary->netgame_flag_count > MIXED_COOP_MAXIMUM_NETGAME_FLAGS)
			return mixed_coop_refuse(why, why_size, "a netgame flag count past the limit");
		if (dictionary->netgame_flag_count * MIXED_COOP_NETGAME_FLAG_BYTES > length - at)
			return mixed_coop_refuse(why, why_size, "cut short (a netgame flag)");
		dictionary->netgame_flags = bytes + at;
		for (index = 0; index < dictionary->netgame_flag_count; index++, at += MIXED_COOP_NETGAME_FLAG_BYTES)
		{
			int value;

			for (value = 0; value < 4; value++)
			{
				uint32_t word = mixed_coop_read_u32(bytes + at + 4 * value);
				float number;

				memcpy(&number, &word, sizeof(number));
				/* (finite and near: NaN fails both) */
				if (!(number >= -MIXED_COOP_NETGAME_FLAG_LIMIT && number <= MIXED_COOP_NETGAME_FLAG_LIMIT))
					return mixed_coop_refuse(why, why_size, "a netgame flag's place past the limits");
			}
		}
	}
	if (at != length)
		return mixed_coop_refuse(why, why_size, "bytes past its end");
	return 1;
}

void mixed_coop_dictionary_tag_get(struct mixed_coop_dictionary const *dictionary, uint32_t index,
	struct mixed_coop_dictionary_tag *tag)
{
	if (!dictionary->tags || index >= dictionary->tag_count)
	{
		tag->path_hash = 0;
		tag->digest = 0;
		tag->datum = 0xFFFFFFFFu;
		return;
	}
	tag->path_hash = mixed_coop_read_u32(dictionary->tags + index * 12);
	tag->digest = mixed_coop_read_u32(dictionary->tags + index * 12 + 4);
	tag->datum = mixed_coop_read_u32(dictionary->tags + index * 12 + 8);
}

int mixed_coop_dictionary_graph_next(struct mixed_coop_dictionary const *dictionary, uint32_t *offset,
	uint16_t *host_tag_index, uint16_t *animation_count, uint8_t const **name_hashes)
{
	uint32_t at = *offset;

	/* (the walk was checked by mixed_coop_dictionary_read; these bounds
	again for a dictionary not read by it) */
	if (!dictionary->graphs || at > dictionary->graphs_bytes || dictionary->graphs_bytes - at < 4)
		return 0;
	*host_tag_index = mixed_coop_read_u16(dictionary->graphs + at);
	*animation_count = mixed_coop_read_u16(dictionary->graphs + at + 2);
	at += 4;
	if ((uint32_t)*animation_count * 4 > dictionary->graphs_bytes - at)
		return 0;
	*name_hashes = dictionary->graphs + at;
	*offset = at + (uint32_t)*animation_count * 4;
	return 1;
}

int mixed_coop_dictionary_netgame_flag_get(struct mixed_coop_dictionary const *dictionary, uint32_t index,
	struct mixed_coop_netgame_flag *flag)
{
	uint8_t const *at;
	int value;

	if (!dictionary->netgame_flags || index >= dictionary->netgame_flag_count)
		return 0;
	at = dictionary->netgame_flags + index * MIXED_COOP_NETGAME_FLAG_BYTES;
	for (value = 0; value < 4; value++)
	{
		uint32_t word = mixed_coop_read_u32(at + 4 * value);

		memcpy(value < 3 ? &flag->position[value] : &flag->facing, &word, sizeof(word));
	}
	flag->type = mixed_coop_read_u16(at + 16);
	flag->team = mixed_coop_read_u16(at + 18);
	return 1;
}

/* ---------- the host's layout (r4) */

void mixed_coop_layout_reassembly_begin(struct mixed_coop_layout_reassembly *reassembly, uint32_t session, uint8_t level)
{
	reassembly->session = session;
	reassembly->level = level;
	reassembly->total_bytes = 0;
	reassembly->count = 0;
	reassembly->received = 0;
	memset(reassembly->have, 0, sizeof(reassembly->have));
}

int mixed_coop_layout_reassembly_take(struct mixed_coop_layout_reassembly *reassembly,
	struct mixed_coop_layout_message const *message)
{
	uint32_t expected_count, offset;

	if (!message || message->session != reassembly->session || message->level != reassembly->level ||
		message->version != MIXED_COOP_LAYOUT_VERSION)
	{
		return _mixed_coop_fragment_refused;
	}
	if (message->total_bytes == 0 || message->total_bytes > MIXED_COOP_LAYOUT_MAXIMUM_BYTES)
		return _mixed_coop_fragment_refused;
	expected_count = (message->total_bytes + MIXED_COOP_LAYOUT_FRAGMENT_BYTES - 1) / MIXED_COOP_LAYOUT_FRAGMENT_BYTES;
	if (message->count != expected_count || message->index >= message->count)
		return _mixed_coop_fragment_refused;
	offset = (uint32_t)message->index * MIXED_COOP_LAYOUT_FRAGMENT_BYTES;
	if (message->bytes != (message->index + 1u < message->count ? MIXED_COOP_LAYOUT_FRAGMENT_BYTES :
		message->total_bytes - offset))
	{
		return _mixed_coop_fragment_refused;
	}
	if (reassembly->received == 0)
	{
		reassembly->total_bytes = message->total_bytes;
		reassembly->count = message->count;
	}
	else if (reassembly->total_bytes != message->total_bytes || reassembly->count != message->count)
	{
		return _mixed_coop_fragment_refused;
	}
	if (reassembly->received >= reassembly->count)
		return _mixed_coop_fragment_refused;
	if (reassembly->have[message->index])
		return _mixed_coop_fragment_refused;
	memcpy(reassembly->bytes + offset, message->data, message->bytes);
	reassembly->have[message->index] = 1;
	reassembly->received++;
	return reassembly->received == reassembly->count ? _mixed_coop_fragment_complete : _mixed_coop_fragment_taken;
}

static int mixed_coop_layout_float_ok(uint8_t const *at)
{
	uint32_t word = mixed_coop_read_u32(at);
	float number;

	memcpy(&number, &word, sizeof(number));
	/* (finite and near: NaN fails both) */
	return number >= -MIXED_COOP_NETGAME_FLAG_LIMIT && number <= MIXED_COOP_NETGAME_FLAG_LIMIT;
}

int mixed_coop_layout_read(uint8_t const *bytes, uint32_t length, struct mixed_coop_layout *layout, char *why, int why_size)
{
	uint32_t at, block, blocks;

	memset(layout, 0, sizeof(*layout));
	if (!bytes || length < 8 + 4)
		return mixed_coop_refuse(why, why_size, "too short");
	if (length > MIXED_COOP_LAYOUT_MAXIMUM_BYTES)
		return mixed_coop_refuse(why, why_size, "too long");
	if (mixed_coop_fnv(MIXED_COOP_FNV_BASIS, bytes, length - 4) != mixed_coop_read_u32(bytes + length - 4))
		return mixed_coop_refuse(why, why_size, "its check does not match");
	length -= 4;
	if (mixed_coop_read_u32(bytes) != MIXED_COOP_LAYOUT_MAGIC)
		return mixed_coop_refuse(why, why_size, "not a layout");
	if (mixed_coop_read_u16(bytes + 4) != MIXED_COOP_LAYOUT_VERSION)
		return mixed_coop_refuse(why, why_size, "another version");
	layout->level = bytes[6];
	if (layout->level < MIXED_COOP_CAMPAIGN_LEVELS || layout->level >= MIXED_COOP_LEVELS)
		return mixed_coop_refuse(why, why_size, "a level not known");
	blocks = bytes[7];
	if (blocks == 0 || blocks >= NUMBER_OF_MIXED_COOP_LAYOUT_BLOCKS)
		return mixed_coop_refuse(why, why_size, "a block count past the limit");
	at = 8;
	for (block = 0; block < blocks; block++)
	{
		uint8_t kind;
		uint16_t element_bytes;
		uint32_t count, index, maximum;

		if (length - at < 8)
			return mixed_coop_refuse(why, why_size, "cut short (a block)");
		kind = bytes[at];
		element_bytes = mixed_coop_read_u16(bytes + at + 2);
		count = mixed_coop_read_u32(bytes + at + 4);
		if (kind == 0 || kind >= NUMBER_OF_MIXED_COOP_LAYOUT_BLOCKS || bytes[at + 1] != 0)
			return mixed_coop_refuse(why, why_size, "a block not known");
		if (layout->blocks[kind].elements)
			return mixed_coop_refuse(why, why_size, "a block twice");
		at += 8;
		switch (kind)
		{
		case MIXED_COOP_LAYOUT_SCENERY_PALETTE:
		case MIXED_COOP_LAYOUT_VEHICLE_PALETTE:
			if (element_bytes != MIXED_COOP_LAYOUT_PALETTE_BYTES)
				return mixed_coop_refuse(why, why_size, "a palette's element size");
			maximum = MIXED_COOP_LAYOUT_MAXIMUM_PALETTE;
			break;
		case MIXED_COOP_LAYOUT_NETGAME_FLAGS:
			if (element_bytes != MIXED_COOP_NETGAME_FLAG_BYTES)
				return mixed_coop_refuse(why, why_size, "a netgame flag's size");
			maximum = MIXED_COOP_MAXIMUM_NETGAME_FLAGS;
			break;
		default:
			if (element_bytes < MIXED_COOP_LAYOUT_MINIMUM_PLACEMENT_BYTES || element_bytes > MIXED_COOP_LAYOUT_MAXIMUM_PLACEMENT_BYTES)
				return mixed_coop_refuse(why, why_size, "a placement's size");
			maximum = MIXED_COOP_LAYOUT_MAXIMUM_PLACEMENTS;
			break;
		}
		if (count > maximum)
			return mixed_coop_refuse(why, why_size, "a count past the limit");
		if (count > (length - at) / element_bytes)
			return mixed_coop_refuse(why, why_size, "cut short (elements)");
		for (index = 0; index < count; index++)
		{
			uint8_t const *element = bytes + at + index * element_bytes;
			int value;

			/* (a flag's place and facing; a placement's place and rotation, at 0x8: struct
			scenario_object_datum) */
			if (kind == MIXED_COOP_LAYOUT_NETGAME_FLAGS)
			{
				for (value = 0; value < 4; value++)
					if (!mixed_coop_layout_float_ok(element + 4 * value))
						return mixed_coop_refuse(why, why_size, "a netgame flag's place past the limits");
			}
			else if (kind == MIXED_COOP_LAYOUT_SCENERY_PALETTE || kind == MIXED_COOP_LAYOUT_VEHICLE_PALETTE)
			{
				/* (its path ends within its bytes) */
				if (element[MIXED_COOP_LAYOUT_PALETTE_BYTES - 1] != 0)
					return mixed_coop_refuse(why, why_size, "a palette's path not ended");
			}
			else if (kind == MIXED_COOP_LAYOUT_SCENERY || kind == MIXED_COOP_LAYOUT_VEHICLES)
			{
				for (value = 0; value < 6; value++)
					if (!mixed_coop_layout_float_ok(element + 8 + 4 * value))
						return mixed_coop_refuse(why, why_size, "a placement's place past the limits");
			}
		}
		layout->blocks[kind].count = count;
		layout->blocks[kind].element_bytes = element_bytes;
		layout->blocks[kind].elements = bytes + at;
		at += count * element_bytes;
	}
	if (at != length)
		return mixed_coop_refuse(why, why_size, "bytes past its end");
	return 1;
}
