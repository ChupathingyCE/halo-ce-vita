/*
VITA_SETUP.C

The Vita's first start and the updater's screens: the console hooks of
HALO_CONSOLE_SETUP_SCREENS (the game's port/linux/include/
halo_console_features.h) over the setup screens (port/vita/setup), before
the game's renderer starts.

	console_setup_start          main, before the maps are looked for
	vita_setup_maps              main: the maps found, offered, copied
	                             (setup_find_maps); the Vita's part of what
	                             platform_offer_game_data and
	                             platform_import_start/poll are on the
	                             desktop, whose sdl_platform.c the Vita
	                             replaces
	console_setup_after_data     once the maps are there: the update check
	                             (vita_updater_console_check: no Vita
	                             updater yet, the hook only)
	console_setup_update_question, console_setup_screen_open, _progress,
	_close, console_setup_notice  the updater's question, its progress and
	                             a failure, as updater.c asks them

The picture is drawn by the processor into one of two 960x544 buffers in
CDRAM (a user RAM block when CDRAM has no room), handed to the display;
when the screens are done the display is blacked out and the buffers
given back, before the renderer makes its own (host/vita_gxm.c). The
buttons are read from the pad, the confirm button the system's (circle in
Japan). While a copy runs, the PS button is locked and the system is kept
from sleeping (as Halo CE Vita's Steam download did).

env.txt's test knobs: HALO_SETUP_ONLY_OWN=1 (no other app's maps folders
looked at: the first start on a card that has Halo CE Vita's),
HALO_SETUP_FREE_MB (the free space said), HALO_SETUP_UPDATE_TEST=1 (the
update screens shown once with a made-up version, nothing fetched),
HALO_SETUP_PAD (presses for a test: "x@5000 d@6200", a button - x o t s u d
l r S - and when, in milliseconds from the first screen) and
HALO_SETUP_SHOTS=1 (the screens' pictures written to the data folder's
setup-shots, a new one at most every 1.5 s, as BMP files).
*/

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/devctl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/shellutil.h>
#include <psp2/system_param.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "setup.h"
#include "vita_host.h"
#include "vita_version.h"

#ifndef VITA_DATA_DIRECTORY
#define VITA_DATA_DIRECTORY "ux0:data/warthog-vita"
#endif

/* the release's folder (a dev build reads its maps and disc image too) */
#define RELEASE_DATA_DIRECTORY "ux0:data/warthog-vita"

void console_setup_start(void);
void console_setup_after_data(const char *data_root);
void *console_setup_screen_open(void);
void console_setup_screen_progress(void *screen, float fraction, const char *title, const char *line);
void console_setup_screen_close(void *screen);
void console_setup_notice(const char *first, const char *second);
int console_setup_update_question(const char *latest, const char *current);
void vita_updater_console_check(void);
int vita_setup_maps(void);

/* ---------- the display */

#define DISPLAY_BYTES (SETUP_WIDTH * SETUP_HEIGHT * 4)

static struct
{
	SceUID blocks[2];
	void *buffers[2];
	int current;
	int open;
	int failed;
} display;

static void log_text(const char *line)
{
	vita_host_log(line);
}

static void *display_block(SceUID *uid, int index)
{
	static const struct { SceKernelMemBlockType type; unsigned int alignment; } kinds[] =
	{
		{ SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * 1024 },
		{ SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, 1024 * 1024 },
	};
	unsigned int kind;
	void *base = NULL;

	for (kind = 0; kind < sizeof(kinds) / sizeof(*kinds); kind++)
	{
		unsigned int size = (DISPLAY_BYTES + kinds[kind].alignment - 1) & ~(kinds[kind].alignment - 1);

		*uid = sceKernelAllocMemBlock(index ? "setup display 1" : "setup display 0", kinds[kind].type, size, NULL);
		if (*uid >= 0 && sceKernelGetMemBlockBase(*uid, &base) >= 0)
			return base;
		if (*uid >= 0)
			sceKernelFreeMemBlock(*uid);
		*uid = -1;
	}
	return NULL;
}

static void display_close(void)
{
	int index;

	if (!display.open)
		return;
	/* (black, then the buffers given back once the display has let go) */
	sceDisplaySetFrameBuf(NULL, SCE_DISPLAY_SETBUF_NEXTFRAME);
	sceDisplayWaitVblankStart();
	sceDisplayWaitVblankStart();
	for (index = 0; index < 2; index++)
	{
		if (display.blocks[index] >= 0)
			sceKernelFreeMemBlock(display.blocks[index]);
		display.blocks[index] = -1;
		display.buffers[index] = NULL;
	}
	display.open = 0;
	log_text("setup: the screens' display given back");
}

static int display_ready(void)
{
	int index;

	if (display.open)
		return 1;
	if (display.failed)
		return 0;
	for (index = 0; index < 2; index++)
	{
		display.buffers[index] = display_block(&display.blocks[index], index);
		if (!display.buffers[index])
		{
			display.open = 1;
			display_close();
			display.failed = 1;
			log_text("setup: no memory for the screens' display");
			return 0;
		}
	}
	display.open = 1;
	display.current = 0;
	return 1;
}

/* (HALO_SETUP_SHOTS) the picture as a 32-bit BMP in the data folder's
setup-shots, when it differs from the last one written and 1.5 s have
passed */
static void shot_write(const unsigned char *pixels)
{
	static int count;
	static unsigned long long last_time;
	static unsigned int last_sum;
	const char *wanted = getenv("HALO_SETUP_SHOTS");
	unsigned long long now = sceKernelGetProcessTimeWide() / 1000;
	unsigned char header[54];
	unsigned int sum = 2166136261u, index, row;
	char path[160];
	unsigned char *line;
	FILE *file;

	if (!wanted || *wanted != '1' || count >= 60 || (count && now - last_time < 1500))
		return;
	for (index = 0; index < DISPLAY_BYTES; index += 64)
		sum = (sum ^ pixels[index] ^ pixels[index + 1] << 8 ^ pixels[index + 2] << 16) * 16777619u;
	if (count && sum == last_sum)
		return;
	sceIoMkdir(VITA_DATA_DIRECTORY "/setup-shots", 0777);
	snprintf(path, sizeof(path), VITA_DATA_DIRECTORY "/setup-shots/%02d.bmp", count);
	file = fopen(path, "wb");
	line = malloc(SETUP_WIDTH * 4);
	if (!file || !line)
	{
		if (file)
			fclose(file);
		free(line);
		return;
	}
	memset(header, 0, sizeof(header));
	header[0] = 'B', header[1] = 'M';
	*(unsigned int *)(void *)(header + 2) = sizeof(header) + DISPLAY_BYTES;
	*(unsigned int *)(void *)(header + 10) = sizeof(header);
	*(unsigned int *)(void *)(header + 14) = 40;
	*(int *)(void *)(header + 18) = SETUP_WIDTH;
	*(int *)(void *)(header + 22) = -SETUP_HEIGHT;
	header[26] = 1, header[28] = 32;
	*(unsigned int *)(void *)(header + 34) = DISPLAY_BYTES;
	fwrite(header, 1, sizeof(header), file);
	for (row = 0; row < SETUP_HEIGHT; row++)
	{
		const unsigned char *from = pixels + row * SETUP_WIDTH * 4;

		for (index = 0; index < SETUP_WIDTH; index++)
		{
			line[index * 4] = from[index * 4 + 2];
			line[index * 4 + 1] = from[index * 4 + 1];
			line[index * 4 + 2] = from[index * 4];
			line[index * 4 + 3] = 0xFF;
		}
		fwrite(line, 1, SETUP_WIDTH * 4, file);
	}
	fclose(file);
	free(line);
	last_time = now;
	last_sum = sum;
	count++;
}

static void present(const unsigned char *pixels)
{
	SceDisplayFrameBuf frame;

	shot_write(pixels);
	if (!display_ready())
		return;
	memcpy(display.buffers[display.current], pixels, DISPLAY_BYTES);
	memset(&frame, 0, sizeof(frame));
	frame.size = sizeof(frame);
	frame.base = display.buffers[display.current];
	frame.pitch = SETUP_WIDTH;
	frame.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
	frame.width = SETUP_WIDTH;
	frame.height = SETUP_HEIGHT;
	sceDisplaySetFrameBuf(&frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
	display.current ^= 1;
}

/* ---------- the system */

/* (HALO_SETUP_PAD) the test's presses held now: each held 150 ms from its
time */
static unsigned int test_presses(void)
{
	static unsigned long long start;
	const char *script = getenv("HALO_SETUP_PAD");
	unsigned long long now = sceKernelGetProcessTimeWide() / 1000;
	unsigned int held = 0;

	if (!script || !*script)
		return 0;
	if (!start)
		start = now;
	while (*script)
	{
		char name;
		unsigned long long at;
		int used = 0;

		while (*script == ' ')
			script++;
		if (!*script || sscanf(script, "%c@%llu%n", &name, &at, &used) != 2 || !used)
			break;
		script += used;
		if (now - start < at || now - start >= at + 150)
			continue;
		held |= name == 'x' ? SETUP_BUTTON_CROSS : name == 'o' ? SETUP_BUTTON_CIRCLE :
			name == 't' ? SETUP_BUTTON_TRIANGLE : name == 's' ? SETUP_BUTTON_SQUARE : name == 'u' ? SETUP_BUTTON_UP :
			name == 'd' ? SETUP_BUTTON_DOWN : name == 'l' ? SETUP_BUTTON_LEFT : name == 'r' ? SETUP_BUTTON_RIGHT :
			name == 'S' ? SETUP_BUTTON_START : 0;
	}
	return held;
}

static unsigned int buttons(void)
{
	static int sampling;
	SceCtrlData pad;
	unsigned int held = test_presses();

	if (!sampling)
	{
		sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
		sampling = 1;
	}
	memset(&pad, 0, sizeof(pad));
	if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0)
		return held;
	if ((pad.buttons & SCE_CTRL_UP) || pad.ly < 40)
		held |= SETUP_BUTTON_UP;
	if ((pad.buttons & SCE_CTRL_DOWN) || pad.ly > 215)
		held |= SETUP_BUTTON_DOWN;
	if (pad.buttons & SCE_CTRL_LEFT)
		held |= SETUP_BUTTON_LEFT;
	if (pad.buttons & SCE_CTRL_RIGHT)
		held |= SETUP_BUTTON_RIGHT;
	if (pad.buttons & SCE_CTRL_CROSS)
		held |= SETUP_BUTTON_CROSS;
	if (pad.buttons & SCE_CTRL_CIRCLE)
		held |= SETUP_BUTTON_CIRCLE;
	if (pad.buttons & SCE_CTRL_TRIANGLE)
		held |= SETUP_BUTTON_TRIANGLE;
	if (pad.buttons & SCE_CTRL_SQUARE)
		held |= SETUP_BUTTON_SQUARE;
	if (pad.buttons & SCE_CTRL_START)
		held |= SETUP_BUTTON_START;
	return held;
}

static unsigned long long now_ms(void)
{
	return sceKernelGetProcessTimeWide() / 1000;
}

static void sleep_ms(unsigned int milliseconds)
{
	sceKernelDelayThread(milliseconds * 1000);
}

/* the free bytes of the device (ux0:) the path is on: the I/O manager's
count, else the app manager's (the devctl is refused on some firmware) */
static long long free_space(const char *path)
{
	struct { unsigned long long maximum, free; unsigned int cluster; unsigned int unknown; } space;
	const char *override = getenv("HALO_SETUP_FREE_MB");
	const char *colon = strchr(path, ':');
	char device[8];
	uint64_t maximum = 0, free_bytes = 0;

	if (override && *override)
		return (long long)strtoull(override, NULL, 10) << 20;
	snprintf(device, sizeof(device), "%.*s", colon && colon - path < 6 ? (int)(colon - path + 1) : 4,
		colon ? path : "ux0:");
	memset(&space, 0, sizeof(space));
	if (sceIoDevctl(device, 0x3001, NULL, 0, &space, sizeof(space)) >= 0 && space.maximum)
		return (long long)space.free;
	if (sceAppMgrGetDevInfo(device, &maximum, &free_bytes) >= 0 && maximum)
		return (long long)free_bytes;
	return -1;
}

static void keep_awake(void)
{
	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
}

/* a copy running: the PS button locked (the system would suspend the app
mid-copy) and auto-suspend held off */
static void busy(int on)
{
	static int shell_ready, locked;

	if (!shell_ready)
		shell_ready = sceShellUtilInitEvents(0) >= 0 ? 1 : -1;
	if (on)
		keep_awake();
	if (shell_ready < 0)
		return;
	if (on && !locked)
		locked = sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN) >= 0;
	else if (!on && locked)
	{
		sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);
		locked = 0;
	}
	log_text(on ? (locked ? "setup: PS button locked, auto-suspend held off" : "setup: auto-suspend held off")
		: "setup: PS button unlocked");
}

static const struct setup_system *vita_system(void)
{
	static struct setup_system system;
	static int made;

	if (!made)
	{
		int enter = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;

		sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &enter);
		system.present = present;
		system.buttons = buttons;
		system.now_ms = now_ms;
		system.sleep_ms = sleep_ms;
		system.free_space = free_space;
		system.busy = busy;
		system.keep_awake = keep_awake;
		system.log = log_text;
		system.circle_confirms = enter == SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE;
		system.version = HALO_VITA_VERSION;
		made = 1;
	}
	return &system;
}

/* the screens done: their memory and the display given back */
static void screens_done(void)
{
	display_close();
	setup_release();
}

/* ---------- the first start */

int vita_setup_maps(void)
{
	/* (a dev build reads the release's maps too, and Halo CE Vita's (1.1.x)
	and Project Ark's (1.2.0) where they are) */
	static const char *const others[] =
	{
		RELEASE_DATA_DIRECTORY "/maps", "Warthog's maps",
		"ux0:data/haloce-vita/maps", "Halo CE Vita's maps",
		"ux0:data/project-ark/maps", "Project Ark's maps",
	};
	static const char *const images_dev[] = { VITA_DATA_DIRECTORY, RELEASE_DATA_DIRECTORY, "ux0:data", "ux0:" };
	static const char *const images[] = { VITA_DATA_DIRECTORY, "ux0:data", "ux0:" };
	const int release = !strcmp(VITA_DATA_DIRECTORY, RELEASE_DATA_DIRECTORY);
	const char *only_own = getenv("HALO_SETUP_ONLY_OWN");
	struct setup_places places;
	char root[512] = "";
	int result;

	memset(&places, 0, sizeof(places));
	places.data_folder = VITA_DATA_DIRECTORY;
	places.maps_folders = release ? others + 2 : others;
	places.maps_folder_count = only_own && *only_own == '1' ? 0 : release ? 2 : 3;
	places.image_folders = release ? images : images_dev;
	places.image_folder_count = release ? 3 : 4;
	places.choice_file = VITA_DATA_DIRECTORY "/maps_source.txt";
	result = setup_find_maps(vita_system(), &places, root, sizeof(root));
	screens_done();
	if (result == SETUP_MAPS_ELSEWHERE)
		setenv("HALO_MAPS_ROOT", root, 1);
	else if (result == SETUP_MAPS_QUIT)
	{
		vita_host_log("vita: no maps, the player quit from the setup");
		sceKernelExitProcess(0);
	}
	return result;
}

/* ---------- the hooks */

void console_setup_start(void)
{
	vita_host_log("setup: start");
}

/* the update check (HALO_CONSOLE_SETUP_SCREENS's updater_console_check): no
Vita updater yet - a later item fills it like warthog-switch's
host_update.c (GitHub releases' Vita asset, a newer build only). The
screens it will use are the hooks below; HALO_SETUP_UPDATE_TEST=1 shows
them once, with nothing fetched */
void vita_updater_console_check(void)
{
	const char *test = getenv("HALO_SETUP_UPDATE_TEST");
	void *screen;
	int step;

	if (!test || *test != '1')
	{
		vita_host_log("update: no Vita updater in this build");
		return;
	}
	vita_host_log("update: the update screens shown as a test (nothing fetched)");
	if (console_setup_update_question("0.9.3 (build 2)", HALO_VITA_VERSION) == 1)
	{
		screen = console_setup_screen_open();
		for (step = 0; step <= 40; step++)
		{
			char line[64];

			snprintf(line, sizeof(line), "%d of 40 MB (a test: nothing is downloaded)", step);
			console_setup_screen_progress(screen, (float)step / 40.0f, "Downloading the update...", line);
			sceKernelDelayThread(50 * 1000);
		}
		console_setup_screen_close(screen);
		console_setup_notice("The update failed, and the game goes on as it was:",
			"There is no Vita updater in this build yet (a test of the screens).");
	}
}

void console_setup_after_data(const char *data_root)
{
	char line[600];

	snprintf(line, sizeof(line), "setup: the maps are there (%s)", data_root);
	vita_host_log(line);
	vita_updater_console_check();
	screens_done();
}

int console_setup_update_question(const char *latest, const char *current)
{
	int answer = setup_update_question(vita_system(), latest, current);
	char line[160];

	snprintf(line, sizeof(line), "update: the player answered %d", answer);
	vita_host_log(line);
	return answer;
}

void *console_setup_screen_open(void)
{
	return setup_progress_open(vita_system());
}

void console_setup_screen_progress(void *screen, float fraction, const char *title, const char *line)
{
	setup_progress_show(screen, fraction, title, line);
}

void console_setup_screen_close(void *screen)
{
	setup_progress_close(screen);
}

void console_setup_notice(const char *first, const char *second)
{
	setup_notice(vita_system(), first, second);
}
