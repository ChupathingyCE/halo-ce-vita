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

`HALO_MAPS_ROOT` (a maps folder elsewhere), `HALO_HEARTBEAT_S`, `HALO_EVENTS`
(lines written at once to `events.txt`), `HALO_HANG_CRASH`, `HALO_CRASH_AT`,
`HALO_CRASH_AT_FRAME` (a deliberate crash for a dump), and the game's own
(`HALO_SCREENSHOT_DIR`, `HALO_SCREENSHOT_EVERY`, ...).

## Known problems

- **The game stops before the main menu.** `HALO_WINDOW_REBASE` moves any value
  in 0x80000000-0x88000000, the window's linked range, and on the Vita that
  range is the application's own memory (the eboot at 0x81000000, the C heap
  above it): host pointers the game keeps in its Xbox-address fields come back
  moved. The Xbox 360, whose executable is up there too, uses `HALO_64BIT` with
  `HALO_WINDOW_TRANSLATED`; the Vita needs the same, with a window small enough
  for its memory.
- Not yet: Link Profile, the Online Games screen, the system keyboard, movies,
  the menus' art and the high-resolution HUD (`HALO_CONSOLE_HOST_ART`), Custom
  Edition maps, an updater.
