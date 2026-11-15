# Network Command's cases of run_netns_online_test.sh (port/linux/game/
# command.c's command_net.inc), sourced by it: its run_copy, fail, $out, $vita,
# $cpu_*, $host_machine, $lan and forge_lan_machine. Every machine on the host's
# LAN (System Link): the host hosts a Command game of Blood Gulch (the network
# test's host:bloodgulch:command: network Forge's session, played, its hello's
# Command bit), the joiners join it by the System Link search, each on a side
# (HALO_NETWORK_TEST_TEAM: 0 red, 1 blue; the other team from the host's by
# default). Each commander plays by the console (HALO_TEST_COMMANDS: command.c's
# commands; a client's are requests to the host).
#   command      1v1: the host red, the joiner blue. Both buy squads and order
#                them (the joiner's through the host), the joiner buys its
#                Spartan and takes it over (first person), leaves it, takes it
#                again; each sees the other's squads; screenshots from both
#   commandcoop  co-op: the joiner on red with the host (blue the AI
#                commander's): both buy for one side, order its squads, share
#                its resources; the Spartan first come
#   commanddrop  1v1, the joiner leaves the game mid-game while in its
#                Spartan: the Spartan the AI's again, blue the AI commander's
#   commandjoin  a joiner of the game in progress (blue: the AI commander's
#                until it comes), given the whole state; then it commands blue
#   commandfuzz  1v1, the joiner sends the host a volley of bad requests
#                (malformed, another side's, off the map, replayed, a burst):
#                dropped and counted, the host plays on
#   commandold   a joiner that does not play network Command (HALO_FORGE_NET=0,
#                an older build's stand-in) is refused from the game
#   h3command    the Command lobby by the controller (Menu style Halo 3, HALO_TEST_PROFILE):
#                the host opens COMMAND, NETWORK: SYSTEM LINK - its lobby live, a Command game of
#                Blood Gulch hosted -; a joiner joins it from the multiplayer lobby's BROWSE
#                GAMES and is shown in the lobby's players, blue; START COMMAND (the host's)
#                takes both into the Command game, the joiner commanding blue

command_shots() { # NAME: the copy's screenshots at the Vita's picture, in $out/shots/NAME
	mkdir -p "$out/shots/$1"
	echo "HALO_SCREENSHOT_DIR=$out/shots/$1 HALO_GL_VITA_DISPLAY=1 HALO_RENDER_SCALE=1"
}

command_host() { # command_host COMMANDS [ENV...]
	local commands=$1
	shift
	run_copy host "$host_machine" "$vita" "$cpu_a" HALO_NET_ONLINE=false HALO_NETWORK_TEST=host:bloodgulch:command \
		HALO_NETWORK_TEST_START=${HALO_TEST_COMMAND_START:-20} HALO_TEST_FORGE_PACKS=none "HALO_TEST_COMMANDS=$commands" "$@"
	host_pid=$last_pid
}

command_joiner() { # command_joiner NAME NETNS COMMANDS [ENV...]
	local name=$1 ns=$2 commands=$3
	shift 3
	run_copy "$name" "$ns" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_TEST_FORGE_PACKS=none \
		"HALO_TEST_COMMANDS=$commands" "$@"
	join_pid=$last_pid
}

command_check_clean() { # no assertion, exception or crash in the copies' logs
	local log
	for log in "$@"; do
		[ -f "$log" ] || continue
		grep -av "assertion skipped (torn read?)" "$log" | grep -aqiE "assert|exception|halt_and_catch_fire|segmentation" &&
			fail "$(basename "$(dirname "$log")"): an assertion, exception or crash"
	done
}

command_case() {
	local hl=$out/host/run.log jl=$out/joiner/run.log host_cmds join_cmds
	case $mode in
	command|commandfuzz)
		host_cmds="L60:command net;L90:command money red 2000;L100:command money blue 2000;L120:command buy marines red"
		host_cmds="$host_cmds;L130:command buy barracks red;L140:command buy armory red;L600:command select first"
		host_cmds="$host_cmds;L610:command move 70 -120 1;L620:command where;L900:command net;L1300:command where"
		host_cmds="$host_cmds;L1320:command camera 60 -110 300 34;L1360:@shot command-host;L1500:command net;L1900:command where"
		join_cmds="L80:command net;L200:command buy marines;L210:command buy barracks;L220:command buy armory"
		join_cmds="$join_cmds;L700:command select first;L710:command move 65 -110 1;L900:command buy spartan;L950:command net"
		join_cmds="$join_cmds;L1050:command possess;L1100:@shot command-joiner-spartan;L1150:command where;L1200:command leave"
		join_cmds="$join_cmds;L1260:command camera 60 -110 120 34;L1300:@shot command-joiner-above;L1320:command possess"
		join_cmds="$join_cmds;L1380:command net"
		[ $mode = commandfuzz ] && join_cmds="$join_cmds;L1400:command net junk;L1450:command net;L1600:command where"
		join_cmds="$join_cmds;L1700:command leave;L1800:command where"
		command_host "$host_cmds" $(command_shots host)
		sleep 5
		command_joiner joiner "$lan" "$join_cmds" $(command_shots joiner)
		wait $join_pid $host_pid 2>/dev/null
		command_check_clean "$hl" "$jl"
		echo "--- host"; grep -aE "command net: |command: (buy|player|in:|out|red's Spartan|blue's Spartan|.* is commanded)" "$hl" | head -60
		echo "--- joiner"; grep -aE "command net: |command: (in:|out|the host's Command game)" "$jl" | head -40
		grep -aq "forge net: machine 1 plays the Forge game" "$hl" || fail "the joiner did not answer the Command game's hello"
		grep -aq "forge net: the host's game is a Command game" "$jl" || fail "the joiner was not told the game is a Command game"
		grep -aq "command: the host's Command game on .*, commanding blue" "$jl" || fail "the joiner did not command blue"
		grep -aq "command: blue is commanded by its human players" "$hl" || fail "blue was not the joiner's on the host"
		grep -aq "command net: the host's game had" "$jl" || fail "the joiner was not given the host's game"
		grep -aq "command net: machine 1 (blue) buys marines" "$hl" || fail "the joiner's buy did not reach the host"
		grep -aq "command: buy: blue bought marines for 40" "$hl" || fail "the joiner's marines were not bought"
		grep -aqE "command net: machine 1 \(blue\) ordered [1-9] squads" "$hl" || fail "the joiner's order was not made"
		grep -aq "command: buy: blue bought spartan" "$hl" || fail "the joiner's Spartan was not bought"
		grep -aq "command: in: player 1 in blue's Spartan" "$hl" || fail "the joiner did not take its Spartan over (host)"
		grep -aq "command: in: first person in the Spartan" "$jl" || fail "the joiner was not in first person in its Spartan"
		grep -aq "command: out (the client's combo): player 1 left blue's Spartan" "$hl" || fail "the joiner did not leave its Spartan"
		grep -aq "command: out: above the map again" "$jl" || fail "the joiner was not above the map again"
		grep -aq "command: buy: red bought marines for 40" "$hl" || fail "the host's marines were not bought"
		# (each side's squads on the other machine: the joiner's table has red's)
		grep -aqE "command net: .*state entries taken [1-9]" "$jl" || fail "the joiner took no state"
		if [ $mode = commandfuzz ]; then
			grep -aq "command net: junk: [0-9]* bad requests sent to the host" "$jl" || fail "the joiner did not send its junk"
			grep -aqE "command net: machine 1: [1-9][0-9]* malformed, [1-9][0-9]* replayed, [1-9][0-9]* rate limited, [1-9][0-9]* refused requests dropped" "$hl" ||
				fail "the host did not drop and count the joiner's junk"
			grep -aq "for red refused: its player commands the other side" "$hl" || fail "the joiner's claim of red was not refused"
			grep -aq "order off the map" "$hl" || fail "the joiner's order off the map was not refused"
			grep -aq "test command at tick 1900: command where" "$hl" || fail "the host did not play on after the junk"
		fi
		;;
	commandcoop)
		host_cmds="L90:command money red 2000;L120:command buy marines;L130:command buy barracks;L140:command buy armory"
		host_cmds="$host_cmds;L600:command select first;L610:command move 70 -120 1;L900:command net;L1000:command where"
		host_cmds="$host_cmds;L1200:command possess;L1250:command net;L1320:command camera 75 -140 300 34;L1360:@shot coop-host"
		join_cmds="L200:command buy grunts;L700:command select all;L710:command move 72 -125 1;L900:command buy spartan"
		join_cmds="$join_cmds;L900:command net;L1100:command possess;L1150:command net;L1300:command possess;L1330:command net"
		join_cmds="$join_cmds;L1340:command camera 75 -140 120 34;L1360:@shot coop-joiner;L1500:command where"
		command_host "$host_cmds" $(command_shots host)
		sleep 5
		command_joiner joiner "$lan" "$join_cmds" HALO_NETWORK_TEST_TEAM=0 $(command_shots joiner)
		wait $join_pid $host_pid 2>/dev/null
		command_check_clean "$hl" "$jl"
		echo "--- host"; grep -aE "command net: |command: (buy|player|in:|out|.* is commanded)" "$hl" | head -60
		echo "--- joiner"; grep -aE "command net: |command: (in:|out|the host's Command game)|Another" "$jl" | head -40
		grep -aq "command: the host's Command game on .*, commanding red" "$jl" || fail "the joiner did not command red with the host"
		grep -aq "command: blue is commanded by the AI commander" "$hl" || grep -aq "command net: hosting the Command game (red human, blue the AI's)" "$hl" ||
			fail "blue was not the AI commander's"
		grep -aq "command: buy: red bought grunts for 40" "$hl" || fail "the joiner's buy was not red's"
		grep -aqE "command net: machine 1 \(red\) ordered [1-9] squads" "$hl" || fail "the joiner did not order red's squads"
		grep -aq "command: buy: red bought spartan" "$hl" || fail "the joiner did not buy red's Spartan"
		grep -aq "command: in: player 1 in red's Spartan" "$hl" || fail "the joiner did not take red's Spartan"
		grep -aq "command net: machine 1: into the Spartan refused: Another commander is in your Spartan" "$hl" ||
			grep -aq "command: in: Another commander is in your Spartan" "$hl" || fail "the second commander's take over was not refused"
		;;
	commanddrop)
		host_cmds="L90:command money blue 2000;L100:command net;L1300:command net;L1500:command where;L1800:command net"
		join_cmds="L200:command buy barracks;L210:command buy armory;L900:command buy spartan;L1050:command possess;L1100:command net"
		command_host "$host_cmds" $(command_shots host)
		sleep 5
		command_joiner joiner "$lan" "$join_cmds" HALO_EXIT_AFTER=75 $(command_shots joiner)
		wait $join_pid $host_pid 2>/dev/null
		command_check_clean "$hl" "$jl"
		echo "--- host"; grep -aE "command net: |command: (player|in:|out|.* is commanded|blue's Spartan)" "$hl" | head -40
		grep -aq "command: in: player 1 in blue's Spartan" "$hl" || fail "the joiner did not take its Spartan over"
		grep -aqE "command: out \(the player (is leaving|quit|is gone)\): player 1 left blue's Spartan, which fights on as AI" "$hl" ||
			fail "the leaver's Spartan was not the AI's again"
		grep -aq "command: blue is commanded by the AI commander" "$hl" || fail "blue did not go to the AI commander"
		grep -aq "test command at tick 1800: command net" "$hl" || fail "the host did not play on"
		;;
	commandjoin)
		# (a network game starts with two players: the host and a first joiner, who leaves; a
		# second machine then joins the game in progress - blue the AI's meanwhile -, given the
		# whole state, and commands blue; its ticks are the host's, counted from the host's latest)
		local j2l=$out/joiner2/run.log i tick=0
		host_cmds="L90:command net;L300:command buy marines red;L1500:command net;L4000:command net;L4300:command where"
		command_host "$host_cmds" $(command_shots host)
		sleep 5
		command_joiner joiner "$lan" "L200:command net" HALO_EXIT_AFTER=${HALO_TEST_FIRST_JOINER_SECONDS:-70}
		wait $join_pid 2>/dev/null
		for i in $(seq 1 60); do
			grep -aq "command: blue is commanded by the AI commander" "$hl" 2>/dev/null && break
			sleep 1
		done
		sleep 5
		tick=$(grep -a "network test: tick [0-9]*" "$hl" | tail -1 | sed 's/.*network test: tick \([0-9]*\).*/\1/')
		tick=$((${tick:-0} + 900))
		echo "the second joiner starts at the host's tick $((tick - 900)), its commands from tick $tick"
		join_cmds="L$tick:command net;L$((tick + 50)):command buy marines;L$((tick + 500)):command select first"
		join_cmds="$join_cmds;L$((tick + 510)):command move 60 -95 1;L$((tick + 600)):command net"
		join_cmds="$join_cmds;L$((tick + 620)):command camera 60 -100 300 34;L$((tick + 660)):@shot join-in-progress"
		run_copy joiner2 "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_TEST_FORGE_PACKS=none \
			"HALO_TEST_COMMANDS=$join_cmds" $(command_shots joiner2)
		wait $last_pid $host_pid 2>/dev/null
		command_check_clean "$hl" "$jl" "$j2l"
		echo "--- host"; grep -aE "command net: |command: (player|.* is commanded|buy: blue)" "$hl" | head -40
		echo "--- second joiner"; grep -aE "command net: |command: the host's|forge net: the host" "$j2l" | head -20
		grep -aq "command: blue is commanded by the AI commander" "$hl" || fail "blue was not the AI's once the first joiner left"
		[ "$(grep -ac "command: blue is commanded by its human players" "$hl")" -ge 2 ] ||
			fail "blue was not the second joiner's once it came"
		grep -aq "forge net: the host's game is a Command game" "$j2l" || fail "the second joiner was not told the game is Command"
		grep -aq "command: the host's Command game on .*, commanding blue" "$j2l" || fail "the second joiner did not command blue"
		grep -aq "command net: the host's game had" "$j2l" || fail "the second joiner was not given the host's game"
		grep -aq "command net: machine [0-9]* given the game" "$hl" || fail "the host did not give the second joiner the whole game"
		grep -aqE "command net: machine [0-9]+ \(blue\) buys marines" "$hl" || fail "the second joiner's marines were not asked for"
		grep -aqE "command net: machine [0-9]+ \(blue\) ordered [1-9] squads" "$hl" || fail "the second joiner's order was not made"
		;;
	h3command)
		local menus_data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
		if [ ! -r "$menus_data/maps/bitmaps.map" ] || [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "SKIP: no Halo PC files in $menus_data/maps (HALO_TEST_DATA_MENUS) or no HALO_TEST_PROFILE"; return 0
		fi
		local name
		for name in host joiner; do
			mkdir -p "$out/$name/save/u/UDATA/869079979327"
			cp "$HALO_TEST_PROFILE" "$out/$name/save/u/UDATA/869079979327/blam.sav"
			[ -r "$(dirname "$HALO_TEST_PROFILE")/SaveMeta.xbx" ] &&
				cp "$(dirname "$HALO_TEST_PROFILE")/SaveMeta.xbx" "$out/$name/save/u/UDATA/869079979327/"
		done
		# (1.1.4, Halo CE alone - no hub: the host: COMMAND (down three, A); the lobby on NETWORK: A, the picker on LOCAL: up two to
		# SYSTEM LINK, A - the lobby live -; a while for the joiner; then down five to START COMMAND,
		# A: the game's lobby, its countdown at once)
		local host_pad="wait:150:3000 down:150:600 down:150:600 down:150:600 a:150:3000 a:150:2000"
		host_pad="$host_pad up:150:500 up:150:500 a:150:3000 @command-lobby:0:300 wait:150:${HALO_TEST_LIVE_WAIT:-45000}"
		host_pad="$host_pad @command-lobby-joined:0:300 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500"
		host_pad="$host_pad a:150:3000 @command-countdown:0:300"
		host_cmds="L300:command net;L400:command buy marines;L900:command net;L1000:command camera 70 -120 300 34;L1040:@shot command-host-game"
		join_cmds="L300:command net;L400:command buy grunts;L900:command net;L1000:command camera 70 -120 120 34;L1040:@shot command-joiner-game"
		HALO_TEST_DATA_HOST=$menus_data run_copy host "$host_machine" "$vita" "$cpu_a" HALO_NET_ONLINE=false \
			HALO_NETWORK_TEST=watch HALO_UI_LOG=1 HALO_MENU_STYLE=halo3 HALO_TEST_FORGE_PACKS=none $(command_shots host) \
			"HALO_TEST_PAD=$host_pad" "HALO_TEST_COMMANDS=$host_cmds"
		host_pid=$last_pid
		sleep 5
		HALO_TEST_DATA_JOINER=$menus_data run_copy joiner "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false \
			HALO_NETWORK_TEST=watch HALO_UI_LOG=1 HALO_MENU_STYLE=halo3 HALO_TEST_FORGE_PACKS=none $(command_shots joiner) \
			"HALO_TEST_PAD=wait:150:3000 down:150:700 a wait:150:3000 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a a a wait:150:15000 a wait:150:10000 @command-joiner-lobby:0:300" \
			"HALO_TEST_COMMANDS=$join_cmds"
		join_pid=$last_pid
		wait $join_pid $host_pid 2>/dev/null
		command_check_clean "$hl" "$jl"
		echo "--- host"; grep -aE "menus: (command lobby|forge lobby|h3 live lobby)|forge net: (machine|hosting)|command net: hosting" "$hl" | head -30
		echo "--- joiner"; grep -aE "menus: h3 pregame|forge net: (the host|holding)|command: the host's|command net: the host's game had" "$jl" | head -20
		grep -aq "menus: forge lobby: network SYSTEM LINK" "$hl" || fail "the Command lobby's NETWORK did not change to SYSTEM LINK"
		grep -aq "menus: command lobby: hosting Command on Blood Gulch" "$hl" || fail "the Command lobby did not host a Command game"
		grep -aq "forge net: hosting a Command game of" "$hl" || fail "no network Command game was hosted"
		grep -aq "system link: in another's lobby" "$jl" || fail "the joiner did not join the Command game from the browser"
		grep -aqE "menus: h3 live lobby: .*; players: [^;]+, [^;]+; 2/" "$hl" || fail "the Command lobby did not show the joiner among its players"
		grep -aq "forge net: machine 1 plays the Forge game" "$hl" || fail "the joiner did not answer the Command game's hello"
		grep -aq "menus: forge lobby: START FORGE: the game's lobby, everyone in it into" "$hl" || fail "START COMMAND did not take the lobby into the game"
		grep -aq "command net: hosting the Command game (red human, blue human)" "$hl" || fail "the host's Command game did not have both sides human"
		grep -aq "command: the host's Command game on .*, commanding blue" "$jl" || fail "the joiner did not command blue"
		grep -aq "command net: machine 1 (blue) buys grunts" "$hl" || fail "the joiner's buy did not reach the host"
		;;
	commandold)
		host_cmds="L90:command net"
		command_host "$host_cmds"
		sleep 5
		command_joiner joiner "$lan" "" HALO_FORGE_NET=0
		wait $join_pid $host_pid 2>/dev/null
		command_check_clean "$hl" "$jl"
		grep -aE "forge net: (machine|refus)" "$hl" | head
		grep -aq "forge net: machine 1 plays" "$hl" && fail "a machine that does not play network Command was let in"
		grep -aqE "forge net: machine 1 (is refused|refused|did not answer)" "$hl" || fail "the old joiner was not refused"
		;;
	esac
}
