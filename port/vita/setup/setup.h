/*
SETUP.H

The first start's screens (setup_flow.c, drawn by setup_draw.c): before the
game, when the data folder has no Xbox maps, the game files on the memory
card are found and offered - Halo CE Vita's maps read where they are, or a
disc image's maps copied into the data folder - and when there are none the
player is told what to put where. The same screens show the updater's
question, its progress and a failure (the console hooks of the game's
port/linux/include/halo_console_features.h, HALO_CONSOLE_SETUP_SCREENS:
host/vita_setup.c).

Plain C over a small system interface (struct setup_system), so the screens
and the flow are the same in the Linux test (tests/setup_test.c), which
draws them into PNG files and presses the buttons from a script. The
picture is 960x544, four bytes a pixel in the Vita's display order (R, G,
B, A in memory).
*/

#ifndef WARTHOG_VITA_SETUP_H
#define WARTHOG_VITA_SETUP_H

#define SETUP_WIDTH 960
#define SETUP_HEIGHT 544

/* the buttons, as setup_system.buttons gives them held */
enum
{
	SETUP_BUTTON_UP = 1 << 0,
	SETUP_BUTTON_DOWN = 1 << 1,
	SETUP_BUTTON_LEFT = 1 << 2,
	SETUP_BUTTON_RIGHT = 1 << 3,
	SETUP_BUTTON_CROSS = 1 << 4,
	SETUP_BUTTON_CIRCLE = 1 << 5,
	SETUP_BUTTON_TRIANGLE = 1 << 6,
	SETUP_BUTTON_SQUARE = 1 << 7,
	SETUP_BUTTON_START = 1 << 8,
};

struct setup_system
{
	/* shows the picture (SETUP_WIDTH x SETUP_HEIGHT, rows from the top) */
	void (*present)(const unsigned char *pixels);
	/* the buttons held now (SETUP_BUTTON_*) */
	unsigned int (*buttons)(void);
	unsigned long long (*now_ms)(void);
	void (*sleep_ms)(unsigned int milliseconds);
	/* the free bytes of the device that holds path, or -1 if it can't say */
	long long (*free_space)(const char *path);
	/* a copy running (on) or done: the system kept from sleeping and its
	home button held off for it */
	void (*busy)(int on);
	/* called about once a second while a copy runs */
	void (*keep_awake)(void);
	void (*log)(const char *line);
	/* the system's confirm button is circle (Japan's), not cross */
	int circle_confirms;
	/* the version shown in the corner */
	const char *version;
};

/* where the flow looks (paths without a closing slash) */
struct setup_places
{
	/* the app's data folder, whose maps folder the game reads first and
	where a disc image's maps are copied */
	const char *data_folder;
	/* other maps folders read where they are (another app's), in order: a
	folder, then the name the screen gives its maps ("Halo CE Vita's
	maps"), maps_folder_count pairs */
	const char *const *maps_folders;
	int maps_folder_count;
	/* folders looked in for disc images (.iso, .xiso), in order */
	const char *const *image_folders;
	int image_folder_count;
	/* the file the player's choice of a maps folder elsewhere is kept in */
	const char *choice_file;
};

enum
{
	/* the data folder has the maps (found, or copied in now) */
	SETUP_MAPS_OWN,
	/* the maps are read from another folder (maps_root) */
	SETUP_MAPS_ELSEWHERE,
	/* the player quit */
	SETUP_MAPS_QUIT,
};

/* the maps found (or offered and copied, or chosen); maps_root the folder
for SETUP_MAPS_ELSEWHERE. No screen is shown when the data folder has the
maps or a kept choice still holds them */
int setup_find_maps(const struct setup_system *system, const struct setup_places *places, char *maps_root,
	int maps_root_size);

/* whether folder/ui.map is an Xbox map ("head", cache version 5, "foot") */
int setup_folder_has_xbox_maps(const char *folder);

/* the updater's screens (console_setup_update_question and the others) */
int setup_update_question(const struct setup_system *system, const char *latest, const char *current);
void setup_notice(const struct setup_system *system, const char *first, const char *second);
void *setup_progress_open(const struct setup_system *system);
void setup_progress_show(void *screen, float fraction, const char *title, const char *line);
void setup_progress_close(void *screen);

/* the screens' memory given back (the picture, the letters) */
void setup_release(void);

#endif
