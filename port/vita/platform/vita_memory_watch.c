/*
VITA_MEMORY_WATCH.C

Guest memory write tracking (port/linux/src/memory_watch.c's interface)
without page protection, which the Vita does not offer applications. A page
counts as written when the host writes into it (memory_watch_prepare_write,
before file reads) or its block changes (memory_watch_forget); writes by
game code itself are not seen. The Vita renderer reads the window in place
(the GPU maps it), so only converted textures depend on this.
*/

#include "platform.h"

#define WATCH_PAGE_SIZE 0x1000UL
/* (the most the window can be: its size is the host's, HALO_WINDOW_REBASE) */
#define WATCH_PAGE_COUNT (PLATFORM_CONTIGUOUS_MAXIMUM / WATCH_PAGE_SIZE)

static unsigned long page_generation[WATCH_PAGE_COUNT];
static unsigned long watch_serial = 1;

void memory_watch_initialize(void)
{
}

void memory_watch_protect(unsigned long address, unsigned long size)
{
	(void)address;
	(void)size;
}

/* a host address of the contiguous memory, as its offset in it (the window's
Xbox address less its base: the layout is translated, xbox_address.h) */
static unsigned long window_offset(unsigned long address)
{
	return PLATFORM_WINDOW_ADDRESS((void *)address) - PLATFORM_CONTIGUOUS_BASE;
}

static void mark_written(unsigned long address, unsigned long size)
{
	unsigned long first, last, page, serial;

	if (!size || !platform_is_contiguous((void *)address))
		return;
	first = window_offset(address) / WATCH_PAGE_SIZE;
	last = (window_offset(address) + size - 1) / WATCH_PAGE_SIZE;
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
	serial = __atomic_add_fetch(&watch_serial, 1, __ATOMIC_RELAXED);
	for (page = first; page <= last; page++)
		page_generation[page] = serial;
}

unsigned long memory_watch_generation(unsigned long address, unsigned long size)
{
	unsigned long first, last, page, newest = 0;

	if (!size || !platform_is_contiguous((void *)address))
		return 0;
	first = window_offset(address) / WATCH_PAGE_SIZE;
	last = (window_offset(address) + size - 1) / WATCH_PAGE_SIZE;
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
	for (page = first; page <= last; page++)
	{
		if (page_generation[page] > newest)
			newest = page_generation[page];
	}
	return newest;
}

unsigned long memory_watch_serial(void)
{
	return __atomic_load_n(&watch_serial, __ATOMIC_RELAXED);
}

void memory_watch_prepare_write(void *address, unsigned long size)
{
	mark_written((unsigned long)address, size);
}

void memory_watch_forget(void *address, unsigned long size)
{
	mark_written((unsigned long)address, size);
}
