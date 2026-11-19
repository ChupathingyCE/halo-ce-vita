/*
VITA_PLATFORM.C

The game's window and system events on the Vita (sdl_platform.h, in place
of the game's sdl_platform.c): the display is GXM's (host/vita_gxm.c, under
the Direct3D device of platform/d3d8_gxm.c), SDL3 is up for the sound only
(dsound_sdl.c's mixer), and what a desktop's window has that a handheld does
not (a mouse, a keyboard, a clipboard, message boxes, other windows) answers
as nothing there.
*/

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "port_config.h"
#include "sdl_platform.h"
#include "vita_host.h"
#include "vita_gxm.h"

static BOOL sdl_started;
static BOOL menus_active;

BOOL platform_sdl_initialize(void)
{
	if (sdl_started)
		return TRUE;
	SDL_SetHint(SDL_HINT_APP_NAME, "Warthog");
	/* (GXM owns the display: host/vita_gxm.c; the pad is read directly:
	host/vita_input.c) */
	if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_EVENTS))
	{
		platform_log("SDL_Init failed: %s", SDL_GetError());
		return FALSE;
	}
	sdl_started = TRUE;
	return TRUE;
}

/* GXM, the display and the renderer (host/vita_gxm.c), the memory window
mapped for the GPU */
BOOL platform_video_initialize(unsigned long width, unsigned long height)
{
	static int result = -1;
	unsigned long size = 0;
	void *arena;

	(void)width;
	(void)height;
	if (result >= 0)
		return result == 0;
	if (!platform_sdl_initialize())
		platform_log("SDL is not up: no sound");
	arena = vita_host_arena(&size);
	result = arena ? vgxm_initialize(arena, size) : -1;
	if (result != 0)
		result = 1;
	return result == 0;
}

void platform_video_drawable_size(int *width, int *height)
{
	*width = 960;
	*height = 544;
}

void platform_video_window_size(int *width, int *height)
{
	*width = 960;
	*height = 544;
}

void platform_video_present_area(int *x, int *y, int *width, int *height)
{
	*x = 0;
	*y = 0;
	*width = 960;
	*height = 544;
}

void platform_video_swap(void)
{
}

BOOL platform_video_hdr(float *sdr_white, float *headroom)
{
	*sdr_white = 1.0f;
	*headroom = 1.0f;
	return FALSE;
}

BOOL platform_video_float_window(void)
{
	return FALSE;
}

void platform_display_apply(void)
{
}

BOOL platform_screen_mode(long *width, long *height)
{
	(void)width;
	(void)height;
	return FALSE;
}

/* the one display: the Vita's 960x544 */
int platform_display_resolutions(long *widths, long *heights, int maximum)
{
	if (maximum < 1)
		return 0;
	widths[0] = 960;
	heights[0] = 544;
	return 1;
}

int platform_display_native_size(long *width, long *height)
{
	*width = 960;
	*height = 544;
	return 1;
}

int platform_window_sizes(long *widths, long *heights, int maximum)
{
	return platform_display_resolutions(widths, heights, maximum);
}

int halo_interpolation_enabled(void)
{
	static int enabled;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.interpolation");
	}
	return enabled;
}

void platform_pump_events(void)
{
	if (sdl_started)
		SDL_PumpEvents();
}

void platform_mouse_capture(BOOL capture)
{
	(void)capture;
}

void platform_show_message(const char *title, const char *message)
{
	platform_log("%s: %s", title, message);
}

/* the maps go in the data folder by hand (port/vita/README.md): nothing to
offer on the Vita */
BOOL platform_offer_game_data(char *destination, unsigned long size)
{
	(void)destination;
	(void)size;
	return FALSE;
}

/* (the main menu's QUIT) the process ends */
void platform_request_quit(void)
{
	platform_log("quit asked for");
	exit(0);
}

void platform_menus_set_active(BOOL active)
{
	menus_active = active;
}

void platform_binding_capture_begin(void)
{
}

int platform_binding_capture_poll(int *input)
{
	(void)input;
	return 0;
}

void platform_scoreboard_scroll(int open, long *notches, long *pages)
{
	(void)open;
	*notches = 0;
	*pages = 0;
}

void platform_ui_pointer_set_active(BOOL active)
{
	(void)active;
}

BOOL platform_ui_pointer_read(struct platform_ui_pointer *pointer)
{
	(void)pointer;
	return FALSE;
}

void platform_test_pointer_event(float x, float y, int button, int wheel)
{
	(void)x;
	(void)y;
	(void)button;
	(void)wheel;
}

void platform_input_read(struct platform_input_state *state, BOOL consume_motion)
{
	(void)consume_motion;
	memset(state, 0, sizeof(*state));
}

BOOL platform_next_keystroke(struct platform_keystroke *keystroke)
{
	(void)keystroke;
	return FALSE;
}

void platform_test_keystroke(int scancode, int modifiers)
{
	(void)scancode;
	(void)modifiers;
}

int platform_movie_skip_pressed(void)
{
	return 0;
}

/* ---------- a keyboard's typing (none: the system's keyboard is not wired
to the game's text boxes yet) */

void platform_text_typing(int typing)
{
	(void)typing;
}

void platform_text_field(int typing)
{
	(void)typing;
}

void platform_chat_typing(int typing)
{
	(void)typing;
}

int platform_chat_escape(void)
{
	return 0;
}

int platform_clipboard_get(char *text, int size)
{
	if (size > 0)
		text[0] = 0;
	return 0;
}

void platform_clipboard_set(const char *text)
{
	(void)text;
}

int platform_take_clipboard_invite(char *text, int size)
{
	if (size > 0)
		text[0] = 0;
	return 0;
}

void platform_open_url(const char *url)
{
	platform_log("a web page the Vita does not open: %s", url);
}
