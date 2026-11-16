/*
STEAM_ZIP.C

steam_zip.h: a zip of one file, and gzip, inflated by libdeflate.
*/

#include "steam_zip.h"

#include "steam_crypto.h"

#include "libdeflate.h"

#include <stdio.h>
#include <string.h>

#define ZIP_LOCAL 0x04034b50u
#define ZIP_CENTRAL 0x02014b50u
#define ZIP_END 0x06054b50u
#define ZIP_END_SIZE 22u
#define ZIP_CENTRAL_SIZE 46u
#define ZIP_LOCAL_SIZE 30u

static uint32_t le16(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

struct zip_entry
{
	uint32_t method, crc, compressed, uncompressed;
	size_t data;
};

/* the one entry: 1, or 0 */
static int zip_find(const uint8_t *zip, size_t size, struct zip_entry *entry)
{
	size_t end, search_from, central, local;
	uint32_t central_size, central_offset, flags;

	if (!zip || size < ZIP_END_SIZE + ZIP_LOCAL_SIZE)
		return 0;
	/* the end record: in the last 22 + 65535 bytes (its comment) */
	search_from = size > ZIP_END_SIZE + 0xffff ? size - ZIP_END_SIZE - 0xffff : 0;
	for (end = size - ZIP_END_SIZE;; end--)
	{
		if (le32(zip + end) == ZIP_END && end + ZIP_END_SIZE + le16(zip + end + 20) == size)
			break;
		if (end == search_from)
			return 0;
	}
	/* one disk, one entry */
	if (le16(zip + end + 4) != 0 || le16(zip + end + 6) != 0 || le16(zip + end + 8) != 1 || le16(zip + end + 10) != 1)
		return 0;
	central_size = le32(zip + end + 12);
	central_offset = le32(zip + end + 16);
	if (central_offset > end || central_size > end - central_offset || central_size < ZIP_CENTRAL_SIZE)
		return 0;
	central = central_offset;
	if (le32(zip + central) != ZIP_CENTRAL)
		return 0;
	if (ZIP_CENTRAL_SIZE + (size_t)le16(zip + central + 28) + le16(zip + central + 30) + le16(zip + central + 32) >
		central_size)
	{
		return 0;
	}
	flags = le16(zip + central + 8);
	/* (encrypted entries, and zip64's sizes, are not Steam's) */
	if (flags & 1)
		return 0;
	entry->method = le16(zip + central + 10);
	entry->crc = le32(zip + central + 16);
	entry->compressed = le32(zip + central + 20);
	entry->uncompressed = le32(zip + central + 24);
	if (entry->compressed == 0xffffffffu || entry->uncompressed == 0xffffffffu)
		return 0;
	local = le32(zip + central + 42);
	if (local > central_offset || central_offset - local < ZIP_LOCAL_SIZE || le32(zip + local) != ZIP_LOCAL)
		return 0;
	entry->data = local + ZIP_LOCAL_SIZE + le16(zip + local + 26) + le16(zip + local + 28);
	if (entry->data > central_offset || entry->compressed > central_offset - entry->data)
		return 0;
	if (entry->method != 0 && entry->method != 8)
		return 0;
	if (entry->method == 0 && entry->compressed != entry->uncompressed)
		return 0;
	return 1;
}

int steam_zip_entry_size(const uint8_t *zip, size_t size, size_t maximum, size_t *entry_size)
{
	struct zip_entry entry;

	if (!zip_find(zip, size, &entry) || entry.uncompressed > maximum)
		return 0;
	*entry_size = entry.uncompressed;
	return 1;
}

static int inflate_exact(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *used)
{
	struct libdeflate_decompressor *decompressor;
	enum libdeflate_result result;
	size_t in_used = 0, out_written = 0;

	if (!output_size)
	{
		/* (an empty stream still holds one final block) */
		static uint8_t scratch[1];

		output = scratch;
	}
	decompressor = libdeflate_alloc_decompressor();
	if (!decompressor)
		return 0;
	result = libdeflate_deflate_decompress_ex(decompressor, input, input_size, output, output_size, &in_used,
		&out_written);
	libdeflate_free_decompressor(decompressor);
	if (result != LIBDEFLATE_SUCCESS || out_written != output_size)
		return 0;
	if (used)
		*used = in_used;
	return 1;
}

int steam_zip_extract(const uint8_t *zip, size_t size, uint8_t *output, size_t entry_size, char *error, int error_size)
{
	struct zip_entry entry;

	if (!zip_find(zip, size, &entry) || entry.uncompressed != entry_size)
	{
		snprintf(error, (size_t)error_size, "not a zip of one file");
		return 0;
	}
	if (entry.method == 0)
		memcpy(output, zip + entry.data, entry_size);
	else if (!inflate_exact(zip + entry.data, entry.compressed, output, entry_size, NULL))
	{
		snprintf(error, (size_t)error_size, "the zip's data does not inflate to its size");
		return 0;
	}
	if (steam_crc32(0, output, entry_size) != entry.crc)
	{
		snprintf(error, (size_t)error_size, "the zip's CRC-32 does not match");
		return 0;
	}
	return 1;
}

int steam_gunzip(const uint8_t *data, size_t size, uint8_t *output, size_t output_size)
{
	size_t cursor = 10, used = 0;
	uint32_t flags;

	if (!data || size < 18 || data[0] != 0x1f || data[1] != 0x8b || data[2] != 8)
		return 0;
	flags = data[3];
	if (flags & 0xe0)
		return 0;
	if (flags & 4)
	{
		/* FEXTRA */
		if (size - cursor < 2)
			return 0;
		{
			size_t extra = le16(data + cursor);

			cursor += 2;
			if (extra > size - cursor)
				return 0;
			cursor += extra;
		}
	}
	/* FNAME, FCOMMENT: NUL-terminated */
	for (int bit = 8; bit <= 16; bit <<= 1)
	{
		if (!(flags & (uint32_t)bit))
			continue;
		while (cursor < size && data[cursor])
			cursor++;
		if (cursor >= size)
			return 0;
		cursor++;
	}
	if (flags & 2)
		cursor += 2;
	if (cursor > size || size - cursor < 8)
		return 0;
	if (!inflate_exact(data + cursor, size - cursor - 8, output, output_size, &used))
		return 0;
	cursor += used;
	if (size - cursor < 8)
		return 0;
	if (le32(data + cursor) != steam_crc32(0, output, output_size) || le32(data + cursor + 4) != (uint32_t)output_size)
		return 0;
	return 1;
}
