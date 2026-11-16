/*
STEAM_WS.C

steam_ws.h: the WebSocket client.
*/

#include "steam_ws.h"

#include "steam_crypto.h"
#include "steam_limits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

long steam_ws_frame_parse(const uint8_t *data, size_t size, struct steam_ws_frame *frame)
{
	size_t header = 2;
	uint64_t length;

	memset(frame, 0, sizeof(*frame));
	if (size < 2)
		return 0;
	frame->fin = data[0] >> 7;
	frame->opcode = data[0] & 15;
	frame->masked = data[1] >> 7;
	/* (no extension was asked for: no reserved bits; a server never masks) */
	if (data[0] & 0x70 || frame->masked)
		return -1;
	if (frame->opcode > STEAM_WS_BINARY && frame->opcode != STEAM_WS_CLOSE && frame->opcode != STEAM_WS_PING &&
		frame->opcode != STEAM_WS_PONG)
	{
		return -1;
	}
	length = data[1] & 127;
	if (length == 126)
	{
		if (size < 4)
			return 0;
		length = (uint64_t)data[2] << 8 | data[3];
		if (length < 126)
			return -1;
		header = 4;
	}
	else if (length == 127)
	{
		unsigned int index;

		if (size < 10)
			return 0;
		length = 0;
		for (index = 0; index < 8; index++)
			length = length << 8 | data[2 + index];
		if (length <= 0xffff || length >> 63)
			return -1;
		header = 10;
	}
	if (frame->opcode >= STEAM_WS_CLOSE && (!frame->fin || length > STEAM_WS_CONTROL_MAXIMUM))
		return -1;
	if (length > STEAM_WS_PAYLOAD_MAXIMUM)
		return -1;
	frame->payload_size = length;
	frame->header_size = header;
	return (long)header;
}

long steam_ws_request(const char *host, const char *path, const char *key, char *output, size_t capacity)
{
	int length = snprintf(output, capacity,
		"GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
		"Sec-WebSocket-Version: 13\r\nUser-Agent: Halo CE Vita\r\n\r\n",
		path, host, key);

	return length > 0 && (size_t)length < capacity ? length : -1;
}

/* a header's value (its name any case), trimmed; length in *value_size */
static const char *header_value(const char *response, size_t size, const char *name, size_t *value_size)
{
	size_t cursor = 0, name_length = strlen(name);

	/* (past the status line) */
	while (cursor < size && response[cursor] != '\n')
		cursor++;
	while (cursor < size)
	{
		size_t line = ++cursor, end, index;

		while (cursor < size && response[cursor] != '\n')
			cursor++;
		end = cursor;
		if (end > line && response[end - 1] == '\r')
			end--;
		if (end - line <= name_length || response[line + name_length] != ':')
			continue;
		for (index = 0; index < name_length; index++)
		{
			char a = response[line + index], b = name[index];

			if (a >= 'A' && a <= 'Z')
				a = (char)(a - 'A' + 'a');
			if (b >= 'A' && b <= 'Z')
				b = (char)(b - 'A' + 'a');
			if (a != b)
				break;
		}
		if (index != name_length)
			continue;
		line += name_length + 1;
		while (line < end && (response[line] == ' ' || response[line] == '\t'))
			line++;
		while (end > line && (response[end - 1] == ' ' || response[end - 1] == '\t'))
			end--;
		*value_size = end - line;
		return response + line;
	}
	return NULL;
}

static int value_has_token(const char *value, size_t size, const char *token)
{
	size_t token_length = strlen(token), index;

	for (index = 0; index + token_length <= size; index++)
	{
		size_t j;

		for (j = 0; j < token_length; j++)
		{
			char a = value[index + j];

			if (a >= 'A' && a <= 'Z')
				a = (char)(a - 'A' + 'a');
			if (a != token[j])
				break;
		}
		if (j == token_length && (index == 0 || value[index - 1] == ',' || value[index - 1] == ' ') &&
			(index + token_length == size || value[index + token_length] == ',' || value[index + token_length] == ' '))
		{
			return 1;
		}
	}
	return 0;
}

int steam_ws_response_ok(const char *response, size_t size, const char *key)
{
	char joined[128], expected[64];
	uint8_t digest[20];
	const char *value;
	size_t value_size;
	int length;

	if (size < 12 || memcmp(response, "HTTP/1.1 101", 12))
		return 0;
	value = header_value(response, size, "upgrade", &value_size);
	if (!value || !value_has_token(value, value_size, "websocket"))
		return 0;
	value = header_value(response, size, "connection", &value_size);
	if (!value || !value_has_token(value, value_size, "upgrade"))
		return 0;
	length = snprintf(joined, sizeof(joined), "%s" WS_GUID, key);
	if (length <= 0 || (size_t)length >= sizeof(joined))
		return 0;
	steam_sha1(joined, (size_t)length, digest);
	if (steam_base64_encode(digest, 20, expected, sizeof(expected)) != 28)
		return 0;
	value = header_value(response, size, "sec-websocket-accept", &value_size);
	return value && value_size == 28 && !memcmp(value, expected, 28);
}

/* ---------- the client */

/* (64-bit: a 32-bit long overflows after 24.8 days of uptime - the PC's, or a Vita's since its boot) */
static long long now_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int send_frame(struct steam_ws *ws, unsigned int opcode, const void *data, size_t size)
{
	uint8_t header[14], mask[4], *frame;
	size_t header_size = 2, index;
	int ok;

	if (ws->closed || size > STEAM_WS_PAYLOAD_MAXIMUM)
		return 0;
	header[0] = (uint8_t)(0x80 | opcode);
	if (size < 126)
		header[1] = (uint8_t)(0x80 | size);
	else if (size <= 0xffff)
	{
		header[1] = 0x80 | 126;
		header[2] = (uint8_t)(size >> 8);
		header[3] = (uint8_t)size;
		header_size = 4;
	}
	else
	{
		header[1] = 0x80 | 127;
		for (index = 0; index < 8; index++)
			header[2 + index] = (uint8_t)((uint64_t)size >> (56 - 8 * index));
		header_size = 10;
	}
	steam_random(mask, 4);
	memcpy(header + header_size, mask, 4);
	header_size += 4;
	frame = malloc(header_size + size);
	if (!frame)
		return 0;
	memcpy(frame, header, header_size);
	for (index = 0; index < size; index++)
		frame[header_size + index] = ((const uint8_t *)data)[index] ^ mask[index & 3];
	ok = ws->stream.write(ws->stream.context, frame, header_size + size);
	/* (the frame held a message, masked but not secret-free: wiped) */
	steam_wipe(frame, header_size + size);
	free(frame);
	return ok;
}

int steam_ws_open(struct steam_ws *ws, const struct steam_stream *stream, const char *host, const char *path,
	int timeout_ms, char *error, int error_size)
{
	uint8_t nonce[16];
	char key[32], request[1024], response[STEAM_WS_HANDSHAKE_MAXIMUM + 1];
	size_t used = 0;
	long length;
	long long deadline = now_ms() + timeout_ms;

	memset(ws, 0, sizeof(*ws));
	ws->stream = *stream;
	error[0] = 0;
	steam_random(nonce, sizeof(nonce));
	if (steam_base64_encode(nonce, sizeof(nonce), key, sizeof(key)) != 24 ||
		(length = steam_ws_request(host, path, key, request, sizeof(request))) < 0 ||
		!ws->stream.write(ws->stream.context, request, (size_t)length))
	{
		snprintf(error, (size_t)error_size, "cannot send the WebSocket upgrade");
		goto failed;
	}
	/* the response's headers, read a byte at a time so nothing after
	them is taken (a frame may follow at once) */
	for (;;)
	{
		long long remaining = deadline - now_ms();
		long got;

		if (remaining <= 0)
		{
			snprintf(error, (size_t)error_size, "no WebSocket upgrade response in time");
			goto failed;
		}
		if (used >= STEAM_WS_HANDSHAKE_MAXIMUM)
		{
			snprintf(error, (size_t)error_size, "WebSocket upgrade response too long");
			goto failed;
		}
		got = ws->stream.read(ws->stream.context, response + used, 1, (int)remaining);
		if (got < 0)
		{
			snprintf(error, (size_t)error_size, "the server closed during the WebSocket upgrade");
			goto failed;
		}
		used += (size_t)got;
		if (used >= 4 && !memcmp(response + used - 4, "\r\n\r\n", 4))
			break;
	}
	response[used] = 0;
	if (!steam_ws_response_ok(response, used, key))
	{
		snprintf(error, (size_t)error_size, "the server refused the WebSocket upgrade");
		goto failed;
	}
	return 1;
failed:
	ws->closed = 1;
	ws->stream.close(ws->stream.context);
	ws->stream.close = NULL;
	return 0;
}

int steam_ws_send(struct steam_ws *ws, const void *data, size_t size)
{
	return send_frame(ws, STEAM_WS_BINARY, data, size);
}

static int ensure_capacity(uint8_t **buffer, size_t *capacity, size_t needed)
{
	uint8_t *grown;
	size_t size = *capacity ? *capacity : 4096;

	if (needed <= *capacity)
		return 1;
	while (size < needed)
		size *= 2;
	grown = malloc(size);
	if (!grown)
		return 0;
	if (*buffer)
	{
		memcpy(grown, *buffer, *capacity);
		steam_wipe(*buffer, *capacity);
		free(*buffer);
	}
	*buffer = grown;
	*capacity = size;
	return 1;
}

/* takes one whole frame from the input, if there: 1 a message is ready,
2 a frame was taken (no message yet), 0 more bytes are needed, -1 refused
or closed */
static int take_frame(struct steam_ws *ws)
{
	struct steam_ws_frame frame;
	long header = steam_ws_frame_parse(ws->input, ws->input_used, &frame);
	size_t total;
	const uint8_t *payload;
	int result = 0;

	if (header < 0)
		return -1;
	if (header == 0)
		return 0;
	total = (size_t)header + (size_t)frame.payload_size;
	if (ws->input_used < total)
		return ensure_capacity(&ws->input, &ws->input_capacity, total) ? 0 : -1;
	payload = ws->input + header;
	switch (frame.opcode)
	{
	case STEAM_WS_PING:
		if (!send_frame(ws, STEAM_WS_PONG, payload, (size_t)frame.payload_size))
			return -1;
		break;
	case STEAM_WS_PONG:
		break;
	case STEAM_WS_CLOSE:
		send_frame(ws, STEAM_WS_CLOSE, payload, frame.payload_size >= 2 ? 2 : 0);
		ws->closed = 1;
		return -1;
	case STEAM_WS_TEXT:
		/* (Steam's CMs send binary only) */
		return -1;
	case STEAM_WS_BINARY:
	case STEAM_WS_CONTINUATION:
		if ((frame.opcode == STEAM_WS_BINARY) == ws->message_open)
			return -1;
		if (frame.opcode == STEAM_WS_BINARY)
			ws->message_size = 0;
		if (frame.payload_size > STEAM_WS_PAYLOAD_MAXIMUM - ws->message_size ||
			!ensure_capacity(&ws->message, &ws->message_capacity, ws->message_size + (size_t)frame.payload_size + 1))
		{
			return -1;
		}
		memcpy(ws->message + ws->message_size, payload, (size_t)frame.payload_size);
		ws->message_size += (size_t)frame.payload_size;
		ws->message_open = !frame.fin;
		result = frame.fin ? 1 : 2;
		break;
	}
	if (!result)
		result = 2;
	memmove(ws->input, ws->input + total, ws->input_used - total);
	ws->input_used -= total;
	/* (the bytes moved down from: a message may have held a token) */
	steam_wipe(ws->input + ws->input_used, total);
	return result;
}

int steam_ws_receive(struct steam_ws *ws, const uint8_t **data, size_t *size, int timeout_ms)
{
	long long deadline = now_ms() + timeout_ms;

	*data = NULL;
	*size = 0;
	if (ws->closed)
		return -1;
	for (;;)
	{
		long long remaining;
		long got;
		int taken = take_frame(ws);

		if (taken < 0)
		{
			ws->refused = !ws->closed;
			ws->closed = 1;
			return -1;
		}
		if (taken == 2)
			continue;
		if (taken == 1)
		{
			*data = ws->message;
			*size = ws->message_size;
			return 1;
		}
		if (!ensure_capacity(&ws->input, &ws->input_capacity, ws->input_used + 16384))
			return -1;
		remaining = deadline - now_ms();
		if (remaining < 0)
			return 0;
		got = ws->stream.read(ws->stream.context, ws->input + ws->input_used, ws->input_capacity - ws->input_used,
			(int)remaining);
		if (got < 0)
		{
			ws->closed = 1;
			return -1;
		}
		ws->input_used += (size_t)got;
		if (!got && now_ms() >= deadline)
			return 0;
	}
}

void steam_ws_wipe_message(struct steam_ws *ws)
{
	if (ws->message)
		steam_wipe(ws->message, ws->message_size);
}

void steam_ws_close(struct steam_ws *ws)
{
	if (!ws->closed && ws->stream.write)
	{
		static const uint8_t normal[2] = { 0x03, 0xe8 };

		send_frame(ws, STEAM_WS_CLOSE, normal, 2);
	}
	ws->closed = 1;
	if (ws->stream.close)
		ws->stream.close(ws->stream.context);
	ws->stream.close = NULL;
	if (ws->input)
		steam_wipe(ws->input, ws->input_capacity);
	if (ws->message)
		steam_wipe(ws->message, ws->message_capacity);
	free(ws->input);
	free(ws->message);
	ws->input = ws->message = NULL;
	ws->input_capacity = ws->message_capacity = ws->input_used = ws->message_size = 0;
}

/* ---------- the fuzzer's and tests' feed */

struct feed
{
	const uint8_t *data;
	size_t size, cursor;
};

static int feed_write(void *context, const void *data, size_t size)
{
	(void)context;
	(void)data;
	(void)size;
	return 1;
}

static long feed_read(void *context, void *buffer, size_t capacity, int timeout_ms)
{
	struct feed *feed = context;
	size_t part = feed->size - feed->cursor;

	(void)timeout_ms;
	if (!part)
		return -1;
	/* (in odd pieces, as a network gives them) */
	if (part > capacity)
		part = capacity;
	if (part > 7 + feed->cursor % 4093)
		part = 7 + feed->cursor % 4093;
	memcpy(buffer, feed->data + feed->cursor, part);
	feed->cursor += part;
	return (long)part;
}

static void feed_close(void *context)
{
	(void)context;
}

long steam_ws_feed_test(const uint8_t *data, size_t size)
{
	struct feed feed = { data, size, 0 };
	struct steam_ws ws;
	long messages = 0;

	memset(&ws, 0, sizeof(ws));
	ws.stream.context = &feed;
	ws.stream.write = feed_write;
	ws.stream.read = feed_read;
	ws.stream.close = feed_close;
	for (;;)
	{
		const uint8_t *message;
		size_t message_size;
		int result = steam_ws_receive(&ws, &message, &message_size, 1000);

		if (result <= 0)
			break;
		messages++;
	}
	{
		int refused = ws.refused;

		ws.closed = 1;
		steam_ws_close(&ws);
		return refused ? -1 : messages;
	}
}
