/*
HALO_VITA_PREFIX.H

Force-included ahead of every game and platform unit of the Vita build
(clang -include), in front of the game's own halo_linux_prefix.h, which the
Vita's clang side shares with the Linux build: the game and the platform
layer are compiled by clang for the Cortex-A9 (ARMv7, 32-bit, little-endian)
with the game's MSVC ABI, against VitaSDK's newlib headers.

Pointers are 32 bits, as the Xbox's, but the Vita's own memory is where the
Xbox's window was linked: the layout is the Xbox 360's, translated
(HALO_64BIT with HALO_WINDOW_TRANSLATED, source/cseries/xbox_address.h;
port/vita/host/vita_memory.c).
*/

#ifndef __HALO_VITA_PREFIX_H
#define __HALO_VITA_PREFIX_H

#if !defined(__arm__) || defined(__aarch64__)
#error the Vita build targets 32-bit ARM
#endif

#ifndef HALO_VITA_CONSOLE
#define HALO_VITA_CONSOLE 1
#endif

/* The debug decomp's developer tools are left out unless the build is
configured with --console-debug-tools, as on the other consoles
(game/vita_debug_tools.c answers the commands that reach them). */
#ifndef HALO_CONSOLE_DEBUG_TOOLS
#define HALO_CONSOLE_NO_DEBUG_TOOLS
void console_debug_tools_unavailable(char const *name);
#endif

/* ---------- newlib, read first

wchar_t is 16 bits (-fshort-wchar) and wint_t with it, typed before newlib
types its own as an int. newlib's <sys/features.h> reads <limits.h>, whose
LONG_MAX, LONG_MIN, CHAR_MAX and CHAR_MIN cseries.h defines as enumerators:
the game's units see no macros for those (the platform layer, which does not
read cseries.h, keeps them). newlib's stdio reaches its timeval and select
declarations, which the Xbox SDK's Winsock declares again under the port's
private names (halo_linux_winsock_names.h): they are read first, under their
own. */

#ifndef __cplusplus
#define _WINT_T 1
#define __wint_t_defined 1
typedef unsigned short wint_t;
#endif
#include <limits.h>
#ifndef HALO_LINUX_PLATFORM_LAYER
#undef LONG_MAX
#undef LONG_MIN
#undef CHAR_MAX
#undef CHAR_MIN
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#endif

#ifdef HALO_LINUX_PLATFORM_LAYER
/* what the platform layer uses of POSIX that newlib leaves out (mmap over
the Xbox window, and the rest: include/posix) */
#include "posix/vita_posix.h"
#endif

/* the console's capabilities, read ahead of xbox_address.h (which
halo_linux_prefix.h reads): HALO_CONSOLE_WINDOW_LOW sizes the Xbox window
of the translated layout (HALO_64BIT with HALO_WINDOW_TRANSLATED, on the
compile line: port/vita/build.py) */
#include "halo_console_features.h"

#include "halo_linux_prefix.h"

/* the classification functions as functions: the game declares them itself
(hs.c), which newlib's macros would expand (its headers are read first in
the translated layout, halo_linux_prefix.h) */
#undef isalnum
#undef isalpha
#undef iscntrl
#undef isdigit
#undef isgraph
#undef islower
#undef isprint
#undef ispunct
#undef isspace
#undef isupper
#undef isxdigit
#undef isblank
#undef isascii
#undef toascii
#undef tolower
#undef toupper

/* the high-res HUD and text (port/linux/src/hud_hires.c, text_hires.c, the
game's hud_hires_tags.c): every console build goes without them
(halo_port_features.h's HALO_CONSOLE_NO_HIRES), drawing the maps' own fonts
and bitmaps; the Vita's renderer draws the HUD's sheets and the text's
atlas (platform/vita_textures.c, host/vita_gxm.c), so they are kept: the
header is read here first and the switch taken back, its guard keeping it
out from then on. Until upstream has a console ask for them
(HALO_CONSOLE_HIRES, a pull request candidate) */
#include "halo_port_features.h"
#undef HALO_CONSOLE_NO_HIRES

#endif /* __HALO_VITA_PREFIX_H */
