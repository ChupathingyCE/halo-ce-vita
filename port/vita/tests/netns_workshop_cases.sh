# Steam Workshop multiplayer maps' cases of run_netns_online_test.sh (port/linux/game/workshop_maps.h,
# map_share.c's Workshop states; docs/steam_download_security.md section 8), sourced by it: its run_copy, fail,
# $out, $vita, $cpu_*, $host_machine and $lan (a machine on the host's LAN, System Link). The host hosts a game of
# Slayer on Workshop item 2900000002's map (its folder mods/2900000002 as the Workshop installs it: a real MCC
# multiplayer map of this PC's own MCC, HALO_TEST_WORKSHOP_MP_MAP, under the item's file name coldsnapextended.map,
# named ws2900000002_coldsna-<hash>: a long name shortened) and the joiner joins it by the System Link search. The
# build must be a Steam Workshop test build (configure.py --steam-workshop --steam-test-ca).
#   workshop      the joiner has the same copy: it plays the host's game; nothing asked of the host (no map share
#                 query), the map loaded from the joiner's mods folder
#   workshopget   the joiner lacks it: asked, it answers yes (HALO_MAP_SHARE_ANSWER, a test build's), its Steam
#                 Workshop download of item 2900000002 runs against steam_fake_server.py in its own namespace (the
#                 QR sign-in, the item, the session revoked and logged off), the item is installed through the
#                 game's own map check, its copy found the host's, precached, and the joiner plays (the item served
#                 without its bitmaps.map: the joiner's pixels from its own MCC, HALO_CONTENT_ROOT); never a map
#                 share query, the host sending nothing; the bytes installed are the map's; no token in a log
#   workshopno    the joiner lacks it and answers no: told, no Steam session begun, back at the menus
#   workshopdiff  the joiner's copy is another map under that name (another version of the item): refused, told
#                 its copy is not the host's; never asked to download, nothing asked of the host
#   workshopold   an older build joins (HALO_TEST_WORKSHOP_OLD: 1.1.2 by default, then the 1.1.3 candidate without
#                 the Workshop, HALO_TEST_WORKSHOP_OLD2): to it a custom map it lacks, which it asks the host for;
#                 the host refuses (never shared); the old joiner is refused cleanly, stays up, plays nothing
# HALO_TEST_WORKSHOP_OTHER_MAP: the other map of workshopdiff (MCC's chillout.map).

ws_id=2900000002
ws_file=coldsnapextended.map
ws_mcc=${HALO_TEST_WORKSHOP_MP_MAP:-$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1/maps/bloodgulch.map}
ws_other=${HALO_TEST_WORKSHOP_OTHER_MAP:-$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1/maps/chillout.map}
# (workshop_maps.c's name: ws<id>_ and the stem, too long here: its first 7 characters, '-', FNV-1a's low 16 bits)
ws_name=$(python3 -I -c '
import sys
stem = sys.argv[1]
h = 2166136261
for c in stem.encode():
    h = ((h ^ c) * 16777619) & 0xffffffff
head = "ws%s_" % sys.argv[2]
room = 25 - len(head)
print(head + (stem if len(stem) <= room else "%s-%04x" % (stem[:room - 5], h & 0xffff)))' "${ws_file%.map}" "$ws_id")

ws_mcc_folder=$(cd "$(dirname "$ws_mcc")/../.." 2>/dev/null && pwd)
ws_install() { # ws_install NAME MAP: MAP as the item's file in NAME's mods folder (mods/<id>/maps), with the item's
	# own halo1/maps/bitmaps.map as real Workshop multiplayer items carry one (a link to MCC's: read only)
	mkdir -p "$out/$1/data/mods/$ws_id/maps" "$out/$1/data/mods/$ws_id/halo1/maps"
	cp "$2" "$out/$1/data/mods/$ws_id/maps/$ws_file"
	ln -sfn "$(dirname "$ws_mcc")/bitmaps.map" "$out/$1/data/mods/$ws_id/halo1/maps/bitmaps.map"
}

ws_host() { # ws_host [ENV...]
	run_copy host "$host_machine" "$vita" "$cpu_a" HALO_NET_ONLINE=false HALO_NETWORK_TEST="host:$ws_name:slayer" \
		HALO_NETWORK_TEST_START=${HALO_TEST_WS_START:-45} HALO_NETWORK_TEST_SCORE=25 HALO_TEST_INPUT=bot:1 "$@"
	host_pid=$last_pid
}

ws_check_clean() { # no assertion, exception or crash in the copies' logs
	local log
	for log in "$@"; do
		[ -f "$log" ] || continue
		grep -av "assertion skipped (torn read?)" "$log" | grep -aqiE "assert|exception|halt_and_catch_fire|segmentation" &&
			fail "$(basename "$(dirname "$log")"): an assertion, exception or crash"
	done
}

ws_played() { # ws_played LOG: the joiner's seconds with two players playing
	grep -a "network test: tick" "$1" | grep -a "| playing" | grep -aEc "player [0-9]+:.* player [0-9]+:"
}

ws_no_map_share() { # nothing of the map asked of the host, nothing sent
	grep -aE "map share: (asking the host|machine .* asks|serving|sending|offering)" "$hl" "$jl" | head -3
	grep -aqE "map share: asking the host about" "$jl" && fail "the joiner asked the host for the Workshop map"
	grep -aqiE "map share: .*(offer|upload|sending)" "$hl" && fail "the host offered or sent a map"
}

workshop_case() {
	local hl=$out/host/run.log jl=$out/joiner/run.log two fake_dir fake_pid fake_cm fake_cdn fake_token old
	[ -f "$ws_mcc" ] || { echo "SKIP ($mode): no MCC multiplayer map (HALO_TEST_WORKSHOP_MP_MAP)"; exit 0; }
	grep -aq "port h3 workshop" "$vita" && grep -aq "HALO_STEAM_TEST_CM" "$vita" ||
		{ echo "SKIP ($mode): not a Steam Workshop test build (configure.py --steam-workshop --steam-test-ca)"; exit 0; }
	echo "the Workshop map: item $ws_id's $ws_file, named $ws_name"
	ws_install host "$ws_mcc"
	case $mode in
	workshop)
		ws_install joiner "$ws_mcc"
		ws_host
		sleep 5
		run_copy joiner "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_TEST_INPUT=bot:1
		wait $last_pid $host_pid 2>/dev/null
		ws_check_clean "$hl" "$jl"
		grep -aE "join: the host's map|workshop|Workshop" "$jl" | head -6
		grep -aq "join: the host's map is levels.test.$ws_name.$ws_name (copy 0x" "$jl" || fail "the joiner was not told the Workshop map"
		grep -aq "custom edition: loading 'd:.mods.$ws_id.maps.$ws_file'" "$out/joiner/data/debug.txt" ||
			fail "the joiner did not load the map from its mods folder"
		grep -aq "custom edition: loading 'd:.mods.$ws_id.maps.$ws_file'" "$out/host/data/debug.txt" ||
			fail "the host did not load the map from its mods folder"
		ws_no_map_share
		two=$(ws_played "$jl")
		echo "joiner's seconds with two players playing: $two"
		[ "$two" -ge 20 ] || fail "the joiner played the host's Workshop game for $two s with two players (20 wanted)"
		;;
	workshopget)
		fake_dir=$out/fake
		mkdir -p "$fake_dir"
		rm -f "$fake_dir/stdin"; mkfifo "$fake_dir/stdin"
		# (the fake Steam in the joiner's own namespace, on its 127.0.0.1: nothing of it reaches the host)
		nsenter -t "$lan" -n python3 -I "$here/steam_fake_server.py" --work "$fake_dir" --chunk-kb 1024 \
			--workshop-mp-map "$ws_mcc" --workshop-mp-name "$ws_file" < "$fake_dir/stdin" > "$fake_dir/server.json" &
		fake_pid=$!
		exec 7> "$fake_dir/stdin"
		for i in $(seq 300); do [ -s "$fake_dir/server.json" ] && break; sleep 0.1; done
		fake_cm=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["cm"])' "$fake_dir/server.json")
		fake_cdn=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["cdn"])' "$fake_dir/server.json")
		fake_token=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["token"])' "$fake_dir/server.json")
		ws_host
		sleep 5
		run_copy joiner "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_TEST_INPUT=bot:1 \
			HALO_MAP_SHARE_ANSWER=yes HALO_STEAM_TEST_CM="ws://127.0.0.1:$fake_cm/cmsocket/" \
			HALO_STEAM_TEST_CDN="http://127.0.0.1:$fake_cdn" HALO_CONTENT_ROOT="$ws_mcc_folder"
		wait $last_pid $host_pid 2>/dev/null
		exec 7>&-
		wait "$fake_pid" 2>/dev/null || true
		ws_check_clean "$hl" "$jl"
		grep -aE "Steam Workshop|workshop|steam job: (started|sign-in|workshop)" "$jl" | head -14
		grep -aq "map share: asking the player about the Steam Workshop map '$ws_name' (item $ws_id" "$jl" ||
			fail "the joiner was not asked about the Workshop map"
		grep -aq "menus: workshop: item $ws_id's download begun for the host's map" "$jl" ||
			fail "the Workshop download of the item did not begin (its screen over the lobby)"
		grep -aq "ui: screen pc.h3.workshop.screen" "$jl" || fail "the STEAM WORKSHOP screen did not open over the lobby"
		grep -aq "steam job: workshop: 1 items chosen" "$jl" || fail "the job did not take the one item"
		grep -aq "steam job: sign-in 1 ended: done (revoked 1, logged off 1)" "$jl" ||
			fail "the session did not end done, revoked and logged off"
		grep -aq "menus: workshop: $ws_id installed in mods" "$jl" || fail "the item was not installed"
		grep -aq "map share: the Steam Workshop map '$ws_name' is installed and the host's" "$jl" ||
			fail "the installed copy was not found the host's"
		cmp -s "$ws_mcc" "$out/joiner/data/mods/$ws_id/maps/$ws_file" || fail "the map installed is not the item's bytes"
		[ -z "$(ls -A "$out/joiner/data/workshop" 2>/dev/null)" ] || fail "the staging folder is not empty"
		grep -aq "cdn: workshop manifest $ws_id" "$fake_dir/transcript.txt" || fail "the fake Steam's CDN was not asked for the item"
		grep -aqF "$fake_token" "$jl" "$hl" && fail "the token is in a log"
		grep -raqF "$fake_token" "$out/joiner/data" "$out/joiner/save" && fail "the token is in a file written"
		ws_no_map_share
		two=$(ws_played "$jl")
		echo "joiner's seconds with two players playing: $two"
		[ "$two" -ge 15 ] || fail "the joiner played the host's Workshop game for $two s with two players (15 wanted)"
		;;
	workshopno)
		ws_host
		sleep 5
		run_copy joiner "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_TEST_INPUT=bot:1 \
			HALO_MAP_SHARE_ANSWER=no HALO_STEAM_TEST_CM="ws://127.0.0.1:9/cmsocket/" HALO_STEAM_TEST_CDN="http://127.0.0.1:9"
		wait $last_pid $host_pid 2>/dev/null
		ws_check_clean "$hl" "$jl"
		grep -aE "Steam Workshop|steam job" "$jl" | head -6
		grep -aq "map share: asking the player about the Steam Workshop map '$ws_name'" "$jl" ||
			fail "the joiner was not asked about the Workshop map"
		grep -aq "You answered no to downloading it" "$jl" || fail "the joiner was not told it answered no"
		grep -aq "steam job: started" "$jl" && fail "a Steam session began though the player said no"
		[ -e "$out/joiner/data/mods/$ws_id" ] && fail "something was installed"
		ws_no_map_share
		two=$(ws_played "$jl")
		[ "${two:-0}" = 0 ] || fail "the joiner played ($two s) though it has no map"
		;;
	workshopdiff)
		ws_install joiner "$ws_other"
		ws_host
		sleep 5
		run_copy joiner "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_TEST_INPUT=bot:1 \
			HALO_MAP_SHARE_ANSWER=yes HALO_STEAM_TEST_CM="ws://127.0.0.1:9/cmsocket/" HALO_STEAM_TEST_CDN="http://127.0.0.1:9"
		wait $last_pid $host_pid 2>/dev/null
		ws_check_clean "$hl" "$jl"
		grep -aE "Steam Workshop|join: the host's map" "$jl" | head -6
		grep -aq "The host's Steam Workshop map $ws_name isn't the same version as yours" "$jl" ||
			fail "the joiner was not told its copy is not the host's"
		grep -aq "asking the player about the Steam Workshop map" "$jl" && fail "the joiner was asked to download"
		grep -aq "steam job: started" "$jl" && fail "a Steam session began"
		cmp -s "$ws_other" "$out/joiner/data/mods/$ws_id/maps/$ws_file" || fail "the joiner's own copy was changed"
		ws_no_map_share
		two=$(ws_played "$jl")
		[ "${two:-0}" = 0 ] || fail "the joiner played ($two s) another copy of the map"
		;;
	workshopold)
		for old in "${HALO_TEST_WORKSHOP_OLD:-}" "${HALO_TEST_WORKSHOP_OLD2:-}"; do
			[ -n "$old" ] && [ -x "$old" ] || continue
			echo "--- an older build joins: $old"
			rm -rf "$out/host/run.log" "$out/joiner"
			ws_host
			sleep 5
			run_copy joiner "$lan" "$old" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_TEST_INPUT=bot:1 \
				HALO_MAP_SHARE_ANSWER=yes
			wait $last_pid $host_pid 2>/dev/null
			ws_check_clean "$hl" "$jl"
			grep -aE "join: the host's map|map share|Halo: (custom )?map" "$jl" | head -6
			grep -aE "map share" "$hl" | head -4
			grep -aq "join: the host's map is levels.test.$ws_name" "$jl" || fail "the older joiner never saw the host's map"
			grep -aqE "map share: (receiving|downloaded|installed)" "$jl" && fail "the older joiner received a map"
			grep -aqiE "map share: .*(offer|upload|sending)" "$hl" && fail "the host offered or sent a map to the older joiner"
			[ -e "$out/joiner/data/maps/$ws_name.map" ] && fail "a map file of the Workshop name appeared in the old joiner's maps"
			two=$(ws_played "$jl")
			[ "${two:-0}" = 0 ] || fail "the older joiner played ($two s) a map it does not have"
			grep -aq "network test: tick" "$jl" || fail "the older joiner did not stay up"
			cp "$hl" "$out/host-$(basename "$old").log"; cp "$jl" "$out/joiner-$(basename "$old").log"
		done
		;;
	esac
}
