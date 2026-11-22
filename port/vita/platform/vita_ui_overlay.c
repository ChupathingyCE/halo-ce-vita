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

#include <string.h>

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
	ui_overlay_present(x, y, width, height, 960, 544);
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
