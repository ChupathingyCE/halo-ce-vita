/*
STEAM_LIMITS.H

The caps on everything a Steam server (a CM over its WebSocket, a CDN over
HTTPS) sends the Steam download: docs/steam_download_security.md, 3.4.
Every count, length and offset read from the network is checked against the
bytes there and against these before it is used.
*/

#ifndef STEAM_LIMITS_H
#define STEAM_LIMITS_H

/* a WebSocket frame's payload, and a fragmented message's whole */
#define STEAM_WS_PAYLOAD_MAXIMUM (4u << 20)
/* a control frame's (RFC 6455 5.5) */
#define STEAM_WS_CONTROL_MAXIMUM 125u
/* the server's upgrade response's headers */
#define STEAM_WS_HANDSHAKE_MAXIMUM 8192u

/* one CM message, and a Multi's body once unzipped (nested Multis are
refused) */
#define STEAM_CM_MESSAGE_MAXIMUM (4u << 20)
#define STEAM_CM_MULTI_UNZIPPED_MAXIMUM (8u << 20)
#define STEAM_CM_MULTI_MESSAGES_MAXIMUM 4096u
/* a protobuf header's string (target_job_name, error_message) */
#define STEAM_CM_STRING_MAXIMUM 256u

/* protobuf: the nesting the callers read, one level at a time */
#define STEAM_PB_DEPTH_MAXIMUM 8

/* KeyValues text (PICS appinfo) */
#define STEAM_KV_TEXT_MAXIMUM (1u << 20)
#define STEAM_KV_DEPTH_MAXIMUM 32
#define STEAM_KV_TOKEN_MAXIMUM 4096u
#define STEAM_KV_NODES_MAXIMUM 65536u

/* a manifest, zipped and not, and what it lists */
#define STEAM_MANIFEST_ZIPPED_MAXIMUM (64u << 20)
#define STEAM_MANIFEST_MAXIMUM (64u << 20)
#define STEAM_MANIFEST_FILES_MAXIMUM 100000u
#define STEAM_MANIFEST_CHUNKS_MAXIMUM 1000000u
#define STEAM_FILE_CHUNKS_MAXIMUM 65536u
#define STEAM_NAME_MAXIMUM 1024u
/* an encrypted name's base64 (its AES blocks, the IV's among them) */
#define STEAM_NAME_ENCRYPTED_MAXIMUM 2800u

/* one chunk, compressed (as the CDN sends it) and decompressed */
#define STEAM_CHUNK_COMPRESSED_MAXIMUM (16u << 20)
#define STEAM_CHUNK_MAXIMUM (16u << 20)
/* a file (the Vita's stdio offsets are signed 32-bit) */
#define STEAM_FILE_MAXIMUM 0x7fffffffull

/* an HTTP response's headers (the CDN's) */
#define STEAM_HTTP_HEADER_MAXIMUM 16384u
#define STEAM_HTTP_LINE_MAXIMUM 4096u

/* a host name from Steam's directories */
#define STEAM_HOST_MAXIMUM 253u

/* a token (a JWT), an account name, a challenge URL */
#define STEAM_TOKEN_MAXIMUM 2048u
#define STEAM_ACCOUNT_MAXIMUM 64u
#define STEAM_URL_MAXIMUM 512u
#define STEAM_REQUEST_ID_MAXIMUM 64u

/* the depot keys one session holds */
#define STEAM_DEPOT_KEYS_MAXIMUM 8

/* a chunk that fails its checks is fetched again, at most this often */
#define STEAM_CHUNK_TRIES 3

#endif
