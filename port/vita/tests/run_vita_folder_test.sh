#!/bin/sh
# The Vita's game folder choice (port/vita/host/vita_folder.c, issue #53:
# ux0:, an SD2Vita's uma0: or imc0:, chosen at start): the desktop test
# (vita_folder_test.c), with ASan and UBSan, in a folder of its own whose
# ux0:, uma0: and imc0: are plain folders.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/vita_folder_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out/run"
cc=${CC:-gcc}
$cc -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all -D_GNU_SOURCE \
	-I"$root/port/vita/include" "$here/vita_folder_test.c" "$root/port/vita/host/vita_folder.c" -o "$out/vita_folder_test"
status=0
(cd "$out/run" && ../vita_folder_test) || status=$?
test_out_done $status
exit $status
