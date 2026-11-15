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
	char destination[160];
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
	int cdn_count = 0, state;

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
	if (!steam_session_logon(&session, error, sizeof(error)) ||
		!steam_session_app_info(&session, STEAM_APP_MCC, &app_info, error, sizeof(error)))
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
