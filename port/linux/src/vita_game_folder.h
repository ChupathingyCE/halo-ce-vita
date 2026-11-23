/*
VITA_GAME_FOLDER.H

The Vita's game folder for the shared code built into the Vita's game
(HALO_VITA): ux0:data/haloce-vita, or the uma0: or imc0: one the Vita's
start chose (port/vita/include/vita_folder.h, issue #53). vita_main.c puts
it in HALO_VITA_FOLDER before anything else runs; without it (a desktop
test built as the Vita's) it is ux0:data/haloce-vita.
*/

#ifndef VITA_GAME_FOLDER_H
#define VITA_GAME_FOLDER_H

#include <stdlib.h>

#define VITA_GAME_FOLDER_VARIABLE "HALO_VITA_FOLDER"
#define VITA_GAME_FOLDER_DEFAULT "ux0:data/haloce-vita"

static inline const char *vita_game_folder(void)
{
	const char *folder = getenv(VITA_GAME_FOLDER_VARIABLE);

	return folder && folder[0] ? folder : VITA_GAME_FOLDER_DEFAULT;
}

#endif
