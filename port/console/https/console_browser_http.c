/*
CONSOLE_BROWSER_HTTP.C

The game list's requests (port/linux/src/browser_http.h: browser.c, the
Guide's friends and party, Online Games' pictures) on a console with the
game list (HALO_GAME_BROWSER) and without the desktop's TLS
(port/linux/src/posix_browser.c): over the consoles' HTTPS client
(console_https.h), which asks only the hosts it pins
(stats.chupathingy.org, GitHub's raw files), each checked by its key or
chain. A URL to any other host, or over plain HTTP, fails with the reason
in its error, as a desktop's request to a host that does not answer.

What the consoles' client does not do: a request with headers of its own
(posix_browser_send's headers: the desktop's device identity, which a
console leaves out, HALO_CONSOLE_NO_DELTA_IDENTITY) and a body that is not
text (a clip's video: no capture on a console) fail; a download (a map's
picture, a playlist) is read whole into memory, at most
CONSOLE_BROWSER_DOWNLOAD_MAXIMUM bytes, as it is opened.

The keys' files (posix_browser_private_key and its kin) are the port's:
they need its files and its random bytes.

Every request blocks: from a thread of its own, with
CONSOLE_HTTPS_STACK_SIZE + CONSOLE_HTTPS_STREAM_BUFFER_SIZE of stack to
spare (console_https.h).
*/

#if defined(HALO_GAME_BROWSER) && defined(HALO_CONSOLE_BROWSER_HTTPS)

#include "browser_http.h"
#include "console_https.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CONSOLE_BROWSER_DOWNLOAD_MAXIMUM
#define CONSOLE_BROWSER_DOWNLOAD_MAXIMUM (4ul * 1024 * 1024)
#endif
/* (an answer's headers, beside its body in the caller's buffer) */
#define HEADERS_ROOM 4096
#define REQUEST_TIMEOUT 10000ul

/* url's host and path (https:// alone): 1, or 0 with why in error */
static int split_url(const char *url, char *host, int host_size, char *path, int path_size, char *error,
	int error_size)
{
	const char *start, *slash;
	int length;

	if (strncmp(url, "https://", 8))
	{
		snprintf(error, (size_t)error_size, "only HTTPS on this console");
		return 0;
	}
	start = url + 8;
	slash = strchr(start, '/');
	length = slash ? (int)(slash - start) : (int)strlen(start);
	if (length <= 0 || length >= host_size || memchr(start, ':', (size_t)length) || memchr(start, '@', (size_t)length))
	{
		snprintf(error, (size_t)error_size, "not a host this console asks");
		return 0;
	}
	memcpy(host, start, (size_t)length);
	host[length] = 0;
	if (snprintf(path, (size_t)path_size, "%s", slash ? slash : "/") >= path_size)
	{
		snprintf(error, (size_t)error_size, "the address is too long");
		return 0;
	}
	return 1;
}

static int request(const char *url, const char *body, const char *content_type, char *response, int response_size,
	char *error, int error_size)
{
	char host[128], path[1024];
	char *answer;
	const char *answer_body;
	unsigned long size, received = 0, body_size = 0;
	int status = 0, result;

	if (error_size > 0)
		error[0] = 0;
	if (response_size > 0)
		response[0] = 0;
	if (response_size <= 0 || !split_url(url, host, sizeof(host), path, sizeof(path), error, error_size))
		return 0;
	/* (the answer whole, headers and all, then its body moved down) */
	size = (unsigned long)response_size + HEADERS_ROOM;
	answer = malloc(size);
	if (!answer)
	{
		snprintf(error, (size_t)error_size, "out of memory");
		return 0;
	}
	result = console_https_request(host, path, body, content_type, answer, size, &received, REQUEST_TIMEOUT);
	if (result == CONSOLE_HTTPS_ERROR_TOO_LARGE && received)
	{
		/* (an answer longer than the buffer: its start, cut as the desktop's
		is, from its status line and the end of its headers) */
		const char *end = memmem(answer, received, "\r\n\r\n", 4);

		if (end && sscanf(answer, "HTTP/%*s %d", &status) == 1)
		{
			answer_body = end + 4;
			body_size = received - (unsigned long)(answer_body - answer);
			result = CONSOLE_HTTPS_OK;
		}
	}
	else if (result == CONSOLE_HTTPS_OK &&
		!console_https_parse_response(answer, received, &status, &answer_body, &body_size))
		result = CONSOLE_HTTPS_ERROR_RESPONSE;
	if (result != CONSOLE_HTTPS_OK)
	{
		snprintf(error, (size_t)error_size, "%s", console_https_error_string(result));
		free(answer);
		return 0;
	}
	if (body_size > (unsigned long)response_size - 1)
		body_size = (unsigned long)response_size - 1;
	memcpy(response, answer_body, body_size);
	response[body_size] = 0;
	free(answer);
	return status;
}

int posix_browser_request(const char *url, const char *body, const char *content_type, char *response,
	int response_size, char *error, int error_size)
{
	/* (form fields when no type is named, as the desktop's) */
	return request(url, body, body && !content_type ? "application/x-www-form-urlencoded" : content_type, response,
		response_size, error, error_size);
}

int posix_browser_request_as(const char *url, const char *body, const char *content_type, const char *user_agent,
	char *response, int response_size, char *error, int error_size)
{
	/* (the console's own user agent: console_https_port's) */
	(void)user_agent;
	return posix_browser_request(url, body, content_type, response, response_size, error, error_size);
}

int posix_browser_send(const char *url, const char *body, size_t body_length, const char *content_type,
	const char *headers, char *response, int response_size, char *error, int error_size)
{
	if ((headers && *headers) || (body && strlen(body) != body_length))
	{
		if (error_size > 0)
			snprintf(error, (size_t)error_size, "not sent from this console");
		if (response_size > 0)
			response[0] = 0;
		return 0;
	}
	return posix_browser_request(url, body, content_type, response, response_size, error, error_size);
}

/* ---------- downloads: the body read whole as it is opened */

static struct
{
	int open;
	unsigned char *data;
	unsigned long length, capacity, read;
	int too_large;
} download;

static void download_size(unsigned long size, void *context)
{
	(void)context;
	if (size > CONSOLE_BROWSER_DOWNLOAD_MAXIMUM)
		download.too_large = 1;
}

static int download_body(const char *data, unsigned long size, void *context)
{
	(void)context;
	if (download.too_large || download.length + size > CONSOLE_BROWSER_DOWNLOAD_MAXIMUM)
	{
		download.too_large = 1;
		return 0;
	}
	if (download.length + size > download.capacity)
	{
		unsigned long capacity = download.capacity ? download.capacity : 64 * 1024;
		unsigned char *grown;

		while (capacity < download.length + size)
			capacity *= 2;
		if (capacity > CONSOLE_BROWSER_DOWNLOAD_MAXIMUM)
			capacity = CONSOLE_BROWSER_DOWNLOAD_MAXIMUM;
		grown = realloc(download.data, capacity);
		if (!grown)
			return 0;
		download.data = grown;
		download.capacity = capacity;
	}
	memcpy(download.data + download.length, data, size);
	download.length += size;
	return 1;
}

void posix_browser_download_close(int handle)
{
	(void)handle;
	free(download.data);
	memset(&download, 0, sizeof(download));
}

int posix_browser_download_open(const char *url, const char *user_agent, int *status, int *length, char *error,
	int error_size)
{
	char host[128], path[1024];
	int result;

	(void)user_agent;
	*status = 0;
	*length = -1;
	if (error_size > 0)
		error[0] = 0;
	if (download.open)
	{
		snprintf(error, (size_t)error_size, "another download is open");
		return 0;
	}
	if (!split_url(url, host, sizeof(host), path, sizeof(path), error, error_size))
		return 0;
	memset(&download, 0, sizeof(download));
	result = console_https_get_stream(host, path, NULL, status, download_size, download_body, NULL, REQUEST_TIMEOUT);
	if (result != CONSOLE_HTTPS_OK || *status < 200 || *status >= 300)
	{
		snprintf(error, (size_t)error_size, "%s", download.too_large ? "too large for this console" :
			result != CONSOLE_HTTPS_OK ? console_https_error_string(result) : "the server refused it");
		posix_browser_download_close(1);
		return 0;
	}
	download.open = 1;
	*length = (int)download.length;
	return 1;
}

int posix_browser_download_read(int handle, void *buffer, int size)
{
	unsigned long left;

	(void)handle;
	if (!download.open || size < 0)
		return -1;
	left = download.length - download.read;
	if ((unsigned long)size > left)
		size = (int)left;
	memcpy(buffer, download.data + download.read, (size_t)size);
	download.read += (unsigned long)size;
	return size;
}

void posix_browser_download_deadline(int handle, int milliseconds)
{
	/* (read whole as it was opened) */
	(void)handle;
	(void)milliseconds;
}

/* (console_https.c takes its port's lock around each request) */
void posix_browser_lock(void)
{
}

void posix_browser_unlock(void)
{
}

void *posix_browser_certificates(void)
{
	return NULL;
}

#endif
