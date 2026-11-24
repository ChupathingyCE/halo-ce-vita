# netns_dedicated_compat.sh: run_netns_online_test.sh's dedicatedcompat mode
# (sourced by it, inside its dedicated servers' case: run_copy, side, console,
# $code, $sl, $sd and fail are its). README_dedicated_compat.md says how to run it.
#
# (Any game type: HALO_TEST_COMPAT_CYCLE's first game's decides the checks,
# Infection's or another's - a team game's teams: README_dedicated_compat.md.)
#
# The question it answers: can the PUBLIC 1.1.0 build (and the insider.21
# build beside it) join and play the dedicated server's Infection with the
# bots filling it (the s8 server)? The server (HALO_TEST_SERVER, this tree's
# halo-server) runs Infection (HALO_TEST_COMPAT_CYCLE, "prisoner infection"),
# sv_maxplayers HALO_TEST_BOTS_MAX (16), sv_bots_fill 1; one 1.1.0 client
# (HALO_TEST_COMPAT_V110, player Old110) joins by the code, one insider.21
# client (HALO_TEST_COMPAT_NEW, player Ins21) 10 s later, both scripted
# (bot input, shooting now and then) until a round or more is over. Then
# netns_compat_check.py reads every log and prints one line a check.

# (1.1.3, the official servers' update: HALO_TEST_COMPAT_INIT, an official server's own init.txt - its name,
# most players, cycle, starting weapons, bots - its sv_port made 2302, its time limit
# HALO_TEST_COMPAT_MINUTES; HALO_TEST_COMPAT_CONFIG, its config.toml beside it; HALO_TEST_COMPAT_V110_FILES /
# HALO_TEST_COMPAT_NEW_FILES=mcc: that client an MCC-only machine (netns_mcc_cases.sh's mcc_setup: no Xbox
# maps, MCC's halo1 as its Halo content folder); HALO_TEST_COMPAT_NEW_EXPECT=refused: the second client must be
# refused before it loads the server's map (an older server: its version plays no mixed games, 1.1.3-insider.8l)
# and is left out of the checks. The Linode's own server build needs its libSDL3.so.0:
# HALO_TEST_SERVER_ENV="LD_LIBRARY_PATH=<its folder>")
compat_v110=${HALO_TEST_COMPAT_V110:-$data_root/wt-v110client/build/linux/halo}
compat_new=${HALO_TEST_COMPAT_NEW:-$data_root/wt-i21client/build/linux/halo}
compat_most=${HALO_TEST_BOTS_MAX:-16}

# dedicated_compat_init FILE: the server's init.txt
dedicated_compat_init() {
	local cycle=${HALO_TEST_COMPAT_CYCLE:-prisoner infection} entry
	[ -n "${HALO_TEST_COMPAT_CONFIG:-}" ] && cp "$HALO_TEST_COMPAT_CONFIG" "$(dirname "$1")/config.toml"
	if [ -n "${HALO_TEST_COMPAT_INIT:-}" ]; then
		# (an official server's init.txt as it is; HALO_TEST_COMPAT_CYCLE in place of its cycle when given)
		{
			grep -vE "^\s*(sv_port|sv_timelimit)\b" "$HALO_TEST_COMPAT_INIT" |
				{ if [ -n "${HALO_TEST_COMPAT_CYCLE:-}" ]; then grep -v "^\s*sv_mapcycle_add"; else cat; fi; }
			[ -n "${HALO_TEST_COMPAT_CYCLE:-}" ] && echo "$cycle" | tr ';' '\n' | while read -r entry; do
				[ -n "$entry" ] && echo "sv_mapcycle_add $entry"
			done
			echo "sv_timelimit ${HALO_TEST_COMPAT_MINUTES:-3}"
			echo "sv_start_delay 5"
			echo "sv_postgame 5"
			echo "sv_end_empty 20"
			echo "sv_port 2302"
		} > "$1"
		compat_most=$(sed -n 's/^\s*sv_maxplayers \([0-9]*\).*/\1/p' "$1" | tail -1)
		echo "dedicatedcompat: the server's init.txt (from $HALO_TEST_COMPAT_INIT):"; sed 's/^/  /' "$1"
		return
	fi
	{
		echo "# the netns test's s8 stand-in (run_netns_online_test.sh dedicatedcompat)"
		echo "sv_name \"Netns Compat\""
		echo "sv_maxplayers $compat_most"
		echo "sv_public 1"
		echo "sv_bots_fill 1"
		# (HALO_TEST_COMPAT_CYCLE: games separated by ';', "map gametype" each)
		echo "$cycle" | tr ';' '\n' | while read -r entry; do
			[ -n "$entry" ] && echo "sv_mapcycle_add $entry"
		done
		# (HALO_TEST_COMPAT_EXTRA: more commands, separated by ';': an official
		# server's own, "sv_starting_weapons generic;sv_bots_skill easy")
		echo "${HALO_TEST_COMPAT_EXTRA:-}" | tr ';' '\n' | while read -r entry; do
			[ -n "$entry" ] && echo "$entry"
		done
		echo "sv_timelimit ${HALO_TEST_COMPAT_MINUTES:-3}"
		echo "sv_start_delay 5"
		echo "sv_postgame 5"
		echo "sv_end_empty 20"
		echo "sv_port 2302"
	} > "$1"
}

dedicated_compat_run() {
	local v110_pid new_pid status_pid v110_rc new_rc server_rc
	for binary in "$compat_v110" "$compat_new"; do
		[ -x "$binary" ] || { echo "dedicatedcompat: no client build at $binary (HALO_TEST_COMPAT_V110, HALO_TEST_COMPAT_NEW)"; exit 2; }
	done
	echo "dedicatedcompat: 1.1.0 client $compat_v110"
	echo "dedicatedcompat: insider client $compat_new"
	echo "dedicatedcompat: server $server"
	# (an MCC-only client: its data folder and Halo content folder)
	local v110_env= new_env=
	if [ "${HALO_TEST_COMPAT_V110_FILES:-xbox}" = mcc ] || [ "${HALO_TEST_COMPAT_NEW_FILES:-xbox}" = mcc ]; then
		. "$here/netns_mcc_cases.sh"
		mcc_setup
	fi
	[ "${HALO_TEST_COMPAT_V110_FILES:-xbox}" = mcc ] && { copy_data_v110=$out/mcc-none; v110_env="HALO_CONTENT_ROOT=$out/mcc-content"; }
	[ "${HALO_TEST_COMPAT_NEW_FILES:-xbox}" = mcc ] && { copy_data_insider=$out/mcc-none; new_env="HALO_CONTENT_ROOT=$out/mcc-content"; }
	echo "dedicatedcompat: the first client's files ${HALO_TEST_COMPAT_V110_FILES:-xbox}, the second's ${HALO_TEST_COMPAT_NEW_FILES:-xbox}${HALO_TEST_COMPAT_NEW_EXPECT:+, the second expected $HALO_TEST_COMPAT_NEW_EXPECT}"
	# (the server's sv_status every 5 s, its sv_items every 20 s)
	(
		for i in $(seq 1 $(((seconds - 20) / 5))); do
			sleep 5
			echo sv_status >&"$console_fd" 2>/dev/null || exit 0
			[ $((i % 4)) = 0 ] && { echo sv_items >&"$console_fd" 2>/dev/null || exit 0; }
		done
	) & status_pid=$!; pids="$pids $status_pid"
	run_copy v110 "$join_machine" "$compat_v110" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-code:$code \
		HALO_NET_PLAYER_NAME=Old110 HALO_NET_LOBBY_NAME=Old110 HALO_EXIT_AFTER=$((seconds - 10)) HALO_TEST_INPUT=bot:2 \
		HALO_NETWORK_TEST_SHOOT=2 $v110_env
	v110_pid=$last_pid
	sleep 10
	side d2 10.10.22 192.168.22
	run_copy insider "$d2_machine" "$compat_new" "$cpu_c" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-code:$code \
		HALO_NET_PLAYER_NAME=Ins21 HALO_NET_LOBBY_NAME=Ins21 HALO_EXIT_AFTER=$((seconds - 20)) HALO_TEST_INPUT=bot:3 \
		HALO_NETWORK_TEST_SHOOT=2 $new_env
	new_pid=$last_pid
	sleep 60
	console sv_players
	console sv_status
	wait "$v110_pid" 2>/dev/null; v110_rc=$?
	wait "$new_pid" 2>/dev/null; new_rc=$?
	kill "$status_pid" 2>/dev/null; wait "$status_pid" 2>/dev/null
	kill -TERM "$server_pid" 2>/dev/null; wait "$server_pid" 2>/dev/null; server_rc=$?
	echo "--- server (joins, bots, game rules, the round)"
	grep -aE "server: (the next game|the game|player #[0-9]+ \S+ (joined|left))|game rules net|infection: (the round|.* is a zombie|.* infected )" "$sl" |
		grep -av "trace:" | head -50
	echo "--- the 1.1.0 client's own lines"
	grep -aE "network test: (joining|the public|player [0-9]+ is named)|system link: |XLaunchNewImage|lost the connection" \
		"$out/v110/run.log" | head -24
	echo "--- the insider client's infection lines"
	grep -aE "infection:|game rules net:" "$out/insider/run.log" | grep -av "trace:" | head -24
	echo "--- checks"
	# (the cycle's first game's type: Infection's checks, or another's - a
	# team game's teams)
	local first_type teams=
	first_type=$(echo "${HALO_TEST_COMPAT_CYCLE:-prisoner infection}" | cut -d';' -f1 | awk '{print $2}')
	[ -n "${HALO_TEST_COMPAT_INIT:-}" ] && [ -z "${HALO_TEST_COMPAT_CYCLE:-}" ] &&
		first_type=$(sed -n 's/^\s*sv_mapcycle_add \S* \(\S*\).*/\1/p' "$out/server/data/init.txt" | head -1)
	case $first_type in team_*|ctf|btb) teams=--teams ;; esac
	local new_client="--client insider:Ins21:$out/insider:$new_rc"
	if [ "${HALO_TEST_COMPAT_NEW_EXPECT:-plays}" = refused ]; then
		# (refused before the server's map loads: told why, never in the game, no map loaded)
		local il=$out/insider/run.log
		grep -a "mixed play: \|Halo: map: \|mixed co-op: refused" "$il" | head -6
		grep -aqE "mixed play: the host never said it plays mixed \(an older version\): \S+ refused, (in its lobby|as its game starts), before the map loads" "$il" ||
			fail "the second client was not refused before the server's map loads"
		grep -aq "MCC level in network co-op loaded\|structure bsp switch" "$il" &&
			fail "the second client loaded the server's map before it was refused"
		grep -a "Halo: map: \|Halo: co-op: " "$il" | grep -aqi "co-op" && fail "the second client's refusal says co-op"
		grep -a "network test: tick" "$il" | grep -aq "| playing" && fail "the second client played the server's game"
		grep -aq "XLaunchNewImage\|EXCEPTION\|damaged disc" "$il" && fail "the second client crashed or showed the damaged disc"
		new_client=
	fi
	python3 "$here/netns_compat_check.py" --server-log "$sl" --server-debug "$sd" --server-rc "$server_rc" \
		--most "$compat_most" --gametype "$first_type" $teams \
		--client "v110:Old110:$out/v110:$v110_rc" $new_client \
		$([ "${HALO_TEST_COMPAT_LENIENT:-0}" = 1 ] && echo --lenient) ||
		fail "the compatibility checks failed (the [FAIL] lines above)"
}
