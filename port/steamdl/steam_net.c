/*
STEAM_NET.C

steam_net.h, over posix_https.c's stream and posix_random_bytes.
*/

#include "steam_net.h"

#include "steam_crypto.h"

#include "posix.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* the open streams (the CM's WebSocket, the CDN's connections, a picture's), so a cancel can end their waits from
another thread (steam_net_abort); and the flag every stream's wait looks at (posix_https.c) */
#define OPEN_MAXIMUM 16
static struct
{
	pthread_mutex_t lock;
	void *open[OPEN_MAXIMUM];
	volatile int abort;
	int flag_given;
} net = { PTHREAD_MUTEX_INITIALIZER, { 0 }, 0, 0 };

static void remember(void *context)
{
	int index;

	pthread_mutex_lock(&net.lock);
	for (index = 0; index < OPEN_MAXIMUM; index++)
		if (!net.open[index])
		{
			net.open[index] = context;
			break;
		}
	/* (opened after the cancel: its waits end at once too) */
	if (net.abort)
		posix_https_stream_shutdown(context);
	pthread_mutex_unlock(&net.lock);
}

static void forget(void *context)
{
	int index;

	pthread_mutex_lock(&net.lock);
	for (index = 0; index < OPEN_MAXIMUM; index++)
		if (net.open[index] == context)
			net.open[index] = NULL;
	pthread_mutex_unlock(&net.lock);
}

void steam_net_abort(int on)
{
	int index;

	pthread_mutex_lock(&net.lock);
	net.abort = on;
	if (on)
		for (index = 0; index < OPEN_MAXIMUM; index++)
			if (net.open[index])
				posix_https_stream_shutdown(net.open[index]);
	pthread_mutex_unlock(&net.lock);
}

int steam_net_aborted(void)
{
	return net.abort;
}

void steam_random(void *buffer, size_t size)
{
	posix_random_bytes(buffer, (posix_ulong)size);
}

static int stream_write(void *context, const void *data, size_t size)
{
	while (size)
	{
		int part = size > (1u << 30) ? 1 << 30 : (int)size;

		if (!posix_https_stream_write(context, data, part))
			return 0;
		data = (const char *)data + part;
		size -= (size_t)part;
	}
	return 1;
}

static long stream_read(void *context, void *buffer, size_t capacity, int timeout_ms)
{
	int part = capacity > (1u << 30) ? 1 << 30 : (int)capacity;

	return posix_https_stream_read(context, buffer, part, timeout_ms);
}

static void stream_close(void *context)
{
	forget(context);
	posix_https_stream_close(context);
}

int steam_net_open(struct steam_stream *stream, const char *url, char *host, size_t host_capacity, char *path,
	size_t path_capacity, char *error, int error_size)
{
	char http_url[600];
	const char *rest;
	int tls;
	void *opened;

	memset(stream, 0, sizeof(*stream));
	if (!strncmp(url, "wss://", 6) || !strncmp(url, "https://", 8))
		tls = 1, rest = strstr(url, "://") + 3;
	else if (!strncmp(url, "ws://", 5) || !strncmp(url, "http://", 7))
		tls = 0, rest = strstr(url, "://") + 3;
	else
	{
		snprintf(error, (size_t)error_size, "not a WebSocket or HTTP URL");
		return 0;
	}
	if (snprintf(http_url, sizeof(http_url), "%s://%s", tls ? "https" : "http", rest) >= (int)sizeof(http_url))
	{
		snprintf(error, (size_t)error_size, "URL too long");
		return 0;
	}
	if (!net.flag_given)
	{
		net.flag_given = 1;
		posix_https_stream_abort_flag(&net.abort);
	}
	/* (posix_https.c refuses plain http to anything but a loopback address) */
	opened = posix_https_stream_open(http_url, host, (int)host_capacity, path, (int)path_capacity, error, error_size);
	if (!opened)
		return 0;
	remember(opened);
	stream->context = opened;
	stream->write = stream_write;
	stream->read = stream_read;
	stream->close = stream_close;
	return 1;
}

int steam_net_http_open(struct steam_http *http, const char *base_url, char *error, int error_size)
{
	char url[600], path[512];

	memset(http, 0, sizeof(*http));
	if (snprintf(url, sizeof(url), "%s/", base_url) >= (int)sizeof(url))
	{
		snprintf(error, (size_t)error_size, "URL too long");
		return 0;
	}
	if (!steam_net_open(&http->stream, url, http->host, sizeof(http->host), path, sizeof(path), error, error_size))
		return 0;
	http->open = 1;
	return 1;
}
