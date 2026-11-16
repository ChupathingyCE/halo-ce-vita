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
#   itemsstay   weapons and equipment placed in Forge stay where they are put
#               (owner, Nov 15: "every time he placed a gun down on the building
#               blocks, the guns would fall through"): an assault rifle held 0.08
#               into a large platform's top, a frag grenade held 0.5 inside a large
#               block (1.1.3 lifted an item only within its bounding radius of the
#               top: the grenade fell through to the ground), an overshield on the
#               ground; 5 s on each lies on its top; 40 s on all three are still
#               there (1.1.0 to 1.1.3-rc.9: a multiplayer game takes away every item
#               not held for 30 s, game_engine_update_purge, and the layout's never
#               came back before the next round); the rifle taken out of the world
#               comes back at its spot its RESPAWN (30 s) later
#               (forge.c forge_item_life_update, forge_physics.inc forge_item_unburied)
#
#   itemsstack  weapons let go inside stacked and overlapping blocks come out on the
#               top of the whole stack: a rifle 0.2 into a large block with a small
#               platform on it (the lower's top and the upper's underside one plane:
#               before, read as "under the upper one", the rifle fell to the ground),
#               a grenade 0.2 into the lower of two blocks stacked, a rifle 0.2 into a
#               block a large block stands 0.3 into (before: lifted on to the block's
#               top, inside the large block, out of sight); a rifle let go in the air
#               under a bridge is not lifted (forge_physics.inc forge_item_inside_solid)
#
#   itemsfall   a weapon let go high in the air keeps the spot where it lands: an
#               assault rifle let go 20 above the ground falls for more than the
#               editor's 3 s of following (forge.c FORGE_SETTLE_MILLISECONDS), so its
#               placed spot was where it was in the air 3 s on (z ~5): 30 s later the
#               game took it from the ground as "away from its spot" and put it back
#               in the air, falling again; now an item is followed until it comes to
#               rest (at most 20 s): its spot on the ground, still there at 40 s
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
cases=${*:-monitorprompt onlinemenu mccblocks mccblockstool itemsstay itemsstack itemsfall}
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
	itemsstay)
		# (SNAP off; a large platform at 86 -150 3 (its top at 3.125), a large block at
		# 92 -150 3 (4.0); the rifle 0.08 into the platform's top, the grenade 0.5 into
		# the block's, the overshield let go over the ground; listed at 5 s, at 40 s;
		# the rifle (#3) destroyed, listed 35 s after)
		cmd="L240:forge snap off;L250:forge camera 81 -150 3 0 0;L258:forge spawn platform_large;L259:forge distance 5;L266:forge drop"
		cmd="$cmd;L270:forge camera 87 -150 3 0 0;L278:forge spawn block_large;L279:forge distance 5;L286:forge drop"
		cmd="$cmd;L290:forge camera 83 -150 3.045 0 0;L312:forge spawn Assault Rifle;L313:forge distance 3;L318:forge drop"
		cmd="$cmd;L322:forge camera 89 -150 3.5 0 0;L344:forge spawn Frag Grenade;L345:forge distance 3;L350:forge drop"
		cmd="$cmd;L354:forge camera 95 -150 1.5 0 0;L376:forge spawn Overshield;L377:forge distance 3;L382:forge drop"
		cmd="$cmd;L530:forge objects;L532:forge camera 80 -155 6 40 -25;L545:@shot 00-placed;L1600:forge objects;L1602:@shot 01-after-40s"
		cmd="$cmd;L1610:forge destroy #3;L1620:forge objects;L2700:forge objects"
		run $c 105 "$cmd"
		checked $c || continue
		# listing N (1..4), object K: "state z"
		item() {
			awk -v n="$1" -v k="$2" '/test command at tick [0-9]+: forge objects/ { l++ }
				l == n && $0 ~ ("forge: object " k ": ") { for (i = 1; i <= NF; i++) if ($i == "at") { z = $(i + 3); sub(",", "", z); print $(i - 1), z; exit } }' "$out/$c/run.log"
		}
		on() { # LISTING OBJECT LOW HIGH
			item "$1" "$2" | awk -v lo="$3" -v hi="$4" '{ exit !($1 == "alive" && $2 >= lo && $2 <= hi) }'
		}
		on 1 2 3.07 3.5 && echo "  ok: $c: the rifle held into the platform's top lies on it ($(item 1 2))" ||
			fail "$c: the rifle is not on the platform's top 5 s on: $(item 1 2)"
		on 1 3 3.95 4.4 && echo "  ok: $c: the grenade held 0.5 inside the large block lies on its top ($(item 1 3))" ||
			fail "$c: the grenade is not on the large block's top 5 s on: $(item 1 3)"
		expect $c "the grenade lifted from inside the block" "forge: frag grenade let go 0\.[45][0-9] into block_large: put on its top"
		on 2 2 3.07 3.5 && on 2 3 3.95 4.4 && [ "$(item 2 4 | cut -d' ' -f1)" = alive ] &&
			echo "  ok: $c: 40 s on the rifle, the grenade and the overshield are still there" ||
			fail "$c: 40 s on: the rifle $(item 2 2), the grenade $(item 2 3), the overshield $(item 2 4)"
		[ "$(item 3 2 | cut -d' ' -f1)" = gone ] && echo "  ok: $c: the rifle taken out of the world" || fail "$c: the rifle not gone: $(item 3 2)"
		expect $c "its RESPAWN counted" "forge: assault rifle \(#3\) gone from its spot: back in 30 s"
		expect $c "back at its spot 30 s later" "forge: assault rifle \(#3\) respawned after 30 s at 8[5-6]\.[0-9]+ -1(49|50)\.[0-9]+ 3\.[0-4]"
		# (respawned: the entry moves to the layout's end, object 4)
		on 4 4 3.07 3.5 && echo "  ok: $c: and lies on the platform again ($(item 4 4))" || fail "$c: the respawned rifle: $(item 4 4)"
		;;
	itemsstack)
		# (SNAP off, a row at y -140: a large block at 81 (top 4.0) with a small platform at
		# 4.125 on it (its top 4.25); a block at 87 with a block at 4.0 on it (top 4.5); a
		# block at 93 (top 3.5) with a large block 0.3 into it at 3.3 (top 4.3); a bridge at
		# 99 (its underside at 2.875); the items held at 3.8, 3.3, 3.3 and 2.0, listed 5 s on)
		cmd="L240:forge snap off"
		t=250
		for b in "76 block_large 3" "76 platform_small 4.125" "82 block 3" "82 block 4" "88 block 3" "88 block_large 3.3" "94 bridge 3"; do
			set -- $b
			cmd="$cmd;L$t:forge camera $1 -140 $3 0 0;L$((t + 8)):forge spawn $2;L$((t + 9)):forge distance 5;L$((t + 16)):forge drop"
			t=$((t + 20))
		done
		for i in "78 3.8 Assault Rifle" "84 3.3 Frag Grenade" "90 3.3 Assault Rifle" "96 2.0 Assault Rifle"; do
			set -- $i
			x=$1 z=$2
			shift 2
			cmd="$cmd;L$t:forge camera $x -140 $z 0 0;L$((t + 22)):forge spawn $*;L$((t + 23)):forge distance 3;L$((t + 28)):forge drop"
			t=$((t + 32))
		done
		cmd="$cmd;L$((t + 150)):forge objects;L$((t + 152)):forge camera 80 -147 7 40 -25;L$((t + 165)):@shot 00-stacks"
		run $c 60 "$cmd"
		checked $c || continue
		# (objects 7..10: the four items; "state z" of listing 1)
		item() {
			awk -v k="$1" '/test command at tick [0-9]+: forge objects/ { l++ }
				l == 1 && $0 ~ ("forge: object " k ": ") { for (i = 1; i <= NF; i++) if ($i == "at") { z = $(i + 3); sub(",", "", z); print $(i - 1), z; exit } }' "$out/$c/run.log"
		}
		on() { # OBJECT LOW HIGH
			item "$1" | awk -v lo="$2" -v hi="$3" '{ exit !($1 == "alive" && $2 >= lo && $2 <= hi) }'
		}
		on 7 4.2 4.7 && echo "  ok: $c: the rifle let go in the large block lies on the small platform stacked on it ($(item 7))" ||
			fail "$c: the rifle in the lower of a stack is not on the stack's top (4.25): $(item 7)"
		on 8 4.45 4.9 && echo "  ok: $c: the grenade let go in the lower of two blocks lies on the upper's top ($(item 8))" ||
			fail "$c: the grenade in the lower of two stacked blocks is not on the upper's top (4.5): $(item 8)"
		on 9 4.25 4.7 && echo "  ok: $c: the rifle let go where two blocks overlap lies on the higher top ($(item 9))" ||
			fail "$c: the rifle in two overlapping blocks is not on the large block's top (4.3): $(item 9)"
		on 10 -5 2.6 && echo "  ok: $c: the rifle let go in the air under the bridge stays under it ($(item 10))" ||
			fail "$c: the rifle let go under the bridge was lifted: $(item 10)"
		expect $c "lifted out of the block on the lower one" "forge: assault rifle let go 0\.[0-9]+ into platform_small: put on its top at .* \(out of the blocks on it too\)"
		;;
	itemsfall)
		# (a rifle let go at 95 -150 20, over the ground at ~0.1; listed 8 s on and 40 s on)
		cmd="L240:forge camera 92 -150 20 0 0;L262:forge spawn Assault Rifle;L263:forge distance 3;L268:forge drop"
		cmd="$cmd;L510:forge objects;L1500:forge objects"
		run $c 70 "$cmd"
		checked $c || continue
		# (listing N, object 0: "state z placed-z")
		item() {
			awk -v n="$1" '/test command at tick [0-9]+: forge objects/ { l++ }
				l == n && $0 ~ "forge: object 0: " { for (i = 1; i <= NF; i++) { if ($i == "at" && !z) { z = $(i + 3); sub(",", "", z); s = $(i - 1) }
					if ($i == "placed") { p = $(i + 4); sub(",", "", p) } } print s, z, p; exit }' "$out/$c/run.log"
		}
		item 1 | awk '{ exit !($1 == "alive" && $2 < 2 && $3 < 2) }' &&
			echo "  ok: $c: the rifle landed and its placed spot is where it landed ($(item 1))" ||
			fail "$c: 8 s on, the rifle's placed spot is not where it landed (state, z, placed z: $(item 1))"
		item 2 | awk '{ exit !($1 == "alive" && $2 < 2) }' &&
			echo "  ok: $c: 40 s on it is still there ($(item 2))" ||
			fail "$c: 40 s on, the rifle is not on the ground where it landed (state, z, placed z: $(item 2))"
		refuse $c "never taken from where it landed" "forge: assault rifle \(#1\) gone from its spot"
		;;
	*)
		fail "unknown case $c"
		;;
	esac
done
test_out_done $status
exit $status
