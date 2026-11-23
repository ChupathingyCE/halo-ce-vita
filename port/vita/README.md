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

The clang side is optimized as one module (link-time optimization, the
game's `--lto`, on by default: `ld.lld -r` makes one object of it for GCC's
link; `--lto off` builds without). A `--release` build also compiles the
debug decomp's asserts out (the game's `--console-no-debug-asserts`; a dev
build keeps them). Measured on b30 (dev builds, 120 s of the beach): 8.8
frames a second as before, 10.1 with LTO, 10.5 without the asserts, 12.4
with both and the view-only interpolation below, 12.8 with the original
structure walk too. Two options stay off:
`--vita-aligned hot` (the hot directories without `-fmax-type-align=1`)
faulted in the b30 fight (an unaligned load in the rasterizer's environment
draw), and `--pgo-profile` with the game's Linux profile gave nothing
measurable (9.0).

The Vita sets two of the game's console capabilities for its speed:
`HALO_CONSOLE_VIEW_INTERPOLATION` (frames between ticks move the view only:
no snapshot of every object after each tick, 9-10 ms of a b30 frame) and
`HALO_CONSOLE_NO_STRUCTURE_CULLING` (the level's surfaces walked as the
original game walks them: the culling's tests cost the A9 more than they
spare the GPU).

Round 2 (on the game's main of Nov 21: 11.6 frames a second on b30, worst
8.4) adds the console gates upstream merged from Halo CE Vita's work
(`HALO_CONSOLE_FEWER_WALKS`, `_MARKER_LOOKUPS`, `_ANIMATION_FRAMES`,
`_XBOX_MAP_SIZES`), still props' node matrices left as they are
(`HALO_CONSOLE_STATIC_SCENERY`: 11.9), four open pull requests from the same
work and the Xbox's particle limit (12.4), and Halo CE Vita's defaults:
dynamic resolution and 8-pixel object culling (13.1, worst 8.6; up to 30
before the beach fight). All of the simulation's changes give the same
results; the defaults change only what is drawn. `overrides/README.md` says
which files wait on which pull request.

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
| `setup/`, `host/vita_setup.c` | the first start's screens before the game (the game's `HALO_CONSOLE_SETUP_SCREENS` hooks): the maps found on the card, a disc image's maps copied in, the updater's screens; drawn by the processor into the display's own buffers, `tests/setup_test.c` and `tools/vita_setup_test.py` run them on Linux |
| `platform/vita_glue.c` | what the renderer asked of Halo CE Vita's game, answered for Chupathingy's |
| `overrides/` | game files the Vita needs fixed, until upstream has them (README there) |

## Debug switches (`env.txt`)

`HALO_MAPS_ROOT` (a maps folder elsewhere: no first start's screens),
`HALO_SETUP_ONLY_OWN=1`, `HALO_SETUP_FREE_MB`, `HALO_SETUP_UPDATE_TEST=1`,
`HALO_SETUP_PAD`, `HALO_SETUP_SHOTS=1` (the first start's tests:
`host/vita_setup.c` says what each does), `HALO_HEARTBEAT_S` (a line every
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

A test build made with `--vita-frame-split` (configure.py) times the game
thread's frame from outside the game: `HALO_FRAME_TIMING=n` writes, every n
frames, the main loop's steps (tick, sound, render, present), the tick's
updates per tick, the render's passes, every thread's run time a frame, and
with `--vita-frame-split device` (the default) the Vita's Direct3D device's
part of each step (its timing slows the device: compare frame rates with
`steps`). `platform/vita_frame_split.c` says how.

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

- Slow: b30 about 13 frames a second (worst 9) where Halo CE Vita 1.1.3
  runs 28; Blood Gulch alone 33. The game thread is busy the whole frame
  (`--vita-frame-split`): the drawing takes as long as Halo CE Vita's (about
  36 ms in the beach fight), but the game's two ticks a frame (about 24 ms
  each) run on the same thread, where Halo CE Vita ran its tick on a thread
  of its own; at two ticks a frame the game runs slower than real time.
  The tick thread is upstream's ChupathingyCE/chupathingyce#1548.
- Each checkpoint stops the picture for about 1.3 s: the game state the
  Vita builds with is the desktop's 20 MB, and the game writes all of it to
  the memory card at once (the Xbox wrote 3.3 MB).
- Not yet: Link Profile and the Online Games screen (the Delta Guide is not
  built for consoles), the system keyboard for text boxes, movies,
  Custom Edition maps, an updater.
- "failed to change bitmap hardware format" lines in debug.txt at start.
