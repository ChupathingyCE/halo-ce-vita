/*
HALO_VITA_PREFIX.H

Force-included ahead of every game and platform unit of the Vita build
(clang -include), in front of the game's own halo_linux_prefix.h, which the
Vita's clang side shares with the Linux build: the game and the platform
layer are compiled by clang for the Cortex-A9 (ARMv7, 32-bit, little-endian)
with the game's MSVC ABI, against VitaSDK's newlib headers.

Pointers are 32 bits, as the Xbox's. The Xbox's memory window is wherever the
Vita's kernel puts the block the host allocates for it (HALO_WINDOW_REBASE,
source/cseries/xbox_address.h; port/vita/host/vita_memory.c).
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

/* the Xbox window, wherever the host put it */
#ifndef HALO_WINDOW_REBASE
#define HALO_WINDOW_REBASE 1
#endif

#include "halo_linux_prefix.h"

#endif /* __HALO_VITA_PREFIX_H */
