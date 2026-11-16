#!/bin/sh
# Builds and runs the desktop test of the Steam Workshop multiplayer maps' names and a joiner's verdict
# (port/linux/game/workshop_maps.c; workshop_maps_test.c), 32-bit as the game is, with AddressSanitizer and UBSan.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/workshop_maps_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
trap 'test_out_done $?' EXIT
cc=${CC:-gcc}
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
echo 'int main(void){return 0;}' | $cc -m32 $sanitize -x c - -o "$out/probe" 2>/dev/null || sanitize=
$cc -m32 -g -O1 -Wall -Wextra $sanitize "$here/workshop_maps_test.c" "$root/port/linux/game/workshop_maps.c" -o "$out/workshop_maps_test"
echo "workshop_maps_test: ${sanitize:-no sanitizers}"
"$out/workshop_maps_test"
