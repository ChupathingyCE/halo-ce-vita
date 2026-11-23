# Overrides of the game's files

**The submodule's pin.** Until ChupathingyCE/chupathingyce#1549
(`HALO_CONSOLE_WINDOW_LOW`, the Vita's block in `halo_console_features.h`) is
merged, `chupathingyce` is pinned to a local merge of that pull request's
branch, `console-window-low`, with `main` (branch `vita-perf2-base`: main of
Nov 21, dd64ad420), which only a local clone has. Once it merges, repin to
`main` (`git -C chupathingyce checkout <main's commit>`, then commit the new
pin). That change is not kept here as an override: the translated layout
needs it in every unit.

Fixes the Vita needs in the game itself (the `chupathingyce` submodule), each
kept here as a whole copy of the fixed file until upstream has it, laid out
as the game is (the Switch port's arrangement). `port/vita/build.py`
compiles a `.c` override in the game file's place with the game file's
flags, and finds a header override first; the submodule stays at
upstream's commit. `diff chupathingyce/<path> port/vita/overrides/<path>`
shows each one's change.

## port/linux/src/posix_files.c

VitaSDK's newlib `struct stat` has whole-second times only (`st_mtime`, no
`st_mtim`), and there is no `dladdr`: the file times are read as seconds, and
a code address is described by its value alone (the program's map file,
`build/vita/warthog.map`, names it). `#ifdef __vita__` only; nothing changes
for the other builds. Upstream PR candidate: "posix_files.c: newlib without
st_mtim or dladdr (the Vita)".

## port/linux/src/xbox_files.c

`d:\maps` read from a maps folder elsewhere on the card (`HALO_MAPS_ROOT`):
Halo CE Vita's own copy (`ux0:data/haloce-vita/maps`, or 1.2.0's
`ux0:data/project-ark/maps`), so a player who had it does not copy 1.7 GB
again. `#ifdef HALO_VITA_CONSOLE` only. Upstream PR candidate: a
`paths.maps` setting every build could use (a maps folder apart from the
data root).

## source/objects/objects.c, objects.h, source/saved games/game_state.c

A still prop's node matrices left as they are when nothing they are made
from has changed (`HALO_CONSOLE_STATIC_SCENERY`, on in `build.py`): b30's
objects step 1-2 ms a tick less. Upstream PR candidate, prepared:
"Consoles: a still prop's node matrices kept when nothing they are made from
changed" (branch `birchwood-console-static-scenery`).

## source/physics/collisions.c, collisions.h, source/scenario/scenario.c, scenario.h, wind.c, source/sound/game_sound.c, source/units/units.c, source/structures/structure_render.c

Four pull requests open upstream, from Halo CE Vita's work, merged together
as they will be: #1624 (sound obstruction rays stop at the first hit and
reuse a repeated ray) and #1620 (a location's weather and water from a table
by cluster), both `HALO_CONSOLE_COLLISION_CACHES`, on in `build.py`; #1626 (a
unit's illumination refreshed only for its debug line); #1583 (the structure
render's surface lists sorted inline, the visible bits walked by set bit).
Each file goes when its pull request is merged.

## port/linux/src/port_config.c

The Vita's own settings (a `HALO_VITA_CONSOLE` block: the lines drawn,
dynamic resolution, the touch buttons, the rear touch guard, gyro aim) and
its default for `tuning.object_cull_pixels` (8, Halo CE Vita's). Its rows are
the port's (`tools/vita_menus.py`). Upstream PR candidate, prepared:
"Settings: the Vita's own, for its port's rows" (branch
`birchwood-vita-settings-rows`). The structure culling default this file
held before is upstream now (#1572).

## port/linux/src/xiso.c, xiso.h

The first start's import (`port/vita/setup`) checks a disc image before
anything is written (Halo's title ID in `default.xbe`, an Xbox `ui.map`;
Halo PC's maps refused with a reason that says so), asks for the copy's size
first (`xiso_check_image`), leaves the movies out (the Vita plays none yet)
and lets the player cancel (`xiso_extract_maps_with`); a copy that fails or
is cancelled removes what it wrote. The free space check is the Vita's own
(`XISO_EXTRACT_NO_SPACE_CHECK`). Upstream PR candidate, prepared: "xiso:
check the disc is Halo's Xbox disc and that there is room, and let a copy be
cancelled" (branch `birchwood-xiso-import-checks`, with
`tools/test_xiso_import.py`).
