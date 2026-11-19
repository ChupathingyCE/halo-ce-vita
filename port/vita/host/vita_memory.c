/*
VITA_MEMORY.C

The Xbox's memory window and the memory mapping the platform layer asks
for (include/posix/sys/mman.h), on the Vita.

The game's data hold Xbox addresses, and the Vita's own memory is where the
Xbox's window was linked (the program at 0x81000000, the C heap above it),
so the Vita has the Xbox 360's layout: translated (HALO_64BIT with
HALO_WINDOW_TRANSLATED, the game's source/cseries/xbox_address.h). The window
is one memory block wherever the kernel puts it, XBOX_WINDOW_LOW to
0x88000000 in Xbox addresses: the game's heap and the contiguous memory
(HALO_CONSOLE_WINDOW_LOW, the game's halo_console_features.h, makes the heap
48 MB). The platform layer asks for it from a constructor
(platform_xbox_window_reserve, xbox_memory.c), and commits its parts with
mmap; on the Vita the whole block is committed at once, so a mapping asked
for inside it is that part of it. Any other is a memory block of its own.
Protection always succeeds and changes nothing (HALO_CONSOLE_NO_WRITE_WATCH).
*/

#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/clib.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "vita_host.h"

/* the host address of Xbox address 0, modulo 2^32 (xbox_address.h): the
platform layer sets it once the window is made (xbox_memory.c) */
unsigned int xbox_address_space_base;

static SceUID window_block = -1;
static uintptr_t window_start, window_end;
static char window_report[192];

static SceUID block_allocate(const char *name, unsigned long size, void **base)
{
	SceUID block = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, size, NULL);

	*base = NULL;
	if (block >= 0)
		sceKernelGetMemBlockBase(block, base);
	return block;
}

/* (xbox_memory.c, from its constructor) the window: size bytes, zeroed */
void *platform_xbox_window_reserve(unsigned int size)
{
	void *base = NULL;
	SceUID block;

	if (window_block >= 0)
		return (void *)window_start;
	/* (the platform layer logs the window to stderr right after this) */
	{
		void vita_host_stderr_open(void);

		vita_host_stderr_open();
	}
	block = block_allocate("xbox_window", ((unsigned long)size + 0xFFFFF) & ~0xFFFFFUL, &base);
	if (block < 0)
	{
		snprintf(window_report, sizeof(window_report), "vita: NO Xbox window (%u MB): 0x%08x", size >> 20,
			(unsigned int)block);
		sceClibPrintf("%s\n", window_report);
		return NULL;
	}
	memset(base, 0, size);
	window_block = block;
	window_start = (uintptr_t)base;
	window_end = window_start + size;
	snprintf(window_report, sizeof(window_report), "vita: Xbox window at 0x%08lx, %u MB, all of it committed",
		(unsigned long)window_start, size >> 20);
	sceClibPrintf("%s\n", window_report);
	return base;
}


const char *vita_host_window_report(void)
{
	return window_report;
}

/* (debug, HALO_NO_GAME=2) the window given back */
void vita_host_window_free(void)
{
	if (window_block >= 0)
		sceKernelFreeMemBlock(window_block);
	window_block = -1;
}

/* the window, for the GPU's mapping (vita_main.c vita_host_arena) */
void *vita_host_window(unsigned long *size)
{
	*size = window_block >= 0 ? (unsigned long)(window_end - window_start) : 0;
	return window_block >= 0 ? (void *)window_start : NULL;
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
	block = block_allocate("halo_mapping", size, &base);
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
