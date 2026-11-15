/*
STEAM_HTTP.H

GETs from a Steam CDN over a kept-alive steam_stream: a manifest's zip and
the chunks, one request after another on the same TLS connection. The
response's headers are bounded (STEAM_HTTP_HEADER_MAXIMUM, lines of
STEAM_HTTP_LINE_MAXIMUM); the body comes by Content-Length or chunked, at
most the caller's maximum; only a 200 is taken. No redirect is followed.
*/

#ifndef STEAM_HTTP_H
#define STEAM_HTTP_H

#include "steam_ws.h"

#include <stddef.h>
#include <stdint.h>

struct steam_http_head
{
	int status;
	int chunked;
	int keep_alive;
	int64_t content_length; /* -1: not given */
};

/* a response's headers (the bytes up to and with the blank line): 1, or 0
if refused */
int steam_http_head_parse(const char *text, size_t size, struct steam_http_head *head);

struct steam_http
{
	struct steam_stream stream;
	int open;
	char host[256];
	/* bytes read past the last response */
	uint8_t *input;
	size_t input_used, input_capacity;
};

/* GET path from host over http's stream (kept open after, if the server
lets it): the body (malloc'd, the caller frees it) and its size, or NULL
with a reason; *status gets the HTTP status (0 if none came) */
uint8_t *steam_http_get(struct steam_http *http, const char *path, size_t maximum, size_t *size, int *status,
	int timeout_ms, char *error, int error_size);
void steam_http_close(struct steam_http *http);

/* a whole server byte stream fed through the response reader (the
fuzzer's): the body's size, or -1 */
long steam_http_feed_test(const uint8_t *data, size_t size);

#endif
