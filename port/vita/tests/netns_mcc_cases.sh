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
#             HALO_TEST_MCCMENUS_HOST=xbox: an Xbox-files host's Blood Gulch instead (mixed play: the MCC
#             joiner takes its layout); HALO_TEST_MCCMENUS_ONLINE=false: the joiner on its own connection (Same
#             Wi-Fi, the Vita's default) - INTERNET switches it to Online (1.1.3-insider.8k, the owner: it only
#             said "Internet play is off ... restart the game")
#   mcclanmenus  System Link from Classic+'s menus on two MCC-only machines (1.1.3, the Classic+ audit): the
#             host's CREATE LAN - the Xbox's SELECT PROFILE (start to join), System Link list, SELECT MAP,
#             SELECT GAMETYPE and lobby, classic/xbox_netscreens.xml -, the joiner's JOIN LAN: SELECT PROFILE,
#             the System Link list lists the host's game, A on it, the lobby; the host's A starts it and both
#             play. Never a Halo 3 screen, the Halo 3 LAN browser, an empty stand-in or a widget that failed to
#             spawn (screenshots: HALO_TEST_SHOTS)
#   mccxbox   an MCC-only host's System Link game, an Xbox machine (no MCC files) joins: refused as a mixed
#             game (custom_edition_maps_family_mixed) with "This game's maps are a different version from
#             yours; mixed games are coming", stays up, never in the game, no download offered
#   xboxmcc   an Xbox host's System Link game on the Xbox's Blood Gulch, an MCC-only machine joins (mixed play
#             r4, Nov 23 2026: the host's placements taken before the map loads): it plays 60 s at least with
#             two players, placed as the host's layout, matched the host's dictionary
#   xboxlan   the Xbox's maps on both, as before (the baseline: lan's game, the same scoreboard check)
#   mixedmp   mixed multiplayer (mixed_coop.h): HALO_TEST_MIXED="HOST JOINER MAP GAME" (HOST, JOINER: xbox or
#             mcc; MAP: one of the maps whose files are one game - chillout longest prisoner putput ratrace
#             wizard boardingaction, and damnation (an MCC client of an Xbox host: its invisible surfaces
#             collide with nothing), and carousel hangemhigh with an Xbox host and an MCC joiner (Halo PC
#             rebuilt their collision alone: the host's decides, mixed_coop_host_decides_*), and (r4) bloodgulch
#             beavercreek sidewinder with an Xbox host and an MCC joiner (Halo PC placed other scenery and vehicles
#             too: the joiner takes the host's layout before it loads the map, mixed_coop.c's host_layout; the host's
#             collision decides); GAME: slayer, ctf ...). (1.1.3) An MCC host's Xbox joiner sends
#             the host the flag stand's and teleporter base's collision from its own files, which the host takes as the
#             Xbox's (its digest) and plays with: the stands collide on both and agree (HALO_MIXED_STAND_PROBE, CTF
#             flags resting on them); where MCC's needler can spawn (Chill Out, Wizard) the MCC host spawns the Xbox's
#             needler (the owner's needler decision, on since Nov 24 2026); with HALO_TEST_ENV_host=
#             HALO_MIXED_DECISIONS_OFF=needler (or an older host) it is refused, naming the Needler. System Link: an MCC host hosts MCC's map, named the
#             Xbox's on the wire; the joiner joins and plays 60 s at least with two players; a mixed joiner
#             matched the host's dictionary (never refused); both scoreboards agree; the per-tick state
#             hashes (HALO_MIXED_COOP_HASH) agree but for runs of 90 ticks at most, and at the end; the placed
#             objects are the same. HALO_TEST_MIXED_DECISIONS: the owner's pending decisions turned on
#             (HALO_MIXED_DECISIONS, mixed_coop.c)
#   mixedrebuilt  a map Halo PC rebuilt (HALO_TEST_MIXED="HOST JOINER MAP": bloodgulch beavercreek
#             sidewinder carousel hangemhigh with an MCC host and an Xbox joiner), the Xbox joiner refused with
#             the message naming the map as one Halo PC rebuilt and that it needs an Xbox-files host, never in
#             the game
#   mixedold  (1.1.3-insider.8l, the owner: an MCC-only Vita joined an official server's game - its 1.1.1 build plays no
#             mixed games -, loaded MCC's Boarding Action whole and only then was refused) an OLDER host
#             (HALO_TEST_MIXED_OLD_HOST: a build of v1.1.2 or insider.8d, required) on an Xbox level
#             (HALO_TEST_MIXED="xbox JOINER MAP GAME", default "xbox mcc boardingaction ctf"): an MCC joiner never
#             loads the map - refused in the lobby or as the game starts, told the host's version can't play it
#             with MCC's files, never "co-op" in a multiplayer game -, stays up; an Xbox joiner plays 60 s with two
#             players as before
#   mixedvitajoin  the dev Vita's MCC-only join (Nov 22 2026): an Xbox host's Longest CTF, an MCC-only joiner
#             with only the files that Vita had (ui.map, bitmaps.map, a10.map, longest.map, custom_edition's
#             bitmaps.map and loc.map), Menu style Halo 3, its game thread held 6.5 s as it enters the game
#             (the Vita's 5.9 s of the Halo 3 menus' pictures read; "@stall"): the mixedmp checks, and the
#             joiner is never told CONNECTION PROBLEM (error 9) for a host that kept sending
#   mixedvitahost  the dev Vita's MCC-only host (Nov 22 2026): that file set hosts a10 co-op from Menu style
#             Classic, Who can join Vitas and PCs, its lobby's own countdown (no early start), an Xbox
#             machine joins: the game starts, the MCC level loads on the host, the joiner matches its
#             dictionary and both play 60 s at least with two players

mcc_root=${HALO_TEST_MCC_ROOT:-$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1}
# (MCC's own folder, as pcmapjoin takes HALO_TEST_MCC_ROOT, holding halo1: its halo1)
[ -d "$mcc_root/halo1/maps" ] && mcc_root=$mcc_root/halo1

# (each MCC-only machine's scripted player, HALO_TEST_INPUT=bot, kept off the menus by a pad of one wait - the
# scripted player plays only in a level when a pad is given, xinput_sdl.c: since the Classic+ audit an MCC-only
# machine's network lobby is the Xbox's, as an Xbox-files one's, where its presses opened Y PLAYERS and the
# host's KICK kicked the joiner as it came in)

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

# mcc_vita_setup: the dev Vita's MCC file set alone ($out/mcc-vita/halo1/maps: ui.map, bitmaps.map,
# a10.map, longest.map, custom_edition's bitmaps.map and loc.map; no sounds.map, no other level)
mcc_vita_setup() {
	local file
	rm -rf "$out/mcc-vita"
	mkdir -p "$out/mcc-vita/halo1/maps/custom_edition"
	for file in ui.map bitmaps.map a10.map longest.map custom_edition/bitmaps.map custom_edition/loc.map; do
		[ -r "$mcc_root/maps/$file" ] || { echo "SKIP ($mode): no MCC $file at $mcc_root"; exit 0; }
		ln -sfn "$mcc_root/maps/$file" "$out/mcc-vita/halo1/maps/$file"
	done
}

# mixed_vita_case: mixedvitajoin, mixedvitahost (the dev Vita's MCC-only sessions, off the Vita)
mixed_vita_case() {
	mcc_setup
	mcc_vita_setup
	mcc_lan_machine
	if [ $mode = mixedvitajoin ]; then
		copy_data_host=$data
		copy_data_joiner=$out/mcc-none
		run_copy host "$host_machine" "$vita" "$cpu_a" $host_env HALO_NET_ONLINE=false \
			HALO_NETWORK_TEST=host:longest:ctf HALO_CONTENT_ROOT=none HALO_MIXED_COOP_HASH=1; host_pid=$last_pid
		sleep 5
		run_copy joiner "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_TEST_INPUT=bot:2 \
			HALO_MENU_STYLE=halo3 HALO_CONTENT_ROOT="$out/mcc-vita" HALO_MIXED_COOP_HASH=1 \
			"HALO_TEST_COMMANDS=L1:@stall,6500"; join_pid=$last_pid
	else
		# (the lobby's 31 s countdown once the joiner is in, then the level: 240 s at least)
		[ "$seconds" -ge 240 ] || seconds=240
		copy_data_host=$out/mcc-none
		copy_data_joiner=$data
		run_copy host "$host_machine" "$vita" "$cpu_a" HALO_NET_ONLINE=false HALO_NET_COOP_LEVEL=a10 \
			HALO_NETWORK_TEST=host:a10 HALO_NETWORK_TEST_START=150 HALO_TEST_INPUT=bot:1:look HALO_NET_COOP_PLAYERS=2 \
			HALO_NET_WHO_CAN_JOIN=vita_pc HALO_MENU_STYLE=classic HALO_UI_LOG=1 HALO_CONTENT_ROOT="$out/mcc-vita" \
			HALO_MIXED_COOP_HASH=1 HALO_TEST_PAD=wait:150:1000; host_pid=$last_pid
		sleep 5
		run_copy joiner "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_TEST_INPUT=bot:2:look \
			HALO_CONTENT_ROOT=none HALO_MIXED_COOP_HASH=1; join_pid=$last_pid
	fi
	wait $join_pid $host_pid 2>/dev/null
	hl=$out/host/run.log
	jl=$out/joiner/run.log
	echo "--- host"; grep -aE "mixed co-op: (hosting|machine|the dictionary|a10's)|network test: (hosting|map|starting)|h3 pregame: Starting" "$hl" | head -8
	echo "--- joiner"; grep -aE "join: the host's map|mixed co-op: (the host plays|the host's level|refused|[0-9]+ of the host)|test command|h3 dialog" "$jl" | head -10
	two=$(grep -a "network test: tick" "$jl" | grep -a "| playing" | grep -aEc "player [0-9]+:.* player [0-9]+:")
	echo "joiner's seconds with two players playing: $two"
	mcc_no_assertion host
	mcc_no_assertion joiner
	[ "$two" -ge 60 ] || fail "the joiner played the host's game for $two s with two players"
	grep -aq "mixed co-op: the host's level matches this machine's" "$jl" || fail "the joiner never matched the host's dictionary"
	if [ $mode = mixedvitajoin ]; then
		grep -aq "test command: the game thread stalls 6500 ms" "$jl" || fail "the joiner's game thread was never held"
		grep -aq "h3 dialog: error 9" "$jl" && fail "the joiner was told CONNECTION PROBLEM (error 9) after its own long frame"
		mcc_scoreboards
		mixed_hashes
	else
		grep -aq "network test: starting the game" "$hl" && fail "the host's game was started early (the test's, not the lobby's countdown)"
		grep -aq "mixed co-op: a10's MCC level in network co-op loaded" "$hl" || fail "the MCC host never loaded a10"
		# (1.1.3, the owner's 8m: this Vita's few MCC files leave the lobby's first maps - the Xbox's names a10 and
		# carousel, before the co-op level is set - unloadable; their "Couldn't load" is taken back once the lobby is
		# on a10, never left on the host's screen)
		local said withdrawn
		said=$(grep -ac "Halo: map: Couldn't load" "$hl")
		withdrawn=$(grep -ac "Halo: map: withdrawn: Couldn't load" "$hl")
		echo "the host's \"Couldn't load\" messages: $said said, $withdrawn withdrawn"
		[ "$said" -le "$withdrawn" ] || fail "the host kept a stale \"Couldn't load\" message ($(grep -a "Halo: map: Couldn't load" "$hl" | head -1))"
	fi
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

# mixed_hashes: the host's and joiner's per-tick state hashes ("mixed hash <tick> <objects> <hash>",
# debug.txt) differ for runs of 90 ticks at most (the host's word reaches the joiner ticks later) and agree at
# the end; the objects the map placed ("mixed placed") are the same
mixed_hashes() {
	python3 -I - "$out/host/data/debug.txt" "$out/joiner/data/debug.txt" <<'PY' || fail "the host's and joiner's state hashes did not agree"
import re, sys
def load(path):
    ticks, placed = {}, None
    for line in open(path, errors='replace'):
        m = re.search(r'mixed hash (\d+) (\d+) ([0-9a-f]{8})', line)
        if m:
            ticks.setdefault(int(m.group(1)), m.group(3))
        m = re.search(r'mixed placed (\d+) ([0-9a-f]{8})', line)
        if m and placed is None:
            placed = (int(m.group(1)), m.group(2))
    return ticks, placed
(h, hp), (j, jp) = load(sys.argv[1]), load(sys.argv[2])
ticks = sorted(set(h) & set(j))
runs, start = [], None
for index, tick in enumerate(ticks):
    if h[tick] != j[tick]:
        start = tick if start is None else start
        if index + 1 == len(ticks) or h[ticks[index + 1]] == j[ticks[index + 1]] or ticks[index + 1] != tick + 1:
            runs.append((start, tick)); start = None
longest = max((b - a + 1 for a, b in runs), default=0)
ends = bool(ticks) and h[ticks[-1]] != j[ticks[-1]]
print('the state hashes: %d ticks in both, %d differing runs, longest %d ticks%s; placed objects host %s joiner %s' %
      (len(ticks), len(runs), longest, ', ends differing' if ends else '', hp, jp))
sys.exit(0 if len(ticks) >= 600 and longest <= 90 and not ends and hp and hp == jp else 1)
PY
}

# (1.1.3, the owner's flag stand decision, Nov 24 2026) an MCC host playing the flag stand and teleporter base with the
# collision an Xbox joiner sent: each machine's HALO_MIXED_STAND_PROBE lines - a ray down onto each stand ("the stand":
# it collides; "the ground": it does not) and each CTF flag's place - must agree, every stand on the host must be one
# that collides, and every CTF flag on the host must rest on its stand (not fallen through it to the ground)
mixed_stands() {
	python3 -I - "$out/host/run.log" "$out/joiner/run.log" <<'PY' || fail "the host's and joiner's flag stands did not agree"
import re, sys
def load(path):
    stands, flags = {}, {}
    for line in open(path, errors='replace'):
        m = re.search(r'mixed stand probe: (\d+) (flag_base|teleporter_base) ([0-9a-f]{4}) at (\S+) (\S+) (\S+): (the stand|the ground|nothing) z (\S+)', line)
        if m:
            stands.setdefault((m.group(2), m.group(3)), set()).add((m.group(7), round(float(m.group(8)), 2), float(m.group(4)), float(m.group(5))))
            continue
        m = re.search(r'mixed stand probe: (\d+) flag ([0-9a-f]{4}) at (\S+) (\S+) (\S+)', line)
        if m:
            flags.setdefault(int(m.group(1)), {})[m.group(2)] = tuple(float(v) for v in m.group(3, 4, 5))
    return stands, flags
(hs, hf), (js, jf) = load(sys.argv[1]), load(sys.argv[2])
bad = []
if not any(k[0] == 'flag_base' for k in hs):
    bad.append('no flag stand probed on the host')
for key, seen in hs.items():
    if any(r != 'the stand' for r, z, x, y in seen):
        bad.append('host: %s %s does not collide (%s)' % (key[0], key[1], sorted(seen)[:2]))
    if key in js and {(r, z) for r, z, x, y in seen} != {(r, z) for r, z, x, y in js[key]}:
        bad.append('%s %s: host %s, joiner %s' % (key[0], key[1], sorted(seen)[:2], sorted(js[key])[:2]))
tops = [(x, y, z) for (name, index), seen in hs.items() if name == 'flag_base' for r, z, x, y in seen if r == 'the stand']
compared = 0
for tick, flags in hf.items():
    for index, (x, y, z) in flags.items():
        near = [t for t in tops if (t[0] - x) ** 2 + (t[1] - y) ** 2 < 1.0]
        if near and z < min(t[2] for t in near) - 0.1:
            bad.append('host tick %d: flag %s at z %.3f, under its stand\'s top %.3f' % (tick, index, z, min(t[2] for t in near)))
        if tick in jf and index in jf[tick]:
            compared += 1
            jx, jy, jz = jf[tick][index]
            if abs(jx - x) > 0.05 or abs(jy - y) > 0.05 or abs(jz - z) > 0.05:
                bad.append('tick %d flag %s: host %.3f %.3f %.3f, joiner %.3f %.3f %.3f' % (tick, index, x, y, z, jx, jy, jz))
print('the flag stands: host %s; joiner %s; CTF flag places compared %d' % (
    ', '.join('%s %s %s' % (k[0], k[1], sorted({(r, z) for r, z, x, y in v})) for k, v in sorted(hs.items())),
    ', '.join('%s %s %s' % (k[0], k[1], sorted({(r, z) for r, z, x, y in v})) for k, v in sorted(js.items())), compared))
for line in bad[:8]:
    print('  ' + line)
sys.exit(1 if bad else 0)
PY
}

mixed_case() {
	local host_family join_family map game host_name
	local mixed_default="mcc xbox chillout slayer"
	[ $mode = mixedold ] && mixed_default="xbox mcc boardingaction ctf"
	set -- ${HALO_TEST_MIXED:-$mixed_default}
	host_family=$1 join_family=$2 map=$3 game=${4:-slayer}
	mcc_setup
	local host_data=$data join_data=$data host_content=none join_content=none
	[ $host_family = mcc ] && host_data=$out/mcc-none host_content=$out/mcc-content
	[ $join_family = mcc ] && join_data=$out/mcc-none join_content=$out/mcc-content
	host_name=$map
	[ $host_family = mcc ] && host_name=mcc_$map
	copy_data_host=$host_data
	copy_data_joiner=$join_data
	mcc_lan_machine
	local decisions="HALO_MIXED_DECISIONS=${HALO_TEST_MIXED_DECISIONS:-}" host_vita=${HALO_TEST_MIXED_HOST_BIN:-$vita}
	local join_vita=${HALO_TEST_MIXED_JOINER_BIN:-$vita}
	if [ $mode = mixedold ]; then
		host_vita=${HALO_TEST_MIXED_OLD_HOST:-}
		[ -x "$host_vita" ] || { echo "mixedold needs HALO_TEST_MIXED_OLD_HOST (a v1.1.2 or insider.8d build)"; exit 2; }
		[ $host_family = xbox ] || { echo "mixedold: an older host plays the Xbox's files (HALO_TEST_MIXED's HOST xbox)"; exit 2; }
		echo "mixedold: the older host $host_vita"
	fi
	run_copy host "$host_machine" "$host_vita" "$cpu_a" $host_env HALO_NET_ONLINE=false \
		HALO_NETWORK_TEST=host:$host_name:$game HALO_CONTENT_ROOT="$host_content" HALO_MIXED_COOP_HASH=1 HALO_MIXED_STAND_PROBE=1 $decisions \
		$([ "$host_content" != none ] && echo HALO_TEST_PAD=wait:150:1000); host_pid=$last_pid
	sleep 5
	run_copy joiner "$lan" "$join_vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_TEST_INPUT=bot:2 \
		HALO_CONTENT_ROOT="$join_content" HALO_MIXED_COOP_HASH=1 HALO_MIXED_STAND_PROBE=1 $decisions $([ "$join_content" != none ] && echo HALO_TEST_PAD=wait:150:1000); join_pid=$last_pid
	wait $join_pid $host_pid 2>/dev/null
	hl=$out/host/run.log
	jl=$out/joiner/run.log
	echo "--- host ($host_family, $host_name, $game)"; grep -aE "mixed play: hosting|mixed co-op: (hosting|machine|the dictionary)|network test: (hosting|map|starting)|mixed play: the owner" "$hl" | head -8
	echo "--- joiner ($join_family)"; grep -aE "join: the host's map|mixed co-op: (the host plays|the host's dictionary|the host's level|refused|[0-9]+ of the host)|mixed play: |map: |platform: message|Halo: (map|co-op)" "$jl" | head -14
	two=$(grep -a "network test: tick" "$jl" | grep -a "| playing" | grep -aEc "player [0-9]+:.* player [0-9]+:")
	echo "joiner's seconds with two players playing: $two"
	mcc_no_assertion host
	mcc_no_assertion joiner
	case $mode in
	mixedmp)
		# (1.1.3, the owner's flag stand decision, Nov 24 2026: an MCC host and an Xbox joiner - the joiner sends the
		# flag stand's and teleporter base's collision from its own files, the host checks it and plays with it; with
		# HALO_TEST_MIXED_COLLISION=old the joiner is an older build, which never answers the ask: refused as before)
		# (an older build either side - HALO_TEST_MIXED_HOST_BIN or HALO_TEST_MIXED_JOINER_BIN, and
		# HALO_TEST_MIXED_COLLISION=old: no collision asked or sent, refused as before, naming the flag stands)
		# (HALO_TEST_MIXED_COLLISION=tampered: the joiner - HALO_TEST_ENV_joiner=HALO_MIXED_COLLISION_TAMPER=1, a debug
		# build - sends its flag stand a vertex off: the host never takes it, and the joiner is refused as before)
		if [ $host_family = mcc ] && [ $join_family = xbox ] && [ "${HALO_TEST_MIXED_COLLISION:-}" = tampered ]; then
			[ "$two" = 0 ] || fail "the Xbox joiner played with a damaged flag stand collision ($two s)"
			grep -aq "flag_base collision not taken: its digest [0-9a-f]* is none of the Xbox's files'" "$hl" ||
				fail "the host did not refuse the damaged collision by its digest"
			grep -aq "plays with the Xbox's collision now" "$hl" && fail "the host played with a damaged collision"
			grep -aq "this map needs an Xbox-files host" "$jl" || fail "the Xbox joiner was not told the map needs an Xbox-files host"
			return
		fi
		if [ $host_family = mcc ] && [ $join_family = xbox ] && [ "${HALO_TEST_MIXED_COLLISION:-}" = old ]; then
			[ "$two" = 0 ] || fail "the Xbox joiner played the older pair's MCC game ($two s)"
			grep -aq "this map needs an Xbox-files host" "$jl" || fail "the Xbox joiner was not told the map needs an Xbox-files host"
			grep -aq "plays with the Xbox's collision now" "$hl" && fail "the host played with collision an older pair never sends"
			return
		fi
		if [ $host_family = mcc ] && [ $join_family = xbox ]; then
			grep -a "mixed play: machine [0-9]*'s .*collision\|mixed play: scenery.* plays with\|mixed play: machine [0-9]* offers" "$hl" | head -4
			grep -aq "mixed play: machine [0-9]* offers the collision" "$hl" || fail "the Xbox joiner never offered the collision"
			grep -aq "scenery.flag_base.flag_base collision taken (1796 bytes, digest 407d8a48[0-9a-f]*, the Xbox's)" "$hl" ||
				fail "the host did not take the flag stand's collision as the Xbox's"
			grep -aq "mixed play: scenery.flag_base.flag_base plays with the Xbox's collision now" "$hl" ||
				fail "the host's flag stand did not play with the Xbox's collision"
			if grep -aq "mixed play: .* teleporter bases placed [1-9]" "$hl"; then
				grep -aq "mixed play: scenery.teleporter_base.teleporter_base plays with the Xbox's collision now" "$hl" ||
					fail "the host's teleporter base did not play with the Xbox's collision"
			fi
			grep -aq "mixed play: in this game: flag_base\|mixed play: in this game: teleporter_base" "$jl" &&
				fail "the Xbox joiner still found the host's stands different"
			# (Chill Out and Wizard: MCC's needler item collection spawns Halo PC's needler unless the owner's
			# needler decision - on by default since Nov 24 2026 - makes it the Xbox's: off, refused, naming the Needler)
			if grep -aq "mixed play: in this game: needler" "$jl"; then
				[ "$two" = 0 ] || fail "the Xbox joiner played the MCC host's game with Halo PC's needler in it ($two s)"
				grep -aq "games with the Needler need an Xbox-files host" "$jl" || fail "the Xbox joiner was not told about the Needler"
				return
			fi
		fi
		[ "$two" -ge 60 ] || fail "the joiner played the host's game for $two s with two players"
		grep -aq "Halo: co-op\|mixed co-op: refused\|platform: message" "$jl" &&
			fail "the joiner was refused or told something ($(grep -a "Halo: co-op\|mixed co-op: refused\|platform: message" "$jl" | head -1))"
		if [ $host_family = mcc ] || [ $join_family = mcc ]; then
			grep -aq "mixed co-op: the host's level matches this machine's" "$jl" ||
				fail "the joiner never matched the host's dictionary"
		fi
		[ $host_family = mcc ] && { grep -aq "mixed play: hosting levels.test.mcc_$map.mcc_$map from MCC's files under the Xbox's name" "$hl" ||
			fail "the MCC host did not host its map under the Xbox's name"; }
		# (r4: the maps whose placements an MCC joiner takes from an Xbox host)
		case $map in bloodgulch|beavercreek|sidewinder)
			if [ $join_family = mcc ]; then
				grep -aq "mixed play: this host's layout of $map sent to machine" "$hl" || fail "the host never sent its layout"
				grep -aq "mixed play: $map placed as the host's (Xbox) layout" "$jl" ||
					fail "the MCC joiner did not place the host's layout"
			fi ;;
		esac
		mcc_scoreboards
		mixed_hashes
		[ $host_family = mcc ] && [ $join_family = xbox ] && mixed_stands
		true ;;
	mixedold)
		if [ $join_family = xbox ]; then
			[ "$two" -ge 60 ] || fail "the Xbox joiner played the older host's game for $two s with two players"
			grep -aq "mixed co-op: refused\|platform: message" "$jl" &&
				fail "the Xbox joiner was refused or told something ($(grep -a "mixed co-op: refused\|platform: message" "$jl" | head -1))"
			mcc_scoreboards
			return
		fi
		[ "$two" = 0 ] || fail "the MCC joiner played the older host's game ($two s)"
		grep -aqE "mixed play: the host never said it plays mixed \(an older version\): $map refused, (in its lobby|as its game starts), before the map loads" "$jl" ||
			fail "the MCC joiner was not refused before the map loads"
		grep -aq "MCC level in network co-op loaded\|structure bsp switch" "$jl" &&
			fail "the MCC joiner loaded the level before it was refused ($(grep -a "MCC level in network co-op loaded\|structure bsp switch" "$jl" | head -1))"
		grep -aq "Halo: map: This game's host plays .* from the Xbox's files on an older version" "$jl" ||
			fail "the MCC joiner was not told (Halo: map) the host's version can't play it with MCC's files"
		grep -a "Halo: map: \|Halo: co-op: " "$jl" | grep -aqi "co-op" && fail "the multiplayer refusal says co-op"
		echo "the MCC joiner, refused: $(grep -a "Halo: map: " "$jl" | head -1)" ;;
	mixedrebuilt)
		[ "$two" = 0 ] || fail "the joiner played the host's game on a map Halo PC rebuilt ($two s)"
		grep -aq "a map Halo PC rebuilt: refused" "$jl" || fail "the joiner was not refused the rebuilt map"
		grep -aq "Halo: map: Halo PC rebuilt " "$jl" || fail "the joiner was not told Halo PC rebuilt the map"
		grep -aq "needs an Xbox-files host" "$jl" || fail "the joiner was not told the map needs an Xbox-files host" ;;
	esac
}

# mcc_lan_menus_case: mcclanmenus (both machines' screens from their pads: HALO_NETWORK_TEST=watch)
mcc_lan_menus_case() {
	local side shots bad
	mcc_setup
	mcc_lan_machine
	copy_data_host=$out/mcc-none
	copy_data_joiner=$out/mcc-none
	shots() { [ -n "${HALO_TEST_SHOTS:-}" ] && echo "HALO_SCREENSHOT_DIR=$HALO_TEST_SHOTS/netns-$mode-$1"; }
	[ -n "${HALO_TEST_SHOTS:-}" ] && mkdir -p "$HALO_TEST_SHOTS/netns-$mode-host" "$HALO_TEST_SHOTS/netns-$mode-joiner"
	# (the host: MULTIPLAYER, CREATE LAN, A to join, A for its profile, A again: the System Link list, A (no game
	# listed: a new one), the first map, the first game type: the lobby; the joiner's in, its A starts the game)
	local d="down:150:500"
	run_copy host "$host_machine" "$vita" "$cpu_a" HALO_NET_ONLINE=false HALO_NETWORK_TEST=watch HALO_UI_LOG=1 \
		HALO_CONTENT_ROOT="$out/mcc-content" HALO_TEST_INPUT=bot:1 $(shots host) \
		"HALO_TEST_PAD=wait:150:4000 $d a:150:3500 $d $d $d $d a:150:4000 a:150:2000 a:150:2000 a:150:3500 @list a:150:4000 @map a:150:4000 @gametype a:150:4000 @lobby wait:150:${HALO_TEST_LANMENUS_WAIT:-40000} @lobby_two a:150:6000 @starting"; host_pid=$last_pid
	sleep 5
	# (the joiner: MULTIPLAYER, JOIN LAN, A to join, A for its profile, A again: the System Link list; the
	# host's game listed, A on it: the lobby)
	run_copy joiner "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=watch HALO_UI_LOG=1 \
		HALO_CONTENT_ROOT="$out/mcc-content" HALO_TEST_INPUT=bot:2 $(shots joiner) \
		"HALO_TEST_PAD=wait:150:4000 $d a:150:3500 $d a:150:4000 @join a:150:2000 @profile a:150:2000 @ready a:150:12000 @list a:150:5000 @lobby wait:150:8000 @lobby_two"; join_pid=$last_pid
	wait $join_pid $host_pid 2>/dev/null
	for side in host joiner; do
		echo "--- $side"
		grep -a "ui: screen" "$out/$side/run.log" | sed 's/.*ui: screen //' | uniq | tr '\n' ' ' | cut -c1-700; echo
		mcc_no_assertion $side
		mcc_unavailable $side
		bad=$(grep -aE "an empty widget stands in|stands for nothing|the handler opens nothing|LAN browser in its place|menus: Halo 3 screen|ui: screen pc\.(h3|online)\." "$out/$side/run.log" | head -2)
		[ -n "$bad" ] && fail "$side: $bad"
		grep -aqE "failed to spawn widget|may not run event handler function" "$out/$side/data/debug.txt" 2>/dev/null &&
			fail "$side: debug.txt: $(grep -aE "failed to spawn widget|may not run event handler function" "$out/$side/data/debug.txt" | head -1)"
		for screen in 'connected.4way_profile_select.4way_start2join_screen' 'connected.server_list.server_list_screen' \
			'connected.pregame.connected_pregame_screen'; do
			grep -aqE "ui: screen .*$screen" "$out/$side/run.log" || fail "$side: $screen did not open"
		done
	done
	for screen in connected_map_select_wrapper gametype_select_screen_wrapper; do
		grep -aqE "ui: screen .*$screen" "$out/host/run.log" || fail "host: $screen did not open"
	done
	two=$(grep -a "network test: tick" "$out/joiner/run.log" | grep -a "| playing" | grep -aEc "player [0-9]+:.* player [0-9]+:")
	echo "joiner's seconds with two players playing: $two"
	[ "$two" -ge 20 ] || fail "the joiner played the host's game for $two s with two players"
}

mcc_case() {
	local host_data=$data join_data=$data host_content=none join_content=none host_game=host:bloodgulch:slayer
	case $mode in mixedmp|mixedrebuilt|mixedold) mixed_case; return ;; mixedvitajoin|mixedvitahost) mixed_vita_case; return ;; esac
	[ $mode = mcclanmenus ] && { mcc_lan_menus_case; return; }
	case $mode in
	mcclan|mccmenus) host_data=$out/mcc-none join_data=$out/mcc-none host_content=$out/mcc-content
		join_content=$out/mcc-content host_game=host:mcc_bloodgulch:slayer ;;
	esac
	# (mccmenus with an Xbox-files host: its Blood Gulch, mixed)
	[ $mode = mccmenus ] && [ "${HALO_TEST_MCCMENUS_HOST:-mcc}" = xbox ] && host_data=$data host_content=none \
		host_game=host:bloodgulch:slayer
	case $mode in
	mccxbox) host_data=$out/mcc-none host_content=$out/mcc-content host_game=host:mcc_bloodgulch:slayer ;;
	xboxmcc) join_data=$out/mcc-none join_content=$out/mcc-content ;;
	esac
	[ $mode = xboxlan ] || mcc_setup
	copy_data_host=$host_data
	copy_data_joiner=$join_data
	if [ $mode = mccmenus ]; then
		# (internet play: the host's game public; the joiner's Classic+ menus - MULTIPLAYER, INTERNET: the
		# profile, then the server browser, A on the host's row; then the System Link list, joined by itself)
		# (the host's scripted player kept off its lobby's buttons - a pad of its own, one wait: since the
		# Classic+ audit the MCC host's lobby is the Xbox's, whose Y opens PLAYERS, and the scripted player's
		# presses there kicked the joiner as it came in)
		run_copy host "$host_machine" "$vita" "$cpu_a" $host_env HALO_NETWORK_TEST=$host_game \
			HALO_CONTENT_ROOT="$host_content" HALO_NETWORK_TEST_START=${HALO_TEST_MENUS_START:-75} \
			HALO_NET_LOBBY_NAME=MccHost HALO_NET_HOST_PUBLIC=true HALO_TEST_PAD=wait:150:1000; host_pid=$last_pid
		code=$(wait_code)
		[ -n "$code" ] || { fail "the host never showed a code"; tail -20 "$out/host/run.log"; exit 1; }
		echo "host's code: $code"
		pad="wait:150:3000 down a wait:150:2000 a wait:150:2000 a wait:150:2000 a wait:150:2000 a"
		pad="$pad wait:150:${HALO_TEST_MENUS_LIST_WAIT:-12000} a"
		run_copy joiner "$join_machine" "$vita" "$cpu_b" HALO_NET_ONLINE=${HALO_TEST_MCCMENUS_ONLINE:-true} HALO_NETWORK_TEST=watch \
			HALO_UI_LOG=1 HALO_CONTENT_ROOT="$join_content" HALO_TEST_INPUT=bot:2 "HALO_TEST_PAD=$pad"; join_pid=$last_pid
	else
		mcc_lan_machine
		run_copy host "$host_machine" "$vita" "$cpu_a" $host_env HALO_NET_ONLINE=false HALO_NETWORK_TEST=$host_game \
			HALO_CONTENT_ROOT="$host_content" $([ "$host_content" != none ] && echo HALO_TEST_PAD=wait:150:1000); host_pid=$last_pid
		sleep 5
		run_copy joiner "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_TEST_INPUT=bot:2 \
			HALO_CONTENT_ROOT="$join_content" $([ "$join_content" != none ] && echo HALO_TEST_PAD=wait:150:1000); join_pid=$last_pid
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
		if [ $mode != xboxlan ] && [ "$host_content" != none ]; then
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
			[ "${HALO_TEST_MCCMENUS_ONLINE:-true}" = false ] && { grep -aq "menus: INTERNET: internet play was off: the connection switched to Online" "$jl" ||
				fail "INTERNET did not switch the joiner's connection to Online"; }
			[ "${HALO_TEST_MCCMENUS_HOST:-mcc}" = xbox ] && { grep -aq "mixed co-op: the host's level matches this machine's" "$jl" ||
				fail "the MCC joiner never matched the Xbox host's dictionary"; }
		fi
		;;
	mccxbox)
		mcc_unavailable host
		[ "$two" = 0 ] || fail "the Xbox joiner played the MCC host's game ($two s)"
		# (Blood Gulch: one of the six maps Halo PC rebuilt, never mixed - mixed_coop.c)
		grep -aq "map: the host's mcc_bloodgulch is of another map family than this machine's (the Xbox's maps here), a map Halo PC rebuilt: refused" "$jl" ||
			fail "the Xbox joiner was not refused the MCC host's map"
		grep -aq "Halo: map: Halo PC rebuilt Blood Gulch" "$jl" ||
			fail "the Xbox joiner was not told Halo PC rebuilt Blood Gulch"
		grep -aiq "download it from\|Download .*?" "$jl" && fail "the Xbox joiner was offered a download"
		;;
	xboxmcc)
		# (r4: an Xbox host's Blood Gulch plays mixed, its placements the MCC joiner's: mixed_coop.c)
		mcc_unavailable joiner
		[ "$two" -ge 60 ] || fail "the MCC joiner played the Xbox host's game for $two s with two players"
		grep -aq "mixed play: bloodgulch placed as the host's (Xbox) layout" "$jl" ||
			fail "the MCC joiner did not place the Xbox host's layout"
		grep -aq "mixed co-op: the host's level matches this machine's" "$jl" || fail "the MCC joiner never matched the host's dictionary"
		grep -aq "Halo: map\|mixed co-op: refused" "$jl" && fail "the MCC joiner was refused ($(grep -a "Halo: map\|mixed co-op: refused" "$jl" | head -1))"
		;;
	esac
}
