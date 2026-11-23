/*
XISO.C

Copies the maps folder, and the Bink movies, out of an Xbox disc image (an "xiso", or a whole
disc's ".iso"), for the desktop ports' first start without game data
(sdl_platform.c). The Android app does the same in Java (XisoExtractor.java
in port/android, a port of this file).

The reader is based on the XISO reader in Milenko's Theseus project
(theseus/desktop/xiso.cpp and theseuslib/src/xiso.c, GPL-3.0-or-later),
which finds default.xbe in an image's root directory. This one walks every
directory below the maps folder instead, and copies the files out.

An Xbox disc's file system (XDVDFS) is made of 2048-byte sectors. Sector 32
(byte 0x10000) holds the volume descriptor: "MICROSOFT*XBOX*MEDIA", then the
root directory's sector and its size in bytes. A directory is a table of
entries, each a 14-byte header and a name:

	0	left subtree offset (16 bits, in 4-byte units; 0: none, since
		offset 0 is the tree's root)
	2	right subtree offset
	4	the file's or directory's first sector (32 bits)
	8	its size in bytes (32 bits)
	12	attributes (0x10: a directory)
	13	the name's length, then the name (not terminated)

Entries are 4-byte aligned and never cross a sector; the rest of a sector is
padding (0xff bytes). Rather than step over the padding, the walk follows
the entries' tree from the table's first entry, which is its root: a
directory's entries form a binary search tree by name (AVL-balanced), so an
in-order walk lists them sorted. A file is one run of sectors. An empty
directory has no table (size 0).

Images made from a whole disc (rather than its game partition, as xiso
tools make) put the partition further in: 0x18300000 bytes on an original
Xbox disc (XGD1), 0x0fd90000 or 0x02080000 on the Xbox 360's XGD2 and XGD3
discs. Each is tried in turn for the volume descriptor. Images are larger
than 2 GB, so every offset is 64-bit.

The maps folder is "maps", or on a European disc with one per language
("maps_en", "maps_fr", ...) the one for the system's language, else
English's, else the first. Every file in it, and in any folders inside it,
is written to <destination>/maps.partial, which becomes <destination>/maps
(in place of an empty maps folder) once all of them are there, so an
interrupted copy never leaves maps/ui.map behind and the next start asks
again.

Before anything is written the image is checked to be Halo: Combat
Evolved's Xbox disc, as the console packager checks it
(tools/console-packager): default.xbe's certificate has Halo's title ID,
and the maps folder's ui.map has an Xbox map's header (cache version 5;
Halo PC's maps are refused, with a reason that says so). Then the
destination's free space is checked against what the copy writes and
64 MB more. A copy that fails, or that the caller cancels (cancel, read
between blocks), removes what it wrote of maps.partial.

The disc's bink folder holds the movies (intro.bik, credits.bik, attract1.bik
to attract3.bik, and one more of each per language on some discs). Each .bik
file the destination's bink folder lacks is copied there, after the maps,
under the disc's name: the game looks for d:\\bink\\<name>.bik, and finds
it whatever the case. A file already there is never replaced. A disc without
a bink folder, or a movie that cannot be copied, does not fail the
extraction: the game skips movies it cannot find.
*/

/* (the desktop ports and iOS's launcher: the Android app imports the game
data itself) */
#if !defined(HALO_ANDROID) || defined(HALO_IOS)

#ifdef HALO_EXTRACTOR_STANDALONE
/* Built for the Switch host, which calls this from its own start-up rather
 * than from inside the game. platform.h is the game's platform layer and
 * brings the SDK's headers with it, which the host build has no business
 * seeing; of everything in it this file uses one function, declared here and
 * supplied by the host. posix.h and xiso.h are its own and are used as they
 * are. */
void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
#else
#include "platform.h"
#endif
#include "posix.h"
#include "xiso.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

enum
{
	SECTOR_SIZE = 2048,
	VOLUME_SECTOR = 32,
	ENTRY_HEADER_SIZE = 14,
	ATTRIBUTE_DIRECTORY = 0x10,
	/* directory tables are a few sectors; anything much larger is not one */
	MAXIMUM_DIRECTORY_SIZE = 4 << 20,
	/* an AVL tree of a full table is under 30 deep; deeper is a loop */
	MAXIMUM_TREE_DEPTH = 64,
	MAXIMUM_FOLDER_DEPTH = 16,
	MAXIMUM_FILES = 4096,
	MAXIMUM_PATH = 1024,
	COPY_BUFFER_SIZE = 1 << 20,
	/* the start of default.xbe read for its certificate, and a map's
	header */
	XBE_HEAD_SIZE = 4096,
	MAP_HEADER_SIZE = 0x800,
	XBOX_MAP_VERSION = 5,
};

/* Halo: Combat Evolved's title ID ("MS" 004), in every region's default.xbe */
#define HALO_TITLE_ID 0x4D530004u
/* room left over after the copy (the console packager's) */
#define SPARE_SPACE (64ull << 20)

static const char volume_magic[] = "MICROSOFT*XBOX*MEDIA";

/* where the game partition starts: an xiso, an original Xbox disc (XGD1),
and the Xbox 360's XGD2 and XGD3 discs */
static const unsigned long long partition_offsets[] = { 0, 0x18300000ull, 0x0fd90000ull, 0x02080000ull };

struct xiso_entry
{
	/* the path below the directory listed, '/'-separated */
	char path[MAXIMUM_PATH];
	unsigned int sector;
	unsigned int size;
	int directory;
};

struct xiso_image
{
	int descriptor;
	unsigned long long partition;
	char *error;
	int error_size;
};

/* entries of one directory, or of a directory and everything below it */
struct xiso_list
{
	struct xiso_entry *entries;
	int count;
	int capacity;
	int damaged;
};

static unsigned int read_u16(const unsigned char *bytes)
{
	return (unsigned int)bytes[0] | (unsigned int)bytes[1] << 8;
}

static unsigned int read_u32(const unsigned char *bytes)
{
	return (unsigned int)bytes[0] | (unsigned int)bytes[1] << 8 | (unsigned int)bytes[2] << 16 |
		(unsigned int)bytes[3] << 24;
}

/* sets the player's reason and returns 0 */
static int fail(struct xiso_image *image, const char *format, const char *detail)
{
	snprintf(image->error, (size_t)image->error_size, format, detail ? detail : "");
	return 0;
}

/* reads size bytes at offset in the game partition; 0 if they are not all
there */
static int image_read(struct xiso_image *image, unsigned long long offset, void *buffer, unsigned int size)
{
	unsigned long long position = image->partition + offset;
	posix_ulong low, high;
	unsigned char *at = buffer;

	if (posix_seek(image->descriptor, (posix_long)(position & 0xffffffffull), (posix_long)(position >> 32),
		SEEK_SET, &low, &high) != 0)
	{
		return 0;
	}
	while (size > 0)
	{
		ssize_t count = read(image->descriptor, at, size);

		if (count <= 0)
			return 0;
		at += count;
		size -= (unsigned int)count;
	}
	return 1;
}

/* finds the game partition and its root directory */
static int find_volume(struct xiso_image *image, unsigned int *root_sector, unsigned int *root_size)
{
	unsigned char descriptor[SECTOR_SIZE];
	size_t index;

	for (index = 0; index < sizeof(partition_offsets) / sizeof(partition_offsets[0]); index++)
	{
		image->partition = partition_offsets[index];
		if (image_read(image, (unsigned long long)VOLUME_SECTOR * SECTOR_SIZE, descriptor, sizeof(descriptor)) &&
			!memcmp(descriptor, volume_magic, sizeof(volume_magic) - 1))
		{
			*root_sector = read_u32(descriptor + 20);
			*root_size = read_u32(descriptor + 24);
			return 1;
		}
	}
	return fail(image, "This is not an Xbox disc image.%s", NULL);
}

/* a directory's table, or NULL if it cannot be read; an empty directory's
is an empty allocation */
static unsigned char *read_table(struct xiso_image *image, unsigned int sector, unsigned int size)
{
	unsigned char *table;

	if (size > MAXIMUM_DIRECTORY_SIZE)
		return NULL;
	table = malloc(size ? size : 1);
	if (table && size && !image_read(image, (unsigned long long)sector * SECTOR_SIZE, table, size))
	{
		free(table);
		return NULL;
	}
	return table;
}

/* whether a name is one to write as a file or folder name: no path, no "."
or "..", and only printable ASCII that every desktop file system takes as
a plain name (a disc's maps folder holds .map files and loading.tga) */
static int name_is_plain(const char *name)
{
	const char *at;

	if (!*name || !strcmp(name, ".") || !strcmp(name, ".."))
		return 0;
	for (at = name; *at; at++)
	{
		unsigned char c = (unsigned char)*at;

		if (c < 0x20 || c > 0x7e || strchr("/\\:*?\"<>|", c))
			return 0;
	}
	return 1;
}

static int same_name(const char *a, const char *b)
{
	for (;; a++, b++)
	{
		int x = *a >= 'A' && *a <= 'Z' ? *a - 'A' + 'a' : *a;
		int y = *b >= 'A' && *b <= 'Z' ? *b - 'A' + 'a' : *b;

		if (x != y)
			return 0;
		if (!x)
			return 1;
	}
}

static int list_add(struct xiso_list *list, const struct xiso_entry *entry)
{
	if (list->count == list->capacity)
	{
		int capacity = list->capacity ? list->capacity * 2 : 64;
		struct xiso_entry *entries;

		if (capacity > MAXIMUM_FILES)
			capacity = MAXIMUM_FILES;
		if (list->count == capacity)
			return 0;
		entries = realloc(list->entries, (size_t)capacity * sizeof(*entries));
		if (!entries)
			return 0;
		list->entries = entries;
		list->capacity = capacity;
	}
	list->entries[list->count++] = *entry;
	return 1;
}

/* adds the entries of the subtree at offset (in 4-byte units) to list, in
order, their paths after prefix; a damaged table (an entry past its end, a
tree that loops) sets list->damaged. Names that cannot be written as they
are (see name_is_plain) are left out */
static void walk_tree(const unsigned char *table, unsigned int size, unsigned int offset, int depth,
	const char *prefix, struct xiso_list *list)
{
	const unsigned char *entry;
	unsigned int position = offset * 4;
	unsigned int length;
	struct xiso_entry found;
	char name[256];

	if (list->damaged)
		return;
	if (depth > MAXIMUM_TREE_DEPTH || position + ENTRY_HEADER_SIZE > size)
	{
		list->damaged = 1;
		return;
	}
	entry = table + position;
	length = entry[13];
	if (!length || position + ENTRY_HEADER_SIZE + length > size)
	{
		list->damaged = 1;
		return;
	}

	if (read_u16(entry))
		walk_tree(table, size, read_u16(entry), depth + 1, prefix, list);

	memcpy(name, entry + ENTRY_HEADER_SIZE, length);
	name[length] = 0;
	if (!name_is_plain(name))
	{
		platform_log("the disc image has a file whose name cannot be written; leaving it out");
	}
	else if ((size_t)snprintf(found.path, sizeof(found.path), "%s%s%s", prefix, *prefix ? "/" : "", name) >=
		sizeof(found.path))
	{
		list->damaged = 1;
		return;
	}
	else
	{
		found.sector = read_u32(entry + 4);
		found.size = read_u32(entry + 8);
		found.directory = (entry[12] & ATTRIBUTE_DIRECTORY) != 0;
		if (!list_add(list, &found))
		{
			list->damaged = 1;
			return;
		}
	}

	if (read_u16(entry + 2))
		walk_tree(table, size, read_u16(entry + 2), depth + 1, prefix, list);
}

/* adds a directory's entries to list, and with recurse everything in the
folders in it, after its own entries (so a folder always comes before what
it holds); 0 if a table cannot be read or is damaged */
static int list_directory(struct xiso_image *image, unsigned int sector, unsigned int size, const char *prefix,
	int recurse, int depth, struct xiso_list *list)
{
	unsigned char *table;
	int first = list->count;
	int last;
	int index;

	if (depth > MAXIMUM_FOLDER_DEPTH)
		return 0;
	table = read_table(image, sector, size);
	if (!table)
		return 0;
	if (size)
		walk_tree(table, size, 0, 0, prefix, list);
	free(table);
	if (list->damaged)
		return 0;
	if (!recurse)
		return 1;

	/* (by index: the list moves as it grows) */
	last = list->count;
	for (index = first; index < last; index++)
	{
		char path[MAXIMUM_PATH];

		if (!list->entries[index].directory)
			continue;
		strcpy(path, list->entries[index].path);
		if (!list_directory(image, list->entries[index].sector, list->entries[index].size, path, 1, depth + 1,
			list))
		{
			return 0;
		}
	}
	return 1;
}

/* the maps folder among the root's entries: "maps", or on a multi-language
European disc (no "maps", a "maps_xx" per language instead) the one for the
language the system is set to, else English's, else the first */
static const struct xiso_entry *find_maps_folder(const struct xiso_list *root)
{
	static const char *const variables[] = { "LC_ALL", "LC_MESSAGES", "LANG" };
	char wanted[8] = "maps_en";
	const struct xiso_entry *english = NULL, *first = NULL;
	size_t variable;
	int index;

	for (variable = 0; variable < sizeof(variables) / sizeof(variables[0]); variable++)
	{
		const char *value = getenv(variables[variable]);

		if (value && value[0] >= 'a' && value[0] <= 'z' && value[1] >= 'a' && value[1] <= 'z')
		{
			wanted[5] = value[0];
			wanted[6] = value[1];
			break;
		}
	}
	for (index = 0; index < root->count; index++)
	{
		if (root->entries[index].directory && same_name(root->entries[index].path, "maps"))
			return &root->entries[index];
	}
	for (index = 0; index < root->count; index++)
	{
		const struct xiso_entry *entry = &root->entries[index];
		char prefix[6];

		if (!entry->directory || strlen(entry->path) != 7)
			continue;
		memcpy(prefix, entry->path, 5);
		prefix[5] = 0;
		if (!same_name(prefix, "maps_"))
			continue;
		if (same_name(entry->path, wanted))
			return entry;
		if (!english && same_name(entry->path, "maps_en"))
			english = entry;
		if (!first)
			first = entry;
	}
	return english ? english : first;
}

/* copies one file's sectors to path */
static int copy_file(struct xiso_image *image, const struct xiso_entry *file, const char *path,
	unsigned char *buffer, unsigned long long *done, unsigned long long total, xiso_progress_proc progress,
	void *context, const volatile int *cancel)
{
	int output = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
	unsigned long long offset = (unsigned long long)file->sector * SECTOR_SIZE;
	unsigned int remaining = file->size;

	if (output < 0)
		return fail(image, "Could not write %s.", path);
	while (remaining > 0)
	{
		unsigned int count = remaining < COPY_BUFFER_SIZE ? remaining : COPY_BUFFER_SIZE;
		unsigned int written = 0;

		if (cancel && *cancel)
		{
			close(output);
			return fail(image, "Cancelled.%s", NULL);
		}
		if (!image_read(image, offset, buffer, count))
		{
			close(output);
			return fail(image, "Could not read %s from the disc image (is it complete?).", file->path);
		}
		while (written < count)
		{
			ssize_t result = write(output, buffer + written, count - written);

			if (result <= 0)
			{
				close(output);
				return fail(image, "Could not write %s (is the disk full?).", path);
			}
			written += (unsigned int)result;
		}
		offset += count;
		remaining -= count;
		*done += count;
		if (progress)
			progress(context, file->path, *done, total);
	}
	if (close(output) != 0)
		return fail(image, "Could not write %s (is the disk full?).", path);
	return 1;
}

static int name_has_ending(const char *name, const char *ending)
{
	size_t length = strlen(name), ending_length = strlen(ending);

	return length >= ending_length && same_name(name + length - ending_length, ending);
}

/* whether <directory>/<name> exists, whatever the case of its name */
static int file_exists(const char *directory, const char *name)
{
	char found[256];

	return posix_find_entry_case_insensitive(directory, name, found, sizeof(found)) != 0;
}

/* lists the .bik files of the disc's bink folder that <destination>/bink
lacks, into movies */
static void list_missing_movies(struct xiso_image *image, const struct xiso_list *root, const char *destination,
	struct xiso_list *movies)
{
	struct xiso_list all = { NULL, 0, 0, 0 };
	char folder[MAXIMUM_PATH];
	int index, movie;

	for (index = 0; index < root->count; index++)
	{
		const struct xiso_entry *entry = &root->entries[index];

		if (!entry->directory || !same_name(entry->path, "bink"))
			continue;
		if (list_directory(image, entry->sector, entry->size, "", 0, 0, &all))
		{
			snprintf(folder, sizeof(folder), "%s/bink", destination);
			for (movie = 0; movie < all.count; movie++)
			{
				if (!all.entries[movie].directory && name_has_ending(all.entries[movie].path, ".bik") &&
					!file_exists(folder, all.entries[movie].path))
					list_add(movies, &all.entries[movie]);
			}
		}
		break;
	}
	free(all.entries);
}

int xiso_is_image(const char *image_path)
{
	struct xiso_image image;
	unsigned int root_sector, root_size;
	char error[64];
	int result;

	image.error = error;
	image.error_size = sizeof(error);
	image.descriptor = open(image_path, O_RDONLY | O_LARGEFILE | O_CLOEXEC);
	if (image.descriptor < 0)
		return 0;
	result = find_volume(&image, &root_sector, &root_size);
	close(image.descriptor);
	return result;
}

/* reads the start of a file on the disc (at most size bytes, fewer for a
smaller file) into buffer; the bytes read, 0 if it cannot be read */
static unsigned int read_head(struct xiso_image *image, const struct xiso_entry *file, unsigned char *buffer,
	unsigned int size)
{
	unsigned int count = file->size < size ? file->size : size;

	if (!count || !image_read(image, (unsigned long long)file->sector * SECTOR_SIZE, buffer, count))
		return 0;
	return count;
}

/* the root's file named name, or NULL */
static const struct xiso_entry *find_file(const struct xiso_list *list, const char *name)
{
	int index;

	for (index = 0; index < list->count; index++)
	{
		if (!list->entries[index].directory && same_name(list->entries[index].path, name))
			return &list->entries[index];
	}
	return NULL;
}

/* whether the disc's default.xbe is Halo: Combat Evolved's (its
certificate's title ID): an XBE's base address at 0x104, its certificate's
address at 0x118, the title ID 8 bytes into the certificate */
static int xbe_is_halo(struct xiso_image *image, const struct xiso_entry *xbe)
{
	unsigned char head[XBE_HEAD_SIZE];
	unsigned int count = read_head(image, xbe, head, sizeof(head));
	unsigned int base, certificate, offset;

	if (count < 0x11C || memcmp(head, "XBEH", 4))
		return 0;
	base = read_u32(head + 0x104);
	certificate = read_u32(head + 0x118);
	if (certificate < base)
		return 0;
	offset = certificate - base;
	if (offset > count - 12)
		return 0;
	return read_u32(head + offset + 8) == HALO_TITLE_ID;
}

/* the cache version of a map's header ("head" and "foot" around it), 0 if
it is no map's */
static long map_header_version(struct xiso_image *image, const struct xiso_entry *map)
{
	unsigned char header[MAP_HEADER_SIZE];

	if (read_head(image, map, header, sizeof(header)) != sizeof(header) || memcmp(header, "daeh", 4) ||
		memcmp(header + MAP_HEADER_SIZE - 4, "toof", 4))
	{
		return 0;
	}
	return (long)read_u32(header + 4);
}

/* what a copy takes from an image: its root, the maps folder's files (the
disc's loading picture named xbox_loading.tga), the movies the destination
lacks (with XISO_EXTRACT_MOVIES), and their bytes. Checked to be Halo's
Xbox disc: its default.xbe's title ID, and an Xbox ui.map */
struct xiso_contents
{
	struct xiso_list root;
	struct xiso_list files;
	struct xiso_list movies;
	unsigned long long total;
};

static void contents_free(struct xiso_contents *contents)
{
	free(contents->root.entries);
	free(contents->files.entries);
	free(contents->movies.entries);
	memset(contents, 0, sizeof(*contents));
}

static int read_contents(struct xiso_image *image, const char *destination, int flags,
	struct xiso_contents *contents)
{
	static const char not_halo[] = "This isn't a Halo: Combat Evolved disc for the original Xbox.%s";
	const struct xiso_entry *maps, *xbe, *ui;
	unsigned int root_sector, root_size;
	long version;
	int index;

	memset(contents, 0, sizeof(*contents));
	if (!find_volume(image, &root_sector, &root_size))
		return 0;

	/* the root's maps folder */
	if (!list_directory(image, root_sector, root_size, "", 0, 0, &contents->root))
		return fail(image, "The disc image's file system is damaged.%s", NULL);
	xbe = find_file(&contents->root, "default.xbe");
	if (!xbe || !xbe_is_halo(image, xbe))
		return fail(image, not_halo, NULL);
	maps = find_maps_folder(&contents->root);
	if (!maps)
		return fail(image, "The disc image has no maps folder: it is not a Halo disc.%s", NULL);
	if (!same_name(maps->path, "maps"))
		platform_log("the disc image has no maps folder but one per language; extracting %s", maps->path);

	/* everything in it */
	if (!list_directory(image, maps->sector, maps->size, "", 1, 0, &contents->files))
		return fail(image, "The disc image's maps folder is damaged.%s", NULL);
	ui = find_file(&contents->files, "ui.map");
	if (!ui)
		return fail(image, "The disc image's maps folder has no ui.map: it is not a Halo disc.%s", NULL);
	/* (the Xbox's maps are cache version 5; Halo PC's 7, Custom Edition's
	609: the desktops take those from their own discs, pc_disc.c) */
	version = map_header_version(image, ui);
	if (version != XBOX_MAP_VERSION)
	{
		return fail(image, version == 7 || version == 609 ?
			"The disc image's maps are Halo PC's, not the Xbox's.%s" : not_halo, NULL);
	}
	for (index = 0; index < contents->files.count; index++)
	{
		if (contents->files.entries[index].directory)
			continue;
		contents->total += contents->files.entries[index].size;
		/* (the disc's loading picture is kept as xbox_loading.tga: the game
		shows Chupathingy's own unless display.xbox_loading_picture is on,
		port/linux/game/loading_picture.c) */
		if (same_name(contents->files.entries[index].path, "loading.tga"))
			strcpy(contents->files.entries[index].path, "xbox_loading.tga");
	}

	if (flags & XISO_EXTRACT_MOVIES)
		list_missing_movies(image, &contents->root, destination, &contents->movies);
	for (index = 0; index < contents->movies.count; index++)
		contents->total += contents->movies.entries[index].size;
	return 1;
}

/* the files written to <destination>/maps.partial, first to count, removed
again (from the last, so a folder is empty when its turn comes), and the
folder itself: a copy that fails or is cancelled leaves nothing behind */
static void remove_partial(const char *partial, const struct xiso_list *files, int count)
{
	char path[MAXIMUM_PATH * 2];
	int index;

	for (index = count - 1; index >= 0; index--)
	{
		snprintf(path, sizeof(path), "%s/%s", partial, files->entries[index].path);
		if (files->entries[index].directory)
			rmdir(path);
		else
			unlink(path);
	}
	rmdir(partial);
}

int xiso_check_image(const char *image_path, const char *destination, int flags, unsigned long long *bytes,
	char *error, int error_size)
{
	struct xiso_image image;
	struct xiso_contents contents;
	int result;

	image.error = error;
	image.error_size = error_size;
	if (bytes)
		*bytes = 0;
	image.descriptor = open(image_path, O_RDONLY | O_LARGEFILE | O_CLOEXEC);
	if (image.descriptor < 0)
		return fail(&image, "Could not open %s.", image_path);
	result = read_contents(&image, destination, flags, &contents);
	if (result && bytes)
		*bytes = contents.total;
	contents_free(&contents);
	close(image.descriptor);
	return result;
}

int xiso_extract_maps_with(const char *image_path, const char *destination, int flags, const volatile int *cancel,
	xiso_progress_proc progress, void *context, char *error, int error_size)
{
	struct xiso_image image;
	struct xiso_contents contents;
	unsigned char *buffer = NULL;
	unsigned long long done = 0;
	char partial[MAXIMUM_PATH], final[MAXIMUM_PATH], path[MAXIMUM_PATH * 2];
	int result = 0;
	int written = 0;
	int index;

	image.error = error;
	image.error_size = error_size;
	memset(&contents, 0, sizeof(contents));
	image.descriptor = open(image_path, O_RDONLY | O_LARGEFILE | O_CLOEXEC);
	if (image.descriptor < 0)
		return fail(&image, "Could not open %s.", image_path);
	buffer = malloc(COPY_BUFFER_SIZE);
	if (!buffer)
	{
		fail(&image, "Out of memory.%s", NULL);
		goto done;
	}
	if (!read_contents(&image, destination, flags, &contents))
		goto done;

	/* room for it all, and some to spare, as the console packager asks
	(tools/console-packager): not when the file system cannot say, nor for
	a port that asks the device itself (XISO_EXTRACT_NO_SPACE_CHECK) */
	if (!(flags & XISO_EXTRACT_NO_SPACE_CHECK))
	{
		posix_ulong free_low, free_high, total_low, total_high;

		if (posix_disk_space(destination, &free_low, &free_high, &total_low, &total_high) == 0 &&
			(total_low || total_high))
		{
			unsigned long long available = (unsigned long long)free_high << 32 | free_low;

			if (available < contents.total + SPARE_SPACE)
			{
				char sizes[96];

				snprintf(sizes, sizeof(sizes), "%llu MB, and there are %llu MB free",
					(contents.total + SPARE_SPACE + (1 << 20) - 1) >> 20, available >> 20);
				fail(&image, "There isn't room for the maps: they need %s.", sizes);
				goto done;
			}
		}
	}

	snprintf(partial, sizeof(partial), "%s/maps.partial", destination);
	snprintf(final, sizeof(final), "%s/maps", destination);
	posix_make_directory(partial);
	for (index = 0; index < contents.files.count; index++)
	{
		const struct xiso_entry *entry = &contents.files.entries[index];

		snprintf(path, sizeof(path), "%s/%s", partial, entry->path);
		written = index + 1;
		if (entry->directory)
		{
			posix_make_directory(path);
			continue;
		}
		platform_log("extracting maps/%s (%u bytes)", entry->path, entry->size);
		if (!copy_file(&image, entry, path, buffer, &done, contents.total, progress, context, cancel))
		{
			remove_partial(partial, &contents.files, written);
			goto done;
		}
	}
	/* (over an empty maps folder, as the first start's "make the folders"
	leaves: sdl_platform.c) */
	if (rename(partial, final) != 0 && (rmdir(final) != 0 || rename(partial, final) != 0))
	{
		fail(&image, "Could not create %s (move the maps folder there aside).", final);
		goto done;
	}
	result = 1;

	/* the movies, each as <name>.partial first, so a copy that stops never
	leaves a short file the next start would take for the movie */
	if (contents.movies.count)
	{
		snprintf(final, sizeof(final), "%s/bink", destination);
		posix_make_directory(final);
	}
	for (index = 0; index < contents.movies.count; index++)
	{
		const struct xiso_entry *entry = &contents.movies.entries[index];
		char movie[MAXIMUM_PATH * 2];

		snprintf(path, sizeof(path), "%s/%s.partial", final, entry->path);
		snprintf(movie, sizeof(movie), "%s/%s", final, entry->path);
		platform_log("extracting bink/%s (%u bytes)", entry->path, entry->size);
		if (!copy_file(&image, entry, path, buffer, &done, contents.total, progress, context, cancel) ||
			rename(path, movie) != 0)
		{
			/* (the maps are all there: the game plays without movies) */
			platform_log("could not copy bink/%s; the game skips it", entry->path);
			unlink(path);
			break;
		}
	}

done:
	close(image.descriptor);
	contents_free(&contents);
	free(buffer);
	return result;
}

int xiso_extract_maps(const char *image_path, const char *destination, xiso_progress_proc progress, void *context,
	char *error, int error_size)
{
	return xiso_extract_maps_with(image_path, destination, XISO_EXTRACT_MOVIES, NULL, progress, context, error,
		error_size);
}

#endif
