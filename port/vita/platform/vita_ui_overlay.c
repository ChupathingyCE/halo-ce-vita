/*
VITA_UI_OVERLAY.C

The game's overlay (its port/linux/src/ui_overlay.c: Online Games, the
Delta Guide, their prompts) on the Vita's renderer. The game builds it with
HALO_CONSOLE_OVERLAY_DRAW: it gathers the frame's shapes and text as the
desktop does and hands the triangles and its textures' texels to
console_overlay_draw, which passes them to the GXM side
(host/vita_gxm.c, vgxm_ui_overlay_submit) to be drawn over the picture at
the next present, at the display's resolution.

The overlay lays its screens out in the menus' 640x480 and maps them onto
the game's picture where the present drew it on the display (all 544 lines;
centred, with bars either side, for a 4:3 picture).
*/

#if defined(HALO_GAME_BROWSER) && defined(HALO_CONSOLE_OVERLAY_DRAW)

#include "platform.h"
#include "ui_overlay.h"
#include "vita_gxm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned long long vita_host_time_us(void);
void vita_host_log(const char *line);

/* (debug) HALO_OVERLAY_TIMING=n: every n frames with the overlay, a line of
its cost: ui_overlay_present's time, the glyphs rasterized and their time,
the atlas rows sent */
static struct
{
	int period;
	unsigned long frames, glyphs, rows;
	unsigned long long present_us, glyph_us;
} timing = { -1 };

unsigned char *__real_posix_ui_font_glyph(int font, float pixel_height, unsigned int codepoint, int *width,
	int *height, int *x_offset, int *y_offset);
unsigned char *__wrap_posix_ui_font_glyph(int font, float pixel_height, unsigned int codepoint, int *width,
	int *height, int *x_offset, int *y_offset)
{
	unsigned long long started = vita_host_time_us();
	unsigned char *glyph = __real_posix_ui_font_glyph(font, pixel_height, codepoint, width, height, x_offset, y_offset);

	timing.glyph_us += vita_host_time_us() - started;
	timing.glyphs++;
	return glyph;
}

/* (D3DDevice_Present, on the game's thread, before the frame's present is
recorded) the frame's overlay, if it gathered any */
void vita_ui_overlay_present(void)
{
	float place[4];
	int x, y, width, height;

	if (!ui_overlay_pending())
		return;
	vgxm_picture_place(place);
	if (place[2] <= 0.0f || place[3] <= 0.0f)
	{
		/* (before the first present: the whole display) */
		place[0] = place[1] = 0.0f;
		place[2] = 960.0f;
		place[3] = 544.0f;
	}
	x = (int)place[0];
	width = (int)place[2];
	height = (int)place[3];
	/* (ui_overlay_present's y is from the window's bottom, as GL's) */
	y = 544 - ((int)place[1] + height);
	if (timing.period < 0)
		timing.period = getenv("HALO_OVERLAY_TIMING") ? atoi(getenv("HALO_OVERLAY_TIMING")) : 0;
	{
		unsigned long long started = vita_host_time_us();

		ui_overlay_present(x, y, width, height, 960, 544);
		timing.present_us += vita_host_time_us() - started;
	}
	if (timing.period > 0 && ++timing.frames >= (unsigned long)timing.period)
	{
		char line[200];

		snprintf(line, sizeof(line), "overlay timing: %lu frames: present %.2f ms a frame, %.1f glyphs made a frame "
			"(%.2f ms), %.1f atlas rows sent a frame", timing.frames, timing.present_us / 1000.0 / timing.frames,
			(double)timing.glyphs / timing.frames, timing.glyph_us / 1000.0 / timing.frames,
			(double)timing.rows / timing.frames);
		vita_host_log(line);
		memset(&timing, 0, sizeof(timing));
		timing.period = getenv("HALO_OVERLAY_TIMING") ? atoi(getenv("HALO_OVERLAY_TIMING")) : 0;
	}
}

int console_overlay_draw(const void *vertices, int count, int vertex_bytes, const unsigned char *atlas, int atlas_size,
	int dirty_top, int dirty_bottom, const unsigned char *icons, int icon_size, int icon_count,
	const unsigned char *picture, int picture_width, int picture_height, int picture_serial,
	const unsigned char *plasma, int plasma_width, int plasma_height, int plasma_serial, const float plasma_shift[8],
	const float cellwall[12], const unsigned char *medals, int medals_width, int medals_height,
	const float cutouts[4][4], int window_width, int window_height)
{
	struct vgxm_ui_overlay frame;

	(void)cellwall;
	(void)medals;
	(void)medals_width;
	(void)medals_height;
	(void)window_width;
	(void)window_height;
	memset(&frame, 0, sizeof(frame));
	frame.vertices = vertices;
	frame.count = count;
	frame.vertex_bytes = vertex_bytes;
	frame.atlas = atlas;
	frame.atlas_size = atlas_size;
	frame.dirty_top = dirty_top;
	if (dirty_bottom > dirty_top)
		timing.rows += (unsigned long)(dirty_bottom - dirty_top);
	frame.dirty_bottom = dirty_bottom;
	frame.icons = icons;
	frame.icon_size = icon_size;
	frame.icon_count = icon_count;
	frame.picture = picture;
	frame.picture_width = picture_width;
	frame.picture_height = picture_height;
	frame.picture_serial = picture_serial;
	frame.plasma = plasma;
	frame.plasma_width = plasma_width;
	frame.plasma_height = plasma_height;
	frame.plasma_serial = plasma_serial;
	if (plasma_shift)
		memcpy(frame.plasma_shift, plasma_shift, sizeof(frame.plasma_shift));
	if (cutouts)
		memcpy(frame.cutouts, cutouts, sizeof(frame.cutouts));
	vgxm_ui_overlay_submit(&frame);
	/* (no medals' atlas in the Vita's build: HALO_CONSOLE_NO_MEDAL_ICONS) */
	return 0;
}

#else

void vita_ui_overlay_present(void)
{
}

#endif
