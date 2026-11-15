#!/bin/bash
# A machine whose map never loads no longer holds a lobby's start for good
# (network_server_manager.c, network_game_server_pregame_load_timeout; after
# ChupathingyCE cfb560935, #1060: a dedicated server's lobby held 45 minutes
# by one machine that never said it had the map).
#
# run_netns_online_test.sh's code mode: the host hosts Damnation, which the
# joiner's cache partition does not hold, and the joiner's memory card is
# made so slow (xbox_files.c's HALO_IO_THROTTLE_WRITE_KBPS, 5 KB/s) that its copy
# of the map takes far longer than the test. The host's countdown ends; the
# joiner has not loaded the map HALO_TEST_PREGAME_LOAD_TIMEOUT seconds (30,
# a debug build's stand-in for the 180 s shipped) later, so the host drops
# it, says so, and its lobby is free again: one machine left (a network
# game needs two, so this one waits for another joiner; with more machines
# the rest start). Before, the host's lobby waited on that machine for good.
#
#   run_pregame_load_timeout_test.sh
#
#   HALO_TEST_VITA   the Linux build (build/linux/halo of this tree, not --release)
#   HALO_TEST_DATA   a folder with the game's maps folder (../data2276)
#   HALO_TEST_OUT    the output folder (kept with HALO_TEST_KEEP=1)
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
data=${HALO_TEST_DATA:-$root/../data2276}
timeout_seconds=${HALO_TEST_PREGAME_LOAD_TIMEOUT:-30}
out=${HALO_TEST_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/pregame_load_timeout_test.XXXXXX")}
. "$here/test_out.sh"
test_out_begin "$out"
# (the joiner's cache partition holds the menus' ui.map already, copied by a
# first start offline at full speed, as run_join_copy_hitch_test.sh does)
mkdir -p "$out/net/joiner/save" "$out/prefill/data"
ln -sfn "$(cd "$data/maps" && pwd)" "$out/prefill/data/maps"
(cd "$out/prefill" && env -u DISPLAY SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/prefill/data" \
	HALO_SAVE_ROOT="$out/net/joiner/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER=15 HALO_FULLSCREEN=0 \
	HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_NET_ONLINE=false HALO_UPDATE_AUTO=false HALO_TICK_THREAD=1 \
	timeout 120 "${HALO_TEST_VITA:-$root/build/linux/halo}" > "$out/prefill/run.log" 2>&1)
grep -aq "starting precaching of map 'ui'" "$out/prefill/data/debug.txt" ||
	{ echo "FAIL (pregame load timeout): the first start did not copy ui.map"; test_out_done 1; exit 1; }
HALO_TEST_OUT=$out/net HALO_TEST_KEEP=1 HALO_TEST_HOST_GAME=host:damnation:slayer HALO_TEST_DATA=$data \
	HALO_TEST_HOST_ENV="HALO_NETWORK_TEST_START=${HALO_TEST_START:-120} HALO_TEST_PREGAME_LOAD_TIMEOUT=$timeout_seconds" \
	HALO_TEST_SECONDS=${HALO_TEST_SECONDS:-420} \
	HALO_TEST_JOIN_ENV="HALO_IO_THROTTLE_KBPS=10000 HALO_IO_THROTTLE_WRITE_KBPS=5" \
	"$here/run_netns_online_test.sh" code > "$out/netns.log" 2>&1
host_logs=$(ls "$out"/net/host/run.log "$out"/net/host/data/debug.txt 2>/dev/null)
joiner_logs=$(ls "$out"/net/joiner/run.log "$out"/net/joiner/data/debug.txt 2>/dev/null)
status=0
fail() { echo "FAIL (pregame load timeout): $*"; status=1; }
[ -n "$host_logs" ] || fail "no host log under $out/net"
dropped=$(cat $host_logs /dev/null | grep -a "has not loaded the map $timeout_seconds seconds after the countdown ended; dropping it" | head -1)
echo "host: ${dropped:-no drop line}"
[ -n "$dropped" ] || fail "the host never dropped the machine that had not loaded the map"
cat $joiner_logs /dev/null | grep -aq "starting precaching of map 'damnation'" || fail "the joiner never began copying damnation"
# (the host's lobby after the drop: its machine alone)
awk '/has not loaded the map/ { dropped = 1; next } dropped && /player count 1 machine count 1/ { found = 1 } END { exit !found }' \
	"$out/net/host/data/debug.txt" || fail "the host's lobby still held the dropped machine"
[ $status -eq 0 ] && echo "PASS (pregame load timeout): the machine still copying the map dropped after $timeout_seconds s, the lobby free again"
test_out_done $status
exit $status
