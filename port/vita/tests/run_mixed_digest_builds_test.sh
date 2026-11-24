#!/bin/bash
# run_mixed_digest_builds_test.sh BIN_A BIN_B [FAMILIES]: mixed play's digests (mixed_coop.c) computed by two
# DIFFERENT builds of the game (a release and a debug Linux build; or another version's) must agree, tag by tag, on
# all 10 campaign levels and the 13 stock multiplayer maps, for each file set (FAMILIES: "xbox mcc" by default).
# A digest that hashes a pointer into the executable differs from build to build: two machines of different builds
# (a Vita and a PC) then refuse each other's game ("gameplay differs") - 1.1.3's holo controls on a30 a50 b30 b40
# c10 c40 d40 did. Each level is loaded alone (data/init.txt), no network (unshare -rn), headless.
# Needs HALO_TEST_DATA (the Xbox maps folder's parent, default ~/github/vitaprojects/halo-ce/data2276) and for the
# mcc family HALO_TEST_MCC_ROOT (a Halo content folder: <root>/halo1/maps). Exit 0 when every level agrees.
a=${1:?BIN_A} b=${2:?BIN_B} families=${3:-"xbox mcc"}
data=${HALO_TEST_DATA:-$HOME/github/vitaprojects/halo-ce/data2276}
mccroot=${HALO_TEST_MCC_ROOT:-$HOME/.steam/steam/steamapps/common/Halo The Master Chief Collection}
out=${HALO_TEST_OUT:-$(mktemp -d /tmp/mixed-digest-XXXXXX)}
sdl=${HALO_TEST_SDL:-$HOME/github/vitaprojects/halo-ce/sdl3-i686/lib}
levels=${HALO_TEST_LEVELS:-"a10 a30 a50 b30 b40 c10 c20 c40 d20 d40 chillout longest prisoner putput ratrace wizard boardingaction damnation carousel hangemhigh bloodgulch beavercreek sidewinder"}
secs=${HALO_TEST_SECONDS:-25}
mkdir -p "$out"
load() { # load WHICH BIN FAMILY LEVEL: the level's digests into $out/WHICH-FAMILY-LEVEL.txt
	local which=$1 bin=$2 fam=$3 level=$4 d name content mp=1
	d=$out/$which-$fam-$level; rm -rf "$d"; mkdir -p "$d/data" "$d/save" "$d/home"
	case $level in a10|a30|a50|b30|b40|c10|c20|c40|d20|d40) mp=0;; esac
	if [ "$fam" = xbox ]; then ln -s "$data/maps" "$d/data/maps"; content=none
		[ $mp = 1 ] && name="levels\\test\\$level\\$level" || name="levels\\$level\\$level"
	else mkdir -p "$d/data/maps"; content=$mccroot
		[ $mp = 1 ] && name="levels\\test\\mcc_$level\\mcc_$level" || name="mods\\mcc_$level"; fi
	printf 'map_name %s\n' "$name" > "$d/data/init.txt"
	(cd "$d" && env -u DISPLAY -u WAYLAND_DISPLAY HOME=$d/home XDG_CONFIG_HOME=$d/home/.config XDG_DATA_HOME=$d/home/.local/share \
		SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LD_LIBRARY_PATH=$sdl HALO_DATA_ROOT=$d/data HALO_SAVE_ROOT=$d/save \
		HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER=$secs HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 \
		HALO_UPDATE_AUTO=false HALO_UPDATE_CHECK=false HALO_DISCORD_APPLICATION= HALO_CRASH_REPORTS=no HALO_NET_ONLINE=false \
		HALO_CONTENT_ROOT="$content" HALO_MIXED_COOP_DIGEST_DUMP=1 HALO_MIXED_COOP_FORCE=1 \
		unshare -rn timeout -k 5 $((secs + 60)) "$bin" > run.log 2>&1)
	# (kind, path hash, digest and name of every tag; the level line's digests)
	grep -a "mixed digest: " "$d/run.log" | sed 's/^.*mixed digest: //' > "$out/$which-$fam-$level.txt"
	rm -rf "$d/save"
}
status=0
for fam in $families; do
	[ "$fam" = mcc ] && [ ! -d "$mccroot/halo1/maps" ] && { echo "SKIP mcc: no $mccroot/halo1/maps"; continue; }
	for level in $levels; do
		load a "$a" $fam $level & load b "$b" $fam $level & wait
		na=$(wc -l < "$out/a-$fam-$level.txt"); nb=$(wc -l < "$out/b-$fam-$level.txt")
		if [ "$na" -lt 2 ] || [ "$nb" -lt 2 ]; then
			echo "FAIL $fam $level: no digests (a $na lines, b $nb; logs $out/a-$fam-$level, $out/b-$fam-$level)"; status=1; continue
		fi
		if cmp -s "$out/a-$fam-$level.txt" "$out/b-$fam-$level.txt"; then
			echo "PASS $fam $level: $((na - 1)) tags' digests agree"
		else
			echo "FAIL $fam $level: the two builds' digests differ:"
			diff "$out/a-$fam-$level.txt" "$out/b-$fam-$level.txt" | grep '^[<>]' | head -8 | sed 's/^/    /'
			status=1
		fi
	done
done
[ $status = 0 ] && echo "MIXED DIGESTS ACROSS BUILDS: PASS" || echo "MIXED DIGESTS ACROSS BUILDS: FAIL"
echo "output in $out"
exit $status
