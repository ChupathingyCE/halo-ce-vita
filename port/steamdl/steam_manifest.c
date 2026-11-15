/*
STEAM_MANIFEST.C

steam_manifest.h: a depot manifest's sections, metadata and files.
*/

#include "steam_manifest.h"

#include "steam_crypto.h"
#include "steam_limits.h"
#include "steam_pb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

struct sections
{
	const uint8_t *payload, *metadata;
	size_t payload_size, metadata_size;
};

static int find_sections(const uint8_t *data, size_t size, struct sections *found, char *error, int error_size)
{
	size_t cursor = 0;
	int count = 0, ended = 0;

	memset(found, 0, sizeof(*found));
	while (cursor < size)
	{
		uint32_t magic, length;

		if (size - cursor < 4)
			break;
		magic = le32(data + cursor);
		cursor += 4;
		if (magic == STEAM_MANIFEST_END)
		{
			ended = 1;
			break;
		}
		if (size - cursor < 4 || ++count > 16)
			break;
		length = le32(data + cursor);
		cursor += 4;
		if (length > size - cursor)
		{
			snprintf(error, (size_t)error_size, "a manifest section runs past the end");
			return 0;
		}
		if (magic == STEAM_MANIFEST_PAYLOAD && !found->payload)
			found->payload = data + cursor, found->payload_size = length;
		else if (magic == STEAM_MANIFEST_METADATA && !found->metadata)
			found->metadata = data + cursor, found->metadata_size = length;
		else if (magic != STEAM_MANIFEST_SIGNATURE)
		{
			snprintf(error, (size_t)error_size, "an unknown manifest section (%08x)", (unsigned int)magic);
			return 0;
		}
		cursor += length;
	}
	if (!ended || !found->payload || !found->metadata)
	{
		snprintf(error, (size_t)error_size, "not a whole manifest (no payload, metadata or end)");
		return 0;
	}
	return 1;
}

static int parse_metadata(const uint8_t *data, size_t size, struct steam_manifest_metadata *metadata)
{
	struct steam_pb_reader reader;
	struct steam_pb_field field;
	int result;

	memset(metadata, 0, sizeof(*metadata));
	steam_pb_reader_init(&reader, data, size);
	while ((result = steam_pb_next(&reader, &field)) > 0)
	{
		if (field.wire == STEAM_PB_BYTES)
			continue;
		switch (field.number)
		{
		case 1: metadata->depot_id = (uint32_t)field.value; break;
		case 2: metadata->gid = field.value; break;
		case 3: metadata->creation_time = (uint32_t)field.value; break;
		case 4: metadata->filenames_encrypted = field.value != 0; break;
		case 5: metadata->original_size = field.value; break;
		case 6: metadata->compressed_size = field.value; break;
		case 7: metadata->unique_chunks = (uint32_t)field.value; break;
		case 8: metadata->crc_encrypted = (uint32_t)field.value; break;
		case 9: metadata->crc_clear = (uint32_t)field.value; break;
		}
	}
	return result == 0;
}

struct parser
{
	const uint8_t *key;
	int encrypted;
	struct steam_chunk_info *chunks;
	uint32_t chunk_capacity;
	uint32_t total_files, total_chunks;
	char *error;
	int error_size;
};

static int chunk_parse(const uint8_t *data, size_t size, struct steam_chunk_info *chunk)
{
	struct steam_pb_reader reader;
	struct steam_pb_field field;
	int result, have_sha = 0;

	memset(chunk, 0, sizeof(*chunk));
	steam_pb_reader_init(&reader, data, size);
	while ((result = steam_pb_next(&reader, &field)) > 0)
	{
		switch (field.number)
		{
		case 1:
			if (field.wire != STEAM_PB_BYTES || field.size != 20)
				return 0;
			memcpy(chunk->sha, field.data, 20);
			have_sha = 1;
			break;
		case 2: chunk->checksum = (uint32_t)field.value; break;
		case 3: chunk->offset = field.value; break;
		case 4:
			if (field.value > 0xffffffffu)
				return 0;
			chunk->size = (uint32_t)field.value;
			break;
		case 5:
			if (field.value > 0xffffffffu)
				return 0;
			chunk->compressed_size = (uint32_t)field.value;
			break;
		}
	}
	return result == 0 && have_sha && chunk->size && chunk->size <= STEAM_CHUNK_MAXIMUM && chunk->compressed_size &&
		chunk->compressed_size <= STEAM_CHUNK_COMPRESSED_MAXIMUM;
}

static int chunk_compare(const void *a, const void *b)
{
	const struct steam_chunk_info *x = a, *y = b;

	return x->offset < y->offset ? -1 : x->offset > y->offset;
}

/* the name, decrypted if it is, '\' turned '/': 1, or 0 */
static int file_name(struct parser *parser, const uint8_t *data, size_t size, char *name, size_t capacity)
{
	size_t index, length;

	if (parser->encrypted)
	{
		uint8_t raw[STEAM_NAME_ENCRYPTED_MAXIMUM], plain[STEAM_NAME_ENCRYPTED_MAXIMUM];
		long raw_size, plain_size;

		if (!parser->key || size > STEAM_NAME_ENCRYPTED_MAXIMUM)
			return 0;
		raw_size = steam_base64_decode((const char *)data, size, raw, sizeof(raw));
		if (raw_size < 32)
			return 0;
		plain_size = steam_symmetric_decrypt(parser->key, raw, (size_t)raw_size, plain);
		if (plain_size < 0)
			return 0;
		/* (the name, then NULs) */
		length = (size_t)plain_size;
		while (length && !plain[length - 1])
			length--;
		if (length >= capacity)
			return 0;
		memcpy(name, plain, length);
		steam_wipe(plain, sizeof(plain));
	}
	else
	{
		length = size;
		while (length && !data[length - 1])
			length--;
		if (length >= capacity)
			return 0;
		memcpy(name, data, length);
	}
	name[length] = 0;
	for (index = 0; index < length; index++)
	{
		if (!name[index])
			return 0;
		if (name[index] == '\\')
			name[index] = '/';
	}
	return 1;
}

static int name_hash_matches(const char *name, const uint8_t *expected)
{
	char lowered[STEAM_NAME_MAXIMUM + 1];
	uint8_t digest[20];
	size_t index;

	for (index = 0; name[index] && index < STEAM_NAME_MAXIMUM; index++)
	{
		char c = name[index];

		lowered[index] = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c == '/' ? '\\' : c;
	}
	steam_sha1(lowered, index, digest);
	return steam_equal(digest, expected, 20);
}

static int file_parse(struct parser *parser, const uint8_t *data, size_t size, steam_manifest_visitor visitor,
	void *context)
{
	static const char *const why_chunk = "a chunk of a file is malformed";
	struct steam_manifest_file *file;
	struct steam_pb_reader reader;
	struct steam_pb_field field;
	const uint8_t *name_data = NULL, *name_sha = NULL;
	size_t name_size = 0;
	uint32_t index;
	uint64_t covered;
	int result, have_name = 0;

	file = calloc(1, sizeof(*file));
	if (!file)
	{
		snprintf(parser->error, (size_t)parser->error_size, "out of memory");
		return -1;
	}
	steam_pb_reader_init(&reader, data, size);
	while ((result = steam_pb_next(&reader, &field)) > 0)
	{
		switch (field.number)
		{
		case 1:
			if (field.wire != STEAM_PB_BYTES)
				goto bad;
			name_data = field.data, name_size = field.size, have_name = 1;
			break;
		case 2: file->size = field.value; break;
		case 3: file->flags = (uint32_t)field.value; break;
		case 4:
			if (field.wire != STEAM_PB_BYTES || field.size != 20)
				goto bad;
			name_sha = field.data;
			break;
		case 5:
			if (field.wire != STEAM_PB_BYTES || (field.size != 20 && field.size != 0))
				goto bad;
			if (field.size == 20)
			{
				memcpy(file->sha_content, field.data, 20);
				file->has_sha_content = 1;
			}
			break;
		case 6:
			if (field.wire != STEAM_PB_BYTES)
				goto bad;
			if (file->chunk_count >= STEAM_FILE_CHUNKS_MAXIMUM || parser->total_chunks >= STEAM_MANIFEST_CHUNKS_MAXIMUM)
			{
				snprintf(parser->error, (size_t)parser->error_size, "too many chunks");
				goto failed;
			}
			if (file->chunk_count == parser->chunk_capacity)
			{
				uint32_t capacity = parser->chunk_capacity ? parser->chunk_capacity * 2 : 256;
				struct steam_chunk_info *grown;

				if (capacity > STEAM_FILE_CHUNKS_MAXIMUM)
					capacity = STEAM_FILE_CHUNKS_MAXIMUM;
				grown = realloc(parser->chunks, capacity * sizeof(*grown));
				if (!grown)
				{
					snprintf(parser->error, (size_t)parser->error_size, "out of memory");
					goto failed;
				}
				parser->chunks = grown;
				parser->chunk_capacity = capacity;
			}
			if (!chunk_parse(field.data, field.size, &parser->chunks[file->chunk_count]))
			{
				snprintf(parser->error, (size_t)parser->error_size, "%s", why_chunk);
				goto failed;
			}
			file->compressed_size += parser->chunks[file->chunk_count].compressed_size;
			file->chunk_count++;
			parser->total_chunks++;
			break;
		}
	}
	if (result < 0 || !have_name)
		goto bad;
	if (!file_name(parser, name_data, name_size, file->name, sizeof(file->name)))
	{
		snprintf(parser->error, (size_t)parser->error_size, "a file name does not decode");
		goto failed;
	}
	if (name_sha)
	{
		file->name_hash_checked = 1;
		file->name_hash_ok = name_hash_matches(file->name, name_sha);
	}
	/* the chunks: inside the file, in order, not overlapping */
	if (file->chunk_count > 1)
		qsort(parser->chunks, file->chunk_count, sizeof(*parser->chunks), chunk_compare);
	covered = 0;
	for (index = 0; index < file->chunk_count; index++)
	{
		const struct steam_chunk_info *chunk = &parser->chunks[index];

		if (chunk->offset > file->size || chunk->size > file->size - chunk->offset ||
			(index && chunk->offset < parser->chunks[index - 1].offset + parser->chunks[index - 1].size))
		{
			snprintf(parser->error, (size_t)parser->error_size, "%s", why_chunk);
			goto failed;
		}
		covered += chunk->size;
	}
	file->covered = covered == file->size;
	file->chunks = parser->chunks;
	result = visitor ? visitor(context, file) : 0;
	free(file);
	return result;
bad:
	snprintf(parser->error, (size_t)parser->error_size, "a file entry is malformed");
failed:
	free(file);
	return -1;
}

int steam_manifest_parse(const uint8_t *data, size_t size, const uint8_t *depot_key,
	struct steam_manifest_metadata *metadata, steam_manifest_visitor visitor, void *context, char *error,
	int error_size)
{
	struct sections sections;
	struct parser parser;
	struct steam_pb_reader reader;
	struct steam_pb_field field;
	int result;

	error[0] = 0;
	memset(metadata, 0, sizeof(*metadata));
	if (!data || size > STEAM_MANIFEST_MAXIMUM)
	{
		snprintf(error, (size_t)error_size, "manifest too large");
		return -1;
	}
	if (!find_sections(data, size, &sections, error, error_size))
		return -1;
	if (!parse_metadata(sections.metadata, sections.metadata_size, metadata))
	{
		snprintf(error, (size_t)error_size, "the manifest's metadata is malformed");
		return -1;
	}
	if (metadata->filenames_encrypted && !depot_key)
	{
		snprintf(error, (size_t)error_size, "the manifest's names are encrypted and no depot key was given");
		return -1;
	}
	memset(&parser, 0, sizeof(parser));
	parser.key = depot_key;
	parser.encrypted = metadata->filenames_encrypted;
	parser.error = error;
	parser.error_size = error_size;
	steam_pb_reader_init(&reader, sections.payload, sections.payload_size);
	while ((result = steam_pb_next(&reader, &field)) > 0)
	{
		if (field.number != 1)
			continue;
		if (field.wire != STEAM_PB_BYTES)
		{
			result = -1;
			break;
		}
		if (++parser.total_files > STEAM_MANIFEST_FILES_MAXIMUM)
		{
			snprintf(error, (size_t)error_size, "too many files");
			free(parser.chunks);
			return -1;
		}
		result = file_parse(&parser, field.data, field.size, visitor, context);
		if (result != 0)
		{
			free(parser.chunks);
			return result;
		}
	}
	free(parser.chunks);
	if (result < 0)
	{
		snprintf(error, (size_t)error_size, "the manifest's payload is malformed");
		return -1;
	}
	return 0;
}

int steam_manifest_crc_ok(const uint8_t *data, size_t size, const struct steam_manifest_metadata *metadata)
{
	struct sections sections;
	uint8_t length[4];
	char error[96];
	uint32_t crc;

	if (!find_sections(data, size, &sections, error, (int)sizeof(error)))
		return 0;
	length[0] = (uint8_t)sections.payload_size;
	length[1] = (uint8_t)(sections.payload_size >> 8);
	length[2] = (uint8_t)(sections.payload_size >> 16);
	length[3] = (uint8_t)(sections.payload_size >> 24);
	crc = steam_crc32(0, length, 4);
	crc = steam_crc32(crc, sections.payload, sections.payload_size);
	return crc == metadata->crc_clear;
}
