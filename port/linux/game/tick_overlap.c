/*
TICK_OVERLAP.C

A console's game tick beside its render (HALO_CONSOLE_TICK_OVERLAP; see
port/linux/include/tick_overlap.h). The thread and its waits are the
platform layer's (port/linux/src/tick_overlap_thread.c).

The simulation is the one main_loop runs without the thread, step for step:
the update starts from the state the frame's other updates left (the input,
the network, the camera) and nothing but the update writes what it reads.
The tick's own view of the game state is exactly the one it has without the
thread: a datum it deletes is freed at once and its slot is taken by the
next datum it makes, as ever, and the memory pools report what they report
without it. What changes is only what the render, running at the same time,
can still reach:

  - a datum the tick deletes is copied aside as it is (the graveyard) before
    the tick clears it, and the render's lookups of its old index find the
    copy until the join, whatever the tick has made in its slot since. So a
    link the render follows never leads to a datum it cannot read, and an
    old list stays whole for a walk already on it (a full graveyard is a
    sync, below);
  - a datum the tick is making is hidden from the render's walks (and, for
    the objects and the lights, its lookups) until it is complete;
  - an object memory pool block the tick frees stays where it is until the
    join, counted free as before; when the tick needs that room (an
    allocation that only fits where the kept blocks are, a compaction), it
    first waits for the frame to be presented, then frees them (a sync: the
    render no longer reads anything, and the rest of the update runs as it
    would without the thread).

The render's own copies, made by the tick thread when it is done: each
moving object's node matrices (the tick rewrites them in place) and the
clusters' object and light lists (the tick takes a moving object out of
them and puts it back every tick: a walk at that moment missed it and the
object flickered). The render's upkeep of the game state (the sound
manager's frame, the lights' and the decals' ends) runs on the tick thread
after the update, where it ran without the thread (the frame drawn next is
the render of that update).
*/

#include "cseries.h"

#ifdef HALO_CONSOLE_TICK_OVERLAP

#include "memory/data.h"
#include "memory/memory_pool.h"
#include "objects/objects.h"
#include "structures/cluster_partitions.h"
#include "structures/structures.h"
#include "structures/structure_bsp_definitions.h"
#include "scenario/scenario.h"
#include "game/game.h"
#include "sound/sound_manager.h"
#include "saved games/game_state.h"

#include <stdlib.h>
#include <string.h>
#ifdef __GLIBC__
#include <execinfo.h>
#endif

void platform_log(const char *format, ...);

/* tick_overlap_thread.c */
int tick_overlap_thread_create(void (*body)(void), unsigned long stack_size);
void tick_overlap_publish_begin(void);
void tick_overlap_publish_end(int waiter);
void tick_overlap_wait(int waiter, int (*ready)(void), unsigned long spins);
unsigned long long tick_overlap_now_us(void);

/* object_lights.c: the render's upkeep of the lights (their ends, the moved
ones' clusters); rasterizer_xbox.c: the decals' */
void lights_port_overlap_upkeep(void);
void rasterizer_decals_port_overlap_upkeep(void);
/* xbox_texture_cache.c: the bitmaps the tick asked for, asked for now */
void texture_cache_port_overlap_requests(void);
/* object_lights.c */
extern struct cluster_partition light_cluster_partition;
extern struct data_array *light_data;

#ifndef HALO_CONSOLE_NO_FORGE
boolean forge_game_active(void);
boolean forge_variant_playing(void);
#endif

/* (the C library's, not cseries.h's tracked allocations) */
#define overlap_calloc(count, size) (calloc)((count), (size))
#define overlap_realloc(block, size) (realloc)((block), (size))
#define overlap_free(block) (free)(block)

/* ---------- constants */

#ifndef HALO_CONSOLE_TICK_OVERLAP_STACK_SIZE
#define HALO_CONSOLE_TICK_OVERLAP_STACK_SIZE (8UL << 20)
#endif

enum
{
	/* the two waiters (tick_overlap_thread.c) */
	_waiter_tick,
	_waiter_game,

	/* a slot's marks */
	_mark_tombstone = 1, /* deleted this epoch: the old datum is in the graveyard */
	_mark_created = 2, /* made this epoch, not complete yet: hidden from the render */

	MARK_LOG_COUNT = 16384,
	CREATED_LOG_COUNT = 8192,
	GRAVEYARD_SIZE = 256 * 1024,
	GRAVEYARD_ENTRIES = 4096,
	DEFERRED_FREE_COUNT = 4096,
	DEFERRED_CALL_COUNT = 16,
};

/* a memory pool block whose free waits for the join (memory_pool.c tests
it: halo_epoch_pool_block_deferred) */
#define DEFERRED_BLOCK_SIGNATURE 'dfrd'

/* ---------- globals */

volatile int halo_epoch_active;
int halo_epoch_threaded;
__UINTPTR_TYPE__ halo_tick_stack_low, halo_tick_stack_high;
unsigned long halo_epoch_generation = 1;
volatile short halo_tick_overlap_elapsed;
volatile float halo_tick_overlap_fraction = 1.0f;
volatile long halo_tick_overlap_game_time;
struct halo_epoch_marked_array halo_epoch_marked_arrays[HALO_EPOCH_ARRAY_TABLE_SIZE];

static struct
{
	int enabled; /* -1 until asked */
	/* (the tick thread's work, counted: started != finished while it runs) */
	volatile unsigned long started, finished;
	volatile float delta;
	/* the main thread is in the join: the frame is presented */
	volatile int joining;
	/* a sync came this epoch: the render is done, the rest runs as without the
	thread */
	int closed;
	unsigned long long update_us, last_report_us, capture_us, capture_total_us;
	unsigned long epochs, syncs, syncs_logged;
	unsigned long long wait_us, update_total_us, join_us;
	unsigned long frames;
	/* (the 300-frame line's counts) the datums the tick deleted and made,
	the bytes kept aside, the blocks whose frees waited */
	unsigned long tombstones, creations, graveyard_bytes, deferred_blocks;
	/* the finished update's elapsed ticks, tick fraction and game time,
	published to the render at the join */
	short elapsed;
	float fraction;
	long game_time;

	void (*deferred_calls[DEFERRED_CALL_COUNT])(void);
	unsigned long deferred_call_count;
} overlap = { -1 };

/* the marks' bookkeeping (the arrays' table is halo_epoch_marked_arrays) */
#define MARK_FLAG(data) ((data)->name[TAG_STRING_LENGTH])
#define MARK_SLOT(data) (*(unsigned short *)((unsigned char *)(data) + 38))
typedef char data_array_padding_assert[offsetof(struct data_array, signature) == 40 ? 1 : -1];
typedef char data_array_name_assert[sizeof(((struct data_array *)0)->name) == TAG_STRING_LENGTH + 1 ? 1 : -1];

static unsigned long array_count;
static const struct data_array *guarded_arrays[4];
static unsigned long guarded_array_count;

static struct { struct halo_epoch_marked_array *entry; long absolute_index; } mark_log[MARK_LOG_COUNT];
static unsigned long mark_log_count, mark_log_overflowed;
static struct { struct halo_epoch_marked_array *entry; long absolute_index; } created_log[CREATED_LOG_COUNT];
static unsigned long created_log_count;

/* the datums the tick deleted this epoch, as they were */
static struct
{
	const struct data_array *data;
	long absolute_index;
	short identifier;
	long offset;
} graveyard_entries[GRAVEYARD_ENTRIES];
static unsigned long graveyard_entry_count, graveyard_used, graveyard_fulls;
static unsigned char graveyard[GRAVEYARD_SIZE];

static struct { struct memory_pool *pool; struct memory_pool_block *block; } deferred_frees[DEFERRED_FREE_COUNT];
static unsigned long deferred_free_count;

/* ---------- the marks */

static struct halo_epoch_marked_array *array_find(const struct data_array *data, int create)
{
	unsigned long slot = ((unsigned long)(__SIZE_TYPE__)data >> 4) % HALO_EPOCH_ARRAY_TABLE_SIZE;
	unsigned long hint = MARK_SLOT(data), probes;

	if (hint && hint <= HALO_EPOCH_ARRAY_TABLE_SIZE && halo_epoch_marked_arrays[hint - 1].data == data)
		return &halo_epoch_marked_arrays[hint - 1];
	for (probes = 0; probes < HALO_EPOCH_ARRAY_TABLE_SIZE; probes++, slot = (slot + 1) % HALO_EPOCH_ARRAY_TABLE_SIZE)
	{
		struct halo_epoch_marked_array *entry = &halo_epoch_marked_arrays[slot];

		if (entry->data == data)
		{
			/* (an array made again at the same address, larger: none of its
			slots past the old size marked; halo_epoch_discard empties the
			table at each map) */
			if (entry->maximum_count < data->maximum_count)
				return NULL;
			MARK_SLOT((struct data_array *)data) = (unsigned short)(slot + 1);
			return entry;
		}
		if (!entry->data)
		{
			long count = data->maximum_count > 0 ? data->maximum_count : 1;

			if (!create || array_count + 1 >= HALO_EPOCH_ARRAY_TABLE_SIZE)
				return NULL;
			/* (the render may look the array up while the tick adds it: the
			marks exist before the entry is visible) */
			entry->marks = overlap_calloc((size_t)count, 1);
			if (!entry->marks)
				return NULL;
			entry->marked = 0;
			entry->maximum_count = count;
			__atomic_store_n(&entry->data, (struct data_array *)data, __ATOMIC_RELEASE);
			MARK_SLOT((struct data_array *)data) = (unsigned short)(slot + 1);
			array_count++;
			return entry;
		}
	}
	return NULL;
}

static __inline__ struct halo_epoch_marked_array *array_of(const struct data_array *data)
{
	unsigned long hint = MARK_SLOT(data);

	if (hint && hint <= HALO_EPOCH_ARRAY_TABLE_SIZE && halo_epoch_marked_arrays[hint - 1].data == data)
		return &halo_epoch_marked_arrays[hint - 1];
	return array_find(data, 0);
}

static __inline__ void *datum_address(const struct data_array *data, long absolute_index)
{
	return (byte *)xbox_pointer(data->data) + data->size * absolute_index;
}

/* the tick, during an open epoch: a mark set on a slot */
static struct halo_epoch_marked_array *mark(struct data_array *data, long absolute_index, unsigned char bits)
{
	struct halo_epoch_marked_array *entry = array_find(data, 1);

	if (!entry || absolute_index < 0 || absolute_index >= data->maximum_count)
		return NULL;
	if (!entry->marks[absolute_index])
	{
		entry->marked++;
		if (mark_log_count < MARK_LOG_COUNT)
		{
			mark_log[mark_log_count].entry = entry;
			mark_log[mark_log_count].absolute_index = absolute_index;
			mark_log_count++;
		}
		else
			mark_log_overflowed = 1;
	}
	entry->marks[absolute_index] |= bits;
	MARK_FLAG(data) = 1;
	return entry;
}

static __inline__ int epoch_open_on_mutator(void)
{
	return halo_epoch_active && !overlap.closed && halo_epoch_on_mutator();
}

void halo_epoch_guard_constructions(struct data_array *data)
{
	unsigned long index;

	if (!data)
		return;
	for (index = 0; index < guarded_array_count; index++)
		if (guarded_arrays[index] == data)
			return;
	if (guarded_array_count < sizeof(guarded_arrays) / sizeof(guarded_arrays[0]))
		guarded_arrays[guarded_array_count++] = data;
}

static int guarded(const struct data_array *data)
{
	unsigned long index;

	for (index = 0; index < guarded_array_count; index++)
		if (guarded_arrays[index] == data)
			return TRUE;
	return FALSE;
}

/* datum_delete, the tick, before the identifier is cleared: the datum as it
is, copied aside for the render's lookups (the tick goes on to clear it, as
it does without the thread: an object's header loses its block) */
int halo_epoch_datum_delete(struct data_array *data, long absolute_index)
{
	long size = data->size;

	if (!epoch_open_on_mutator() || absolute_index < 0 || absolute_index >= data->maximum_count)
		return 0;
	if (graveyard_entry_count >= GRAVEYARD_ENTRIES || size <= 0 ||
		graveyard_used + (unsigned long)size > GRAVEYARD_SIZE)
	{
		/* (no room for the copy: once the render is done, none is needed) */
		if (graveyard_fulls++ < 4)
			platform_log("tick overlap: the graveyard is full (%lu datums, %lu bytes) at %.32s: a sync",
				graveyard_entry_count, graveyard_used, data->name);
		halo_tick_overlap_wait_for_render();
		return 0;
	}
	if (!mark(data, absolute_index, _mark_tombstone))
		return 0;
	csmemcpy(graveyard + graveyard_used, datum_address(data, absolute_index), size);
	graveyard_entries[graveyard_entry_count].data = data;
	graveyard_entries[graveyard_entry_count].absolute_index = absolute_index;
	graveyard_entries[graveyard_entry_count].identifier = ((struct datum_header *)datum_address(data, absolute_index))->identifier;
	graveyard_entries[graveyard_entry_count].offset = (long)graveyard_used;
	graveyard_used += (unsigned long)((size + 7) & ~7);
	overlap.tombstones++;
	overlap.graveyard_bytes += (unsigned long)size;
	/* (the copy is whole before the render can find its entry) */
	__atomic_store_n(&graveyard_entry_count, graveyard_entry_count + 1, __ATOMIC_RELEASE);
	return 0;
}

/* datum_new and datum_new_at_index, the tick, before the slot is cleared */
void halo_epoch_datum_created(struct data_array *data, long absolute_index)
{
	struct halo_epoch_marked_array *entry;

	if (!epoch_open_on_mutator())
		return;
	entry = mark(data, absolute_index, _mark_created);
	overlap.creations++;
	/* (the mark is seen before the slot is cleared and its identifier written) */
	__atomic_thread_fence(__ATOMIC_RELEASE);
	if (entry && created_log_count < CREATED_LOG_COUNT)
	{
		created_log[created_log_count].entry = entry;
		created_log[created_log_count].absolute_index = absolute_index;
		created_log_count++;
	}
}

void halo_epoch_datum_ready(struct data_array *data, long absolute_index)
{
	struct halo_epoch_marked_array *entry;

	if (!MARK_FLAG(data) || !halo_epoch_active || !halo_epoch_on_mutator())
		return;
	entry = array_of(data);
	if (entry && absolute_index >= 0 && absolute_index < entry->maximum_count)
	{
		__atomic_thread_fence(__ATOMIC_RELEASE);
		entry->marks[absolute_index] &= (unsigned char)~_mark_created;
	}
}

unsigned long halo_epoch_scope_begin(void)
{
	return created_log_count;
}

void halo_epoch_scope_ready(unsigned long scope)
{
	if (!halo_epoch_active || !halo_epoch_on_mutator())
		return;
	__atomic_thread_fence(__ATOMIC_RELEASE);
	while (created_log_count > scope)
	{
		created_log_count--;
		created_log[created_log_count].entry->marks[created_log[created_log_count].absolute_index] &=
			(unsigned char)~_mark_created;
	}
}

int halo_epoch_datum_state(const struct data_array *data, long absolute_index)
{
	const struct halo_epoch_marked_array *entry;
	unsigned char bits;

	if (!MARK_FLAG(data))
		return _halo_epoch_datum_live;
	entry = array_of(data);
	if (!entry || absolute_index < 0 || absolute_index >= entry->maximum_count)
		return _halo_epoch_datum_live;
	bits = entry->marks[absolute_index];
	return (bits & _mark_created) ? _halo_epoch_datum_created :
		(bits & _mark_tombstone) ? _halo_epoch_datum_tombstoned : _halo_epoch_datum_live;
}

/* the walks: the render skips a datum the tick is making (the tick sees its
array as without the thread) */
int halo_epoch_datum_hidden_from_caller(const struct data_array *data, long absolute_index)
{
	const struct halo_epoch_marked_array *entry;

	if (!halo_epoch_active || !MARK_FLAG(data) || halo_epoch_on_mutator())
		return FALSE;
	/* (the identifier read before the mark: datum_new marks first) */
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	entry = array_of(data);
	return entry && absolute_index >= 0 && absolute_index < entry->maximum_count &&
		(entry->marks[absolute_index] & _mark_created);
}

/* the lookups: what the render finds for index, given what the usual test
found (found: the datum, or NULL) */
void *halo_epoch_render_lookup(const struct data_array *data, long index, void *found)
{
	const struct halo_epoch_marked_array *entry;
	long absolute_index = (short)index;
	short identifier = (short)(index >> 16);
	unsigned char bits;

	if (!halo_epoch_active || !MARK_FLAG(data) || halo_epoch_on_mutator())
		return found;
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	entry = array_of(data);
	if (!entry || absolute_index < 0 || absolute_index >= entry->maximum_count || absolute_index >= data->maximum_count)
		return found;
	bits = entry->marks[absolute_index];
	if (found)
		return (bits & _mark_created) && guarded(data) ? NULL : found;
	if (bits & _mark_tombstone)
	{
		unsigned long count = __atomic_load_n(&graveyard_entry_count, __ATOMIC_ACQUIRE), slot;

		for (slot = count; slot-- > 0;)
			if (graveyard_entries[slot].data == data && graveyard_entries[slot].absolute_index == absolute_index &&
				(!identifier || graveyard_entries[slot].identifier == identifier))
			{
				return graveyard + graveyard_entries[slot].offset;
			}
	}
	return NULL;
}

/* a datum made or deleted by the render while a tick runs: the tick's
allocator is not the render's to use. The arrays only the render changes
(the objects' cached render states; the texture cache, whose requests from
the tick wait for the join: xbox_texture_cache.c) are its own */
static char const *const render_owned_arrays[] =
{
	"cached object render states",
	"xbox texture",
	"xbox texture cache",
};

void halo_epoch_check_reader_mutation(const struct data_array *data, long absolute_index, const char *what)
{
	static struct { const struct data_array *data; const char *what; } seen[32];
	static unsigned long seen_count, total;
	unsigned long index;

	if (!halo_epoch_active || overlap.closed || halo_epoch_on_mutator())
		return;
	for (index = 0; index < sizeof(render_owned_arrays) / sizeof(render_owned_arrays[0]); index++)
		if (!strncmp(data->name, render_owned_arrays[index], TAG_STRING_LENGTH))
			return;
	total++;
	for (index = 0; index < seen_count; index++)
		if (seen[index].data == data && seen[index].what == what)
			return;
	if (seen_count < sizeof(seen) / sizeof(seen[0]))
	{
		seen[seen_count].data = data;
		seen[seen_count].what = what;
		seen_count++;
		platform_log("tick overlap: %s of %.31s #%ld by the render while a tick runs (%lu so far)",
			what, data->name, absolute_index, total);
#ifdef __GLIBC__
		{
			/* (a desktop test build: where from) */
			void *frames[14];
			int count = backtrace(frames, 14), frame;

			for (frame = 1; frame < count; frame++)
				platform_log("tick overlap:   %p", frames[frame]);
		}
#endif
	}
}

/* every mark cleared (the join, or a sync) */
static void marks_sweep(void)
{
	unsigned long slot;

	for (slot = 0; slot < mark_log_count; slot++)
	{
		struct halo_epoch_marked_array *entry = mark_log[slot].entry;

		entry->marks[mark_log[slot].absolute_index] = 0;
	}
	mark_log_count = 0;
	for (slot = 0; slot < HALO_EPOCH_ARRAY_TABLE_SIZE; slot++)
	{
		struct halo_epoch_marked_array *entry = &halo_epoch_marked_arrays[slot];

		if (!entry->data)
			continue;
		if (mark_log_overflowed && entry->marked)
			memset(entry->marks, 0, (size_t)entry->maximum_count);
		entry->marked = 0;
		/* (the array's bytes as they are without the thread: the game state
		is saved between updates) */
		if (MARK_FLAG(entry->data))
			MARK_FLAG(entry->data) = 0;
		MARK_SLOT(entry->data) = 0;
	}
	mark_log_overflowed = 0;
	created_log_count = 0;
	graveyard_entry_count = 0;
	graveyard_used = 0;
}

/* ---------- deferred pool frees */

int halo_epoch_pool_free(struct memory_pool *pool, void *block)
{
	struct memory_pool_block *pool_block = block;

	if (!epoch_open_on_mutator())
		return 0;
	if (deferred_free_count >= DEFERRED_FREE_COUNT)
	{
		static unsigned long said;

		if (said++ < 4)
			platform_log("tick overlap: deferred frees full: a sync");
		halo_tick_overlap_wait_for_render();
		return 0;
	}
	deferred_frees[deferred_free_count].pool = pool;
	deferred_frees[deferred_free_count].block = pool_block;
	deferred_free_count++;
	overlap.deferred_blocks++;
	/* (counted free at once, as without the thread: memory_pool.c) */
	pool_block->header_signature = DEFERRED_BLOCK_SIGNATURE;
	return 1;
}

int halo_epoch_pool_block_deferred(const void *block)
{
	return ((const struct memory_pool_block *)block)->header_signature == DEFERRED_BLOCK_SIGNATURE;
}

static void deferred_frees_release(void)
{
	unsigned long slot;

	for (slot = 0; slot < deferred_free_count; slot++)
		memory_pool_block_release(deferred_frees[slot].pool, deferred_frees[slot].block);
	deferred_free_count = 0;
}

int halo_epoch_compaction_defer(struct memory_pool *pool)
{
	(void)pool;
	/* (a compaction moves what the render reads: it runs once the frame is
	presented, where it ran without the thread) */
	if (epoch_open_on_mutator())
		halo_tick_overlap_wait_for_render();
	return 0;
}

/* memory_pool.c, an allocation that fits only where kept blocks are */
void halo_epoch_pool_needs_room(void)
{
	if (epoch_open_on_mutator())
		halo_tick_overlap_wait_for_render();
}

/* ---------- the game state replaced */

void halo_epoch_discard(void)
{
	unsigned long slot;

	halo_epoch_generation++;
	/* (no tick runs while the game state is replaced) */
	for (slot = 0; slot < HALO_EPOCH_ARRAY_TABLE_SIZE; slot++)
	{
		struct halo_epoch_marked_array *entry = &halo_epoch_marked_arrays[slot];

		if (!entry->data)
			continue;
		overlap_free(entry->marks);
		entry->marks = NULL;
		entry->marked = 0;
		entry->maximum_count = 0;
		entry->data = NULL;
	}
	array_count = 0;
	mark_log_count = 0;
	mark_log_overflowed = 0;
	created_log_count = 0;
	graveyard_entry_count = 0;
	graveyard_used = 0;
	deferred_free_count = 0;
}

/* ---------- assertions (cseries.h) */

static void halo_assert_soft(const char *information, const char *file, long line, void *caller)
{
	static struct { const char *file; long line; } seen[64];
	static unsigned long seen_count;
	unsigned long index;

	for (index = 0; index < seen_count; index++)
		if (seen[index].file == file && seen[index].line == line)
			return;
	if (seen_count < sizeof(seen) / sizeof(seen[0]))
	{
		seen[seen_count].file = file;
		seen[seen_count].line = line;
		seen_count++;
	}
	platform_log("tick overlap: the render's assertion skipped while a tick ran: %s in %s,#%ld (from %p)",
		information ? information : "?", file ? file : "?", line, caller);
#ifdef __GLIBC__
	{
		/* (a desktop test build: where from) */
		void *frames[12];
		int count = backtrace(frames, 12), frame;

		for (frame = 1; frame < count; frame++)
			platform_log("tick overlap:   %p", frames[frame]);
	}
#endif
}

void halo_assert_failed(char *information, char *file, long line)
{
	if (halo_epoch_active && !halo_epoch_on_mutator())
	{
		halo_assert_soft(information, file, line, __builtin_return_address(0));
		return;
	}
	display_assert(information, file, line, TRUE);
	system_exit(-1);
}

/* ---------- the render's copies: the objects' poses */

struct pose_entry
{
	long object_index;
	long first_matrix;
	short node_count;
};

struct pose_buffer
{
	struct pose_entry *entries; /* by absolute object index */
	real_matrix4x3 *matrices;
	long matrix_capacity;
	unsigned long capture;
	unsigned long map_generation;
	boolean valid;
};

static struct pose_buffer pose_buffers[2];
/* the buffer the render draws from (-1: none), and the one being made */
static int pose_published = -1, pose_captured = -1;
static unsigned long pose_capture_serial;

static void poses_capture(void)
{
	int which = pose_published == 0 ? 1 : 0;
	struct pose_buffer *buffer = &pose_buffers[which];
	struct object_iterator iterator;
	struct object_datum *object;
	long used = 0;

	pose_captured = -1;
	if (!buffer->entries)
	{
		buffer->entries = overlap_calloc(MAXIMUM_OBJECTS_PER_MAP, sizeof(*buffer->entries));
		if (!buffer->entries)
			return;
	}
	buffer->capture = ++pose_capture_serial;
	object_iterator_new(&iterator, _object_mask_all, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.index);
		short node_count = (short)(object->object.node_matrices.size / (short)sizeof(real_matrix4x3));
		struct object_header_datum *header = datum_get(object_header_data, iterator.index);
		struct pose_entry *entry;

		if (absolute_index >= MAXIMUM_OBJECTS_PER_MAP || node_count <= 0)
			continue;
		/* (an object the tick does not move keeps its matrices: an inactive
		one with no parent, which is not updated; the render reads those live) */
		if (object->object.parent_object_index == NONE && !TEST_FLAG(header->flags, _object_header_active_bit))
		{
			buffer->entries[absolute_index].object_index = NONE;
			continue;
		}
		if (used + node_count > buffer->matrix_capacity)
		{
			long capacity = buffer->matrix_capacity ? buffer->matrix_capacity * 2 : 4096;
			real_matrix4x3 *matrices;

			while (capacity < used + node_count)
				capacity *= 2;
			matrices = overlap_realloc(buffer->matrices, (size_t)capacity * sizeof(real_matrix4x3));
			if (!matrices)
			{
				buffer->entries[absolute_index].object_index = NONE;
				continue;
			}
			buffer->matrices = matrices;
			buffer->matrix_capacity = capacity;
		}
		csmemcpy(buffer->matrices + used, object_header_block_get(iterator.index, &object->object.node_matrices),
			node_count * sizeof(real_matrix4x3));
		entry = &buffer->entries[absolute_index];
		entry->object_index = iterator.index;
		entry->first_matrix = used;
		entry->node_count = node_count;
		used += node_count;
	}
	buffer->map_generation = halo_epoch_generation;
	buffer->valid = TRUE;
	pose_captured = which;
}

void *halo_tick_overlap_node_matrices(long object_index)
{
	struct pose_buffer *buffer;
	struct pose_entry *entry;
	long absolute_index;

	if (!halo_epoch_active || pose_published < 0 || object_index == NONE || halo_epoch_on_mutator())
		return NULL;
	buffer = &pose_buffers[pose_published];
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	if (!buffer->valid || buffer->map_generation != halo_epoch_generation || absolute_index < 0 ||
		absolute_index >= MAXIMUM_OBJECTS_PER_MAP)
	{
		return NULL;
	}
	entry = &buffer->entries[absolute_index];
	if (entry->object_index != object_index || entry->node_count <= 0)
		return NULL;
	return buffer->matrices + entry->first_matrix;
}

/* ---------- the render's copies: the clusters' lists */

struct cluster_list
{
	long *first; /* by cluster: where its datums start in datums */
	long first_capacity;
	long *datums; /* each cluster's datums, then NONE */
	long datum_capacity;
	short cluster_count;
	boolean valid;
};

struct list_buffer
{
	struct cluster_list lists[NUMBER_OF_TICK_OVERLAP_LISTS];
	unsigned long map_generation;
};

static struct list_buffer list_buffers[2];
static int list_published = -1, list_captured = -1;

static boolean list_room(long **array, long *capacity, long wanted)
{
	long grown;
	long *larger;

	if (wanted <= *capacity)
		return TRUE;
	grown = *capacity ? *capacity * 2 : 1024;
	while (grown < wanted)
		grown *= 2;
	larger = overlap_realloc(*array, (size_t)grown * sizeof(long));
	if (!larger)
		return FALSE;
	*array = larger;
	*capacity = grown;
	return TRUE;
}

static struct cluster_partition *list_partition(int which)
{
	switch (which)
	{
	case _tick_overlap_list_collideable: return &collideable_object_cluster_partition;
	case _tick_overlap_list_noncollideable: return &noncollideable_object_cluster_partition;
	default: return &light_cluster_partition;
	}
}

static struct data_array *list_data(int which)
{
	return which == _tick_overlap_list_light ? light_data : object_header_data;
}

static void lists_capture(void)
{
	int which_buffer = list_published == 0 ? 1 : 0;
	struct list_buffer *buffer = &list_buffers[which_buffer];
	struct structure_bsp *structure_bsp = global_structure_bsp_try_and_get();
	short cluster_count = structure_bsp ? (short)structure_bsp->clusters.count : 0;
	int which;

	list_captured = -1;
	for (which = 0; which < NUMBER_OF_TICK_OVERLAP_LISTS; which++)
	{
		struct cluster_list *list = &buffer->lists[which];
		struct cluster_partition *partition = list_partition(which);
		long used = 0;
		short cluster_index;

		list->valid = FALSE;
		if (!cluster_count || !partition->data_reference_data || !partition->data_reference_data->valid ||
			!list_room(&list->first, &list->first_capacity, cluster_count))
		{
			continue;
		}
		for (cluster_index = 0; cluster_index < cluster_count; cluster_index++)
		{
			long reference, datum_index;

			list->first[cluster_index] = used;
			for (datum_index = cluster_partition_get_first_datum(partition, &reference, cluster_index);
				datum_index != NONE;
				datum_index = cluster_partition_get_next_datum(partition, &reference))
			{
				if (!list_room(&list->datums, &list->datum_capacity, used + 2))
					break;
				list->datums[used++] = datum_index;
			}
			if (!list_room(&list->datums, &list->datum_capacity, used + 1))
				break;
			list->datums[used++] = NONE;
		}
		if (cluster_index == cluster_count)
		{
			list->cluster_count = cluster_count;
			list->valid = TRUE;
		}
	}
	buffer->map_generation = halo_epoch_generation;
	list_captured = which_buffer;
}

int halo_tick_overlap_lists_active(int which)
{
	return halo_epoch_active && list_published >= 0 && which >= 0 && which < NUMBER_OF_TICK_OVERLAP_LISTS &&
		!halo_epoch_on_mutator() && list_buffers[list_published].map_generation == halo_epoch_generation &&
		list_buffers[list_published].lists[which].valid;
}

long halo_tick_overlap_list_next(int which, long *iterator)
{
	struct cluster_list *list = &list_buffers[list_published].lists[which];

	while (*iterator != NONE)
	{
		long datum_index = list->datums[*iterator];

		if (datum_index == NONE)
		{
			*iterator = NONE;
			break;
		}
		(*iterator)++;
		/* (one deleted since, which the render's lookups still find, or
		not: then skipped) */
		if (datum_try_and_get(list_data(which), datum_index))
			return datum_index;
	}
	return NONE;
}

long halo_tick_overlap_list_first(int which, long *iterator, short cluster_index)
{
	struct cluster_list *list = &list_buffers[list_published].lists[which];

	*iterator = cluster_index >= 0 && cluster_index < list->cluster_count ? list->first[cluster_index] : NONE;
	return halo_tick_overlap_list_next(which, iterator);
}

/* ---------- the thread */

static int work_requested(void)
{
	return __atomic_load_n(&overlap.started, __ATOMIC_ACQUIRE) != overlap.finished;
}

static int work_done(void)
{
	return __atomic_load_n(&overlap.finished, __ATOMIC_ACQUIRE) == overlap.started;
}

static int main_joining(void)
{
	return __atomic_load_n(&overlap.joining, __ATOMIC_ACQUIRE) != 0;
}

static void tick_thread_body(void)
{
	__UINTPTR_TYPE__ here = halo_epoch_stack_pointer();

	/* (the thread is told by its stack: from here down, the size it was
	made with less a margin for what is above this frame) */
	halo_tick_stack_high = here + 1024;
	halo_tick_stack_low = here - (HALO_CONSOLE_TICK_OVERLAP_STACK_SIZE - 64 * 1024);
	platform_log("tick overlap: the tick thread runs (stack %p..%p)", (void *)halo_tick_stack_low, (void *)halo_tick_stack_high);
	for (;;)
	{
		unsigned long long began;
		float delta;

		tick_overlap_wait(_waiter_tick, work_requested, 400);
		delta = overlap.delta;
		began = tick_overlap_now_us();
		game_time_update(delta);
		overlap.elapsed = game_time_get_elapsed();
		overlap.fraction = game_time_get_tick_fraction();
		overlap.game_time = game_time_get();
		/* what the render of this update did to the game state without the
		thread, in its order, here: the sound manager's frame
		(main_game_render), the decals' upkeep (the rasterizer's frame) and
		the lights' (lights_preprocess_scene) */
		sound_render();
		rasterizer_decals_port_overlap_upkeep();
		lights_port_overlap_upkeep();
		overlap.capture_us = tick_overlap_now_us();
		poses_capture();
		lists_capture();
		{
			unsigned long long ended = tick_overlap_now_us();

			overlap.capture_us = ended - overlap.capture_us;
			overlap.update_us = ended - began;
		}
		tick_overlap_publish_begin();
		__atomic_store_n(&overlap.finished, overlap.finished + 1, __ATOMIC_RELEASE);
		tick_overlap_publish_end(_waiter_game);
	}
}

int halo_tick_overlap_enabled(void)
{
	if (overlap.enabled < 0)
	{
		const char *setting = getenv("HALO_TICK_OVERLAP");

		overlap.enabled = !setting || atoi(setting) != 0;
		if (overlap.enabled)
		{
			overlap.enabled = tick_overlap_thread_create(tick_thread_body, HALO_CONSOLE_TICK_OVERLAP_STACK_SIZE);
			if (!overlap.enabled)
				platform_log("tick overlap: no thread: the tick stays on the game's thread");
		}
		else
			platform_log("tick overlap: off (HALO_TICK_OVERLAP=0)");
		halo_epoch_threaded = overlap.enabled;
		halo_epoch_guard_constructions(object_header_data);
	}
	return overlap.enabled;
}

int halo_tick_overlap_wanted(void)
{
	if (!halo_tick_overlap_enabled())
		return FALSE;
#ifndef HALO_CONSOLE_NO_FORGE
	/* (Forge's monitor and menus share the Forge state with the tick's
	spawns: a Forge game keeps the tick on the game's thread) */
	if (forge_game_active() || forge_variant_playing())
		return FALSE;
#endif
	return TRUE;
}

void halo_tick_overlap_start(float delta)
{
	overlap.delta = delta;
	/* (the lights, guarded as the objects) */
	halo_epoch_guard_constructions(light_data);
	/* the epoch opens here, on the game's thread, so the whole of the
	render that follows sees it open */
	overlap.closed = FALSE;
	__atomic_store_n(&halo_epoch_active, 1, __ATOMIC_RELEASE);
	tick_overlap_publish_begin();
	__atomic_store_n(&overlap.started, overlap.started + 1, __ATOMIC_RELEASE);
	tick_overlap_publish_end(_waiter_tick);
}

/* the epoch's end: on the game's thread at the join, with the tick done */
static void epoch_end(void)
{
	__atomic_store_n(&halo_epoch_active, 0, __ATOMIC_RELEASE);
	deferred_frees_release();
	marks_sweep();
	overlap.epochs++;
}

void halo_tick_overlap_join(void)
{
	unsigned long long began = tick_overlap_now_us(), joined;

	tick_overlap_publish_begin();
	__atomic_store_n(&overlap.joining, 1, __ATOMIC_RELEASE);
	tick_overlap_publish_end(_waiter_tick);
	tick_overlap_wait(_waiter_game, work_done, 400);
	joined = tick_overlap_now_us();
	__atomic_store_n(&overlap.joining, 0, __ATOMIC_RELEASE);
	epoch_end();
	halo_tick_overlap_elapsed = overlap.elapsed;
	halo_tick_overlap_fraction = overlap.fraction;
	halo_tick_overlap_game_time = overlap.game_time;
	if (pose_captured >= 0)
	{
		pose_published = pose_captured;
		pose_captured = -1;
	}
	if (list_captured >= 0)
	{
		list_published = list_captured;
		list_captured = -1;
	}
	/* the bitmaps the tick asked for (xbox_texture_cache.c) */
	texture_cache_port_overlap_requests();
	if (overlap.deferred_call_count)
	{
		unsigned long index, count = overlap.deferred_call_count;

		overlap.deferred_call_count = 0;
		for (index = 0; index < count; index++)
			overlap.deferred_calls[index]();
	}
	overlap.wait_us += joined - began;
	overlap.update_total_us += overlap.update_us;
	overlap.capture_total_us += overlap.capture_us;
	overlap.join_us += tick_overlap_now_us() - joined;
	if (++overlap.frames >= 300)
	{
		platform_log("tick overlap (ms/frame, %lu frames): update %.2f (the copies %.2f), the join's wait %.2f, the join's "
			"work %.2f; %lu syncs; a frame: %.1f datums deleted (%.1f KB kept), %.1f made, %.1f blocks kept",
			overlap.frames, overlap.update_total_us / 1000.0 / overlap.frames,
			overlap.capture_total_us / 1000.0 / overlap.frames, overlap.wait_us / 1000.0 / overlap.frames,
			overlap.join_us / 1000.0 / overlap.frames, overlap.syncs, (double)overlap.tombstones / overlap.frames,
			overlap.graveyard_bytes / 1024.0 / overlap.frames, (double)overlap.creations / overlap.frames,
			(double)overlap.deferred_blocks / overlap.frames);
		overlap.frames = 0;
		overlap.wait_us = overlap.update_total_us = overlap.join_us = overlap.capture_total_us = 0;
		overlap.syncs = overlap.tombstones = overlap.creations = overlap.graveyard_bytes = overlap.deferred_blocks = 0;
	}
}

int halo_tick_overlap_defer(void (*call)(void))
{
	if (overlap.enabled <= 0 || !halo_epoch_active || overlap.closed || halo_epoch_on_mutator() ||
		overlap.deferred_call_count >= DEFERRED_CALL_COUNT)
	{
		return FALSE;
	}
	overlap.deferred_calls[overlap.deferred_call_count++] = call;
	return TRUE;
}

void halo_tick_overlap_wait_for_render(void)
{
	if (overlap.enabled <= 0 || !halo_epoch_active || overlap.closed || !halo_epoch_on_mutator())
		return;
	tick_overlap_wait(_waiter_tick, main_joining, 0);
	/* the frame is presented and the game's thread waits in the join: the
	rest of the update runs as it would without the thread */
	deferred_frees_release();
	marks_sweep();
	overlap.closed = TRUE;
	overlap.syncs++;
}

unsigned long long halo_tick_overlap_last_us(void)
{
	return overlap.update_us;
}

#else

/* (an empty unit otherwise) */
typedef int tick_overlap_unused;

#endif
