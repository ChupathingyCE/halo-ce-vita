#!/bin/sh
# The Steam download's LIVE test binary for the PC (docs/steam_download_live_test.md): builds
# port/vita/tests/steam_download_test.c with port/steamdl against the real posix_https.c (ISRG roots), 64-bit, with
# AddressSanitizer and UBSan, into $OUT (default ~/halo-test-out/steam11x-runs/live), and prints how to run its
# "live" mode. It RUNS NOTHING against Steam: the owner starts the binary himself and scans the QR with his own
# Steam app. No agent runs the printed command (docs/steam_download_security.md).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${OUT:-$HOME/halo-test-out/steam11x-runs/live}
cc=${CC:-clang}
mkdir -p "$out/obj"
mbed=$root/port/third_party/mbedtls
config='-DMBEDTLS_USER_CONFIG_FILE="mbedtls_linux_config.h"'
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
include="-I$root/port/steamdl -I$mbed/include -I$root/port/linux/src -I$root/port/third_party/libdeflate
	-I$root/port/third_party/lzma -I$root/port/third_party/zstd -I$root/port/third_party/qrcodegen"
vendored="$root/port/third_party/lzma/LzmaDec.c $root/port/third_party/zstd/zstddeclib.c
	$root/port/third_party/libdeflate/lib/deflate_decompress.c $root/port/third_party/libdeflate/lib/utils.c
	$root/port/third_party/libdeflate/lib/x86/cpu_features.c $root/port/third_party/qrcodegen/qrcodegen.c"
echo "--- build ($cc, 64-bit, ASan+UBSan) into $out"
ls "$mbed"/library/*.c | xargs -P "$(nproc)" -I{} sh -c \
	'$0 -O1 -w -I"$1/include" -I"$1/library" -I"$3" "$4" -c "{}" -o "$2/mbed_$(basename "{}" .c).o"' \
	"$cc" "$mbed" "$out/obj" "$root/port/linux/src" "$config"
for source in $vendored; do
	$cc -O1 -g -w $sanitize $include -c "$source" -o "$out/obj/v_$(basename "$source" .c).o" &
done
wait
$cc -g -O1 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 $sanitize $include $config \
	"$here/steam_download_test.c" "$root"/port/steamdl/*.c "$root/port/linux/src/posix_https.c" \
	"$root/port/linux/src/posix_net.c" "$out"/obj/*.o -lpthread -o "$out/steam_download_test"
# (a game build's roots alone: no test authority, no test server - the binary must not know the test's names)
if grep -aq "HALO_STEAM_TEST_CM\|posix_https_test_roots_pem" "$out/steam_download_test"; then
	echo "FAIL: the live binary carries test hooks"; exit 1
fi
cat <<TEXT

Built: $out/steam_download_test  (nothing was run against Steam)

The owner, and only the owner, runs the live test, in a terminal, with his phone's Steam app at hand:

    $out/steam_download_test live $out/run1 1       # Halo CE's maps, PC files, sounds_adpcm.fsb + lists (~470 MB)
    $out/steam_download_test live $out/run1 3       # the same plus the dialogue/music/stream banks (~2 GB)

It prints a QR in the terminal (make the window tall; a dark theme reads best). Steam app > Steam Guard >
scan a QR code > approve "$(grep -o 'STEAM_DEVICE_NAME "[^"]*"' "$root/port/steamdl/steam_session.h" | cut -d'"' -f2)".
Enter cancels cleanly (revoke + log off). At the end check Steam app > Steam Guard > Authorized devices: the
device must be gone. The log (no secrets) is $out/run1/live-log.txt; the files $out/run1/haloce-vita/mcc/halo1/.
TEXT
