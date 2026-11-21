/*
TICK_OVERLAP_THREAD.C

The tick thread's thread and waits, for a console's tick beside its render
(HALO_CONSOLE_TICK_OVERLAP; port/linux/game/tick_overlap.c is the game's
side). Two threads hand the update back and forth: the game's thread starts
it and later joins it, the tick thread waits for work and, in a structure
bsp switch, for the frame to be presented. Each waits on a wake of its own,
published to by the other alone.

A wait spins a little for a near miss, then sleeps: a sleeper sets its flag,
tests once more and sleeps; a publish (sequentially consistent) then looks
at the other side's flag and wakes it when it sleeps, so either the test
sees the publish or the publisher sees the sleeper. A wake nobody waited
for is a spurious one later; every wait tests again in a loop, and sleeps
at most 100 ms at a time.

A console whose condition variables can miss a wake-up (the Vita's: its
pthread_cond_timedwait slept through broadcasts to its deadline) gives
kernel semaphores instead (given through console_tick_overlap_port_set); elsewhere the
waits sleep on a condition variable, each publish made under its lock.
*/

#ifdef HALO_CONSOLE_TICK_OVERLAP

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tick_overlap.h"

void platform_log(const char *format, ...);

/* a console's own (tick_overlap.h), given before the game starts; none
elsewhere */
static struct console_tick_overlap_port port;

void console_tick_overlap_port_set(const struct console_tick_overlap_port *given)
{
	port = *given;
}

enum { TICK_OVERLAP_WAITERS = 2, WAKE_TIMEOUT_US = 100000 };

static pthread_mutex_t wake_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake_condition = PTHREAD_COND_INITIALIZER;
static volatile int sleeping[TICK_OVERLAP_WAITERS];
static int wakes[TICK_OVERLAP_WAITERS] = { -1, -1 };
static int semaphores;

unsigned long long tick_overlap_now_us(void)
{
	struct timespec now;

	if (port.time_us)
		return port.time_us();
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0;
	return (unsigned long long)now.tv_sec * 1000000ULL + (unsigned long long)now.tv_nsec / 1000ULL;
}

static void (*thread_body)(void);

static void *thread_main(void *unused)
{
	(void)unused;
	if (port.thread_started)
		port.thread_started();
	thread_body();
	return NULL;
}

/* the tick thread, running body; 0 when it could not be made */
int tick_overlap_thread_create(void (*body)(void), unsigned long stack_size)
{
	pthread_attr_t attributes;
	pthread_t thread;
	int made;

	if (port.wake_create && port.wake_wait && port.wake_signal)
	{
		wakes[0] = port.wake_create("tick overlap: the tick");
		wakes[1] = port.wake_create("tick overlap: the join");
		semaphores = wakes[0] >= 0 && wakes[1] >= 0;
	}
	platform_log("tick overlap: the waits on %s", semaphores ? "the console's semaphores" : "a condition variable");
	thread_body = body;
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, stack_size);
	made = pthread_create(&thread, &attributes, thread_main, NULL) == 0;
	pthread_attr_destroy(&attributes);
	if (made)
		pthread_detach(thread);
	return made;
}

/* before the store the other side waits for */
void tick_overlap_publish_begin(void)
{
	if (!semaphores)
		pthread_mutex_lock(&wake_lock);
}

/* after it: the waiter woken if it sleeps */
void tick_overlap_publish_end(int waiter)
{
	if (semaphores)
	{
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		if (__atomic_load_n(&sleeping[waiter], __ATOMIC_SEQ_CST))
		{
			__atomic_store_n(&sleeping[waiter], 0, __ATOMIC_SEQ_CST);
			port.wake_signal(wakes[waiter]);
		}
		return;
	}
	pthread_cond_broadcast(&wake_condition);
	pthread_mutex_unlock(&wake_lock);
}

/* until ready(): spins tests, then asleep */
void tick_overlap_wait(int waiter, int (*ready)(void), unsigned long spins)
{
	unsigned long spin = 0;

	while (!ready())
	{
		if (++spin < spins)
			continue;
		if (semaphores)
		{
			int result;

			__atomic_store_n(&sleeping[waiter], 1, __ATOMIC_SEQ_CST);
			__atomic_thread_fence(__ATOMIC_SEQ_CST);
			if (ready())
			{
				__atomic_store_n(&sleeping[waiter], 0, __ATOMIC_SEQ_CST);
				break;
			}
			result = port.wake_wait(wakes[waiter], WAKE_TIMEOUT_US);
			__atomic_store_n(&sleeping[waiter], 0, __ATOMIC_SEQ_CST);
			(void)result;
			continue;
		}
		pthread_mutex_lock(&wake_lock);
		while (!ready())
		{
			struct timespec deadline;

			clock_gettime(CLOCK_REALTIME, &deadline);
			deadline.tv_nsec += WAKE_TIMEOUT_US * 1000L;
			if (deadline.tv_nsec >= 1000000000L)
			{
				deadline.tv_sec++;
				deadline.tv_nsec -= 1000000000L;
			}
			pthread_cond_timedwait(&wake_condition, &wake_lock, &deadline);
		}
		pthread_mutex_unlock(&wake_lock);
	}
}

#else

/* (an empty unit otherwise) */
typedef int tick_overlap_thread_unused;

#endif
