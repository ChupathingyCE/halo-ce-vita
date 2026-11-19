/*
VITA_STUBS.C

What the desktop platform layer asks of the host (the game's
port/linux/src/posix.h) that the Vita does without or does its own way:
UPnP, the desktop's process and link handling, Discord, the C library's
missing pieces, and the generic atomics clang calls for the game's
unaligned atomic fields. Sockets are vita_net.c; files the game's
posix_files.c (port/vita/overrides), built with the SDK's ABI.
*/

#include <psp2/kernel/rng.h>
#include <psp2/kernel/threadmgr.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "posix.h"
#include "vita_compat.h"

/* the stack of a thread made with no attributes (pthread_create(&thread,
NULL, ...)): the SDK's pthread library takes this in place of its 32 KB
minimum. Internet play's threads (p2p.c's, which keeps two 754-socket
lists on its stack and selects over up to 128 sockets, its UPnP thread)
were made for desktops' megabytes; xbox_kernel.c gives the game's own
threads theirs explicitly. One made with attributes takes their size alone,
and with none set (0) gets the library's 4 KiB minimum, not this */
unsigned int _pthread_stack_default_user = 256 * 1024;

/* the kernel's random number generator, four bytes at a time */
void posix_random_bytes(void *buffer, posix_ulong size)
{
	unsigned char *out = buffer;
	posix_ulong done = 0;

	while (done < size)
	{
		unsigned char word[4];
		posix_ulong take = size - done < sizeof(word) ? size - done : sizeof(word);

		if (sceKernelGetRandomNumber(word, sizeof(word)) < 0)
			abort();
		memcpy(out + done, word, take);
		done += take;
	}
}

/* no UPnP on the Vita: internet play hosts through its tunnel */
int posix_upnp_forward_udp(unsigned short port, unsigned short preferred_port, posix_ulong *external_address,
	unsigned short *external_port, char *error, int error_size)
{
	(void)port;
	(void)preferred_port;
	(void)external_address;
	(void)external_port;
	if (error && error_size > 0)
		snprintf(error, (size_t)error_size, "the Vita has no UPnP");
	return 0;
}

void posix_upnp_stop_forwarding_udp(unsigned short external_port)
{
	(void)external_port;
}

/* no secret of the user's to derive keys from: the game makes its own */
int posix_user_secret(unsigned char *secret, int size)
{
	(void)secret;
	(void)size;
	return 0;
}

int posix_command_line_argument(int index, char *buffer, posix_ulong size) { (void)index; (void)buffer; (void)size; return 0; }
posix_ulong posix_process_id(void) { return 1; }
int posix_register_url_scheme(const char *scheme, const char *description) { (void)scheme; (void)description; return 0; }
int posix_register_discord_launch(const char *application) { (void)application; return 0; }
int posix_discord_connect(void) { return -1; }
int posix_discord_write(int handle, const void *buffer, int length) { (void)handle; (void)buffer; (void)length; return -1; }
int posix_discord_read(int handle, void *buffer, int length) { (void)handle; (void)buffer; (void)length; return -1; }
void posix_discord_close(int handle) { (void)handle; }

/* newlib has no utimensat (posix_files.c sets file times with it): the
Vita keeps the times it sets itself */
int utimensat(int directory, const char *path, const struct timespec times[2], int flags)
{
	(void)directory; (void)path; (void)times; (void)flags;
	return 0;
}

/* nor clock_nanosleep (dsound_sdl.c paces its mixer with it) */
int clock_nanosleep(clockid_t clock, int flags, const struct timespec *request, struct timespec *remain)
{
	struct timespec now, duration;

	(void)remain;
	if (!(flags & TIMER_ABSTIME))
		return nanosleep(request, NULL) == 0 ? 0 : errno;
	clock_gettime(clock, &now);
	duration.tv_sec = request->tv_sec - now.tv_sec;
	duration.tv_nsec = request->tv_nsec - now.tv_nsec;
	if (duration.tv_nsec < 0)
	{
		duration.tv_nsec += 1000000000L;
		duration.tv_sec--;
	}
	if (duration.tv_sec < 0)
		return 0;
	return nanosleep(&duration, NULL) == 0 ? 0 : errno;
}

/* xbox_files.c looks for the executable's folder through /proc; the Vita's
data root comes from HALO_DATA_ROOT (vita_main.c) */
ssize_t readlink(const char *path, char *buffer, size_t size)
{
	(void)path; (void)buffer; (void)size;
	errno = ENOSYS;
	return -1;
}

/* Sleep(INFINITE) (xbox_kernel.c) */
int pause(void)
{
	for (;;)
		sceKernelDelayThread(1000000);
	return -1;
}

/* the game's stack walk for a failed assertion (stack_walk_windows.c, a dev
build's display_assert): newlib has no execinfo, so no frames; the crash
dump has the stack */
int backtrace(void **buffer, int size)
{
	(void)buffer; (void)size;
	return 0;
}

/* ---------- generic atomics (libatomic)

clang, told nothing is aligned (-fmax-type-align=1), turns an atomic on any
field it cannot prove aligned - a structure's member reached through a
pointer, not only a short in a packed structure - into these library calls:
some 90 in the game (d3d8_gxm.c's waits, the scene packs, the texture
decoder, the stream reads, the lights...), on words that other code reaches
with the processor's own atomics (ldrex/strex, plain aligned loads and
stores). A lock and a byte copy were atomic only against each other: an
exchange here and an inline increment on the same word could lose either
(the render worker's sleepers), and a byte-wise copy could be read torn.

So a naturally aligned object of 1, 2, 4 or 8 bytes - every cross-thread
field there is - takes the processor's atomic of its size (this file is
GCC's, which assumes the alignment of the types below); only a truly
unaligned one (a packed structure's short) takes the lock, as before, and
is atomic against the lock's other users only. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t atomic_8;
typedef uint16_t atomic_16 __attribute__((aligned(2)));
typedef uint32_t atomic_32 __attribute__((aligned(4)));
typedef uint64_t atomic_64 __attribute__((aligned(8)));

static volatile int generic_atomic_lock;

void vita_host_log(const char *line);

/* (the first few objects that take the lock, named: a packed field the
game's other code may reach with the processor's atomics, which the lock
does not exclude) */
static void generic_atomic_unaligned(size_t size, const volatile void *object)
{
	static volatile int logged;

	if (__atomic_add_fetch(&logged, 1, __ATOMIC_RELAXED) <= 4)
	{
		char line[128];

		snprintf(line, sizeof(line), "atomics: a %u-byte object at %p not aligned to its size: under the lock",
			(unsigned)size, (const void *)object);
		vita_host_log(line);
	}
}

static void generic_atomic_acquire(void)
{
	while (__atomic_load_n(&generic_atomic_lock, __ATOMIC_RELAXED) ||
		__atomic_exchange_n(&generic_atomic_lock, 1, __ATOMIC_ACQUIRE))
		;
}

static void generic_atomic_release(void)
{
	__atomic_store_n(&generic_atomic_lock, 0, __ATOMIC_RELEASE);
}

/* the object's own size, naturally aligned: the processor's atomics */
static int generic_atomic_native(size_t size, const volatile void *object)
{
	return (size == 1 || size == 2 || size == 4 || size == 8) && ((uintptr_t)object & (size - 1)) == 0;
}

/* (the native ones, by size: `type` the size's, a and b its values) */
#define GENERIC_ATOMIC_BY_SIZE(size, operation) \
	switch (size) \
	{ \
	case 1: operation(atomic_8); break; \
	case 2: operation(atomic_16); break; \
	case 4: operation(atomic_32); break; \
	default: operation(atomic_64); break; \
	}

bool __atomic_compare_exchange(size_t size, void *object, void *expected, const void *desired, int success, int failure)
{
	bool matched;

	(void)success;
	(void)failure;
	if (generic_atomic_native(size, object))
	{
#define COMPARE_EXCHANGE(type) \
		{ \
			type was, want; \
			memcpy(&was, expected, sizeof(type)); \
			memcpy(&want, desired, sizeof(type)); \
			matched = __atomic_compare_exchange_n((type *)object, &was, want, false, __ATOMIC_SEQ_CST, \
				__ATOMIC_SEQ_CST); \
			if (!matched) \
				memcpy(expected, &was, sizeof(type)); \
		}
		GENERIC_ATOMIC_BY_SIZE(size, COMPARE_EXCHANGE)
#undef COMPARE_EXCHANGE
		return matched;
	}
	generic_atomic_unaligned(size, object);
	generic_atomic_acquire();
	matched = memcmp(object, expected, size) == 0;
	if (matched)
		memcpy(object, desired, size);
	else
		memcpy(expected, object, size);
	generic_atomic_release();
	return matched;
}

void __atomic_load(size_t size, const void *object, void *result, int order)
{
	(void)order;
	if (generic_atomic_native(size, object))
	{
#define LOAD(type) \
		{ \
			type value = __atomic_load_n((const type *)object, __ATOMIC_SEQ_CST); \
			memcpy(result, &value, sizeof(type)); \
		}
		GENERIC_ATOMIC_BY_SIZE(size, LOAD)
#undef LOAD
		return;
	}
	generic_atomic_unaligned(size, object);
	generic_atomic_acquire();
	memcpy(result, object, size);
	generic_atomic_release();
}

void __atomic_store(size_t size, void *object, const void *value, int order)
{
	(void)order;
	if (generic_atomic_native(size, object))
	{
#define STORE(type) \
		{ \
			type stored; \
			memcpy(&stored, value, sizeof(type)); \
			__atomic_store_n((type *)object, stored, __ATOMIC_SEQ_CST); \
		}
		GENERIC_ATOMIC_BY_SIZE(size, STORE)
#undef STORE
		return;
	}
	generic_atomic_unaligned(size, object);
	generic_atomic_acquire();
	memcpy(object, value, size);
	generic_atomic_release();
}

void __atomic_exchange(size_t size, void *object, const void *value, void *result, int order)
{
	(void)order;
	if (generic_atomic_native(size, object))
	{
#define EXCHANGE(type) \
		{ \
			type stored, was; \
			memcpy(&stored, value, sizeof(type)); \
			was = __atomic_exchange_n((type *)object, stored, __ATOMIC_SEQ_CST); \
			memcpy(result, &was, sizeof(type)); \
		}
		GENERIC_ATOMIC_BY_SIZE(size, EXCHANGE)
#undef EXCHANGE
		return;
	}
	generic_atomic_unaligned(size, object);
	generic_atomic_acquire();
	memcpy(result, object, size);
	memcpy(object, value, size);
	generic_atomic_release();
}
