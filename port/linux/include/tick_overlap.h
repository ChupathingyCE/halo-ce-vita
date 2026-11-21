/*
TICK_OVERLAP.H

A console's game tick beside its render (HALO_CONSOLE_TICK_OVERLAP,
halo_console_features.h; port/linux/game/tick_overlap.c).

Without it a frame runs the game time update (its ticks and the frame's
effects) and then draws what the update left. With it, main_loop starts the
update on a thread of its own just before it draws and waits for it after
the frame is presented: the render draws the state the previous update left
while the next update runs, and a frame takes about the longer of the two
instead of both. Nothing of the game state is copied. The render reads it
as the tick changes it, and the tick's reclamation is made lazy so that
what the render reaches stays whole until the join (the render epoch):

  - a datum the tick deletes is freed as without the thread (its slot is
    the next datum's), and a copy of it is kept until the join, which the
    render's lookups of its old index find (it is drawn a frame late at
    worst);
  - a datum the tick creates is hidden from the render's walks until it is
    complete (a constructor's scope, or a reference list's link);
  - a memory pool block the tick frees stays allocated until the join, and
    a compaction the tick asks for waits for it;
  - a reference list insert publishes its node with a release fence.

Stale links then always lead to whole nodes: an unlinked node keeps its
next link and no slot is reused, so a walk that mixes old and new links
ends. The render draws the objects' poses and the clusters' object lists as
the last update left them, copied by the tick thread when it is done (the
render's own copies of the few things a tick changes in place). The tick
computes exactly what it computes without the thread: only when the render
reads the state changes.

The tick thread is told by its stack: a frame address inside it. Nothing
here uses thread-local storage (a console may have none). Every name below
exists only in a build with HALO_CONSOLE_TICK_OVERLAP.
*/

#ifndef __TICK_OVERLAP_H
#define __TICK_OVERLAP_H

#ifdef HALO_CONSOLE_TICK_OVERLAP

struct data_array;
struct memory_pool;

/* ---------- the console's part (port/linux/src/tick_overlap_thread.c) */

/* what a console gives, before the game starts (any member may be NULL): a
wake (a semaphore) made, waited on (1 woken, 0 timed out, below 0 an error)
and signalled, for the two threads' waits, where its condition variables
can miss a wake-up; the tick thread placed (its core) and named, as it
starts; the clock in microseconds. Without them the waits are a condition
variable's and the clock CLOCK_MONOTONIC */
struct console_tick_overlap_port
{
	int (*wake_create)(const char *name);
	int (*wake_wait)(int id, unsigned long timeout_us);
	void (*wake_signal)(int id);
	void (*thread_started)(void);
	unsigned long long (*time_us)(void);
};
void console_tick_overlap_port_set(const struct console_tick_overlap_port *given);

/* ---------- the thread */

/* the setting (HALO_TICK_OVERLAP=0 in the environment turns it off, a test);
the first call makes the thread */
int halo_tick_overlap_enabled(void);
/* main_loop: should this frame's update run beside its render? */
int halo_tick_overlap_wanted(void);
/* main_loop: the update starts on the tick thread (game_time_update(delta),
then the frame's sounds and the copies the render draws next) */
void halo_tick_overlap_start(float delta);
/* main_loop, after the present: waits for the update, ends the epoch and
publishes what the render draws next */
void halo_tick_overlap_join(void);
/* the main thread, while an update runs: a call into the game state made at
the join instead (0: none runs, call it now) */
int halo_tick_overlap_defer(void (*call)(void));
/* the tick, before it changes what the render cannot have changed under it
(a structure bsp switch): waits until the frame is presented */
void halo_tick_overlap_wait_for_render(void);
/* the update's run time (microseconds) */
unsigned long long halo_tick_overlap_last_us(void);

/* ---------- the epoch */

/* nonzero while an update runs beside the render */
extern volatile int halo_epoch_active;
/* nonzero once the tick has its thread */
extern int halo_epoch_threaded;
/* the tick thread's stack */
extern __UINTPTR_TYPE__ halo_tick_stack_low, halo_tick_stack_high;

/* the caller's stack pointer: read from the register, so that a function
asking (every inline lookup) needs no frame pointer of its own */
static __inline__ __UINTPTR_TYPE__ halo_epoch_stack_pointer(void)
{
	__UINTPTR_TYPE__ here;

#if defined(__arm__) || defined(__aarch64__)
	__asm__("mov %0, sp" : "=r"(here));
#elif defined(__x86_64__)
	__asm__("movq %%rsp, %0" : "=r"(here));
#elif defined(__i386__)
	__asm__("movl %%esp, %0" : "=r"(here));
#else
	here = (__UINTPTR_TYPE__)__builtin_frame_address(0);
#endif
	return here;
}

/* is the caller the tick thread? */
static __inline__ int halo_epoch_on_mutator(void)
{
	__UINTPTR_TYPE__ here = halo_epoch_stack_pointer();

	return here >= halo_tick_stack_low && here < halo_tick_stack_high;
}

/* is the caller the render, while an update runs? */
static __inline__ int halo_epoch_render_overlapped(void)
{
	return halo_epoch_active && !halo_epoch_on_mutator();
}

/* the game state was replaced (a revert, a saved game, a new map): the marks
and the deferred frees refer to what is gone, and are dropped */
void halo_epoch_discard(void);
/* changes whenever the game state is replaced */
extern unsigned long halo_epoch_generation;

/* the tick deletes a datum during an epoch: tombstoned (1), or 0 when the
delete proceeds as usual */
int halo_epoch_datum_delete(struct data_array *data, long absolute_index);
/* the tick made a datum during an epoch: hidden from the render until it
is complete */
void halo_epoch_datum_created(struct data_array *data, long absolute_index);
void halo_epoch_datum_ready(struct data_array *data, long absolute_index);
/* a constructor's scope: everything the tick made in it is complete at once */
unsigned long halo_epoch_scope_begin(void);
void halo_epoch_scope_ready(unsigned long scope);

enum
{
	_halo_epoch_datum_live,
	_halo_epoch_datum_tombstoned,
	_halo_epoch_datum_created
};
int halo_epoch_datum_state(const struct data_array *data, long absolute_index);
/* the walks' test (data_iterator_next, data_next_index): a tombstone is
hidden from the tick, a datum the tick is making from the render */
int halo_epoch_datum_hidden_from_caller(const struct data_array *data, long absolute_index);
/* (the walks' test inline: an array the tick has not marked this epoch, or
the tick itself, sees every datum) */
#define halo_epoch_datum_visible(data, absolute_index) \
	(!((char const *)(data))[TAG_STRING_LENGTH] || halo_epoch_on_mutator() || \
		!halo_epoch_datum_hidden_from_caller((data), (absolute_index)))
/* the lookups' test (datum_get, datum_try_and_get): a tombstone is hidden
from the tick; a datum under construction from the render only in the
arrays whose constructors are long (objects, lights: their links are
published before they are complete), any other new datum being linked to
only once it is filled in */
void halo_epoch_guard_constructions(struct data_array *data);
/* (the render, while an update runs, makes or deletes a datum in the game
state: outside the epoch's protocol; named in the log the first times) */
void halo_epoch_check_reader_mutation(const struct data_array *data, long absolute_index, const char *what);

/* the marks, read by data.h's inline lookups: an array with marks has its
table entry's index plus one in its two padding bytes (38 and 39) and a
nonzero last byte of its name, and a byte per slot, 0 for a live datum */
#define HALO_EPOCH_ARRAY_TABLE_SIZE 256
struct halo_epoch_marked_array
{
	struct data_array *data;
	unsigned char *marks;
	unsigned long marked;
	long maximum_count;
};
extern struct halo_epoch_marked_array halo_epoch_marked_arrays[HALO_EPOCH_ARRAY_TABLE_SIZE];

/* data.h's inline lookup, for an array with marks (its name's last byte
set): the tick's own view, or a slot the tick has not touched this epoch */
static __inline__ int halo_epoch_slot_unmarked(const void *data, long absolute_index)
{
	unsigned long hint = *(unsigned short const *)((unsigned char const *)data + 38);
	struct halo_epoch_marked_array const *entry;

	if (halo_epoch_on_mutator())
		return 1;
	if (!hint || hint > HALO_EPOCH_ARRAY_TABLE_SIZE)
		return 0;
	entry = &halo_epoch_marked_arrays[hint - 1];
	return entry->data == (struct data_array const *)data && entry->marks && absolute_index >= 0 &&
		absolute_index < entry->maximum_count && !entry->marks[absolute_index];
}

/* the tick frees a memory pool block during an epoch: kept until the join
(1), or 0 when the free proceeds as usual */
int halo_epoch_pool_free(struct memory_pool *pool, void *block);
/* the tick compacts a memory pool during an epoch: done at the join (1) */
int halo_epoch_compaction_defer(struct memory_pool *pool);
/* memory_pool.c: a block released by its header (the join's deferred frees) */
void memory_pool_block_release(struct memory_pool *pool, void *block);

/* a failed assertion (cseries.h): the render's while an update runs is
logged once a site and skipped (a value it read may be torn for a frame);
every other stops the game as before */
void halo_assert_failed(char *information, char *file, long line);

/* ---------- what the render draws from (tick_overlap.c) */

/* objects.c object_get_node_matrices: the object's nodes as the last update
left them, for the render while the next runs (real_matrix4x3s); NULL to
read them live */
void *halo_tick_overlap_node_matrices(long object_index);
/* the clusters' object and light lists as the last update left them: the
render's walk of a cluster (render_objects.c, object_lights.c) */
enum
{
	_tick_overlap_list_collideable,
	_tick_overlap_list_noncollideable,
	_tick_overlap_list_light,
	NUMBER_OF_TICK_OVERLAP_LISTS
};
int halo_tick_overlap_lists_active(int which);
long halo_tick_overlap_list_first(int which, long *iterator, short cluster_index);
long halo_tick_overlap_list_next(int which, long *iterator);
/* game_time.c: the finished update's elapsed ticks and tick fraction, which
the render counts down by and blends with */
extern volatile short halo_tick_overlap_elapsed;
extern volatile float halo_tick_overlap_fraction;
/* ... and its game time (the render's clock while the next update runs) */
extern volatile long halo_tick_overlap_game_time;
/* data.c: what the render finds for index while an update runs, given what
the usual test found (the datum, or NULL): a datum the tick deleted is found
whole (the copy kept aside at its delete), one it is making is not */
void *halo_epoch_render_lookup(const struct data_array *data, long index, void *found);
/* memory_pool.c: a block kept until the join; an allocation that fits only
where kept blocks are waits for the frame to be presented first */
int halo_epoch_pool_block_deferred(const void *block);
void halo_epoch_pool_needs_room(void);

#endif

#endif
