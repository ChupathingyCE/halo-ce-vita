/*
STEAM_CHUNK.H

One depot chunk as the CDN sends it (GET /depot/<depot>/chunk/<sha hex>):
decrypted (Steam's symmetric AES-256 under the depot key), decompressed by
its plaintext's magic - "VZa" (Valve's LZMA: a 7-byte header, 5 bytes of
LZMA properties, the stream, a 10-byte footer: CRC-32, size, "zv"), "VSZa"
(Valve's zstd: an 8-byte header, the frame, a 15-byte footer: CRC-32, size,
4 bytes, "zsv") or "PK\3\4" (a zip of one file) - into exactly the
manifest's cb_original bytes, and checked: the container's CRC-32 and size,
the manifest's Adler-32 (from 0), and the SHA-1, which is the chunk's id.
Only a chunk that passes all of them is handed back to be written
(docs/steam_download_security.md 3.6).

The LZMA decoder is the LZMA SDK's (port/third_party/lzma, public domain),
zstd's is the reference decoder alone (port/third_party/zstd, BSD); both
write into the exact output buffer and never past it.
*/

#ifndef STEAM_CHUNK_H
#define STEAM_CHUNK_H

#include "steam_manifest.h"

#include <stddef.h>
#include <stdint.h>

/* buffers one download thread reuses chunk after chunk */
struct steam_chunk_work
{
	uint8_t *plain;
	size_t plain_capacity;
	void *zstd;
	/* the decoders' time (microseconds), output and chunks, by container: 0 VZa, 1 VSZa, 2 zip; the largest
	plaintext buffer */
	uint64_t decode_us[3], decoded_bytes[3];
	unsigned int decoded_chunks[3];
	size_t peak_plain;
};

void steam_chunk_work_init(struct steam_chunk_work *work);
void steam_chunk_work_free(struct steam_chunk_work *work);

/* data (size bytes, as the CDN sent them) into output (info->size bytes),
checked against info: 1, or 0 with a reason (nothing in output is to be
used then) */
int steam_chunk_process(struct steam_chunk_work *work, const uint8_t key[32], const struct steam_chunk_info *info,
	const uint8_t *data, size_t size, uint8_t *output, char *error, int error_size);

/* the decompression alone (a decrypted chunk's plaintext into exactly
output_size bytes, its container's CRC-32 and size checked): 1, or 0 */
int steam_chunk_decompress(struct steam_chunk_work *work, const uint8_t *plain, size_t plain_size, uint8_t *output,
	size_t output_size, char *error, int error_size);

#endif
