#!/bin/bash
# run_mixed_tables.sh BIN [OUT.inc]: mixed play's built-in tables (port/linux/game/mixed_coop_tables.h) made again from
# the Xbox's maps and MCC's with BIN (a Linux build of this tree): each of the 23 levels loaded alone, headless, no
# network (unshare -rn), HALO_MIXED_COOP_FORCE=1 HALO_MIXED_TABLE_OUT=1 writing its streams as hex lines in its log;
# then tools/mixed_tables.py. With OUT.inc it writes that file; without, it compares with the tables built in and
# exits 0 when they are the same (PASS). Needs HALO_TEST_DATA (the Xbox maps folder's parent, default
# ~/github/vitaprojects/halo-ce/data2276) and HALO_TEST_MCC_ROOT (a Halo content folder: <root>/halo1/maps).
bin=$(readlink -f "${1:?BIN}") target=${2:-}
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../../.." && pwd)
data=${HALO_TEST_DATA:-$HOME/github/vitaprojects/halo-ce/data2276}
mccroot=${HALO_TEST_MCC_ROOT:-$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection}
sdl=${HALO_TEST_SDL:-$HOME/github/vitaprojects/halo-ce/sdl3-i686/lib}
out=$(mktemp -d "${HALO_TEST_OUT_PARENT:-/tmp}/mixed-tables-XXXXXX")
levels="a10 a30 a50 b30 b40 c10 c20 c40 d20 d40 chillout longest prisoner putput ratrace wizard boardingaction damnation carousel hangemhigh bloodgulch beavercreek sidewinder"
one() { # one LEVEL FAMILY
	local level=$1 fam=$2 d name mp=1 content=none
	d=$out/run-$level-$fam; mkdir -p "$d/data" "$d/save" "$d/home"
	case $level in a10|a30|a50|b30|b40|c10|c20|c40|d20|d40) mp=0;; esac
	if [ $fam = xbox ]; then ln -s "$data/maps" "$d/data/maps"
		[ $mp = 1 ] && name="levels\\test\\$level\\$level" || name="levels\\$level\\$level"
	else mkdir -p "$d/data/maps"; content=$mccroot
		[ $mp = 1 ] && name="levels\\test\\mcc_$level\\mcc_$level" || name="mods\\mcc_$level"; fi
	printf 'map_name %s\n' "$name" > "$d/data/init.txt"
	(cd "$d" && env -u DISPLAY -u WAYLAND_DISPLAY HOME=$d/home XDG_CONFIG_HOME=$d/home/.config XDG_DATA_HOME=$d/home/.local/share \
		SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LD_LIBRARY_PATH=$sdl HALO_DATA_ROOT=$d/data HALO_SAVE_ROOT=$d/save \
		HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER=20 HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 \
		HALO_UPDATE_AUTO=false HALO_UPDATE_CHECK=false HALO_DISCORD_APPLICATION= HALO_CRASH_REPORTS=no HALO_NET_ONLINE=false \
		HALO_CONTENT_ROOT="$content" HALO_MIXED_COOP_FORCE=1 HALO_MIXED_TABLE_OUT=1 \
		unshare -rn timeout -k 5 80 "$bin" > run.log 2>&1)
	grep -a 'mixed table hex: ' "$d/run.log" | sed 's/^.*mixed table hex: //' > "$out/$level.$fam.hex"
	[ -s "$out/$level.$fam.hex" ] || echo "FAIL $level ($fam): no table in its log ($d/run.log)"
	rm -rf "$d/save"
}
set -- $levels
while [ $# -gt 0 ]; do one $1 xbox & [ -n "${2:-}" ] && one $2 xbox & wait; shift; [ $# -gt 0 ] && shift; done
one bloodgulch mcc & one beavercreek mcc & wait
one sidewinder mcc
inc=${target:-$out/mixed_coop_tables.inc}
python3 -I "$root/tools/mixed_tables.py" "$out" "$inc" || { echo "MIXED TABLES: FAIL (the tool)"; exit 1; }
status=0
if [ -z "$target" ]; then
	if cmp -s "$inc" "$root/port/linux/game/mixed_coop_tables.inc"; then echo "MIXED TABLES: PASS (the built-in tables are the maps')"
	else echo "MIXED TABLES: FAIL (the built-in tables differ from the maps' made now: $inc)"; status=1; fi
fi
[ $status = 0 ] && [ -z "${HALO_TEST_KEEP:-}" ] && rm -rf "$out"
exit $status
