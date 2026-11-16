/*
STEAM_WS.H

A WebSocket client (RFC 6455) for Steam's CM servers (wss://<cm>/cmsocket/):
the HTTP upgrade (its Sec-WebSocket-Accept checked), binary messages out in
masked frames, messages in from unmasked frames (fragments joined, ping
answered, close answered and ended), every length bounded
(STEAM_WS_PAYLOAD_MAXIMUM, STEAM_WS_CONTROL_MAXIMUM).

It runs over a steam_stream: the bytes of a TLS session (steam_net.c, over
posix_https.c's, with the built-in authorities), or in the PC tests of a
plain loopback connection.
*/

#ifndef STEAM_WS_H
#define STEAM_WS_H

#include <stddef.h>
#include <stdint.h>

/* a byte stream; read gives the bytes read, 0 if none came in timeout_ms,
-1 at its end or on an error */
struct steam_stream
{
	void *context;
	int (*write)(void *context, const void *data, size_t size); /* 1, or 0 */
	long (*read)(void *context, void *buffer, size_t capacity, int timeout_ms);
	void (*close)(void *context);
};

struct steam_ws_frame
{
	int fin;
	unsigned int opcode;
	int masked;
	uint8_t mask[4];
	uint64_t payload_size;
	size_t header_size;
};

enum
{
	STEAM_WS_CONTINUATION = 0,
	STEAM_WS_TEXT = 1,
	STEAM_WS_BINARY = 2,
	STEAM_WS_CLOSE = 8,
	STEAM_WS_PING = 9,
	STEAM_WS_PONG = 10,
};

/* a frame's header from a server: its size (and the frame's), 0 if more
bytes are needed, -1 if it is refused (masked, reserved bits, an unknown
opcode, a control frame fragmented or over 125 bytes, a payload over
STEAM_WS_PAYLOAD_MAXIMUM, a length not in its shortest form) */
long steam_ws_frame_parse(const uint8_t *data, size_t size, struct steam_ws_frame *frame);

/* the upgrade request for host and path with key (base64 of 16 random
bytes): its length, or -1 if it does not fit */
long steam_ws_request(const char *host, const char *path, const char *key, char *output, size_t capacity);
/* the server's response to it (all its headers, up to the blank line):
1 if it is a 101 with the right Upgrade, Connection and Accept, else 0 */
int steam_ws_response_ok(const char *response, size_t size, const char *key);

struct steam_ws
{
	struct steam_stream stream;
	uint8_t *input;
	size_t input_used, input_capacity;
	uint8_t *message;
	size_t message_size, message_capacity;
	int message_open;
	int closed;
	/* the server broke the protocol (not a close) */
	int refused;
};

/* the upgrade over stream (the steam_ws owns it from here): 1, or 0 with a
reason (the stream closed then) */
int steam_ws_open(struct steam_ws *ws, const struct steam_stream *stream, const char *host, const char *path,
	int timeout_ms, char *error, int error_size);
/* one binary message: 1, or 0 */
int steam_ws_send(struct steam_ws *ws, const void *data, size_t size);
/* the next message (valid until the next call): 1, 0 if none came in
timeout_ms, -1 when the connection ended or broke */
int steam_ws_receive(struct steam_ws *ws, const uint8_t **data, size_t *size, int timeout_ms);
/* the last message handed back wiped (it may have held a secret) */
void steam_ws_wipe_message(struct steam_ws *ws);
/* a close frame if still open, the stream closed, the buffers wiped and
freed */
void steam_ws_close(struct steam_ws *ws);

/* a whole server byte stream fed through the frame reader (the fuzzer's,
and the tests'): the count of messages, -1 at a refusal */
long steam_ws_feed_test(const uint8_t *data, size_t size);

#endif
