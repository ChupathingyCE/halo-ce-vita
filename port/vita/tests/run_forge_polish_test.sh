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
cases=${*:-monitorprompt}
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
	*)
		fail "unknown case $c"
		;;
	esac
done
test_out_done $status
exit $status
