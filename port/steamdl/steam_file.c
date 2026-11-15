/*
STEAM_FILE.C

steam_file.h: chunks written at their offsets, the journal, the final
check.
*/

#include "steam_file.h"

#include "steam_crypto.h"
#include "steam_limits.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define JOURNAL_MAGIC "steamdl-journal 1"
#define READ_BLOCK (256u << 10)

static int seek_to(FILE *file, uint64_t offset)
{
	if (offset > STEAM_FILE_MAXIMUM)
		return 0;
	return fseeko(file, (off_t)offset, SEEK_SET) == 0;
}

static uint64_t file_size_of(const char *path, int *exists)
{
	FILE *file = fopen(path, "rb");
	off_t size;

	*exists = file != NULL;
	if (!file)
		return 0;
	if (fseeko(file, 0, SEEK_END) != 0 || (size = ftello(file)) < 0)
		size = 0;
	fclose(file);
	return (uint64_t)size;
}

/* the SHA-1 of size bytes from offset: 1, or 0 if they cannot be read */
static int hash_range(FILE *file, uint64_t offset, uint64_t size, uint8_t digest[20], uint8_t *block)
{
	struct steam_sha1_context sha;

	if (!seek_to(file, offset))
		return 0;
	steam_sha1_start(&sha);
	while (size)
	{
		size_t part = size < READ_BLOCK ? (size_t)size : READ_BLOCK;

		if (fread(block, 1, part, file) != part)
		{
			steam_sha1_finish(&sha, digest);
			return 0;
		}
		steam_sha1_update(&sha, block, part);
		size -= part;
	}
	steam_sha1_finish(&sha, digest);
	return 1;
}

/* the whole file's SHA-1 against sha_content and its header: 1, or 0 */
static int whole_file_ok(struct steam_file_job *job, FILE *file)
{
	uint8_t digest[20], head[16], *block = malloc(READ_BLOCK);
	size_t head_size;
	int ok;

	if (!block)
		return 0;
	ok = !job->has_sha_content || (hash_range(file, 0, job->size, digest, block) &&
		steam_equal(digest, job->sha_content, 20));
	free(block);
	if (!ok || !seek_to(file, 0))
		return 0;
	head_size = fread(head, 1, sizeof(head), file);
	return steam_plan_header_ok(job->kind, job->destination, head, head_size, job->size);
}

long steam_file_journal_parse(const char *text, size_t size, uint32_t depot, uint64_t gid, uint64_t file_size,
	uint32_t chunk_count, const struct steam_chunk_info *chunks, uint8_t *marks)
{
	char expected[160];
	size_t cursor, header_length;
	long count = 0;

	header_length = (size_t)snprintf(expected, sizeof(expected), JOURNAL_MAGIC "\ndepot %u\ngid %llu\nsize %llu\nchunks %u\n",
		(unsigned int)depot, (unsigned long long)gid, (unsigned long long)file_size, (unsigned int)chunk_count);
	if (size < header_length || memcmp(text, expected, header_length))
		return -1;
	cursor = header_length;
	while (cursor < size)
	{
		size_t end = cursor, length;
		unsigned long index = 0;
		uint8_t sha[20];
		int digits = 0;

		while (end < size && text[end] != '\n')
			end++;
		/* (a line cut short by a crash is the journal's end) */
		if (end >= size)
			break;
		length = end - cursor;
		while (digits < (int)length && digits < 7 && text[cursor + digits] >= '0' && text[cursor + digits] <= '9')
			index = index * 10 + (unsigned long)(text[cursor + digits++] - '0');
		if (digits && length == (size_t)digits + 41 && text[cursor + digits] == ' ' && index < chunk_count &&
			steam_unhex(text + cursor + digits + 1, 40, sha, 20) && !memcmp(sha, chunks[index].sha, 20) &&
			!marks[index])
		{
			marks[index] = 1;
			count++;
		}
		cursor = end + 1;
	}
	return count;
}

static int journal_line(FILE *journal, uint32_t index, const uint8_t sha[20])
{
	char hex[41];

	steam_hex(sha, 20, hex);
	return fprintf(journal, "%u %s\n", (unsigned int)index, hex) > 0;
}

static int journal_write_header(struct steam_file_job *job)
{
	return fprintf(job->journal, JOURNAL_MAGIC "\ndepot %u\ngid %llu\nsize %llu\nchunks %u\n", (unsigned int)job->depot,
		(unsigned long long)job->gid, (unsigned long long)job->size, (unsigned int)job->chunk_count) > 0;
}

/* what the journal says is in, each read back and checked */
static void resume(struct steam_file_job *job)
{
	FILE *journal = fopen(job->journal_path, "rb");
	uint8_t *marks, *block;
	char *text;
	size_t size, maximum;
	uint32_t index;

	if (!journal)
		return;
	maximum = 160 + 64 * (size_t)job->chunk_count;
	text = malloc(maximum);
	marks = calloc(job->chunk_count ? job->chunk_count : 1, 1);
	block = malloc(READ_BLOCK);
	if (text && marks && block)
	{
		size = fread(text, 1, maximum, journal);
		if (steam_file_journal_parse(text, size, job->depot, job->gid, job->size, job->chunk_count, job->chunks,
			marks) > 0)
		{
			for (index = 0; index < job->chunk_count; index++)
			{
				uint8_t digest[20];

				if (marks[index] && hash_range(job->part, job->chunks[index].offset, job->chunks[index].size, digest,
					block) && steam_equal(digest, job->chunks[index].sha, 20))
				{
					job->done[index] = 1;
					job->done_count++;
					job->resumed_bytes += job->chunks[index].size;
				}
			}
		}
	}
	free(text);
	free(marks);
	free(block);
	fclose(journal);
}

int steam_file_open(struct steam_file_job *job, const char *path, uint32_t depot, uint64_t gid,
	const struct steam_manifest_file *file, enum steam_plan_kind kind, const char *destination, char *error,
	int error_size)
{
	uint32_t index;
	int exists;

	memset(job, 0, sizeof(*job));
	error[0] = 0;
	if (!file->covered || file->size > STEAM_FILE_MAXIMUM || !file->chunk_count ||
		file->chunk_count > STEAM_FILE_CHUNKS_MAXIMUM || strlen(path) >= sizeof(job->path))
	{
		snprintf(error, (size_t)error_size, "file not downloadable (size, chunks or path)");
		return STEAM_FILE_FAILED;
	}
	snprintf(job->path, sizeof(job->path), "%s", path);
	snprintf(job->part_path, sizeof(job->part_path), "%s.part", path);
	snprintf(job->journal_path, sizeof(job->journal_path), "%s.part.journal", path);
	job->depot = depot;
	job->gid = gid;
	job->size = file->size;
	job->chunk_count = file->chunk_count;
	job->kind = kind;
	job->destination = destination;
	job->has_sha_content = file->has_sha_content;
	memcpy(job->sha_content, file->sha_content, 20);
	job->chunks = malloc(file->chunk_count * sizeof(*job->chunks));
	job->done = calloc(file->chunk_count, 1);
	if (!job->chunks || !job->done)
	{
		snprintf(error, (size_t)error_size, "out of memory");
		steam_file_close(job);
		return STEAM_FILE_FAILED;
	}
	memcpy(job->chunks, file->chunks, file->chunk_count * sizeof(*job->chunks));
	/* already there, whole? */
	if (file_size_of(path, &exists) == job->size && exists)
	{
		FILE *final = fopen(path, "rb");
		int ok = final && whole_file_ok(job, final);

		if (final)
			fclose(final);
		if (ok)
		{
			steam_file_close(job);
			return STEAM_FILE_ALREADY;
		}
	}
	file_size_of(job->part_path, &exists);
	job->part = fopen(job->part_path, exists ? "r+b" : "w+b");
	if (!job->part)
	{
		snprintf(error, (size_t)error_size, "cannot open %s (%d)", job->part_path, errno);
		steam_file_close(job);
		return STEAM_FILE_FAILED;
	}
	if (exists)
		resume(job);
	/* the journal afresh: its header and what was checked */
	job->journal = fopen(job->journal_path, "wb");
	if (!job->journal || !journal_write_header(job))
	{
		snprintf(error, (size_t)error_size, "cannot write %s", job->journal_path);
		steam_file_close(job);
		return STEAM_FILE_FAILED;
	}
	for (index = 0; index < job->chunk_count; index++)
		if (job->done[index] && !journal_line(job->journal, index, job->chunks[index].sha))
		{
			snprintf(error, (size_t)error_size, "cannot write %s", job->journal_path);
			steam_file_close(job);
			return STEAM_FILE_FAILED;
		}
	fflush(job->journal);
	return STEAM_FILE_STARTED;
}

int steam_file_write(struct steam_file_job *job, uint32_t index, const uint8_t *data, char *error, int error_size)
{
	const struct steam_chunk_info *chunk;

	if (!job->part || index >= job->chunk_count)
	{
		snprintf(error, (size_t)error_size, "no such chunk");
		return 0;
	}
	if (job->done[index])
		return 1;
	chunk = &job->chunks[index];
	if (!seek_to(job->part, chunk->offset) || fwrite(data, 1, chunk->size, job->part) != chunk->size ||
		fflush(job->part) != 0)
	{
		snprintf(error, (size_t)error_size, "cannot write %s (%d): is the memory card full?", job->part_path, errno);
		return 0;
	}
	job->done[index] = 1;
	job->done_count++;
	if (!journal_line(job->journal, index, chunk->sha) || fflush(job->journal) != 0)
	{
		snprintf(error, (size_t)error_size, "cannot write %s", job->journal_path);
		return 0;
	}
	return 1;
}

static void close_files(struct steam_file_job *job)
{
	if (job->part)
		fclose(job->part);
	if (job->journal)
		fclose(job->journal);
	job->part = job->journal = NULL;
}

int steam_file_finish(struct steam_file_job *job, char *error, int error_size)
{
	int ok;

	if (!job->part || job->done_count != job->chunk_count)
	{
		snprintf(error, (size_t)error_size, "file not complete");
		return 0;
	}
	ok = whole_file_ok(job, job->part);
	close_files(job);
	if (!ok)
	{
		snprintf(error, (size_t)error_size, "%s: the whole file's SHA-1 or header does not match", job->path);
		steam_file_discard(job);
		return 0;
	}
	/* (the Vita's rename does not replace) */
	remove(job->path);
	if (rename(job->part_path, job->path) != 0)
	{
		snprintf(error, (size_t)error_size, "cannot rename %s (%d)", job->part_path, errno);
		steam_file_close(job);
		return 0;
	}
	remove(job->journal_path);
	steam_file_close(job);
	return 1;
}

void steam_file_close(struct steam_file_job *job)
{
	close_files(job);
	free(job->chunks);
	free(job->done);
	job->chunks = NULL;
	job->done = NULL;
}

void steam_file_discard(struct steam_file_job *job)
{
	close_files(job);
	remove(job->part_path);
	remove(job->journal_path);
	steam_file_close(job);
}
