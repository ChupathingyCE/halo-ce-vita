/*
STEAM_JOB.H

The Steam download as the game runs it (phase 2): one job at a time on a
worker thread of its own (steam_download_run, docs/steam_download_security.md),
and a snapshot of where it is for the screens to show (the menus' DOWNLOAD
FROM STEAM, port/linux/game/menu_functions.c h3_steam_*) - the phase, the QR's modules
(made here from Steam's challenge URL, which never leaves this file and is
never logged), the game in hand, the bytes, the rate and the time left, the
error. The device's side (free space, the battery, staying awake, the PS
button's lock, the power warnings) comes through steam_job_platform: the
Vita's in port/vita/host/vita_steam.c, the PC's the defaults below.

The CM session lost under the download (the Wi-Fi gone too long, the server's
log-off) starts the flow again with a new QR, at most twice: the files
already in, and the chunks of the one in hand, are kept (the journal).
*/

#ifndef STEAM_JOB_H
#define STEAM_JOB_H

#include <stddef.h>
#include <stdint.h>

enum steam_job_phase
{
	STEAM_JOB_IDLE,
	STEAM_JOB_CONNECTING,
	/* the QR is up, waiting for the player's Steam app */
	STEAM_JOB_QR,
	/* approved: signing in, the app's info, the depot's key and manifest */
	STEAM_JOB_PREPARING,
	/* (the Workshop) signed in: the list, the player choosing (steam_job_workshop_*) */
	STEAM_JOB_BROWSING,
	STEAM_JOB_DOWNLOADING,
	/* the battery low and not charging */
	STEAM_JOB_PAUSED,
	/* revoking and logging off (the end, whatever happened) */
	STEAM_JOB_ENDING,
	STEAM_JOB_DONE,
	STEAM_JOB_FAILED,
	STEAM_JOB_CANCELLED,
};

/* the QR's side at most: version 10, 57 modules */
#define STEAM_JOB_QR_MAXIMUM 57

struct steam_job_status
{
	int phase;
	int games; /* STEAM_GAME_* chosen */
	/* the QR: size x size modules, 1 dark; serial changes with each new QR */
	int qr_size;
	unsigned int qr_serial;
	uint8_t qr[STEAM_JOB_QR_MAXIMUM * STEAM_JOB_QR_MAXIMUM];
	/* the depot in hand (index of count), its game (STEAM_GAME_*), its bytes on the card done of total */
	uint32_t depot;
	int depot_index, depot_count, depot_game;
	uint64_t done, total;
	/* bytes a second (smoothed), seconds left (-1 unknown) */
	double rate;
	long seconds_left;
	/* the file in hand (a destination of the plan's: no secret) */
	char file[96];
	/* sign-ins this job (a lost session starts a new one) */
	int attempts;
	/* the last error, the files done and already there, the end's facts */
	char error[256];
	unsigned int files_done, files_already, network_waits;
	int revoked, logged_off, session_lost;
	/* the measurements the dev Vita run reads (also logged at the end) */
	unsigned int connect_ms;
	uint64_t decode_us[3], decoded_bytes[3];
	size_t peak_chunk_buffer;
};

struct steam_job_options
{
	int games;
	char ce_root[256], app_root[256];
	char marker_path[256];
	/* "" asks Steam's directory for a CM (ISteamDirectory, over TLS); the tests' fake otherwise */
	char cm_url[256];
	/* the tests' CDN instead of Steam's directory: "" in the game */
	char cdn_override[256];
	/* the Steam Workshop (steam_workshop.h) instead of the game's depots: ce_root is the data root (its workshop
	and mods folders) */
	int workshop;
	/* (the browser) the tests' image server instead of Steam's image CDN (http://127.0.0.1:port): "" in the game; a
	test build's HALO_STEAM_TEST_IMAGES alone. Every picture's URL is checked first as ever (steam_image_url) */
	char images_override[128];
};

struct steam_job_platform
{
	/* the free bytes where a root is, -1 unknown */
	int64_t (*free_space)(const char *root);
	/* nonzero: the battery is low and not charging (the download pauses) */
	int (*battery_low)(void);
	/* called often while a job runs: keep the device awake (no auto-suspend) */
	void (*keep_awake)(void);
	/* a job begun (1) or ended (0): the PS button's lock on the Vita */
	void (*busy)(int on);
	void (*log)(const char *line);
	/* where Halo CE's files go (ux0:data/haloce-vita) and the app's own folder (the marker; 1.1.x: the same) */
	void (*roots)(char *ce_root, char *app_root, size_t size);
	/* nonzero: the battery allows a start (the Vita: 30% or charging) */
	int (*battery_ok)(void);
};

/* the device's side; NULL or a NULL member keeps the default (statvfs, never low, nothing, nothing, stderr) */
void steam_job_set_platform(const struct steam_job_platform *platform);

/* a job begun: 1, or 0 if one is running already */
int steam_job_start(const struct steam_job_options *options);
/* (the screens') a job for these games (STEAM_GAME_*) begun with the platform's folders - in a test build
(HALO_STEAM_TEST) the environment's HALO_STEAM_TEST_ROOT, _CM and _CDN: 1, 0 if one runs, -1 if the battery is too
low to start */
int steam_job_start_games(int games);
/* (the screens') a sign-in left behind (its marker in the app's folder): 1 with its date */
int steam_job_marker_check(char *date, size_t date_size);

/* the player's cancel (Circle): the session ends as on any path */
void steam_job_cancel(void);
/* a power warning or the app's exit: cancel and wait (at most timeout_ms) until the session has ended */
void steam_job_abort(int timeout_ms);
/* where the job is */
void steam_job_get(struct steam_job_status *status);
/* the snapshot for the game's own code (the menus: port/linux/game/steam_screen.c), in ints and bytes alone -
the game's units are built with another ABI than this file's (the Linux harness's -malign-double), so no 64-bit
field or double crosses */
struct steam_job_view
{
	int phase, games;
	int qr_size;
	unsigned int qr_serial;
	unsigned char qr[STEAM_JOB_QR_MAXIMUM * STEAM_JOB_QR_MAXIMUM];
	int depot_index, depot_count, depot_game;
	/* the depot's bytes on the card done and in all, in kilobytes; kilobytes a second; seconds left (-1 unknown) */
	unsigned int done_kb, total_kb, rate_kb;
	int seconds_left;
	char file[96];
	int attempts;
	char error[256];
	unsigned int files_done, files_already;
	int revoked;
};
void steam_job_view(struct steam_job_view *view);

/* a finished job's state cleared (back to IDLE); 0 while one runs */
int steam_job_forget(void);
/* a marker left by a sign-in never signed out (the app killed mid-download): 1, with its date as text */
int steam_job_marker_stale(const char *marker_path, char *date, size_t date_size);
/* the player read the warning: the marker removed */
void steam_job_marker_clear(const char *marker_path);

/* ---------- the Steam Workshop (steam_workshop.h): one signed-in session, the list, the choice, the download

The player signs in by QR as for the depots; then, while the phase is STEAM_JOB_BROWSING, the screen asks for pages
(steam_job_workshop_ask) and reads them (steam_job_workshop_view), and ends the list with the items chosen
(steam_job_workshop_choose; none: the session just ends). The chosen items go to <data root>/workshop/<id>; after the
job, the game installs each into mods/<id> (steam_workshop_install with its own map check). The list is left after
STEAM_JOB_WORKSHOP_IDLE_SECONDS without a key pressed (steam_job_workshop_ask or _touch). */

#define STEAM_JOB_WORKSHOP_IDLE_SECONDS 600

/* a Workshop job begun: data_root the game's (ux0:data/haloce-vita): 1, 0 if one runs, -1 if the battery is low */
int steam_job_start_workshop(const char *data_root);
/* the page asked for (1 the first), the order (steam_workshop_sort), the search ("" none) */
void steam_job_workshop_ask(int page, int sort, const char *search);
/* the player is still there (a key pressed on the list) */
void steam_job_workshop_touch(void);
/* the list ended: these ids (the view's id texts) downloaded, then the session ends (count 0: none) */
void steam_job_workshop_choose(const char (*ids)[24], int count);

/* (ints and bytes alone, as steam_job_view) */
struct steam_job_workshop_row
{
	char id[24];
	/* (steam_workshop_text: our font's characters, UTF-8) */
	char title[128];
	/* the author's public name ("" until it came, or none) */
	char author[48];
	unsigned int size_kb;
	int tags;
	/* 1: may be downloaded; 0: why in reason (another game's, banned, multiplayer maps only, too large) */
	int downloadable;
	char reason[64];
	/* Steam's score 0-100 (-1 none), its votes, when it was last updated (seconds since 1970) */
	int score;
	unsigned int votes, time_updated;
	/* its version (the manifest's gid, decimal: no 64-bit field crosses) for INSTALLED and UPDATE */
	char version[24];
	/* the card's picture: 0 coming, 1 ready (steam_job_workshop_picture), -1 none (no preview, or refused) */
	int picture;
};

struct steam_job_workshop_view
{
	int phase;
	/* the page shown: its number, list (steam_workshop_sort, or STEAM_WORKSHOP_LIST_SUBSCRIBED), search, the list's
	total; loading while a page is asked */
	int page, sort, total, count, loading, serial;
	char search[64];
	struct steam_job_workshop_row rows[10];
	/* the list's last error ("" none) */
	char error[160];
	/* the download: the item in hand (index of count) and its title, the items done and failed, the last failure */
	int item_index, item_count, items_done, items_failed;
	char item_title[128], failure[200];
};
void steam_job_workshop_view(struct steam_job_workshop_view *view);

/* ---------- the browser's details page and pictures (docs/steam_download_security.md section 9) */

/* a details page asked for (the id a row's; "" closes it): its details, its first picture, then what its files are */
void steam_job_workshop_details_ask(const char *id);
/* the details page's picture asked for (0 the preview, then the extra pictures) */
void steam_job_workshop_details_picture(int index);

struct steam_job_workshop_details
{
	/* 0 none asked, 1 coming, 2 here, -1 failed (error) */
	int state;
	unsigned int serial;
	char id[24];
	char title[128], author[48];
	/* (steam_workshop_text with its lines: '\n' between paragraphs) */
	char description[1600];
	unsigned int size_kb, time_updated, time_created, votes_up, votes_down;
	int score, tags, downloadable;
	char reason[64];
	char version[24];
	/* the pictures: how many, the one asked for, its state (0 coming, 1 here, -1 none) */
	int pictures, picture_index, picture;
	/* what its files are (the manifest by the name rule): 0 coming, 1 here, -1 not had (contents_why) */
	int contents;
	int maps, files, skipped, refused;
	unsigned int contents_kb;
	char map_names[12][48];
	char contents_why[160];
	char error[160];
};
void steam_job_workshop_details(struct steam_job_workshop_details *details);

/* a picture of the browser's, copied: slot 0-9 a card of the page shown (its row), -1 the details page's; its serial
(changes with each new picture; 0: none ready) and, with pixels (at least width * height * 4 bytes, capacity), its
B, G, R, A, its size */
unsigned int steam_job_workshop_picture(int slot, unsigned char *pixels, int capacity, int *width, int *height);
/* the memory the browser's pictures hold in the job now, and the decoder's most at once (bytes; for the tests) */
void steam_job_workshop_memory(unsigned int *pictures, unsigned int *decoder_peak);


/* the QR of a text (for the tests): its size (0 if it does not fit), modules 1 dark */
int steam_job_qr_encode(const char *text, uint8_t *modules, int maximum_size);

#endif
