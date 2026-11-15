/*
STEAM_FILE.H

One planned file being downloaded: its chunks written at their offsets into
<path>.part, a resume journal beside it (<path>.part.journal), and when every
chunk is in, the whole file's SHA-1 checked against the manifest's
sha_content and its header against Setup's check (steam_plan_header_ok),
then renamed over <path>.

The journal holds no secret: a header (the depot, the manifest's gid, the
file's size and chunk count) and one line per chunk written, "<index>
<sha-1 hex>". It is a hint only: at the next start every chunk it names is
read back from the .part file and its SHA-1 checked again before it counts
(docs/steam_download_security.md 3.7).
*/

#ifndef STEAM_FILE_H
#define STEAM_FILE_H

#include "steam_manifest.h"
#include "steam_plan.h"

#include <stdint.h>
#include <stdio.h>

#define STEAM_PATH_MAXIMUM 512

struct steam_file_job
{
	FILE *part, *journal;
	char path[STEAM_PATH_MAXIMUM], part_path[STEAM_PATH_MAXIMUM + 8], journal_path[STEAM_PATH_MAXIMUM + 16];
	uint32_t depot;
	uint64_t gid, size;
	uint32_t chunk_count, done_count;
	struct steam_chunk_info *chunks;
	uint8_t *done;
	uint8_t sha_content[20];
	int has_sha_content;
	enum steam_plan_kind kind;
	const char *destination;
	/* the bytes already in (resumed) */
	uint64_t resumed_bytes;
};

enum
{
	STEAM_FILE_FAILED = -1,
	STEAM_FILE_STARTED = 0,
	/* the file is there already, whole and checked */
	STEAM_FILE_ALREADY = 1,
};

/* starts (or resumes) file into path: STEAM_FILE_STARTED, STEAM_FILE_ALREADY,
or STEAM_FILE_FAILED with a reason */
int steam_file_open(struct steam_file_job *job, const char *path, uint32_t depot, uint64_t gid,
	const struct steam_manifest_file *file, enum steam_plan_kind kind, const char *destination, char *error,
	int error_size);
/* chunk index's checked bytes (job->chunks[index].size of them) written at
its offset, then noted in the journal: 1, or 0 */
int steam_file_write(struct steam_file_job *job, uint32_t index, const uint8_t *data, char *error, int error_size);
/* every chunk in: the file checked and put in place: 1, or 0 with a reason
(the .part and journal are removed when the whole file's check fails) */
int steam_file_finish(struct steam_file_job *job, char *error, int error_size);
/* stops, keeping the .part and journal for a resume */
void steam_file_close(struct steam_file_job *job);
/* stops and removes the .part and journal */
void steam_file_discard(struct steam_file_job *job);

/* the journal's text checked against the job (for the fuzzer too): the
count of chunk lines read into marks (chunk indexes, before the read-back
check), or -1 if the header does not match */
long steam_file_journal_parse(const char *text, size_t size, uint32_t depot, uint64_t gid, uint64_t file_size,
	uint32_t chunk_count, const struct steam_chunk_info *chunks, uint8_t *marks);

#endif
