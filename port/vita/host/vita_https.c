/*
VITA_HTTPS.C

HTTPS on the Vita: the consoles' client (the game's
port/console/https/console_https.c, Mbed TLS with the site's key and
GitHub's roots pinned) over the Vita's sockets, through
struct console_https_port; and the game list's key files
(port/linux/src/browser_http.h posix_browser_private_key and its kin), in
the app's data folder. The game list's requests themselves
(posix_browser_request ...) are the game's
port/console/https/console_browser_http.c, over this.

The client asks only the hosts it pins. A dev build made with
configure.py --vita-delta-standin can be pointed at a stand-in for the site
for tests (never the real servers, nor Steam's): HALO_DELTA_STANDIN=a.b.c.d:port in
env.txt sends every HTTPS connection there, and HALO_DELTA_STANDIN_PIN=<64
hex digits> (the SHA-256 of the stand-in certificate's public key, as
console_https_trust.h has its pins) is the key the site's name is taken
with in place of the site's own. A release build has neither.
*/

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/rng.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/rtc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "posix.h"
#include "console_https.h"
#include "vita_host.h"
#ifdef VITA_DELTA_STANDIN
#include "console_https_trust.h"
#endif

#ifndef VITA_DATA_DIRECTORY
#define VITA_DATA_DIRECTORY "ux0:data/warthog-vita"
#endif

static SceUID https_lock;

#ifdef VITA_DELTA_STANDIN
/* (the site, and Steam's Workshop pages, API and pictures the Guide's
WORKSHOP asks for: the stand-in answers for each, by its one key) */
static const char *const standin_hosts[] = { CONSOLE_HTTPS_SITE, "steamcommunity.com", "api.steampowered.com",
	"images.steamusercontent.com" };
#define STANDIN_HOSTS 4
static struct
{
	int on;
	unsigned long address;
	unsigned short port;
	unsigned char pin[1][CONSOLE_HTTPS_PIN_SIZE];
	struct console_https_trust trust[STANDIN_HOSTS];
} standin;
#endif

static void log_text(const char *text)
{
	char line[256];

	snprintf(line, sizeof(line), "https: %s", text);
	vita_host_log(line);
}

static void set_timeout(int socket, int option, unsigned long timeout_ms)
{
	int microseconds = (int)(timeout_ms > 2000000ul ? 2000000000 : timeout_ms * 1000);

	sceNetSetsockopt(socket, SCE_NET_SOL_SOCKET, option, &microseconds, sizeof(microseconds));
}

static int https_connect(const char *host, unsigned short port, unsigned long timeout_ms, console_https_socket *out)
{
	SceNetSockaddrIn address;
	unsigned long long deadline = sceKernelGetProcessTimeWide() + (unsigned long long)timeout_ms * 1000;
	unsigned long resolved;
	int socket, result, nonblocking = 1, blocking = 0;

	resolved = (unsigned long)posix_resolve_ipv4(host);
#ifdef VITA_DELTA_STANDIN
	if (standin.on)
	{
		resolved = standin.address;
		port = standin.port;
	}
#endif
	if (!resolved)
		return 0;
	socket = sceNetSocket("https", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
	if (socket < 0)
		return 0;
	memset(&address, 0, sizeof(address));
	address.sin_len = sizeof(address);
	address.sin_family = SCE_NET_AF_INET;
	address.sin_port = sceNetHtons(port);
	address.sin_addr.s_addr = (unsigned int)resolved;
	/* (a connection begun without waiting, then asked after until it is
	made or the time is up) */
	sceNetSetsockopt(socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nonblocking, sizeof(nonblocking));
	for (;;)
	{
		result = sceNetConnect(socket, (SceNetSockaddr *)&address, sizeof(address));
		if (result >= 0 || (result & 0xFF) == SCE_NET_EISCONN)
			break;
		if ((result & 0xFF) != SCE_NET_EINPROGRESS && (result & 0xFF) != SCE_NET_EALREADY &&
			(result & 0xFF) != SCE_NET_EWOULDBLOCK)
		{
			sceNetSocketClose(socket);
			return 0;
		}
		if (sceKernelGetProcessTimeWide() >= deadline)
		{
			sceNetSocketClose(socket);
			return 0;
		}
		sceKernelDelayThread(10000);
	}
	sceNetSetsockopt(socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &blocking, sizeof(blocking));
	*out = (console_https_socket)(long)(socket + 1);
	return 1;
}

static int socket_of(console_https_socket socket)
{
	return (int)(long)socket - 1;
}

static int https_send(console_https_socket socket, const unsigned char *data, int size, unsigned long timeout_ms)
{
	int result;

	set_timeout(socket_of(socket), SCE_NET_SO_SNDTIMEO, timeout_ms);
	result = sceNetSend(socket_of(socket), data, (unsigned int)size, 0);
	if (result > 0)
		return result;
	if ((result & 0xFF) == SCE_NET_EAGAIN || (result & 0xFF) == SCE_NET_ETIMEDOUT)
		return CONSOLE_HTTPS_IO_TIMEOUT;
	return -1;
}

static int https_receive(console_https_socket socket, unsigned char *buffer, int size, unsigned long timeout_ms)
{
	int result;

	set_timeout(socket_of(socket), SCE_NET_SO_RCVTIMEO, timeout_ms);
	result = sceNetRecv(socket_of(socket), buffer, (unsigned int)size, 0);
	if (result >= 0)
		return result;
	if ((result & 0xFF) == SCE_NET_EAGAIN || (result & 0xFF) == SCE_NET_ETIMEDOUT)
		return CONSOLE_HTTPS_IO_TIMEOUT;
	return -1;
}

static void https_close(console_https_socket socket)
{
	sceNetSocketClose(socket_of(socket));
}

static int https_random(unsigned char *buffer, int size)
{
	/* (the kernel's generator gives at most 64 bytes a call) */
	while (size > 0)
	{
		int piece = size > 64 ? 64 : size;

		if (sceKernelGetRandomNumber(buffer, (SceSize)piece) < 0)
			return 0;
		buffer += piece;
		size -= piece;
	}
	return 1;
}

static unsigned long https_milliseconds(void)
{
	return (unsigned long)(sceKernelGetProcessTimeWide() / 1000);
}

static void https_take(void)
{
	sceKernelLockMutex(https_lock, 1, NULL);
}

static void https_give(void)
{
	sceKernelUnlockMutex(https_lock, 1);
}

static long long https_unix_seconds(void)
{
	SceRtcTick tick;

	/* (ticks are microseconds since 0001-01-01; 1970's is 62135596800 s on) */
	if (sceRtcGetCurrentTickUtc(&tick) < 0)
		return 0;
	return (long long)(tick.tick / 1000000ull) - 62135596800ll;
}

static const struct console_https_port https_port =
{
	https_connect, https_send, https_receive, https_close, https_random, https_milliseconds, https_take,
	https_give, log_text, "Warthog/1 (PS Vita)", NULL, VITA_DATA_DIRECTORY "/console-pins.json",
	https_unix_seconds, NULL,
};

#ifdef VITA_DELTA_STANDIN
static int hex_value(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* (dev builds with --vita-delta-standin) HALO_DELTA_STANDIN and its pin */
static void standin_read(void)
{
	const char *where = getenv("HALO_DELTA_STANDIN");
	const char *pin = getenv("HALO_DELTA_STANDIN_PIN");
	unsigned int a, b, c, d, port;
	int index;

	if (!where || !*where)
		return;
	if (sscanf(where, "%u.%u.%u.%u:%u", &a, &b, &c, &d, &port) != 5 || a > 255 || b > 255 || c > 255 || d > 255 ||
		!port || port > 65535 || !pin || strlen(pin) != 2 * CONSOLE_HTTPS_PIN_SIZE)
	{
		log_text("HALO_DELTA_STANDIN needs a.b.c.d:port and HALO_DELTA_STANDIN_PIN (64 hex digits): not used");
		return;
	}
	for (index = 0; index < CONSOLE_HTTPS_PIN_SIZE; index++)
	{
		int high = hex_value(pin[2 * index]), low = hex_value(pin[2 * index + 1]);

		if (high < 0 || low < 0)
		{
			log_text("HALO_DELTA_STANDIN_PIN is not hex: not used");
			return;
		}
		standin.pin[0][index] = (unsigned char)(high * 16 + low);
	}
	/* (s_addr in the network's order: the first number first in memory) */
	standin.address = (unsigned long)(a | (b << 8) | (c << 16) | (d << 24));
	standin.port = (unsigned short)port;
	for (index = 0; index < STANDIN_HOSTS; index++)
	{
		standin.trust[index].host = standin_hosts[index];
		standin.trust[index].pins = (const unsigned char (*)[CONSOLE_HTTPS_PIN_SIZE])standin.pin;
		standin.trust[index].pin_count = 1;
		standin.trust[index].roots = NULL;
		standin.trust[index].root_count = 0;
	}
	console_https_trust_for_tests(standin.trust, STANDIN_HOSTS);
	standin.on = 1;
	{
		char line[160];

		snprintf(line, sizeof(line), "a dev build's stand-in for the site: every connection to %u.%u.%u.%u:%u, "
			"its key pinned for the site's and Steam's Workshop names alone", a, b, c, d, port);
		log_text(line);
	}
}
#endif

void vita_host_https_start(void)
{
	https_lock = sceKernelCreateMutex("https", 0, 0, NULL);
	if (https_lock < 0)
	{
		log_text("no lock: no HTTPS");
		return;
	}
#ifdef VITA_DELTA_STANDIN
	standin_read();
#endif
	console_https_initialize(&https_port);
}

/* ---------- the game list's keys (browser_http.h) */

/* a key file's size bytes into key: read, or when there is none made of
random bytes and written; 1 when key holds the file's */
static int key_file(const char *path, unsigned char *key, int size, const char *what)
{
	SceUID file = sceIoOpen(path, SCE_O_RDONLY, 0);
	char line[192];
	int ok;

	if (file >= 0)
	{
		ok = sceIoRead(file, key, (SceSize)size) == size;
		sceIoClose(file);
		if (!ok)
		{
			snprintf(line, sizeof(line), "%s's file is short: not used", what);
			log_text(line);
		}
		return ok;
	}
	if (!https_random(key, size))
		return 0;
	file = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_EXCL, 0600);
	if (file < 0)
		return 0;
	ok = sceIoWrite(file, key, (SceSize)size) == size;
	sceIoClose(file);
	if (!ok)
		sceIoRemove(path);
	snprintf(line, sizeof(line), "%s made", what);
	log_text(line);
	return ok;
}

int posix_browser_private_key(const char *path, unsigned char *key, int size)
{
	return key_file(path, key, size, "the player key");
}

int posix_browser_device_key(const char *path, unsigned char *key, int size)
{
	return key_file(path, key, size, "the device key");
}

int posix_browser_replace_key(const char *path, const unsigned char *key, int size)
{
	char temporary[1100];
	SceUID file;
	int ok;

	snprintf(temporary, sizeof(temporary), "%s.new", path);
	sceIoRemove(temporary);
	file = sceIoOpen(temporary, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_EXCL, 0600);
	if (file < 0)
		return 0;
	ok = sceIoWrite(file, key, (SceSize)size) == size;
	sceIoClose(file);
	if (ok)
	{
		sceIoRemove(path);
		ok = sceIoRename(temporary, path) >= 0;
	}
	if (!ok)
		sceIoRemove(temporary);
	return ok;
}
