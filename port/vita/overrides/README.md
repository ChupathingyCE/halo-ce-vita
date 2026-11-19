# Overrides of the game's files

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
