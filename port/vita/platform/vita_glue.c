/*
VITA_GLUE.C

What the Vita's renderer and controls (Halo CE Vita's d3d8_gxm.c,
vita_textures.c, vita_gxm.c, vita_pad.c) asked of Halo CE Vita's own game
code, which Chupathingy's game does not have, answered here as Warthog has it
now; and the few hooks a renderer of its own owes the game
(halo_console_features.h: HALO_CONSOLE_HOST_ART).

Each answer says what it stands in for. Several are Halo CE Vita's
performance and diagnostics hooks into its main loop and menus (the frame
timing line, the render phases, the menus' still frame, the clocks'
governor's idle test): with them answered as nothing, those features are off
on Warthog until the game has a hook for them (port/vita/README.md, "What
the Vita's port does not have yet").
*/

#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "port_config.h"

/* ---------- the renderer's hooks into the game */

/* the renderer told a map's tags went (Halo CE Vita's cache_files.c,
between maps): the game has no such call; never called on Warthog yet */
void (*platform_renderer_map_unloaded)(void);

/* the settings' generation: bumped by Halo CE Vita's settings panel. The
game's settings are config.toml's (config_changes); the pad's are read once */
volatile unsigned long halo_settings_generation = 1;

/* the map's generation (a new map's textures forgotten): Halo CE Vita's
cache_files.c bumped it at each map load */
unsigned long halo_map_generation = 1;

/* texture streaming's stand-ins (Halo CE Vita's rasterizer_xbox.c bound a
copy of the default texture while a bitmap loaded): Chupathingy's game does
not stream, so none */
D3DBaseTexture d3d_stand_in_textures[1];
const unsigned long d3d_stand_in_texture_count = 0;

/* the render phase the frame is in (Halo CE Vita's render.c named its
passes for the GPU timing line) */
int halo_render_phase;
const char *halo_render_phase_names[64];

/* a screenshot's name, asked for by Halo CE Vita's console command */
char halo_screenshot_name[64];

/* the frame timing line's numbers (Halo CE Vita's main.c measured them) */
void halo_frame_timing_recent(float *frame_ms, float *tick_ms, float *render_ms)
{
	*frame_ms = *tick_ms = *render_ms = 0.0f;
}

/* a test run that must repeat exactly (Halo CE Vita's main.c,
HALO_REPEATABLE): never on Warthog */
int halo_repeatable_run(void)
{
	return 0;
}

/* whether the loading screen is up (Halo CE Vita's main.c): the GPU's
clocks are lowered under it (d3d8_gxm.c clocks_governor) */
unsigned char game_loading_screen_up_here(void)
{
	return 0;
}

/* the main menu's state (Halo CE Vita's menus: its Halo 3 style's backdrop
and the menus' still frame, which let the GPU rest while nothing moves) */
int halo_main_menu_idle(void)
{
	return 0;
}

int halo_main_menu_loaded(void)
{
	return 0;
}

int halo_menu_style_online(void)
{
	return 0;
}

/* the narrowest screen the menus are laid out on: the Xbox's 640 */
int halo_menus_minimum_width(void)
{
	return 640;
}

int menu_still_present_wanted(void)
{
	return 0;
}

void menu_still_store(const unsigned char *pixels, unsigned long pitch, unsigned long width, unsigned long height)
{
	(void)pixels;
	(void)pitch;
	(void)width;
	(void)height;
}

/* the screen's brightness curve (display.brightness, 0 to 10, 5 the
Xbox's): the exponent the renderer's last copy applies */
float halo_brightness_curve(void)
{
	static float curve = 1.0f;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		long value = config_integer("display.brightness");

		read_at = config_changes();
		if (value < 0 || value > 10)
			value = 5;
		curve = 1.0f - (float)(value - 5) * 0.08f;
	}
	return curve;
}

/* Halo CE Vita's settings panel is not on Warthog: the game's menus are
the settings */
int vita_settings_panel_open(void)
{
	return 0;
}

/* ---------- what the game asks of a Direct3D device of a console's own */

/* the screen's targets to the Xbox's: the Vita draws the Xbox's 480 lines */
float halo_screen_scale(void)
{
	return 1.0f;
}

/* the shadow maps at the Xbox's 128 */
long halo_shadow_map_scale(void)
{
	return 1;
}

/* per-pixel model lighting (display.per_pixel_lighting) is the OpenGL
renderer's: the Vita lights for each vertex, as the Xbox did */
void halo_vertex_shader_lighting(unsigned long handle)
{
	(void)handle;
}

/* display.anti_aliasing's pass: none on the Vita */
void halo_screen_anti_alias(short x0, short y0, short x1, short y1)
{
	(void)x0;
	(void)y0;
	(void)x1;
	(void)y1;
}

/* the loading screen's picture (the game's loading_screen.c): not drawn by
the Vita's renderer yet; the game's own progress bar is */
void halo_draw_picture(const unsigned char *rgba, long width, long height)
{
	(void)rgba;
	(void)width;
	(void)height;
}

/* the high-res HUD's sheets, decoded by a console's own renderer
(HALO_CONSOLE_HOST_ART) as the map loads: brought down to the size the
Vita's screen shows on a thread of their own, and kept until the texture
cache builds their bitmaps (vita_textures.c hud_ahead) */
void vita_hud_ahead_start(const long *assets, long count);
void vita_hud_ahead_forget(void);

void dk_hud_prepare(const long *assets, long count)
{
	vita_hud_ahead_start(assets, count);
}

void dk_hud_forget(void)
{
	vita_hud_ahead_forget();
}

/* ---------- the console's developer tools and Link Profile */

/* (HALO_CONSOLE_NO_DEBUG_TOOLS) a command or script of the debug decomp's
developer tools, which the build leaves out */
void console_debug_tools_unavailable(char const *name)
{
	platform_log("%s: a developer tool this build leaves out (--console-debug-tools)", name);
}

/* Delta Peer's PROFILE: the profile this console is linked to with Link
Profile (port/console/link). Not linked yet on the Vita: 0, none */
int console_profile_id(unsigned char *id)
{
	(void)id;
	return 0;
}

/* ---------- OpenGL the shared code still names

menu_files.c lets go of a menu art's OpenGL texture when its art changes;
the Vita's renderer never makes one (HALO_CONSOLE_HOST_ART), so the call is
never made with a texture. (gl.h's pointer, which gl_functions.c fills on
the desktop) */
static void no_texture_deletion(int count, const unsigned int *textures)
{
	(void)count;
	(void)textures;
}

void (*halo_glDeleteTextures)(int count, const unsigned int *textures) = no_texture_deletion;

/* ---------- (debug) HALO_LOCK_TRACE=n: the first n texture locks, to the
UDP log: which resource, its data, the bits the lock gave */
void vita_host_udp_line(const char *text);
void __real_D3DTexture_LockRect(D3DTexture *texture, UINT level, D3DLOCKED_RECT *locked, CONST RECT *rectangle,
	DWORD flags);

void __wrap_D3DTexture_LockRect(D3DTexture *texture, UINT level, D3DLOCKED_RECT *locked, CONST RECT *rectangle,
	DWORD flags)
{
	static int left = -1;

	__real_D3DTexture_LockRect(texture, level, locked, rectangle, flags);
	if (left < 0)
		left = getenv("HALO_LOCK_TRACE") ? atoi(getenv("HALO_LOCK_TRACE")) : 0;
	if (left > 0)
	{
		const DWORD *words = (const DWORD *)texture;
		char line[200];

		left--;
		snprintf(line, sizeof(line), "lock %p level %u: common %08lx data %08lx format %08lx size %08lx -> bits %p pitch %d",
			(void *)texture, level, (unsigned long)words[0], (unsigned long)words[1], (unsigned long)words[3],
			(unsigned long)words[4], locked->pBits, locked->Pitch);
		vita_host_udp_line(line);
	}
}
