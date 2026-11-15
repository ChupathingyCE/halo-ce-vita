/*
STEAM_SESSION.H

One Steam session on a CM's WebSocket, for the download alone:
  connect      wss://<cm>/cmsocket/, then ClientHello
  QR sign-in   Authentication.BeginAuthSessionViaQR (not signed in), then
               PollAuthSessionStatus every interval until the player approves
               in the Steam app (the refresh and access tokens come back);
               never a password (docs/steam_download_security.md 3.1)
  logon        ClientLogon with the refresh token, should_remember_password
               false; heartbeats while signed in
  app info     PICS: ClientPICSAccessTokenRequest, then
               ClientPICSProductInfoRequest (the appinfo KeyValues: the
               depots' public manifests)
  depot key    ClientGetDepotDecryptionKey (refused for a depot the account
               does not own)
  manifest     ContentServerDirectory.GetManifestRequestCode
  CDN servers  ContentServerDirectory.GetServersForSteamPipe
  end          Authentication.RevokeToken (permanent) on our refresh token,
               ClientLogOff, the socket closed, every secret wiped
               (steam_session_end: on every path, twice is harmless)

Every secret (the tokens, the QR session's ids, the account name, the PICS
access token, the depot keys, the request codes the caller holds) lives in
struct steam_secrets, in RAM only: never logged, never written to a file
(docs/steam_download_security.md 3.2). The log hears what happened, with
sizes at most.
*/

#ifndef STEAM_SESSION_H
#define STEAM_SESSION_H

#include "steam_cm.h"
#include "steam_kv.h"
#include "steam_limits.h"
#include "steam_ws.h"

#include <stddef.h>
#include <stdint.h>

#define STEAM_PROTOCOL_VERSION 65580u
#define STEAM_DEVICE_NAME "Halo CE Vita (PS Vita)"
#define STEAM_ERESULT_OK 1

struct steam_secrets
{
	char refresh_token[STEAM_TOKEN_MAXIMUM + 1];
	char access_token[STEAM_TOKEN_MAXIMUM + 1];
	char account_name[STEAM_ACCOUNT_MAXIMUM + 1];
	uint64_t qr_client_id;
	uint8_t qr_request_id[STEAM_REQUEST_ID_MAXIMUM];
	size_t qr_request_id_size;
	uint64_t app_access_token;
	struct
	{
		uint32_t depot;
		uint8_t key[32];
	} depot_keys[STEAM_DEPOT_KEYS_MAXIMUM];
	int depot_key_count;
};

typedef void (*steam_log_proc)(void *context, const char *line);

struct steam_session
{
	struct steam_ws ws;
	int connected, logged_on;
	/* the server signed the session off */
	int kicked;
	uint64_t steamid;
	int32_t session_id;
	uint64_t next_job;
	uint32_t cell_id;
	int heartbeat_seconds;
	long long last_heartbeat_ms;
	struct steam_secrets *secrets;
	steam_log_proc log;
	void *log_context;
	/* the awaited reply, copied out of its WebSocket message (or Multi) */
	uint8_t *reply;
	size_t reply_size, reply_capacity;
	struct steam_cm_header reply_header;
	/* what the end did (the tests read these) */
	int revoked, logged_off, ended;
	/* the no-secret marker (steam-session.txt) written at sign-in and
	removed after a revoke; "" for none */
	char marker_path[512];
	/* the CM's URL (a connection lost with a token still live is opened again to revoke it) */
	char url[STEAM_HOST_MAXIMUM + 64];
};

/* a CDN server: its host name (a plain DNS name) */
struct steam_cdn_server
{
	char host[STEAM_HOST_MAXIMUM + 1];
	int https;
};

enum
{
	STEAM_QR_FAILED = -1,
	STEAM_QR_WAITING = 0,
	/* Steam rotated the QR: show the new challenge URL */
	STEAM_QR_NEW_URL = 1,
	STEAM_QR_APPROVED = 2,
};

/* 1, or 0 (out of memory) */
int steam_session_init(struct steam_session *session, steam_log_proc log, void *log_context, const char *marker_path);
/* the CM's WebSocket (wss://host[:port]/path; ws:// only to a loopback
address) and the ClientHello: 1, or 0 with a reason */
int steam_session_connect(struct steam_session *session, const char *url, char *error, int error_size);

/* a QR sign-in begun: the challenge URL to draw (at most url_capacity) and
the poll interval in seconds: 1, or 0 */
int steam_session_qr_begin(struct steam_session *session, char *url, size_t url_capacity, float *interval, char *error,
	int error_size);
/* one poll (the caller waits the interval between): STEAM_QR_* (a new
challenge URL into url) */
int steam_session_qr_poll(struct steam_session *session, char *url, size_t url_capacity, char *error, int error_size);

/* signed in with the approved session's refresh token: 1, or 0 */
int steam_session_logon(struct steam_session *session, char *error, int error_size);
/* waits for messages (heartbeats sent) for up to timeout_ms; 0 if the
session broke */
int steam_session_pump(struct steam_session *session, int timeout_ms);

/* the app's appinfo (PICS) as KeyValues: 1, or 0 */
int steam_session_app_info(struct steam_session *session, uint32_t app, struct steam_kv *kv, char *error, int error_size);
/* a depot's key (held in the secrets): the key, or NULL with a reason */
const uint8_t *steam_session_depot_key(struct steam_session *session, uint32_t app, uint32_t depot, char *error,
	int error_size);
/* a manifest's request code: 1, or 0 */
int steam_session_manifest_code(struct steam_session *session, uint32_t app, uint32_t depot, uint64_t gid,
	const char *branch, uint64_t *code, char *error, int error_size);
/* the CDN servers for the session's cell (plain DNS names, HTTPS ones
first): their count, or -1 */
int steam_session_cdn_servers(struct steam_session *session, struct steam_cdn_server *servers, int maximum, char *error,
	int error_size);

/* revoke, log off, close, wipe: on every path; harmless twice */
void steam_session_end(struct steam_session *session);

/* (parsers the session uses, public for the fuzzers and tests) */
/* a GetServersForSteamPipe response body: the count, -1 if malformed */
int steam_parse_cdn_servers(const uint8_t *body, size_t size, uint32_t app, struct steam_cdn_server *servers,
	int maximum);
/* a host name from Steam: letters, digits, '-', '.', at most 253 */
int steam_host_ok(const char *host, size_t length);
/* ISteamDirectory/GetCMListForConnect's JSON: the WebSocket CMs'
"host:port" endpoints into urls as wss://host:port/cmsocket/: the count */
int steam_parse_cm_list(const char *json, size_t size, char urls[][STEAM_HOST_MAXIMUM + 32], int maximum);

#endif
