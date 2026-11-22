/*
VITA_GAME_LIST.C

What the game list's builds (HALO_GAME_BROWSER: Online Games, Link Profile,
the Delta Guide) ask of the platform layer that the Vita's build has no
unit for: the desktop's updater, window thread and screen readback are not
on the Vita (sdl_platform.c, updater.c and xbox_textures.c are replaced or
left out), so their answers are the Vita's here.
*/

#ifdef HALO_GAME_BROWSER

#include "platform.h"
#include "sdl_platform.h"

#include <stddef.h>

/* (updater.c's, which the Vita does not build: its version is the game's,
and it never runs under gamescope) */
const char *updater_version(void)
{
	return HALO_VERSION;
}

int updater_gamescope(void)
{
	return 0;
}

/* (sdl_platform.c's) someone holds the Vita */
BOOL platform_run_is_unwatched(void)
{
	return FALSE;
}

/* (sdl_platform.c's) the game's thread draws, but a halt is said in the
log and the Vita's own way (halt_screen.c's screen is the desktop's) */
int platform_on_event_thread(void)
{
	return 0;
}

/* (xbox_textures.c's) a texture's pixels for the overlay's plasma: not
read back on the Vita (the overlay's own noise instead) */
unsigned char *xgpu_texture_rgba(const DWORD *resource, unsigned long *width, unsigned long *height)
{
	(void)resource;
	*width = *height = 0;
	return NULL;
}

#endif

#if defined(HALO_GAME_BROWSER) && defined(HALO_MCC) && HALO_MCC
/* (MCC maps' textures, mcc_texture_bridge.c: the desktop's renderer keeps
their GPU copies apart from the Xbox's texture cache. The Vita's renderer
has no such copies yet - an MCC map's bitmaps are not drawn - so there is
nothing to let go of or to bind again) */
void xgpu_mcc_texture_unbind(DWORD const *resource)
{
	(void)resource;
}

void xgpu_gl_state_invalidate(void)
{
}
#endif
