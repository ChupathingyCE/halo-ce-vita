/*
MENU_STILL.C

(port) The live background's still: the loading screen's picture when the
player has no retail one.

The loading screen's picture is the retail executable's (progress_bar.c, the
player's own default.xbe). An install from Halo: MCC alone has no such file,
and its loading screens were black; without the intro movies (they are
optional) the start was black too. The owner's call (Nov 13 2026): show the
live menu's picture there. Made of the player's own files only: once the live
background (menu_backdrop.c, a30 from the player's own map) is up and has
settled at the main menu, one frame is drawn without the menus' widgets
(render_ui_widgets asks menu_still_hide_interface), the device reads it back
at its present (d3d8_gl.c, d3d8_gxm.c: menu_still_store) and it is kept,
320x240 as the retail picture, in t:\menu_still.bin - once per install (made
again only when the file is gone). progress_bar.c draws it as it draws the
retail picture, the screen's blur and glow over it.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menu_still.h"

void platform_log(const char *format, ...);

#define MENU_STILL_PATH "t:\\menu_still.bin"
#define MENU_STILL_MAGIC "HCVSTIL1"
#define MENU_STILL_HEADER 16
#define MENU_STILL_BYTES (MENU_STILL_WIDTH * MENU_STILL_HEIGHT * 4)

enum
{
	_still_idle,
	/* the next frame's widgets left out */
	_still_armed,
	/* this frame drawn without them: read back at its present */
	_still_drawn,
	/* the present asked for it: the device's (the Vita's worker) to read */
	_still_reading,
	/* read: menu_still_update writes it */
	_still_read,
	_still_done
};

static struct
{
	volatile int state;
	/* t:\menu_still.bin checked: -1 not yet, 0 none, 1 there */
	int cached;
	/* the picture read back, A8B8G8R8 as the loading texture (progress_bar.c) */
	unsigned long *pixels;
} menu_still = { _still_idle, -1, NULL };

int menu_still_cached(void)
{
	if (menu_still.cached < 0)
	{
		FILE *file = fopen(MENU_STILL_PATH, "rb");
		char header[MENU_STILL_HEADER];

		menu_still.cached = 0;
		if (file)
		{
			if (fread(header, 1, sizeof(header), file) == sizeof(header) &&
				!memcmp(header, MENU_STILL_MAGIC, 8) && fseek(file, 0, SEEK_END) == 0 &&
				ftell(file) == MENU_STILL_HEADER + MENU_STILL_BYTES)
			{
				menu_still.cached = 1;
			}
			fclose(file);
		}
	}
	return menu_still.cached;
}

void menu_still_request(void)
{
	if (menu_still.state == _still_idle && !menu_still_cached())
		menu_still.state = _still_armed;
}

int menu_still_hide_interface(void)
{
	if (menu_still.state == _still_armed)
		menu_still.state = _still_drawn;
	return menu_still.state == _still_drawn;
}

int menu_still_present_wanted(void)
{
	if (menu_still.state != _still_drawn)
		return 0;
	menu_still.state = _still_reading;
	return 1;
}

void menu_still_store(const unsigned char *pixels, unsigned long pitch, unsigned long width, unsigned long height)
{
	unsigned long *out;
	unsigned long x, y;

	if (menu_still.state != _still_reading)
		return;
	out = pixels && width >= MENU_STILL_WIDTH && height >= MENU_STILL_HEIGHT ? malloc(MENU_STILL_BYTES) : NULL;
	if (!out)
	{
		/* (no picture: asked again at the next settled main menu) */
		menu_still.state = _still_idle;
		return;
	}
	/* (the whole frame, each of the picture's texels the mean of its box:
	the screen's shape squeezed into 4:3, stretched back as the loading
	screen fills the screen) */
	for (y = 0; y < MENU_STILL_HEIGHT; y++)
	{
		unsigned long y0 = y * height / MENU_STILL_HEIGHT, y1 = (y + 1) * height / MENU_STILL_HEIGHT;

		for (x = 0; x < MENU_STILL_WIDTH; x++)
		{
			unsigned long x0 = x * width / MENU_STILL_WIDTH, x1 = (x + 1) * width / MENU_STILL_WIDTH;
			unsigned long sum[3] = { 0, 0, 0 }, count = 0, sx, sy;

			for (sy = y0; sy < y1; sy++)
			{
				const unsigned char *row = pixels + sy * pitch;

				for (sx = x0; sx < x1; sx++)
				{
					/* (B, G, R, A as read) */
					sum[0] += row[sx * 4];
					sum[1] += row[sx * 4 + 1];
					sum[2] += row[sx * 4 + 2];
					count++;
				}
			}
			if (!count)
				count = 1;
			out[y * MENU_STILL_WIDTH + x] = 0xFF000000UL | ((sum[0] / count) << 16) | ((sum[1] / count) << 8) |
				(sum[2] / count);
		}
	}
	menu_still.pixels = out;
	menu_still.state = _still_read;
}

void menu_still_update(void)
{
	FILE *file;
	char header[MENU_STILL_HEADER];
	int written;

	if (menu_still.state != _still_read || !menu_still.pixels)
		return;
	memset(header, 0, sizeof(header));
	memcpy(header, MENU_STILL_MAGIC, 8);
	header[8] = (char)(MENU_STILL_WIDTH & 0xFF);
	header[9] = (char)(MENU_STILL_WIDTH >> 8);
	header[10] = (char)(MENU_STILL_HEIGHT & 0xFF);
	header[11] = (char)(MENU_STILL_HEIGHT >> 8);
	file = fopen(MENU_STILL_PATH ".tmp", "wb");
	written = file && fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
		fwrite(menu_still.pixels, 1, MENU_STILL_BYTES, file) == MENU_STILL_BYTES;
	if (file && fclose(file))
		written = 0;
	if (written)
	{
		remove(MENU_STILL_PATH);
		written = rename(MENU_STILL_PATH ".tmp", MENU_STILL_PATH) == 0;
	}
	if (!written)
		remove(MENU_STILL_PATH ".tmp");
	free(menu_still.pixels);
	menu_still.pixels = NULL;
	menu_still.cached = -1;
	menu_still.state = _still_done;
	platform_log("menu still: the live background's picture %s (%s): the loading screen's when there is no retail "
		"default.xbe picture", written ? "kept" : "could not be written", MENU_STILL_PATH);
}

int menu_still_load(unsigned long *pixels, long pitch)
{
	FILE *file;
	long y;
	int loaded;

	if (!menu_still_cached() || !pixels)
		return 0;
	file = fopen(MENU_STILL_PATH, "rb");
	if (!file)
	{
		menu_still.cached = -1;
		return 0;
	}
	loaded = fseek(file, MENU_STILL_HEADER, SEEK_SET) == 0;
	for (y = 0; loaded && y < MENU_STILL_HEIGHT; y++)
		loaded = fread((char *)pixels + y * pitch, 4, MENU_STILL_WIDTH, file) == MENU_STILL_WIDTH;
	fclose(file);
	return loaded;
}
