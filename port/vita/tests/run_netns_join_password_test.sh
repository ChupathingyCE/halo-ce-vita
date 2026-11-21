#!/bin/bash
# A game with a password, run for real, offline: a host (network_test.c's
# "host:", with network.lobby_password and internet play on) and a joiner of
# system link's "join" (internet play off: straight to the host's game
# port), each with its own data and save folders (maps linked, never
# copied), in a network namespace of the test's own with no way out to any
# real network (unshare -rn: nothing reaches a broker, STUN server, relay or
# the Linode's servers). Each case runs a pair once:
#
#   lan        the two on the host's local network (loopback addresses
#              127.0.0.50 and .51), the game with a password: the joiner
#              joins as ever
#   internet   the two at internet addresses (203.0.113.2 and .3, TEST-NET-3
#              on a dummy interface of the namespace), the game with a
#              password: the host refuses the joiner with the password's
#              reason (_rejection_code_bad_password), the joiner never joins
#   nopass     the same addresses, the game without a password: the joiner
#              joins as ever
#
# Before the fix (network_join_password.c) internet joined: a game with a
# password took a machine that reached its port from the internet without
# it, since only the server browser's listing asks for the password.
#
#   run_netns_join_password_test.sh [CASE...]   (default: all three)
#
#   HALO_TEST_VITA   the Linux build (build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder (../data2276)
#   HALO_TEST_OUT    the output folder (kept with HALO_TEST_KEEP=1 or on a failure)
#   HALO_TEST_SECONDS  each pair's run (70)
# Needs unprivileged user namespaces (unshare -rn) and ip; no root.
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
# (the maps found before the namespace: in it, git takes the checkout for
# another user's)
. "$here/test_data_root.sh"
export HALO_TEST_DATA=${HALO_TEST_DATA:-$data_root/data2276}
if [ "${HALO_JOIN_PASSWORD_NETNS:-}" != 1 ]; then
	exec env HALO_JOIN_PASSWORD_NETNS=1 unshare -rn "$0" "$@"
fi
data=$HALO_TEST_DATA
vita=${HALO_TEST_VITA:-$root/build/linux/halo}
seconds=${HALO_TEST_SECONDS:-70}
password=gulch-night
out=${HALO_TEST_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/join_password_test.XXXXXX")}
. "$here/test_out.sh"
test_out_begin "$out"
# (mktemp made it: the test's own)
[ -z "${HALO_TEST_OUT:-}" ] && test_out_made=1
mkdir -p "$out"
[ -x "$vita" ] || { echo "FAIL (join password): no build at $vita"; test_out_done 1; exit 1; }
[ -d "$data/maps" ] || { echo "SKIP (join password): no maps folder at $data/maps"; test_out_done 0; exit 0; }
ip link set lo up &&
	ip link add d0 type dummy &&
	ip addr add 203.0.113.2/24 dev d0 &&
	ip addr add 203.0.113.3/24 dev d0 &&
	ip link set d0 up ||
	{ echo "FAIL (join password): the namespace's addresses could not be made"; test_out_done 1; exit 1; }
# (no route out: nothing but this namespace's own addresses answers)
if ip route get 8.8.8.8 > /dev/null 2>&1; then
	echo "FAIL (join password): the namespace has a route out"; test_out_done 1; exit 1
fi

pids=
cleanup() {
	local pid
	for pid in $pids; do kill -9 "$pid" 2> /dev/null; done
	for pid in $pids; do wait "$pid" 2> /dev/null; done
	pids=
}
trap cleanup EXIT

# copy NAME ADDRESS BROADCAST [ENV...]: a copy of the game in $out/$case/NAME
copy() {
	local name=$1 address=$2 broadcast=$3 folder=$out/$case/$1
	shift 3
	mkdir -p "$folder/data" "$folder/save" "$folder/home"
	ln -sfn "$(cd "$data" && pwd)/maps" "$folder/data/maps"
	(cd "$folder" && exec env -u DISPLAY -u WAYLAND_DISPLAY HOME="$folder/home" SDL_AUDIODRIVER=dummy \
		SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$folder/data" HALO_SAVE_ROOT="$folder/save" HALO_NO_VSYNC=1 \
		HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 \
		HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_DISCORD_APPLICATION= HALO_NET_ALLOW_UPNP=false \
		HALO_MENU_STYLE=classic HALO_BOTS_ONLINE=off HALO_NET_BROKERS=127.0.0.1:9 HALO_NET_STUN=127.0.0.1:9 \
		HALO_NET_RELAYS= HALO_NET_ADDRESS="$address" HALO_NET_BROADCAST="$broadcast" "$@" \
		timeout -k 5 $((seconds + 60)) "$vita" > "$folder/run.log" 2>&1) &
	pids="$pids $!"
}

status=0
fail() { echo "FAIL (join password, $case): $*"; status=1; }
REFUSED_ON_HOST="the game has a password, and the machine did not come through internet play"
REFUSED_ON_JOINER="unable to join game: reason= #3/_rejection_code_bad_password"
JOINED="successfully joined a net game"

# play HOST_ADDRESS JOINER_ADDRESS PASSWORD: the pair's run, its logs in $host_log, $joiner_log
play() {
	copy host "$1" "$2" HALO_NET_ONLINE=true HALO_NET_LOBBY_PASSWORD="$3" HALO_NETWORK_TEST=host:bloodgulch:slayer \
		HALO_NETWORK_TEST_START=45
	sleep 8
	copy joiner "$2" "$1" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join
	for pid in $pids; do wait "$pid" 2> /dev/null; done
	pids=
	host_log=$(cat "$out/$case/host/run.log" "$out/$case/host/data/debug.txt" 2> /dev/null)
	joiner_log=$(cat "$out/$case/joiner/run.log" "$out/$case/joiner/data/debug.txt" 2> /dev/null)
	grep -q "server added machine" <<< "$host_log" || fail "the host never took its own machine: $out/$case/host"
}

cases=${*:-lan internet nopass}
for case in $cases; do
	before=$status
	status=0
	case $case in
	lan)
		play 127.0.0.50 127.0.0.51 "$password"
		grep -q "$JOINED" <<< "$joiner_log" || fail "a machine on the local network did not join"
		grep -q "$REFUSED_ON_HOST" <<< "$host_log" && fail "the host refused a machine on its local network"
		;;
	internet)
		play 203.0.113.2 203.0.113.3 "$password"
		grep -q "$JOINED" <<< "$joiner_log" && fail "a machine at an internet address joined without the password"
		grep -q "$REFUSED_ON_HOST" <<< "$host_log" || fail "the host did not refuse it for the password"
		grep -q "$REFUSED_ON_JOINER" <<< "$joiner_log" || fail "the joiner was not told the password's reason"
		;;
	nopass)
		play 203.0.113.2 203.0.113.3 ""
		grep -q "$JOINED" <<< "$joiner_log" || fail "a machine at an internet address did not join a game without a password"
		grep -q "$REFUSED_ON_HOST" <<< "$host_log" && fail "the host refused for a password it does not have"
		;;
	*)
		fail "no such case"
		;;
	esac
	[ $status -eq 0 ] && echo "ok (join password, $case)"
	[ $before -eq 0 ] || status=1
done
[ $status -eq 0 ] && echo "PASS (join password): $cases"
test_out_done $status
exit $status
