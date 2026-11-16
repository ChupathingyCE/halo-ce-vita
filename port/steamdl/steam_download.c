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

static uint8_t *cdn_get(struct cdn *cdn, const char *path, size_t maximum, size_t *size, char *error, int error_size,
	int timeout_ms)
{
	/* (a round tries each server twice, at most 6 tries; between rounds the Wi-Fi may come back: waits of 2, 5, 10,
	20, 30 and 30 s, about 1.5 minutes in all, before the download stops - resumable) */
	static const int waits_ms[] = { 2000, 5000, 10000, 20000, 30000, 30000 };
	int attempt, round, refused = 0;

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
				return body;
			/* (this server failed: the next one; a server's own refusal is no Wi-Fi loss: no more rounds) */
			steam_http_close(&cdn->http);
			cdn->current = (cdn->current + 1) % cdn->count;
			if (status == 403 || status == 404)
				refused = 1;
		}
	}
	return NULL;
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
	opened = steam_file_open(&job, path, depot, gid, planned->file, planned->kind, planned->destination, error,
		sizeof(error));
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
				CHUNK_TIMEOUT_MS);

			if (!data)
			{
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
	if (!steam_file_finish(&job, error, sizeof(error)))
	{
		fail(result, "%s", error);
		goto stopped;
	}
	say(callbacks, "%s: done (%llu bytes)", planned->destination, (unsigned long long)planned->file->size);
	result->files_done++;
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
	zipped = cdn_get(&cdn, path, STEAM_MANIFEST_ZIPPED_MAXIMUM, &zipped_size, error, sizeof(error), MANIFEST_TIMEOUT_MS);
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
	if (metadata.depot_id != depot || metadata.gid != gid || !steam_manifest_crc_ok(manifest, manifest_size, &metadata))
	{
		fail(result, "depot %u's manifest is not the one asked for", (unsigned int)depot);
		goto done;
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
	for (index = 0; index < collected.count; index++)
	{
		struct planned *planned = &collected.files[index];

		if (!download_file(session, planned, depot, gid, key, &cdn, options, callbacks, result, &done, total))
			goto done;
	}
	ok = 1;
done:
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
	zipped = cdn_get(&cdn, path, STEAM_MANIFEST_ZIPPED_MAXIMUM, &zipped_size, error, sizeof(error), MANIFEST_TIMEOUT_MS);
	steam_wipe(path, sizeof(path));
	code = 0;
	if (!zipped)
	{
		fail(result, "item %s's manifest: %s", id_text, error);
		goto done;
	}
	if (!steam_zip_entry_size(zipped, zipped_size, STEAM_MANIFEST_MAXIMUM, &manifest_size) ||
		!(manifest = malloc(manifest_size + 1)) ||
		!steam_zip_extract(zipped, zipped_size, manifest, manifest_size, error, sizeof(error)))
	{
		fail(result, "item %s's manifest is not a zip of one file", id_text);
		goto done;
	}
	snprintf(collected.prefix, sizeof(collected.prefix), "workshop/%s", id_text);
	collected.callbacks = callbacks;
	if (steam_manifest_parse(manifest, manifest_size, key, &metadata, collect_workshop, &collected, error,
		sizeof(error)) != 0)
	{
		fail(result, "item %s's manifest refused: %s", id_text, error[0] ? error : "malformed");
		goto done;
	}
	if (metadata.depot_id != STEAM_WORKSHOP_DEPOT || metadata.gid != item->hcontent ||
		!steam_manifest_crc_ok(manifest, manifest_size, &metadata))
	{
		fail(result, "item %s's manifest is not the one asked for", id_text);
		goto done;
	}
	if (collected.refused)
	{
		fail(result, "item %s refused: %s", id_text, collected.why);
		goto done;
	}
	if (collected.skipped)
		say(callbacks, "workshop %s: %u files of other kinds left out", id_text, collected.skipped);
	total = (uint64_t)planned_needed(&collected.base);
	if (!collected.base.count || total > STEAM_WORKSHOP_ITEM_BYTES_MAXIMUM)
	{
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

int steam_download_workshop_query(struct steam_session *session, int page, enum steam_workshop_sort sort,
	const char *search, struct steam_workshop_item *items, int maximum, uint32_t *total, char *error, int error_size)
{
	uint8_t body[256];
	const uint8_t *reply;
	size_t size = steam_workshop_query_request(body, sizeof(body), page, sort, search), reply_size;
	int count;

	if (!size)
	{
		snprintf(error, (size_t)error_size, "a search too long");
		return -1;
	}
	if (!steam_session_call(session, "PublishedFile.QueryFiles#1", body, size, &reply, &reply_size, error, error_size))
		return -1;
	count = steam_workshop_parse_query(reply, reply_size, items, maximum, total);
	if (count < 0)
		snprintf(error, (size_t)error_size, "the Workshop's list was malformed");
	return count;
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
		if (steam_download_workshop_item(session, item, options, callbacks, cdn, cdn_count, result))
		{
			result->items_done++;
			if (callbacks->item_end)
				callbacks->item_end(callbacks->context, item, 1, "");
			continue;
		}
		/* (a cancel or a lost session ends them all, its download kept for a resume; a refused or failed item: its
		staging folder removed - nothing of it kept - and the next one goes on) */
		if (result->cancelled || result->session_lost || session->kicked)
			return 0;
		{
			char staging[512], gid_path[512], done_path[512];

			if (steam_workshop_paths(options->ce_root, item->id, staging, gid_path, done_path, NULL, sizeof(staging)))
			{
				steam_workshop_remove_tree(staging);
				remove(gid_path);
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
	int cdn_count = 0, state, workshop_count = 0;
	uint64_t workshop_ids[STEAM_WORKSHOP_CHOSEN_MAXIMUM];

	memset(result, 0, sizeof(*result));
	memset(&app_info, 0, sizeof(app_info));
	if (!steam_session_init(&session, callbacks->log, callbacks->context, options->marker_path))
	{
		fail(result, "out of memory");
		steam_session_end(&session);
		return;
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
	steam_session_end(&session);
	result->revoked = session.revoked;
	result->logged_off = session.logged_off;
}
