/*
STEAM_MANIFEST.H

A depot manifest (Steam's binary format, as the CDN sends it inside a zip
and as Steam keeps it in depotcache/<depot>_<gid>.manifest): sections of
<magic u32><length u32><protobuf>, the payload (ContentManifestPayload: the
files and their chunks), the metadata (ContentManifestMetadata), the
signature, then the end magic.

steam_manifest_parse checks every section, field and count against the
data and the caps (steam_limits.h), decrypts the file names when the
metadata says they are encrypted (base64 of Steam's symmetric encryption
under the depot key), turns '\' into '/', and hands each file to a visitor
with its chunks sorted by offset. A chunk outside its file, one of 0 bytes
or over 16 MB, a SHA-1 that is not 20 bytes, overlapping chunks: the
manifest is refused. A file whose chunks do not cover it exactly is passed
with covered = 0 (the download refuses such a file).
*/

#ifndef STEAM_MANIFEST_H
#define STEAM_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#define STEAM_MANIFEST_PAYLOAD 0x71f617d0u
#define STEAM_MANIFEST_METADATA 0x1f4812beu
#define STEAM_MANIFEST_SIGNATURE 0x1b81b817u
#define STEAM_MANIFEST_END 0x32c415abu

/* EDepotFileFlag's */
#define STEAM_FILE_FLAG_DIRECTORY 64u
#define STEAM_FILE_FLAG_SYMLINK 512u

struct steam_chunk_info
{
	uint8_t sha[20];
	uint32_t checksum; /* Adler-32 from 0 of the decompressed data */
	uint64_t offset;
	uint32_t size; /* cb_original */
	uint32_t compressed_size; /* cb_compressed */
};

struct steam_manifest_file
{
	/* '/'-separated, as the depot has it (not a path of ours: steam_plan.c
	only compares it) */
	char name[1024 + 1];
	uint64_t size;
	uint32_t flags;
	uint8_t sha_content[20];
	int has_sha_content;
	/* the name's SHA-1 as the manifest gives it, and whether it matches
	the (decrypted) name's, lowercase with '\' */
	int name_hash_checked, name_hash_ok;
	uint32_t chunk_count;
	const struct steam_chunk_info *chunks;
	/* the chunks cover the file exactly, end to end */
	int covered;
	uint64_t compressed_size;
};

struct steam_manifest_metadata
{
	uint32_t depot_id;
	uint64_t gid;
	uint32_t creation_time;
	int filenames_encrypted;
	uint64_t original_size, compressed_size;
	uint32_t unique_chunks;
	uint32_t crc_encrypted, crc_clear;
};

/* a file of the manifest: 0 to go on, else the parse stops with that */
typedef int (*steam_manifest_visitor)(void *context, const struct steam_manifest_file *file);

/* parses data (an unzipped manifest); depot_key is needed only when the
names are encrypted (NULL refuses such a manifest). 0, or -1 with a reason,
or the visitor's own result */
int steam_manifest_parse(const uint8_t *data, size_t size, const uint8_t *depot_key,
	struct steam_manifest_metadata *metadata, steam_manifest_visitor visitor, void *context, char *error,
	int error_size);

/* the payload's CRC-32 as the metadata's crc_clear has it (the payload's
length, little-endian, then its bytes): 1 if it matches */
int steam_manifest_crc_ok(const uint8_t *data, size_t size, const struct steam_manifest_metadata *metadata);

#endif
