/*
VITA_FOLDER.H

The game's own folder on the Vita: its maps, saves, settings, logs, shader
cache, update downloads and the Steam download's files. It was
ux0:data/haloce-vita alone; since 1.1.3 (issue #53) it can be on the card
an SD2Vita mounts as uma0: (the official memory card staying ux0:), or on
imc0: (the PS Vita 2000's internal storage).

It is chosen once, at start, before anything is read or written
(vita_main.c), and stays for the run:

1. the player's choice, the settings panel's Game folder row: kept in
   VITA_FOLDER_CHOICE_FILE (one word: auto, ux0, uma0 or imc0). That file
   is outside every game folder, on ux0:, which every Vita has. A device
   named there that is not mounted falls back to auto (halo.log says so).
2. auto: the first of ux0:, uma0:, imc0: whose data/haloce-vita has the
   game's files (an Xbox maps/ui.map, or MCC's mcc/halo1/maps/ui.map);
   else the first of them whose data/haloce-vita folder exists; else
   ux0:data/haloce-vita (a new install).

Nothing is ever moved between cards. The choice logic has no Vita calls
in it (vita_folder.c; a desktop test, port/vita/tests/vita_folder_test.c):
the files are looked at with the C library's stat, as newlib's on the Vita.
*/

#ifndef VITA_FOLDER_H
#define VITA_FOLDER_H

#include <stddef.h>

#define VITA_FOLDER_DEFAULT "ux0:data/haloce-vita"
/* the player's choice (the settings panel's Game folder row) */
#define VITA_FOLDER_CHOICE_FILE "ux0:data/haloce-vita-folder.txt"
/* the longest folder name ("imc0:data/haloce-vita" and a margin) */
#define VITA_FOLDER_SIZE 64

enum
{
	VITA_FOLDER_AUTO,
	VITA_FOLDER_UX0,
	VITA_FOLDER_UMA0,
	VITA_FOLDER_IMC0,
	VITA_FOLDER_CHOICES
};

/* the folder chosen (VITA_FOLDER_DEFAULT until vita_folder_start), no
trailing '/' */
const char *vita_folder(void);
/* its device, "uma0:" */
const char *vita_folder_device(void);
/* a file in it: "<folder>/<name>" into path; path */
char *vita_folder_path(char *path, size_t size, const char *name);
/* the folder used from now on (vita_folder_start's; a test's) */
void vita_folder_set(const char *folder);

/* the choice's word ("auto", "ux0", ...); a choice from one (case not
minded, a trailing ':' or white space allowed), -1 for none */
const char *vita_folder_choice_word(int choice);
int vita_folder_choice_parse(const char *text);
/* the folder of a device choice ("uma0:data/haloce-vita"); NULL for auto */
const char *vita_folder_of_choice(int choice);

/* whether path exists (a file or a folder) */
typedef int (*vita_folder_exists_function)(const char *path, void *context);

/* the choice made: choice (a VITA_FOLDER_* the player made, or AUTO) with
exists to look at the cards; the folder into folder, a line saying why into
why (either may be NULL). Returns the device choice the folder is
(VITA_FOLDER_UX0 ...) */
int vita_folder_choose(int choice, vita_folder_exists_function exists, void *context, char *folder, size_t folder_size,
	char *why, size_t why_size);

/* the player's choice as kept in VITA_FOLDER_CHOICE_FILE (AUTO when there
is none, or it says nothing taken) */
int vita_folder_choice_read(void);
/* kept: nonzero once written (AUTO removes the file) */
int vita_folder_choice_write(int choice);
/* the choice read, the cards looked at with stat, the folder set; why says
how (for halo.log) */
void vita_folder_start(char *why, size_t why_size);

#endif
