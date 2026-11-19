# Vita

`ninja vita` builds the game for the PlayStation Vita as a homebrew VPK
(`build/vita/warthog.vpk`). The game is the `chupathingyce/` submodule;
this folder holds the Vita's own code and `build.py`, which the game's
`configure.py` loads (`--console-port port/vita`).

## Two compilers

- **clang** compiles the game, the desktop platform layer it shares
  (the game's `port/linux/src`, less what `REPLACED_PLATFORM_SOURCES` lists)
  and the port's units in the platform layer's terms (`platform/`) for the
  Cortex-A9 with the game's MSVC ABI: 32-bit enums, 16-bit `wchar_t`, signed
  `char`, and `-fmax-type-align=1` (the game reads floats through pointers of
  no particular alignment, which the A9's VLDR faults on).
- **VitaSDK's GCC** compiles the Vita side (`host/`) with the SDK's ABI,
  because the Sce headers' structures depend on it, and links the whole
  against newlib, SDL3 (sound only) and the system's stub libraries. The two
  halves meet only through `include/vita_host.h`, whose parameters are 32-bit
  scalars and pointers.

## What is the Vita's own

| Folder | What |
| --- | --- |
| `host/vita_main.c` | the process, the data folder, `env.txt`, the log thread, the game's thread (16 MB stack), the watchdog |
| `host/vita_memory.c` | the Xbox window (one 128 MB block, made before the platform layer's constructors) and `mmap` over it |
| `host/vita_gxm.c`, `platform/d3d8_gxm.c`, `vita_textures.c`, `nv2a_*_cg.c` | the Direct3D device on GXM (in d3d8_gl.c's place), its Cg translators, texture cache and shader cache; `app0/shaders.pak` the shipped programs |
| `host/vita_input.c`, `vita_controls.c`, `vita_ctrl_ports.c`, `platform/vita_pad.c`, `vita_xinput.c` | buttons, sticks, touch zones, the rear pad's guard, gyro aiming, as XInput |
| `host/vita_net.c` | sockets over SceNet |
| `host/vita_clocks.c`, `vita_fourth_core.c`, `vita_cpu.c` | clocks, the fourth core, core load |
| `platform/vita_platform.c` | the window and events the game asks of `sdl_platform.c` |
| `platform/vita_glue.c` | what the renderer asked of Halo CE Vita's game, answered for Chupathingy's |
| `overrides/` | game files the Vita needs fixed, until upstream has them (README there) |

## Debug switches (`env.txt`)

`HALO_MAPS_ROOT` (a maps folder elsewhere), `HALO_HEARTBEAT_S` (a line every
n seconds: frames presented, free memory, the game's heap), `HALO_EVENTS`
(those lines written at once to `events.txt`), `HALO_UDP_LOG=a.b.c.d:port`
(the same as datagrams to a listener, with the file calls, for a run whose
card writes stop), `HALO_THREAD_SAMPLE` (every thread's state twice a second
to the UDP log), `HALO_LOCK_TRACE=n` (the first n texture locks),
`HALO_HANG_CRASH`, `HALO_CRASH_AT`, `HALO_CRASH_AT_FRAME` (a deliberate crash
for a dump), `HALO_NO_GAME`, `HALO_NO_NET`, and the game's own
(`HALO_SCREENSHOT_DIR`, `HALO_SCREENSHOT_EVERY`, `HALO_NULL_RENDERER`,
`HALO_FRAME_TIMES`). `init.txt` in the data folder runs console commands at
the start (`map_name levels\test\bloodgulch\bloodgulch`).

## Memory

The layout is the Xbox 360's, translated (`HALO_64BIT` with
`HALO_WINDOW_TRANSLATED`): one 176 MB block for the Xbox window, the game's
48 MB heap (`HALO_CONSOLE_WINDOW_LOW`, the game's
`halo_console_features.h`) and the 128 MB of contiguous memory, wherever the
kernel puts it; newlib's heap is 40 MB. The rebased layout (`HALO_WINDOW_REBASE`)
cannot work on the Vita: it moves values in 0x80000000-0x88000000, which is
the Vita's own memory (the program at 0x81000000, the C heap above it).
Measured at the main menu: the game's heap 39 MB, the C heap 11 MB, 27 MB of
user memory free; on b30, 44 MB of the heap and 21 MB free.

The program keeps the system from suspending it (`sceKernelPowerTick`, once
a second on a thread of its own): without it the system suspended any
application on the dev Vita 14 s after it started.

## Known problems

- Slow: b30 about 9 frames a second where Halo CE Vita 1.1.3 runs 28; Blood
  Gulch alone 33. None of Halo CE Vita's game-side performance work is in
  the game (PLAN.md has the list).
- Not yet: Link Profile and the Online Games screen (the Delta Guide is not
  built for consoles), the system keyboard for text boxes, movies, the
  high-resolution text and HUD (`HALO_CONSOLE_HOST_ART`'s text atlas and HUD
  sheets; the game's own fonts are used), Custom Edition maps, an updater.
- "failed to change bitmap hardware format" lines in debug.txt at start.
