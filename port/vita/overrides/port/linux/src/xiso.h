/*
XISO.H

The maps folder and the Bink movies out of an Xbox disc image (xiso.c).
*/

#ifndef __HALO_LINUX_XISO_H
#define __HALO_LINUX_XISO_H

/* called as the copy goes: the file being copied, and the bytes copied of
all the files' */
typedef void (*xiso_progress_proc)(void *context, const char *file, unsigned long long done,
	unsigned long long total);

/* whether the file is an Xbox disc image (of the game partition or a whole
disc) */
int xiso_is_image(const char *image_path);

/* what xiso_extract_maps_with and xiso_check_image do besides the maps */
enum
{
	/* the .bik files of the disc's bink folder that <destination>/bink lacks
	(a port that plays no Bink movies leaves it out) */
	XISO_EXTRACT_MOVIES = 1,
	/* no check of the destination's free space (a port that asks its device
	itself, before the copy) */
	XISO_EXTRACT_NO_SPACE_CHECK = 2,
};

/* whether the image is Halo: Combat Evolved's Xbox disc (default.xbe's
title ID, an Xbox ui.map in its maps folder): nonzero with the bytes the
copy would write in bytes (the maps, and with XISO_EXTRACT_MOVIES the
movies destination lacks), else 0 with the reason (for the player) in
error */
int xiso_check_image(const char *image_path, const char *destination, int flags, unsigned long long *bytes,
	char *error, int error_size);

/* copies the image's maps folder to <destination>/maps, after the checks of
xiso_check_image and of the free space, then the movies with
XISO_EXTRACT_MOVIES (a movie that cannot be copied does not fail it).
cancel (or NULL) is read between blocks: once it is nonzero the copy
stops. A copy that fails or is cancelled removes the files it wrote.
Returns nonzero on success, or 0 with the reason (for the player) in
error. Called from any thread */
int xiso_extract_maps_with(const char *image_path, const char *destination, int flags, const volatile int *cancel,
	xiso_progress_proc progress, void *context, char *error, int error_size);

/* xiso_extract_maps_with, the movies too and no cancel */
int xiso_extract_maps(const char *image_path, const char *destination, xiso_progress_proc progress, void *context,
	char *error, int error_size);

#endif
