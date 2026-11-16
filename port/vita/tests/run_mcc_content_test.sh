#!/bin/bash
# The Halo content folder (paths.content, HALO_CONTENT_ROOT: the player's own
# Halo: The Master Chief Collection folder, halo1 in it; no switch:
# port/linux/game/mcc_content.c, docs/mcc_cea_maps.md).
#
# 1. Without any MCC file, always: mcc_content_test.c (MCC's maps' names,
#    Steam's libraryfolders.vdf, its truncations, the loaders' table by a
#    map file's header), under AddressSanitizer
#    and undefined-behaviour trapping when the compiler has them.
# 2. With the player's own MCC halo1 folder (HALO_TEST_MCC_ROOT, default
#    Steam's; SKIP without one) and the Xbox maps (HALO_TEST_DATA), on the
#    Linux harness: MCC's maps linked read only into a content folder of the
#    test's (<out>/mcc/halo1/maps, never copied), which HALO_CONTENT_ROOT
#    names:
#      campaign  each of HALO_TEST_MCC_LEVELS (b30 d40) started as the campaign
#                list starts it (mods\mcc_<level>), its cinematics skipped,
#                played HALO_TEST_MCC_SECONDS (60): it must load from MCC's
#                folder, say where its sounds are (MCC's FMOD banks), tick on, and log no
#                assertion or exception
#      won       mcc_b30 won goes on to mcc_b40, MCC's next level
#      mp        mcc_bloodgulch, an offline game with HALO_TEST_MCC_BOTS (6)
#                bots (the network test's local: game, in a namespace with no
#                network), HALO_TEST_MCC_MP_SECONDS (60): the bots must score
#      xbox      no content folder (HALO_CONTENT_ROOT=none): the Xbox maps
#                alone, mods\mcc_b30 does not load, no MCC map is listed
#      shadow    a file of the maps folder named mcc_bloodgulch.map (an Xbox
#                map) is never listed nor played for MCC's name
#   HALO_TEST_SHOTS: a folder for the runs' frames (every 60 frames; the GL
#   build's).
# 3. menu: MCC's own menu files, with the MCC folder alone or beside the Xbox
#    maps (the Xbox install "hidden": a data folder with no maps in it):
#      pcfiles    the Xbox's ui.map and the content folder, no bitmaps.map,
#                 loc.map or sounds.map in the maps folder: the Halo PC files
#                 the Halo 3 menu reads are the content folder's
#                 halo1/maps/custom_edition/ ones (not halo1/maps/bitmaps.map,
#                 another file of the same name): the Halo 3 main menu comes up
#      priority   both ui.maps there: the Xbox's is the menus' map; with
#                 HALO_MENU_SOURCE=mcc (paths.menu_source) MCC's, and the
#                 campaign still starts the Xbox's a10; with "xbox" and no
#                 Xbox ui.map the menus' map is missing, not MCC's
#      mccmenu    no Xbox file at all, Menu style Classic: MCC's ui.map is the
#                 menus' map (menu_tags_loaded after it), Halo PC's own rows:
#                 CAMPAIGN, CREDITS and QUIT reached, MULTIPLAYER and PROFILES
#                 greyed and passed over; SETTINGS (Halo PC's function 137)
#                 edits a profile (made, none yet), its MOUSE, AUDIO and VIDEO
#                 SETUP open, their OK sets this build's settings, which
#                 stay after a restart (mccmenu2); its rows with no equivalent
#                 greyed (halo_pc_menus.c), no assertion
#      mcccplus   no Xbox file, Menu style Classic+ (after campaign, with its
#                 profile): Classic+'s rows on MCC's main menu (CAMPAIGN
#                 MULTIPLAYER SETTINGS MEDIA FORGE) and every screen behind
#                 them the Xbox's as on the Xbox's files (classic/
#                 xbox_screens.xml), no Halo 3 screen: SELECT PROFILE, LOAD
#                 LEVEL, CHOOSE DIFFICULTY; split screen's START TO JOIN,
#                 SELECT MAP (another map), SELECT GAMETYPE, lobby; SELECT
#                 PROFILE TO EDIT, EDIT PROFILE SETTINGS and its pages; MEDIA;
#                 FORGE's START FORGE playing MCC's Blood Gulch
#      b30        mods\mcc_b30 started by the console from that menu, played
#      campaign   CAMPAIGN, a new profile, the difficulty, with no Xbox map:
#                 MCC's a10 starts (mods\mcc_a10)
#      mccgames   (1.1.3) no Xbox file, Classic+: MEDIA > GAME MANAGER's GAMES
#                 tab says Halo CE installed from MCC's files alone (its maps
#                 counted, its PC files the custom_edition ones), nothing wrong
#      bothgames  (1.1.3) the Xbox's maps and MCC's, Halo 3: START > MEDIA >
#                 GAME MANAGER says both, each one's maps counted
#
# 4. auto: the PC finding the player's MCC by itself (paths.content "auto"),
#    HOME a folder of the test's with a made-up Steam: its libraryfolders.vdf
#    names a second library whose steamapps/common/Halo The Master Chief
#    Collection is a link to the real one (read only, never copied):
#      autobg     no Xbox file, no data folder set (a copy of the harness in a
#                 folder of its own, as a player's unpacked PC build): MCC
#                 found in the second library, no disc image asked for,
#                 mcc_bloodgulch offline with bots HALO_TEST_MCC_MP_SECONDS
#                 (60): the bots must score, no assertion
#      autogone   paths.content a folder that is gone, the Xbox maps there:
#                 said, and MCC found by itself
#      autonone   no Steam in HOME: "Halo MCC not found" said (the main
#                 menu's notice), the Xbox maps alone
#
# 5. inmaps (1.1.3): Newk's layout, MCC's maps copied by hand into the Xbox
#    maps folder (ux0:data/haloce-vita/maps), no Xbox file at all:
#      inmaps     the game does not play them from there; halo.log says the
#                 maps folder holds Halo MCC's maps and where they belong
#                 (mcc/halo1/maps), not the old "ui.map is missing ... or
#                 isn't an Xbox map"
#      beside     the Vita's layout (HALO_CONTENT_BESIDE_DATA=1: the
#                 handhelds' always): <data root>/../mcc found by itself, and
#                 <data root>/mcc too; one with MCC's maps wins over an empty
#                 folder of the other
#
#   run_mcc_content_test.sh [unit|harness|menu|auto|inmaps]   (default all)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
. "$here/test_data_root.sh"
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$data_root/data2276}
mcc=${HALO_TEST_MCC_ROOT:-$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1}
levels=${HALO_TEST_MCC_LEVELS:-b30 d40}
seconds=${HALO_TEST_MCC_SECONDS:-60}
mp_seconds=${HALO_TEST_MCC_MP_SECONDS:-60}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_mcc_content_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
status=0
fail() { echo "FAIL: $1"; status=1; }
parts=${*:-unit harness menu auto inmaps}

case " $parts " in *" unit "*)
	cc=${CC:-gcc}
	sanitize="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
	echo 'int main(void){return 0;}' | $cc $sanitize -x c - -o "$out/probe" 2>/dev/null || sanitize=
	$cc -g -O1 -Wall -Wextra -Werror $sanitize -I"$root/port/linux/game" -I"$root/port/linux/src" \
		"$here/mcc_content_test.c" "$root/port/linux/game/mcc_content.c" "$root/port/linux/src/mcc_paths.c" \
		-o "$out/mcc_content_test" || fail "mcc_content_test did not build"
	[ -x "$out/mcc_content_test" ] && { "$out/mcc_content_test" || fail "mcc_content_test"; }
	;;
esac

case " $parts " in *" harness "*)
	if [ ! -x "$binary" ]; then
		echo "SKIP (harness): no harness at $binary (HALO_TEST_VITA)"
	elif [ ! -e "$data/maps/ui.map" ]; then
		echo "SKIP (harness): no Xbox maps in $data/maps (HALO_TEST_DATA)"
	elif [ ! -r "$mcc/maps/bitmaps.map" ]; then
		echo "SKIP (harness): no MCC halo1 folder at $mcc (HALO_TEST_MCC_ROOT)"
	else
		# (MCC's maps, linked read only, never copied)
		mkdir -p "$out/mcc/halo1/maps"
		for file in "$mcc"/maps/*.map; do
			ln -sfn "$file" "$out/mcc/halo1/maps/"
		done
		# run NAME SECONDS [VAR=value...]: the harness, the data folder's
		# maps the Xbox's, init.txt and the commands the case's
		run() {
			local name=$1 run_seconds=$2 dir="$out/$1"
			shift 2
			rm -rf "$dir"
			mkdir -p "$dir/data/maps" "$dir/save"
			for file in "$(cd "$data" && pwd)"/maps/*.map; do
				ln -sfn "$file" "$dir/data/maps/"
			done
			[ -n "${init_line:-}" ] && printf 'game_difficulty_set hard\n%s\n' "$init_line" > "$dir/data/init.txt"
			[ -n "${extra_map:-}" ] && ln -sfn "$extra_map" "$dir/data/maps/$extra_map_name"
			local shots=()
			if [ -n "${HALO_TEST_SHOTS:-}" ]; then
				mkdir -p "$HALO_TEST_SHOTS/$name"
				shots=("HALO_SCREENSHOT_DIR=$HALO_TEST_SHOTS/$name" HALO_SCREENSHOT_EVERY=60)
			fi
			(cd "$dir" && exec unshare -rn sh -c 'ip link set lo up 2> /dev/null; exec "$@"' sh \
				env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
				HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$run_seconds" \
				HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
				HALO_CONTENT_ROOT="${content_root:-$out/mcc}" HALO_FRAME_TIMING=300 "${shots[@]}" "$@" \
				timeout -k 5 $((run_seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
			local code=$?
			[ "$code" = 0 ] || fail "$name: the harness exited with $code ($dir/run.log)"
			if cat "$dir/run.log" "$dir/data/debug.txt" 2>/dev/null | grep -av "render assertion skipped" |
				grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
				fail "$name: an assertion or exception ($dir)"
			fi
		}
		ticks_of() { grep -a "frame-timing: game tick" "$1" | tail -1 | sed -E 's/.*game tick ([0-9]+).*/\1/'; }
		campaign_commands='30:(set cheat_deathless_player true);60:@skip;300:@skip;600:@skip;900:@skip;1200:@skip'

		for level in $levels; do
			init_line="map_name mods\\mcc_$level" run "mcc_$level" "$seconds" \
				HALO_TICK_THREAD=0 "HALO_TEST_COMMANDS=$campaign_commands"
			log="$out/mcc_$level/run.log"
			debug="$out/mcc_$level/data/debug.txt"
			ticks=$(ticks_of "$log")
			if ! grep -aq "memory: loaded mods\\\\mcc_$level" "$log" ||
				! grep -aq "custom edition: loading 'd:\\\\mcc\\\\halo1\\\\maps\\\\$level.map'" "$debug"; then
				fail "mcc_$level was not loaded from MCC's folder ($out/mcc_$level)"
			elif ! grep -aq "mcc: '$level' is MCC's own campaign map: its sounds are in MCC's FMOD banks" "$debug"; then
				fail "mcc_$level: where its sounds are was not said"
			elif [ "${ticks:-0}" -lt $((seconds * 20)) ]; then
				fail "mcc_$level reached tick ${ticks:-0} in $seconds s"
			else
				echo "mcc_$level: loaded from MCC's folder, tick $ticks in $seconds s;" \
					"$(grep -a 'objects: ' "$log" | tail -1 | sed 's/.*objects: \([0-9]*\) in the game.*/\1 objects/')"
			fi
		done

		# won: on to MCC's next level
		init_line='map_name mods\mcc_b30' run won 40 HALO_TICK_THREAD=0 'HALO_TEST_COMMANDS=M300:(game_won);'
		if grep -aq "memory: loaded mods\\\\mcc_b40" "$out/won/run.log"; then
			echo "won mcc_b30: on to mcc_b40"
		else
			fail "mcc_b30 won did not go on to mcc_b40 ($out/won)"
		fi

		# mp: offline with bots on MCC's Blood Gulch
		init_line= run mp "$mp_seconds" HALO_TICK_THREAD=1 \
			HALO_NETWORK_TEST="local:mcc_bloodgulch:slayer" HALO_NETWORK_TEST_START=5 HALO_NETWORK_TEST_SCORE=500 \
			HALO_TEST_INPUT=bot:1 HALO_BOTS="${HALO_TEST_MCC_BOTS:-6}"
		log="$out/mp/run.log"
		# (the kills of every player, the network test's last tick line's kN)
		kills=$(grep -a "network test: tick" "$log" | tail -1 | grep -aoE " k[0-9]+" | tr -d ' k' | awk '{s+=$1} END {print s+0}')
		if ! grep -aq "custom edition: loading 'd:\\\\mcc\\\\halo1\\\\maps\\\\bloodgulch.map'" "$out/mp/data/debug.txt"; then
			fail "mp: MCC's bloodgulch was not loaded ($out/mp)"
		elif ! grep -aq "network test: starting the game" "$log"; then
			fail "mp: the game did not start ($out/mp)"
		elif [ "${kills:-0}" -lt 1 ]; then
			fail "mp: no kill in $mp_seconds s ($out/mp)"
		else
			echo "mp: mcc_bloodgulch, $(grep -ac 'network test: player [0-9]* is named' "$log") players, kills ${kills:-0};" \
				"$(grep -a 'mcc: [0-9]* multiplayer maps' "$out/mp/data/debug.txt" | tail -1 | sed 's/.*mcc: //')"
		fi

		# xbox: no content folder, the Xbox maps alone
		content_root=none init_line='map_name mods\mcc_b30' run xbox 25 HALO_TICK_THREAD=0
		if grep -aq "memory: loaded mods" "$out/xbox/run.log" || grep -aq "^.*mcc: [0-9]* .* maps" "$out/xbox/data/debug.txt" ||
			! grep -aq "content: no Halo content folder (paths.content is \"none\")" "$out/xbox/run.log"; then
			fail "no content folder: an MCC map was listed or loaded ($out/xbox)"
		else
			echo "no content folder: the Xbox maps alone, mods\\mcc_b30 not loaded"
		fi

		# shadow: the maps folder's mcc_bloodgulch.map (an Xbox map) is not
		# MCC's Blood Gulch, nor listed
		extra_map="$(cd "$data" && pwd)/maps/bloodgulch.map" extra_map_name=mcc_bloodgulch.map init_line= \
			run shadow 40 HALO_TICK_THREAD=1 \
			HALO_NETWORK_TEST="local:mcc_bloodgulch:slayer" HALO_NETWORK_TEST_START=5 HALO_TEST_INPUT=bot:1 HALO_BOTS=1
		if ! grep -aq "the maps folder's 'mcc_bloodgulch' is not listed" "$out/shadow/data/debug.txt" ||
			! grep -aq "custom edition: loading 'd:\\\\mcc\\\\halo1\\\\maps\\\\bloodgulch.map'" "$out/shadow/data/debug.txt"; then
			fail "shadow: the maps folder's mcc_bloodgulch.map was listed or played ($out/shadow)"
		else
			echo "shadow: the maps folder's mcc_bloodgulch.map left out; MCC's played"
		fi
	fi
	;;
esac

case " $parts " in *" menu "*)
	menus_folder=${HALO_MENUS_FOLDER:-$root/port/vita/app0/menus/}
	if [ ! -x "$binary" ]; then
		echo "SKIP (menu): no harness at $binary (HALO_TEST_VITA)"
	elif [ ! -r "$mcc/maps/ui.map" ] || [ ! -r "$mcc/maps/custom_edition/bitmaps.map" ]; then
		echo "SKIP (menu): no MCC halo1 folder with maps/ui.map and maps/custom_edition at $mcc (HALO_TEST_MCC_ROOT)"
	elif [ ! -r "$menus_folder/menus.txt" ]; then
		echo "SKIP (menu): no menus folder at $menus_folder (HALO_MENUS_FOLDER)"
	else
		# (MCC's maps and its custom_edition folder, linked read only, never copied)
		mkdir -p "$out/mccmenu_content/halo1/maps"
		for file in "$mcc"/maps/*.map "$mcc"/maps/custom_edition; do
			ln -sfn "$file" "$out/mccmenu_content/halo1/maps/"
		done
		# mrun NAME SECONDS XBOX(yes|no) [VAR=value...]: the harness with the Xbox maps
		# (yes: HALO_TEST_DATA's) or a data folder with none (no); the content folder MCC's
		mrun() {
			local name=$1 run_seconds=$2 xbox=$3 dir="$out/$1"
			shift 3
			rm -rf "$dir"
			mkdir -p "$dir/data/maps" "$dir/save" "$dir/shots"
			if [ "$xbox" = yes ]; then
				for file in "$(cd "$data" && pwd)"/maps/*.map; do
					ln -sfn "$file" "$dir/data/maps/"
				done
			fi
			[ -n "${init_line:-}" ] && printf 'game_difficulty_set hard\n%s\n' "$init_line" > "$dir/data/init.txt"
			(cd "$dir" && exec unshare -rn sh -c 'ip link set lo up 2> /dev/null; exec "$@"' sh \
				env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
				HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$run_seconds" \
				HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
				HALO_CONTENT_ROOT="$out/mccmenu_content" HALO_MENUS_FOLDER="$menus_folder" HALO_UI_LOG=2 \
				HALO_TICK_THREAD=1 HALO_FRAME_TIMING=300 HALO_SCREENSHOT_DIR="$dir/shots" HALO_SCREENSHOT_EVERY=60 "$@" \
				timeout -k 5 $((run_seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
			local code=$?
			[ "$code" = 0 ] || fail "$name: the harness exited with $code ($dir/run.log)"
			if cat "$dir/run.log" "$dir/data/debug.txt" 2>/dev/null | grep -av "render assertion skipped" |
				grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
				fail "$name: an assertion or exception ($dir)"
			fi
			if [ -n "${HALO_TEST_SHOTS:-}" ]; then
				mkdir -p "$HALO_TEST_SHOTS/$name"
				cp "$dir"/shots/* "$HALO_TEST_SHOTS/$name/" 2> /dev/null
			fi
		}
		# the last frame of a run is not one colour (a menu or a level drawn)
		picture_drawn() {
			python3 - "$1" <<'PY'
import glob, sys
frames = sorted(glob.glob(sys.argv[1] + "/shots/*.bmp"))
if not frames:
    sys.exit(1)
data = open(frames[-1], "rb").read()
sys.exit(0 if len(set(data[54::37])) > 40 else 1)
PY
		}
		ticks_of_log() { grep -a "frame-timing: game tick" "$1" | tail -1 | sed -E 's/.*game tick ([0-9]+).*/\1/'; }
		menu_log() { echo "$out/$1/run.log"; }
		menu_debug() { echo "$out/$1/data/debug.txt"; }

		# pcfiles: the Xbox's ui.map, the content folder's custom_edition Halo PC files
		mrun pcfiles 20 yes HALO_MENU_STYLE=halo3 "HALO_TEST_PAD=wait:150:3000"
		if ! grep -aq "menus: the Halo PC pictures and text read" "$(menu_log pcfiles)" ||
			! grep -aq "menus: Halo 3 screen h3/main/screen for ui.shell.main_menu.main_menu" "$(menu_log pcfiles)" ||
			! grep -aq "ui: screen pc.h3.main.screen" "$(menu_log pcfiles)"; then
			fail "pcfiles: the Halo 3 menu did not come up from the content folder's custom_edition files ($out/pcfiles)"
		elif ! grep -aq "content: Halo PC file bitmaps.map: not in the maps folder, the Halo content folder's custom_edition one" "$(menu_log pcfiles)" ||
			! grep -aq "content: Halo PC file loc.map: not in the maps folder, the Halo content folder's custom_edition one" "$(menu_log pcfiles)"; then
			fail "pcfiles: bitmaps.map and loc.map were not the content folder's custom_edition ones ($out/pcfiles)"
		elif ! picture_drawn "$out/pcfiles"; then
			fail "pcfiles: no picture drawn ($out/pcfiles)"
		else
			echo "pcfiles: the Xbox's ui.map and the Halo 3 menu, its Halo PC files MCC's custom_edition ones"
		fi

		# priority: both ui.maps there
		mrun priority 20 yes HALO_MENU_STYLE=classic "HALO_TEST_PAD=wait:150:3000"
		if ! grep -aq "menus: the menu map is the Xbox's ui.map" "$(menu_debug priority)" ||
			grep -aq "custom edition: loading 'd:.mcc.halo1.maps.ui.map'" "$(menu_debug priority)"; then
			fail "priority: with both, the Xbox's ui.map was not the menus' ($out/priority)"
		else
			echo "priority: both ui.maps there, the Xbox's is the menus'"
		fi
		mrun forced 40 yes HALO_MENU_SOURCE=mcc HALO_MENU_STYLE=classic \
			"HALO_TEST_PAD=wait:150:3000 a wait:150:4000 a wait:150:4000 a wait:150:5000 wait:150:25000"
		if ! grep -aq "menus: the menu map is MCC's halo1.maps.ui.map (paths.menu_source is mcc)" "$(menu_debug forced)" ||
			! grep -aq "custom edition: loading 'd:.mcc.halo1.maps.ui.map'" "$(menu_debug forced)"; then
			fail "forced: paths.menu_source=mcc did not make MCC's ui.map the menus' ($out/forced)"
		elif ! grep -aq "memory: loaded levels.a10.a10" "$(menu_log forced)"; then
			fail "forced: Campaign, the difficulty did not start the Xbox's a10 ($out/forced)"
		else
			echo "forced: paths.menu_source=mcc with the Xbox's files there: MCC's menus, the Xbox's a10 started from them"
		fi
		mrun xboxonly 15 no HALO_MENU_SOURCE=xbox HALO_MENU_STYLE=classic
		if grep -aq "custom edition: loading 'd:.mcc.halo1.maps.ui.map'" "$(menu_debug xboxonly)" ||
			! grep -aq "menus: the menu map is the Xbox's ui.map (paths.menu_source is xbox, but there is no ui.map" "$(menu_debug xboxonly)"; then
			fail "xboxonly: paths.menu_source=xbox with no Xbox ui.map used MCC's ($out/xboxonly)"
		else
			echo "xboxonly: paths.menu_source=xbox: MCC's ui.map not read"
		fi

		# mccmenu: no Xbox file, Menu style Classic ("original": Halo PC's own menus; Classic+
		# has its own, mcccplus). The main menu's six rows: MULTIPLAYER and PROFILES greyed and
		# passed over (Halo PC's network games and its profile list: port/linux/game/
		# halo_pc_menus.c), CAMPAIGN, CREDITS and QUIT open theirs; SETTINGS (Halo PC's own
		# function 137) makes a profile (none yet) and opens EDIT PROFILE SETTINGS, whose
		# CONTROLS SETUP, GAMEPADS, NETWORK SETUP and CHANGE COLOR are greyed; MOUSE SETUP,
		# AUDIO SETUP and VIDEO SETUP open, AUDIO's MASTER VOLUME a step down with OK, MOUSE's
		# sensitivity a step up and invert with OK. mccmenu2, a restart on the same files:
		# AUDIO SETUP and MOUSE SETUP open with those values.
		pad="wait:150:3000 a:150:3500 b:150:2500 down:150:700 a:150:4000"
		pad="$pad down:150:700 down:150:700 a:150:3000 left:150:700 down:150:600 down:150:600 down:150:600 right:150:600 a:150:3000"
		pad="$pad up:150:600 a:150:3000 right:150:600 down:150:600 right:150:600 down:150:600 right:150:600 a:150:3000"
		pad="$pad down:150:600 down:150:600 a:150:3000 right:150:700 b:150:2500 b:150:2500"
		pad="$pad down:150:700 a:150:3500 b:150:2500 down:150:700 a:150:3000 b:150:2500"
		mrun mccmenu 75 no HALO_MENU_STYLE=original HALO_TEST_CONFIG="$out/mccmenu_config.toml" "HALO_TEST_PAD=$pad"
		log=$(menu_log mccmenu)
		debug=$(menu_debug mccmenu)
		missing=
		for row in load_camp settings credits quit_game; do
			grep -aq "ui: handler of ui.shell.main_menu.main_menu_item_$row: event 0" "$log" || missing="$missing $row"
		done
		for row in main_menu_item_multiplayer main_menu_item_profile controller_setup_profile_item gamepads_profile_item \
			network_settings_profile_item color_profile_item; do
			grep -aq "menus: Halo PC's $row greyed: " "$log" || missing="$missing $row(greyed)"
		done
		for screen in player_profile_edit_screen mouse_settings.mouse_settings_screen \
			audio_settings.audio_settings_screen video_settings.video_settings_screen; do
			grep -aq "ui: screen ui.shell.main_menu.settings_select.player_setup.player_profile_edit.$screen" "$log" ||
				missing="$missing $screen"
		done
		if ! grep -aq "menus: the menu map is MCC's halo1.maps.ui.map (no Xbox ui.map in the maps folder)" "$debug" ||
			! grep -aq "custom edition: loading 'd:.mcc.halo1.maps.ui.map'" "$debug" ||
			! grep -aq "ui: screen ui.shell.main_menu.main_menu" "$log"; then
			fail "mccmenu: MCC's ui.map was not the menus' with no Xbox file ($out/mccmenu)"
		elif [ -n "$missing" ]; then
			fail "mccmenu: rows, greyed rows or SETTINGS' screens not reached:$missing ($out/mccmenu)"
		elif grep -aq "ran function 137, one of Halo PC's own\|MCC's menu file: This menu isn't supported" "$debug" "$log" ||
			! grep -aq "menus: Halo PC's SETTINGS: editing profile" "$log" ||
			grep -aq "ui: handler of ui.shell.main_menu.main_menu_item_\(multiplayer\|profile\): event 0" "$log"; then
			fail "mccmenu: SETTINGS did not edit a profile, or a greyed row was pressed ($out/mccmenu)"
		elif ! grep -aq "menus: Halo PC's AUDIO SETUP: set master 9, music 10, effects 10" "$log" ||
			! grep -aq "menus: Halo PC's MOUSE SETUP: set sensitivity 6, invert yes" "$log"; then
			fail "mccmenu: AUDIO SETUP's or MOUSE SETUP's OK did not set the settings ($out/mccmenu)"
		elif ! picture_drawn "$out/mccmenu"; then
			fail "mccmenu: no picture drawn ($out/mccmenu)"
		else
			mrun mccmenu2 25 no HALO_MENU_STYLE=original HALO_TEST_CONFIG="$out/mccmenu_config.toml" \
				"HALO_TEST_PAD=wait:150:3000 down:150:700 a:150:3500 down:150:700 down:150:700 a:150:3000 b:150:2500 up:150:700 a:150:3000 b:150:2500"
			log=$(menu_log mccmenu2)
			if ! grep -aq "menus: Halo PC's AUDIO SETUP: master 9, music 10, effects 10" "$log" ||
				! grep -aq "menus: Halo PC's MOUSE SETUP: sensitivity 6, invert yes" "$log"; then
				fail "mccmenu2: AUDIO SETUP's or MOUSE SETUP's settings did not stay after a restart ($out/mccmenu2)"
			else
				echo "mccmenu: MCC's ui.map alone, Menu style Classic: CAMPAIGN CREDITS QUIT reached, MULTIPLAYER PROFILES greyed; SETTINGS edits a profile, MOUSE AUDIO VIDEO open, their OK sets the settings, which stay after a restart"
			fi
		fi

		# mcconline: no Xbox file, Menu style Online (ElDewrito): the set of screens is added
		# (the Xbox's lobby, server list, profile screens a Halo PC ui.map has not stood in for
		# by an empty widget each) and the Online main menu comes up in place of MCC's own
		mrun mcconline 25 no HALO_MENU_STYLE=online "HALO_TEST_PAD=wait:150:4000 a wait:150:9000"
		log=$(menu_log mcconline)
		if grep -aq "OpenCE's multiplayer screens not added" "$log"; then
			fail "mcconline: the menus folder's screens were not added with MCC's ui.map alone ($out/mcconline)"
		elif ! grep -aq "connected.pregame.connected_pregame_screen: an empty widget stands in for it" "$log"; then
			fail "mcconline: no stand-in for the Xbox's lobby screen ($out/mcconline)"
		elif ! grep -aq "menus: online screen pc.online.main.screen for pc.h3.main.screen" "$log" ||
			! grep -aq "menus: Halo 3 screen h3/main/screen for ui.shell.main_menu.main_menu" "$log"; then
			fail "mcconline: the Online main menu did not replace MCC's ($out/mcconline)"
		elif ! picture_drawn "$out/mcconline"; then
			fail "mcconline: no picture drawn ($out/mcconline)"
		else
			echo "mcconline: MCC's ui.map alone, Menu style Online: the screens added, the Online main menu up"
		fi

		# mccgames / bothgames (1.1.3): GAME MANAGER's GAMES tab - where Halo CE was installed from, its
		# maps counted and checked (game_manager.c), Triangle its details. Classic+: down three to MEDIA,
		# down two to GAME MANAGER; Halo 3: Select (START), down three to GAME MANAGER
		mrun mccgames 32 no HALO_MENU_STYLE=classic \
			"HALO_TEST_PAD=wait:150:5000 down:150:600 down:150:600 down:150:600 a wait:150:2500 down:150:600 down:150:600 a wait:150:5000 y wait:150:2500 b wait:150:2000"
		log=$(menu_log mccgames)
		if ! grep -aq "ui: screen pc.ce.mods.screen" "$log"; then
			fail "mccgames: Classic+'s MEDIA > GAME MANAGER did not open ($out/mccgames)"
		elif ! grep -aqE "game manager: checked: Halo CE [1-9][0-9]* maps \(Xbox 0, MCC [1-9][0-9]*\), sources 2, 0 issues, PC files 7" "$log"; then
			fail "mccgames: GAMES did not say Halo CE from MCC's files alone, all good ($out/mccgames)"
		elif ! grep -aq "ui: screen pc.ce.mods.details" "$log"; then
			fail "mccgames: Triangle did not open the game's details ($out/mccgames)"
		else
			echo "mccgames: MCC's files alone, Classic+: GAME MANAGER says Halo CE from MCC's files, good"
		fi
		mrun bothgames 32 yes HALO_MENU_STYLE=halo3 \
			"HALO_TEST_PAD=wait:150:6000 back:150:2500 down:150:600 down:150:600 down:150:600 a wait:150:5000 y wait:150:2500 b wait:150:2000"
		log=$(menu_log bothgames)
		if ! grep -aq "ui: screen pc.h3.mods.screen" "$log"; then
			fail "bothgames: START > MEDIA > GAME MANAGER did not open ($out/bothgames)"
		elif ! grep -aqE "game manager: checked: Halo CE [1-9][0-9]* maps \(Xbox [1-9][0-9]*, MCC [1-9][0-9]*\), sources 3, " "$log"; then
			fail "bothgames: GAMES did not say Halo CE from both the Xbox's files and MCC's ($out/bothgames)"
		elif ! grep -aq "ui: screen pc.h3.mods.details" "$log"; then
			fail "bothgames: Triangle did not open the game's details ($out/bothgames)"
		else
			echo "bothgames: the Xbox's files and MCC's, Halo 3: GAME MANAGER says both"
		fi

		# b30: started from that menu (the console's map_name, as the menus' tests start a level)
		mrun b30 "$seconds" no HALO_MENU_STYLE=classic \
			'HALO_TEST_COMMANDS=150:map_name mods\mcc_b30;L450:(set cheat_deathless_player true);L600:@skip;L900:@skip'
		ticks=$(ticks_of_log "$(menu_log b30)")
		if ! grep -aq "memory: loaded levels.ui.ui" "$(menu_log b30)" ||
			! grep -aq "menus: the menu map is MCC's" "$(menu_debug b30)"; then
			fail "b30: MCC's menus alone did not come up first ($out/b30)"
		elif ! grep -aq "memory: loaded mods.mcc_b30" "$(menu_log b30)"; then
			fail "b30: mods\\mcc_b30 was not started from MCC's menus alone ($out/b30)"
		elif [ "${ticks:-0}" -lt $((seconds * 10)) ]; then
			fail "b30: reached tick ${ticks:-0} in $seconds s ($out/b30)"
		else
			echo "b30: MCC's menus alone, then mods\\mcc_b30 started and played to tick $ticks"
		fi

		# campaign: Classic+ starts MCC's a10 with no Xbox map (a first start: CAMPAIGN makes a
		# profile, as the Xbox's does, then CHOOSE DIFFICULTY)
		mrun campaign 70 no HALO_MENU_STYLE=classic \
			"HALO_TEST_PAD=wait:150:3000 a wait:150:4000 a wait:150:4000 a wait:150:5000 wait:150:40000"
		if ! grep -aq "memory: loaded mods.mcc_a10" "$(menu_log campaign)"; then
			fail "campaign: CAMPAIGN, the difficulty did not start MCC's a10 with no Xbox map ($out/campaign)"
		else
			echo "campaign: CAMPAIGN, a new profile, the difficulty: MCC's a10 started (mods\\mcc_a10), no Xbox map"
		fi

		# mcccplus: no Xbox file, Menu style Classic+ (rc.9): every row and the screens behind it are
		# the Xbox's screens as Classic+ has them on the Xbox's files (classic/xbox_screens.xml), never
		# the Halo 3 set's: CAMPAIGN's SELECT PROFILE, LOAD LEVEL, CHOOSE DIFFICULTY; MULTIPLAYER, its
		# SPLIT SCREEN's START TO JOIN, SELECT MAP (another map picked), SELECT GAMETYPE and lobby;
		# SETTINGS' SELECT PROFILE TO EDIT, EDIT PROFILE SETTINGS and each of its pages (CONTROLLER
		# SETUP, ADVANCED CONTROLS, CHANGE COLOR, MORE OPTIONS); MEDIA; FORGE, its START FORGE
		# playing MCC's Blood Gulch. Each backed out of to the main menu. The profile: campaign's.
		pad="wait:150:4000 a:150:3500 a:150:3500 a:150:3500 b:150:2500 b:150:2500 b:150:2500"
		pad="$pad down:150:700 a:150:3500 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500"
		pad="$pad a:150:4000 start:150:2500 a:150:3000 a:150:4000 right:150:1500 a:150:4000 a:150:4000"
		pad="$pad b:150:2500 b:150:2500 b:150:2500 b:150:2500 b:150:2500"
		pad="$pad down:150:700 a:150:3000 a:150:3000 down:150:600 a:150:3000 b:150:2500 down:150:600 a:150:3000"
		pad="$pad b:150:2500 down:150:600 a:150:3000 b:150:2500 down:150:600 a:150:3000 b:150:2500 b:150:2500 b:150:2500"
		pad="$pad down:150:700 a:150:3000 b:150:2500 down:150:700 a:150:4000 down:150:600 down:150:600 a:150:3000"
		pad="$pad level:150:5000"
		rm -rf "$out/mcccplus_save"
		cp -a "$out/campaign/save" "$out/mcccplus_save" 2> /dev/null
		mrun mcccplus 200 no HALO_MENU_STYLE=classic HALO_SAVE_ROOT="$out/mcccplus_save" "HALO_TEST_PAD=$pad"
		log=$(menu_log mcccplus)
		missing=
		for screen in load_campaign_player_profile solo_level_select.solo_level_select_screen \
			difficulty_select.difficulty_select_list_screen \
			multiplayer_type_select.split_screen.4way_profile_select.4way_start2join_screen \
			multiplayer_type_select.split_screen.splitscreen_map_select_wrapper \
			multiplayer_type_select.split_screen.gametype_select_screen_wrapper \
			multiplayer_type_select.split_screen.pregame.splitscreen_pregame_wrapper_normal \
			settings_select.player_setup.player_profile_select_screen \
			settings_select.player_setup.player_profile_edit.player_profile_edit_screen \
			settings_select.player_setup.player_profile_edit.color_edit.color_select_screen; do
			grep -aq "ui: screen ui.shell.main_menu.$screen\$" "$log" || missing="$missing $screen"
		done
		for screen in 'pc.mp.screen' 'pc.cp.xb.settings_select.player_setup.player_profile_edit.controller_edit.controller_options_screen' \
			'pc.cp.xb.settings_select.player_setup.player_profile_edit.advanced_controls.advanced_controls_screen' \
			'pc.cp.custom.screen' 'pc.ce.media.screen' 'pc.cp.forge.lobby'; do
			grep -aq "ui: screen $screen\$" "$log" || missing="$missing $screen"
		done
		if grep -aq "OpenCE's multiplayer screens not added" "$log"; then
			fail "mcccplus: the menus folder's screens were not added with MCC's ui.map alone, Classic+ ($out/mcccplus)"
		elif ! grep -aq "menus: Classic+ on Halo PC's ui.map: [0-9]* of the Xbox screens' widgets as the Xbox has them" "$log"; then
			fail "mcccplus: the Xbox screens' widgets were not put in Halo PC's ui.map ($out/mcccplus)"
		elif grep -aq "menus: Halo 3 screen" "$log"; then
			fail "mcccplus: a Halo 3 screen opened in Classic+: $(grep -a -m1 'menus: Halo 3 screen' "$log") ($out/mcccplus)"
		elif [ -n "$missing" ]; then
			fail "mcccplus: not opened:$missing ($out/mcccplus)"
		elif [ "$(grep -ac "ui: screen ui.shell.main_menu.main_menu$" "$log")" -lt 5 ]; then
			fail "mcccplus: not back on the main menu after each row ($out/mcccplus)"
		elif grep -aq "has no sequences\|segmentation fault" "$log"; then
			fail "mcccplus: a widget's bitmap with no sequences, or a fault ($out/mcccplus)"
		elif ! grep -aq "memory: loaded levels.test.mcc_bloodgulch" "$log"; then
			fail "mcccplus: FORGE's START FORGE did not play MCC's Blood Gulch ($out/mcccplus)"
		elif ! picture_drawn "$out/mcccplus"; then
			fail "mcccplus: no picture drawn ($out/mcccplus)"
		else
			echo "mcccplus: MCC's ui.map alone, Classic+: the Xbox's screens for every row (SELECT PROFILE, LOAD LEVEL, CHOOSE DIFFICULTY; split screen's START TO JOIN, SELECT MAP, SELECT GAMETYPE, lobby; SELECT PROFILE TO EDIT, EDIT PROFILE SETTINGS and its pages), MEDIA, FORGE played; no Halo 3 screen"
		fi
	fi
	;;
esac

case " $parts " in *" auto "*)
	mcc_folder=$(cd "$mcc/.." 2> /dev/null && pwd)
	if [ ! -x "$binary" ]; then
		echo "SKIP (auto): no harness at $binary (HALO_TEST_VITA)"
	elif [ ! -r "$mcc/maps/bloodgulch.map" ] || [ ! -r "$mcc/maps/ui.map" ]; then
		echo "SKIP (auto): no MCC halo1 folder with maps/bloodgulch.map and maps/ui.map at $mcc (HALO_TEST_MCC_ROOT)"
	else
		# (the made-up Steam: the Steam folder's own library has no MCC, the
		# second one a link to the real MCC folder)
		home="$out/auto_home"
		mkdir -p "$home/.local/share/Steam/steamapps/common" "$out/auto_lib2/steamapps/common" "$out/auto_empty_home"
		printf '"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n\t"1"\n\t{\n\t\t"path"\t\t"%s"\n\t\t"apps"\n\t\t{\n\t\t\t"976730"\t\t"1"\n\t\t}\n\t}\n}\n' \
			"$home/.local/share/Steam" "$out/auto_lib2" > "$home/.local/share/Steam/steamapps/libraryfolders.vdf"
		ln -sfn "$mcc_folder" "$out/auto_lib2/steamapps/common/Halo The Master Chief Collection"
		found_root="$out/auto_lib2/steamapps/common/Halo The Master Chief Collection"
		# (a copy of the harness in a folder of its own, where the PC build
		# keeps its config.toml and, with no maps folder, its log)
		mkdir -p "$out/auto_bin"
		cp "$binary" "$out/auto_bin/halo"
		# arun NAME SECONDS HOME XBOX(yes|no) [VAR=value...]: the harness with
		# paths.content unset unless given, HOME the made-up one
		arun() {
			local name=$1 run_seconds=$2 run_home=$3 xbox=$4 dir="$out/$1"
			shift 4
			rm -rf "$dir"
			mkdir -p "$dir/run" "$dir/save"
			local data=()
			if [ "$xbox" = yes ]; then
				mkdir -p "$dir/data/maps"
				for file in "$(cd "$data_folder" && pwd)"/maps/*.map; do
					ln -sfn "$file" "$dir/data/maps/"
				done
				data=("HALO_DATA_ROOT=$dir/data")
			fi
			rm -f "$out/auto_bin/debug.txt"
			(cd "$dir/run" && exec unshare -rn sh -c 'ip link set lo up 2> /dev/null; exec "$@"' sh \
				env -u HALO_CONTENT_ROOT -u HALO_DATA_ROOT HOME="$run_home" SDL_AUDIODRIVER=dummy \
				SDL_VIDEODRIVER=offscreen HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 \
				HALO_EXIT_AFTER="$run_seconds" HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 \
				HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false HALO_FRAME_TIMING=300 "${data[@]}" "$@" \
				timeout -k 5 $((run_seconds + 120)) "$out/auto_bin/halo" > "$dir/run.log" 2>&1)
			local code=$?
			[ "$code" = 0 ] || fail "$name: the harness exited with $code ($dir/run.log)"
			[ -f "$out/auto_bin/debug.txt" ] && mv "$out/auto_bin/debug.txt" "$dir/debug.txt"
			[ -f "$dir/data/debug.txt" ] && mv "$dir/data/debug.txt" "$dir/debug.txt"
			if cat "$dir/run.log" "$dir/debug.txt" 2>/dev/null | grep -av "render assertion skipped" |
				grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
				fail "$name: an assertion or exception ($dir)"
			fi
		}
		data_folder=$data

		# autobg: no Xbox file, MCC found by itself; Blood Gulch with bots
		arun autobg "$mp_seconds" "$home" no HALO_TICK_THREAD=1 \
			HALO_NETWORK_TEST="local:mcc_bloodgulch:slayer" HALO_NETWORK_TEST_START=5 HALO_NETWORK_TEST_SCORE=500 \
			HALO_TEST_INPUT=bot:1 HALO_BOTS="${HALO_TEST_MCC_BOTS:-6}"
		log="$out/autobg/run.log"
		kills=$(grep -a "network test: tick" "$log" | tail -1 | grep -aoE " k[0-9]+" | tr -d ' k' | awk '{s+=$1} END {print s+0}')
		if ! grep -aqF "content: Halo MCC found (Steam library $out/auto_lib2): $found_root" "$log" ||
			! grep -aqF "no maps/ folder: Halo MCC is at $found_root" "$log" ||
			! grep -aqF "content: the Halo content folder: $found_root" "$log"; then
			fail "autobg: MCC was not found by itself in the second Steam library ($out/autobg)"
		elif grep -aq "no game data: quitting" "$log"; then
			fail "autobg: the Xbox disc image was asked for ($out/autobg)"
		elif ! grep -aq "custom edition: loading 'd:\\\\mcc\\\\halo1\\\\maps\\\\bloodgulch.map'" "$out/autobg/debug.txt"; then
			fail "autobg: MCC's bloodgulch was not loaded ($out/autobg)"
		elif [ "${kills:-0}" -lt 1 ]; then
			fail "autobg: no kill in $mp_seconds s ($out/autobg)"
		else
			echo "autobg: MCC found by itself (second Steam library), no Xbox file: mcc_bloodgulch $mp_seconds s," \
				"$(grep -ac 'network test: player [0-9]* is named' "$log") players, kills $kills"
		fi

		# autogone: paths.content gone, the Xbox maps there: MCC found anyway
		arun autogone 20 "$home" yes HALO_CONTENT_ROOT="$out/gone_mcc" HALO_TICK_THREAD=1
		log="$out/autogone/run.log"
		if ! grep -aqF "content: paths.content is $out/gone_mcc, but no Halo MCC folder is there" "$log" ||
			! grep -aqF "content: the Halo content folder: $found_root" "$log"; then
			fail "autogone: a gone paths.content did not fall back to MCC found by itself ($out/autogone)"
		else
			echo "autogone: paths.content gone: said, MCC found by itself"
		fi

		# autonone: no Steam: said, the Xbox maps alone
		arun autonone 20 "$out/auto_empty_home" yes HALO_TICK_THREAD=1 HALO_MENU_STYLE=halo3
		log="$out/autonone/run.log"
		if ! grep -aq "content: Halo MCC not found (paths.content; " "$log" ||
			grep -aq "content: the Halo content folder: " "$log" ||
			! grep -aq "memory: loaded levels.ui.ui" "$log"; then
			fail "autonone: no MCC was not said, or the menus did not come up ($out/autonone)"
		else
			echo "autonone: no Steam: \"Halo MCC not found\" said, the Xbox maps' menus up"
		fi
	fi
	;;
esac

case " $parts " in *" inmaps "*)
	if [ ! -x "$binary" ]; then
		echo "SKIP (inmaps): no harness at $binary (HALO_TEST_VITA)"
	elif [ ! -r "$mcc/maps/ui.map" ]; then
		echo "SKIP (inmaps): no MCC halo1 folder with maps/ui.map at $mcc (HALO_TEST_MCC_ROOT)"
	else
		# irun NAME DATA_ROOT [VAR=value...]: the harness for 15 s, no Xbox file
		irun() {
			local name=$1 data_root=$2 dir="$out/$1"
			shift 2
			mkdir -p "$dir/save"
			(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$data_root" \
				HALO_SAVE_ROOT="$dir/save" HALO_NO_AUDIO=1 HALO_HIDDEN_WINDOW=1 HALO_EXIT_AFTER=15 \
				HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false "$@" \
				timeout -k 5 100 "$binary" > "$dir/run.log" 2>&1)
		}
		# inmaps: MCC's files (linked, read only) where the Xbox's belong
		mkdir -p "$out/inmaps/data/maps"
		for file in "$mcc"/maps/*.map; do
			ln -sfn "$file" "$out/inmaps/data/maps/"
		done
		irun inmaps "$out/inmaps/data" HALO_CONTENT_ROOT=none
		if ! grep -aq "damaged disc error: the maps folder holds Halo MCC's maps (.*/inmaps/data/maps/ui.map), not the Xbox disc's: MCC's files belong in the mcc/halo1/maps folder" "$out/inmaps/run.log"; then
			fail "inmaps: the log does not say the maps folder holds MCC's maps ($out/inmaps/run.log)"
		elif grep -aq "ui.map is missing from the maps folder" "$out/inmaps/run.log"; then
			fail "inmaps: the old, misleading reason was given"
		else
			echo "inmaps: MCC's maps in the Xbox maps folder: said so, and where they belong"
		fi

		# beside: <data root>/../mcc (ux0:data/haloce-vita/mcc), then <data root>/mcc
		mkdir -p "$out/beside/haloce-vita/data/maps" "$out/beside/haloce-vita/mcc/halo1/maps" \
			"$out/beside/haloce-vita/data/mcc/halo1/maps"
		irun beside "$out/beside/haloce-vita/data" HALO_CONTENT_ROOT=auto HALO_CONTENT_BESIDE_DATA=1
		if ! grep -aq "content: the Halo content folder: .*/beside/haloce-vita/mcc$" "$out/beside/run.log"; then
			fail "beside: the empty folder beside the data root was not taken when both are empty ($out/beside/run.log)"
		fi
		# (MCC's maps only in <data root>/mcc: they win over the empty folder beside)
		for file in "$mcc"/maps/*.map; do
			ln -sfn "$file" "$out/beside/haloce-vita/data/mcc/halo1/maps/"
		done
		irun beside_data "$out/beside/haloce-vita/data" HALO_CONTENT_ROOT=auto HALO_CONTENT_BESIDE_DATA=1
		if ! grep -aq "content: the Halo content folder: .*/beside/haloce-vita/data/mcc$" "$out/beside_data/run.log"; then
			fail "beside: MCC's maps in <data root>/mcc were not found ($out/beside_data/run.log)"
		fi
		# (and in the folder beside it, the documented one: that is the one taken)
		for file in "$mcc"/maps/*.map "$mcc"/maps/custom_edition; do
			ln -sfn "$file" "$out/beside/haloce-vita/mcc/halo1/maps/"
		done
		mkdir -p "$out/beside/haloce-vita/mcc/halo1/sound"
		[ -e "$mcc/sound/pc" ] && ln -sfn "$mcc/sound/pc" "$out/beside/haloce-vita/mcc/halo1/sound/pc"
		irun beside_mcc "$out/beside/haloce-vita/data" HALO_CONTENT_ROOT=auto HALO_CONTENT_BESIDE_DATA=1
		# (1.1.3: and with no Xbox file at all that is a whole start: MCC's ui.map the menus'
		# map, the Halo PC files from its custom_edition, its sound bank found)
		if ! grep -aq "the menu map is MCC's halo1.maps.ui.map (no Xbox ui.map" "$out/beside_mcc/data/debug.txt" 2>/dev/null &&
			! grep -aq "the menu map is MCC's halo1.maps.ui.map (no Xbox ui.map" "$out/beside/haloce-vita/data/debug.txt"; then
			fail "beside: no Xbox file: MCC's ui.map was not the menus' map"
		elif ! grep -aq "mcc sound: .* from sounds_adpcm.fsb" "$out/beside/haloce-vita/data/debug.txt"; then
			fail "beside: MCC's sound bank was not found"
		fi
		if grep -aq "content: the Halo content folder: .*/beside/haloce-vita/mcc$" "$out/beside_mcc/run.log"; then
			echo "beside: ux0:data/haloce-vita/mcc found by itself, and data/mcc when it is the one with the maps"
		else
			fail "beside: MCC's maps beside the data root were not found ($out/beside_mcc/run.log)"
		fi
	fi
	;;
esac

[ "$status" = 0 ] && echo "PASS"
test_out_done "$status"
exit "$status"
