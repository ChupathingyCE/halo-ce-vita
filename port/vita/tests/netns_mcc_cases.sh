# Halo: MCC's maps in multiplayer (1.1.3: cache_files.c's CACHE_FILES_REGION_MCC), cases of
# run_netns_online_test.sh, sourced by it: its run_copy, fail, holder, in_ns, wait_code, $out, $vita,
# $cpu_*, $host_machine, $join_machine, $host_router and $host_env. An MCC-only machine is a data folder
# whose maps folder is empty and a Halo content folder (HALO_CONTENT_ROOT) whose halo1/maps links this PC's
# Steam MCC maps, read only, never copied (HALO_TEST_MCC_ROOT: MCC's halo1 folder; SKIP without one). Its
# menus are MCC's ui.map (build 01.03.43.0000), which insider.8d refused for multiplayer ("Your maps (build
# 01.03.43.0000) aren't supported for multiplayer yet"). An Xbox machine is the test's Xbox maps
# (HALO_TEST_DATA) with no content folder (HALO_CONTENT_ROOT=none).
#   mcclan    System Link: an MCC-only host plays Slayer on MCC's Blood Gulch (mcc_bloodgulch), an MCC-only
#             joiner on its LAN joins it by the System Link search and plays: two players 60 s at least, and
#             the two machines' scoreboards (each player's kills and deaths) the same at the same tick
#   mccmenus  internet play from Classic+'s menus (Menu style classic, OpenCE's multiplayer screens) on an
#             MCC-only joiner: MULTIPLAYER, INTERNET opens (never "multiplayer unavailable"), the server
#             browser lists the MCC-only host's public game, the joiner joins it and plays
#   mccxbox   an MCC-only host's System Link game, an Xbox machine (no MCC files) joins: refused as a mixed
#             game (custom_edition_maps_family_mixed) with "This game's maps are a different version from
#             yours; mixed games are coming", stays up, never in the game, no download offered
#   xboxmcc   an Xbox host's System Link game on the Xbox's Blood Gulch, an MCC-only machine joins: refused the
#             same way, stays up, never in the game
#   xboxlan   the Xbox's maps on both, as before (the baseline: lan's game, the same scoreboard check)

mcc_root=${HALO_TEST_MCC_ROOT:-$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1}
# (MCC's own folder, as pcmapjoin takes HALO_TEST_MCC_ROOT, holding halo1: its halo1)
[ -d "$mcc_root/halo1/maps" ] && mcc_root=$mcc_root/halo1

# mcc_setup: the MCC-only data folder ($out/mcc-none, its maps folder empty) and the content folder
# ($out/mcc-content/halo1/maps: links to MCC's files)
mcc_setup() {
	if [ ! -r "$mcc_root/maps/ui.map" ] || [ ! -r "$mcc_root/maps/bloodgulch.map" ] ||
		[ ! -r "$mcc_root/maps/custom_edition/bitmaps.map" ]; then
		echo "SKIP ($mode): no MCC halo1 folder at $mcc_root (HALO_TEST_MCC_ROOT)"
		exit 0
	fi
	rm -rf "$out/mcc-content" "$out/mcc-none"
	mkdir -p "$out/mcc-content/halo1/maps" "$out/mcc-none/maps"
	local file
	for file in "$mcc_root"/maps/*.map "$mcc_root"/maps/custom_edition; do
		ln -sfn "$file" "$out/mcc-content/halo1/maps/"
	done
	# (MCC's sounds and the rest of halo1 beside maps, as Steam has them)
	for file in "$mcc_root"/*; do
		[ "$(basename "$file")" = maps ] || ln -sfn "$file" "$out/mcc-content/halo1/"
	done
}

# mcc_lan_machine: a machine on the host's LAN ($lan), as lan's
mcc_lan_machine() {
	holder; lan=$held
	in_ns "$host_router" ip link add l2_host type veth peer name m2_host
	in_ns "$host_router" ip link set m2_host netns "$lan"
	in_ns "$host_router" ip link set l2_host master b_host
	in_ns "$host_router" ip link set l2_host up
	in_ns "$lan" ip link set lo up
	in_ns "$lan" ip addr add 192.168.1.3/24 broadcast 192.168.1.255 dev m2_host
	in_ns "$lan" ip link set m2_host up
	in_ns "$lan" ip route add default via 192.168.1.1
}

# mcc_no_assertion NAME: no assertion, exception or halt in NAME's log, and it ran to its end
mcc_no_assertion() {
	local log=$out/$1/run.log
	grep -av "render assertion skipped" "$log" | grep -aqiE "assertion|exception|halt" &&
		fail "$1: an assertion, exception or halt ($(grep -av "render assertion skipped" "$log" |
			grep -aiE "assertion|exception|halt" | head -1))"
	grep -aq "XLaunchNewImage" "$log" && fail "$1 went to the dashboard"
	grep -aq "damaged disc error\|error dialog [0-9]* (the disc" "$log" && fail "$1 showed the damaged disc error"
	grep -aq "exiting after debug.exit_after" "$log" || fail "$1 did not run to its end (hung?)"
}

# mcc_unavailable NAME: NAME was never told its maps don't play multiplayer
mcc_unavailable() {
	grep -aq "multiplayer is unavailable" "$out/$1/run.log" &&
		fail "$1 was told multiplayer is unavailable ($(grep -a "multiplayer is unavailable" "$out/$1/run.log" | head -1))"
}

# mcc_scoreboards: the host's and the joiner's scoreboards (each player's score, kills and deaths) at the
# same tick agree, 85% of the samples at least (a kill reaches a client a tick or two apart), 30 at least
mcc_scoreboards() {
	python3 -I - "$out/host/run.log" "$out/joiner/run.log" <<'PY' || fail "the joiner's scoreboard did not agree with the host's"
import re, sys
over = []
def read(path):
    boards = {}
    for line in open(path, errors='replace'):
        m = re.search(r'network test: tick (\d+) (.*)\| (playing|game over)', line)
        if not m:
            continue
        players = re.findall(r'player (\d+): .*?s(-?\d+) k(\d+) d(\d+)', m.group(2))
        if len(players) >= 2:
            boards[int(m.group(1))] = sorted(players)
            if m.group(3) == 'game over':
                over.append(sorted(players))
    return boards
host = read(sys.argv[1])
host_over, over[:] = list(over), []
joiner = read(sys.argv[2])
joiner_over = list(over)
samples = agree = 0
host_ticks = sorted(host)
for tick, board in joiner.items():
    if not host_ticks:
        break
    nearest = min(host_ticks, key=lambda t: abs(t - tick))
    if abs(nearest - tick) > 30:
        continue
    samples += 1
    agree += host[nearest] == board
last = joiner[max(joiner)] if joiner else None
print('the scoreboards: %d of %d samples agree; the joiner\'s last: %s' % (agree, samples, last))
# (the game's end, when it came: the same final scoreboard on both, as (player, score, kills, deaths))
ended = bool(host_over) and bool(joiner_over)
print('the game\'s end: host %s, joiner %s' % (host_over[-1] if host_over else None, joiner_over[-1] if joiner_over else None))
sys.exit(0 if samples >= 30 and agree >= 0.85 * samples and (not ended or host_over[-1] == joiner_over[-1]) else 1)
PY
}

mcc_case() {
	local host_data=$data join_data=$data host_content=none join_content=none host_game=host:bloodgulch:slayer
	case $mode in
	mcclan|mccmenus) host_data=$out/mcc-none join_data=$out/mcc-none host_content=$out/mcc-content
		join_content=$out/mcc-content host_game=host:mcc_bloodgulch:slayer ;;
	mccxbox) host_data=$out/mcc-none host_content=$out/mcc-content host_game=host:mcc_bloodgulch:slayer ;;
	xboxmcc) join_data=$out/mcc-none join_content=$out/mcc-content ;;
	esac
	[ $mode = xboxlan ] || mcc_setup
	copy_data_host=$host_data
	copy_data_joiner=$join_data
	if [ $mode = mccmenus ]; then
		# (internet play: the host's game public; the joiner's Classic+ menus - MULTIPLAYER, INTERNET: the
		# profile, then the server browser, A on the host's row; then the System Link list, joined by itself)
		run_copy host "$host_machine" "$vita" "$cpu_a" $host_env HALO_NETWORK_TEST=$host_game \
			HALO_CONTENT_ROOT="$host_content" HALO_NETWORK_TEST_START=${HALO_TEST_MENUS_START:-75} \
			HALO_NET_LOBBY_NAME=MccHost HALO_NET_HOST_PUBLIC=true; host_pid=$last_pid
		code=$(wait_code)
		[ -n "$code" ] || { fail "the host never showed a code"; tail -20 "$out/host/run.log"; exit 1; }
		echo "host's code: $code"
		pad="wait:150:3000 down a wait:150:2000 a wait:150:2000 a wait:150:2000 a wait:150:2000 a"
		pad="$pad wait:150:${HALO_TEST_MENUS_LIST_WAIT:-12000} a"
		run_copy joiner "$join_machine" "$vita" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=watch HALO_UI_LOG=1 \
			HALO_CONTENT_ROOT="$join_content" HALO_TEST_INPUT=bot:2 "HALO_TEST_PAD=$pad"; join_pid=$last_pid
	else
		mcc_lan_machine
		run_copy host "$host_machine" "$vita" "$cpu_a" $host_env HALO_NET_ONLINE=false HALO_NETWORK_TEST=$host_game \
			HALO_CONTENT_ROOT="$host_content"; host_pid=$last_pid
		sleep 5
		run_copy joiner "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_TEST_INPUT=bot:2 \
			HALO_CONTENT_ROOT="$join_content"; join_pid=$last_pid
	fi
	wait $join_pid $host_pid 2>/dev/null
	hl=$out/host/run.log
	jl=$out/joiner/run.log
	echo "--- host"; grep -aE "starting with|menus: the menus' map|multiplayer is unavailable|system link: |network test: (hosting|map|starting)|join: the host's map" "$hl" | head -8
	echo "--- joiner"; grep -aE "multiplayer is unavailable|menus: (OpenCE|Server Browser|joining)|ui: screen pc.browser|system link: (in |joining)|network test: (join|search)|map: |platform: message|Halo: (map|custom map)" "$jl" | head -14
	two=$(grep -a "network test: tick" "$jl" | grep -a "| playing" | grep -aEc "player [0-9]+:.* player [0-9]+:")
	echo "joiner's seconds with two players playing: $two"
	mcc_no_assertion host
	mcc_no_assertion joiner
	case $mode in
	mcclan|mccmenus|xboxlan)
		mcc_unavailable host
		mcc_unavailable joiner
		[ "$two" -ge $([ $mode = mccmenus ] && echo 30 || echo 60) ] ||
			fail "the joiner played the host's game for $two s with two players"
		mcc_scoreboards
		if [ $mode != xboxlan ]; then
			grep -aq "join: the host's map is levels.test.mcc_bloodgulch.mcc_bloodgulch (copy 0x" "$jl" ||
				fail "the joiner did not check the host's MCC map by its copy"
			for side in host joiner; do
				grep -aq "starting with Halo MCC's files alone\|the menus' map: MCC's\|ui.map: MCC's\|halo1.maps.ui.map" "$out/$side/run.log" ||
					grep -aq "build 01.03.43.0000\|MCC's ui.map" "$out/$side/data/debug.txt" 2>/dev/null ||
					echo "(note: no line names MCC's ui.map in the $side's log)"
			done
		fi
		if [ $mode = mccmenus ]; then
			grep -aq "menus: OpenCE's multiplayer screens: .* added" "$jl" || fail "the joiner did not add OpenCE's screens"
			grep -aq "ui: screen pc.browser.screen" "$jl" || fail "the server browser did not open"
			grep -aq "menus: joining the public game MccHost" "$jl" || fail "the joiner did not join the host's game from the browser"
			grep -aq "system link: in another's lobby\|system link: in a network game" "$jl" || fail "the joiner did not get into the host's game"
		fi
		;;
	mccxbox)
		mcc_unavailable host
		[ "$two" = 0 ] || fail "the Xbox joiner played the MCC host's game ($two s)"
		grep -aq "map: the host's mcc_bloodgulch is of another map family than this machine's (the Xbox's maps here): refused" "$jl" ||
			fail "the Xbox joiner was not refused the MCC host's map"
		grep -aq "Halo: map: This game's maps are a different version from yours; mixed games are coming." "$jl" ||
			fail "the Xbox joiner was not told the game's maps are a different version"
		grep -aiq "download it from\|Download .*?" "$jl" && fail "the Xbox joiner was offered a download"
		;;
	xboxmcc)
		mcc_unavailable joiner
		[ "$two" = 0 ] || fail "the MCC joiner played the Xbox host's game ($two s)"
		grep -aq "map: the host's bloodgulch is of another map family than this machine's (MCC's maps here): refused" "$jl" ||
			fail "the MCC joiner was not refused the Xbox host's map"
		grep -aq "Halo: map: This game's maps are a different version from yours; mixed games are coming." "$jl" ||
			fail "the MCC joiner was not told the game's maps are a different version"
		;;
	esac
}
