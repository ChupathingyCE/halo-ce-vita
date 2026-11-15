/*
STEAM_HTTP.C

steam_http.h: GETs on a kept-alive connection.
*/

#include "steam_http.h"

#include "steam_crypto.h"
#include "steam_limits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* (64-bit: a 32-bit long overflows after 24.8 days of uptime - the PC's, or a Vita's since its boot) */
static long long now_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

static int line_is(const char *line, size_t length, const char *name, const char **value, size_t *value_size)
{
	size_t name_length = strlen(name), index;

	if (length <= name_length || line[name_length] != ':')
		return 0;
	for (index = 0; index < name_length; index++)
		if (lower(line[index]) != name[index])
			return 0;
	index = name_length + 1;
	while (index < length && (line[index] == ' ' || line[index] == '\t'))
		index++;
	while (length > index && (line[length - 1] == ' ' || line[length - 1] == '\t'))
		length--;
	*value = line + index;
	*value_size = length - index;
	return 1;
}

static int same_lower(const char *value, size_t size, const char *word)
{
	size_t index;

	if (strlen(word) != size)
		return 0;
	for (index = 0; index < size; index++)
		if (lower(value[index]) != word[index])
			return 0;
	return 1;
}

int steam_http_head_parse(const char *text, size_t size, struct steam_http_head *head)
{
	size_t cursor = 0;
	int line_number = 0;

	memset(head, 0, sizeof(*head));
	head->content_length = -1;
	head->keep_alive = 1;
	if (size > STEAM_HTTP_HEADER_MAXIMUM)
		return 0;
	while (cursor < size)
	{
		size_t start = cursor, end;
		const char *value;
		size_t value_size;

		while (cursor < size && text[cursor] != '\n')
			cursor++;
		if (cursor >= size)
			return 0;
		end = cursor++;
		if (end > start && text[end - 1] == '\r')
			end--;
		if (end - start > STEAM_HTTP_LINE_MAXIMUM)
			return 0;
		if (line_number++ == 0)
		{
			int status = 0, index;

			if (end - start < 12 || memcmp(text + start, "HTTP/1.", 7) || (text[start + 7] != '0' && text[start + 7] != '1') ||
				text[start + 8] != ' ')
			{
				return 0;
			}
			for (index = 9; index < 12; index++)
			{
				if (text[start + index] < '0' || text[start + index] > '9')
					return 0;
				status = status * 10 + (text[start + index] - '0');
			}
			head->status = status;
			if (text[start + 7] == '0')
				head->keep_alive = 0;
			continue;
		}
		if (end == start)
			return cursor == size;
		if (line_is(text + start, end - start, "content-length", &value, &value_size))
		{
			int64_t length = 0;
			size_t index;

			if (!value_size || value_size > 12 || head->content_length >= 0)
				return 0;
			for (index = 0; index < value_size; index++)
			{
				if (value[index] < '0' || value[index] > '9')
					return 0;
				length = length * 10 + (value[index] - '0');
			}
			head->content_length = length;
		}
		else if (line_is(text + start, end - start, "transfer-encoding", &value, &value_size))
		{
			if (!same_lower(value, value_size, "chunked"))
				return 0;
			head->chunked = 1;
		}
		else if (line_is(text + start, end - start, "connection", &value, &value_size))
		{
			if (same_lower(value, value_size, "close"))
				head->keep_alive = 0;
		}
	}
	return 0;
}

/* ---------- reading */

static int fill(struct steam_http *http, long long deadline)
{
	long long remaining = deadline - now_ms();
	long got;

	if (remaining <= 0)
		return 0;
	if (http->input_capacity - http->input_used < 16384)
	{
		size_t capacity = http->input_capacity ? http->input_capacity * 2 : 65536;
		uint8_t *grown;

		/* (the input never holds more than a header, a chunk line or a read's worth) */
		if (capacity > STEAM_HTTP_HEADER_MAXIMUM * 16)
			return 0;
		grown = realloc(http->input, capacity);
		if (!grown)
			return 0;
		http->input = grown;
		http->input_capacity = capacity;
	}
	got = http->stream.read(http->stream.context, http->input + http->input_used,
		http->input_capacity - http->input_used, (int)remaining);
	if (got <= 0)
		return 0;
	http->input_used += (size_t)got;
	return 1;
}

static void consume(struct steam_http *http, size_t count)
{
	memmove(http->input, http->input + count, http->input_used - count);
	http->input_used -= count;
}

/* count bytes of body into output, from the input then the stream */
static int read_exact(struct steam_http *http, uint8_t *output, size_t count, long long deadline)
{
	while (count)
	{
		size_t part;

		if (!http->input_used && !fill(http, deadline))
			return 0;
		part = http->input_used < count ? http->input_used : count;
		memcpy(output, http->input, part);
		consume(http, part);
		output += part;
		count -= part;
	}
	return 1;
}

/* a line (without its CRLF) of at most 256 bytes */
static int read_line(struct steam_http *http, char *line, size_t capacity, long long deadline)
{
	for (;;)
	{
		uint8_t *end = http->input_used ? memchr(http->input, '\n', http->input_used) : NULL;

		if (end)
		{
			size_t length = (size_t)(end - http->input);

			if (length && http->input[length - 1] == '\r')
				length--;
			if (length >= capacity)
				return 0;
			memcpy(line, http->input, length);
			line[length] = 0;
			consume(http, (size_t)(end - http->input) + 1);
			return 1;
		}
		if (http->input_used > capacity + 2 || !fill(http, deadline))
			return 0;
	}
}

uint8_t *steam_http_get(struct steam_http *http, const char *path, size_t maximum, size_t *size, int *status,
	int timeout_ms, char *error, int error_size)
{
	struct steam_http_head head;
	char request[1024];
	uint8_t *body = NULL;
	size_t body_size = 0, head_size = 0;
	long long deadline = now_ms() + timeout_ms;
	int length;

	*size = 0;
	*status = 0;
	error[0] = 0;
	if (!http->open)
	{
		snprintf(error, (size_t)error_size, "connection closed");
		return NULL;
	}
	length = snprintf(request, sizeof(request),
		"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Valve/Steam HTTP Client 1.0\r\nAccept: */*\r\n"
		"Accept-Encoding: identity\r\nConnection: keep-alive\r\n\r\n",
		path, http->host);
	if (length <= 0 || (size_t)length >= sizeof(request) || strpbrk(path, "\r\n ") ||
		!http->stream.write(http->stream.context, request, (size_t)length))
	{
		steam_wipe(request, sizeof(request));
		snprintf(error, (size_t)error_size, "cannot send the request");
		goto broken;
	}
	/* (a manifest's path holds its request code) */
	steam_wipe(request, sizeof(request));
	/* the headers */
	for (;;)
	{
		size_t index;

		for (index = head_size >= 3 ? head_size - 3 : 0; index + 4 <= http->input_used; index++)
			if (!memcmp(http->input + index, "\r\n\r\n", 4))
				break;
		if (index + 4 <= http->input_used)
		{
			head_size = index + 4;
			break;
		}
		head_size = http->input_used;
		if (http->input_used > STEAM_HTTP_HEADER_MAXIMUM || !fill(http, deadline))
		{
			snprintf(error, (size_t)error_size, "no response headers");
			goto broken;
		}
	}
	if (!steam_http_head_parse((const char *)http->input, head_size, &head))
	{
		snprintf(error, (size_t)error_size, "malformed response headers");
		goto broken;
	}
	consume(http, head_size);
	*status = head.status;
	if (head.chunked)
	{
		for (;;)
		{
			char line[260];
			uint64_t chunk = 0;
			size_t index;

			if (!read_line(http, line, sizeof(line), deadline))
				goto bad_body;
			for (index = 0; line[index] && line[index] != ';'; index++)
			{
				int c = lower(line[index]), digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;

				if (digit < 0 || index >= 8)
					goto bad_body;
				chunk = chunk << 4 | (uint64_t)digit;
			}
			if (!index)
				goto bad_body;
			if (!chunk)
			{
				/* (trailers, then the blank line) */
				do
				{
					if (!read_line(http, line, sizeof(line), deadline))
						goto bad_body;
				} while (line[0]);
				break;
			}
			if (chunk > maximum - body_size)
				goto too_large;
			{
				uint8_t *grown = realloc(body, body_size + (size_t)chunk + 1);

				if (!grown)
					goto bad_body;
				body = grown;
			}
			if (!read_exact(http, body + body_size, (size_t)chunk, deadline) || !read_line(http, line, sizeof(line), deadline) ||
				line[0])
			{
				goto bad_body;
			}
			body_size += (size_t)chunk;
		}
	}
	else
	{
		if (head.content_length < 0)
		{
			/* (a body to the connection's end is not taken) */
			snprintf(error, (size_t)error_size, "response without Content-Length");
			goto broken;
		}
		if ((uint64_t)head.content_length > maximum)
			goto too_large;
		body = malloc((size_t)head.content_length + 1);
		if (!body || !read_exact(http, body, (size_t)head.content_length, deadline))
			goto bad_body;
		body_size = (size_t)head.content_length;
	}
	if (!head.keep_alive)
		steam_http_close(http);
	if (head.status != 200)
	{
		snprintf(error, (size_t)error_size, "HTTP status %d", head.status);
		free(body);
		return NULL;
	}
	if (!body)
		body = malloc(1);
	*size = body_size;
	return body;
too_large:
	snprintf(error, (size_t)error_size, "response too large");
	goto broken_body;
bad_body:
	snprintf(error, (size_t)error_size, "response body cut short or malformed");
broken_body:
	free(body);
broken:
	steam_http_close(http);
	return NULL;
}

void steam_http_close(struct steam_http *http)
{
	if (http->open && http->stream.close)
		http->stream.close(http->stream.context);
	http->open = 0;
	http->stream.close = NULL;
	free(http->input);
	http->input = NULL;
	http->input_used = http->input_capacity = 0;
}

/* ---------- the fuzzer's feed */

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
	if (part > capacity)
		part = capacity;
	if (part > 5 + feed->cursor % 3001)
		part = 5 + feed->cursor % 3001;
	memcpy(buffer, feed->data + feed->cursor, part);
	feed->cursor += part;
	return (long)part;
}

static void feed_close(void *context)
{
	(void)context;
}

long steam_http_feed_test(const uint8_t *data, size_t size)
{
	struct feed feed = { data, size, 0 };
	struct steam_http http;
	uint8_t *body;
	size_t body_size = 0;
	char error[128];
	int status;
	long result;

	memset(&http, 0, sizeof(http));
	http.stream.context = &feed;
	http.stream.write = feed_write;
	http.stream.read = feed_read;
	http.stream.close = feed_close;
	http.open = 1;
	snprintf(http.host, sizeof(http.host), "cdn.test");
	body = steam_http_get(&http, "/depot/1/chunk/00", 1u << 20, &body_size, &status, 1000, error, sizeof(error));
	result = body ? (long)body_size : -1;
	free(body);
	steam_http_close(&http);
	return result;
}
