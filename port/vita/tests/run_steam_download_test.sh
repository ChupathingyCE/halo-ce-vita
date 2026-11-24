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
#   browse     the Workshop browser's job (pages, tabs, SUBSCRIBED, authors, pictures - the fake's hostile ones -,
#              a details page) against the same fake, its image server on the CDN's port
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
	$root/port/steamdl/steam_download.c $root/port/steamdl/steam_job.c $root/port/steamdl/steam_workshop.c
	$root/port/steamdl/steam_image.c $root/port/steamdl/steam_signin.c"
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
# (-fwrapv as the builds compile port/steamdl: stb_image's IDCT wraps on hostile coefficients, its pixels clamped after)
flags="$bits -g -O1 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -fwrapv $sanitize $include $config"
$cc $flags "$here/steam_download_test.c" $steam "$root/port/steamdl/steam_net.c" "$root/port/linux/src/posix_https.c" \
	"$root/port/linux/src/posix_net.c" "$out"/obj/*.o -lpthread -o "$out/steam_download_test"
$cc $flags "$here/steam_manifest_dump.c" $steam "$root/port/steamdl/steam_net.c" "$root/port/linux/src/posix_https.c" \
	"$root/port/linux/src/posix_net.c" "$out"/obj/*.o -lpthread -o "$out/steam_manifest_dump"

echo "--- units"
python3 -I "$here/steam_fake_server.py" --fixtures "$out/fixtures" --seed 3
"$out/steam_download_test" units "$out/fixtures" || status=1

echo "--- pictures (steam_image_cases/: the ws_image fuzzer's past findings, under the sanitizers)"
"$out/steam_download_test" pictures "$here"/steam_image_cases/* || { echo "FAIL pictures"; status=1; }

echo "--- flows (a fake CM and CDN on 127.0.0.1)"
# flow <name> <mode> <games> <expect: ok|fail|cancel> [cancel after] [keep: reuse the last run's card]
last=
flow() {
	name=$1 mode=$2 games=$3 expect=$4 cancel=$5 keep=$6
	work=$out/flow-$name
	if [ -n "$keep" ]; then cp -a "$out/flow-$keep" "$work"; rm -f "$work/transcript.txt" "$work/log.txt" "$work/server.json"
	else mkdir -p "$work"; fi
	fifo=$work/stdin
	rm -f "$fifo"
	mkfifo "$fifo"
	python3 -I "$here/steam_fake_server.py" --work "$work" --mode "$mode" $server_tls ${server_extra:-} < "$fifo" > "$work/server.json" &
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
	# signed out: the token revoked (and, once signed in, logged off after it) - the kept sign-in's flows check their own
	if [ -n "${signin_flow:-}" ]; then
		:
	elif [ "$mode" = losesession ]; then
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
# (1.1.3: the verified files' record - the first run recorded every file it checked or wrote; the second takes them
# from it without reading them: no "checking", every file "verified before, unchanged")
grep -q "already=20 recorded=20" "$out/flow-ce-again/line" &&
	[ "$(grep -c "already there (verified before, unchanged)" "$out/flow-ce-again/log.txt")" = 20 ] ||
	{ echo "FAIL ce-again: not taken from the record"; status=1; }
[ -s "$out/flow-ce/card/haloce-vita/steam-verified.txt" ] || { echo "FAIL ce: no verified files' record"; status=1; }
# (a file changed on the card after it was recorded - same size, another time -: checked whole, found wrong, fetched
# again; the others still from the record)
cp -a "$out/flow-ce-again" "$out/flow-cemod"
changed=$(find "$out/flow-cemod/card/haloce-vita/mcc/halo1" -type f -name '*.map' | sort | head -1)
python3 -I -c 'import sys; f=open(sys.argv[1],"r+b"); f.seek(100); b=f.read(1); f.seek(100); f.write(bytes([b[0]^0xFF]))' "$changed"
touch -d '2001-01-01 00:00:00' "$changed"
flow cemod-again normal 1 ok "" cemod
grep -q "files=1 already=19 recorded=19" "$out/flow-cemod-again/line" &&
	diff -r "$out/flow-ce/card/haloce-vita/mcc" "$out/flow-cemod-again/card/haloce-vita/mcc" > /dev/null ||
	{ echo "FAIL cemod-again: the changed file not fetched again, or files differ"; status=1; }
# (a record that does not read: dropped, every file checked whole, a new one written)
cp -a "$out/flow-ce-again" "$out/flow-cebadrec"
printf 'steamdl-verified 1\nnot a record line\n' > "$out/flow-cebadrec/card/haloce-vita/steam-verified.txt"
flow cebadrec-again normal 1 ok "" cebadrec
grep -q "already=20 recorded=0" "$out/flow-cebadrec-again/line" &&
	grep -q "record does not read: dropped" "$out/flow-cebadrec-again/log.txt" &&
	grep -q "^steamdl-verified 1" "$out/flow-cebadrec-again/card/haloce-vita/steam-verified.txt" &&
	[ "$(wc -l < "$out/flow-cebadrec-again/card/haloce-vita/steam-verified.txt")" = 21 ] ||
	{ echo "FAIL cebadrec-again: a bad record not dropped and made again"; status=1; }
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
# (1.1.3, the owner: "If the download fails it needs to cache it." A server error mid-download - every chunk 404 after
# the fourth - ends it failed, not cancelled: what it got stays (the .part files and their journals), the next run
# (a new process, as after the game is closed and started again) resumes from it and finishes; a journalled chunk
# damaged on the card meanwhile is found by its hash and fetched again)
flow gone chunk_gone 1 fail
w=$out/flow-gone
parts=$(find "$w/card" -name '*.part' | wc -l)
kept=$(find "$w/card" \( -name '*.part' -o -name '*.part.journal' \) -printf '%s\n' | awk '{ s += $1 } END { print s + 0 }')
journalled=$(find "$w/card" -name '*.part.journal' -exec sh -c 'tail -n +6 "$1"' sh {} \; | wc -l)
finished=$(find "$w/card/haloce-vita/mcc/halo1" -type f ! -name '*.part' ! -name '*.part.journal' | wc -l)
echo "  gone: $finished files finished, $parts partial with $journalled chunks journalled, $kept bytes kept"
[ "$parts" -ge 1 ] && [ "$journalled" -ge 1 ] || { echo "FAIL gone: nothing of the failed download kept"; status=1; }
grep -q "a chunk could not be downloaded" "$w/line" || { echo "FAIL gone: not the server's failure"; status=1; }
flow gone-resume normal 1 ok "" gone
w=$out/flow-gone-resume
grep -q "resumed (" "$w/log.txt" || { echo "FAIL gone-resume: nothing resumed"; status=1; }
left=$(find "$w/card" -name '*.part*' | wc -l)
n=$(find "$w/card/haloce-vita/mcc/halo1" -type f | wc -l)
echo "  gone-resume: $n files, $left partial"
[ "$n" = 20 ] && [ "$left" = 0 ] || { echo "FAIL gone-resume: $n files, $left partial left"; status=1; }
# (every file the same bytes as a download that never failed)
diff -r "$out/flow-ce/card/haloce-vita/mcc" "$w/card/haloce-vita/mcc" > /dev/null || { echo "FAIL gone-resume: files differ"; status=1; }
# (the same failed card, a journalled chunk damaged on it meanwhile: the resume finds it by its hash and fetches it again)
cp -a "$out/flow-gone" "$out/flow-gonedmg"
w=$out/flow-gonedmg
damaged=
for j in $(find "$w/card" -name '*.part.journal' | sort); do
	[ "$(tail -n +6 "$j" | wc -l)" -ge 1 ] || continue
	part=${j%.journal}
	index=$(tail -n +6 "$j" | head -1 | cut -d' ' -f1)
	damaged=$part
	python3 -I - "$part" "$index" "$j" "$here/steam_fake_server.py" <<'PY'
import sys
part, index = sys.argv[1], int(sys.argv[2])
# (the chunk's offset: the fake cuts every file in chunks of its CHUNK bytes, the last one shorter)
expression = next(l.split("=")[1].split("#")[0] for l in open(sys.argv[4]) if l.startswith("CHUNK ="))
step = 1
for factor in expression.split("*"):
    step *= int(factor)
with open(part, "r+b") as f:
    f.seek(index * step)
    data = bytearray(f.read(64))
    for i in range(len(data)):
        data[i] ^= 0xA5
    f.seek(index * step)
    f.write(data)
PY
	break
done
[ -n "$damaged" ] || { echo "FAIL gone: no journalled chunk to damage"; status=1; }
flow gonedmg-resume normal 1 ok "" gonedmg
w=$out/flow-gonedmg-resume
name=$(basename "${damaged%.part}")
had=$(tail -n +6 "$damaged.journal" | wc -l)
took=$(sed -n "s|.*$name: resumed (\([0-9]*\) of.*|\1|p" "$w/log.txt" | head -1)
left=$(find "$w/card" -name '*.part*' | wc -l)
echo "  gonedmg-resume: $name: $had chunks journalled, ${took:-0} taken from the card, $left partial left"
[ "${took:-0}" -lt "$had" ] || { echo "FAIL gonedmg-resume: the damaged chunk was taken"; status=1; }
[ "$left" = 0 ] && diff -r "$out/flow-ce/card/haloce-vita/mcc" "$w/card/haloce-vita/mcc" > /dev/null || \
	{ echo "FAIL gonedmg-resume: files differ or partial left"; status=1; }
flow logon-refused logon_refuse 1 fail
flow qr-refused qr_refuse 1 fail
flow not-owned not_owned 1 fail
flow kicked kick 1 fail
flow badmulti badmulti 1 ok
flow slow slow 1 ok
grep -q "cm: heartbeat" "$out/flow-slow/transcript.txt" || { echo "FAIL slow: no heartbeat during the download"; status=1; }
# (the old-style message in the logon's Multi: skipped, the rest read - insider.7 dropped the whole Multi)
grep -q "a multi message held 1 old-style messages (not protobuf; the last EMsg 5510): skipped" "$out/flow-ce/log.txt" &&
	! grep -q "malformed multi" "$out/flow-ce/log.txt" || { echo "FAIL ce: the logon's Multi"; status=1; }

echo "--- the kept sign-in (steam_signin.h: sealed to the device, 14 days, SIGN OUT)"
# (one made-up account's token across these runs: --token-file; the card's sealed file carries over with keep)
signin_flow=1 client_env="env STEAM_TEST_SIGNIN=1"
signin_check() {
	# signin_check <name> <expected revoke/logoff order> <file there: 1|0> <QR used: 1|0>
	w=$out/flow-$1
	order=$(grep -E "cm: (revoke|logoff)" "$w/transcript.txt" | tr '\n' ' ')
	[ "$order" = "$2" ] || { echo "FAIL $1: end was '$order', not '$2'"; status=1; }
	if [ "$3" = 1 ]; then [ -s "$w/card/haloce-vita/steam-signin.bin" ] || { echo "FAIL $1: no kept sign-in"; status=1; }
	else [ ! -e "$w/card/haloce-vita/steam-signin.bin" ] || { echo "FAIL $1: the kept sign-in is still there"; status=1; }; fi
	if [ "$4" = 1 ]; then grep -q "BeginAuthSessionViaQR" "$w/transcript.txt" || { echo "FAIL $1: no QR"; status=1; }
	else ! grep -q "BeginAuthSessionViaQR" "$w/transcript.txt" || { echo "FAIL $1: a QR was asked for"; status=1; }; fi
	[ ! -e "$w/steam-session.txt" ] || { echo "FAIL $1: the not-signed-out marker left"; status=1; }
	[ ! -e "$w/card/haloce-vita/steam-signin.bin.new" ] || { echo "FAIL $1: a .new left"; status=1; }
	# (no token of the account's, live or revoked, in the clear anywhere this run wrote: the card, the log)
	for t in $(cut -d' ' -f1 "$out/signin-token.txt" | sort -u); do
		if grep -r -l -a -F -e "$t" "$w/card" "$w/log.txt" 2>/dev/null | grep .; then echo "FAIL $1: a token in the clear"; status=1; fi
	done
}
server_extra="--token-file $out/signin-token.txt"
rm -f "$out/signin-token.txt"
flow keep-first normal 1 ok
grep -q "kept=1" "$out/flow-keep-first/line" || { echo "FAIL keep-first: not kept"; status=1; }
signin_check keep-first "cm: logoff " 1 1
flow keep-again normal 1 ok "" keep-first
grep -q "found=1 kept=1" "$out/flow-keep-again/line" && grep -q "already=20" "$out/flow-keep-again/line" ||
	{ echo "FAIL keep-again: the kept sign-in not used"; status=1; }
signin_check keep-again "cm: logoff " 1 0
grep -q "sign-in kept on this device is used (saved 0 days ago" "$out/flow-keep-again/log.txt" || { echo "FAIL keep-again: log"; status=1; }
client_env="env STEAM_TEST_SIGNIN=1 STEAM_TEST_SIGN_OUT=1"
flow keep-signout normal 1 ok "" keep-again
grep -q "signed_out=1" "$out/flow-keep-signout/line" || { echo "FAIL keep-signout: not confirmed"; status=1; }
signin_check keep-signout "cm: revoke ok cm: logoff " 0 0
client_env="env STEAM_TEST_SIGNIN=1"
# (revoked at Steam: refused at the logon with Revoked - wiped, then a new QR; the fake refuses that token from now on)
flow keep-revoked normal 1 ok "" keep-first
grep -q "the kept sign-in was refused (result 26, Revoked): removed from this device; a new QR" "$out/flow-keep-revoked/log.txt" ||
	{ echo "FAIL keep-revoked: log"; status=1; }
signin_check keep-revoked "cm: logoff " 1 1
# (older than 14 days: revoked before the QR, not signed in; then a new QR, kept again)
rm -f "$out/signin-token.txt"
flow keep-base normal 1 ok
token_now=$(tail -1 "$out/signin-token.txt")
mkdir -p "$out/flow-keep-old-base" && cp -r "$out/flow-keep-base/card" "$out/flow-keep-old-base/"
"$out/steam_download_test" signin-write "$out/flow-keep-old-base/card/haloce-vita/steam-signin.bin" "$token_now" fakeplayer \
	$(($(date +%s) - 15 * 86400)) || { echo "FAIL signin-write"; status=1; }
flow keep-old normal 1 ok "" keep-old-base
signin_check keep-old "cm: revoke ok cm: logoff " 1 1
# (the expired one revoked before the QR, not signed in: no logon between the connection and the revoke)
awk '/cm: revoke ok/ {print "revoke"; exit} /cm: logon ok/ {print "logon"; exit}' "$out/flow-keep-old/transcript.txt" | grep -q revoke ||
	{ echo "FAIL keep-old: not revoked before the logon"; status=1; }
grep -q "older than 14 days: removed from it, to be revoked with Steam" "$out/flow-keep-old/log.txt" || { echo "FAIL keep-old: log"; status=1; }
# (another console's file, or a damaged one: wiped, a new QR, kept again)
rm -f "$out/signin-token.txt"
mkdir -p "$out/flow-keep-bad-base/card/haloce-vita"
head -c 300 /dev/urandom > "$out/flow-keep-bad-base/card/haloce-vita/steam-signin.bin"
flow keep-bad normal 1 ok "" keep-bad-base
signin_check keep-bad "cm: logoff " 1 1
grep -q "found=3 kept=1" "$out/flow-keep-bad/line" || { echo "FAIL keep-bad: state"; status=1; }
# (Steam busy at the logon - ServiceUnavailable -: kept for the next try, neither wiped nor revoked)
flow keep-busy logon_busy 1 fail "" keep-bad
signin_check keep-busy "" 1 0
grep -q "found=1 kept=1" "$out/flow-keep-busy/line" || { echo "FAIL keep-busy: state"; status=1; }
# (SIGN OUT with nothing kept, or a file that does not open: wiped, and no connection at all)
client_env="env STEAM_TEST_SIGNIN=1 STEAM_TEST_SIGN_OUT=1"
mkdir -p "$out/flow-keep-none-base/card/haloce-vita"
flow keep-out-none normal 1 ok "" keep-none-base
mkdir -p "$out/flow-keep-out-bad-base/card/haloce-vita"
head -c 200 /dev/urandom > "$out/flow-keep-out-bad-base/card/haloce-vita/steam-signin.bin"
flow keep-out-bad normal 1 ok "" keep-out-bad-base
for n in keep-out-none keep-out-bad; do
	[ ! -e "$out/flow-$n/card/haloce-vita/steam-signin.bin" ] || { echo "FAIL $n: file left"; status=1; }
	! grep -q "cm: connected" "$out/flow-$n/transcript.txt" 2>/dev/null || { echo "FAIL $n: it connected"; status=1; }
	grep -q "no sign-in was kept on this device (nothing to revoke, no connection made)" "$out/flow-$n/log.txt" ||
		{ echo "FAIL $n: log"; status=1; }
done
signin_flow= client_env= server_extra=

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
# (staging empty; workshop/installed/ holds the versions installed - the browser's UPDATE - and nothing else)
[ -z "$(ls -A "$w/card/haloce-vita/workshop" 2>/dev/null | grep -v -x -e installed)" ] || \
	{ echo "FAIL ws-items: workshop staging not empty: $(ls "$w/card/haloce-vita/workshop")"; status=1; }
[ "$(ls "$w/card/haloce-vita/workshop/installed" 2>/dev/null | tr '\n' ' ')" = "2900000001.gid 2900000002.gid " ] || \
	{ echo "FAIL ws-items: installed versions: $(ls "$w/card/haloce-vita/workshop/installed")"; status=1; }
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
grep -q "workshop list: page 1: 8 of 19" "$w/log.txt" || { echo "FAIL ws-items: page 1"; status=1; }
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
# (1.1.3: a Workshop item that fails mid-download on a server error keeps its verified part in staging - as the
# game's depot download does - and the next run resumes it and installs it; only an item whose data failed its checks
# or that Steam refused loses it: ws-wrong-manifest below)
flow ws-gone chunk_gone 2900000001 ok
w=$out/flow-ws-gone
grep -q "items=0 failed=1" "$w/line" || { echo "FAIL ws-gone: not one failed item"; status=1; }
staged=$(find "$w/card/haloce-vita/workshop" \( -name '*.part' -o -name '*.part.journal' \) | wc -l)
kept=$(find "$w/card/haloce-vita/workshop" -type f -printf '%s\n' | awk '{ s += $1 } END { print s + 0 }')
echo "  ws-gone: $staged partial files staged, $kept bytes kept"
[ "$staged" -ge 1 ] && grep -q "kept for the next try" "$w/log.txt" || { echo "FAIL ws-gone: nothing kept"; status=1; }
[ ! -e "$w/card/haloce-vita/workshop/2900000001.done" ] || { echo "FAIL ws-gone: marked done"; status=1; }
flow ws-gone-resume normal 2900000001 ok "" ws-gone
w=$out/flow-ws-gone-resume
grep -q "resumed\|already there" "$w/log.txt" && grep -q "installed=1" "$w/line" || \
	{ echo "FAIL ws-gone-resume: nothing resumed or not installed"; status=1; }
[ -z "$(find "$w/card/haloce-vita/workshop" -path "*/workshop/installed" -prune -o -type f -print 2>/dev/null)" ] || \
	{ echo "FAIL ws-gone-resume: staging left"; status=1; }
cmp -s "$w/card/haloce-vita/mods/2900000001/maps/a10.map" "$out/flow-ws-resume/card/haloce-vita/mods/2900000001/maps/a10.map" || \
	{ echo "FAIL ws-gone-resume: the map differs"; status=1; }
grep -q "installed=1" "$out/flow-ws-update/line" && [ -s "$out/flow-ws-update/card/haloce-vita/mods/2900000001/maps/a10.map" ] || \
	{ echo "FAIL ws-update: the item not installed again"; status=1; }
flow ws-not-owned ws_not_owned 2900000001 ok
grep -q "items=0 failed=1" "$out/flow-ws-not-owned/line" || { echo "FAIL ws-not-owned"; status=1; }
flow ws-wrong-manifest ws_wrong_manifest 2900000001 ok
grep -q "items=0 failed=1" "$out/flow-ws-wrong-manifest/line" && \
	grep -q "not the one asked for" "$out/flow-ws-wrong-manifest/log.txt" || { echo "FAIL ws-wrong-manifest"; status=1; }
[ ! -e "$out/flow-ws-wrong-manifest/card/haloce-vita/workshop/2900000001" ] || \
	{ echo "FAIL ws-wrong-manifest: its staging kept"; status=1; }
flow ws-hostile-list ws_hostile_list 2900000001 ok
grep -q "items=0 failed=0" "$out/flow-ws-hostile-list/line" && \
	grep -q "malformed" "$out/flow-ws-hostile-list/log.txt" || { echo "FAIL ws-hostile-list"; status=1; }
sub=

echo "--- the job (steam_job.c: the worker thread, the QR, the snapshot, a lost session started again)"
sub=job
flow job-ce normal 1 ok
grep -q "qr=2[59] serials=2 attempts=1" "$out/flow-job-ce/line" || { echo "FAIL job-ce: QR or attempts"; status=1; }
# (a cancel cuts the CM's connection: the token revoked on a new one, no log-off on the cut one - Steam ends it)
signin_flow=1
flow job-cancel slow 1 cancel 3
signin_flow=
[ "$(grep -E "cm: (revoke|logoff)" "$out/flow-job-cancel/transcript.txt" | tr '\n' ' ')" = "cm: revoke ok " ] ||
	{ echo "FAIL job-cancel: not revoked after the cancel"; status=1; }
flow job-lost losesession 1 ok
grep -q "attempts=2" "$out/flow-job-lost/line" || { echo "FAIL job-lost: not signed in again"; status=1; }
grep -q "resumed\|already there" "$out/flow-job-lost/log.txt" || { echo "FAIL job-lost: nothing kept"; status=1; }
flow job-outage outage 1 ok
# (1.1.3: the device sleeps mid-download - the power button, a suspend: its sockets die - and wakes: the job pauses,
# then connects again by itself and finishes, nothing cancelled; the screen saw "asleep" and "carrying on")
signin_flow=1 client_env="env STEAM_TEST_SLEEP=300" flow job-sleep slow 1 ok
signin_flow= client_env=
# (the slept session's token revoked over a new connection - its own died with the "suspend" -, the next one signed in
# and ended as usual, as a lost session's)
[ "$(grep -E "cm: (revoke|logoff)" "$out/flow-job-sleep/transcript.txt" | tr '\n' ' ')" = "cm: revoke ok cm: revoke ok cm: logoff " ] ||
	{ echo "FAIL job-sleep: the sessions' end"; status=1; }
grep -q "asleep_seen=3" "$out/flow-job-sleep/line" &&
	grep -q "the device slept during the download: connecting again" "$out/flow-job-sleep/log.txt" &&
	diff -r "$out/flow-ce/card/haloce-vita/mcc" "$out/flow-job-sleep/card/haloce-vita/mcc" > /dev/null ||
	{ echo "FAIL job-sleep: not carried on after the wake"; status=1; }
grep -q "waits=[1-9]" "$out/flow-job-outage/line" || { echo "FAIL job-outage: no wait for the network"; status=1; }
# (1.1.3-insider.8, the owner's daily: a cancel while the job checked gigabytes already on the card took minutes and
# kept the PS button locked. The player's cancel at each phase, and with a CDN that stalls: the cancel call (the
# game's thread) under 100 ms, the device's hold let go at once and at the end exactly once, every snapshot call
# under 100 ms, the job ended within 5 s - insider.8's would wait out the 60 s chunk timeout, and more)
cancel_check() {
	l=$(cat "$out/flow-$1/line")
	for want in "cancelled=1" "busy_end=0" "busy_twice=0" "busy_after_cancel=0"; do
		echo "$l" | grep -q "$want" || { echo "FAIL $1: not $want"; status=1; }
	done
	call=$(echo "$l" | sed -n 's/.*cancel_call_ms=\([0-9.]*\).*/\1/p')
	view=$(echo "$l" | sed -n 's/.*view_max_ms=\([0-9.]*\).*/\1/p')
	end=$(echo "$l" | sed -n 's/.*end_ms=\([0-9.-]*\).*/\1/p')
	awk -v c="$call" -v v="$view" -v e="$end" 'BEGIN { exit !(c != "" && c >= 0 && c < 100 && v < 100 && e >= 0 && e < 5000) }' ||
		{ echo "FAIL $1: cancel call $call ms, longest snapshot $view ms, ended $end ms after the cancel"; status=1; }
	# (signed in, no sign-in kept here: the token revoked all the same, on a new connection - the cut one is gone)
	if grep -q "cm: logon ok" "$out/flow-$1/transcript.txt"; then
		grep -q "cm: revoke ok" "$out/flow-$1/transcript.txt" || { echo "FAIL $1: not revoked after the cancel"; status=1; }
	fi
	echo "    $1: $(echo "$l" | grep -o 'busy_end.*end_ms=[0-9.-]*')"
}
signin_flow=1
client_env="env STEAM_TEST_CANCEL=qr"
flow job-cancel-qr normal 1 cancel
cancel_check job-cancel-qr
client_env="env STEAM_TEST_CANCEL=preparing"
flow job-cancel-prep slow 1 cancel
cancel_check job-cancel-prep
client_env="env STEAM_TEST_CANCEL=downloading STEAM_TEST_CANCEL_MS=300"
flow job-cancel-dl slow 1 cancel
cancel_check job-cancel-dl
client_env="env STEAM_TEST_CANCEL=downloading STEAM_TEST_CANCEL_MS=1500"
flow job-cancel-stall stall_chunk 1 cancel
cancel_check job-cancel-stall
grep -q "cdn: stalled mid-chunk" "$out/flow-job-cancel-stall/transcript.txt" || { echo "FAIL job-cancel-stall: no stall"; status=1; }
client_env="env STEAM_TEST_CANCEL=preparing STEAM_TEST_CANCEL_MS=1500"
flow job-cancel-manifest stall_manifest 1 cancel
cancel_check job-cancel-manifest
grep -q "cdn: stalled before the manifest" "$out/flow-job-cancel-manifest/transcript.txt" ||
	{ echo "FAIL job-cancel-manifest: no stall"; status=1; }
client_env= signin_flow=
sub=

echo "--- the Workshop browser (steam_job.c: pages, tabs, SUBSCRIBED, authors, the cards' pictures, a details page;"
echo "    steam_image.c: the URL rule, the bounded decode, the cache)"
sub=browse
flow ws-browse normal 0 ok
w=$out/flow-ws-browse
for want in "pages=6 " "subscribed=3 " "details=1 " "maps=2 " "details_pictures=3 " "held_after=0 "; do
	grep -q "$want" "$w/line" || { echo "FAIL ws-browse: not $want"; status=1; }
done
refused=$(sed -n 's/.* refused=\([0-9]*\).*/\1/p' "$w/line")
most=$(sed -n 's/.* most_held=\([0-9]*\).*/\1/p' "$w/line")
[ "${refused:-0}" -ge 8 ] || { echo "FAIL ws-browse: $refused hostile pictures refused (8 at least)"; status=1; }
[ "${most:-0}" -le $((8 * 256 * 128 * 4 + 512 * 256 * 4)) ] || { echo "FAIL ws-browse: $most bytes of pictures held"; status=1; }
# (a picture's URL not Steam's image CDN's is never fetched; each picture fetched once - the cache after - and
# always the CDN's resized one)
grep -E "img: (2900000015|2900000001 9)" "$w/transcript.txt" && { echo "FAIL ws-browse: a picture fetched from a URL not Steam's"; status=1; }
grep "img: " "$w/transcript.txt" | grep -v "resized" | grep . && { echo "FAIL ws-browse: a picture asked for whole"; status=1; }
dup=$(grep "img: " "$w/transcript.txt" | sort | uniq -d | head -1)
[ -z "$dup" ] || { echo "FAIL ws-browse: fetched twice: $dup"; status=1; }
grep -q "cm: workshop subscribed page 1" "$w/transcript.txt" || { echo "FAIL ws-browse: SUBSCRIBED not asked"; status=1; }
grep -q "cm: subscriptions of another account" "$w/transcript.txt" && { echo "FAIL ws-browse: another account's"; status=1; }
grep -q "cm: names of" "$w/transcript.txt" || { echo "FAIL ws-browse: no authors asked"; status=1; }
grep -q "cm: workshop details 2900000001 (a page)" "$w/transcript.txt" || { echo "FAIL ws-browse: no details page"; status=1; }
grep -q 'by "Ñandú Mapper"' "$w/log.txt" || { echo "FAIL ws-browse: the Latin-1 author"; status=1; }
grep -q "browse description: Fake Rebalanced Campaign" "$w/log.txt" || { echo "FAIL ws-browse: the description"; status=1; }
grep "browse row 2900000016" "$w/log.txt" | head -1 | grep -q 'by "Evil/nName' || { echo "FAIL ws-browse: the hostile author"; status=1; }
n=$(ls "$w/card/haloce-vita/workshop/previews" | grep -c '^[0-9a-f]\{16\}\.px$')
echo "  ws-browse: $(cat "$w/line" | cut -c1-200); $n pictures in the cache"
[ "$n" -ge 10 ] && [ "$n" -le 96 ] || { echo "FAIL ws-browse: the cache holds $n"; status=1; }
[ -z "$(ls "$w/card/haloce-vita" | grep -v -x -e workshop)" ] || { echo "FAIL ws-browse: something but workshop/"; status=1; }
[ -z "$(ls "$w/card/haloce-vita/workshop" | grep -v -x -e previews)" ] || \
	{ echo "FAIL ws-browse: something in workshop/ but previews"; status=1; }
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
	# (every one's CRC as steam_manifest_crc_ok checks it: the depotcache's names are clear, so crc_clear)
	n=0 bad=0
	for manifest in "$depotcache"/97673*_*.manifest; do
		[ -f "$manifest" ] || continue
		n=$((n + 1))
		"$out/steam_manifest_dump" -c "$manifest" > "$out/crc_$(basename "$manifest").txt" 2>&1 ||
			{ bad=$((bad + 1)); echo "FAIL crc $(basename "$manifest"): $(cat "$out/crc_$(basename "$manifest").txt")"; status=1; }
	done
	[ "$n" = 0 ] || echo "  CRC of the depotcache's MCC manifests (names clear, crc_clear): $n, $bad bad"
fi

# REAL manifests from Steam's CDN, names encrypted as the CDN sends them (1.1.3-insider.7 refused every one: it checked
# crc_clear, which the CDN leaves 0). STEAM_REAL=1 first fetches fresh ones with NO account: steam_real_probe signs in
# as an anonymous Steam user and reads free depots (app 1007 depot 1006, app 232250 depot 232256) through the
# download's own code - the logon's Multis, app info, depot key, request code, CDN, manifest, one chunk decoded.
# STEAM_REAL_MANIFESTS (default ~/halo-test-out/steamfix-out/real-manifests) keeps the zips; each one's CRC is checked.
real=${STEAM_REAL_MANIFESTS:-$HOME/halo-test-out/steamfix-out/real-manifests}
if [ "${STEAM_REAL:-0}" = 1 ]; then
	echo "--- real (Steam's servers, anonymous, free depots)"
	mkdir -p "$real"
	$cc $flags -DSTEAM_SESSION_ANONYMOUS "$here/steam_real_probe.c" $steam "$root/port/steamdl/steam_net.c" \
		"$root/port/linux/src/posix_https.c" "$root/port/linux/src/posix_net.c" "$out"/obj/*.o -lpthread \
		-o "$out/steam_real_probe"
	for pair in "1007 1006" "232250 232256"; do
		# (shellcheck: word splitting wanted)
		"$out/steam_real_probe" $pair "$real" > "$out/real_$(echo $pair | tr ' ' _).txt" 2>&1 || true
		if grep -q "^REAL OK" "$out/real_$(echo $pair | tr ' ' _).txt"; then
			echo "  app/depot $pair: $(grep -E '^(manifest|chunk):' "$out/real_$(echo $pair | tr ' ' _).txt" | tr '\n' ' ')"
		else
			echo "FAIL real $pair: $(grep -E '^REAL' "$out/real_$(echo $pair | tr ' ' _).txt")"; status=1
		fi
	done
fi
if [ -d "$real" ]; then
	n=0
	for manifest in "$real"/*.zip; do
		[ -f "$manifest" ] || continue
		n=$((n + 1))
		line=$("$out/steam_manifest_dump" -c "$manifest" 2>&1) || { echo "FAIL real manifest $(basename "$manifest"): $line"; status=1; }
		case "$line" in *"names encrypted"*) ;; *) echo "FAIL real manifest $(basename "$manifest") is not the CDN's form: $line"; status=1;; esac
	done
	[ "$n" = 0 ] || echo "  real CDN manifests (names encrypted, crc_encrypted): $n checked"
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
	clang -g -O1 $fsan -fwrapv -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 $include $config "$here/steam_fuzz.c" $steam \
		"$found"/obj/*.o -lpthread -o "$found/steam_fuzz"
	for target in protobuf manifest manifest_zip kv ws_frames ws_upgrade cm multi signin http chunk journal cdn_list ws_list ws_names ws_image ws_text; do
		mkdir -p "$found/$target"
		seeds=$out/fixtures/seeds/$target
		[ -d "$seeds" ] || seeds=
		# (ws_image: its past findings too)
		[ "$target" = ws_image ] && seeds="$seeds $here/steam_image_cases"
		STEAM_FUZZ_TARGET=$target "$found/steam_fuzz" -max_total_time="$seconds" -max_len=200000 -timeout=20 \
			-rss_limit_mb=2048 -print_final_stats=1 -artifact_prefix="$found/$target/" "$found/$target" $seeds \
			> "$found/$target.log" 2>&1 &
	done
	wait
	for target in protobuf manifest manifest_zip kv ws_frames ws_upgrade cm multi signin http chunk journal cdn_list ws_list ws_names ws_image ws_text; do
		runs=$(grep -E 'stat::number_of_executed_units' "$found/$target.log" | awk '{print $2}')
		cov=$(grep -E '^#[0-9]+.*cov:' "$found/$target.log" | tail -1 | sed 's/.*cov: \([0-9]*\).*/\1/')
		bad=$(ls "$found/$target" | grep -c -E '^(crash|leak|timeout|oom)-' || true)
		echo "  $target: ${runs:-?} runs, coverage $cov, findings $bad"
		[ "$bad" = 0 ] && [ -n "$runs" ] || status=1
	done
fi

[ $status = 0 ] && echo "PASS steam download" || echo "FAIL steam download"
exit $status
