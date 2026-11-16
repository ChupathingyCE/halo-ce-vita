#!/bin/bash
# MCC's Halo CE sound effects (port/linux/src/fsb5.c, mcc_sound_list.c,
# port/linux/game/mcc_sound.c, cache_file_formats.c mcc_sound_map,
# docs/mcc_cea_maps.md): the FMOD bank of IMA ADPCM streams
# (halo1\sound\pc\sounds_adpcm.fsb) and the lists that name the sounds in it
# (lst\sounds_adpcm.lst.bin), played as the Xbox ADPCM they are.
#
# 1. unit    mcc_sound_test.c: the FSB5 reader and the list parser on made-up
#            banks and lists (no MCC file), under AddressSanitizer and UBSan
# 2. fuzz    mcc_sound_fuzz.c: MCC_FUZZ_ITERATIONS (default 100000) changed
#            banks and lists from a fixed seed; with MCC_FUZZ_SECONDS=n also n
#            seconds of libFuzzer (clang), what it finds in MCC_FUZZ_OUT
#    celtfuzz celt_fuzz.c (1.1.3 audit): MCC's FMOD CELT streams decoded and made
#            Xbox ADPCM as the sound cache does (h2_sound_codec.c, fsb_celt.c,
#            libcelt) on changed chunks, CELT_FUZZ_SECONDS (default 60) of
#            libFuzzer under ASan and UBSan (clang; SKIP without), seeded from
#            the player's own sounds_dialogs.fsb and sounds_music.fsb when there
#            (celt_fuzz_seeds.py); what it finds in CELT_FUZZ_OUT
# 3. audit   mcc_sound_audit.c on the player's own sound folder (HALO_TEST_MCC_ROOT,
#            default Steam's; SKIP without one): every bank and list parses, the
#            lists name only streams the banks have, the ADPCM bank is covered once
#            in order by its list with names that agree, and every CELT stream starts
#            with FMOD's CELT frame header
# 4. harness on the Linux harness with sound on (SDL's dummy driver, the mixer's
#            output dumped, HALO_AUDIO_DUMP; the MCC maps and sound folder linked
#            read only into the test's content folder, never copied):
#      events    mcc_bloodgulch with sounds played through HALO_TEST_COMMANDS' "@sound"
#                (an assault rifle's fire, a footstep, a grenade's explosion, a
#                Warthog's engine (a record to each pitch range), two stereo sounds):
#                for each, the permutation the sound manager queued is found in
#                the bank and decoded here, and the mixer must have played the same
#                number of frames at the same RMS (mcc_sound_check.py); the mixer's
#                output must not be silent; most of the map's sounds must be played
#                from the bank and none may not match its streams
#      campaign  mcc_b30 the same way (a quarter of a second at 0.001 RMS or more
#                must be most of them), and its dialogue and music counted as
#                what is still silent
#      nosound   a content folder without the sound folder: the map loads and plays,
#                silent, saying so
#      damaged   the list with another version, and the bank cut short (copies of
#                the first bytes in the test's folder): the sounds are silent, said, no
#                assertion
#      foreign   a Custom Edition map (extinction, HALO_TEST_CE_MAPS, with Halo PC's
#                maps, HALO_TEST_CE_RESOURCES) whose sound claims to be streams of
#                MCC's CELT banks (ce_celt_claim_map.py): played, it must never be
#                read from the player's bank (only MCC's own maps and scene packs)
#   HALO_TEST_VITA  the harness (default build/linux/halo)   HALO_TEST_DATA  the Xbox maps
#   HALO_TEST_MCC_ROOT  MCC's halo1 folder                  HALO_TEST_OUT   where the logs go
#   run_mcc_sound_test.sh [unit|fuzz|celtfuzz|audit|harness]   (default all)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
. "$here/test_data_root.sh"
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$data_root/data2276}
mcc=${HALO_TEST_MCC_ROOT:-$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_mcc_sound_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
status=0
fail() { echo "FAIL: $1"; status=1; }
parts=${*:-unit fuzz celtfuzz audit harness}
cc=${CC:-gcc}
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
echo 'int main(void){return 0;}' | $cc $sanitize -x c - -o "$out/probe" 2>/dev/null || sanitize=
src="$root/port/linux/src"
export ASAN_OPTIONS=detect_leaks=1:exitcode=99 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:exitcode=98

case " $parts " in *" unit "*)
	$cc -g -O1 -Wall -Wextra -Werror $sanitize -I"$src" "$here/mcc_sound_test.c" "$src/fsb5.c" "$src/mcc_sound_list.c" \
		-o "$out/mcc_sound_test" || fail "mcc_sound_test did not build"
	[ -x "$out/mcc_sound_test" ] && { "$out/mcc_sound_test" || fail "mcc_sound_test"; }
	;;
esac

case " $parts " in *" fuzz "*)
	$cc -g -O1 -Wall -Wextra $sanitize -D_GNU_SOURCE -I"$src" "$here/mcc_sound_fuzz.c" "$src/fsb5.c" "$src/mcc_sound_list.c" \
		-o "$out/mcc_sound_fuzz" || fail "mcc_sound_fuzz did not build"
	[ -x "$out/mcc_sound_fuzz" ] && { MCC_FUZZ_ITERATIONS=${MCC_FUZZ_ITERATIONS:-100000} "$out/mcc_sound_fuzz" || fail "mcc_sound_fuzz"; }
	if [ -n "${MCC_FUZZ_SECONDS:-}" ]; then
		if command -v clang > /dev/null 2>&1 &&
			clang -g -O1 -fsanitize=fuzzer,address,undefined -DLIBFUZZER -D_GNU_SOURCE -I"$src" "$here/mcc_sound_fuzz.c" \
				"$src/fsb5.c" "$src/mcc_sound_list.c" -o "$out/mcc_sound_libfuzzer" 2> /dev/null; then
			fuzz_out=${MCC_FUZZ_OUT:-$out/fuzz_findings}
			mkdir -p "$fuzz_out/corpus"
			"$out/mcc_sound_libfuzzer" -max_total_time="$MCC_FUZZ_SECONDS" -artifact_prefix="$fuzz_out/" "$fuzz_out/corpus" \
				> "$out/libfuzzer.log" 2>&1 || fail "libFuzzer found something (see $fuzz_out)"
			tail -2 "$out/libfuzzer.log"
		else
			echo "SKIP (libFuzzer): no clang with libFuzzer"
		fi
	fi
	;;
esac

have_mcc=0
[ -r "$mcc/sound/pc/sounds_adpcm.fsb" ] && [ -r "$mcc/maps/bitmaps.map" ] && have_mcc=1

case " $parts " in *" celtfuzz "*)
	# libcelt's files with celt_port.h each, as tools/linux_build.py builds them
	celt="$root/port/third_party/celt"
	fuzz_seconds=${CELT_FUZZ_SECONDS:-60}
	if ! command -v clang > /dev/null 2>&1 ||
		! echo 'int LLVMFuzzerTestOneInput(const unsigned char *d, unsigned long n){(void)d;(void)n;return 0;}' |
			clang -fsanitize=fuzzer -x c - -o "$out/celt_probe" 2> /dev/null; then
		echo "SKIP (celtfuzz): no clang with libFuzzer"
	else
		# (rate.c's own "-1 << n" in the mode's setup, libcelt's, benign: not a finding)
		celt_flags="-g -O1 -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize=shift -fno-sanitize-recover=undefined"
		mkdir -p "$out/celt_obj"
		objects=
		while read -r source; do
			[ -n "$source" ] || continue
			object="$out/celt_obj/$(basename "$source" .c).o"
			clang $celt_flags -std=gnu11 -w -I"$celt/libcelt" -include "$celt/celt_port.h" -c "$celt/$source" -o "$object" ||
				fail "celtfuzz: libcelt's $source did not build"
			objects="$objects $object"
		done < "$celt/sources.txt"
		if clang -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize=shift -fno-sanitize-recover=undefined \
			-D_GNU_SOURCE -I"$src" "$here/celt_fuzz.c" "$src/h2_sound_codec.c" "$src/fsb_celt.c" "$src/xbox_adpcm_encoder.c" \
			$objects -lm -o "$out/celt_fuzz"; then
			fuzz_out=${CELT_FUZZ_OUT:-$out/celt_fuzz_findings}
			mkdir -p "$fuzz_out/corpus"
			if [ -r "$mcc/sound/pc/sounds_dialogs.fsb" ]; then
				echo "celtfuzz: $(python3 -I "$here/celt_fuzz_seeds.py" "$mcc/sound/pc" "$fuzz_out/corpus") seeds from MCC's CELT banks"
			else
				echo "celtfuzz: no MCC CELT bank at $mcc/sound/pc (HALO_TEST_MCC_ROOT): no seeds, the fuzzer's own"
			fi
			if "$out/celt_fuzz" -max_total_time="$fuzz_seconds" -max_len=8192 -artifact_prefix="$fuzz_out/" "$fuzz_out/corpus" \
				> "$out/celt_fuzz.log" 2>&1; then
				echo "celtfuzz: $(grep -a "^Done" "$out/celt_fuzz.log" | tail -1), no crash, no UB"
			else
				tail -20 "$out/celt_fuzz.log"
				fail "celtfuzz: libFuzzer found something (see $fuzz_out, $out/celt_fuzz.log)"
			fi
		else
			fail "celtfuzz: celt_fuzz did not build"
		fi
	fi
	;;
esac

case " $parts " in *" audit "*)
	if [ $have_mcc = 0 ]; then
		echo "SKIP (audit): no MCC halo1 folder at $mcc (HALO_TEST_MCC_ROOT)"
	else
		$cc -g -O1 -Wall -Wextra -Werror $sanitize -D_FILE_OFFSET_BITS=64 -I"$src" "$here/mcc_sound_audit.c" "$src/fsb5.c" "$src/mcc_sound_list.c" \
			-o "$out/mcc_sound_audit" || fail "mcc_sound_audit did not build"
		[ -x "$out/mcc_sound_audit" ] && { "$out/mcc_sound_audit" "$mcc/sound/pc" || fail "mcc_sound_audit"; }
	fi
	;;
esac

case " $parts " in *" harness "*)
	if [ ! -x "$binary" ]; then
		echo "SKIP (harness): no harness at $binary (HALO_TEST_VITA)"
	elif [ ! -e "$data/maps/ui.map" ]; then
		echo "SKIP (harness): no Xbox maps in $data/maps (HALO_TEST_DATA)"
	elif [ $have_mcc = 0 ] || ! command -v python3 > /dev/null; then
		echo "SKIP (harness): no MCC halo1 folder with sounds at $mcc (HALO_TEST_MCC_ROOT), or no python3"
	else
		# run NAME SECONDS CONTENT_ROOT [VAR=value...]: sound on (SDL's dummy
		# driver), the mix dumped to <run>/audio.f32; init_line is the map
		run() {
			local name=$1 run_seconds=$2 content=$3 dir="$out/$1"
			shift 3
			rm -rf "$dir"
			mkdir -p "$dir/data/maps" "$dir/save"
			for file in "$(cd "$data" && pwd)"/maps/*.map; do
				ln -sfn "$file" "$dir/data/maps/"
			done
			printf 'game_difficulty_set hard\n%s\n' "${init_line:-}" > "$dir/data/init.txt"
			(cd "$dir" && exec unshare -rn sh -c 'ip link set lo up 2> /dev/null; exec "$@"' sh \
				env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
				HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$run_seconds" \
				HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
				HALO_CONTENT_ROOT="$content" HALO_FRAME_TIMING=300 HALO_AUDIO_DUMP="$dir/audio.f32" "$@" \
				timeout -k 5 $((run_seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
			local code=$?
			[ "$code" = 0 ] || fail "$name: the harness exited with $code ($dir/run.log)"
			if cat "$dir/run.log" "$dir/data/debug.txt" 2>/dev/null | grep -av "render assertion skipped" |
				grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
				fail "$name: an assertion or exception ($dir)"
			fi
		}
		# a content folder: MCC's maps linked, its sound folder linked (or not)
		content() {
			local folder=$1 sound=${2:-link}
			mkdir -p "$folder/halo1/maps"
			for file in "$mcc"/maps/*.map; do
				ln -sfn "$file" "$folder/halo1/maps/"
			done
			[ "$sound" = link ] && ln -sfn "$mcc/sound" "$folder/halo1/sound"
			return 0
		}
		# the mixer's output: the share of quarter seconds that are not silent
		loud_share() {
			python3 -I - "$1" << 'PYEOF'
import array, math, sys
a = array.array('f')
a.frombytes(open(sys.argv[1], 'rb').read())
step = 48000 // 4 * 2
parts = [a[i:i + step] for i in range(0, len(a) - step, step)]
loud = sum(1 for p in parts if math.sqrt(sum(x * x for x in p) / len(p)) >= 0.001)
print("%d %d" % (loud, len(parts)))
PYEOF
		}
		summary_of() { grep -a "mcc sound: [0-9]* of the map" "$1/data/debug.txt" | tail -1; }
		mapped_of() { summary_of "$1" | sed -E 's/.*mcc sound: ([0-9]+) of the map.s ([0-9]+) sounds.*/\1/'; }
		total_of() { summary_of "$1" | sed -E 's/.*mcc sound: ([0-9]+) of the map.s ([0-9]+) sounds.*/\2/'; }
		mismatched_of() { summary_of "$1" | sed -E 's/.*, ([0-9]+) that do not match their streams.*/\1/'; }

		content "$out/mcc"
		checker="$here/mcc_sound_check.py"
		# events: sounds played by name in MCC's Blood Gulch, quiet enough to hear them
		tags=('sound\sfx\weapons\assault rifle\fire' 'sound\sfx\impulse\footsteps\cyborg\dirt\run' 'sound\sfx\weapons\frag grenade\expl'
			'sound\sfx\vehicles\warthog_engine5' 'sound\sfx\impulse\coolant\enter_water' 'sound\sfx\ui\pickup_dbl_shield'
			'sound\sfx\ambience\multiplayer\countryday2')
		commands=
		tick=300
		for tag in "${tags[@]}"; do
			commands="$commands$tick:@sound $tag;"
			tick=$((tick + 150))
		done
		init_line= run events 60 "$out/mcc" HALO_TICK_THREAD=1 HALO_NETWORK_TEST="local:mcc_bloodgulch:slayer" HALO_NETWORK_TEST_START=3 HALO_BOTS=1 \
			HALO_NETWORK_TEST_SCORE=500 HALO_SOUND_TRACE=1 HALO_AUDIO_PACKET_TRACE=1 "HALO_TEST_COMMANDS=$commands"
		if ! python3 -I "$checker" events "$out/events/run.log" "$mcc/sound/pc" "${tags[@]}" > "$out/events/check.txt" 2>&1; then
			cat "$out/events/check.txt"
			fail "events: the mixer did not play the bank's streams ($out/events)"
		else
			cat "$out/events/check.txt"
		fi
		mapped=$(mapped_of "$out/events"); total=$(total_of "$out/events")
		if [ "${mapped:-0}" -lt $((${total:-1} * 8 / 10)) ] || [ "$(mismatched_of "$out/events")" != 0 ]; then
			fail "events: $(summary_of "$out/events")"
		else
			echo "events: $mapped of $total sounds of mcc_bloodgulch play from the bank; $(summary_of "$out/events" | sed 's/.*; still silent/still silent/')"
		fi
		read -r loud parts_count <<< "$(loud_share "$out/events/audio.f32")"
		[ "${loud:-0}" -ge 4 ] || fail "events: the mixer's output was silent ($loud of $parts_count quarter seconds)"

		# campaign
		init_line='map_name mods\mcc_b30' run campaign 40 "$out/mcc" HALO_TICK_THREAD=0 \
			'HALO_TEST_COMMANDS=30:(set cheat_deathless_player true);60:@skip;300:@skip;600:@skip'
		mapped=$(mapped_of "$out/campaign"); total=$(total_of "$out/campaign")
		read -r loud parts_count <<< "$(loud_share "$out/campaign/audio.f32")"
		if [ "${mapped:-0}" -lt 400 ] || [ "$(mismatched_of "$out/campaign")" != 0 ]; then
			fail "campaign: $(summary_of "$out/campaign")"
		elif [ "${loud:-0}" -lt $((${parts_count:-1} / 2)) ]; then
			fail "campaign: the mixer's output was quiet ($loud of $parts_count quarter seconds)"
		else
			echo "campaign mcc_b30: $mapped of $total sounds from the bank, $loud of $parts_count quarter seconds of output heard;" \
				"$(summary_of "$out/campaign" | sed 's/.*; still silent/still silent/')"
		fi

		# no sound folder: the map plays, silent
		mkdir -p "$out/nosound_content"
		content "$out/nosound_content" none
		init_line='map_name mods\mcc_b30' run nosound 20 "$out/nosound_content" HALO_TICK_THREAD=0 'HALO_TEST_COMMANDS=60:@skip'
		if ! grep -aq "mcc: no sound effects bank" "$out/nosound/data/debug.txt"; then
			fail "nosound: its missing sound bank was not said ($out/nosound)"
		elif [ "$(grep -ac 'mcc sound: [0-9]* of the map' "$out/nosound/data/debug.txt")" != 0 ]; then
			fail "nosound: sounds were mapped without a bank"
		else
			echo "nosound: the map plays without MCC's sound folder, saying so"
		fi

		# damaged files: the list of another version, the bank cut short
		mkdir -p "$out/damaged_content/halo1/sound/pc/lst"
		content "$out/damaged_content" none
		cp "$mcc/sound/pc/lst/sounds_adpcm.lst.bin" "$out/damaged_content/halo1/sound/pc/lst/"
		cp "$mcc/sound/pc/lst/sounds_dialogs.lst.bin" "$out/damaged_content/halo1/sound/pc/lst/"
		head -c 1048576 "$mcc/sound/pc/sounds_adpcm.fsb" > "$out/damaged_content/halo1/sound/pc/sounds_adpcm.fsb"
		init_line='map_name mods\mcc_b30' run damaged_bank 20 "$out/damaged_content" HALO_TICK_THREAD=0 'HALO_TEST_COMMANDS=60:@skip'
		if ! grep -aq "mcc sound: the sound effects are silent: sounds_adpcm.fsb:" "$out/damaged_bank/data/debug.txt"; then
			fail "damaged_bank: the cut bank was not refused, said ($out/damaged_bank)"
		else
			echo "damaged_bank: $(grep -a 'mcc sound: the sound effects are silent' "$out/damaged_bank/data/debug.txt" | sed 's/.*are silent: //')"
		fi
		ln -sfn "$mcc/sound/pc/sounds_adpcm.fsb" "$out/damaged_content/halo1/sound/pc/sounds_adpcm.fsb"
		python3 -I -c "
import sys
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
d[0] = 2
open(p, 'wb').write(d)" "$out/damaged_content/halo1/sound/pc/lst/sounds_adpcm.lst.bin"
		init_line='map_name mods\mcc_b30' run damaged_list 20 "$out/damaged_content" HALO_TICK_THREAD=0 'HALO_TEST_COMMANDS=60:@skip'
		if ! grep -aq "mcc sound: the sound effects are silent: sounds_adpcm.lst.bin: version 2" "$out/damaged_list/data/debug.txt"; then
			fail "damaged_list: the list of another version was not refused, said ($out/damaged_list)"
		else
			echo "damaged_list: refused: version 2"
		fi

		# foreign: a Custom Edition map (a download, say) whose sound says its
		# permutations are streams of MCC's CELT banks must not read them (1.1.3
		# audit, LOW-1): only MCC's own maps and scene packs built from them may
		ce_maps=${HALO_TEST_CE_MAPS:-$data_root/custom-maps-dl}
		resources=${HALO_TEST_CE_RESOURCES:-$mcc/maps/custom_edition}
		foreign_tag='sound\sfx\weapons\gunfire_sounds\fire2'
		if [ ! -e "$ce_maps/extinction.map" ] || [ ! -e "$resources/sounds.map" ]; then
			echo "SKIP (foreign): no extinction.map in $ce_maps (HALO_TEST_CE_MAPS) or Halo PC's maps in $resources (HALO_TEST_CE_RESOURCES)"
		else
			dir="$out/foreign"
			rm -rf "$dir"
			mkdir -p "$dir/data/maps" "$dir/save"
			for file in "$(cd "$data" && pwd)"/maps/*.map; do
				ln -sfn "$file" "$dir/data/maps/"
			done
			for name in bitmaps sounds loc; do
				ln -sfn "$(cd "$resources" && pwd)/$name.map" "$dir/data/maps/$name.map"
			done
			if ! python3 -I "$here/ce_celt_claim_map.py" "$ce_maps/extinction.map" "$dir/data/maps/extinction.map" "$foreign_tag" > /dev/null; then
				fail "foreign: the map could not be made"
			else
				(cd "$dir" && exec unshare -rn sh -c 'ip link set lo up 2> /dev/null; exec "$@"' sh \
					env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
					HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER=45 \
					HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
					HALO_CUSTOM_EDITION=1 HALO_CONTENT_ROOT="$out/mcc" HALO_OGG_TRACE=1 HALO_SOUND_CACHE_PREFILL=0 \
					"HALO_TEST_COMMANDS=M240:map_name levels\\test\\extinction\\extinction;L300:@sound $foreign_tag;L330:@sound $foreign_tag;L420:@menu;" \
					timeout -k 5 165 "$binary" > "$dir/run.log" 2>&1)
				code=$?
				if [ "$code" != 0 ]; then
					fail "foreign: the harness exited with $code ($dir/run.log)"
				elif ! grep -aq "test command: sound .*fire2 played" "$dir/run.log"; then
					fail "foreign: the map's sound was never asked for ($dir/run.log)"
				elif grep -a "celt sound: " "$dir/run.log" | grep -aqF "of $foreign_tag:"; then
					grep -a "celt sound: " "$dir/run.log" | head -2 | sed 's/^/  /'
					fail "foreign: a Custom Edition map's sound was read from MCC's CELT bank ($dir/run.log)"
				elif cat "$dir/run.log" "$dir/data/debug.txt" 2>/dev/null | grep -av "render assertion skipped" |
					grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
					fail "foreign: an assertion or exception ($dir)"
				else
					echo "foreign: a Custom Edition map's sound that says it is MCC's CELT is silent, never read from the bank"
				fi
			fi
		fi
	fi
	;;
esac

test_out_done "$status"
if [ "$status" = 0 ]; then echo "PASS"; else echo "FAILED"; fi
exit "$status"
