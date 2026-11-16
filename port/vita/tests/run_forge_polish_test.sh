#!/bin/bash
# Forge polish (1.1.4, polish/114-forge): the cases of its fixes, each a headless
# run of the Linux harness in a Forge game started by the console
# (HALO_SYSTEM_LINK_TEST=forge:<map>, Menu style Halo CE) and its log read.
#
#   monitorprompt  the monitor's HUD never shows the hidden Spartan's action
#               prompt (hud.c hud_show_action_response: "Hold X to pick up",
#               "enter vehicle"): a weapon let go at the monitor's feet, where
#               its hidden unit stands - the game still works out the unit's
#               action (forge where: an action on the weapon) -, the prompt
#               is left out of the monitor's frames and drawn again for the
#               Spartan (forge off)
#
#   onlinemenu  Menu style Online's Forge menu (online/forge.xml, written from
#               halo3/forge.xml by tools/online_menu_forge.py) has every row of
#               the Halo 3 one: by the pad, Start, six down (past RESUME, PLAY AS
#               SPARTAN, SAVE MAP VARIANT, UNDO, REDO, SNAP) lands on MAGNETS, A
#               turns them off; down, A steps NUDGE to 10 cm (1.1.3's file was
#               never written again after 1.1.2 added the two rows: it had
#               neither, and its fourteenth row was RESUME again)
#
#   mccblocks   Forge's building blocks on an install from MCC alone (no Xbox
#               map in the maps folder, MCC's halo1 folder as the Halo content:
#               HALO_TEST_MCC_ROOT, default Steam's; SKIP without): a Forge
#               game of MCC's Blood Gulch builds blocks_bloodgulch from MCC's
#               own map (scene_pack.c build_blocks on the Custom Edition
#               builder's tags: before 1.1.4 "the recipe's blocks need an Xbox
#               map" and the browser's BLOCKS was empty), the browser lists 17
#               blocks, a wall is placed and a shot's line meets it
#   mccblockstool  the same pack by the tool (build/linux/scene_pack_test build,
#               a recipe naming MCC's bloodgulch.map as a Custom Edition map):
#               built twice to the same bytes, passing the pack check with no
#               correction, its walls and floors the Xbox build's bitmaps
#
#   run_forge_polish_test.sh [CASE...]      (default: all)
#   HALO_TEST_VITA        the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA_MENUS  a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_OUT         where the logs go (kept with HALO_TEST_KEEP=1)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
. "$here/test_data_root.sh"
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA_MENUS:-$data_root/triage/menus/data}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_forge_polish_test.$$}
export HALO_MENUS_FOLDER=${HALO_MENUS_FOLDER:-$root/port/vita/app0/menus/}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-monitorprompt onlinemenu mccblocks mccblockstool}
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -r "$data/maps/bloodgulch.map" ]; then
	echo "SKIP: no maps in $data/maps (HALO_TEST_DATA_MENUS)"
	exit 0
fi

# run NAME SECONDS COMMANDS [VAR=value...]: a Forge game of Blood Gulch by the console
run() {
	local name=$1 seconds=$2 commands=$3 dir="$out/$1"
	shift 3
	rm -rf "$dir"
	mkdir -p "$dir/data" "$dir/save" "$dir/shots"
	ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy HALO_DATA_ROOT="$dir/data" HALO_SAVE_ROOT="$dir/save" \
		HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 \
		HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false HALO_UI_LOG=1 \
		HALO_MENU_STYLE=classic HALO_SYSTEM_LINK_TEST=forge:9 HALO_TEST_FORGE_PACKS=none \
		HALO_SCREENSHOT_DIR="$dir/shots" HALO_TEST_COMMANDS="$commands" "$@" \
		timeout -k 5 $((seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
	echo $? > "$dir/exit"
	rm -rf "$dir/save"
}

mcc=${HALO_TEST_MCC_ROOT:-$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1}

# run_mcc NAME SECONDS COMMANDS [VAR=value...]: as run, on an install from MCC alone: the maps folder
# empty, MCC's halo1 folder linked into a Halo content folder of the run's (never copied)
run_mcc() {
	local name=$1 seconds=$2 commands=$3 dir="$out/$1"
	shift 3
	rm -rf "$dir"
	mkdir -p "$dir/data/maps" "$dir/save" "$dir/shots" "$dir/content/halo1"
	ln -sfn "$mcc/maps" "$dir/content/halo1/maps"
	[ -d "$mcc/sound" ] && ln -sfn "$mcc/sound" "$dir/content/halo1/sound"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy HALO_DATA_ROOT="$dir/data" HALO_SAVE_ROOT="$dir/save" \
		HALO_CONTENT_ROOT="$dir/content" \
		HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 \
		HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false HALO_UI_LOG=1 \
		HALO_MENU_STYLE=classic HALO_SYSTEM_LINK_TEST=forge:9 HALO_TEST_FORGE_PACKS=none \
		HALO_SCREENSHOT_DIR="$dir/shots" HALO_TEST_COMMANDS="$commands" "$@" \
		timeout -k 5 $((seconds + 180)) "$binary" > "$dir/run.log" 2>&1)
	echo $? > "$dir/exit"
	rm -rf "$dir/save"
}

checked() {
	local log="$out/$1/run.log" code
	code=$(cat "$out/$1/exit" 2>/dev/null)
	if [ "$code" != 0 ]; then
		fail "$1: the harness exited with $code ($log)"
		return 1
	fi
	if grep -av "render assertion skipped" "$log" | grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
		fail "$1: an assertion or exception ($log)"
		return 1
	fi
	return 0
}

# expect NAME TEXT PATTERN: a line of the log matches PATTERN (grep -E)
expect() {
	if grep -aqE "$3" "$out/$1/run.log"; then
		echo "  ok: $1: $2"
	else
		fail "$1: $2 ($out/$1/run.log)"
	fi
}
# refuse NAME TEXT PATTERN: no line of the log matches PATTERN
refuse() {
	if grep -aqE "$3" "$out/$1/run.log"; then
		fail "$1: $2 ($out/$1/run.log)"
	else
		echo "  ok: $1: $2"
	fi
}

for c in $cases; do
	echo "--- $c"
	case $c in
	monitorprompt)
		# (the monitor stays where it started: its hidden unit is right under
		# the camera; a weapon spawned a little in front, drawn in to 0.6 world
		# units and let go lands at the unit's feet, within its reach)
		cmd="L300:forge spawn weapons\;L306:forge distance 0.6;L312:forge drop;L400:forge where;L402:@shot 00-monitor"
		cmd="$cmd;L410:forge off;L470:forge where;L472:@shot 01-spartan;L480:forge on;L540:forge where"
		run $c 30 "$cmd"
		checked $c || continue
		expect $c "the weapon let go at the monitor's feet" "forge: dropped Assault Rifle"
		expect $c "the hidden unit's action is on the weapon (forge where: an action with an object)" "forge: the player's unit at .* action [1-9][0-9]* on #-?[0-9]+"
		expect $c "the prompt left out of the monitor's frames" "forge: the Spartan's action prompt not drawn for the monitor"
		expect $c "drawn again for the Spartan" "forge: the Spartan's action prompt drawn again"
		# (the order: not drawn (the monitor at the start), drawn again (forge
		# off), not drawn (forge on))
		seq=$(grep -aoE "forge: the Spartan's action prompt (not drawn for the monitor|drawn again)" "$out/$c/run.log" | \
			sed 's/.*prompt //' | tr '\n' ',')
		[ "$seq" = "not drawn for the monitor,drawn again,not drawn for the monitor," ] && echo "  ok: $c: in order" || \
			fail "$c: the prompt's log order: $seq"
		;;
	onlinemenu)
		d="down:150:400"
		run $c 40 "L700:forge where" HALO_MENU_STYLE=online \
			"HALO_TEST_PAD=level:150:3000 start:150:2000 @00-menu:0:300 $d $d $d $d $d $d @01-magnets:0:300 a:150:800 $d a:150:800 @02-nudge:0:300 b:150:1500"
		checked $c || continue
		expect $c "the Forge menu opened (Menu style Online)" "ui: screen pc.online.forge.pause.screen"
		expect $c "MAGNETS: A on the seventh row turns them off" "forge: magnets off"
		expect $c "NUDGE: A on the eighth row steps it to 10 cm" "forge: nudge step 10 cm"
		;;
	mccblocks)
		if [ ! -r "$mcc/maps/bloodgulch.map" ]; then
			echo "SKIP: no MCC bloodgulch.map in $mcc/maps (HALO_TEST_MCC_ROOT)"; continue
		fi
		# (the pack built as the game asks for it, before the Forge game starts: the
		# session's first commands come after; a wall spawned 4 world units off and let
		# go, a projectile's line from the camera meets it)
		cmd="L300:forge list;L306:forge spawn wall;L312:forge distance 4;L318:forge drop;L330:forge blocks ray;L336:@shot 00-wall"
		run_mcc $c 60 "$cmd"
		checked $c || continue
		expect $c "the blocks' recipe falls back to MCC's own map" "scene packs: blocks_bloodgulch: no Xbox map of its in the maps folder: built from MCC's own bloodgulch.map"
		expect $c "built from it, the Xbox build's bitmaps" "scene packs: blocks_bloodgulch: the blocks' walls of levels.test.bloodgulch.bitmaps.cap_metal, floors of levels.b30.bitmaps.metal flat generic"
		expect $c "loaded for the Forge game" "forge: blocks: blocks_bloodgulch loaded"
		expect $c "the browser's BLOCKS: 17" "forge: palette blocks: 17"
		expect $c "a wall placed" "forge: spawned Wall \\(wall, blocks\\)"
		expect $c "a shot's line meets it" "forge: blocks: ray from .* meets the object forge.blocks.wall "
		;;
	mccblockstool)
		tool=$root/build/linux/scene_pack_test
		if [ ! -r "$mcc/maps/bloodgulch.map" ] || [ ! -x "$tool" ]; then
			echo "SKIP: no MCC bloodgulch.map in $mcc/maps or no $tool"; continue
		fi
		mkdir -p "$out/$c"
		python3 -I - "$mcc/maps/bloodgulch.map" "$out/$c/recipe.txt" <<'PYEOF'
import sys, struct, os
path, recipe = sys.argv[1], sys.argv[2]
header = open(path, 'rb').read(0x800)
open(recipe, 'w').write("pack blocks_bloodgulch\ncemap bloodgulch %d %08x\nblocks\n" % (os.path.getsize(path), struct.unpack_from('<I', header, 0x64)[0]))
PYEOF
		for n in 1 2; do
			"$tool" build "$out/$c/recipe.txt" "$mcc/maps" "$out/$c/blocks_$n.pack" > "$out/$c/build_$n.log" 2>&1 || fail "$c: build $n failed ($out/$c/build_$n.log)"
		done
		if [ -s "$out/$c/blocks_1.pack" ] && cmp -s "$out/$c/blocks_1.pack" "$out/$c/blocks_2.pack"; then
			echo "  ok: $c: built twice to the same bytes ($(stat -c %s "$out/$c/blocks_1.pack") bytes)"
		else
			fail "$c: the two builds differ or are empty"
		fi
		grep -q "the blocks' walls of levels.test.bloodgulch.bitmaps.cap_metal, floors of levels.b30.bitmaps.metal flat generic" "$out/$c/build_1.log" && \
			echo "  ok: $c: the Xbox build's bitmaps" || fail "$c: the bitmaps chosen ($out/$c/build_1.log)"
		"$tool" check "$out/$c/blocks_1.pack" > "$out/$c/check.log" 2>&1 && grep -q "56 tags from bloodgulch" "$out/$c/check.log" && \
			grep -q "0 corrections" "$out/$c/check.log" && echo "  ok: $c: 56 tags, checked with no correction" || fail "$c: the pack check ($out/$c/check.log)"
		;;
	*)
		fail "unknown case $c"
		;;
	esac
done
test_out_done $status
exit $status
