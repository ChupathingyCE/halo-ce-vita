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
};

/* runs it all; result says how it went. Never leaves a session open */
void steam_download_run(const struct steam_download_options *options, const struct steam_download_callbacks *callbacks,
	struct steam_download_result *result);

/* the part for one depot, with a session already signed in (exposed for
the tests): 1, or 0 with result->error set */
int steam_download_depot(struct steam_session *session, const struct steam_kv *app_info, uint32_t depot,
	const struct steam_download_options *options, const struct steam_download_callbacks *callbacks,
	const char *const *cdn, int cdn_count, struct steam_download_result *result);

#endif
