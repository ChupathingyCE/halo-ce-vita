/*
VITA_MEMORY.C

The Xbox's memory window and the memory mapping the platform layer asks
for (include/posix/sys/mman.h), on the Vita.

The game's data is linked for the Xbox's window at 0x80000000, which the
Vita's user memory cannot have: the kernel places a memory block where it
likes, from about 0x81000000 up. The window is moved (HALO_WINDOW_REBASE,
the game's source/cseries/xbox_address.h): one 128 MB block, allocated
before any of the platform layer's constructors runs (its window
reservation is constructor 101), and the platform layer told where it is
(platform_contiguous_base and platform_contiguous_size).

The block must not overlap the linked window, 0x80000000 to 0x88000000:
xbox_pointer moves a value in that range and leaves any other, so a window
starting inside it would have some of its own addresses moved again. It is
asked for with that alignment first; if the kernel will not align a block
that much, room below 0x88000000 is taken by a block of its own, the window
allocated above it, and that block given back.

A user application has neither page protection nor anonymous mappings: a
mapping asked for inside the window is that part of it; any other is a
memory block of its own. Protection always succeeds and changes nothing
(HALO_CONSOLE_NO_WRITE_WATCH).
*/

#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/clib.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "vita_host.h"

#define WINDOW_SIZE 0x08000000UL
#define LINKED_WINDOW_END 0x88000000UL

/* the platform layer's (the game's port/linux/src/xbox_memory.c) */
extern unsigned long platform_contiguous_base;
extern unsigned long platform_contiguous_size;

static SceUID window_block = -1;
static uintptr_t window_start, window_end;
static char window_report[192];

static SceUID block_allocate(const char *name, unsigned long size, unsigned long alignment, void **base)
{
	SceUID block;

	*base = NULL;
	if (alignment)
	{
		SceKernelAllocMemBlockOpt options;

		memset(&options, 0, sizeof(options));
		options.size = sizeof(options);
		options.attr = 0x00000004; /* SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT */
		options.alignment = alignment;
		block = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, size, &options);
	}
	else
		block = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, size, NULL);
	if (block >= 0)
		sceKernelGetMemBlockBase(block, base);
	return block;
}

static int window_acceptable(void *base)
{
	uintptr_t start = (uintptr_t)base;

	return base && (start == 0x80000000UL || start >= LINKED_WINDOW_END ||
		start + WINDOW_SIZE <= 0x80000000UL) && start >= WINDOW_SIZE;
}

/* (before the platform layer's constructors: 101 and up) */
__attribute__((constructor(100)))
static void window_allocate(void)
{
	void *base = NULL;
	SceUID block = block_allocate("xbox_window", WINDOW_SIZE, WINDOW_SIZE, &base);
	const char *how = "aligned";

	if (block >= 0 && !window_acceptable(base))
	{
		sceKernelFreeMemBlock(block);
		block = -1;
	}
	if (block < 0)
	{
		/* room below the linked window's end taken first, then the window
		above it, then the room given back */
		void *probe = NULL;
		SceUID probe_block = block_allocate("xbox_window_probe", 0x1000, 0, &probe);
		SceUID filler = -1;

		how = "above a filler";
		if (probe_block >= 0)
		{
			uintptr_t next = (uintptr_t)probe;

			sceKernelFreeMemBlock(probe_block);
			if (next < LINKED_WINDOW_END)
			{
				void *filler_base;
				unsigned long filler_size = (unsigned long)(LINKED_WINDOW_END - next + 0xFFFFF) & ~0xFFFFFUL;

				filler = block_allocate("xbox_window_filler", filler_size, 0, &filler_base);
			}
		}
		block = block_allocate("xbox_window", WINDOW_SIZE, 0, &base);
		if (filler >= 0)
			sceKernelFreeMemBlock(filler);
		if (block >= 0 && !window_acceptable(base))
		{
			sceKernelFreeMemBlock(block);
			block = -1;
		}
	}
	if (block < 0)
	{
		snprintf(window_report, sizeof(window_report), "vita: NO Xbox window (%lu MB): the game cannot start",
			WINDOW_SIZE >> 20);
		sceClibPrintf("%s\n", window_report);
		return;
	}
	window_block = block;
	window_start = (uintptr_t)base;
	window_end = window_start + WINDOW_SIZE;
	platform_contiguous_base = (unsigned long)window_start;
	platform_contiguous_size = WINDOW_SIZE;
	snprintf(window_report, sizeof(window_report), "vita: Xbox window at 0x%08lx, %lu MB (%s)",
		(unsigned long)window_start, WINDOW_SIZE >> 20, how);
	sceClibPrintf("%s\n", window_report);
}

const char *vita_host_window_report(void)
{
	return window_report;
}

/* ---------- sys/mman.h */

#define MAPPING_MAXIMUM 64

static struct
{
	uintptr_t start;
	unsigned long size;
	SceUID block;
} mappings[MAPPING_MAXIMUM];

void *mmap(void *address, size_t length, int protection, int flags, int descriptor, off_t offset)
{
	uintptr_t start = (uintptr_t)address;
	unsigned long size = ((unsigned long)length + 0xFFF) & ~0xFFFUL;
	void *base = NULL;
	SceUID block;
	int index;

	(void)protection;
	(void)offset;
	if (descriptor != -1 || !length)
	{
		errno = EINVAL;
		return (void *)-1;
	}
	/* inside the window: that part of it */
	if (window_block >= 0 && start >= window_start && start + size <= window_end)
		return address;
	/* a fixed address anywhere else cannot be had */
	if (address && (flags & (0x10 | 0x100000)))
	{
		errno = ENOMEM;
		return (void *)-1;
	}
	for (index = 0; index < MAPPING_MAXIMUM && mappings[index].size; index++)
		;
	if (index == MAPPING_MAXIMUM)
	{
		errno = ENOMEM;
		return (void *)-1;
	}
	block = block_allocate("halo_mapping", size, 0, &base);
	if (block < 0)
	{
		errno = ENOMEM;
		return (void *)-1;
	}
	memset(base, 0, size);
	{
		char message[128];

		snprintf(message, sizeof(message), "vita: mmap %lu KB at %p (asked %p, flags 0x%x)", size >> 10, base, address,
			(unsigned)flags);
		vita_host_log(message);
	}
	mappings[index].start = (uintptr_t)base;
	mappings[index].size = size;
	mappings[index].block = block;
	return base;
}

int munmap(void *address, size_t length)
{
	uintptr_t start = (uintptr_t)address;
	int index;

	(void)length;
	if (window_block >= 0 && start >= window_start && start < window_end)
		return 0;
	for (index = 0; index < MAPPING_MAXIMUM; index++)
	{
		if (mappings[index].size && mappings[index].start == start)
		{
			char message[96];

			snprintf(message, sizeof(message), "vita: munmap %lu KB at %p", mappings[index].size >> 10, address);
			vita_host_log(message);
			sceKernelFreeMemBlock(mappings[index].block);
			memset(&mappings[index], 0, sizeof(mappings[index]));
			return 0;
		}
	}
	{
		char message[96];

		snprintf(message, sizeof(message), "vita: munmap of %p (%lu KB): not a mapping of ours", address,
			(unsigned long)length >> 10);
		vita_host_log(message);
	}
	errno = EINVAL;
	return -1;
}

int mprotect(void *address, size_t length, int protection)
{
	(void)address;
	(void)length;
	(void)protection;
	return 0;
}

int madvise(void *address, size_t length, int advice)
{
	(void)address;
	(void)length;
	(void)advice;
	return 0;
}
