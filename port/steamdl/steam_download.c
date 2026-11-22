/*
STEAM_DOWNLOAD.C

steam_download.h: the download's flow, the session always ended.
*/

#include "steam_download.h"

#include "steam_chunk.h"
#include "steam_crypto.h"
#include "steam_file.h"
#include "steam_net.h"
#include "steam_plan.h"
#include "steam_signin.h"
#include "steam_zip.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#define CDN_MAXIMUM 16
#define CHUNK_TIMEOUT_MS 60000
#define MANIFEST_TIMEOUT_MS 120000

static void say(const struct steam_download_callbacks *callbacks, const char *format, ...)
	__attribute__((format(printf, 2, 3)));

static void say(const struct steam_download_callbacks *callbacks, const char *format, ...)
{
	char line[300];
	va_list arguments;

	if (!callbacks->log)
		return;
	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	callbacks->log(callbacks->context, line);
}

static void fail(struct steam_download_result *result, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void fail(struct steam_download_result *result, const char *format, ...)
{
	va_list arguments;

	if (result->error[0])
		return;
	va_start(arguments, format);
	vsnprintf(result->error, sizeof(result->error), format, arguments);
	va_end(arguments);
}

/* ---------- what a manifest gives */

struct planned
{
	const struct steam_plan_file *plan;
	char depot_path[160];
	/* (a Workshop item's: workshop/<id>/<its name>, at most STEAM_WORKSHOP_NAME_MAXIMUM of it) */
	char destination[200];
	enum steam_plan_kind kind;
	enum steam_plan_root root;
	struct steam_manifest_file *file; /* chunks copied into chunks */
	struct steam_chunk_info *chunks;
};

struct collected
{
	uint32_t games, depot;
	struct planned *files;
	size_t count, capacity;
	int failed;
};

static int keep_file(struct planned *planned, const struct steam_manifest_file *file)
{
	planned->file = malloc(sizeof(*planned->file));
	planned->chunks = malloc(file->chunk_count * sizeof(*planned->chunks) + 1);
	if (!planned->file || !planned->chunks)
		return 0;
	memcpy(planned->file, file, sizeof(*file));
	if (file->chunk_count)
		memcpy(planned->chunks, file->chunks, file->chunk_count * sizeof(*planned->chunks));
	planned->file->chunks = planned->chunks;
	return 1;
}

static void free_planned(struct planned *planned)
{
	free(planned->file);
	free(planned->chunks);
	planned->file = NULL;
	planned->chunks = NULL;
}

static int collect(void *context, const struct steam_manifest_file *file)
{
	struct collected *collected = context;
	const struct steam_plan_file *plan;

	if (file->flags & (STEAM_FILE_FLAG_DIRECTORY | STEAM_FILE_FLAG_SYMLINK))
		return 0;
	plan = steam_plan_match(collected->games, collected->depot, file->name);
	if (plan)
	{
		struct planned *planned;

		if (collected->count == collected->capacity)
		{
			size_t capacity = collected->capacity ? collected->capacity * 2 : 32;
			struct planned *grown = realloc(collected->files, capacity * sizeof(*grown));

			if (!grown)
				return -1;
			collected->files = grown;
			collected->capacity = capacity;
		}
		planned = &collected->files[collected->count];
		memset(planned, 0, sizeof(*planned));
		/* (a manifest naming one planned file twice: refused) */
		{
			size_t index;

			for (index = 0; index < collected->count; index++)
				if (collected->files[index].plan == plan)
					return -1;
		}
		planned->plan = plan;
		snprintf(planned->depot_path, sizeof(planned->depot_path), "%s", plan->depot_path);
		snprintf(planned->destination, sizeof(planned->destination), "%s", plan->destination);
		planned->kind = plan->kind;
		planned->root = plan->root;
		if (!keep_file(planned, file))
		{
			free_planned(planned);
			return -1;
		}
		collected->count++;
		return 0;
	}
	return 0;
}

/* ---------- the CDN */

struct cdn
{
	const char *const *servers;
	int count, current, tries;
	struct steam_http http;
	/* (the Wi-Fi's loss: rounds of waits between tries, the session kept meanwhile) */
	struct steam_session *session;
	const struct steam_download_callbacks *callbacks;
	struct steam_download_result *result;
	/* (the last cdn_get that gave up: a server's 403/404 - a refusal, not the network) */
	int refused;
};

/* waits ms in steps, the CM session pumped (its heartbeat): 0 if the session ended or the player cancelled */
static int cdn_wait(struct cdn *cdn, int ms)
{
	while (ms > 0)
	{
		int step = ms < 1000 ? ms : 1000;

		if (cdn->session)
		{
			if (!steam_session_pump(cdn->session, step) || cdn->session->kicked)
				return 0;
		}
		else if (cdn->callbacks && cdn->callbacks->sleep_ms)
			cdn->callbacks->sleep_ms(cdn->callbacks->context, step);
		if (cdn->callbacks && cdn->callbacks->progress && cdn->callbacks->progress(cdn->callbacks->context, 0, 0, NULL))
			return 0;
		ms -= step;
	}
	return 1;
}

/* a CDN server's name for the log (its base URL without the scheme: no path, so no request code) */
static const char *cdn_host(const char *base)
{
	const char *host = strstr(base, "://");

	return host ? host + 3 : base;
}

/* (what: a manifest's name for the log - its host, HTTP status and size said; NULL for a chunk: failures alone) */
static uint8_t *cdn_get(struct cdn *cdn, const char *path, size_t maximum, size_t *size, char *error, int error_size,
	int timeout_ms, const char *what)
{
	/* (a round tries each server twice, at most 6 tries; between rounds the Wi-Fi may come back: waits of 2, 5, 10,
	20, 30 and 30 s, about 1.5 minutes in all, before the download stops - resumable) */
	static const int waits_ms[] = { 2000, 5000, 10000, 20000, 30000, 30000 };
	int attempt, round, refused = 0;

	cdn->refused = 0;
	for (round = 0; round <= (int)(sizeof(waits_ms) / sizeof(waits_ms[0])) && !refused; round++)
	{
		if (round)
		{
			if (cdn->result)
				cdn->result->network_waits++;
			if (!cdn_wait(cdn, waits_ms[round - 1]))
				return NULL;
		}
		for (attempt = 0; attempt < cdn->count * 2 && attempt < 6; attempt++)
		{
			uint8_t *body;
			int status = 0;

			if (!cdn->http.open && !steam_net_http_open(&cdn->http, cdn->servers[cdn->current], error, error_size))
			{
				cdn->current = (cdn->current + 1) % cdn->count;
				continue;
			}
			body = steam_http_get(&cdn->http, path, maximum, size, &status, timeout_ms, error, error_size);
			if (body)
			{
				if (what && cdn->callbacks)
					say(cdn->callbacks, "steam: %s from %s: HTTP %d, %u bytes", what, cdn_host(cdn->servers[cdn->current]),
						status, (unsigned int)*size);
				return body;
			}
			if (cdn->callbacks)
				say(cdn->callbacks, "steam: %s from %s failed: HTTP %d (%s)", what ? what : "a chunk",
					cdn_host(cdn->servers[cdn->current]), status, error[0] ? error : "no reason");
			/* (this server failed: the next one; a server's own refusal is no Wi-Fi loss: no more rounds) */
			steam_http_close(&cdn->http);
			cdn->current = (cdn->current + 1) % cdn->count;
			if (status == 403 || status == 404)
				refused = 1;
		}
	}
	cdn->refused = refused;
	return NULL;
}

/* ---------- a manifest is the one asked for */

/* the manifest's metadata against what was asked (the depot, the gid) and its CRC (steam_manifest_crc_ok: the names'
form picks crc_encrypted or crc_clear): 1, or 0 with why (a fixed text for the player) - the log has the facts
either way (asked and got: depot, gid, the names' form, sizes, the CRCs; no key, no request code) */
static int manifest_is_asked(const struct steam_download_callbacks *callbacks, const char *what, uint32_t app,
	uint32_t depot, uint64_t gid, const uint8_t *manifest, size_t manifest_size, size_t zipped_size,
	const struct steam_manifest_metadata *metadata, const char **why)
{
	uint32_t crc = 0;
	int crc_ok = steam_manifest_crc(manifest, manifest_size, &crc) &&
		crc == (metadata->filenames_encrypted ? metadata->crc_encrypted : metadata->crc_clear);

	*why = metadata->depot_id != depot ? "it is another depot's" : metadata->gid != gid ? "it is another version's" :
		!crc_ok ? "its checksum is wrong" : NULL;
	say(callbacks, "steam: %s manifest (app %u): asked depot %u gid %llu, got depot %u gid %llu; names %s; %u bytes zipped, "
		"%u unzipped, %u unique chunks, %llu bytes on disk; payload crc %08x, crc_encrypted %08x, crc_clear %08x: %s", what,
		(unsigned int)app, (unsigned int)depot, (unsigned long long)gid, (unsigned int)metadata->depot_id,
		(unsigned long long)metadata->gid, metadata->filenames_encrypted ? "encrypted" : "clear",
		(unsigned int)zipped_size, (unsigned int)manifest_size, (unsigned int)metadata->unique_chunks,
		(unsigned long long)metadata->original_size, (unsigned int)crc, (unsigned int)metadata->crc_encrypted,
		(unsigned int)metadata->crc_clear, *why ? *why : "ok");
	return *why == NULL;
}

/* ---------- one file */

static int make_parents(const char *path)
{
	char folder[STEAM_PATH_MAXIMUM];
	size_t index, length = strlen(path);

	if (length >= sizeof(folder))
		return 0;
	memcpy(folder, path, length + 1);
	/* (each folder from the first '/' after a device's "ux0:" or the root) */
	for (index = 1; index < length; index++)
	{
		if (folder[index] != '/')
			continue;
		folder[index] = 0;
		if (folder[index - 1] != ':')
			mkdir(folder, 0777);
		folder[index] = '/';
	}
	return 1;
}

/* (a file's checks - one already there, a resume's chunks, the finished file -: the progress shown as "checking", the
player's cancel looked at every 256 KB) */
struct checking
{
	const struct steam_download_callbacks *callbacks;
	uint64_t done, total;
	char label[120];
};

static int checking_progress(void *context, uint64_t read, uint64_t total)
{
	struct checking *checking = context;

	(void)total;
	return checking->callbacks->progress && checking->callbacks->progress(checking->callbacks->context,
		checking->done + read, checking->total, checking->label);
}

/* the player's cancel, between steps (progress with no file: the screen's numbers unchanged) */
static int cancelled_now(const struct steam_download_callbacks *callbacks, uint64_t done, uint64_t total)
{
	return callbacks->progress && callbacks->progress(callbacks->context, done, total, NULL);
}

/* ---------- the verified files' record (1.1.3: the owner's 8d test spent 11 minutes reading 2.7 GB already on the
card at every start). After a file is checked whole or written and checked, its size, modification time and SHA-1 go
into <app root>/steam-verified.txt; at a later start a file whose record matches - the manifest's SHA-1 for it, its
size, the size and time on the card - is taken as there without reading it. Anything else (no record, a changed
time or size, another SHA-1 in the manifest: a new version) gets the whole check. The record is one file written
whole (a temporary, then renamed); a record that does not read exactly is dropped: every file checked again */

#define RECORD_MAGIC "steamdl-verified 1"
#define RECORD_ENTRIES_MAXIMUM 8192
#define RECORD_KEY_MAXIMUM 224
#define RECORD_BYTES_MAXIMUM (2u << 20)

struct record_entry
{
	char key[RECORD_KEY_MAXIMUM];
	uint64_t size;
	long long mtime;
	uint8_t sha[20];
};

static struct
{
	char path[STEAM_PATH_MAXIMUM];
	struct record_entry *entries;
	int count, dirty, loaded;
	time_t saved;
} record;

static int hex_value(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static void record_free(void)
{
	free(record.entries);
	memset(&record, 0, sizeof(record));
}

/* the record of app_root read (once per depot run): its entries, or none if anything in it does not read */
static void record_load(const struct steam_download_options *options, const struct steam_download_callbacks *callbacks)
{
	FILE *file;
	char *text = NULL, *line, *next;
	long size;
	int bad = 0;

	record_free();
	record.loaded = 1;
	record.entries = calloc(RECORD_ENTRIES_MAXIMUM, sizeof(*record.entries));
	if (!record.entries || snprintf(record.path, sizeof(record.path), "%s/steam-verified.txt", options->app_root) >=
		(int)sizeof(record.path))
	{
		record_free();
		return;
	}
	file = fopen(record.path, "rb");
	if (!file)
		return;
	if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 || size > (long)RECORD_BYTES_MAXIMUM ||
		fseek(file, 0, SEEK_SET) != 0 || !(text = malloc((size_t)size + 1)) || fread(text, 1, (size_t)size, file) != (size_t)size)
	{
		bad = 1;
	}
	fclose(file);
	if (!bad)
	{
		text[size] = 0;
		bad = strncmp(text, RECORD_MAGIC "\n", sizeof(RECORD_MAGIC)) != 0;
	}
	for (line = bad ? NULL : text + sizeof(RECORD_MAGIC); line && *line && !bad; line = next)
	{
		struct record_entry *entry;
		char *end;
		int index;

		next = strchr(line, '\n');
		if (!next)
		{
			bad = 1;
			break;
		}
		*next++ = 0;
		if (record.count >= RECORD_ENTRIES_MAXIMUM || strlen(line) < 46 || line[40] != ' ')
		{
			bad = 1;
			break;
		}
		entry = &record.entries[record.count];
		for (index = 0; index < 20; index++)
		{
			int high = hex_value(line[index * 2]), low = hex_value(line[index * 2 + 1]);

			if (high < 0 || low < 0)
				bad = 1;
			entry->sha[index] = (uint8_t)(high << 4 | low);
		}
		entry->size = strtoull(line + 41, &end, 10);
		if (bad || end == line + 41 || *end != ' ')
		{
			bad = 1;
			break;
		}
		line = end + 1;
		entry->mtime = strtoll(line, &end, 10);
		if (end == line || *end != ' ' || !end[1] || strlen(end + 1) >= sizeof(entry->key) || strchr(end + 1, '\r'))
		{
			bad = 1;
			break;
		}
		snprintf(entry->key, sizeof(entry->key), "%s", end + 1);
		record.count++;
	}
	free(text);
	if (bad)
	{
		say(callbacks, "steam: the verified files' record does not read: dropped (every file is checked)");
		record.count = 0;
		remove(record.path);
	}
	else if (record.count)
		say(callbacks, "steam: the verified files' record: %d files", record.count);
}

static int record_save(const struct steam_download_callbacks *callbacks)
{
	char temporary[STEAM_PATH_MAXIMUM + 8];
	FILE *file;
	int index, ok;

	if (!record.loaded || !record.entries || !record.dirty)
		return 1;
	snprintf(temporary, sizeof(temporary), "%s.tmp", record.path);
	file = fopen(temporary, "wb");
	if (!file)
		return 0;
	ok = fprintf(file, RECORD_MAGIC "\n") > 0;
	for (index = 0; index < record.count && ok; index++)
	{
		char hex[41];

		steam_hex(record.entries[index].sha, 20, hex);
		ok = fprintf(file, "%s %llu %lld %s\n", hex, (unsigned long long)record.entries[index].size,
			record.entries[index].mtime, record.entries[index].key) > 0;
	}
	ok = fflush(file) == 0 && ok;
	ok = fclose(file) == 0 && ok;
	/* (the Vita's rename does not replace: the old one removed first - a stop in between leaves no record, and every
	file is checked the next time: never a wrong one) */
	if (ok)
	{
		remove(record.path);
		ok = rename(temporary, record.path) == 0;
	}
	if (!ok)
	{
		remove(temporary);
		say(callbacks, "steam: the verified files' record could not be written");
		return 0;
	}
	record.dirty = 0;
	record.saved = time(NULL);
	return 1;
}

static void record_key(const struct planned *planned, char *key, size_t size)
{
	snprintf(key, size, "%s:%s", planned->root == STEAM_ROOT_CE ? "ce" : "app", planned->destination);
}

static struct record_entry *record_find(const char *key)
{
	int index;

	for (index = 0; record.entries && index < record.count; index++)
		if (!strcmp(record.entries[index].key, key))
			return &record.entries[index];
	return NULL;
}

/* the file at path, checked whole just now: recorded (its size and time on the card, the manifest's SHA-1) */
static void record_verified(const struct planned *planned, const char *path,
	const struct steam_download_callbacks *callbacks)
{
	struct record_entry *entry;
	struct stat info;
	char key[RECORD_KEY_MAXIMUM];

	if (!record.entries || !planned->file->has_sha_content || stat(path, &info) != 0 ||
		(uint64_t)info.st_size != planned->file->size)
		return;
	record_key(planned, key, sizeof(key));
	if (strlen(key) >= sizeof(entry->key) - 1)
		return;
	entry = record_find(key);
	if (!entry)
	{
		if (record.count >= RECORD_ENTRIES_MAXIMUM)
			return;
		entry = &record.entries[record.count++];
		snprintf(entry->key, sizeof(entry->key), "%s", key);
	}
	entry->size = planned->file->size;
	entry->mtime = (long long)info.st_mtime;
	memcpy(entry->sha, planned->file->sha_content, 20);
	record.dirty = 1;
	if (time(NULL) - record.saved >= 10)
		record_save(callbacks);
}

/* 1: the record says this file is there whole (the manifest's SHA-1, its size and time unchanged on the card) */
static int record_trusted(const struct planned *planned, const char *path)
{
	struct record_entry *entry;
	struct stat info;
	char key[RECORD_KEY_MAXIMUM];

	if (!record.entries || !planned->file->has_sha_content)
		return 0;
	record_key(planned, key, sizeof(key));
	entry = record_find(key);
	return entry && entry->size == planned->file->size && !memcmp(entry->sha, planned->file->sha_content, 20) &&
		stat(path, &info) == 0 && S_ISREG(info.st_mode) && (uint64_t)info.st_size == entry->size &&
		(long long)info.st_mtime == entry->mtime;
}

static int download_file(struct steam_session *session, struct planned *planned, uint32_t depot, uint64_t gid,
	const uint8_t *key, struct cdn *cdn,
	const struct steam_download_options *options, const struct steam_download_callbacks *callbacks,
	struct steam_download_result *result, uint64_t *done, uint64_t total)
{
	struct steam_file_job job;
	struct steam_chunk_work work;
	char path[STEAM_PATH_MAXIMUM], error[256];
	uint8_t *output = NULL;
	size_t output_capacity = 0;
	uint32_t index;
	int opened, ok = 0;

	if (snprintf(path, sizeof(path), "%s/%s", planned->root == STEAM_ROOT_CE ? options->ce_root : options->app_root,
		planned->destination) >= (int)sizeof(path) || !make_parents(path))
	{
		fail(result, "path too long: %s", planned->destination);
		return 0;
	}
	/* (the Workshop's staging is moved into mods: no record for it) */
	if (!options->workshop && record_trusted(planned, path))
	{
		say(callbacks, "%s: already there (verified before, unchanged)", planned->destination);
		result->files_already++;
		result->files_recorded++;
		*done += planned->file->size;
		return 1;
	}
	{
		struct checking checking;

		checking.callbacks = callbacks;
		checking.done = *done;
		checking.total = total;
		snprintf(checking.label, sizeof(checking.label), "checking %.100s", planned->destination);
		opened = steam_file_open_checked(&job, path, depot, gid, planned->file, planned->kind, planned->destination,
			checking_progress, &checking, error, sizeof(error));
		/* (the finished file's own check, later, counts from here) */
		job.checking_context = NULL;
		job.checking = NULL;
	}
	if (opened == STEAM_FILE_STOPPED)
	{
		result->cancelled = 1;
		fail(result, "cancelled");
		return 0;
	}
	if (opened == STEAM_FILE_FAILED)
	{
		fail(result, "%s: %s", planned->destination, error);
		return 0;
	}
	if (opened == STEAM_FILE_ALREADY)
	{
		say(callbacks, "%s: already there", planned->destination);
		result->files_already++;
		*done += planned->file->size;
		if (!options->workshop)
			record_verified(planned, path, callbacks);
		return 1;
	}
	*done += job.resumed_bytes;
	if (job.resumed_bytes)
		say(callbacks, "%s: resumed (%u of %u chunks)", planned->destination, (unsigned int)job.done_count,
			(unsigned int)job.chunk_count);
	steam_chunk_work_init(&work);
	for (index = 0; index < job.chunk_count; index++)
	{
		const struct steam_chunk_info *chunk = &job.chunks[index];
		char hex[41], chunk_path[96];
		int tries, written = 0;

		if (job.done[index])
			continue;
		/* (the CM session kept: its heartbeat, a log-off seen) */
		if (!steam_session_pump(session, 0) || session->kicked)
		{
			result->session_lost = 1;
			fail(result, "the Steam session ended during the download");
			goto stop;
		}
		/* (the device's own gate: a low battery pauses, the player may cancel meanwhile) */
		while (callbacks->device_gate && callbacks->device_gate(callbacks->context))
		{
			if (!cdn_wait(cdn, 2000))
				break;
		}
		if (callbacks->progress && callbacks->progress(callbacks->context, *done, total, planned->destination))
		{
			result->cancelled = 1;
			fail(result, "cancelled");
			goto stop;
		}
		if (output_capacity < chunk->size)
		{
			free(output);
			output = malloc(chunk->size);
			output_capacity = output ? chunk->size : 0;
			if (!output)
			{
				fail(result, "out of memory");
				goto stop;
			}
		}
		steam_hex(chunk->sha, 20, hex);
		snprintf(chunk_path, sizeof(chunk_path), "/depot/%u/chunk/%s", (unsigned int)depot, hex);
		for (tries = 0; tries < STEAM_CHUNK_TRIES && !written; tries++)
		{
			size_t size = 0;
			uint8_t *data = cdn_get(cdn, chunk_path, STEAM_CHUNK_COMPRESSED_MAXIMUM, &size, error, sizeof(error),
				CHUNK_TIMEOUT_MS, NULL);

			if (!data)
			{
				if (cancelled_now(callbacks, *done, total))
				{
					result->cancelled = 1;
					fail(result, "cancelled");
					goto stop;
				}
				say(callbacks, "%s: chunk %u: %s", planned->destination, (unsigned int)index, error);
				result->chunks_retried++;
				continue;
			}
			result->bytes_downloaded += size;
			if (!steam_chunk_process(&work, key, chunk, data, size, output, error, sizeof(error)))
			{
				/* (never written: fetched again, from the next server) */
				say(callbacks, "%s: chunk %u refused: %s", planned->destination, (unsigned int)index, error);
				result->chunks_retried++;
				steam_http_close(&cdn->http);
				cdn->current = (cdn->current + 1) % cdn->count;
				free(data);
				continue;
			}
			free(data);
			if (!steam_file_write(&job, index, output, error, sizeof(error)))
			{
				fail(result, "%s", error);
				goto stop;
			}
			result->bytes_written += chunk->size;
			*done += chunk->size;
			written = 1;
		}
		if (!written)
		{
			fail(result, "%s: a chunk could not be downloaded", planned->destination);
			goto stop;
		}
	}
	{
		struct checking checking;

		checking.callbacks = callbacks;
		checking.done = *done - planned->file->size;
		checking.total = total;
		snprintf(checking.label, sizeof(checking.label), "checking %.100s", planned->destination);
		job.checking = checking_progress;
		job.checking_context = &checking;
		if (!steam_file_finish(&job, error, sizeof(error)))
		{
			if (job.stopped)
				result->cancelled = 1;
			/* (bad data, already removed: a Workshop item's staging goes with it) */
			if (job.discarded)
				result->item_dropped = 1;
			fail(result, "%s", job.stopped ? "cancelled" : error);
			goto stopped;
		}
	}
	say(callbacks, "%s: done (%llu bytes)", planned->destination, (unsigned long long)planned->file->size);
	result->files_done++;
	if (!options->workshop)
		record_verified(planned, path, callbacks);
	ok = 1;
	goto stopped;
stop:
	steam_file_close(&job);
stopped:
	{
		int kind;

		for (kind = 0; kind < 3; kind++)
		{
			result->decode_us[kind] += work.decode_us[kind];
			result->decoded_bytes[kind] += work.decoded_bytes[kind];
			result->decoded_chunks[kind] += work.decoded_chunks[kind];
		}
		if (work.peak_plain > result->peak_chunk_buffer)
			result->peak_chunk_buffer = work.peak_plain;
	}
	steam_chunk_work_free(&work);
	free(output);
	return ok;
}

static int64_t planned_needed(const struct collected *collected)
{
	int64_t total = 0;
	size_t index;

	for (index = 0; index < collected->count; index++)
		total += (int64_t)collected->files[index].file->size;
	return total;
}

int steam_download_depot(struct steam_session *session, const struct steam_kv *app_info, uint32_t depot,
	const struct steam_download_options *options, const struct steam_download_callbacks *callbacks,
	const char *const *cdn_servers, int cdn_count, struct steam_download_result *result)
{
	struct collected collected;
	struct steam_manifest_metadata metadata;
	struct cdn cdn;
	const uint8_t *key;
	uint8_t *zipped = NULL, *manifest = NULL;
	size_t zipped_size = 0, manifest_size = 0, index, plan_count;
	const struct steam_plan_file *plan;
	uint64_t gid = 0, code = 0, done = 0, total;
	char path[128], error[256];
	int ok = 0;

	memset(&collected, 0, sizeof(collected));
	memset(&cdn, 0, sizeof(cdn));
	cdn.servers = cdn_servers;
	cdn.count = cdn_count;
	cdn.session = session;
	cdn.callbacks = callbacks;
	cdn.result = result;
	if (!steam_kv_depot_manifest(app_info, depot, "public", &gid))
	{
		fail(result, "depot %u has no public manifest in the app's info", (unsigned int)depot);
		return 0;
	}
	key = steam_session_depot_key(session, STEAM_APP_MCC, depot, error, sizeof(error));
	if (!key)
	{
		fail(result, "%s", error);
		return 0;
	}
	if (!steam_session_manifest_code(session, STEAM_APP_MCC, depot, gid, "public", &code, error, sizeof(error)))
	{
		fail(result, "%s", error);
		return 0;
	}
	/* (the code is in the URL, which is never logged) */
	snprintf(path, sizeof(path), "/depot/%u/manifest/%llu/5/%llu", (unsigned int)depot, (unsigned long long)gid,
		(unsigned long long)code);
	{
		char what[48];

		snprintf(what, sizeof(what), "depot %u's manifest", (unsigned int)depot);
		zipped = cdn_get(&cdn, path, STEAM_MANIFEST_ZIPPED_MAXIMUM, &zipped_size, error, sizeof(error), MANIFEST_TIMEOUT_MS,
			what);
	}
	steam_wipe(path, sizeof(path));
	code = 0;
	if (!zipped)
	{
		fail(result, "depot %u's manifest: %s", (unsigned int)depot, error);
		goto done;
	}
	if (!steam_zip_entry_size(zipped, zipped_size, STEAM_MANIFEST_MAXIMUM, &manifest_size) ||
		!(manifest = malloc(manifest_size + 1)) ||
		!steam_zip_extract(zipped, zipped_size, manifest, manifest_size, error, sizeof(error)))
	{
		fail(result, "depot %u's manifest is not a zip of one file", (unsigned int)depot);
		goto done;
	}
	collected.games = options->games;
	collected.depot = depot;
	plan = steam_plan_files(&plan_count);
	if (steam_manifest_parse(manifest, manifest_size, key, &metadata, collect, &collected, error, sizeof(error)) != 0)
	{
		fail(result, "depot %u's manifest refused: %s", (unsigned int)depot, error[0] ? error : "malformed");
		goto done;
	}
	{
		const char *why;

		if (!manifest_is_asked(callbacks, "depot", STEAM_APP_MCC, depot, gid, manifest, manifest_size, zipped_size,
			&metadata, &why))
		{
			fail(result, "depot %u's manifest is not the one asked for: %s", (unsigned int)depot, why);
			goto done;
		}
	}
	/* every required file of this depot there; the others noted */
	for (index = 0; index < plan_count; index++)
	{
		size_t found;

		if (steam_plan_depot_of(options->games, &plan[index]) != depot)
			continue;
		for (found = 0; found < collected.count; found++)
			if (collected.files[found].plan == &plan[index])
				break;
		if (found < collected.count)
			continue;
		if (plan[index].required)
		{
			fail(result, "depot %u lacks %s", (unsigned int)depot, plan[index].depot_path);
			goto done;
		}
		say(callbacks, "note: %s is not in depot %u: left out", plan[index].depot_path, (unsigned int)depot);
	}
	total = (uint64_t)planned_needed(&collected);
	if (callbacks->free_space)
	{
		int64_t free_bytes = callbacks->free_space(callbacks->context, options->app_root);

		if (free_bytes >= 0 && (uint64_t)free_bytes < total + total / 20)
		{
			fail(result, "not enough free space: %.1f GB needed, %.1f GB free", (total + total / 20) / 1e9,
				free_bytes / 1e9);
			goto done;
		}
	}
	say(callbacks, "depot %u: %u files, %.1f MB", (unsigned int)depot, (unsigned int)collected.count, total / 1e6);
	record_load(options, callbacks);
	for (index = 0; index < collected.count; index++)
	{
		struct planned *planned = &collected.files[index];

		if (!download_file(session, planned, depot, gid, key, &cdn, options, callbacks, result, &done, total))
			goto done;
	}
	ok = 1;
done:
	/* (what was verified kept for the next start, whatever ended it) */
	record_save(callbacks);
	record_free();
	steam_http_close(&cdn.http);
	for (index = 0; index < collected.count; index++)
		free_planned(&collected.files[index]);
	free(collected.files);
	free(zipped);
	free(manifest);
	return ok;
}

/* ---------- a Workshop item */

struct workshop_collected
{
	struct collected base;
	char prefix[48];
	int refused;
	char why[200];
	unsigned int skipped;
	const struct steam_download_callbacks *callbacks;
};

/* a manifest's file of a Workshop item: its name by the rule (steam_workshop_name), kept or skipped, or the item
refused */
static int collect_workshop(void *context, const struct steam_manifest_file *file)
{
	struct workshop_collected *collected = context;
	char relative[STEAM_WORKSHOP_NAME_MAXIMUM + 1];
	enum steam_plan_kind kind = STEAM_KIND_WORKSHOP_TEXT;
	struct planned *planned;
	size_t index;
	int taken;

	if (collected->refused)
		return 0;
	if (file->flags & STEAM_FILE_FLAG_DIRECTORY)
		return 0;
	if (file->flags & STEAM_FILE_FLAG_SYMLINK)
	{
		collected->skipped++;
		return 0;
	}
	taken = steam_workshop_name(file->name, relative, sizeof(relative), &kind);
	if (taken == STEAM_WORKSHOP_NAME_REFUSE)
	{
		/* (the name itself is the item maker's: kept out of the player's text, a short form in the log) */
		collected->refused = 1;
		snprintf(collected->why, sizeof(collected->why), "a file name that is not allowed");
		say(collected->callbacks, "workshop: refused name (%u bytes)", (unsigned int)strnlen(file->name, 2048));
		return 0;
	}
	if (taken == STEAM_WORKSHOP_NAME_SKIP)
	{
		collected->skipped++;
		return 0;
	}
	for (index = 0; index < collected->base.count; index++)
		if (steam_workshop_names_collide(collected->base.files[index].depot_path, relative))
		{
			collected->refused = 1;
			snprintf(collected->why, sizeof(collected->why), "two files of one name");
			return 0;
		}
	if (collected->base.count >= STEAM_WORKSHOP_FILES_MAXIMUM)
	{
		collected->refused = 1;
		snprintf(collected->why, sizeof(collected->why), "too many files");
		return 0;
	}
	if ((kind == STEAM_KIND_WORKSHOP_TEXT && file->size > STEAM_WORKSHOP_TEXT_BYTES_MAXIMUM) ||
		((kind == STEAM_KIND_WORKSHOP_PNG || kind == STEAM_KIND_WORKSHOP_BMP) &&
			file->size > STEAM_WORKSHOP_PICTURE_BYTES_MAXIMUM) || file->size > STEAM_FILE_MAXIMUM || !file->size)
	{
		collected->refused = 1;
		snprintf(collected->why, sizeof(collected->why), "a file too large (or empty) for its kind");
		return 0;
	}
	if (collected->base.count == collected->base.capacity)
	{
		size_t capacity = collected->base.capacity ? collected->base.capacity * 2 : 32;
		struct planned *grown = realloc(collected->base.files, capacity * sizeof(*grown));

		if (!grown)
			return -1;
		collected->base.files = grown;
		collected->base.capacity = capacity;
	}
	planned = &collected->base.files[collected->base.count];
	memset(planned, 0, sizeof(*planned));
	/* (depot_path: the relative name, for the collision check; destination: under the CE root) */
	snprintf(planned->depot_path, sizeof(planned->depot_path), "%s", relative);
	if ((size_t)snprintf(planned->destination, sizeof(planned->destination), "%s/%s", collected->prefix, relative) >=
		sizeof(planned->destination))
	{
		collected->refused = 1;
		snprintf(collected->why, sizeof(collected->why), "a file name too long");
		return 0;
	}
	planned->kind = kind;
	planned->root = STEAM_ROOT_CE;
	if (!keep_file(planned, file))
	{
		free_planned(planned);
		return -1;
	}
	collected->base.count++;
	return 0;
}

static int write_text_file(const char *path, const char *text)
{
	FILE *file = fopen(path, "wb");
	int ok;

	if (!file)
		return 0;
	ok = fputs(text, file) >= 0;
	return fclose(file) == 0 && ok;
}

static int read_gid_file(const char *path, uint64_t *gid)
{
	char text[40];
	FILE *file = fopen(path, "rb");
	size_t length;

	*gid = 0;
	if (!file)
		return 0;
	length = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	text[length] = 0;
	*gid = strtoull(text, NULL, 10);
	return *gid != 0;
}

int steam_download_workshop_item(struct steam_session *session, const struct steam_workshop_item *item,
	const struct steam_download_options *options, const struct steam_download_callbacks *callbacks,
	const char *const *cdn_servers, int cdn_count, struct steam_download_result *result)
{
	struct workshop_collected collected;
	struct steam_manifest_metadata metadata;
	struct cdn cdn;
	const uint8_t *key;
	uint8_t *zipped = NULL, *manifest = NULL;
	size_t zipped_size = 0, manifest_size = 0, index;
	uint64_t code = 0, done = 0, total, staged_gid;
	char path[128], error[256], id_text[24], staging[512], gid_path[512], done_path[512], text[96], folder[512];
	int ok = 0;

	memset(&collected, 0, sizeof(collected));
	memset(&cdn, 0, sizeof(cdn));
	cdn.servers = cdn_servers;
	cdn.count = cdn_count;
	cdn.session = session;
	cdn.callbacks = callbacks;
	cdn.result = result;
	if (!steam_workshop_id_text(item->id, id_text, sizeof(id_text)) ||
		!steam_workshop_paths(options->ce_root, item->id, staging, gid_path, done_path, NULL, sizeof(staging)))
	{
		fail(result, "the item's number is too long");
		return 0;
	}
	/* (a staging folder of another version of the item: begun again) */
	if (read_gid_file(gid_path, &staged_gid) && staged_gid != item->hcontent)
	{
		say(callbacks, "workshop %s: a new version: the old download dropped", id_text);
		steam_workshop_remove_tree(staging);
	}
	remove(done_path);
	snprintf(folder, sizeof(folder), "%s/workshop", options->ce_root);
	make_parents(staging);
	mkdir(folder, 0777);
	snprintf(text, sizeof(text), "%llu\n", (unsigned long long)item->hcontent);
	if (!write_text_file(gid_path, text))
	{
		fail(result, "cannot write in %s", folder);
		return 0;
	}
	key = steam_session_depot_key(session, STEAM_WORKSHOP_APP, STEAM_WORKSHOP_DEPOT, error, sizeof(error));
	if (!key)
	{
		fail(result, "%s", error);
		return 0;
	}
	if (!steam_session_manifest_code(session, STEAM_WORKSHOP_APP, STEAM_WORKSHOP_DEPOT, item->hcontent, "public", &code,
		error, sizeof(error)))
	{
		fail(result, "%s", error);
		return 0;
	}
	snprintf(path, sizeof(path), "/depot/%u/manifest/%llu/5/%llu", (unsigned int)STEAM_WORKSHOP_DEPOT,
		(unsigned long long)item->hcontent, (unsigned long long)code);
	{
		char what[64];

		snprintf(what, sizeof(what), "item %s's manifest", id_text);
		zipped = cdn_get(&cdn, path, STEAM_MANIFEST_ZIPPED_MAXIMUM, &zipped_size, error, sizeof(error), MANIFEST_TIMEOUT_MS,
			what);
	}
	steam_wipe(path, sizeof(path));
	code = 0;
	if (!zipped)
	{
		/* (a server's refusal drops the item; the network's failure keeps what is staged) */
		result->item_dropped = cdn.refused;
		fail(result, "item %s's manifest: %s", id_text, error);
		goto done;
	}
	if (!steam_zip_entry_size(zipped, zipped_size, STEAM_MANIFEST_MAXIMUM, &manifest_size) ||
		!(manifest = malloc(manifest_size + 1)) ||
		!steam_zip_extract(zipped, zipped_size, manifest, manifest_size, error, sizeof(error)))
	{
		result->item_dropped = 1;
		fail(result, "item %s's manifest is not a zip of one file", id_text);
		goto done;
	}
	snprintf(collected.prefix, sizeof(collected.prefix), "workshop/%s", id_text);
	collected.callbacks = callbacks;
	if (steam_manifest_parse(manifest, manifest_size, key, &metadata, collect_workshop, &collected, error,
		sizeof(error)) != 0)
	{
		result->item_dropped = 1;
		fail(result, "item %s's manifest refused: %s", id_text, error[0] ? error : "malformed");
		goto done;
	}
	{
		const char *why;

		if (!manifest_is_asked(callbacks, "workshop item", STEAM_WORKSHOP_APP, STEAM_WORKSHOP_DEPOT, item->hcontent,
			manifest, manifest_size, zipped_size, &metadata, &why))
		{
			result->item_dropped = 1;
			fail(result, "item %s's manifest is not the one asked for: %s", id_text, why);
			goto done;
		}
	}
	if (collected.refused)
	{
		result->item_dropped = 1;
		fail(result, "item %s refused: %s", id_text, collected.why);
		goto done;
	}
	if (collected.skipped)
		say(callbacks, "workshop %s: %u files of other kinds left out", id_text, collected.skipped);
	total = (uint64_t)planned_needed(&collected.base);
	if (!collected.base.count || total > STEAM_WORKSHOP_ITEM_BYTES_MAXIMUM)
	{
		result->item_dropped = 1;
		fail(result, "item %s: %s", id_text, collected.base.count ? "too large" : "no files to take");
		goto done;
	}
	if (callbacks->free_space)
	{
		int64_t free_bytes = callbacks->free_space(callbacks->context, options->ce_root);

		if (free_bytes >= 0 && (uint64_t)free_bytes < total + total / 20)
		{
			fail(result, "not enough free space: %.1f GB needed, %.1f GB free", (total + total / 20) / 1e9,
				free_bytes / 1e9);
			goto done;
		}
	}
	say(callbacks, "workshop %s: %u files, %.1f MB", id_text, (unsigned int)collected.base.count, total / 1e6);
	for (index = 0; index < collected.base.count; index++)
	{
		if (!download_file(session, &collected.base.files[index], STEAM_WORKSHOP_DEPOT, item->hcontent, key, &cdn,
			options, callbacks, result, &done, total))
		{
			goto done;
		}
	}
	snprintf(text, sizeof(text), "%llu\n%u\n", (unsigned long long)item->hcontent, (unsigned int)collected.base.count);
	if (!write_text_file(done_path, text))
	{
		fail(result, "cannot write in %s", folder);
		goto done;
	}
	ok = 1;
done:
	steam_http_close(&cdn.http);
	for (index = 0; index < collected.base.count; index++)
		free_planned(&collected.base.files[index]);
	free(collected.base.files);
	free(zipped);
	free(manifest);
	return ok;
}

/* a manifest's file seen for the details page: maps named, files counted */
static int contents_file(void *context, const struct steam_manifest_file *file)
{
	struct steam_workshop_contents *contents = context;
	char relative[STEAM_WORKSHOP_NAME_MAXIMUM + 1];
	enum steam_plan_kind kind = STEAM_KIND_WORKSHOP_TEXT;
	int taken;

	if (file->flags & STEAM_FILE_FLAG_DIRECTORY)
		return 0;
	if (file->flags & STEAM_FILE_FLAG_SYMLINK)
	{
		contents->skipped++;
		return 0;
	}
	taken = steam_workshop_name(file->name, relative, sizeof(relative), &kind);
	if (taken == STEAM_WORKSHOP_NAME_REFUSE)
	{
		contents->refused = 1;
		snprintf(contents->why, sizeof(contents->why), "a file name that is not allowed");
		return 0;
	}
	if (taken == STEAM_WORKSHOP_NAME_SKIP)
	{
		contents->skipped++;
		return 0;
	}
	contents->files++;
	contents->bytes += file->size;
	if (kind == STEAM_KIND_MCC_MAP)
	{
		if (contents->maps < STEAM_WORKSHOP_MAP_NAMES)
		{
			const char *base = strrchr(relative, '/');
			size_t length;

			base = base ? base + 1 : relative;
			length = strlen(base);
			if (length > 4)
				length -= 4;
			snprintf(contents->map_names[contents->maps], sizeof(contents->map_names[0]), "%.*s",
				(int)(length < sizeof(contents->map_names[0]) - 1 ? length : sizeof(contents->map_names[0]) - 1), base);
		}
		contents->maps++;
	}
	return 0;
}

int steam_download_workshop_contents(struct steam_session *session, const struct steam_workshop_item *item,
	const char *const *cdn_override, int cdn_override_count, struct steam_workshop_contents *contents, char *error,
	int error_size)
{
	struct steam_cdn_server servers[4];
	char urls[4][STEAM_HOST_MAXIMUM + 16], path[128];
	const char *cdn[4];
	struct steam_manifest_metadata metadata;
	const uint8_t *key;
	uint8_t *zipped = NULL, *manifest = NULL;
	size_t zipped_size = 0, manifest_size = 0;
	uint64_t code = 0;
	int count = 0, index, ok = 0;
	struct steam_download_callbacks said;
	char id_text[24];

	memset(contents, 0, sizeof(*contents));
	error[0] = 0;
	if (!item->hcontent)
	{
		snprintf(error, (size_t)error_size, "the item has no files");
		return 0;
	}
	if (cdn_override && cdn_override_count > 0)
	{
		for (index = 0; index < cdn_override_count && count < 4; index++)
			cdn[count++] = cdn_override[index];
	}
	else
	{
		int found = steam_session_cdn_servers(session, servers, 4, error, error_size);

		for (index = 0; index < found; index++)
		{
			snprintf(urls[index], sizeof(urls[index]), "https://%.253s", servers[index].host);
			cdn[count++] = urls[index];
		}
		if (count <= 0)
			return 0;
	}
	key = steam_session_depot_key(session, STEAM_WORKSHOP_APP, STEAM_WORKSHOP_DEPOT, error, error_size);
	if (!key || !steam_session_manifest_code(session, STEAM_WORKSHOP_APP, STEAM_WORKSHOP_DEPOT, item->hcontent,
		"public", &code, error, error_size))
	{
		return 0;
	}
	snprintf(path, sizeof(path), "/depot/%u/manifest/%llu/5/%llu", (unsigned int)STEAM_WORKSHOP_DEPOT,
		(unsigned long long)item->hcontent, (unsigned long long)code);
	memset(&said, 0, sizeof(said));
	said.log = session->log;
	said.context = session->log_context;
	snprintf(id_text, sizeof(id_text), "%llu", (unsigned long long)item->id);
	for (index = 0; index < count && !zipped; index++)
	{
		struct steam_http http;
		int status = 0;

		if (!steam_net_http_open(&http, cdn[index], error, error_size))
		{
			say(&said, "steam: item %s's manifest: %s cannot be reached (%s)", id_text, cdn_host(cdn[index]), error);
			continue;
		}
		zipped = steam_http_get(&http, path, STEAM_MANIFEST_ZIPPED_MAXIMUM, &zipped_size, &status, MANIFEST_TIMEOUT_MS / 4,
			error, error_size);
		steam_http_close(&http);
		if (zipped)
			say(&said, "steam: item %s's manifest from %s: HTTP %d, %u bytes", id_text, cdn_host(cdn[index]), status,
				(unsigned int)zipped_size);
		else
			say(&said, "steam: item %s's manifest from %s failed: HTTP %d (%s)", id_text, cdn_host(cdn[index]), status,
				error[0] ? error : "no reason");
	}
	steam_wipe(path, sizeof(path));
	code = 0;
	if (!zipped)
	{
		if (!error[0])
			snprintf(error, (size_t)error_size, "the item's list of files did not come");
		return 0;
	}
	if (!steam_zip_entry_size(zipped, zipped_size, STEAM_MANIFEST_MAXIMUM, &manifest_size) ||
		!(manifest = malloc(manifest_size + 1)) ||
		!steam_zip_extract(zipped, zipped_size, manifest, manifest_size, error, error_size))
	{
		snprintf(error, (size_t)error_size, "the item's list of files is not a zip of one file");
		goto done;
	}
	if (steam_manifest_parse(manifest, manifest_size, key, &metadata, contents_file, contents, error, error_size) != 0)
	{
		say(&said, "steam: item %s's manifest refused: %s", id_text, error[0] ? error : "malformed");
		snprintf(error, (size_t)error_size, "the item's list of files was refused");
		goto done;
	}
	{
		const char *why;

		if (!manifest_is_asked(&said, "workshop item", STEAM_WORKSHOP_APP, STEAM_WORKSHOP_DEPOT, item->hcontent, manifest,
			manifest_size, zipped_size, &metadata, &why))
		{
			snprintf(error, (size_t)error_size, "the item's list of files is not the one asked for (%s)", why);
			goto done;
		}
	}
	ok = 1;
done:
	free(zipped);
	free(manifest);
	if (!ok)
		memset(contents, 0, sizeof(*contents));
	return ok;
}

int steam_download_workshop_query(struct steam_session *session, int page, enum steam_workshop_sort sort,
	const char *search, struct steam_workshop_item *items, int maximum, uint32_t *total, char *error, int error_size)
{
	uint8_t body[256];
	const uint8_t *reply;
	size_t size, reply_size;
	int count, subscribed = (int)sort == STEAM_WORKSHOP_LIST_SUBSCRIBED;

	/* (SUBSCRIBED: the signed-in account's own list - its steamid the session's, as Steam gave it at the logon) */
	size = subscribed ? steam_workshop_subscribed_request(body, sizeof(body), session->steamid, page) :
		steam_workshop_query_request(body, sizeof(body), page, sort, search);
	if (!size)
	{
		snprintf(error, (size_t)error_size, subscribed ? "not signed in" : "a search too long");
		return -1;
	}
	if (!steam_session_call(session, subscribed ? "PublishedFile.GetUserFiles#1" : "PublishedFile.QueryFiles#1", body,
		size, &reply, &reply_size, error, error_size))
	{
		return -1;
	}
	count = subscribed ? steam_workshop_parse_subscribed(reply, reply_size, items, maximum, total) :
		steam_workshop_parse_query(reply, reply_size, items, maximum, total);
	if (count < 0)
		snprintf(error, (size_t)error_size, "the Workshop's list was malformed");
	return count;
}

int steam_download_workshop_page(struct steam_session *session, uint64_t id, struct steam_workshop_details *details,
	char *error, int error_size)
{
	uint8_t body[64];
	const uint8_t *reply;
	size_t size = steam_workshop_page_request(body, sizeof(body), id), reply_size;
	int found;

	memset(details, 0, sizeof(*details));
	if (!size || !steam_session_call(session, "PublishedFile.GetDetails#1", body, size, &reply, &reply_size, error,
		error_size))
	{
		if (!size)
			snprintf(error, (size_t)error_size, "no such item");
		return 0;
	}
	found = steam_workshop_parse_page(reply, reply_size, id, details);
	if (found <= 0)
		snprintf(error, (size_t)error_size, found < 0 ? "the item's details were malformed" : "Steam gave no details of it");
	return found > 0;
}

int steam_download_workshop_names(struct steam_session *session, const uint64_t *steamids, int count,
	char (*names)[STEAM_WORKSHOP_AUTHOR_SIZE])
{
	uint8_t body[512];
	const uint8_t *reply;
	size_t size = steam_workshop_names_request(body, sizeof(body), steamids, count), reply_size;
	char error[160];
	int index;

	for (index = 0; index < count; index++)
		names[index][0] = 0;
	if (!size || !steam_session_call(session, "Player.GetPlayerLinkDetails#1", body, size, &reply, &reply_size, error,
		sizeof(error)))
	{
		return -1;
	}
	return steam_workshop_parse_names(reply, reply_size, steamids, count, names);
}

/* the chosen items' details (one call), each checked, then each downloaded */
static int workshop_flow(struct steam_session *session, const struct steam_download_options *options,
	const struct steam_download_callbacks *callbacks, const char *const *cdn, int cdn_count, const uint64_t *ids,
	int count, struct steam_download_result *result)
{
	struct steam_workshop_item items[STEAM_WORKSHOP_CHOSEN_MAXIMUM];
	uint8_t body[256];
	const uint8_t *reply;
	size_t size, reply_size;
	char error[256];
	int found, index;

	size = steam_workshop_details_request(body, sizeof(body), ids, count);
	if (!size || !steam_session_call(session, "PublishedFile.GetDetails#1", body, size, &reply, &reply_size, error,
		sizeof(error)))
	{
		fail(result, "%s", size ? error : "too many items");
		return 0;
	}
	found = steam_workshop_parse_details(reply, reply_size, items, STEAM_WORKSHOP_CHOSEN_MAXIMUM);
	if (found < 0)
	{
		fail(result, "the items' details were malformed");
		return 0;
	}
	for (index = 0; index < count; index++)
	{
		const struct steam_workshop_item *item = NULL;
		char why[160];
		int other;

		for (other = 0; other < found; other++)
			if (items[other].id == ids[index])
				item = &items[other];
		if (!item)
		{
			struct steam_workshop_item missing;

			memset(&missing, 0, sizeof(missing));
			missing.id = ids[index];
			result->items_failed++;
			if (callbacks->item_end)
				callbacks->item_end(callbacks->context, &missing, 0, "Steam gave no details of it");
			continue;
		}
		if (callbacks->item_start && callbacks->item_start(callbacks->context, item, index, count))
		{
			result->cancelled = 1;
			fail(result, "cancelled");
			return 0;
		}
		if (!steam_workshop_item_ok(item, why, sizeof(why)))
		{
			say(callbacks, "workshop %llu: %s", (unsigned long long)item->id, why);
			result->items_failed++;
			if (callbacks->item_end)
				callbacks->item_end(callbacks->context, item, 0, why);
			continue;
		}
		result->error[0] = 0;
		result->item_dropped = 0;
		if (steam_download_workshop_item(session, item, options, callbacks, cdn, cdn_count, result))
		{
			result->items_done++;
			if (callbacks->item_end)
				callbacks->item_end(callbacks->context, item, 1, "");
			continue;
		}
		/* (a cancel or a lost session ends them all, its download kept for a resume. A failed item: what it already
		got (its verified chunks, journalled) kept in staging for the next try, as the depot download keeps it - only an
		item whose data failed its checks or that Steam refused has its staging folder removed; the next one goes on) */
		if (result->cancelled || result->session_lost || session->kicked)
			return 0;
		{
			char staging[512], gid_path[512], done_path[512];

			if (steam_workshop_paths(options->ce_root, item->id, staging, gid_path, done_path, NULL, sizeof(staging)))
			{
				if (result->item_dropped)
				{
					steam_workshop_remove_tree(staging);
					remove(gid_path);
				}
				else
				{
					uint64_t kept = steam_workshop_tree_bytes(staging);

					result->items_kept++;
					result->kept_bytes += kept;
					say(callbacks, "workshop %llu: failed; %.1f MB of it kept for the next try", (unsigned long long)item->id,
						kept / 1e6);
				}
				remove(done_path);
			}
		}
		result->items_failed++;
		if (callbacks->item_end)
		{
			/* (the player's words: the card's folder left out of the reason) */
			char why_shown[256], *at;
			size_t root_length = strlen(options->ce_root);

			snprintf(why_shown, sizeof(why_shown), "%s", result->error);
			while (root_length && (at = strstr(why_shown, options->ce_root)) != NULL)
				memmove(at, at + root_length + (at[root_length] == '/'), strlen(at + root_length + (at[root_length] == '/')) + 1);
			callbacks->item_end(callbacks->context, item, 0, why_shown);
		}
	}
	return 1;
}

/* ---------- the whole flow */

void steam_download_run(const struct steam_download_options *options, const struct steam_download_callbacks *callbacks,
	struct steam_download_result *result)
{
	struct steam_session session;
	struct steam_kv app_info;
	struct steam_cdn_server servers[CDN_MAXIMUM];
	char urls[CDN_MAXIMUM][STEAM_HOST_MAXIMUM + 16];
	const char *cdn[CDN_MAXIMUM];
	char url[STEAM_URL_MAXIMUM + 1], error[256];
	uint32_t depots[4];
	size_t depot_count, index;
	float interval = 5.0f;
	int cdn_count = 0, state, workshop_count = 0, use_kept = 0, revoke_kept = 0;
	uint64_t workshop_ids[STEAM_WORKSHOP_CHOSEN_MAXIMUM];

	memset(result, 0, sizeof(*result));
	memset(&app_info, 0, sizeof(app_info));
	if (!steam_session_init(&session, callbacks->log, callbacks->context, options->marker_path))
	{
		fail(result, "out of memory");
		steam_session_end(&session);
		return;
	}
	/* the sign-in kept on the device, read before any connection: used, or (expired, or SIGN OUT) to revoke; unreadable:
	wiped (SIGN OUT wipes it whatever it holds, and connects only if there is a token to revoke) */
	if (options->signin_path && options->device_secret && options->device_secret_size)
	{
		struct steam_signin *kept = calloc(1, sizeof(*kept));
		int state = kept ? steam_signin_load(options->signin_path, options->device_secret, options->device_secret_size,
			(int64_t)time(NULL), kept) : STEAM_SIGNIN_NONE;

		result->signin_found = state;
		if (options->sign_out)
			steam_signin_wipe(options->signin_path);
		if (state == STEAM_SIGNIN_KEPT || state == STEAM_SIGNIN_EXPIRED)
		{
			steam_session_use_token(&session, kept->refresh_token, kept->account_name);
			if (state == STEAM_SIGNIN_EXPIRED || options->sign_out)
			{
				revoke_kept = 1;
				say(callbacks, state == STEAM_SIGNIN_EXPIRED ? "steam: the sign-in kept on this device is older than "
					"14 days: removed from it, to be revoked with Steam" : "steam: sign out: the kept sign-in removed from "
					"this device, to be revoked with Steam");
			}
			else
			{
				/* (kept from here on: a connection that fails ends without revoking it) */
				use_kept = 1;
				session.keep_token = 1;
				result->kept = 1;
				say(callbacks, "steam: the sign-in kept on this device is used (saved %d days ago; kept 14)",
					(int)(((int64_t)time(NULL) - kept->saved_at) / 86400));
			}
		}
		else if (state == STEAM_SIGNIN_UNREADABLE)
			say(callbacks, "steam: the kept sign-in could not be read (damaged, changed or another console's): removed");
		if (kept)
		{
			steam_wipe(kept, sizeof(*kept));
			free(kept);
		}
	}
	if (options->sign_out && !revoke_kept)
	{
		say(callbacks, "steam: sign out: no sign-in was kept on this device (nothing to revoke, no connection made)");
		result->ok = 1;
		goto end;
	}
	{
		struct timespec before, after;
		int connected;

		clock_gettime(CLOCK_MONOTONIC, &before);
		connected = steam_session_connect(&session, options->cm_url, error, sizeof(error));
		clock_gettime(CLOCK_MONOTONIC, &after);
		result->connect_ms = (unsigned int)((after.tv_sec - before.tv_sec) * 1000 + (after.tv_nsec - before.tv_nsec) / 1000000);
		if (connected)
			say(callbacks, "steam: the CM connection took %u ms", result->connect_ms);
		else
		{
			fail(result, "%s", error);
			goto end;
		}
	}
	if (revoke_kept)
	{
		/* (SIGN OUT signs in first, so the revoke is the account's own; an expired one, or a refused logon, is revoked
		unsigned) */
		if (options->sign_out && !steam_session_logon(&session, error, sizeof(error)) && !session.connected)
			steam_session_reconnect(&session, error, sizeof(error));
		result->signed_out = steam_session_revoke(&session);
		if (options->sign_out)
		{
			result->ok = 1;
			goto end;
		}
		if (!session.connected && !steam_session_reconnect(&session, error, sizeof(error)))
		{
			fail(result, "%s", error);
			goto end;
		}
	}
	if (use_kept)
	{
		if (steam_session_logon(&session, error, sizeof(error)))
		{
			session.keep_token = 1;
			result->kept = 1;
			goto signed_in;
		}
		/* (Steam not answering or busy: not the token's fault - kept for the next try) */
		if (session.logon_eresult == 0 || session.logon_eresult == 10 || session.logon_eresult == 16 ||
			session.logon_eresult == 20 || session.logon_eresult == 48 || session.logon_eresult == 84)
		{
			session.keep_token = 1;
			result->kept = 1;
			result->session_lost = !session.connected;
			fail(result, "%s", error);
			goto end;
		}
		/* (refused: revoked or expired at Steam, the password changed, the device removed in the Steam app) */
		say(callbacks, "steam: the kept sign-in was refused (result %d, %s): removed from this device; a new QR",
			session.logon_eresult, steam_eresult_name(session.logon_eresult));
		steam_signin_wipe(options->signin_path);
		steam_session_use_token(&session, "", "");
		session.keep_token = 0;
		result->kept = 0;
		if (!steam_session_reconnect(&session, error, sizeof(error)))
		{
			fail(result, "%s", error);
			goto end;
		}
	}
	if (!steam_session_qr_begin(&session, url, sizeof(url), &interval, error, sizeof(error)))
	{
		fail(result, "%s", error);
		goto end;
	}
	/* the QR, until approved, refused, expired or cancelled */
	if (callbacks->show_qr && callbacks->show_qr(callbacks->context, url))
		goto cancelled;
	for (;;)
	{
		if (callbacks->waiting && callbacks->waiting(callbacks->context))
			goto cancelled;
		if (callbacks->sleep_ms)
			callbacks->sleep_ms(callbacks->context, (int)(interval * 1000));
		else
			steam_session_pump(&session, (int)(interval * 1000));
		state = steam_session_qr_poll(&session, url, sizeof(url), error, sizeof(error));
		if (state == STEAM_QR_FAILED)
		{
			fail(result, "%s", error);
			goto end;
		}
		if (state == STEAM_QR_APPROVED)
			break;
		if (state == STEAM_QR_NEW_URL && callbacks->show_qr && callbacks->show_qr(callbacks->context, url))
			goto cancelled;
	}
	steam_wipe(url, sizeof(url));
	if (!steam_session_logon(&session, error, sizeof(error)))
	{
		fail(result, "%s", error);
		goto end;
	}
	/* (kept on the device for the next 14 days: the refresh token, the account's name, the date - sealed to this
	console; if it cannot be kept, it is revoked at the end as before) */
	if (options->signin_path && options->device_secret && options->device_secret_size && session.secrets)
	{
		struct steam_signin *keep = calloc(1, sizeof(*keep));

		if (keep)
		{
			memcpy(keep->refresh_token, session.secrets->refresh_token, sizeof(keep->refresh_token));
			memcpy(keep->account_name, session.secrets->account_name, sizeof(keep->account_name));
			keep->saved_at = (int64_t)time(NULL);
		}
		if (keep && steam_signin_save(options->signin_path, options->device_secret, options->device_secret_size, keep))
		{
			session.keep_token = 1;
			result->kept = 1;
			say(callbacks, "steam: the sign-in is kept on this device for 14 days (sealed to this console)");
			/* (the "not signed out" marker is for a live token nothing accounts for: this one is the file's) */
			if (options->marker_path)
				remove(options->marker_path);
		}
		else
			say(callbacks, "steam: the sign-in could not be kept on this device: it is revoked at the end");
		if (keep)
		{
			steam_wipe(keep, sizeof(*keep));
			free(keep);
		}
	}
signed_in:
	if (options->workshop)
	{
		/* (the list and the choice first; the CDN's servers after) */
		if (!callbacks->workshop_choose)
		{
			fail(result, "no Workshop list");
			goto end;
		}
		workshop_count = callbacks->workshop_choose(callbacks->context, &session, workshop_ids,
			STEAM_WORKSHOP_CHOSEN_MAXIMUM);
		if (workshop_count < 0)
			goto cancelled;
		if (session.kicked)
		{
			result->session_lost = 1;
			fail(result, "the Steam session ended");
			goto end;
		}
		if (workshop_count == 0)
		{
			result->ok = 1;
			goto end;
		}
		if (workshop_count > STEAM_WORKSHOP_CHOSEN_MAXIMUM)
			workshop_count = STEAM_WORKSHOP_CHOSEN_MAXIMUM;
	}
	else if (!steam_session_app_info(&session, STEAM_APP_MCC, &app_info, error, sizeof(error)))
	{
		fail(result, "%s", error);
		goto end;
	}
	if (options->cdn_override && options->cdn_override_count > 0)
	{
		for (index = 0; index < (size_t)options->cdn_override_count && index < CDN_MAXIMUM; index++)
			cdn[cdn_count++] = options->cdn_override[index];
	}
	else
	{
		int count = steam_session_cdn_servers(&session, servers, CDN_MAXIMUM, error, sizeof(error));

		if (count <= 0)
		{
			fail(result, "%s", error);
			goto end;
		}
		for (index = 0; index < (size_t)count; index++)
		{
			snprintf(urls[index], sizeof(urls[index]), "https://%.253s", servers[index].host);
			cdn[cdn_count++] = urls[index];
		}
	}
	if (options->workshop)
	{
		if (!workshop_flow(&session, options, callbacks, cdn, cdn_count, workshop_ids, workshop_count, result))
			goto end;
		result->ok = 1;
		goto end;
	}
	depot_count = steam_plan_depots(options->games, depots);
	for (index = 0; index < depot_count; index++)
	{
		if (callbacks->depot_start && callbacks->depot_start(callbacks->context, depots[index], (int)index, (int)depot_count))
			goto cancelled;
		if (!steam_download_depot(&session, &app_info, depots[index], options, callbacks, cdn, cdn_count, result))
			goto end;
	}
	result->ok = 1;
	goto end;
cancelled:
	result->cancelled = 1;
	fail(result, "cancelled");
end:
	steam_wipe(url, sizeof(url));
	steam_kv_free(&app_info);
	/* (a cancel cut the connections; the end's log-off - and a revoke, on a new connection if need be - may use them
	again: each bounded by its own timeouts) */
	if (steam_net_aborted())
	{
		/* (the CM's connection was cut under the session: dropped, so the end opens a new one if a token is to be
		revoked - a kept one is not, the end is then at once) */
		steam_session_drop(&session);
		steam_net_abort(0);
	}
	steam_session_end(&session);
	result->revoked = session.revoked;
	result->logged_off = session.logged_off;
}
