<p align="center"><img src="port/vita/sce_sys/icon0.png" width="120" alt=""></p>

<h1 align="center">Warthog for Vita</h1>

<p align="center"><b>Chupathingy on the PlayStation Vita.</b></p>

<p align="center">
<a href="https://github.com/ChupathingyCE/chupathingyce">The game (Chupathingy)</a> ·
<a href="https://stats.chupathingy.org">Games online now</a> ·
<a href="https://chupathingy.org">chupathingy.org</a> ·
<a href="https://discord.gg/chupathingy">Discord</a>
</p>

Warthog for Vita is the Halo: Combat Evolved decompilation built for the
PlayStation Vita, on the same game code as
[Chupathingy](https://github.com/ChupathingyCE/chupathingyce) (the Windows,
Mac, Linux and Android builds) and the other Warthog ports (the original
Xbox, the Xbox 360, the Wii U, the Switch). It runs as homebrew on a Vita
or PS TV with HENkaku/Ensō, with no emulator.

This repository holds the Vita's own code. The game is the `chupathingyce`
submodule, and the shared code's console hooks live there.

It follows Halo CE Vita, whose last release is 1.1.3. The Vita side here
(its renderer on the Vita's GPU, its controls, its memory and its
packaging) is that port's, moved onto Chupathingy's game.

You need your own copy of Halo. No game files are included, and nothing
from Sony's SDK: the port is built with VitaSDK's free tools.

## Status

**Development: not released.** The version is the game's and the Vita's
build count after it, as the other Warthog ports say theirs:
**0.9.2 (build 1)**.

- Builds a VPK with VitaSDK (`ninja vita`).
- On a Vita it starts to Chupathingy's main menu and plays Blood Gulch and
  the campaign, slowly for now ([port/vita/README.md](port/vita/README.md),
  "Known problems").
- Not yet: Link Profile, the Online Games screen, the movies, the menus'
  high-resolution art, Custom Edition maps.

## What you need

- A PS Vita or PS TV on firmware 3.60 to 3.74 with HENkaku/Ensō and VitaShell.
- About 1.5 GB free on `ux0:` for the maps' cache files.
- Your own Halo disc's `maps` folder (the Xbox's). Halo CE Vita's copy in
  `ux0:data/haloce-vita/maps` (or `ux0:data/project-ark/maps`) is read where
  it is, without a second copy.

## Getting started (test builds)

1. Install `warthog.vpk` with VitaShell. The bubble is **Warthog** (title ID
   `WRTV00001`, a test ID of its own: it never touches Halo CE Vita or its
   saves).
2. Copy the Xbox game's `maps` folder to `ux0:data/warthog-vita/maps/`, or
   keep Halo CE Vita's where it is.
3. Start the game.

Everything is in `ux0:data/warthog-vita/`:

| What | Where |
| --- | --- |
| Settings | `config.toml` |
| Saves (and the maps' cache files) | `saves/` |
| The game's log | `logs/debug.txt` |
| The Vita side's log | `halo.log` (the run before: `halo-prev.log`) |
| The renderer's compiled shaders | `shaders/` |
| Test switches | `env.txt`, one `NAME=value` a line |

## Building it yourself

Clone with the game:

```sh
git clone --recursive <this repository>
cd warthog-vita
```

You need [VitaSDK](https://vitasdk.org) (`VITASDK`, or `~/vitasdk`), clang
(any recent one: it compiles the game for ARMv7), Python 3 and
[ninja](https://ninja-build.org/).

```sh
python3 chupathingyce/configure.py --console-port port/vita [--vitasdk DIR] [--release] [--vita-build-number N]
ninja vita                          # build/vita/warthog.vpk
```

[port/vita/README.md](port/vita/README.md) has the port's own account: the
two compilers, the memory window, the renderer, what is the Vita's own and
what is the game's. Changes to the game itself go to
[ChupathingyCE/chupathingyce](https://github.com/ChupathingyCE/chupathingyce).
Until one is merged, the fixed file is kept whole in
[`port/vita/overrides/`](port/vita/overrides/README.md), as the Switch port
does.

## License

Warthog for Vita is free software: you can redistribute it and modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License or (at your option) any
later version (`GPL-3.0-or-later`). It comes with no warranty.

- **The Vita port** is BirchWoodGod's work (Halo CE Vita's Vita side),
  contributed under GPL-3.0-or-later.
- **The game** (`chupathingyce/`) is under its own repository's license,
  with its third-party code, fonts and art under theirs.
- **Third-party code** keeps its own license, beside it: TLSF (BSD) in
  `port/vita/third_party/tlsf`. The build links VitaSDK's newlib, SDL3 and
  the system's stub libraries from your VitaSDK install; none is included.
- **No console SDK.** Nothing from Sony's SDK is in this repository, and
  none of it may be committed.
- **The icon** is Chupathingy's own (the game's `port/art/icon.svg`, made into the LiveArea's pictures by `tools/vita_livearea.py`); the LiveArea background and start picture are plain placeholders.
- **Halo itself is not covered.** The license gives no rights in Halo, its
  maps or anything else Microsoft owns.

## Credits

- The decompilation: [punpckhdq/halo](https://github.com/punpckhdq/halo) and
  [bnunu/halo-1](https://github.com/bnunu/halo-1), of the Xbox build 2342.
- The game: [Chupathingy](https://github.com/ChupathingyCE/chupathingyce)
  and its contributors, and OpenCE and its contributors before it.
- Warthog, the consoles' line: Milenko and contributors.
- The Vita port: BirchWoodGod (Halo CE Vita).
- The toolchain: [VitaSDK](https://vitasdk.org), SDL3.

Halo is a trademark of Microsoft, and PlayStation Vita a trademark of Sony
Interactive Entertainment. Warthog for Vita is a fan project, not made or
endorsed by Microsoft, Bungie, 343 Industries or Sony, and includes none of
the game's content.
