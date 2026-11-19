/*
VITA_MAIN.C

The process: the data folder, the log, the settings passed as environment
variables, the clocks, and the game's main (the game's
source/shell/shell_xbox.c, renamed halo_main for the Vita build), which
runs on a thread of its own with a large stack.

Everything is kept in VITA_DATA_DIRECTORY (ux0:data/warthog-vita for a release,
ux0:data/warthog-vita-dev for a dev build;
port/vita/build.py), never the released Halo CE Vita's folder:

    maps/          the Xbox disc's maps (ui.map, bloodgulch.map, ...)
    config.toml    the settings (the game's port_config.c)
    saves/         profiles and saved games
    logs/          the game's log (debug.txt)
    halo.log       this side's log; halo-prev.log the run before's
    env.txt        optional NAME=value lines set in the environment first
*/

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/error.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/apputil.h>
#include <psp2/power.h>

#include <pthread.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vita_host.h"
#include "vita_version.h"

#ifndef VITA_DATA_DIRECTORY
#define VITA_DATA_DIRECTORY "ux0:data/warthog-vita"
#endif
#ifndef HALO_VERSION
#define HALO_VERSION "unknown"
#endif

/* newlib's heap (malloc): the platform layer's and the libraries'. The game's
own allocations are in the Xbox window's heap (the translated layout's
xbox_heap.c), so this is smaller than Halo CE Vita's 48 MB, which held both */
unsigned int _newlib_heap_size_user = 40 * 1024 * 1024;

/* the game's frames are large (the AI's firing position search overflowed
the main thread's default stack on hardware; Linux gives 8 MB) */
#define GAME_THREAD_STACK_SIZE (16 * 1024 * 1024)

int halo_main(int argc, char **argv);

const char *vita_host_data_directory(void)
{
	return VITA_DATA_DIRECTORY;
}

/* ---------- threads */

struct thread_start
{
	void (*function)(void *);
	void *argument;
};

static int thread_trampoline(SceSize size, void *argument)
{
	struct thread_start start = *(struct thread_start *)argument;

	(void)size;
	start.function(start.argument);
	return 0;
}

int vita_host_thread_start_stack(const char *name, void (*function)(void *), void *argument, int core, int priority,
	int stack_size)
{
	static const int masks[3] = { 0x10000, 0x20000, 0x40000 }; /* SCE_KERNEL_CPU_MASK_USER_0..2 */
	struct thread_start start;
	SceUID thread;

	start.function = function;
	start.argument = argument;
	thread = sceKernelCreateThread(name, thread_trampoline, priority, stack_size, 0,
		core >= 0 && core < 3 ? masks[core] : 0, NULL);
	if (thread < 0)
		return -1;
	sceKernelStartThread(thread, sizeof(start), &start);
	return 0;
}

int vita_host_thread_start_priority(const char *name, void (*function)(void *), void *argument, int core, int priority)
{
	return vita_host_thread_start_stack(name, function, argument, core, priority, 1024 * 1024);
}

/* a kernel semaphore: no pthread data needed, so any thread may wait on it */
#define WAKE_MAXIMUM 8

int vita_host_wake_create(const char *name)
{
	return sceKernelCreateSema(name, 0, 0, WAKE_MAXIMUM, NULL);
}

int vita_host_wake_wait(int id, unsigned long timeout_us)
{
	SceUInt timeout = (SceUInt)timeout_us;
	int result = sceKernelWaitSema(id, 1, timeout_us ? &timeout : NULL);

	if (result == (int)SCE_KERNEL_ERROR_WAIT_TIMEOUT)
		return 1;
	return result < 0 ? result : 0;
}

int vita_host_wake_poll(int id)
{
	return sceKernelPollSema(id, 1) < 0;
}

void vita_host_wake_delete(int id)
{
	if (id >= 0)
		sceKernelDeleteSema(id);
}

void vita_host_wake_signal(int id)
{
	sceKernelSignalSema(id, 1);
}

void vita_host_thread_priority_set(int priority)
{
	if (priority >= 64 && priority <= 191)
		sceKernelChangeThreadPriority(sceKernelGetThreadId(), priority);
}

void vita_host_pin_current_thread(int core)
{
	static const int masks[3] = { 0x10000, 0x20000, 0x40000 };

	if (core >= 0 && core < 3)
		sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), masks[core]);
}

/* the Xbox window (vita_memory.c), which the renderer maps for the GPU */
void *vita_host_window(unsigned long *size);

void *vita_host_arena(unsigned long *size)
{
	return vita_host_window(size);
}

int vita_host_memory_window_made(void)
{
	unsigned long size;

	return vita_host_window(&size) != NULL;
}

int vita_host_thread_start(const char *name, void (*function)(void *), void *argument, int core)
{
	return vita_host_thread_start_priority(name, function, argument, core, 0x10000100);
}

void vita_host_sleep_us(unsigned long microseconds)
{
	sceKernelDelayThread((SceUInt)microseconds);
}

unsigned long long vita_host_time_us(void)
{
	return sceKernelGetProcessTimeWide();
}

unsigned long vita_host_thread_id(void)
{
	return (unsigned long)sceKernelGetThreadId();
}

void *vita_host_block_alloc(const char *name, unsigned long size, int *uid)
{
	SceUID block = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, (size + 0xFFF) & ~0xFFFUL, NULL);
	void *base = NULL;

	if (block < 0)
		return NULL;
	sceKernelGetMemBlockBase(block, &base);
	*uid = block;
	return base;
}

void vita_host_block_free(int uid)
{
	if (uid >= 0)
		sceKernelFreeMemBlock(uid);
}

void vita_host_log_memory(const char *when)
{
	SceKernelFreeMemorySizeInfo info;
	char message[160];

	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);
	sceKernelGetFreeMemorySize(&info);
	snprintf(message, sizeof(message), "vita: free memory %s: user %d KB, cdram %d KB, phycont %d KB", when,
		info.size_user / 1024, info.size_cdram / 1024, info.size_phycont / 1024);
	vita_host_log(message);
}

/* ---------- the log

halo.log, written through a thread of its own: three sceIoWrite calls a
line waited for the memory card, which the cache file thread's reads keep
busy for up to a second. A line is copied into a ring the log thread
empties; a line that says it crashes is written at once, with everything
before it. */

#define LOG_RING_SIZE (256 * 1024)
static char log_ring[LOG_RING_SIZE];
static volatile unsigned long log_head, log_tail;
static volatile int log_lock;
static SceUID log_file = -1, log_semaphore = -1;
static int log_thread_state; /* 0 none, 1 running, -1 could not start */

static void log_acquire(void)
{
	while (__atomic_load_n(&log_lock, __ATOMIC_RELAXED) || __atomic_exchange_n(&log_lock, 1, __ATOMIC_ACQUIRE))
		;
}

static void log_release(void)
{
	__atomic_store_n(&log_lock, 0, __ATOMIC_RELEASE);
}

static void log_drain(void)
{
	static volatile int draining;
	static char chunk[16 * 1024];

	while (__atomic_load_n(&draining, __ATOMIC_RELAXED) || __atomic_exchange_n(&draining, 1, __ATOMIC_ACQUIRE))
		sceKernelDelayThread(100);
	for (;;)
	{
		unsigned long length, start, first;

		log_acquire();
		length = log_tail - log_head;
		log_release();
		if (length > sizeof(chunk))
			length = sizeof(chunk);
		start = log_head % LOG_RING_SIZE;
		first = length < LOG_RING_SIZE - start ? length : LOG_RING_SIZE - start;
		memcpy(chunk, log_ring + start, first);
		memcpy(chunk + first, log_ring, length - first);
		log_acquire();
		log_head += length;
		log_release();
		if (!length)
			break;
		if (log_file >= 0)
		{
			unsigned long long before = sceKernelGetProcessTimeWide();

			sceIoWrite(log_file, chunk, length);
			if (sceKernelGetProcessTimeWide() - before > 500000ULL)
			{
				char note[96];

				snprintf(note, sizeof(note), "log: a write of %lu bytes took %llu ms", length,
					(sceKernelGetProcessTimeWide() - before) / 1000ULL);
				vita_host_udp_line(note);
			}
		}
	}
	__atomic_store_n(&draining, 0, __ATOMIC_RELEASE);
}

static int log_thread(SceSize arguments_size, void *arguments)
{
	(void)arguments_size;
	(void)arguments;
	for (;;)
	{
		sceKernelWaitSema(log_semaphore, 1, NULL);
		log_drain();
	}
	return 0;
}

void vita_host_log(const char *line)
{
	char text[1100];
	unsigned long long now = sceKernelGetProcessTimeWide();
	int length;
	int crashing = strstr(line, "crash") != NULL;

	if (log_file < 0)
		log_file = sceIoOpen(VITA_DATA_DIRECTORY "/halo.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
	length = snprintf(text, sizeof(text), "%7llu.%03llu %s\n", now / 1000000ULL, now / 1000ULL % 1000ULL, line);
	if (length < 0)
		return;
	if (length >= (int)sizeof(text))
	{
		length = sizeof(text) - 1;
		text[length - 1] = '\n';
	}
	sceClibPrintf("%s", text);
	if (log_thread_state == 0)
	{
		log_thread_state = -1;
		log_semaphore = sceKernelCreateSema("halo log", 0, 0, 0x7fffffff, NULL);
		if (log_semaphore >= 0)
		{
			SceUID thread = sceKernelCreateThread("halo log", log_thread, 0x10000100, 32 * 1024, 0, 0, NULL);

			if (thread >= 0 && sceKernelStartThread(thread, 0, NULL) >= 0)
				log_thread_state = 1;
		}
	}
	log_acquire();
	if (log_thread_state == 1 && !crashing && log_tail - log_head + (unsigned long)length <= LOG_RING_SIZE)
	{
		unsigned long start = log_tail % LOG_RING_SIZE;
		unsigned long first = (unsigned long)length < LOG_RING_SIZE - start ? (unsigned long)length : LOG_RING_SIZE - start;

		memcpy(log_ring + start, text, first);
		memcpy(log_ring, text + first, (unsigned long)length - first);
		log_tail += (unsigned long)length;
		log_release();
		sceKernelSignalSema(log_semaphore, 1);
		return;
	}
	log_release();
	log_drain();
	if (log_file >= 0)
		sceIoWrite(log_file, text, length);
}

/* the log written out at the end (exit()'s handlers) */
static void log_flush_at_exit(void)
{
	vita_host_log("vita: the process is ending (exit)");
	log_drain();
}

/* a line written at once to events.txt (opened, written, closed): what must
reach the card even when the log's thread no longer runs */
static void event_line(const char *text)
{
	SceUID file = sceIoOpen(VITA_DATA_DIRECTORY "/events.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
	char line[200];
	unsigned long long now = sceKernelGetProcessTimeWide();
	int length = snprintf(line, sizeof(line), "%llu.%03llu %s\n", now / 1000000ULL, now / 1000ULL % 1000ULL, text);

	if (file >= 0)
	{
		sceIoWrite(file, line, length > 0 && length < (int)sizeof(line) ? length : (int)sizeof(line) - 1);
		sceIoClose(file);
	}
}

/* ---------- (debug) the file calls, to the UDP log (HALO_UDP_LOG): which
call a stall is in */
void vita_host_udp_line(const char *text);
static int udp_trace = -1;

static void file_trace(const char *call, const char *path, int result, unsigned long long started)
{
	char line[400];
	unsigned long long now = sceKernelGetProcessTimeWide();

	if (udp_trace < 0)
		udp_trace = getenv("HALO_UDP_LOG") != NULL;
	if (!udp_trace)
		return;
	snprintf(line, sizeof(line), "%llu.%03llu t%x %s %s -> %d (%llu us)", now / 1000000ULL, now / 1000ULL % 1000ULL,
		(unsigned)sceKernelGetThreadId(), call, path ? path : "", result, now - started);
	vita_host_udp_line(line);
}

static void file_trace_begin(const char *call, const char *path)
{
	char line[400];
	unsigned long long now = sceKernelGetProcessTimeWide();

	if (udp_trace < 0)
		udp_trace = getenv("HALO_UDP_LOG") != NULL;
	if (!udp_trace)
		return;
	snprintf(line, sizeof(line), "%llu.%03llu t%x %s %s ...", now / 1000000ULL, now / 1000ULL % 1000ULL,
		(unsigned)sceKernelGetThreadId(), call, path ? path : "");
	vita_host_udp_line(line);
}

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
FILE *__real_fopen(const char *path, const char *mode);
FILE *__wrap_fopen(const char *path, const char *mode)
{
	unsigned long long started = sceKernelGetProcessTimeWide();
	FILE *file;

	file_trace_begin("fopen", path);
	file = __real_fopen(path, mode);
	file_trace("fopen", path, file != NULL, started);
	return file;
}

int __real_open(const char *path, int flags, int mode);
int __wrap_open(const char *path, int flags, int mode)
{
	unsigned long long started = sceKernelGetProcessTimeWide();
	int result;

	file_trace_begin("open", path);
	result = __real_open(path, flags, mode);
	file_trace("open", path, result, started);
	return result;
}

DIR *__real_opendir(const char *path);
DIR *__wrap_opendir(const char *path)
{
	unsigned long long started = sceKernelGetProcessTimeWide();
	DIR *result;

	file_trace_begin("opendir", path);
	result = __real_opendir(path);
	file_trace("opendir", path, result != NULL, started);
	return result;
}

int __real_stat(const char *path, struct stat *information);
int __wrap_stat(const char *path, struct stat *information)
{
	unsigned long long started = sceKernelGetProcessTimeWide();
	int result = __real_stat(path, information);

	file_trace("stat", path, result, started);
	return result;
}

/* exit and abort, wrapped (port/vita/build.py -Wl,--wrap): who ended the
process, in the log, before newlib's own end */
void __real_exit(int status) __attribute__((noreturn));
void __real_abort(void) __attribute__((noreturn));

void __wrap_exit(int status)
{
	char message[128];

	snprintf(message, sizeof(message), "vita: exit(%d) called from %p", status, __builtin_return_address(0));
	event_line(message);
	vita_host_log(message);
	__real_exit(status);
}

void __real__exit(int status) __attribute__((noreturn));

void __wrap__exit(int status)
{
	char message[128];

	snprintf(message, sizeof(message), "vita: _exit(%d) called from %p", status, __builtin_return_address(0));
	event_line(message);
	vita_host_log(message);
	log_drain();
	__real__exit(status);
}

/* (debug) the process ended by the kernel's call: who called it */
int __real_sceKernelExitProcess(int status);
int __wrap_sceKernelExitProcess(int status)
{
	char message[128];

	snprintf(message, sizeof(message), "vita: sceKernelExitProcess(%d) called from %p", status,
		__builtin_return_address(0));
	vita_host_udp_line(message);
	event_line(message);
	return __real_sceKernelExitProcess(status);
}

/* no processes of its own on the Vita: a fork is refused (the game's Steam
bridge and updater run helpers on the desktop) */
int __wrap_fork(void)
{
	char message[128];

	snprintf(message, sizeof(message), "vita: fork() refused, called from %p", __builtin_return_address(0));
	vita_host_log(message);
	return -1;
}

void __wrap_abort(void)
{
	char message[128];

	snprintf(message, sizeof(message), "vita: abort() called from %p: crashing for a dump", __builtin_return_address(0));
	event_line(message);
	vita_host_log(message);
	log_drain();
	*(volatile int *)0 = 0;
	__real_abort();
}

/* ---------- start-up */

/* env.txt: NAME=value lines set in the environment before the game reads
it (a test's switches); # starts a comment */
static void read_environment_file(void)
{
	FILE *file = fopen(VITA_DATA_DIRECTORY "/env.txt", "r");
	char line[512];

	if (!file)
		return;
	while (fgets(line, sizeof(line), file))
	{
		char *equals = strchr(line, '=');
		char *end = line + strlen(line);

		while (end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' '))
			*--end = 0;
		if (line[0] == '#' || !equals || equals == line)
			continue;
		*equals = 0;
		setenv(line, equals + 1, 1);
		{
			char message[600];

			snprintf(message, sizeof(message), "vita: env.txt: %s=%s", line, equals + 1);
			vita_host_log(message);
		}
	}
	fclose(file);
}

/* the game's heap in the Xbox window, carved so far (xbox_heap.c) */
size_t xbox_heap_used(void);

void vita_host_udp_line(const char *text);

/* the presents so far (d3d8_gxm.c sets it) */
volatile unsigned long vita_presented_frames;

/* 8 s without a present after the first few is logged; HALO_HANG_CRASH=1
makes it a deliberate crash, for a dump of every thread (the crash comes
first: a log write could block on whatever the hung thread holds) */
static void watchdog_thread(void *unused)
{
	unsigned long last = 0, same = 0;
	int crash = getenv("HALO_HANG_CRASH") && atoi(getenv("HALO_HANG_CRASH"));

	(void)unused;
	for (;;)
	{
		unsigned long now;

		sceKernelDelayThread(1000000);
		now = vita_presented_frames;
		if (now < 10)
			continue;
		same = now == last ? same + 1 : 0;
		last = now;
		if (same >= 8)
		{
			if (crash)
				*(volatile int *)0 = 0;
			vita_host_log("watchdog: 8 s without a present; HALO_HANG_CRASH=1 would crash for a dump");
			same = 0;
		}
	}
}

/* (debug) HALO_CRASH_AT=<seconds>: a deliberate crash then, for a dump of
every thread (what a quiet process is doing) */
static void tripwire_thread(void *argument)
{
	sceKernelDelayThread((SceUInt)(unsigned long)argument * 1000000U);
	*(volatile int *)0 = (int)vita_presented_frames;
}

/* (debug) every thread made, for the thread sampler (HALO_THREAD_SAMPLE):
sceKernelCreateThread wrapped (port/vita/build.py), pthread's too */
#define TRACKED_THREADS 96
static SceUID tracked_threads[TRACKED_THREADS];
static volatile int tracked_count;

SceUID __real_sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int priority, SceSize stack,
	SceUInt attributes, int affinity, const SceKernelThreadOptParam *options);
SceUID __wrap_sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int priority, SceSize stack,
	SceUInt attributes, int affinity, const SceKernelThreadOptParam *options)
{
	SceUID thread = __real_sceKernelCreateThread(name, entry, priority, stack, attributes, affinity, options);
	int index;

	if (thread >= 0 && (index = __atomic_fetch_add(&tracked_count, 1, __ATOMIC_RELAXED)) < TRACKED_THREADS)
		tracked_threads[index] = thread;
	return thread;
}

/* each thread's state, run time and priority, as UDP lines (HALO_UDP_LOG) */
static void thread_sample(void)
{
	char line[1400];
	int index, used = 0, count = tracked_count < TRACKED_THREADS ? tracked_count : TRACKED_THREADS;
	unsigned long long now = sceKernelGetProcessTimeWide();

	used = snprintf(line, sizeof(line), "%llu.%03llu threads:", now / 1000000ULL, now / 1000ULL % 1000ULL);
	for (index = 0; index < count && used < (int)sizeof(line) - 64; index++)
	{
		SceKernelThreadInfo info;

		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		if (sceKernelGetThreadInfo(tracked_threads[index], &info) < 0)
			continue;
		used += snprintf(line + used, sizeof(line) - used, " [%.12s s%x p%d w%x r%llu]", info.name,
			(unsigned)info.status, info.currentPriority, (unsigned)info.waitType,
			(unsigned long long)info.runClocks / 1000ULL);
	}
	vita_host_udp_line(line);
}

/* (debug: a --vita-frame-split build's HALO_FRAME_TIMING lines) every
thread's run time a frame since the last report, the busiest first */
void vita_host_threads_report(unsigned long frames)
{
	static unsigned long long previous[TRACKED_THREADS];
	struct thread_run { char name[16]; unsigned long long run; } busy[TRACKED_THREADS];
	char line[1400];
	int index, count = tracked_count < TRACKED_THREADS ? tracked_count : TRACKED_THREADS, found = 0, used, i, j;

	if (!frames)
		return;
	for (index = 0; index < count; index++)
	{
		SceKernelThreadInfo info;
		unsigned long long run;

		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		if (sceKernelGetThreadInfo(tracked_threads[index], &info) < 0)
			continue;
		run = info.runClocks >= previous[index] ? info.runClocks - previous[index] : 0;
		previous[index] = info.runClocks;
		snprintf(busy[found].name, sizeof(busy[found].name), "%s", info.name);
		busy[found++].run = run;
	}
	for (i = 1; i < found; i++)
		for (j = i; j > 0 && busy[j].run > busy[j - 1].run; j--)
		{
			struct thread_run swap = busy[j];

			busy[j] = busy[j - 1];
			busy[j - 1] = swap;
		}
	used = snprintf(line, sizeof(line), "threads (ms/frame, %lu frames):", frames);
	for (i = 0; i < found && used < (int)sizeof(line) - 40; i++)
	{
		if ((double)busy[i].run / 1000.0 / (double)frames < 0.05)
			break;
		used += snprintf(line + used, sizeof(line) - used, " %s %.2f%s", busy[i].name,
			(double)busy[i].run / 1000.0 / (double)frames, i + 1 < found ? "," : "");
	}
	vita_host_log(line);
}

static void sampler_thread(void *unused)
{
	(void)unused;
	for (;;)
	{
		sceKernelDelayThread(500000);
		thread_sample();
	}
}

/* The system's auto-suspend held off, once a second, for as long as the game
runs: an application that does not tick is suspended with the screen after
the system's standby time (on the dev Vita 14 s after it started, a map's
load included, Nov 19). Halo CE Vita got this from SDL's gamepad subsystem;
Warthog starts SDL for its sound only. */
static void awake_thread(void *unused)
{
	(void)unused;
	for (;;)
	{
		sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
		sceKernelDelayThread(1000000);
	}
}

static void heartbeat_thread(void *unused)
{
	(void)unused;
	if (getenv("HALO_CRASH_AT") && atoi(getenv("HALO_CRASH_AT")) > 0)
		vita_host_thread_start_priority("tripwire", tripwire_thread,
			(void *)(unsigned long)atoi(getenv("HALO_CRASH_AT")), -1, 64);
	for (;;)
	{
		char message[96];

		SceKernelFreeMemorySizeInfo info;
		static int period = -1;

		if (period < 0)
			period = getenv("HALO_HEARTBEAT_S") ? atoi(getenv("HALO_HEARTBEAT_S")) : 10;
		sceKernelDelayThread((SceUInt)(period > 0 ? period : 10) * 1000000U);
		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		sceKernelGetFreeMemorySize(&info);
		snprintf(message, sizeof(message), "heartbeat: %lu frames presented; free user %d KB, cdram %d KB; "
			"the game's heap %u KB", vita_presented_frames, info.size_user / 1024, info.size_cdram / 1024,
			(unsigned int)(xbox_heap_used() >> 10));
		vita_host_udp_line(message);
		if (getenv("HALO_EVENTS"))
			event_line(message);
		vita_host_log(message);
	}
}

static void *game_pthread(void *unused)
{
	static char *arguments[] = { "halo", NULL };

	(void)unused;
	vita_host_thread_start_stack("awake", awake_thread, NULL, -1, 64, 16 * 1024);
	if (vita_host_thread_start_priority("watchdog", watchdog_thread, NULL, -1, 64) < 0)
		vita_host_log("watchdog: could not start");
	vita_host_thread_start_priority("heartbeat", heartbeat_thread, NULL, -1, 96);
	if (getenv("HALO_THREAD_SAMPLE"))
		vita_host_thread_start_priority("sampler", sampler_thread, NULL, -1, 64);
	/* (debug) HALO_NO_GAME=1: the host alone, the game never started */
	if (getenv("HALO_NO_GAME") && atoi(getenv("HALO_NO_GAME")))
	{
		/* (HALO_NO_GAME=2: and the Xbox window given back) */
		if (atoi(getenv("HALO_NO_GAME")) == 2)
		{
			void vita_host_window_free(void);

			vita_host_window_free();
			vita_host_log_memory("with the window given back");
		}
		for (;;)
			sceKernelDelayThread(1000000);
	}
	return (void *)(intptr_t)halo_main(1, arguments);
}

/* stderr (the platform layer's log before its own is open) to stderr.txt,
before the platform layer's constructors (101 and up: the Xbox window's
reservation logs from there) */
void vita_host_stderr_open(void)
{
	static int opened;

	if (opened)
		return;
	opened = 1;
	sceIoMkdir(VITA_DATA_DIRECTORY, 0777);
	sceIoMkdir(VITA_DATA_DIRECTORY "/logs", 0777);
	if (freopen(VITA_DATA_DIRECTORY "/stderr.txt", "w", stderr))
		setvbuf(stderr, NULL, _IONBF, 0);
	/* (the platform layer's log, debug.txt in the logs folder, from its
	first line: the Xbox window's, which a constructor logs before main) */
	setenv("HALO_LOG_ROOT", VITA_DATA_DIRECTORY "/logs", 0);
}

/* (the link does not sort the constructors by priority: the Xbox window's
reservation opens it too, vita_memory.c) */
__attribute__((constructor(100)))
static void stderr_early(void)
{
	vita_host_stderr_open();
}

int main(int argc, char **argv)
{
	SceAppUtilInitParam init;
	SceAppUtilBootParam boot;
	char message[256];

	(void)argc;
	(void)argv;
	sceIoMkdir(VITA_DATA_DIRECTORY, 0777);
	sceIoMkdir(VITA_DATA_DIRECTORY "/saves", 0777);
	sceIoMkdir(VITA_DATA_DIRECTORY "/logs", 0777);
	/* the last run's log is kept (a crash leaves no other trace) */
	sceIoRemove(VITA_DATA_DIRECTORY "/halo-prev.log");
	sceIoRemove(VITA_DATA_DIRECTORY "/events.txt");
	sceIoRename(VITA_DATA_DIRECTORY "/halo.log", VITA_DATA_DIRECTORY "/halo-prev.log");
	atexit(log_flush_at_exit);
	snprintf(message, sizeof(message), "vita: Warthog for Vita %s (game %s) starting, realtime %ld", HALO_VITA_VERSION, HALO_VERSION,
		(long)time(NULL));
	vita_host_log(message);
	vita_host_log(vita_host_window_report());
	{
		void *probe = malloc(16);

		snprintf(message, sizeof(message), "vita: C heap %u MB, a block of it at %p", _newlib_heap_size_user >> 20, probe);
		vita_host_log(message);
		free(probe);
	}
	vita_host_log_memory("at start");

	memset(&init, 0, sizeof(init));
	memset(&boot, 0, sizeof(boot));
	sceAppUtilInit(&init, &boot);
	/* the clocks the game needs as floors, raised to (vita_clocks.c) */
	vita_host_clocks_start();

	setenv("HALO_DATA_ROOT", VITA_DATA_DIRECTORY, 0);
	setenv("HALO_SAVE_ROOT", VITA_DATA_DIRECTORY "/saves", 0);
	setenv("HALO_LOG_ROOT", VITA_DATA_DIRECTORY "/logs", 0);
	/* the Vita's 16:9 screen: 480 lines of 848 columns */
	setenv("HALO_DISPLAY_WIDTH", "848", 0);
	/* the game's own fonts and HUD, not the high-resolution ones: the Vita's
	renderer does not draw those yet (HALO_CONSOLE_HOST_ART's text atlas and
	HUD sheets, port/vita/README.md) */
	setenv("HALO_HIGH_RES_TEXT", "0", 0);
	setenv("HALO_HIGH_RES_HUD", "0", 0);
	read_environment_file();
	/* (debug) the folder a test's screenshots go to (debug.screenshot_directory) */
	if (getenv("HALO_SCREENSHOT_DIR") && *getenv("HALO_SCREENSHOT_DIR"))
		sceIoMkdir(getenv("HALO_SCREENSHOT_DIR"), 0777);
	{
		/* the maps: maps/ in the data folder; else the release's (for a dev
		build), else Halo CE Vita's, read where they are (HALO_MAPS_ROOT, the
		port's xbox_files.c override): its 1.1.x folder or 1.2.0's */
		static const char *const others[] = { "ux0:data/warthog-vita/maps", "ux0:data/haloce-vita/maps",
			"ux0:data/project-ark/maps" };
		SceIoStat stat;
		unsigned int index;

		if (!getenv("HALO_MAPS_ROOT") && sceIoGetstat(VITA_DATA_DIRECTORY "/maps/ui.map", &stat) < 0)
		{
			for (index = 0; index < sizeof(others) / sizeof(*others); index++)
			{
				char path[128];

				snprintf(path, sizeof(path), "%s/ui.map", others[index]);
				if (sceIoGetstat(path, &stat) >= 0)
				{
					setenv("HALO_MAPS_ROOT", others[index], 1);
					break;
				}
			}
		}
		snprintf(message, sizeof(message), "vita: maps from %s",
			getenv("HALO_MAPS_ROOT") ? getenv("HALO_MAPS_ROOT") : VITA_DATA_DIRECTORY "/maps");
		vita_host_log(message);
	}
	snprintf(message, sizeof(message), "vita: arm %d MHz, bus %d MHz, gpu %d MHz, xbar %d MHz",
		scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency(),
		scePowerGetGpuXbarClockFrequency());
	vita_host_log(message);
	{
		/* (a pthread: the game's waits are pthread condition variables,
		which a thread the kernel made without pthread's own data does not
		take - halo_cond.h) */
		pthread_t thread;
		pthread_attr_t attributes;
		void *result = NULL;
		int status = 0;

		pthread_attr_init(&attributes);
		pthread_attr_setstacksize(&attributes, GAME_THREAD_STACK_SIZE);
		if (pthread_create(&thread, &attributes, game_pthread, NULL) != 0)
		{
			static char *arguments[] = { "halo", NULL };

			vita_host_log("vita: cannot create the game thread");
			status = halo_main(1, arguments);
		}
		else
		{
			pthread_join(thread, &result);
			status = (int)(intptr_t)result;
		}
		snprintf(message, sizeof(message), "vita: the game ended (%d)", status);
		vita_host_log(message);
		log_drain();
		sceKernelExitProcess(status);
	}
	return 0;
}
