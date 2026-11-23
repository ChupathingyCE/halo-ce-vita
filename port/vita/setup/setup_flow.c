/*
SETUP_FLOW.C

The first start (setup.h): what is on the memory card, offered to the
player, and a disc image's maps copied with the screen up.

	the data folder's maps/ui.map an Xbox map     the game starts, no screen
	a kept choice (choice_file) still good        read from there, no screen
	one maps folder elsewhere, no disc image      read from there, no screen
	maps folders elsewhere, disc images found     SET UP WARTHOG: each one a
	                                              row, the ones that can't be
	                                              used dimmed with the reason
	nothing usable                                MAPS NOT FOUND: where the
	                                              maps folder goes, the maps
	                                              it needs, where a disc image
	                                              can go

A disc image is checked by the game's own disc reader (port/linux/src/
xiso.c, xiso_check_image): Halo's title ID in default.xbe and an Xbox
ui.map, else the reason is shown. Its maps are copied by the same reader on
a thread of their own (xiso_extract_maps_with) into the data folder's
maps.partial, which becomes maps once all of them are there; the copy can
be cancelled, and a copy that stops leaves nothing behind. The free space
is checked first (the maps and 64 MB more, as the game's console packager
asks). The loading picture is kept as xbox_loading.tga; the movies are not
copied (the Vita plays none yet). Nothing is written outside the data
folder: other apps' folders and the disc image are only read.
*/

#include "setup.h"
#include "setup_draw.h"
#include "xiso.h"

#include <ctype.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	MAXIMUM_CANDIDATES = 16,
	VISIBLE_ROWS = 4,
	ROW_HEIGHT = 66,
	LIST_TOP = 196,
	/* the copy's room to spare (the console packager's) */
	SPARE_BYTES_MB = 64,
	PASS_MILLISECONDS = 16,
	/* a held direction repeats after this, then every REPEAT */
	REPEAT_DELAY = 400,
	REPEAT_EVERY = 120,
	/* the update question waits no longer for a player who isn't there */
	QUESTION_MILLISECONDS = 60000,
	DONE_MILLISECONDS = 8000,
	COPY_STACK_BYTES = 256 * 1024,
};

/* the maps an Xbox disc's maps folder has (the game reads ui.map first) */
static const char needed_maps[] =
	"ui.map, the campaign's a10 a30 a50 b30 b40 c10 c20 c40 d20 d40, and the multiplayer maps beavercreek "
	"bloodgulch boardingaction carousel chillout damnation hangemhigh longest prisoner putput ratrace "
	"sidewinder wizard";

enum
{
	CANDIDATE_FOLDER,
	CANDIDATE_IMAGE,
};

struct candidate
{
	int kind;
	int usable;
	char path[512];
	char name[160];
	char detail[320];
	char reason[256];
	unsigned long long bytes;
};

struct flow
{
	const struct setup_system *system;
	const struct setup_places *places;
	struct candidate candidates[MAXIMUM_CANDIDATES];
	int count;
	int usable;
	int selected;
	int top;
	unsigned int held;
	unsigned long long held_since;
	unsigned long long repeated_at;
	/* a line said under the list once (a cancelled copy) */
	char note[256];
};

/* ---------- small things */

static void log_line(const struct setup_system *system, const char *format, const char *a, const char *b)
{
	char line[1200];

	snprintf(line, sizeof(line), format, a ? a : "", b ? b : "");
	if (system->log)
		system->log(line);
}

/* a size in MB with its thousands (1,774 MB) */
static void megabytes(unsigned long long bytes, char *text, size_t size)
{
	unsigned long long mb = (bytes + (1 << 20) - 1) >> 20;

	if (mb >= 1000)
		snprintf(text, size, "%llu,%03llu MB", mb / 1000, mb % 1000);
	else
		snprintf(text, size, "%llu MB", mb);
}

static const char *file_name(const char *path)
{
	const char *slash = strrchr(path, '/');
	const char *colon = strrchr(path, ':');

	if (slash)
		return slash + 1;
	return colon ? colon + 1 : path;
}

static int ends_with(const char *name, const char *ending)
{
	size_t length = strlen(name), ending_length = strlen(ending);
	size_t index;

	if (length < ending_length)
		return 0;
	for (index = 0; index < ending_length; index++)
	{
		if (tolower((unsigned char)name[length - ending_length + index]) != tolower((unsigned char)ending[index]))
			return 0;
	}
	return 1;
}

/* a folder and a name joined: "ux0:" + "halo.iso" is "ux0:halo.iso" */
static void join(char *out, size_t size, const char *folder, const char *name)
{
	size_t length = strlen(folder);

	snprintf(out, size, "%s%s%s", folder, length && (folder[length - 1] == ':' || folder[length - 1] == '/') ?
		"" : "/", name);
}

int setup_folder_has_xbox_maps(const char *folder)
{
	unsigned char header[0x800];
	char path[600];
	FILE *file;
	int xbox = 0;

	join(path, sizeof(path), folder, "ui.map");
	file = fopen(path, "rb");
	if (!file)
		return 0;
	if (fread(header, 1, sizeof(header), file) == sizeof(header))
	{
		xbox = !memcmp(header, "daeh", 4) && !memcmp(header + 0x7FC, "toof", 4) &&
			(header[4] | header[5] << 8 | header[6] << 16 | (unsigned int)header[7] << 24) == 5;
	}
	fclose(file);
	return xbox;
}

static int file_exists(const char *folder, const char *name)
{
	char path[600];
	FILE *file;

	join(path, sizeof(path), folder, name);
	file = fopen(path, "rb");
	if (file)
		fclose(file);
	return file != NULL;
}

/* ---------- the buttons */

static unsigned int confirm_button(const struct flow *flow)
{
	return flow->system->circle_confirms ? SETUP_BUTTON_CIRCLE : SETUP_BUTTON_CROSS;
}

static unsigned int back_button(const struct flow *flow)
{
	return flow->system->circle_confirms ? SETUP_BUTTON_CROSS : SETUP_BUTTON_CIRCLE;
}

static int confirm_glyph(const struct setup_system *system)
{
	return system->circle_confirms ? SETUP_GLYPH_CIRCLE : SETUP_GLYPH_CROSS;
}

static int back_glyph(const struct setup_system *system)
{
	return system->circle_confirms ? SETUP_GLYPH_CROSS : SETUP_GLYPH_CIRCLE;
}

/* the buttons pressed since the last pass (a held direction repeating) */
static unsigned int pressed(struct flow *flow)
{
	unsigned int now = flow->system->buttons();
	unsigned int fresh = now & ~flow->held;
	unsigned long long time = flow->system->now_ms();
	const unsigned int directions = SETUP_BUTTON_UP | SETUP_BUTTON_DOWN;

	if (fresh & directions)
	{
		flow->held_since = time;
		flow->repeated_at = time;
	}
	else if ((now & directions) && time - flow->held_since >= REPEAT_DELAY && time - flow->repeated_at >= REPEAT_EVERY)
	{
		fresh |= now & directions;
		flow->repeated_at = time;
	}
	flow->held = now;
	return fresh;
}

/* the prompts along the bottom, from the left */
static void prompts(const struct setup_system *system, int count, const int *glyph, const char *const *label)
{
	float x = 40.0f;
	int index;

	setup_draw_rectangle(40, 486, SETUP_WIDTH - 80, 1, SETUP_COLOR_SEPARATOR);
	for (index = 0; index < count; index++)
		x += setup_draw_button(x, 520.0f, glyph[index], label[index]) + 34.0f;
	(void)system;
}

static void present(const struct setup_system *system)
{
	if (setup_draw_pixels())
		system->present(setup_draw_pixels());
}

/* ---------- what is on the card */

static void add_folder(struct flow *flow, const char *folder, const char *label)
{
	struct candidate *candidate;
	int index;

	if (flow->count >= MAXIMUM_CANDIDATES || !file_exists(folder, "ui.map"))
		return;
	for (index = 0; index < flow->count; index++)
	{
		if (!strcmp(flow->candidates[index].path, folder))
			return;
	}
	candidate = &flow->candidates[flow->count++];
	memset(candidate, 0, sizeof(*candidate));
	candidate->kind = CANDIDATE_FOLDER;
	snprintf(candidate->path, sizeof(candidate->path), "%s", folder);
	snprintf(candidate->name, sizeof(candidate->name), "Use %s where they are", label);
	snprintf(candidate->detail, sizeof(candidate->detail), "%s  -  nothing is copied", folder);
	candidate->usable = setup_folder_has_xbox_maps(folder);
	if (!candidate->usable)
		snprintf(candidate->reason, sizeof(candidate->reason), "Its ui.map isn't the Xbox's.");
	log_line(flow->system, "setup: maps folder %s%s", folder, candidate->usable ? "" : " (not the Xbox's maps)");
}

static void add_image(struct flow *flow, const char *path)
{
	struct candidate *candidate;
	char error[256] = "";
	char size[32];
	int index;

	if (flow->count >= MAXIMUM_CANDIDATES)
		return;
	for (index = 0; index < flow->count; index++)
	{
		if (!strcmp(flow->candidates[index].path, path))
			return;
	}
	candidate = &flow->candidates[flow->count++];
	memset(candidate, 0, sizeof(*candidate));
	candidate->kind = CANDIDATE_IMAGE;
	snprintf(candidate->path, sizeof(candidate->path), "%s", path);
	snprintf(candidate->name, sizeof(candidate->name), "Copy the maps from %s", file_name(path));
	candidate->usable = xiso_check_image(path, flow->places->data_folder, 0, &candidate->bytes, error,
		sizeof(error)) != 0;
	if (candidate->usable)
	{
		megabytes(candidate->bytes, size, sizeof(size));
		snprintf(candidate->detail, sizeof(candidate->detail), "%s  -  %s into %s/maps", path, size,
			flow->places->data_folder);
	}
	else
	{
		snprintf(candidate->detail, sizeof(candidate->detail), "%s", path);
		snprintf(candidate->reason, sizeof(candidate->reason), "%s", error);
	}
	log_line(flow->system, "setup: disc image %s: %s", path, candidate->usable ? "Halo's Xbox disc" : error);
}

static void scan_images(struct flow *flow, const char *folder)
{
	DIR *directory = opendir(folder);
	struct dirent *entry;
	char names[MAXIMUM_CANDIDATES][256];
	int count = 0, index;

	if (!directory)
		return;
	while ((entry = readdir(directory)) != NULL && count < MAXIMUM_CANDIDATES)
	{
		if (entry->d_name[0] == '.' || !(ends_with(entry->d_name, ".iso") || ends_with(entry->d_name, ".xiso")))
			continue;
		snprintf(names[count++], sizeof(names[0]), "%s", entry->d_name);
	}
	closedir(directory);
	/* (in name order: the card's order is the order they were written) */
	qsort(names, (size_t)count, sizeof(names[0]), (int (*)(const void *, const void *))strcmp);
	for (index = 0; index < count; index++)
	{
		char path[600];

		join(path, sizeof(path), folder, names[index]);
		add_image(flow, path);
	}
}

static void scan(struct flow *flow)
{
	int index;

	flow->count = 0;
	flow->usable = 0;
	flow->selected = 0;
	flow->top = 0;
	for (index = 0; index < flow->places->maps_folder_count; index++)
	{
		const char *folder = flow->places->maps_folders[index * 2];
		const char *label = flow->places->maps_folders[index * 2 + 1];

		add_folder(flow, folder, label);
	}
	for (index = 0; index < flow->places->image_folder_count; index++)
		scan_images(flow, flow->places->image_folders[index]);
	for (index = 0; index < flow->count; index++)
		flow->usable += flow->candidates[index].usable;
	/* (the first that can be used, selected) */
	for (index = 0; index < flow->count; index++)
	{
		if (flow->candidates[index].usable)
		{
			flow->selected = index;
			break;
		}
	}
}

/* ---------- the screens */

static void draw_notice(const struct setup_system *system, const char *title, const char *first, const char *second,
	const int *glyph, const char *const *label, int count)
{
	float baseline;

	if (!setup_draw_begin(system->version))
		return;
	setup_draw_title(60.0f, 140.0f, SETUP_ALIGN_LEFT, title);
	setup_draw_panel(40, 170, SETUP_WIDTH - 80, 280);
	baseline = setup_draw_wrapped(SETUP_FONT_TEXT, 26.0f, 70.0f, 222.0f, SETUP_WIDTH - 140, 34.0f, SETUP_COLOR_TEXT,
		first ? first : "");
	if (second && *second)
		setup_draw_wrapped(SETUP_FONT_TEXT, 22.0f, 70.0f, baseline + 12.0f, SETUP_WIDTH - 140, 30.0f,
			SETUP_COLOR_STATUS, second);
	prompts(system, count, glyph, label);
	present(system);
}

/* a notice until the confirm button (or seconds without one, 0: none) */
static void notice_wait(struct flow *flow, const char *title, const char *first, const char *second,
	unsigned long long milliseconds)
{
	const struct setup_system *system = flow->system;
	const int glyph[] = { confirm_glyph(system) };
	const char *const label[] = { "Continue" };
	unsigned long long start = system->now_ms();

	log_line(system, "setup: %s %s", first, second);
	draw_notice(system, title, first, second, glyph, label, 1);
	flow->held = system->buttons();
	for (;;)
	{
		if (pressed(flow) & (confirm_button(flow) | back_button(flow)))
			break;
		if (milliseconds && system->now_ms() - start >= milliseconds)
			break;
		system->sleep_ms(PASS_MILLISECONDS);
	}
}

static void draw_missing(struct flow *flow)
{
	const struct setup_system *system = flow->system;
	const int glyph[] = { SETUP_GLYPH_TRIANGLE, back_glyph(system) };
	const char *const label[] = { "Look again", "Quit" };
	char text[1024];
	float baseline;
	int index, shown = 0;

	if (!setup_draw_begin(system->version))
		return;
	setup_draw_title(60.0f, 120.0f, SETUP_ALIGN_LEFT, "MAPS NOT FOUND");
	setup_draw_text(SETUP_FONT_TEXT, 22.0f, 62.0f, 156.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_TEXT_DIM,
		"Warthog plays the maps from your Halo: Combat Evolved disc for the original Xbox.");
	setup_draw_panel(40, 174, SETUP_WIDTH - 80, 300);
	setup_draw_text(SETUP_FONT_TEXT, 22.0f, 64.0f, 208.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_TEXT,
		"Put the disc's maps folder here:");
	snprintf(text, sizeof(text), "%s/maps", flow->places->data_folder);
	setup_draw_text(SETUP_FONT_TEXT, 24.0f, 64.0f, 238.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_LABEL, text);
	snprintf(text, sizeof(text), "It needs %s (NTSC 01.10.12.2276).", needed_maps);
	baseline = setup_draw_wrapped(SETUP_FONT_TEXT, 18.0f, 64.0f, 266.0f, SETUP_WIDTH - 128, 23.0f,
		SETUP_COLOR_STATUS, text);
	setup_draw_rectangle(64, (int)baseline - 8, SETUP_WIDTH - 128, 1, SETUP_COLOR_SEPARATOR);
	text[0] = 0;
	for (index = 0; index < flow->places->image_folder_count; index++)
	{
		size_t length = strlen(text);

		snprintf(text + length, sizeof(text) - length, "%s%s%s", index ? (index + 1 ==
			flow->places->image_folder_count ? " or " : ", ") : "", flow->places->image_folders[index],
			strchr(flow->places->image_folders[index], '/') ? "/" : "");
	}
	{
		char line[1200];

		snprintf(line, sizeof(line), "Or put your disc image (.iso or .xiso) in %s, and Warthog copies the maps "
			"from it.", text);
		baseline = setup_draw_wrapped(SETUP_FONT_TEXT, 20.0f, 64.0f, baseline + 18.0f, SETUP_WIDTH - 128, 26.0f,
			SETUP_COLOR_TEXT, line);
	}
	/* (what was found and why it isn't used) */
	for (index = 0; index < flow->count && shown < 2 && baseline < 462.0f; index++)
	{
		const struct candidate *candidate = &flow->candidates[index];

		snprintf(text, sizeof(text), "%s: %s", candidate->kind == CANDIDATE_IMAGE ? file_name(candidate->path) :
			candidate->path, candidate->reason);
		setup_draw_text(SETUP_FONT_TEXT, 18.0f, 64.0f, baseline + 4.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_BAD, text);
		baseline += 23.0f;
		shown++;
	}
	prompts(system, 2, glyph, label);
	present(system);
}

static void draw_choices(struct flow *flow)
{
	const struct setup_system *system = flow->system;
	const int glyph[] = { confirm_glyph(system), SETUP_GLYPH_TRIANGLE, back_glyph(system) };
	const char *const label[] = { "Select", "Look again", "Quit" };
	int index, row;

	if (!setup_draw_begin(system->version))
		return;
	setup_draw_title(60.0f, 120.0f, SETUP_ALIGN_LEFT, "SET UP WARTHOG");
	setup_draw_text(SETUP_FONT_TEXT, 22.0f, 62.0f, 156.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_TEXT_DIM,
		"Warthog needs the maps from your Halo: Combat Evolved disc for the original Xbox.");
	setup_draw_text(SETUP_FONT_TEXT, 22.0f, 62.0f, 182.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_TEXT_DIM,
		flow->count == 1 ? "This was found on the memory card:" : "These were found on the memory card:");
	setup_draw_panel(40, LIST_TOP, SETUP_WIDTH - 80, VISIBLE_ROWS * ROW_HEIGHT + 12);
	if (flow->selected < flow->top)
		flow->top = flow->selected;
	if (flow->selected >= flow->top + VISIBLE_ROWS)
		flow->top = flow->selected - VISIBLE_ROWS + 1;
	for (row = 0; row < VISIBLE_ROWS; row++)
	{
		const struct candidate *candidate;
		int y = LIST_TOP + 6 + row * ROW_HEIGHT;

		index = flow->top + row;
		if (index >= flow->count)
			break;
		candidate = &flow->candidates[index];
		if (index == flow->selected)
			setup_draw_selection(48, y + 2, SETUP_WIDTH - 96, ROW_HEIGHT - 4);
		setup_draw_text(SETUP_FONT_TEXT, 25.0f, 66.0f, (float)y + 30.0f, SETUP_ALIGN_LEFT,
			candidate->usable ? SETUP_COLOR_TEXT : SETUP_COLOR_FAINT, candidate->name);
		setup_draw_text(SETUP_FONT_TEXT, 18.0f, 66.0f, (float)y + 54.0f, SETUP_ALIGN_LEFT,
			candidate->usable ? SETUP_COLOR_STATUS : SETUP_COLOR_BAD,
			candidate->usable ? candidate->detail : candidate->reason);
	}
	if (flow->top > 0)
		setup_draw_text(SETUP_FONT_TEXT, 18.0f, SETUP_WIDTH - 60.0f, LIST_TOP + 22.0f, SETUP_ALIGN_RIGHT,
			SETUP_COLOR_FAINT, "MORE ABOVE");
	if (flow->top + VISIBLE_ROWS < flow->count)
		setup_draw_text(SETUP_FONT_TEXT, 18.0f, SETUP_WIDTH - 60.0f, LIST_TOP + VISIBLE_ROWS * ROW_HEIGHT + 4.0f,
			SETUP_ALIGN_RIGHT, SETUP_COLOR_FAINT, "MORE BELOW");
	if (flow->note[0])
		setup_draw_text(SETUP_FONT_TEXT, 20.0f, 62.0f, 474.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_WARNING, flow->note);
	prompts(system, 3, glyph, label);
	present(system);
}

/* ---------- the copy */

struct copy
{
	pthread_mutex_t lock;
	char image[512];
	char destination[512];
	volatile int cancel;
	int finished;
	int succeeded;
	char file[256];
	unsigned long long done;
	unsigned long long total;
	char error[512];
};

static void copy_progress(void *context, const char *file, unsigned long long done, unsigned long long total)
{
	struct copy *copy = context;

	pthread_mutex_lock(&copy->lock);
	snprintf(copy->file, sizeof(copy->file), "%s", file);
	copy->done = done;
	copy->total = total;
	pthread_mutex_unlock(&copy->lock);
}

static void *copy_thread(void *context)
{
	struct copy *copy = context;
	char error[512] = "";
	/* (the free space is the setup's own check: the device's) */
	int succeeded = xiso_extract_maps_with(copy->image, copy->destination, XISO_EXTRACT_NO_SPACE_CHECK,
		&copy->cancel, copy_progress, copy, error, sizeof(error)) != 0;

	pthread_mutex_lock(&copy->lock);
	copy->succeeded = succeeded;
	snprintf(copy->error, sizeof(copy->error), "%s", error);
	copy->finished = 1;
	pthread_mutex_unlock(&copy->lock);
	return NULL;
}

static void draw_copy(const struct setup_system *system, const struct copy *copy, const char *file,
	unsigned long long done, unsigned long long total, unsigned long long elapsed, int asking)
{
	char text[700], left[96] = "", sizes[48], of[48];
	float fraction = total ? (float)((double)done / (double)total) : 0.0f;

	if (!setup_draw_begin(system->version))
		return;
	setup_draw_title(60.0f, 120.0f, SETUP_ALIGN_LEFT, "COPYING THE MAPS");
	snprintf(text, sizeof(text), "From %s", copy->image);
	setup_draw_text(SETUP_FONT_TEXT, 22.0f, 62.0f, 156.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_TEXT_DIM, text);
	snprintf(text, sizeof(text), "to %s/maps", copy->destination);
	setup_draw_text(SETUP_FONT_TEXT, 22.0f, 62.0f, 182.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_TEXT_DIM, text);
	setup_draw_panel(40, 204, SETUP_WIDTH - 80, 200);
	setup_draw_text(SETUP_FONT_TEXT, 26.0f, 66.0f, 248.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_TEXT,
		file && *file ? file : "Reading the disc image...");
	setup_draw_progress(66, 268, SETUP_WIDTH - 132, 30, fraction);
	megabytes(done, sizes, sizeof(sizes));
	megabytes(total, of, sizeof(of));
	/* (the time left, once there is a rate to go by) */
	if (done && total > done && elapsed > 4000)
	{
		unsigned long long seconds = (unsigned long long)((double)(total - done) * (double)elapsed /
			(double)done / 1000.0);

		if (seconds >= 90)
			snprintf(left, sizeof(left), "  -  about %llu minutes left", (seconds + 30) / 60);
		else
			snprintf(left, sizeof(left), "  -  under 2 minutes left");
	}
	snprintf(text, sizeof(text), "%s of %s  (%d%%)%s", sizes, of, (int)(fraction * 100.0f), left);
	setup_draw_text(SETUP_FONT_TEXT, 22.0f, 66.0f, 332.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_STATUS, text);
	setup_draw_text(SETUP_FONT_TEXT, 19.0f, 66.0f, 374.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_TEXT_DIM,
		"Keep Warthog open. The PS button is off until the copy is done.");
	if (asking)
	{
		const int glyph[] = { confirm_glyph(system), back_glyph(system) };
		const char *const label[] = { "Stop copying", "Keep copying" };

		setup_draw_text(SETUP_FONT_TEXT, 24.0f, 62.0f, 448.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_WARNING,
			"Stop the copy? What was copied so far is deleted.");
		prompts(system, 2, glyph, label);
	}
	else
	{
		const int glyph[] = { back_glyph(system) };
		const char *const label[] = { "Cancel" };

		prompts(system, 1, glyph, label);
	}
	present(system);
}

/* the image's maps copied into the data folder: 1 copied, 0 failed or
cancelled (said on the screen or in flow->note) */
static int copy_image(struct flow *flow, const struct candidate *candidate)
{
	const struct setup_system *system = flow->system;
	struct copy *copy = calloc(1, sizeof(*copy));
	pthread_t thread;
	pthread_attr_t attributes;
	int started;
	unsigned long long start = system->now_ms(), last_draw = 0, last_awake = 0;
	int asking = 0, finished = 0, succeeded = 0;
	char error[512] = "";

	if (!copy)
		return 0;
	pthread_mutex_init(&copy->lock, NULL);
	snprintf(copy->image, sizeof(copy->image), "%s", candidate->path);
	snprintf(copy->destination, sizeof(copy->destination), "%s", flow->places->data_folder);
	copy->total = candidate->bytes;
	log_line(system, "setup: copying the maps from %s into %s", copy->image, copy->destination);
	/* (the disc reader walks its directory trees recursively: a stack of
	its own, not the system's smallest) */
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, COPY_STACK_BYTES);
	started = pthread_create(&thread, &attributes, copy_thread, copy) == 0;
	pthread_attr_destroy(&attributes);
	if (!started)
	{
		pthread_mutex_destroy(&copy->lock);
		free(copy);
		notice_wait(flow, "SETUP", "The copy could not be started.", "", 0);
		return 0;
	}
	if (system->busy)
		system->busy(1);
	flow->held = system->buttons();
	while (!finished)
	{
		unsigned int fresh = pressed(flow);
		unsigned long long now = system->now_ms();
		char file[256];
		unsigned long long done, total;

		if (!asking && (fresh & back_button(flow)))
			asking = 1, last_draw = 0;
		else if (asking && (fresh & confirm_button(flow)))
		{
			copy->cancel = 1;
			log_line(system, "setup: the copy cancelled by the player%s%s", NULL, NULL);
			asking = 0;
		}
		else if (asking && (fresh & back_button(flow)))
			asking = 0, last_draw = 0;
		pthread_mutex_lock(&copy->lock);
		finished = copy->finished;
		succeeded = copy->succeeded;
		snprintf(file, sizeof(file), "%s", copy->file);
		snprintf(error, sizeof(error), "%s", copy->error);
		done = copy->done;
		total = copy->total;
		pthread_mutex_unlock(&copy->lock);
		if (system->keep_awake && now - last_awake >= 1000)
		{
			system->keep_awake();
			last_awake = now;
		}
		if (!last_draw || now - last_draw >= 100 || finished)
		{
			draw_copy(system, copy, copy->cancel ? "Stopping..." : file, done, total, now - start, asking);
			last_draw = now;
		}
		system->sleep_ms(PASS_MILLISECONDS);
	}
	pthread_join(thread, NULL);
	if (system->busy)
		system->busy(0);
	{
		char seconds[32];

		snprintf(seconds, sizeof(seconds), "%llu", (system->now_ms() - start) / 1000);
		log_line(system, succeeded ? "setup: the maps copied in %s s%s" : "setup: the copy stopped after %s s: %s",
			seconds, error);
	}
	if (!succeeded && copy->cancel)
		snprintf(flow->note, sizeof(flow->note), "The copy was stopped. Nothing of it was kept.");
	else if (!succeeded)
		notice_wait(flow, "COPY FAILED", "The maps could not be copied from the disc image:", error, 0);
	pthread_mutex_destroy(&copy->lock);
	free(copy);
	return succeeded;
}

/* ---------- the flow */

static void keep_choice(const struct flow *flow, const char *folder)
{
	FILE *file;

	if (!flow->places->choice_file)
		return;
	file = fopen(flow->places->choice_file, "w");
	if (!file)
		return;
	fprintf(file, "%s\n", folder);
	fclose(file);
}

static int kept_choice(const struct flow *flow, char *folder, int size)
{
	FILE *file;
	char line[600];
	int index;

	if (!flow->places->choice_file || !(file = fopen(flow->places->choice_file, "r")))
		return 0;
	line[0] = 0;
	if (!fgets(line, sizeof(line), file))
		line[0] = 0;
	fclose(file);
	line[strcspn(line, "\r\n")] = 0;
	/* (only one of the folders the flow offers: the file is the player's
	to edit) */
	for (index = 0; index < flow->places->maps_folder_count; index++)
	{
		if (!strcmp(line, flow->places->maps_folders[index * 2]) && setup_folder_has_xbox_maps(line))
		{
			snprintf(folder, (size_t)size, "%s", line);
			return 1;
		}
	}
	return 0;
}

static int has_room(struct flow *flow, const struct candidate *candidate)
{
	long long free_bytes = flow->system->free_space ? flow->system->free_space(flow->places->data_folder) : -1;
	unsigned long long needed = candidate->bytes + ((unsigned long long)SPARE_BYTES_MB << 20);
	char need[48], have[48], first[400];

	if (free_bytes < 0 || (unsigned long long)free_bytes >= needed)
		return 1;
	megabytes(needed, need, sizeof(need));
	megabytes((unsigned long long)free_bytes, have, sizeof(have));
	snprintf(first, sizeof(first), "The maps need %s on the memory card, and it has %s free.", need, have);
	notice_wait(flow, "NOT ENOUGH SPACE", first, "Free some space, then choose the disc image again.", 0);
	return 0;
}

static void done_screen(struct flow *flow, const struct candidate *candidate)
{
	char first[700], second[700];

	snprintf(first, sizeof(first), "The maps are in %s/maps.", flow->places->data_folder);
	snprintf(second, sizeof(second), "Warthog doesn't need %s any more: you can delete it to free the space.",
		candidate->path);
	notice_wait(flow, "MAPS READY", first, second, DONE_MILLISECONDS);
}

int setup_find_maps(const struct setup_system *system, const struct setup_places *places, char *maps_root,
	int maps_root_size)
{
	struct flow *flow;
	char own[600];
	int result = SETUP_MAPS_QUIT;
	int dirty = 1;

	join(own, sizeof(own), places->data_folder, "maps");
	if (setup_folder_has_xbox_maps(own))
		return SETUP_MAPS_OWN;
	flow = calloc(1, sizeof(*flow));
	if (!flow)
		return SETUP_MAPS_OWN;
	flow->system = system;
	flow->places = places;
	if (kept_choice(flow, maps_root, maps_root_size))
	{
		log_line(system, "setup: the maps from %s (the player's choice)%s", maps_root, NULL);
		free(flow);
		return SETUP_MAPS_ELSEWHERE;
	}
	log_line(system, "setup: no Xbox maps in %s; looking on the memory card%s", own, NULL);
	scan(flow);
	{
		/* (one maps folder and no disc image to copy: nothing to choose, so
		it is read without a screen, as before the screens) */
		const struct candidate *only = NULL;
		int index, images = 0, folders = 0;

		for (index = 0; index < flow->count; index++)
		{
			if (!flow->candidates[index].usable)
				continue;
			if (flow->candidates[index].kind == CANDIDATE_IMAGE)
				images++;
			else
				folders++, only = &flow->candidates[index];
		}
		if (!images && folders == 1)
		{
			snprintf(maps_root, (size_t)maps_root_size, "%s", only->path);
			log_line(system, "setup: the maps from %s (the only maps on the card)%s", only->path, NULL);
			free(flow);
			return SETUP_MAPS_ELSEWHERE;
		}
	}
	flow->held = system->buttons();
	for (;;)
	{
		unsigned int fresh;

		if (dirty)
		{
			if (flow->usable)
				draw_choices(flow);
			else
				draw_missing(flow);
			dirty = 0;
		}
		system->sleep_ms(PASS_MILLISECONDS);
		fresh = pressed(flow);
		if (fresh & back_button(flow))
		{
			log_line(system, "setup: the player quit%s%s", NULL, NULL);
			break;
		}
		if (fresh & SETUP_BUTTON_TRIANGLE)
		{
			log_line(system, "setup: looking again%s%s", NULL, NULL);
			flow->note[0] = 0;
			scan(flow);
			dirty = 1;
			continue;
		}
		if (!flow->usable)
			continue;
		if ((fresh & SETUP_BUTTON_UP) && flow->selected > 0)
			flow->selected--, dirty = 1;
		if ((fresh & SETUP_BUTTON_DOWN) && flow->selected + 1 < flow->count)
			flow->selected++, dirty = 1;
		if (fresh & confirm_button(flow))
		{
			const struct candidate *candidate = &flow->candidates[flow->selected];

			dirty = 1;
			flow->note[0] = 0;
			if (!candidate->usable)
			{
				notice_wait(flow, "CAN'T USE THIS", candidate->reason, candidate->path, 0);
				continue;
			}
			if (candidate->kind == CANDIDATE_FOLDER)
			{
				snprintf(maps_root, (size_t)maps_root_size, "%s", candidate->path);
				keep_choice(flow, candidate->path);
				log_line(system, "setup: the player chose the maps in %s%s", candidate->path, NULL);
				result = SETUP_MAPS_ELSEWHERE;
				break;
			}
			if (!has_room(flow, candidate))
				continue;
			if (copy_image(flow, candidate))
			{
				if (setup_folder_has_xbox_maps(own))
				{
					done_screen(flow, candidate);
					result = SETUP_MAPS_OWN;
					break;
				}
				notice_wait(flow, "COPY FAILED", "The copied maps folder has no Xbox ui.map.", own, 0);
			}
			scan(flow);
		}
	}
	free(flow);
	return result;
}

/* ---------- the updater's screens */

int setup_update_question(const struct setup_system *system, const char *latest, const char *current)
{
	struct flow flow;
	const int glyph[] = { confirm_glyph(system), back_glyph(system), SETUP_GLYPH_TRIANGLE };
	const char *const label[] = { "Update now", "Not now", "Don't ask again" };
	char first[200];
	unsigned long long start = system->now_ms();
	int answer = -1;

	memset(&flow, 0, sizeof(flow));
	flow.system = system;
	snprintf(first, sizeof(first), "Version %s is out. You have %s.", latest, current);
	log_line(system, "setup: update question: %s%s", first, NULL);
	draw_notice(system, "UPDATE AVAILABLE", first, "The update is downloaded and installed, then Warthog starts again.",
		glyph, label, 3);
	flow.held = system->buttons();
	while (answer < 0 && system->now_ms() - start < QUESTION_MILLISECONDS)
	{
		unsigned int fresh = pressed(&flow);

		if (fresh & confirm_button(&flow))
			answer = 1;
		else if (fresh & back_button(&flow))
			answer = 0;
		else if (fresh & SETUP_BUTTON_TRIANGLE)
			answer = 2;
		system->sleep_ms(PASS_MILLISECONDS);
	}
	return answer < 0 ? 0 : answer;
}

void setup_notice(const struct setup_system *system, const char *first, const char *second)
{
	struct flow flow;

	memset(&flow, 0, sizeof(flow));
	flow.system = system;
	notice_wait(&flow, "WARTHOG", first, second, QUESTION_MILLISECONDS);
}

struct progress_screen
{
	const struct setup_system *system;
};

void *setup_progress_open(const struct setup_system *system)
{
	struct progress_screen *screen = calloc(1, sizeof(*screen));

	if (screen)
		screen->system = system;
	return screen;
}

void setup_progress_show(void *opened, float fraction, const char *title, const char *line)
{
	struct progress_screen *screen = opened;

	if (!screen || !setup_draw_begin(screen->system->version))
		return;
	setup_draw_title(60.0f, 120.0f, SETUP_ALIGN_LEFT, "UPDATING");
	setup_draw_panel(40, 204, SETUP_WIDTH - 80, 170);
	setup_draw_text(SETUP_FONT_TEXT, 26.0f, 66.0f, 248.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_TEXT, title ? title : "");
	setup_draw_progress(66, 268, SETUP_WIDTH - 132, 30, fraction);
	setup_draw_text(SETUP_FONT_TEXT, 22.0f, 66.0f, 332.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_STATUS, line ? line : "");
	setup_draw_text(SETUP_FONT_TEXT, 19.0f, 66.0f, 440.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_TEXT_DIM,
		"Keep Warthog open until the update is done.");
	present(screen->system);
}

void setup_progress_close(void *opened)
{
	free(opened);
}

void setup_release(void)
{
	setup_draw_release();
}
