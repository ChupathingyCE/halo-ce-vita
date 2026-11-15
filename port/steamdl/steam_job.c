/*
STEAM_JOB.C

steam_job.h: the download's worker thread and its snapshot.
*/

#include "steam_job.h"

#include "steam_crypto.h"
#include "steam_download.h"
#include "steam_plan.h"
#include "steam_session.h"

#include "qrcodegen.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef __vita__
#include <sys/statvfs.h>
#endif
#include <time.h>

#ifdef __vita__
/* (the Vita's: a TLS handshake and the decoders on this thread) */
#define JOB_STACK_BYTES (512 * 1024)
#else
#define JOB_STACK_BYTES (1024 * 1024)
#endif
/* a lost session starts the flow again with a new QR, at most this often */
#define JOB_RESTARTS 2
#define CM_DIRECTORY_URL "https://api.steampowered.com/ISteamDirectory/GetCMListForConnect/v1/?cellid=0&cmtype=websockets"

int posix_https_get(const char *url, const char *user_agent, char *body, int capacity, char *error, int error_size);

static struct
{
	pthread_mutex_t lock;
	int running;
	volatile int cancel;
	struct steam_job_status status;
	struct steam_job_options options;
	/* the rate's last sample */
	double sample_time;
	uint64_t sample_done;
	int sample_valid;
} job = { PTHREAD_MUTEX_INITIALIZER };

/* ---------- the platform's side, and its defaults */

static int64_t default_free_space(const char *root)
{
#ifdef __vita__
	(void)root;
	return -1;
#else
	struct statvfs info;
	char folder[256];
	size_t length;

	/* (the root may not be there yet: its parent's volume) */
	snprintf(folder, sizeof(folder), "%s", root);
	while (statvfs(folder, &info) != 0)
	{
		char *slash = strrchr(folder, '/');

		if (!slash || slash == folder)
			return -1;
		*slash = 0;
	}
	length = (size_t)info.f_bavail;
	return (int64_t)length * (int64_t)info.f_frsize;
#endif
}

static int default_battery_low(void)
{
	return 0;
}

static void default_nothing(void)
{
}

static void default_busy(int on)
{
	(void)on;
}

static void default_log(const char *line)
{
	fprintf(stderr, "%s\n", line);
}

/* (the PC: HALO_STEAM_ROOT, else steam-download beside the game; the Vita's
vita_steam.c gives ux0:data/haloce-vita for both) */
static void default_roots(char *ce_root, char *app_root, size_t size)
{
	const char *root = getenv("HALO_STEAM_ROOT");

	snprintf(ce_root, size, "%s", root && root[0] ? root : "steam-download");
	snprintf(app_root, size, "%s", root && root[0] ? root : "steam-download");
}

static int default_battery_ok(void)
{
	return 1;
}

static struct steam_job_platform platform = { default_free_space, default_battery_low, default_nothing, default_busy,
	default_log, default_roots, default_battery_ok };

void steam_job_set_platform(const struct steam_job_platform *given)
{
	if (!given)
		return;
	if (given->free_space)
		platform.free_space = given->free_space;
	if (given->battery_low)
		platform.battery_low = given->battery_low;
	if (given->keep_awake)
		platform.keep_awake = given->keep_awake;
	if (given->busy)
		platform.busy = given->busy;
	if (given->log)
		platform.log = given->log;
	if (given->roots)
		platform.roots = given->roots;
	if (given->battery_ok)
		platform.battery_ok = given->battery_ok;
}

static void job_options(struct steam_job_options *options, int games)
{
	memset(options, 0, sizeof(*options));
	options->games = games;
	platform.roots(options->ce_root, options->app_root, sizeof(options->ce_root));
#ifdef HALO_STEAM_TEST
	{
		/* (a test build alone: the fake server and a scratch folder, never the game's files) */
		const char *value;

		if ((value = getenv("HALO_STEAM_TEST_ROOT")) != NULL && value[0])
		{
			snprintf(options->ce_root, sizeof(options->ce_root), "%s/haloce-vita", value);
			snprintf(options->app_root, sizeof(options->app_root), "%s/haloce-vita", value);
		}
		if ((value = getenv("HALO_STEAM_TEST_CM")) != NULL)
			snprintf(options->cm_url, sizeof(options->cm_url), "%s", value);
		if ((value = getenv("HALO_STEAM_TEST_CDN")) != NULL)
			snprintf(options->cdn_override, sizeof(options->cdn_override), "%s", value);
	}
#endif
	snprintf(options->marker_path, sizeof(options->marker_path), "%.200s/steam-session.txt", options->app_root);
}

int steam_job_start_games(int games)
{
	struct steam_job_options options;

	if (!platform.battery_ok())
		return -1;
	job_options(&options, games);
	return steam_job_start(&options);
}

int steam_job_marker_check(char *date, size_t date_size)
{
	struct steam_job_options options;

	job_options(&options, 0);
	return steam_job_marker_stale(options.marker_path, date, date_size);
}

static void say(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void say(const char *format, ...)
{
	char line[320];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	platform.log(line);
}

static double now_seconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

/* ---------- the QR */

int steam_job_qr_encode(const char *text, uint8_t *modules, int maximum_size)
{
	uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)], temporary[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
	int size, x, y;

	if (!qrcodegen_encodeText(text, temporary, qr, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true))
		return 0;
	size = qrcodegen_getSize(qr);
	if (size > maximum_size)
		return 0;
	for (y = 0; y < size; y++)
		for (x = 0; x < size; x++)
			modules[y * size + x] = qrcodegen_getModule(qr, x, y) ? 1 : 0;
	steam_wipe(qr, sizeof(qr));
	steam_wipe(temporary, sizeof(temporary));
	return size;
}

/* ---------- the download's callbacks */

static void set_phase(int phase)
{
	pthread_mutex_lock(&job.lock);
	job.status.phase = phase;
	pthread_mutex_unlock(&job.lock);
}

static int show_qr(void *context, const char *url)
{
	uint8_t modules[STEAM_JOB_QR_MAXIMUM * STEAM_JOB_QR_MAXIMUM];
	int size = steam_job_qr_encode(url, modules, STEAM_JOB_QR_MAXIMUM);

	(void)context;
	pthread_mutex_lock(&job.lock);
	job.status.qr_size = size;
	memcpy(job.status.qr, modules, (size_t)(size * size));
	job.status.qr_serial++;
	job.status.phase = STEAM_JOB_QR;
	pthread_mutex_unlock(&job.lock);
	steam_wipe(modules, sizeof(modules));
	say("steam job: QR %u up (%d modules)", job.status.qr_serial, size);
	return size == 0 || job.cancel;
}

static int waiting(void *context)
{
	(void)context;
	platform.keep_awake();
	return job.cancel;
}

static int depot_start(void *context, uint32_t depot, int index, int count)
{
	(void)context;
	pthread_mutex_lock(&job.lock);
	job.status.phase = STEAM_JOB_PREPARING;
	job.status.depot = depot;
	job.status.depot_index = index;
	job.status.depot_count = count;
	/* (1.1.x: Halo CE alone; the sound banks come with depot 976733's files) */
	job.status.depot_game = STEAM_GAME_HALO_CE;
	job.status.done = job.status.total = 0;
	job.status.rate = 0;
	job.status.seconds_left = -1;
	job.sample_valid = 0;
	pthread_mutex_unlock(&job.lock);
	return job.cancel;
}

static int progress(void *context, uint64_t done, uint64_t total, const char *file)
{
	double now = now_seconds();

	(void)context;
	platform.keep_awake();
	pthread_mutex_lock(&job.lock);
	if (file)
	{
		job.status.phase = STEAM_JOB_DOWNLOADING;
		job.status.done = done;
		job.status.total = total;
		snprintf(job.status.file, sizeof(job.status.file), "%s", file);
		if (!job.sample_valid)
		{
			job.sample_time = now;
			job.sample_done = done;
			job.sample_valid = 1;
		}
		else if (now - job.sample_time >= 1.0)
		{
			double rate = (double)(done - job.sample_done) / (now - job.sample_time);

			job.status.rate = job.status.rate > 0 ? job.status.rate * 0.7 + rate * 0.3 : rate;
			job.sample_time = now;
			job.sample_done = done;
			job.status.seconds_left = job.status.rate > 1 ? (long)((double)(total - done) / job.status.rate) : -1;
		}
	}
	pthread_mutex_unlock(&job.lock);
	return job.cancel;
}

static int device_gate(void *context)
{
	int low = platform.battery_low();

	(void)context;
	pthread_mutex_lock(&job.lock);
	if (low && job.status.phase == STEAM_JOB_DOWNLOADING)
		job.status.phase = STEAM_JOB_PAUSED;
	else if (!low && job.status.phase == STEAM_JOB_PAUSED)
		job.status.phase = STEAM_JOB_DOWNLOADING;
	pthread_mutex_unlock(&job.lock);
	return low && !job.cancel;
}

static void log_line(void *context, const char *line)
{
	(void)context;
	platform.log(line);
}

static int64_t free_space(void *context, const char *root)
{
	(void)context;
	return platform.free_space(root);
}

/* ---------- the thread */

/* a CM from Steam's directory (no sign-in: the public list) */
static int directory_cm(char *url, size_t size)
{
	char *body = malloc(1u << 18), error[160], urls[4][STEAM_HOST_MAXIMUM + 32];
	int length, count;

	if (!body)
		return 0;
	length = posix_https_get(CM_DIRECTORY_URL, "Halo CE Vita", body, 1 << 18, error, sizeof(error));
	count = length > 0 ? steam_parse_cm_list(body, (size_t)length, urls, 4) : 0;
	free(body);
	if (count <= 0)
	{
		say("steam job: no CM from Steam's directory (%s)", length > 0 ? "none listed" : error);
		return 0;
	}
	snprintf(url, size, "%s", urls[0]);
	return 1;
}

static void *worker(void *unused)
{
	struct steam_download_options options;
	struct steam_download_callbacks callbacks;
	struct steam_download_result result;
	const char *cdn[1];
	char cm[STEAM_HOST_MAXIMUM + 32];
	int attempt, final_phase;

	(void)unused;
	platform.busy(1);
	memset(&options, 0, sizeof(options));
	options.games = (uint32_t)job.options.games;
	options.ce_root = job.options.ce_root;
	options.app_root = job.options.app_root;
	options.marker_path = job.options.marker_path[0] ? job.options.marker_path : NULL;
	if (job.options.cdn_override[0])
	{
		cdn[0] = job.options.cdn_override;
		options.cdn_override = cdn;
		options.cdn_override_count = 1;
	}
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.show_qr = show_qr;
	callbacks.waiting = waiting;
	callbacks.depot_start = depot_start;
	callbacks.progress = progress;
	callbacks.log = log_line;
	callbacks.free_space = free_space;
	callbacks.device_gate = device_gate;
	memset(&result, 0, sizeof(result));
	for (attempt = 0; attempt <= JOB_RESTARTS; attempt++)
	{
		set_phase(STEAM_JOB_CONNECTING);
		pthread_mutex_lock(&job.lock);
		job.status.attempts = attempt + 1;
		pthread_mutex_unlock(&job.lock);
		if (job.options.cm_url[0])
			snprintf(cm, sizeof(cm), "%s", job.options.cm_url);
		else if (!directory_cm(cm, sizeof(cm)))
		{
			snprintf(result.error, sizeof(result.error), "Steam could not be reached");
			break;
		}
		options.cm_url = cm;
		steam_download_run(&options, &callbacks, &result);
		say("steam job: sign-in %d ended: %s (revoked %d, logged off %d)", attempt + 1,
			result.ok ? "done" : result.error, result.revoked, result.logged_off);
		if (result.ok || result.cancelled || job.cancel || !result.session_lost)
			break;
		say("steam job: the session was lost: a new QR (the download resumes)");
	}
	final_phase = result.ok ? STEAM_JOB_DONE : result.cancelled || job.cancel ? STEAM_JOB_CANCELLED : STEAM_JOB_FAILED;
	{
		static const char *const names[] = { "VZa LZMA", "VSZa zstd", "zip" };
		int kind;

		for (kind = 0; kind < 3; kind++)
			if (result.decoded_chunks[kind])
				say("steam job: %s: %u chunks, %.1f MB out in %.2f s (%.1f MB/s)", names[kind], result.decoded_chunks[kind],
					(double)result.decoded_bytes[kind] / 1e6, (double)result.decode_us[kind] / 1e6,
					result.decode_us[kind] ? (double)result.decoded_bytes[kind] / (double)result.decode_us[kind] : 0.0);
		say("steam job: CM connection %u ms, largest chunk buffer %u bytes, network waits %u", result.connect_ms,
			(unsigned int)result.peak_chunk_buffer, result.network_waits);
	}
	pthread_mutex_lock(&job.lock);
	job.status.phase = final_phase;
	snprintf(job.status.error, sizeof(job.status.error), "%s", result.ok ? "" : result.error);
	job.status.files_done = result.files_done;
	job.status.files_already = result.files_already;
	job.status.network_waits = result.network_waits;
	job.status.revoked = result.revoked;
	job.status.logged_off = result.logged_off;
	job.status.session_lost = result.session_lost;
	job.status.connect_ms = result.connect_ms;
	memcpy(job.status.decode_us, result.decode_us, sizeof(job.status.decode_us));
	memcpy(job.status.decoded_bytes, result.decoded_bytes, sizeof(job.status.decoded_bytes));
	job.status.peak_chunk_buffer = result.peak_chunk_buffer;
	steam_wipe(job.status.qr, sizeof(job.status.qr));
	job.status.qr_size = 0;
	job.running = 0;
	pthread_mutex_unlock(&job.lock);
	platform.busy(0);
	return NULL;
}

int steam_job_start(const struct steam_job_options *options)
{
	pthread_attr_t attributes;
	pthread_t thread;
	int made;

	pthread_mutex_lock(&job.lock);
	if (job.running)
	{
		pthread_mutex_unlock(&job.lock);
		return 0;
	}
	memset(&job.status, 0, sizeof(job.status));
	job.options = *options;
	job.status.games = options->games;
	job.status.phase = STEAM_JOB_CONNECTING;
	job.status.seconds_left = -1;
	job.cancel = 0;
	job.running = 1;
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, JOB_STACK_BYTES);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	made = pthread_create(&thread, &attributes, worker, NULL) == 0;
	pthread_attr_destroy(&attributes);
	if (!made)
	{
		job.running = 0;
		job.status.phase = STEAM_JOB_FAILED;
		snprintf(job.status.error, sizeof(job.status.error), "no thread for the download");
	}
	pthread_mutex_unlock(&job.lock);
	say("steam job: started (games %d)%s", options->games, made ? "" : ": no thread");
	return made;
}

void steam_job_cancel(void)
{
	if (job.running && !job.cancel)
	{
		job.cancel = 1;
		say("steam job: cancelled by the player");
	}
}

void steam_job_abort(int timeout_ms)
{
	double until = now_seconds() + timeout_ms / 1000.0;

	if (!job.running)
		return;
	job.cancel = 1;
	say("steam job: aborted (a power warning or the app's exit)");
	while (job.running && now_seconds() < until)
	{
		struct timespec step = { 0, 20 * 1000 * 1000 };

		nanosleep(&step, NULL);
	}
}

void steam_job_get(struct steam_job_status *status)
{
	pthread_mutex_lock(&job.lock);
	*status = job.status;
	if (job.cancel && job.running)
		status->phase = STEAM_JOB_ENDING;
	pthread_mutex_unlock(&job.lock);
}

void steam_job_view(struct steam_job_view *view)
{
	struct steam_job_status status;

	steam_job_get(&status);
	memset(view, 0, sizeof(*view));
	view->phase = status.phase;
	view->games = status.games;
	view->qr_size = status.qr_size;
	view->qr_serial = status.qr_serial;
	memcpy(view->qr, status.qr, sizeof(view->qr));
	view->depot_index = status.depot_index;
	view->depot_count = status.depot_count;
	view->depot_game = status.depot_game;
	view->done_kb = (unsigned int)(status.done >> 10);
	view->total_kb = (unsigned int)(status.total >> 10);
	view->rate_kb = (unsigned int)(status.rate / 1024.0);
	view->seconds_left = (int)status.seconds_left;
	memcpy(view->file, status.file, sizeof(view->file));
	view->attempts = status.attempts;
	memcpy(view->error, status.error, sizeof(view->error));
	view->files_done = status.files_done;
	view->files_already = status.files_already;
	view->revoked = status.revoked;
	steam_wipe(status.qr, sizeof(status.qr));
}

int steam_job_forget(void)
{
	int forgot = 0;

	pthread_mutex_lock(&job.lock);
	if (!job.running)
	{
		memset(&job.status, 0, sizeof(job.status));
		forgot = 1;
	}
	pthread_mutex_unlock(&job.lock);
	return forgot;
}

int steam_job_marker_stale(const char *marker_path, char *date, size_t date_size)
{
	FILE *marker = fopen(marker_path, "rb");
	char text[256];
	size_t length;
	long started = 0;

	if (!marker)
		return 0;
	length = fread(text, 1, sizeof(text) - 1, marker);
	fclose(marker);
	text[length] = 0;
	if (sscanf(text, "Steam sign-in started %ld", &started) == 1 && started > 0)
	{
		time_t when = (time_t)started;
		struct tm *parts = gmtime(&when);

		if (parts)
			strftime(date, date_size, "%Y-%m-%d", parts);
		else
			snprintf(date, date_size, "?");
	}
	else
		snprintf(date, date_size, "?");
	return 1;
}

void steam_job_marker_clear(const char *marker_path)
{
	remove(marker_path);
}
