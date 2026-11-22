/*
VITA_MCC.C

What the game's MCC maps and Workshop (configure.py --vita-mcc: HALO_MCC)
ask of the platform that the desktop's posix_steam_bridge.c and
posix_mcc_preview.c do with a desktop's processes and descriptors:

- the Steam bridge's (steam_client_bridge.h steam_bridge_platform_*): no
  Steam client, no steamcmd and no child processes on a Vita: none found,
  none started. Folders are the card's.
- an MCC package's preview (posix_mcc_preview_read): read from beneath its
  package's folder, by a relative path of plain names (no "..", no
  absolute path), a regular file of at most capacity bytes.
*/

#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>

#include <stdio.h>
#include <string.h>

#include "posix.h"

void *steam_bridge_platform_library_open(const char *path)
{
	(void)path;
	return NULL;
}

void *steam_bridge_platform_library_symbol(void *library, const char *name)
{
	(void)library;
	(void)name;
	return NULL;
}

void steam_bridge_platform_set_app_id(const char *app_id)
{
	(void)app_id;
}

void steam_bridge_platform_sleep(int milliseconds)
{
	sceKernelDelayThread((SceUInt)(milliseconds > 0 ? milliseconds : 0) * 1000);
}

int steam_bridge_platform_find_steamcmd(char *path, int size)
{
	if (size > 0)
		path[0] = 0;
	return 0;
}

int steam_bridge_platform_steamcmd_root(const char *steamcmd, char *root, int size)
{
	(void)steamcmd;
	if (size > 0)
		root[0] = 0;
	return 0;
}

void *steam_bridge_platform_child_start(const char *const *arguments)
{
	(void)arguments;
	return NULL;
}

int steam_bridge_platform_child_line(void *child, char *line, int size, int milliseconds)
{
	(void)child;
	(void)milliseconds;
	if (size > 0)
		line[0] = 0;
	return -1;
}

void steam_bridge_platform_child_end(void *child)
{
	(void)child;
}

int steam_bridge_platform_open_sign_in(const char *steamcmd)
{
	(void)steamcmd;
	return 0;
}

int steam_bridge_platform_is_folder(const char *path)
{
	SceIoStat stat;

	return path && sceIoGetstat(path, &stat) >= 0 && SCE_S_ISDIR(stat.st_mode);
}

int steam_bridge_platform_remove_folder(const char *path)
{
	(void)path;
	return 0;
}

int steam_bridge_platform_merge_folder(const char *from, const char *to)
{
	(void)from;
	(void)to;
	return 0;
}

int posix_mcc_preview_read(const char *root, const char *relative, void *buffer, posix_ulong capacity)
{
	char path[512];
	const char *part;
	SceIoStat stat;
	FILE *file;
	size_t got;

	if (!root || !relative || !*relative || relative[0] == '/' || strchr(relative, ':') || strchr(relative, '\\'))
		return -1;
	for (part = relative; part; part = strchr(part, '/') ? strchr(part, '/') + 1 : NULL)
	{
		if (!strncmp(part, "..", 2) && (part[2] == '/' || !part[2]))
			return -1;
	}
	if (snprintf(path, sizeof(path), "%s/%s", root, relative) >= (int)sizeof(path))
		return -1;
	if (sceIoGetstat(path, &stat) < 0 || !SCE_S_ISREG(stat.st_mode) || stat.st_size <= 0 ||
		(unsigned long long)stat.st_size > (unsigned long long)capacity)
		return -1;
	file = fopen(path, "rb");
	if (!file)
		return -1;
	got = fread(buffer, 1, (size_t)stat.st_size, file);
	fclose(file);
	return got == (size_t)stat.st_size ? (int)got : -1;
}
