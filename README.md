<p align="center"><img src="port/vita/sce_sys/icon0.png" width="120" alt=""></p>

<h1 align="center">Warthog for Vita</h1>

<p align="center"><b>Chupathingy on the PlayStation Vita.</b></p>

<p align="center">
<a href="https://github.com/ChupathingyCE/chupathingyce">The game (Chupathingy)</a> ·
<a href="https://stats.chupathingy.org">Games online now</a> ·
<a href="https://chupathingy.org">chupathingy.org</a> ·
<a href="https://discord.gg/chupathingy">Discord</a>
</p>

<p align="center">
<a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue" alt="License: GPL-3.0-or-later"></a>
<a href="https://discord.gg/chupathingy"><img src="https://img.shields.io/discord/1555995995555365045?label=discord&color=5865F2" alt="Discord"></a>
</p>

Warthog for Vita is the Halo: Combat Evolved decompilation built for the
PlayStation Vita, on the same game code as
[Chupathingy](https://github.com/ChupathingyCE/chupathingyce) (the Windows,
Mac, Linux and Android builds) and the other Warthog ports (the original
Xbox, the Xbox 360, the Wii U and the Switch). It runs as homebrew on a Vita
or PS TV with HENkaku or Ensō, with no emulator. Its network code is the
game's, so it is built to play with the PCs and the other consoles.

This repository holds the Vita's own code. The game is the `chupathingyce`
submodule, and the shared code's console hooks live there.

You need your own copy of Halo. No game files are included, and nothing from
Sony's SDK: the port is built with VitaSDK's free tools.

Halo CE Vita, the port this one follows, stays in this repository's history
and releases, up to 1.1.3.

## Status

In development, not released yet. The version is the game's and the Vita's
build count: 0.9.2 (build 1).

- Starts to the Chupathingy main menu in about 7 seconds, at 60 frames a
  second.
- Plays the Xbox maps. Measured on a PS Vita 2000:
  - Blood Gulch, alone: 33 frames a second, steady.
  - The Silent Cartographer (b30), the beach: 13 frames a second on average
    (up to 30 before the Covenant engage), 9 at the worst moment, when they
    engage on the beach. Halo CE Vita 1.1.3 runs the same beach at 28: its
    game tick runs beside the drawing, on a core of its own.
- Halo CE Vita's defaults: 480 lines (or 544, the screen's), dynamic
  resolution, objects too small to see left out, and its touch, rear touch
  guard and gyro settings, on rows of the Chupathingy menus.
- Chupathingy's high-resolution HUD and sharp text.
- **Not yet:**
  - Online play through Delta: Online Games, Link Profile, friends and
    parties.
  - The system keyboard for text boxes. The game's on-screen keyboard works.
  - The intro and the cutscene movies.
  - Custom Edition maps.
  - Updates from inside the game.
  - Most of the speed Halo CE Vita had.

## What you need

- A PS Vita or PS TV on firmware 3.60 to 3.74 with HENkaku or Ensō, and
  VitaShell.
- About 1.5 GB free on `ux0:`, for the maps' cache files.
- Your own Halo disc's `maps` folder (the Xbox game's: `ui.map`,
  `bloodgulch.map`, `a10.map` and the rest). Warthog never provides maps,
  disc images or other game data, and never links to them. Don't share yours.

## Getting started

### Install

1. Install `warthog.vpk` with VitaShell. The bubble is **Warthog** (title ID
   `WRTV00001`). It sits next to Halo CE Vita and doesn't touch it or its
   saves. Test builds are a separate app, **Warthog Dev** (`WRTD00001`), with
   their own folder, `ux0:data/warthog-vita-dev/`.
2. Copy your disc's `maps` folder to `ux0:data/warthog-vita/maps/`.
   If Halo CE Vita is installed, Warthog reads its maps where they are
   (`ux0:data/haloce-vita/maps/`), so you don't need a second copy.
3. Start Warthog.

The first start writes `config.toml`. The first load of each map takes a
while: the game writes the map's cache file to the memory card. Later loads
are quicker.

### Where things go

Everything is in `ux0:data/warthog-vita/`:

| What | Where |
| --- | --- |
| Settings | `config.toml` |
| Retail maps | `maps/` (or Halo CE Vita's, read where they are) |
| Saves and the maps' cache files | `saves/` |
| The game's log | `logs/debug.txt` |
| The Vita side's log | `halo.log` (the run before: `halo-prev.log`) |
| Compiled shaders | `shaders/` |

## Controls

| Vita | Xbox |
| --- | --- |
| Cross | A |
| Circle | B |
| Square | X |
| Triangle | Y |
| L | Left trigger |
| R | Right trigger |
| Left stick, right stick | The sticks |
| D-pad down | Left stick click (crouch) |
| D-pad up | Right stick click (zoom) |
| D-pad left | Black |
| D-pad right | White |
| Start | Start |
| Select | Back |

In the menus the D-pad moves the selection. Touching the front screen moves it too.

## Settings

The settings are in `ux0:data/warthog-vita/config.toml`, the same file and
settings as on the PC and Android builds. The first start writes it with
every setting, commented out at its default. Remove the `# ` in front of
one, change its value and save. The menus' Settings change the same file.

## Reporting problems

1. Right after the problem, copy `logs/debug.txt` and `halo.log` from
   `ux0:data/warthog-vita/` to your computer. After a crash, also copy the
   newest `psp2core-*.psp2dmp` from `ux0:data/`.
2. Tell us in #bug-reports on the
   [Discord](https://discord.gg/chupathingy) what you were doing, and attach
   the files.

## Building it yourself

Clone with the game:

```sh
git clone --recursive https://github.com/ChupathingyCE/warthog-vita.git
cd warthog-vita
```

(In a clone you already have: `git submodule update --init`.)

You need [VitaSDK](https://vitasdk.org) (in `VITASDK`, or `--vitasdk DIR`),
clang, Python 3 and [ninja](https://ninja-build.org/). clang compiles the
game and VitaSDK's GCC compiles and links the Vita side.

```sh
python3 chupathingyce/configure.py --console-port port/vita [--vitasdk DIR] [--release]
ninja vita                          # build/vita/warthog.vpk
```

`configure.py` is the game's, and `port/vita/build.py` adds the Vita's
targets to it. The game's
[docs/building.md](https://github.com/ChupathingyCE/chupathingyce/blob/main/docs/building.md),
"Console ports", has the layout and the options every console takes.
`--release` makes the release build (`WRTV00001`, `ux0:data/warthog-vita`).
Without it the build is a dev build (`WRTD00001`, `ux0:data/warthog-vita-dev`),
which stops at the first failed assertion and logs it. `--vita-title-id` and
`--vita-data-folder` choose others.

[port/vita/README.md](port/vita/README.md) has the port's own account: the
two compilers, the memory, the renderer and the debug switches. Changes to
the game itself go to
[ChupathingyCE/chupathingyce](https://github.com/ChupathingyCE/chupathingyce).
Until one is merged, the fixed file is kept whole in
[`port/vita/overrides/`](port/vita/overrides/README.md).

## Generative AI

This port was made with heavy use of generative AI tools. A person decides
what to build, reviews every change, tests it on the console and is
responsible for it. The game's
[policy](https://github.com/ChupathingyCE/chupathingyce/blob/main/CONTRIBUTING.md#using-ai-tools)
applies to contributions here.

## License

Warthog for Vita is free software: you can redistribute it and modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License or (at your option) any
later version (`GPL-3.0-or-later`). It comes with no warranty. The full text
is in [LICENSE](LICENSE). The game's console SDK permission
([docs/license.md](https://github.com/ChupathingyCE/chupathingyce/blob/main/docs/license.md#console-sdk-permission))
applies to it too.

- **The Vita port** is BirchWood's work, contributed under
  GPL-3.0-or-later.
- **The game** (`chupathingyce/`) is under its own repository's license,
  with its third-party code, fonts and art under theirs.
- **Third-party code** keeps its own license, beside it: TLSF (BSD) in
  `port/vita/third_party/tlsf`. The build links VitaSDK's newlib, SDL3 and
  the system's stub libraries from your VitaSDK install. None of them is
  included here.
- **No console SDK.** The port is built only with free, legally available
  tools and libraries. Nothing from Sony's SDK is in this repository, and
  none of it may be committed.
- **The icon** is Chupathingy's own (the game's `port/art/icon.svg`).
- **Halo itself is not covered.** The license covers the work of this
  repository's and Chupathingy's authors. It gives no rights in Halo, its
  maps or anything else Microsoft owns.

## Credits

- The decompilation: [punpckhdq/halo](https://github.com/punpckhdq/halo) and
  [bnunu/halo-1](https://github.com/bnunu/halo-1), of the Xbox build 2342.
- The game: [Chupathingy](https://github.com/ChupathingyCE/chupathingyce)
  and its contributors, and OpenCE and its contributors before it.
- Warthog, the consoles' line: [Milenko](https://github.com/MrMilenko) and
  contributors.
- The Vita port: BirchWood ([BirchWoodGod](https://github.com/BirchWoodGod)),
  its maintainer.
- The toolchain and libraries: [VitaSDK](https://vitasdk.org), SDL3, and the
  libraries the game uses, with their licenses in the game's
  `port/third_party`.

## Links

- The game: [ChupathingyCE/chupathingyce](https://github.com/ChupathingyCE/chupathingyce)
- Games online now: [stats.chupathingy.org](https://stats.chupathingy.org)
- Discord: [discord.gg/chupathingy](https://discord.gg/chupathingy)
  (#bug-reports for problems, #dev for development)

Halo is a trademark of Microsoft, and PlayStation Vita a trademark of Sony
Interactive Entertainment. Warthog for Vita is a fan project, not made or
endorsed by Microsoft, Bungie, 343 Industries or Sony, and includes none of
the game's content. The code is released under the GNU GPL, version 3 or
later (see [License](#license)).
