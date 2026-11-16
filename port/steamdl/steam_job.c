/*
STEAM_JOB.C

steam_job.h: the download's worker thread and its snapshot.
*/

#include "steam_job.h"

#include "steam_crypto.h"
#include "steam_download.h"
#include "steam_image.h"
#include "steam_plan.h"
#include "steam_session.h"
#include "steam_workshop.h"

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
	/* the Workshop: the page asked (serial) and the one shown, the choice, the player's last key */
	struct
	{
		unsigned int asked, served;
		int page, sort;
		char search[STEAM_WORKSHOP_SEARCH_SIZE];
		int chosen, chosen_count, lost;
		char chosen_ids[STEAM_WORKSHOP_CHOSEN_MAXIMUM][24];
		double touched;
		struct steam_job_workshop_view view;
		/* (the browser) the details page asked (serial) and served, its id, the picture asked */
		unsigned int details_asked, details_served, picture_asked, picture_served;
		char details_id[24];
		int details_picture;
		struct steam_job_workshop_details details;
	} workshop;
} job = { PTHREAD_MUTEX_INITIALIZER, 0, 0, { 0 }, { 0 }, 0, 0, 0, { 0 } };

/* (the browser's pictures: the job thread's, under job.lock; freed as the page changes and when the job ends) */
static struct
{
	uint8_t *cards[STEAM_WORKSHOP_PAGE_ITEMS];
	unsigned int card_serials[STEAM_WORKSHOP_PAGE_ITEMS];
	uint8_t *large;
	unsigned int large_serial;
	unsigned int serial;
	/* the page's preview URLs, the cards still to fetch, the details page's pictures */
	char urls[STEAM_WORKSHOP_PAGE_ITEMS][STEAM_IMAGE_URL_MAXIMUM + 1];
	int next;
	char details_urls[STEAM_WORKSHOP_PREVIEWS][STEAM_IMAGE_URL_MAXIMUM + 1];
	int details_count;
	uint64_t details_hcontent;
	int contents_pending;
	struct steam_workshop_item details_item;
	struct steam_image_fetcher fetcher;
	/* the pictures refused this session (a hash of the URL and size): never fetched again */
	uint64_t refused[64];
	int refused_count;
} pictures;

static uint64_t picture_key(const char *url, int width)
{
	uint64_t hash = 14695981039346656037ull ^ (uint64_t)width;

	for (; *url; url++)
	{
		hash ^= (uint8_t)*url;
		hash *= 1099511628211ull;
	}
	return hash;
}

/* (job thread) the picture refused before, or now remembered as refused */
static int picture_refused(const char *url, int width, int remember)
{
	uint64_t key = picture_key(url, width);
	int index;

	for (index = 0; index < pictures.refused_count; index++)
		if (pictures.refused[index] == key)
			return 1;
	if (remember)
	{
		if (pictures.refused_count < (int)(sizeof(pictures.refused) / sizeof(pictures.refused[0])))
			pictures.refused[pictures.refused_count++] = key;
		else
			pictures.refused[key % pictures.refused_count] = key;
	}
	return 0;
}

/* a picture made by steam_image_get unless refused already this session (then refused again, no fetch) */
static int picture_get(const char *url, int width, int height, int fit, uint8_t *pixels, char *why, size_t why_size)
{
	int ok;

	if (picture_refused(url, width, 0))
	{
		snprintf(why, why_size, "refused before");
		return 0;
	}
	ok = steam_image_get(&pictures.fetcher, job.options.ce_root, url, width, height, fit, pixels, why, why_size);
	if (!ok && pictures.fetcher.refused)
		picture_refused(url, width, 1);
	return ok;
}

/* the page's pictures let go (job.lock held) */
static void pictures_free_cards(void)
{
	int index;

	for (index = 0; index < STEAM_WORKSHOP_PAGE_ITEMS; index++)
	{
		free(pictures.cards[index]);
		pictures.cards[index] = NULL;
		pictures.card_serials[index] = 0;
		pictures.urls[index][0] = 0;
	}
	pictures.next = STEAM_WORKSHOP_PAGE_ITEMS;
}

static void pictures_free_all(void)
{
	pictures_free_cards();
	free(pictures.large);
	pictures.large = NULL;
	pictures.large_serial = 0;
	pictures.details_count = 0;
	pictures.contents_pending = 0;
}

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
		if ((value = getenv("HALO_STEAM_TEST_IMAGES")) != NULL)
			snprintf(options->images_override, sizeof(options->images_override), "%s", value);
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

/* ---------- the Workshop's list (on the job's thread, the session signed in) */

static void workshop_row(struct steam_job_workshop_row *row, const struct steam_workshop_item *item)
{
	char why[160];

	memset(row, 0, sizeof(*row));
	snprintf(row->id, sizeof(row->id), "%llu", (unsigned long long)item->id);
	snprintf(row->title, sizeof(row->title), "%s", item->title[0] ? item->title : row->id);
	row->size_kb = item->file_size >> 10 > 0xffffffffull ? 0xffffffffu : (unsigned int)(item->file_size >> 10);
	row->tags = item->tags;
	/* (campaign mods and multiplayer maps alike: a multiplayer item's maps are the multiplayer level list's, played
	offline and online - every player with their own download, never one player's copy sent to another:
	docs/steam_download_security.md section 8) */
	row->downloadable = steam_workshop_item_ok(item, why, sizeof(why));
	if (!row->downloadable)
		snprintf(row->reason, sizeof(row->reason), "%s", why);
	row->score = item->score;
	row->votes = item->votes_up + item->votes_down < item->votes_up ? 0xffffffffu : item->votes_up + item->votes_down;
	row->time_updated = item->time_updated;
	snprintf(row->version, sizeof(row->version), "%llu", (unsigned long long)item->hcontent);
	row->picture = item->preview_url[0] ? 0 : -1;
}

/* the authors of a page (one call; a name that does not come stays "") */
static void workshop_authors(struct steam_session *session, const struct steam_workshop_item *items, int count)
{
	uint64_t steamids[STEAM_WORKSHOP_PAGE_ITEMS];
	char names[STEAM_WORKSHOP_PAGE_ITEMS][STEAM_WORKSHOP_AUTHOR_SIZE];
	int unique = 0, index, other, found;

	for (index = 0; index < count && index < STEAM_WORKSHOP_PAGE_ITEMS; index++)
	{
		if (!items[index].creator)
			continue;
		for (other = 0; other < unique && steamids[other] != items[index].creator; other++)
			;
		if (other == unique)
			steamids[unique++] = items[index].creator;
	}
	if (!unique)
		return;
	found = steam_download_workshop_names(session, steamids, unique, names);
	say("steam job: workshop authors: %d of %d", found < 0 ? 0 : found, unique);
	if (found <= 0)
		return;
	pthread_mutex_lock(&job.lock);
	for (index = 0; index < count && index < STEAM_WORKSHOP_PAGE_ITEMS; index++)
		for (other = 0; other < unique; other++)
			if (steamids[other] == items[index].creator)
			{
				memcpy(job.workshop.view.rows[index].author, names[other], sizeof(job.workshop.view.rows[index].author));
				job.workshop.view.rows[index].author[sizeof(job.workshop.view.rows[index].author) - 1] = 0;
			}
	pthread_mutex_unlock(&job.lock);
}

/* (job.lock not held) the next card's picture of the page, one: 1 if one was tried */
static int workshop_card_picture(void)
{
	char url[STEAM_IMAGE_URL_MAXIMUM + 1], why[160];
	unsigned int asked;
	uint8_t *pixels;
	int slot, ok;

	pthread_mutex_lock(&job.lock);
	slot = pictures.next;
	while (slot < STEAM_WORKSHOP_PAGE_ITEMS && !pictures.urls[slot][0])
		slot++;
	pictures.next = slot + 1;
	if (slot >= STEAM_WORKSHOP_PAGE_ITEMS)
	{
		pictures.next = STEAM_WORKSHOP_PAGE_ITEMS;
		pthread_mutex_unlock(&job.lock);
		return 0;
	}
	memcpy(url, pictures.urls[slot], sizeof(url));
	asked = job.workshop.asked;
	pthread_mutex_unlock(&job.lock);
	pixels = malloc((size_t)STEAM_IMAGE_CARD_WIDTH * STEAM_IMAGE_CARD_HEIGHT * 4);
	if (!pixels)
		return 1;
	ok = picture_get(url, STEAM_IMAGE_CARD_WIDTH, STEAM_IMAGE_CARD_HEIGHT, STEAM_IMAGE_FILL, pixels, why, sizeof(why));
	say("steam job: workshop picture %d: %s", slot, ok ? "made" : why);
	pthread_mutex_lock(&job.lock);
	/* (still the page it was for) */
	if (job.workshop.asked == asked && job.workshop.served == asked && !strcmp(pictures.urls[slot], url))
	{
		job.workshop.view.rows[slot].picture = ok ? 1 : -1;
		if (ok)
		{
			free(pictures.cards[slot]);
			pictures.cards[slot] = pixels;
			pictures.card_serials[slot] = ++pictures.serial;
			pixels = NULL;
		}
	}
	pthread_mutex_unlock(&job.lock);
	free(pixels);
	return 1;
}

/* (job.lock not held) the details page asked for: its details, then its first picture (workshop_details_picture),
then what its files are (workshop_details_contents) */
static void workshop_details(struct steam_session *session)
{
	struct steam_workshop_details *details = calloc(1, sizeof(*details));
	struct steam_job_workshop_details *view;
	char id_text[24], error[160];
	unsigned int asked;
	uint64_t id = 0;
	int ok;

	pthread_mutex_lock(&job.lock);
	asked = job.workshop.details_asked;
	memcpy(id_text, job.workshop.details_id, sizeof(id_text));
	pthread_mutex_unlock(&job.lock);
	if (!details)
		return;
	ok = steam_workshop_id_parse(id_text, &id) && steam_download_workshop_page(session, id, details, error, sizeof(error));
	if (!ok && !id)
		snprintf(error, sizeof(error), "no such item");
	if (ok && details->item.creator)
	{
		char names[1][STEAM_WORKSHOP_AUTHOR_SIZE];

		if (steam_download_workshop_names(session, &details->item.creator, 1, names) <= 0)
			names[0][0] = 0;
		pthread_mutex_lock(&job.lock);
		snprintf(job.workshop.details.author, sizeof(job.workshop.details.author), "%s", names[0]);
		pthread_mutex_unlock(&job.lock);
	}
	say("steam job: workshop details %s: %s (%d pictures)", id_text, ok ? "here" : error, ok ? details->preview_count : 0);
	pthread_mutex_lock(&job.lock);
	view = &job.workshop.details;
	if (job.workshop.details_asked == asked)
	{
		char author[sizeof(view->author)];

		memcpy(author, view->author, sizeof(author));
		job.workshop.details_served = asked;
		memset(view, 0, sizeof(*view));
		view->serial = asked;
		snprintf(view->id, sizeof(view->id), "%s", id_text);
		if (!ok)
		{
			view->state = -1;
			snprintf(view->error, sizeof(view->error), "%s", error);
		}
		else
		{
			struct steam_job_workshop_row row;
			int index;

			workshop_row(&row, &details->item);
			view->state = 2;
			memcpy(view->author, author, sizeof(view->author));
			snprintf(view->title, sizeof(view->title), "%s", row.title);
			memcpy(view->description, details->description, sizeof(view->description));
			view->size_kb = row.size_kb;
			view->time_updated = details->item.time_updated;
			view->time_created = details->item.time_created;
			view->votes_up = details->item.votes_up;
			view->votes_down = details->item.votes_down;
			view->score = details->item.score;
			view->tags = details->item.tags;
			view->downloadable = row.downloadable;
			memcpy(view->reason, row.reason, sizeof(view->reason));
			memcpy(view->version, row.version, sizeof(view->version));
			view->pictures = details->preview_count;
			view->picture_index = 0;
			view->picture = details->preview_count ? 0 : -1;
			view->contents = details->item.hcontent ? 0 : -1;
			if (!details->item.hcontent)
				snprintf(view->contents_why, sizeof(view->contents_why), "%s", "the item has no files");
			for (index = 0; index < details->preview_count; index++)
				memcpy(pictures.details_urls[index], details->previews[index], sizeof(pictures.details_urls[0]));
			pictures.details_count = details->preview_count;
			pictures.details_item = details->item;
			pictures.contents_pending = details->item.hcontent != 0;
			free(pictures.large);
			pictures.large = NULL;
			pictures.large_serial = 0;
			job.workshop.details_picture = 0;
			job.workshop.picture_asked++;
		}
	}
	pthread_mutex_unlock(&job.lock);
	free(details);
}

/* (job.lock not held) the details page's picture asked for */
static void workshop_details_picture(void)
{
	char url[STEAM_IMAGE_URL_MAXIMUM + 1], why[160];
	unsigned int asked, details;
	uint8_t *pixels;
	int index, ok = 0;

	pthread_mutex_lock(&job.lock);
	asked = job.workshop.picture_asked;
	details = job.workshop.details_served;
	index = job.workshop.details_picture;
	job.workshop.picture_served = asked;
	if (index < 0 || index >= pictures.details_count)
	{
		job.workshop.details.picture = -1;
		pthread_mutex_unlock(&job.lock);
		return;
	}
	memcpy(url, pictures.details_urls[index], sizeof(url));
	job.workshop.details.picture = 0;
	job.workshop.details.picture_index = index;
	pthread_mutex_unlock(&job.lock);
	pixels = malloc((size_t)STEAM_IMAGE_LARGE_WIDTH * STEAM_IMAGE_LARGE_HEIGHT * 4);
	if (pixels)
		ok = picture_get(url, STEAM_IMAGE_LARGE_WIDTH, STEAM_IMAGE_LARGE_HEIGHT, STEAM_IMAGE_FIT, pixels, why, sizeof(why));
	else
		snprintf(why, sizeof(why), "out of memory");
	say("steam job: workshop details picture %d: %s", index, ok ? "made" : why);
	pthread_mutex_lock(&job.lock);
	if (job.workshop.details_served == details && job.workshop.picture_asked == asked)
	{
		job.workshop.details.picture = ok ? 1 : -1;
		if (ok)
		{
			free(pictures.large);
			pictures.large = pixels;
			pictures.large_serial = ++pictures.serial;
			pixels = NULL;
		}
	}
	pthread_mutex_unlock(&job.lock);
	free(pixels);
}

/* (job.lock not held) what the details page's item's files are */
static void workshop_details_contents(struct steam_session *session)
{
	struct steam_workshop_contents *contents = calloc(1, sizeof(*contents));
	struct steam_workshop_item item;
	const char *cdn[1];
	char error[160];
	unsigned int details;
	int ok = 0, index;

	pthread_mutex_lock(&job.lock);
	pictures.contents_pending = 0;
	details = job.workshop.details_served;
	item = pictures.details_item;
	pthread_mutex_unlock(&job.lock);
	cdn[0] = job.options.cdn_override;
	if (contents)
		ok = steam_download_workshop_contents(session, &item, cdn, job.options.cdn_override[0] ? 1 : 0, contents, error,
			sizeof(error));
	else
		snprintf(error, sizeof(error), "out of memory");
	say("steam job: workshop contents %llu: %s", (unsigned long long)item.id, ok ? "read" : error);
	pthread_mutex_lock(&job.lock);
	if (job.workshop.details_served == details)
	{
		struct steam_job_workshop_details *view = &job.workshop.details;

		view->contents = ok ? 1 : -1;
		if (ok)
		{
			view->maps = contents->maps;
			view->files = contents->files;
			view->skipped = contents->skipped;
			view->refused = contents->refused;
			view->contents_kb = contents->bytes >> 10 > 0xffffffffull ? 0xffffffffu : (unsigned int)(contents->bytes >> 10);
			for (index = 0; index < STEAM_WORKSHOP_MAP_NAMES && index < contents->maps; index++)
				memcpy(view->map_names[index], contents->map_names[index], sizeof(view->map_names[0]));
			memcpy(view->contents_why, contents->why, sizeof(view->contents_why));
		}
		else
			snprintf(view->contents_why, sizeof(view->contents_why), "%s", error);
	}
	pthread_mutex_unlock(&job.lock);
	free(contents);
}

static int job_workshop_choose(void *context, struct steam_session *session, uint64_t *ids, int maximum)
{
	(void)context;
	pthread_mutex_lock(&job.lock);
	job.status.phase = STEAM_JOB_BROWSING;
	job.workshop.view.phase = STEAM_JOB_BROWSING;
	job.workshop.touched = now_seconds();
	/* (a session started again after a loss: the page asked again) */
	job.workshop.served = job.workshop.asked - 1;
	pictures_free_all();
	pictures.refused_count = 0;
	snprintf(pictures.fetcher.test_base, sizeof(pictures.fetcher.test_base), "%s", job.options.images_override);
	pthread_mutex_unlock(&job.lock);
	say("steam job: workshop list open");
	for (;;)
	{
		struct steam_workshop_item items[STEAM_WORKSHOP_PAGE_ITEMS];
		char search[STEAM_WORKSHOP_SEARCH_SIZE], error[160];
		unsigned int asked;
		uint32_t total = 0;
		int page, sort, count, index;

		platform.keep_awake();
		if (job.cancel)
			return -1;
		pthread_mutex_lock(&job.lock);
		if (job.workshop.chosen)
		{
			count = 0;
			for (index = 0; index < job.workshop.chosen_count && count < maximum; index++)
				if (steam_workshop_id_parse(job.workshop.chosen_ids[index], &ids[count]))
					count++;
			job.status.phase = STEAM_JOB_PREPARING;
			pictures_free_all();
			pthread_mutex_unlock(&job.lock);
			steam_image_fetcher_close(&pictures.fetcher);
			say("steam job: workshop: %d items chosen", count);
			return count;
		}
		if (now_seconds() - job.workshop.touched > STEAM_JOB_WORKSHOP_IDLE_SECONDS)
		{
			pthread_mutex_unlock(&job.lock);
			say("steam job: workshop list left alone %d s: signing out", STEAM_JOB_WORKSHOP_IDLE_SECONDS);
			return 0;
		}
		asked = job.workshop.asked;
		if (asked == job.workshop.served)
		{
			int details = job.workshop.details_asked != job.workshop.details_served;
			int picture = !details && job.workshop.details_served && job.workshop.picture_asked != job.workshop.picture_served;
			int contents = !details && !picture && pictures.contents_pending;

			pthread_mutex_unlock(&job.lock);
			/* (the details page first, its picture, what its files are; then the cards' pictures, one at a time) */
			if (details)
				workshop_details(session);
			else if (picture)
				workshop_details_picture();
			else if (contents)
				workshop_details_contents(session);
			else if (!workshop_card_picture() && (!steam_session_pump(session, 250) || session->kicked))
			{
				pthread_mutex_lock(&job.lock);
				job.workshop.lost = 1;
				pthread_mutex_unlock(&job.lock);
				say("steam job: workshop: the session ended under the list");
				return 0;
			}
			if (session->kicked)
			{
				pthread_mutex_lock(&job.lock);
				job.workshop.lost = 1;
				pthread_mutex_unlock(&job.lock);
				say("steam job: workshop: the session ended under the list");
				return 0;
			}
			continue;
		}
		page = job.workshop.page;
		sort = job.workshop.sort;
		memcpy(search, job.workshop.search, sizeof(search));
		job.workshop.view.loading = 1;
		/* (the page's pictures let go: the cards' images are freed off-page) */
		pictures_free_cards();
		pthread_mutex_unlock(&job.lock);
		count = steam_download_workshop_query(session, page, (enum steam_workshop_sort)sort, search, items,
			STEAM_WORKSHOP_PAGE_ITEMS, &total, error, sizeof(error));
		say("steam job: workshop page %d (list %d%s): %d items of %u", page, sort, search[0] ? ", a search" : "", count,
			(unsigned int)total);
		pthread_mutex_lock(&job.lock);
		job.workshop.served = asked;
		job.workshop.view.loading = 0;
		job.workshop.view.serial++;
		job.workshop.view.page = page;
		job.workshop.view.sort = sort;
		memcpy(job.workshop.view.search, search, sizeof(job.workshop.view.search));
		if (count < 0)
		{
			job.workshop.view.count = 0;
			snprintf(job.workshop.view.error, sizeof(job.workshop.view.error), "%s", error);
		}
		else
		{
			job.workshop.view.error[0] = 0;
			job.workshop.view.total = (int)(total > 100000 ? 100000 : total);
			job.workshop.view.count = count;
			for (index = 0; index < count; index++)
			{
				workshop_row(&job.workshop.view.rows[index], &items[index]);
				memcpy(pictures.urls[index], items[index].preview_url, sizeof(pictures.urls[0]));
			}
			pictures.next = 0;
		}
		pthread_mutex_unlock(&job.lock);
		if (count > 0)
			workshop_authors(session, items, count);
	}
}

static int job_item_start(void *context, const struct steam_workshop_item *item, int index, int count)
{
	(void)context;
	pthread_mutex_lock(&job.lock);
	job.workshop.view.item_index = index;
	job.workshop.view.item_count = count;
	snprintf(job.workshop.view.item_title, sizeof(job.workshop.view.item_title), "%s", item->title);
	job.status.phase = STEAM_JOB_PREPARING;
	job.status.done = job.status.total = 0;
	job.status.rate = 0;
	job.status.seconds_left = -1;
	job.sample_valid = 0;
	pthread_mutex_unlock(&job.lock);
	return job.cancel;
}

static void job_item_end(void *context, const struct steam_workshop_item *item, int ok, const char *why)
{
	(void)context;
	pthread_mutex_lock(&job.lock);
	if (ok)
		job.workshop.view.items_done++;
	else
	{
		job.workshop.view.items_failed++;
		snprintf(job.workshop.view.failure, sizeof(job.workshop.view.failure), "%.60s: %.130s",
			item->title[0] ? item->title : "an item", why);
	}
	pthread_mutex_unlock(&job.lock);
	say("steam job: workshop item %llu %s", (unsigned long long)item->id, ok ? "downloaded" : "failed");
}

int steam_job_start_workshop(const char *data_root)
{
	struct steam_job_options options;

	if (!platform.battery_ok())
		return -1;
	job_options(&options, 0);
	options.workshop = 1;
	snprintf(options.ce_root, sizeof(options.ce_root), "%s", data_root);
	return steam_job_start(&options);
}

void steam_job_workshop_ask(int page, int sort, const char *search)
{
	pthread_mutex_lock(&job.lock);
	job.workshop.page = page < 1 ? 1 : page;
	job.workshop.sort = sort >= 0 && sort <= STEAM_WORKSHOP_LIST_SUBSCRIBED ? sort : 0;
	snprintf(job.workshop.search, sizeof(job.workshop.search), "%s", search ? search : "");
	job.workshop.asked++;
	job.workshop.view.loading = 1;
	job.workshop.touched = now_seconds();
	pthread_mutex_unlock(&job.lock);
}

void steam_job_workshop_touch(void)
{
	pthread_mutex_lock(&job.lock);
	job.workshop.touched = now_seconds();
	pthread_mutex_unlock(&job.lock);
}

void steam_job_workshop_choose(const char (*ids)[24], int count)
{
	int index;

	pthread_mutex_lock(&job.lock);
	job.workshop.chosen_count = 0;
	for (index = 0; index < count && index < STEAM_WORKSHOP_CHOSEN_MAXIMUM; index++)
	{
		memcpy(job.workshop.chosen_ids[index], ids[index], 24);
		job.workshop.chosen_ids[index][23] = 0;
		job.workshop.chosen_count++;
	}
	job.workshop.chosen = 1;
	pthread_mutex_unlock(&job.lock);
}

void steam_job_workshop_view(struct steam_job_workshop_view *view)
{
	pthread_mutex_lock(&job.lock);
	*view = job.workshop.view;
	view->phase = job.cancel && job.running ? STEAM_JOB_ENDING : job.status.phase;
	pthread_mutex_unlock(&job.lock);
}

void steam_job_workshop_details_ask(const char *id)
{
	pthread_mutex_lock(&job.lock);
	snprintf(job.workshop.details_id, sizeof(job.workshop.details_id), "%s", id ? id : "");
	job.workshop.touched = now_seconds();
	if (!id || !id[0])
	{
		/* (closed: what it held let go; a details call on its way is dropped as it comes) */
		job.workshop.details_asked++;
		job.workshop.details_served = job.workshop.details_asked;
		memset(&job.workshop.details, 0, sizeof(job.workshop.details));
		free(pictures.large);
		pictures.large = NULL;
		pictures.large_serial = 0;
		pictures.details_count = 0;
		pictures.contents_pending = 0;
		job.workshop.picture_served = job.workshop.picture_asked;
	}
	else
	{
		job.workshop.details_asked++;
		memset(&job.workshop.details, 0, sizeof(job.workshop.details));
		job.workshop.details.state = 1;
		snprintf(job.workshop.details.id, sizeof(job.workshop.details.id), "%s", id);
		free(pictures.large);
		pictures.large = NULL;
		pictures.large_serial = 0;
	}
	pthread_mutex_unlock(&job.lock);
}

void steam_job_workshop_details_picture(int index)
{
	pthread_mutex_lock(&job.lock);
	job.workshop.touched = now_seconds();
	if (job.workshop.details.state == 2 && index >= 0 && index < job.workshop.details.pictures)
	{
		job.workshop.details_picture = index;
		job.workshop.details.picture_index = index;
		job.workshop.details.picture = 0;
		job.workshop.picture_asked++;
	}
	pthread_mutex_unlock(&job.lock);
}

void steam_job_workshop_details(struct steam_job_workshop_details *details)
{
	pthread_mutex_lock(&job.lock);
	*details = job.workshop.details;
	pthread_mutex_unlock(&job.lock);
}

unsigned int steam_job_workshop_picture(int slot, unsigned char *pixels, int capacity, int *width, int *height)
{
	unsigned int serial = 0;
	const uint8_t *source = NULL;
	int w = 0, h = 0;

	pthread_mutex_lock(&job.lock);
	if (slot == -1 && pictures.large)
	{
		source = pictures.large;
		serial = pictures.large_serial;
		w = STEAM_IMAGE_LARGE_WIDTH;
		h = STEAM_IMAGE_LARGE_HEIGHT;
	}
	else if (slot >= 0 && slot < STEAM_WORKSHOP_PAGE_ITEMS && pictures.cards[slot])
	{
		source = pictures.cards[slot];
		serial = pictures.card_serials[slot];
		w = STEAM_IMAGE_CARD_WIDTH;
		h = STEAM_IMAGE_CARD_HEIGHT;
	}
	if (width)
		*width = w;
	if (height)
		*height = h;
	if (source && pixels)
	{
		if (capacity >= w * h * 4)
			memcpy(pixels, source, (size_t)w * (size_t)h * 4);
		else
			serial = 0;
	}
	pthread_mutex_unlock(&job.lock);
	return serial;
}

void steam_job_workshop_memory(unsigned int *held, unsigned int *decoder_peak)
{
	unsigned int bytes = 0;
	int index;

	pthread_mutex_lock(&job.lock);
	for (index = 0; index < STEAM_WORKSHOP_PAGE_ITEMS; index++)
		if (pictures.cards[index])
			bytes += STEAM_IMAGE_CARD_WIDTH * STEAM_IMAGE_CARD_HEIGHT * 4;
	if (pictures.large)
		bytes += STEAM_IMAGE_LARGE_WIDTH * STEAM_IMAGE_LARGE_HEIGHT * 4;
	pthread_mutex_unlock(&job.lock);
	if (held)
		*held = bytes;
	if (decoder_peak)
		*decoder_peak = (unsigned int)steam_image_decode_peak();
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
	options.workshop = job.options.workshop;
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
	if (job.options.workshop)
	{
		callbacks.workshop_choose = job_workshop_choose;
		callbacks.item_start = job_item_start;
		callbacks.item_end = job_item_end;
	}
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
	steam_image_fetcher_close(&pictures.fetcher);
	if (job.options.workshop && result.ok && job.workshop.lost && !job.workshop.chosen)
	{
		result.ok = 0;
		snprintf(result.error, sizeof(result.error), "The Steam session was lost");
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
	/* (the browser's pictures let go; the connection to the image CDN closed) */
	pictures_free_all();
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
	memset(&job.workshop, 0, sizeof(job.workshop));
	job.workshop.asked = 1;
	job.workshop.page = 1;
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
		memset(&job.workshop, 0, sizeof(job.workshop));
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
