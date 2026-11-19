/*
VITA_GLUE.H

Force-included ahead of the Vita's own platform units (port/vita/platform),
with vita_xgpu_game.h: what they call of the game that Halo CE Vita's tree
had and Chupathingy's has under another name, or not at all
(platform/vita_glue.c answers those).
*/

#ifndef __VITA_GLUE_H
#define __VITA_GLUE_H

/* the renderer told a map's tags went (Halo CE Vita's cache_files.c called
it between maps; Warthog: at the next map's load, vita_glue.c) */
extern void (*platform_renderer_map_unloaded)(void);

/* bumped when the settings change: the pad reads its settings again */
extern volatile unsigned long halo_settings_generation;

#endif
