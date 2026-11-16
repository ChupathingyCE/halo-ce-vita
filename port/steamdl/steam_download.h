/*
STEAM_DOWNLOAD.H

The whole download, start to end: connect to a CM, the QR sign-in, logon,
the app's info, then for each depot the choice needs (steam_plan.h) its key,
its manifest (request code, then the CDN), the planned files in it, their
chunks fetched, checked and written (steam_chunk.h, steam_file.h); then the
session ends - revoke, log off, close, wipe - on every path, a success, a
cancel or any failure (docs/steam_download_security.md).

The caller (the Vita's screen; the PC tests) is told what to show through
callbacks: the QR's URL to draw, the progress, the log's lines (none holds a
secret), and may cancel at any callback.
*/

#ifndef STEAM_DOWNLOAD_H
#define STEAM_DOWNLOAD_H

#include "steam_session.h"
#include "steam_workshop.h"

#include <stdint.h>

struct steam_download_callbacks
{
	void *context;
	/* a QR to draw (a new one when Steam rotates it; the URL is a secret of the session's: never logged): nonzero
	cancels */
	int (*show_qr)(void *context, const char *url);
	/* the QR is still waiting: nonzero cancels (called each poll) */
	int (*waiting)(void *context);
	/* a depot begun (index of count, in the order steam_plan_depots gives): nonzero cancels; NULL: none */
	int (*depot_start)(void *context, uint32_t depot, int index, int count);
	/* bytes done of the total, and the file in hand: nonzero cancels */
	int (*progress)(void *context, uint64_t done, uint64_t total, const char *file);
	steam_log_proc log;
	/* the free bytes where root is, or -1 if unknown (NULL: not checked) */
	int64_t (*free_space)(void *context, const char *root);
	/* waits ms (the QR's poll interval; NULL: the session's own wait) */
	void (*sleep_ms)(void *context, int ms);
	/* nonzero while the download must pause (the Vita's battery low and not charging); NULL: never. progress is
	still called meanwhile (file NULL), so the player can cancel */
	int (*device_gate)(void *context);
	/* (the Workshop, options->workshop) the list and the player's choice, with the session signed in: the callback
	pages through steam_download_workshop_query as the player asks (and keeps the session alive meanwhile:
	steam_session_pump) and gives the chosen items' ids: their count (0: none, the session ends), -1 cancels */
	int (*workshop_choose)(void *context, struct steam_session *session, uint64_t *ids, int maximum);
	/* (the Workshop) an item begun (index of count): nonzero cancels; NULL: none */
	int (*item_start)(void *context, const struct steam_workshop_item *item, int index, int count);
	/* (the Workshop) an item ended: ok, or the player's reason; NULL: none */
	void (*item_end)(void *context, const struct steam_workshop_item *item, int ok, const char *why);
};

struct steam_download_options
{
	uint32_t games; /* STEAM_GAME_* */
	const char *cm_url; /* wss://<cm>/cmsocket/ (the tests' ws://127.0.0.1:port/...) */
	/* the CDN's base URLs to use instead of asking Steam (the tests' only:
	NULL in the game) */
	const char *const *cdn_override;
	int cdn_override_count;
	const char *ce_root; /* ux0:data/haloce-vita (Halo CE's files) */
	const char *app_root; /* the app's own folder (1.1.x: the same; the marker, the free-space check) */
	const char *marker_path; /* the no-secret marker, or NULL */
	/* the Steam Workshop instead of the game's depots: the list (callbacks->workshop_choose), then each chosen item
	into <ce_root>/workshop/<id> (steam_workshop.h; the install into the mods folder is the game's, after) */
	int workshop;
};

struct steam_download_result
{
	int ok;
	int cancelled;
	char error[256];
	unsigned int files_done, files_already;
	uint64_t bytes_downloaded, bytes_written;
	unsigned int chunks_retried;
	/* the session's end (for the tests): the token revoked, logged off */
	int revoked, logged_off;
	/* the CM session ended under the download (the Wi-Fi lost too long, the server's log-off): a new QR resumes it */
	int session_lost;
	/* rounds of waits for the network (the Wi-Fi lost for a while) */
	unsigned int network_waits;
	/* the decoders' time and output, by container (0 VZa LZMA, 1 VSZa zstd, 2 zip), the largest chunk buffer */
	uint64_t decode_us[3], decoded_bytes[3];
	unsigned int decoded_chunks[3];
	size_t peak_chunk_buffer;
	/* the CM connection's time (DNS, TCP, the TLS handshake and the WebSocket upgrade), ms */
	unsigned int connect_ms;
	/* (the Workshop) items downloaded whole (waiting for their install), items refused or failed */
	unsigned int items_done, items_failed;
};

/* runs it all; result says how it went. Never leaves a session open */
void steam_download_run(const struct steam_download_options *options, const struct steam_download_callbacks *callbacks,
	struct steam_download_result *result);

/* the part for one depot, with a session already signed in (exposed for
the tests): 1, or 0 with result->error set */
int steam_download_depot(struct steam_session *session, const struct steam_kv *app_info, uint32_t depot,
	const struct steam_download_options *options, const struct steam_download_callbacks *callbacks,
	const char *const *cdn, int cdn_count, struct steam_download_result *result);

/* (the Workshop) one page of the list, from inside workshop_choose: the items read (at most maximum), -1 with a
reason; sort STEAM_WORKSHOP_LIST_SUBSCRIBED: the signed-in account's own subscriptions (search not used) */
int steam_download_workshop_query(struct steam_session *session, int page, enum steam_workshop_sort sort,
	const char *search, struct steam_workshop_item *items, int maximum, uint32_t *total, char *error, int error_size);

/* (the browser's details page) one item's details (GetDetails with its votes, pictures and description): 1, or 0
with a reason */
int steam_download_workshop_page(struct steam_session *session, uint64_t id, struct steam_workshop_details *details,
	char *error, int error_size);
/* (the browser) these accounts' public names ("" where none came): the names found, -1 if the call failed */
int steam_download_workshop_names(struct steam_session *session, const uint64_t *steamids, int count,
	char (*names)[STEAM_WORKSHOP_AUTHOR_SIZE]);

/* (the browser's details page) the item's manifest read (the depot key, its request code, a CDN - cdn_override's,
else Steam's list - one try each, no waits) and its files put through the name rule: 1, or 0 with a reason. Nothing
is written */
int steam_download_workshop_contents(struct steam_session *session, const struct steam_workshop_item *item,
	const char *const *cdn_override, int cdn_override_count, struct steam_workshop_contents *contents, char *error,
	int error_size);

/* (the Workshop) one item, its details checked already, into <ce_root>/workshop/<id> with its .done mark: 1, or 0
with result->error set (exposed for the tests) */
int steam_download_workshop_item(struct steam_session *session, const struct steam_workshop_item *item,
	const struct steam_download_options *options, const struct steam_download_callbacks *callbacks,
	const char *const *cdn, int cdn_count, struct steam_download_result *result);

#endif
