/*
VITA_XGPU_GAME.H

Force-included ahead of the Vita renderer's units (port/vita/build.py): the
game's xgpu.h with its OpenGL renderer's pixel shader key renamed out of the
way of the Vita's own (vita_pixel_key.h), so that vita_xgpu.h, which
includes xgpu.h, finds it read already. vita_xgpu.h and the Cg translators
stay as Halo CE Vita had them: tools/vita_shader_generator_id.py hashes
them, and the shipped shader pack (app0/shaders.pak) is stamped with that
hash.
*/

#ifndef __VITA_XGPU_GAME_H
#define __VITA_XGPU_GAME_H

#define nv2a_pixel_shader_key upstream_nv2a_pixel_shader_key
#include "xgpu.h"
#undef nv2a_pixel_shader_key
#include "vita_pixel_key.h"

#endif
