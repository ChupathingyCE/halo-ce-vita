/*
STEAM_CHUNK.C

steam_chunk.h: decrypt, decompress, check.
*/

#include "steam_chunk.h"

#include "steam_crypto.h"
#include "steam_limits.h"
#include "steam_zip.h"

#include "LzmaDec.h"
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void steam_chunk_work_init(struct steam_chunk_work *work)
{
	memset(work, 0, sizeof(*work));
}

void steam_chunk_work_free(struct steam_chunk_work *work)
{
	if (work->plain)
	{
		steam_wipe(work->plain, work->plain_capacity);
		free(work->plain);
	}
	if (work->zstd)
		ZSTD_freeDCtx(work->zstd);
	memset(work, 0, sizeof(*work));
}

static void *lzma_alloc(ISzAllocPtr unused, size_t size)
{
	(void)unused;
	return malloc(size);
}

static void lzma_free(ISzAllocPtr unused, void *address)
{
	(void)unused;
	free(address);
}

static const ISzAlloc lzma_allocator = { lzma_alloc, lzma_free };

/* "VZa": 'V' 'Z' 'a', 4 bytes (a CRC or a time), 5 bytes of properties, the
LZMA stream, then the CRC-32 of the output, its size, 'z' 'v' */
static int decompress_vzip(const uint8_t *plain, size_t size, uint8_t *output, size_t output_size, char *error,
	int error_size)
{
	const uint8_t *footer, *properties;
	SizeT written, consumed;
	ELzmaStatus status;
	unsigned int lc, lp, pb, value;
	SRes result;

	if (size < 7 + LZMA_PROPS_SIZE + 10)
	{
		snprintf(error, (size_t)error_size, "VZa chunk too short");
		return 0;
	}
	footer = plain + size - 10;
	if (footer[8] != 'z' || footer[9] != 'v' || le32(footer + 4) != output_size)
	{
		snprintf(error, (size_t)error_size, "VZa footer bad or wrong size");
		return 0;
	}
	properties = plain + 7;
	/* (lc + lp bounds the decoder's tables: Steam's are lc 3, lp 0) */
	value = properties[0];
	if (value >= 9 * 5 * 5)
		goto bad_properties;
	lc = value % 9;
	value /= 9;
	lp = value % 5;
	pb = value / 5;
	if (lc + lp > 8 || pb > 4)
		goto bad_properties;
	written = output_size;
	consumed = size - 7 - LZMA_PROPS_SIZE - 10;
	result = LzmaDecode(output, &written, plain + 7 + LZMA_PROPS_SIZE, &consumed, properties, LZMA_PROPS_SIZE,
		LZMA_FINISH_ANY, &status, &lzma_allocator);
	if (result != SZ_OK || written != output_size)
	{
		snprintf(error, (size_t)error_size, "VZa data does not decode to its size");
		return 0;
	}
	if (steam_crc32(0, output, output_size) != le32(footer))
	{
		snprintf(error, (size_t)error_size, "VZa CRC-32 does not match");
		return 0;
	}
	return 1;
bad_properties:
	snprintf(error, (size_t)error_size, "VZa properties out of range");
	return 0;
}

/* "VSZa": the magic, a CRC-32, the zstd frame, then the CRC-32 again, the
output's size, 4 bytes, 'z' 's' 'v' */
static int decompress_vzstd(struct steam_chunk_work *work, const uint8_t *plain, size_t size, uint8_t *output,
	size_t output_size, char *error, int error_size)
{
	const uint8_t *footer;
	size_t written;

	if (size < 8 + 15)
	{
		snprintf(error, (size_t)error_size, "VSZa chunk too short");
		return 0;
	}
	footer = plain + size - 15;
	if (footer[12] != 'z' || footer[13] != 's' || footer[14] != 'v' || le32(footer + 4) != output_size)
	{
		snprintf(error, (size_t)error_size, "VSZa footer bad or wrong size");
		return 0;
	}
	if (!work->zstd)
	{
		work->zstd = ZSTD_createDCtx();
		if (!work->zstd)
		{
			snprintf(error, (size_t)error_size, "out of memory");
			return 0;
		}
		/* (the window a frame may ask for: Steam's chunks are 1 MB) */
		ZSTD_DCtx_setParameter(work->zstd, ZSTD_d_windowLogMax, 24);
	}
	written = ZSTD_decompressDCtx(work->zstd, output, output_size, plain + 8, size - 8 - 15);
	if (ZSTD_isError(written) || written != output_size)
	{
		/* (a failed frame leaves the context as it was: reset it anyway) */
		ZSTD_DCtx_reset(work->zstd, ZSTD_reset_session_only);
		snprintf(error, (size_t)error_size, "VSZa data does not decode to its size");
		return 0;
	}
	if (steam_crc32(0, output, output_size) != le32(footer))
	{
		snprintf(error, (size_t)error_size, "VSZa CRC-32 does not match");
		return 0;
	}
	return 1;
}

static uint64_t now_us(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
}

static int decompress_any(struct steam_chunk_work *work, const uint8_t *plain, size_t plain_size, uint8_t *output,
	size_t output_size, char *error, int error_size);

int steam_chunk_decompress(struct steam_chunk_work *work, const uint8_t *plain, size_t plain_size, uint8_t *output,
	size_t output_size, char *error, int error_size)
{
	uint64_t started = now_us();
	int kind = plain_size >= 4 && !memcmp(plain, "VSZa", 4) ? 1 : plain_size >= 3 && !memcmp(plain, "VZa", 3) ? 0 : 2;
	int ok = decompress_any(work, plain, plain_size, output, output_size, error, error_size);

	if (ok)
	{
		work->decode_us[kind] += now_us() - started;
		work->decoded_bytes[kind] += output_size;
		work->decoded_chunks[kind]++;
	}
	return ok;
}

static int decompress_any(struct steam_chunk_work *work, const uint8_t *plain, size_t plain_size, uint8_t *output,
	size_t output_size, char *error, int error_size)
{
	error[0] = 0;
	if (!output_size || output_size > STEAM_CHUNK_MAXIMUM || plain_size < 4)
	{
		snprintf(error, (size_t)error_size, "chunk size out of range");
		return 0;
	}
	if (!memcmp(plain, "VSZa", 4))
		return decompress_vzstd(work, plain, plain_size, output, output_size, error, error_size);
	if (!memcmp(plain, "VZa", 3))
		return decompress_vzip(plain, plain_size, output, output_size, error, error_size);
	if (!memcmp(plain, "PK\3\4", 4))
	{
		size_t entry;

		if (!steam_zip_entry_size(plain, plain_size, STEAM_CHUNK_MAXIMUM, &entry) || entry != output_size)
		{
			snprintf(error, (size_t)error_size, "zip chunk is not one file of its size");
			return 0;
		}
		return steam_zip_extract(plain, plain_size, output, output_size, error, error_size);
	}
	snprintf(error, (size_t)error_size, "unknown chunk compression");
	return 0;
}

int steam_chunk_process(struct steam_chunk_work *work, const uint8_t key[32], const struct steam_chunk_info *info,
	const uint8_t *data, size_t size, uint8_t *output, char *error, int error_size)
{
	uint8_t digest[20];
	long plain_size;

	error[0] = 0;
	if (!info->size || info->size > STEAM_CHUNK_MAXIMUM || size < 32 || size > STEAM_CHUNK_COMPRESSED_MAXIMUM ||
		size % 16)
	{
		snprintf(error, (size_t)error_size, "chunk size out of range");
		return 0;
	}
	if (work->plain_capacity < size)
	{
		uint8_t *grown = malloc(size);

		if (!grown)
		{
			snprintf(error, (size_t)error_size, "out of memory");
			return 0;
		}
		if (work->plain)
		{
			steam_wipe(work->plain, work->plain_capacity);
			free(work->plain);
		}
		work->plain = grown;
		work->plain_capacity = size;
		if (size > work->peak_plain)
			work->peak_plain = size;
	}
	plain_size = steam_symmetric_decrypt(key, data, size, work->plain);
	if (plain_size < 16)
	{
		snprintf(error, (size_t)error_size, "chunk does not decrypt (wrong key or damaged)");
		return 0;
	}
	if (!steam_chunk_decompress(work, work->plain, (size_t)plain_size, output, info->size, error, error_size))
		return 0;
	if (steam_adler32_zero(output, info->size) != info->checksum)
	{
		snprintf(error, (size_t)error_size, "chunk checksum does not match the manifest");
		return 0;
	}
	steam_sha1(output, info->size, digest);
	if (!steam_equal(digest, info->sha, 20))
	{
		snprintf(error, (size_t)error_size, "chunk SHA-1 does not match its id");
		return 0;
	}
	return 1;
}
