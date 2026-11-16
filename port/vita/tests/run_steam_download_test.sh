#!/bin/sh
# The Steam download (port/steamdl, docs/steam_download_security.md) on the
# PC, with no Steam sign-in and nothing from Steam's servers: built 32-bit as
# the Vita is where the compiler can (else as the host is), with
# AddressSanitizer and UBSan:
#   units      steam_download_test.c's tests of each piece, on fixtures
#              steam_fake_server.py makes (made-up chunks and a manifest)
#   flows      the whole download against steam_fake_server.py on 127.0.0.1
#              (a fake CM over a WebSocket and a fake CDN; made-up tokens and
#              files): Halo CE, with and without the sound banks; a damaged chunk; a
#              connection cut; a cancel then a resume; a refused logon; a
#              refused QR; a depot not owned; a kick; a bad Multi. After each:
#              the token revoked then logged off (when signed in), no token in
#              the log, the files, the journals or the marker, nothing written
#              outside the card folder (the manifests list ../ and absolute
#              names), the files' bytes the fake's
#   manifests  every local depotcache manifest of depots 976733-976736
#              (STEAM_DEPOTCACHE, default ~/.local/share/Steam/depotcache,
#              read only): steam_manifest_dump's list against
#              tools/steam_manifest_probe.py's, and the plan's files in each
# With STEAM_FUZZ_SECONDS=n, each fuzz target (steam_fuzz.c) also runs n
# seconds under libFuzzer (64-bit, clang), its findings in STEAM_FUZZ_OUT.
#   CC  the compiler (default clang)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/steam_download_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
cc=${CC:-clang}
mkdir -p "$out"
server_pid=
client= client_env= server_tls= sub=
cleanup() {
	rc=$?
	[ -n "$server_pid" ] && kill "$server_pid" 2>/dev/null || true
	test_out_done $rc
}
trap cleanup EXIT
mbed=$root/port/third_party/mbedtls
config='-DMBEDTLS_USER_CONFIG_FILE="mbedtls_linux_config.h"'
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
include="-I$root/port/steamdl -I$mbed/include -I$root/port/linux/src -I$root/port/third_party/libdeflate
	-I$root/port/third_party/lzma -I$root/port/third_party/zstd -I$root/port/third_party/qrcodegen"
vendored="$root/port/third_party/lzma/LzmaDec.c $root/port/third_party/zstd/zstddeclib.c
	$root/port/third_party/libdeflate/lib/deflate_decompress.c $root/port/third_party/libdeflate/lib/utils.c
	$root/port/third_party/libdeflate/lib/x86/cpu_features.c $root/port/third_party/qrcodegen/qrcodegen.c"
steam="$root/port/steamdl/steam_pb.c $root/port/steamdl/steam_crypto.c $root/port/steamdl/steam_zip.c
	$root/port/steamdl/steam_manifest.c $root/port/steamdl/steam_chunk.c $root/port/steamdl/steam_file.c
	$root/port/steamdl/steam_kv.c $root/port/steamdl/steam_ws.c $root/port/steamdl/steam_http.c
	$root/port/steamdl/steam_cm.c $root/port/steamdl/steam_plan.c $root/port/steamdl/steam_session.c
	$root/port/steamdl/steam_download.c $root/port/steamdl/steam_job.c $root/port/steamdl/steam_workshop.c"
export ASAN_OPTIONS=detect_leaks=1:exitcode=99 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:exitcode=98
bits=-m32
echo 'int main(void) { return 0; }' > "$out/probe.c"
$cc $bits $sanitize "$out/probe.c" -o "$out/probe" 2>/dev/null && "$out/probe" || bits=
status=0

echo "--- build ($cc ${bits:-64-bit})"
mkdir -p "$out/obj"
ls "$mbed"/library/*.c | xargs -P "$(nproc)" -I{} sh -c \
	'$0 $5 -O1 -w -I"$1/include" -I"$1/library" -I"$3" "$4" -c "{}" -o "$2/mbed_$(basename "{}" .c).o"' \
	"$cc" "$mbed" "$out/obj" "$root/port/linux/src" "$config" "$bits"
for source in $vendored; do
	$cc $bits -O1 -g -w $sanitize $include -c "$source" -o "$out/obj/v_$(basename "$source" .c).o" &
done
wait
flags="$bits -g -O1 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 $sanitize $include $config"
$cc $flags "$here/steam_download_test.c" $steam "$root/port/steamdl/steam_net.c" "$root/port/linux/src/posix_https.c" \
	"$root/port/linux/src/posix_net.c" "$out"/obj/*.o -lpthread -o "$out/steam_download_test"
$cc $flags "$here/steam_manifest_dump.c" $steam "$root/port/steamdl/steam_net.c" "$root/port/linux/src/posix_https.c" \
	"$root/port/linux/src/posix_net.c" "$out"/obj/*.o -lpthread -o "$out/steam_manifest_dump"

echo "--- units"
python3 -I "$here/steam_fake_server.py" --fixtures "$out/fixtures" --seed 3
"$out/steam_download_test" units "$out/fixtures" || status=1

echo "--- flows (a fake CM and CDN on 127.0.0.1)"
# flow <name> <mode> <games> <expect: ok|fail|cancel> [cancel after] [keep: reuse the last run's card]
last=
flow() {
	name=$1 mode=$2 games=$3 expect=$4 cancel=$5 keep=$6
	work=$out/flow-$name
	if [ -n "$keep" ]; then cp -r "$out/flow-$keep" "$work"; rm -f "$work/transcript.txt" "$work/log.txt"
	else mkdir -p "$work"; fi
	fifo=$work/stdin
	rm -f "$fifo"
	mkfifo "$fifo"
	python3 -I "$here/steam_fake_server.py" --work "$work" --mode "$mode" $server_tls < "$fifo" > "$work/server.json" &
	server_pid=$!
	exec 3> "$fifo"
	i=0
	while [ ! -s "$work/server.json" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
	ports=$(python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d["cm"], d["cdn"])' "$work/server.json")
	token=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["token"])' "$work/server.json")
	access=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["access"])' "$work/server.json")
	set -- $ports
	line=$(${client_env:-} "${client:-$out/steam_download_test}" ${sub:-flow} "$1" "$2" "$work" "$games" $cancel) || \
		{ echo "FAIL $name: the test crashed"; status=1; }
	exec 3>&-
	wait "$server_pid" 2>/dev/null || true
	server_pid=
	echo "  $name: $line"
	echo "$line" > "$work/line"
	ok=$(echo "$line" | sed -n 's/.* ok=\([0-9]\).*/\1/p')
	cancelled=$(echo "$line" | sed -n 's/.*cancelled=\([0-9]\).*/\1/p')
	case "$expect" in
	ok) [ "$ok" = 1 ] || { echo "FAIL $name: not ok"; status=1; } ;;
	fail) [ "$ok" = 0 ] && [ "$cancelled" = 0 ] || { echo "FAIL $name: should have failed"; status=1; } ;;
	cancel) [ "$cancelled" = 1 ] || { echo "FAIL $name: not cancelled"; status=1; } ;;
	esac
	# signed out: the token revoked (and, once signed in, logged off after it)
	if [ "$mode" = losesession ]; then
		# (the first session lost: revoked over a new connection; the second ends as usual)
		order=$(grep -E "cm: (revoke|logoff|connection dropped)" "$work/transcript.txt" | tr '\n' ' ')
		[ "$order" = "cm: connection dropped cm: revoke ok cm: revoke ok cm: logoff " ] || \
			{ echo "FAIL $name: end was '$order'"; status=1; }
	elif grep -q "cm: kicked" "$work/transcript.txt"; then
		# (the server logged it off: the revoke alone, not signed in)
		order=$(grep -E "cm: (revoke|logoff)" "$work/transcript.txt" | tr '\n' ' ')
		[ "$order" = "cm: revoke ok " ] || { echo "FAIL $name: end was '$order'"; status=1; }
	elif grep -q "cm: logon ok" "$work/transcript.txt"; then
		order=$(grep -E "cm: (revoke|logoff)" "$work/transcript.txt" | tr '\n' ' ')
		[ "$order" = "cm: revoke ok cm: logoff " ] || { echo "FAIL $name: end was '$order'"; status=1; }
	elif grep -q "cm: call Authentication.PollAuthSessionStatus" "$work/transcript.txt" && \
		[ "$mode" != qr_refuse ]; then
		grep -q "cm: revoke ok" "$work/transcript.txt" || { echo "FAIL $name: not revoked"; status=1; }
	fi
	# no token anywhere we wrote
	if grep -r -l -a -F -e "$token" -e "$access" "$work/card" "$work/log.txt" 2>/dev/null | grep .; then
		echo "FAIL $name: a token was written"; status=1
	fi
	[ ! -e "$work/steam-session.txt" ] || grep -q "cm: revoke ok" "$work/transcript.txt" && \
		{ [ ! -e "$work/steam-session.txt" ] || { echo "FAIL $name: marker left after a revoke"; status=1; }; } || true
	# nothing outside the card's two folders, no hostile file
	find "$work" -name 'evil*' | grep . && { echo "FAIL $name: a hostile name was written"; status=1; }
	[ ! -d "$work/card" ] || [ -z "$(ls "$work/card" | grep -v -x -e haloce-vita)" ] || \
		{ echo "FAIL $name: something outside the folders"; status=1; }
	[ ! -e /tmp/evil.map ] || { echo "FAIL $name: /tmp/evil.map"; status=1; }
	last=$work
	return 0
}
flow ce normal 1 ok
# (the files: present, whole, no .part or journal left; Halo CE alone takes no CELT bank)
n=$(find "$out/flow-ce/card/haloce-vita/mcc/halo1" -type f | wc -l)
left=$(find "$out/flow-ce/card" -name '*.part*' | wc -l)
banks=$(ls "$out/flow-ce/card/haloce-vita/mcc/halo1/sound/pc" | grep -c fsb)
echo "  ce: $n files under mcc/halo1, $left partial, $banks banks"
[ "$n" = 20 ] && [ "$left" = 0 ] && [ "$banks" = 1 ] || { echo "FAIL ce: files"; status=1; }
flow ce-again normal 1 ok "" ce
grep -q "already=20" "$out/flow-ce-again/line" || { echo "FAIL ce-again: not all already there"; status=1; }
# (the sound banks alone: depot 976733 only, the three the fake has; the music's absence is a note)
flow banks normal 2 ok
n=$(find "$out/flow-banks/card/haloce-vita/mcc/halo1" -type f | wc -l)
banks=$(ls "$out/flow-banks/card/haloce-vita/mcc/halo1/sound/pc" | tr '\n' ' ')
echo "  banks: $n files; sound/pc: $banks"
[ "$n" = 3 ] && [ "$banks" = "sounds_debug.fsb sounds_dialogs.fsb sounds_stream.fsb " ] || { echo "FAIL banks: files"; status=1; }
grep -q "sounds_music.fsb is not in depot 976733: left out" "$out/flow-banks/log.txt" || { echo "FAIL banks: the music's note"; status=1; }
grep -q "cm: depot key 976734" "$out/flow-banks/transcript.txt" && { echo "FAIL banks: depot 976734 asked for"; status=1; }
# (both: 23 files, the maps and the banks)
flow all normal 3 ok
n=$(find "$out/flow-all/card/haloce-vita/mcc/halo1" -type f | wc -l)
echo "  all: $n files"
[ "$n" = 23 ] || { echo "FAIL all: files"; status=1; }
flow corrupt corrupt_once 1 ok
grep -q "refused" "$out/flow-corrupt/log.txt" || { echo "FAIL corrupt: no chunk refused"; status=1; }
flow drop drop_once 1 ok
flow cancel normal 1 cancel 13
flow resume normal 1 ok "" cancel
grep -q "resumed" "$out/flow-resume/log.txt" || { echo "FAIL resume: nothing resumed"; status=1; }
flow logon-refused logon_refuse 1 fail
flow qr-refused qr_refuse 1 fail
flow not-owned not_owned 1 fail
flow kicked kick 1 fail
flow badmulti badmulti 1 ok
flow slow slow 1 ok
grep -q "cm: heartbeat" "$out/flow-slow/transcript.txt" || { echo "FAIL slow: no heartbeat during the download"; status=1; }

echo "--- the Steam Workshop (the list, the chosen items into workshop/<id>, installed into mods/<id> after the session)"
sub=workshop
# (01 a campaign, 02 multiplayer: downloaded and installed; 03 a name leaving the folder, 04 two names one file,
# 05 another game's, 06 banned, 07 a map not version 13: refused; 08 downloaded, refused by the map check)
flow ws-items normal 2900000001,2900000002,2900000003,2900000004,2900000005,2900000006,2900000007,2900000008 ok
w=$out/flow-ws-items
grep -q "items=3 failed=5 installed=2 refused=1" "$w/line" || { echo "FAIL ws-items: counts"; status=1; }
for f in maps/a10.map maps/a30.map CampaignInfo.json campaign/a10.json assets/a10.png ModInfo.json; do
	[ -s "$w/card/haloce-vita/mods/2900000001/$f" ] || { echo "FAIL ws-items: mods/2900000001/$f missing"; status=1; }
done
[ -s "$w/card/haloce-vita/mods/2900000002/maps/fakegulch.map" ] || { echo "FAIL ws-items: 2900000002 missing"; status=1; }
n=$(find "$w/card/haloce-vita/mods" -type f | wc -l)
echo "  ws-items: $n files in mods"
[ "$n" = 8 ] || { echo "FAIL ws-items: $n files in mods (8 expected)"; status=1; }
find "$w/card" -name 'setup.exe' -o -name 'link.map' -o -name '*.part*' -o -name '*.journal' -o -name '.json' -o -name '*Screenshot*' | grep . && \
	{ echo "FAIL ws-items: a file that must not be written"; status=1; }
[ -z "$(ls -A "$w/card/haloce-vita/workshop" 2>/dev/null)" ] || \
	{ echo "FAIL ws-items: workshop staging not empty: $(ls "$w/card/haloce-vita/workshop")"; status=1; }
for id in 2900000003 2900000004 2900000005 2900000006 2900000007 2900000008; do
	[ ! -e "$w/card/haloce-vita/mods/$id" ] || { echo "FAIL ws-items: $id installed"; status=1; }
done
grep -q "workshop install 2900000008: refused: maps/badcheck.map refused" "$w/log.txt" || \
	{ echo "FAIL ws-items: the map check's refusal"; status=1; }
grep -q "cm: workshop query page 1 sort 3 search ''" "$w/transcript.txt" && \
	grep -q "cm: workshop query page 2" "$w/transcript.txt" && \
	grep -q "cm: workshop query page 1 sort 12 search 'rebalanced'" "$w/transcript.txt" && \
	grep -q "workshop list: search: 1 (Fake Rebalanced Campaign" "$w/log.txt" || \
	{ echo "FAIL ws-items: the list's pages"; status=1; }
grep -q "workshop list: page 1: 10 of 11" "$w/log.txt" || { echo "FAIL ws-items: page 1"; status=1; }
grep -q "cm: depot key 976730" "$w/transcript.txt" || { echo "FAIL ws-items: no Workshop depot key"; status=1; }
grep -q "cm: call PublishedFile.GetDetails#1 (signed in)" "$w/transcript.txt" || { echo "FAIL ws-items: no details"; status=1; }
grep -E "cm: depot key (976733|976734)" "$w/transcript.txt" && { echo "FAIL ws-items: a game depot asked for"; status=1; }
grep -q "cm: call PICS\|emsg 8903" "$w/transcript.txt" && { echo "FAIL ws-items: PICS asked"; status=1; }
flow ws-names normal 2900000009,2900000010,2900000011 ok
grep -q "items=1 failed=2 installed=0 refused=1" "$out/flow-ws-names/line" || { echo "FAIL ws-names: counts"; status=1; }
grep -q "refused: the item has no maps" "$out/flow-ws-names/log.txt" || { echo "FAIL ws-names: no-maps refusal"; status=1; }
flow ws-corrupt corrupt_once 2900000001 ok
grep -q "items=1 failed=0 installed=1" "$out/flow-ws-corrupt/line" && grep -q "refused" "$out/flow-ws-corrupt/log.txt" || \
	{ echo "FAIL ws-corrupt"; status=1; }
flow ws-cancel normal 2900000001 cancel 3
flow ws-resume normal 2900000001 ok "" ws-cancel
grep -q "resumed\|already there" "$out/flow-ws-resume/log.txt" && grep -q "installed=1" "$out/flow-ws-resume/line" || \
	{ echo "FAIL ws-resume: nothing resumed or not installed"; status=1; }
flow ws-update normal 2900000001 ok "" ws-resume
grep -q "installed=1" "$out/flow-ws-update/line" && [ -s "$out/flow-ws-update/card/haloce-vita/mods/2900000001/maps/a10.map" ] || \
	{ echo "FAIL ws-update: the item not installed again"; status=1; }
flow ws-not-owned ws_not_owned 2900000001 ok
grep -q "items=0 failed=1" "$out/flow-ws-not-owned/line" || { echo "FAIL ws-not-owned"; status=1; }
flow ws-wrong-manifest ws_wrong_manifest 2900000001 ok
grep -q "items=0 failed=1" "$out/flow-ws-wrong-manifest/line" && \
	grep -q "not the one asked for" "$out/flow-ws-wrong-manifest/log.txt" || { echo "FAIL ws-wrong-manifest"; status=1; }
flow ws-hostile-list ws_hostile_list 2900000001 ok
grep -q "items=0 failed=0" "$out/flow-ws-hostile-list/line" && \
	grep -q "malformed" "$out/flow-ws-hostile-list/log.txt" || { echo "FAIL ws-hostile-list"; status=1; }
sub=

echo "--- the job (steam_job.c: the worker thread, the QR, the snapshot, a lost session started again)"
sub=job
flow job-ce normal 1 ok
grep -q "qr=2[59] serials=2 attempts=1" "$out/flow-job-ce/line" || { echo "FAIL job-ce: QR or attempts"; status=1; }
flow job-cancel slow 1 cancel 3
flow job-lost losesession 1 ok
grep -q "attempts=2" "$out/flow-job-lost/line" || { echo "FAIL job-lost: not signed in again"; status=1; }
grep -q "resumed\|already there" "$out/flow-job-lost/log.txt" || { echo "FAIL job-lost: nothing kept"; status=1; }
flow job-outage outage 1 ok
grep -q "waits=[1-9]" "$out/flow-job-outage/line" || { echo "FAIL job-outage: no wait for the network"; status=1; }
sub=

echo "--- TLS (the test's own authority built into a test build; never in a game build)"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 2 -subj "/CN=Steam Download Test CA" \
	-addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign" \
	-keyout "$out/ca_key.pem" -out "$out/ca.pem" 2>/dev/null
for name in localhost other.test; do
	openssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -subj "/CN=$name" \
		-keyout "$out/$name.key" -out "$out/$name.csr" 2>/dev/null
	printf 'subjectAltName=DNS:%s\nbasicConstraints=CA:FALSE\n' "$name" > "$out/$name.ext"
	openssl x509 -req -in "$out/$name.csr" -CA "$out/ca.pem" -CAkey "$out/ca_key.pem" -CAcreateserial -days 2 \
		-extfile "$out/$name.ext" -out "$out/$name.pem" 2>/dev/null
done
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 2 -subj "/CN=localhost" \
	-addext "subjectAltName=DNS:localhost" -keyout "$out/self.key" -out "$out/self.pem" 2>/dev/null
python3 -I - "$out/ca.pem" "$out/test_roots.h" <<'PY'
import sys
lines = open(sys.argv[1]).read().strip().splitlines()
with open(sys.argv[2], "w") as out:
    out.write("static const char posix_https_test_roots_pem[] =\n")
    for line in lines:
        out.write('\t"%s\\n"\n' % line)
    out.write(";\n")
PY
printf '#define POSIX_HTTPS_TEST_ROOTS "%s"\n' "$out/test_roots.h" > "$out/test_roots_define.h"
$cc $flags -include "$out/test_roots_define.h" "$here/steam_download_test.c" $steam "$root/port/steamdl/steam_net.c" \
	"$root/port/linux/src/posix_https.c" "$root/port/linux/src/posix_net.c" "$out"/obj/*.o -lpthread \
	-o "$out/steam_download_test_tls"
client=$out/steam_download_test_tls client_env="env STEAM_TEST_TLS=1"
server_tls="--cert $out/localhost.pem --key $out/localhost.key"
flow tls normal 1 ok
server_tls="--cert $out/self.pem --key $out/self.key"
flow tls-self-signed normal 1 fail
grep -q "cm: connected" "$out/flow-tls-self-signed/transcript.txt" && { echo "FAIL tls-self-signed: it went on"; status=1; }
server_tls="--cert $out/other.test.pem --key $out/other.test.key"
flow tls-wrong-name normal 1 fail
grep -q "cm: connected" "$out/flow-tls-wrong-name/transcript.txt" && { echo "FAIL tls-wrong-name: it went on"; status=1; }
client= client_env= server_tls=

depotcache=${STEAM_DEPOTCACHE:-$HOME/.local/share/Steam/depotcache}
if [ -d "$depotcache" ]; then
	echo "--- manifests ($depotcache, read only)"
	for manifest in "$depotcache"/976733_*.manifest "$depotcache"/976734_*.manifest; do
		[ -f "$manifest" ] || continue
		name=$(basename "$manifest")
		"$out/steam_manifest_dump" "$manifest" > "$out/$name.c.txt" 2> "$out/$name.meta"
		python3 -I "$root/tools/steam_manifest_probe.py" "$manifest" > "$out/$name.py.txt"
		if cut -f1-5 "$out/$name.c.txt" | cmp -s - "$out/$name.py.txt"; then
			echo "  $name: the same as the probe ($(($(wc -l < "$out/$name.py.txt") - 1)) files; $(cat "$out/$name.meta");" \
				"$(grep -c namehash-ok "$out/$name.c.txt") names' SHA-1 match, $(grep -c '	gap' "$out/$name.c.txt") gaps)"
		else
			echo "FAIL $name differs from the probe"; status=1
		fi
		"$out/steam_manifest_dump" -p "$manifest" | tail -1 | sed 's/^/  /'
	done
	# (the Workshop's own depot: every item manifest this PC keeps, through the name rule; none may crash, the
	# verdicts are a report - an item with a REFUSE is refused whole)
	n=0 taken=0 refused=0
	for manifest in "$depotcache"/976730_*.manifest; do
		[ -f "$manifest" ] || continue
		n=$((n + 1))
		line=$("$out/steam_manifest_dump" -w "$manifest" > "$out/ws_$(basename "$manifest").txt" 2>&1; echo $?)
		[ "$line" = 0 ] || [ "$line" = 1 ] || { echo "FAIL workshop manifest $(basename "$manifest"): exit $line"; status=1; }
		grep -q "^WORKSHOP .* 0 refused" "$out/ws_$(basename "$manifest").txt" && taken=$((taken + 1)) || refused=$((refused + 1))
	done
	[ "$n" = 0 ] || echo "  Workshop item manifests (depot 976730): $n, $taken with no name refused, $refused refused or unreadable"
fi

seconds=${STEAM_FUZZ_SECONDS:-0}
if [ "$seconds" != 0 ]; then
	echo "--- fuzz ($seconds s each, libFuzzer)"
	found=${STEAM_FUZZ_OUT:-$out/fuzz}
	mkdir -p "$found/obj"
	ls "$mbed"/library/*.c | xargs -P "$(nproc)" -I{} sh -c \
		'clang -O1 -w -I"$1/include" -I"$1/library" -I"$3" "$4" -c "{}" -o "$2/mbed_$(basename "{}" .c).o"' \
		"$cc" "$mbed" "$found/obj" "$root/port/linux/src" "$config"
	fsan="-fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all"
	for source in $vendored; do
		clang -O1 -g -w -fsanitize=fuzzer-no-link,address,undefined $include -c "$source" \
			-o "$found/obj/v_$(basename "$source" .c).o"
	done
	clang -g -O1 $fsan -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 $include $config "$here/steam_fuzz.c" $steam \
		"$found"/obj/*.o -lpthread -o "$found/steam_fuzz"
	for target in protobuf manifest manifest_zip kv ws_frames ws_upgrade cm http chunk journal cdn_list ws_list ws_names; do
		mkdir -p "$found/$target"
		seeds=$out/fixtures/seeds/$target
		[ -d "$seeds" ] || seeds=
		STEAM_FUZZ_TARGET=$target "$found/steam_fuzz" -max_total_time="$seconds" -max_len=200000 -timeout=20 \
			-rss_limit_mb=2048 -print_final_stats=1 -artifact_prefix="$found/$target/" "$found/$target" $seeds \
			> "$found/$target.log" 2>&1 &
	done
	wait
	for target in protobuf manifest manifest_zip kv ws_frames ws_upgrade cm http chunk journal cdn_list ws_list ws_names; do
		runs=$(grep -E 'stat::number_of_executed_units' "$found/$target.log" | awk '{print $2}')
		cov=$(grep -E '^#[0-9]+.*cov:' "$found/$target.log" | tail -1 | sed 's/.*cov: \([0-9]*\).*/\1/')
		bad=$(ls "$found/$target" | grep -c -E '^(crash|leak|timeout|oom)-' || true)
		echo "  $target: ${runs:-?} runs, coverage $cov, findings $bad"
		[ "$bad" = 0 ] && [ -n "$runs" ] || status=1
	done
fi

[ $status = 0 ] && echo "PASS steam download" || echo "FAIL steam download"
exit $status
