/*
CONSOLE_BROWSER_HTTP_TEST.C

The game list's requests over the consoles' HTTPS
(port/console/https/console_browser_http.c) on the host, for
tools/test_console_browser_http.py: its port is BSD sockets that connect
every host to the loopback address and the test server's port; the site's
name is taken with the test's own key (console_https_trust_for_tests).

  console_browser_http_test PORT PIN_HEX

Each check prints a line, "ok ..." or "FAILED ...", and the exit status is
the failures' count.
*/

#include "browser_http.h"
#include "console_https.h"
#include "console_https_trust.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static unsigned short server_port;
static int failures;

static unsigned long now_ms(void)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	return (unsigned long)now.tv_sec * 1000ul + (unsigned long)now.tv_usec / 1000ul;
}

static int wait_ready(int socket_value, int write, unsigned long timeout_ms)
{
	fd_set set;
	struct timeval wait;

	FD_ZERO(&set);
	FD_SET(socket_value, &set);
	wait.tv_sec = (long)(timeout_ms / 1000);
	wait.tv_usec = (long)(timeout_ms % 1000) * 1000;
	return select(socket_value + 1, write ? NULL : &set, write ? &set : NULL, NULL, &wait) > 0;
}

static int test_connect(const char *host, unsigned short port, unsigned long timeout_ms, console_https_socket *out)
{
	struct sockaddr_in target;
	int socket_value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	(void)host;
	(void)port;
	(void)timeout_ms;
	if (socket_value < 0)
		return 0;
	memset(&target, 0, sizeof(target));
	target.sin_family = AF_INET;
	target.sin_port = htons(server_port);
	target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(socket_value, (struct sockaddr *)&target, sizeof(target)) != 0)
	{
		close(socket_value);
		return 0;
	}
	*out = (console_https_socket)(long)socket_value;
	return 1;
}

static int test_send(console_https_socket socket_handle, const unsigned char *data, int size, unsigned long timeout_ms)
{
	int socket_value = (int)(long)socket_handle;
	ssize_t count;

	if (!wait_ready(socket_value, 1, timeout_ms))
		return CONSOLE_HTTPS_IO_TIMEOUT;
	count = send(socket_value, data, (size_t)size, 0);
	return count > 0 ? (int)count : -1;
}

static int test_receive(console_https_socket socket_handle, unsigned char *buffer, int size, unsigned long timeout_ms)
{
	int socket_value = (int)(long)socket_handle;
	ssize_t count;

	if (!wait_ready(socket_value, 0, timeout_ms))
		return CONSOLE_HTTPS_IO_TIMEOUT;
	count = recv(socket_value, buffer, (size_t)size, 0);
	if (count < 0)
		return errno == ECONNRESET ? 0 : -1;
	return (int)count;
}

static void test_close(console_https_socket socket_handle)
{
	close((int)(long)socket_handle);
}

static int test_random(unsigned char *buffer, int size)
{
	FILE *file = fopen("/dev/urandom", "rb");
	int ok;

	if (!file)
		return 0;
	ok = fread(buffer, 1, (size_t)size, file) == (size_t)size;
	fclose(file);
	return ok;
}

static void test_log(const char *text)
{
	fprintf(stderr, "%s\n", text);
}

static const struct console_https_port port =
{
	test_connect, test_send, test_receive, test_close, test_random, now_ms, NULL, NULL, test_log,
	"Warthog/1 (test)", NULL, NULL, NULL, NULL,
};

static void check(int ok, const char *what, const char *detail)
{
	printf("%s %s%s%s\n", ok ? "ok" : "FAILED", what, detail && *detail ? ": " : "", detail ? detail : "");
	failures += !ok;
}

int main(int argc, char **argv)
{
	static unsigned char pins[1][CONSOLE_HTTPS_PIN_SIZE];
	static struct console_https_trust trust[1];
	char response[512], error[256], small[8];
	int index, status, handle, length;

	if (argc != 3 || strlen(argv[2]) != 2 * CONSOLE_HTTPS_PIN_SIZE)
	{
		fprintf(stderr, "usage: console_browser_http_test PORT PIN_HEX\n");
		return 2;
	}
	server_port = (unsigned short)atoi(argv[1]);
	for (index = 0; index < CONSOLE_HTTPS_PIN_SIZE; index++)
	{
		unsigned int byte;

		sscanf(argv[2] + 2 * index, "%2x", &byte);
		pins[0][index] = (unsigned char)byte;
	}
	trust[0].host = CONSOLE_HTTPS_SITE;
	trust[0].pins = (const unsigned char (*)[CONSOLE_HTTPS_PIN_SIZE])pins;
	trust[0].pin_count = 1;
	console_https_trust_for_tests(trust, 1);
	console_https_initialize(&port);

	/* a GET: the body alone, NUL terminated */
	status = posix_browser_request("https://" CONSOLE_HTTPS_SITE "/v1/games.txt?fields=28", NULL, NULL, response,
		sizeof(response), error, sizeof(error));
	check(status == 200 && !strncmp(response, "GET /v1/games.txt?fields=28|", 28), "GET's body", status ? response : error);
	/* a POST of JSON, and one of form fields when no type is named */
	status = posix_browser_request("https://" CONSOLE_HTTPS_SITE "/v1/connect/start", "{\"key\": \"00\"}",
		"application/json", response, sizeof(response), error, sizeof(error));
	check(status == 200 && strstr(response, "|application/json|{\"key\": \"00\"}") != NULL, "POST of JSON", response);
	status = posix_browser_request_as("https://" CONSOLE_HTTPS_SITE "/v1/withdraw", "invite=ab", NULL, "Other/2",
		response, sizeof(response), error, sizeof(error));
	check(status == 200 && strstr(response, "|application/x-www-form-urlencoded|invite=ab") != NULL &&
		strstr(response, "|Warthog/1 (test)|") != NULL, "POST of form fields, the console's user agent", response);
	/* an answer cut to the buffer, as the desktop's */
	status = posix_browser_request("https://" CONSOLE_HTTPS_SITE "/big", NULL, NULL, small, sizeof(small), error,
		sizeof(error));
	check(status == 200 && strlen(small) == sizeof(small) - 1, "an answer cut to the buffer", small);
	/* the hosts it never asks, and plain HTTP */
	status = posix_browser_request("https://example.com/v1/games.txt", NULL, NULL, response, sizeof(response), error,
		sizeof(error));
	check(!status && strstr(error, "not a host") != NULL, "another host refused", error);
	status = posix_browser_request("http://" CONSOLE_HTTPS_SITE "/v1/games.txt", NULL, NULL, response,
		sizeof(response), error, sizeof(error));
	check(!status && strstr(error, "HTTPS") != NULL, "plain HTTP refused", error);
	status = posix_browser_request("https://" CONSOLE_HTTPS_SITE ":8443/v1/games.txt", NULL, NULL, response,
		sizeof(response), error, sizeof(error));
	check(!status, "a port in the address refused", error);
	/* headers of its own (the desktop's device identity) and a body that is
	not text are not sent */
	status = posix_browser_send("https://" CONSOLE_HTTPS_SITE "/v1/presence", "{}", 2, "application/json",
		"Authorization: Delta x\r\n", response, sizeof(response), error, sizeof(error));
	check(!status, "a request with headers of its own refused", error);
	status = posix_browser_send("https://" CONSOLE_HTTPS_SITE "/v1/presence", "{}", 2, "application/json", NULL,
		response, sizeof(response), error, sizeof(error));
	check(status == 200, "a send without headers", status ? response : error);
	/* a download, read whole as it is opened */
	handle = posix_browser_download_open("https://" CONSOLE_HTTPS_SITE "/big", "Other/2", &status, &length, error,
		sizeof(error));
	if (handle)
	{
		char piece[1000];
		int total = 0, count;

		while ((count = posix_browser_download_read(handle, piece, sizeof(piece))) > 0)
			total += count;
		posix_browser_download_close(handle);
		check(status == 200 && length == 6000 && total == 6000 && count == 0, "a download read to its end", NULL);
	}
	else
		check(0, "a download", error);
	handle = posix_browser_download_open("https://" CONSOLE_HTTPS_SITE "/missing", NULL, &status, &length, error,
		sizeof(error));
	check(!handle && status == 404, "a download the server refuses", error);
	return failures;
}
