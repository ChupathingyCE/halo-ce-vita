#!/bin/sh
# Mixed play's built-in tables (port/linux/game/mixed_coop_tables.c; mixed_coop_tables_test.c), with ASan and UBSan.
# Needs a C compiler (CC, default cc) with the sanitizers.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/mixed_coop_tables_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
cc=${CC:-cc}
z=$root/port/third_party/zlib
status=0
$cc -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -D_GNU_SOURCE -DZ_PREFIX \
	-I"$z" -o "$out/mixed_coop_tables_test" "$here/mixed_coop_tables_test.c" "$root/port/linux/game/mixed_coop_tables.c" \
	"$root/port/linux/game/mixed_coop_protocol.c" "$z/uncompr.c" "$z/inflate.c" "$z/inftrees.c" "$z/inffast.c" \
	"$z/adler32.c" "$z/crc32.c" "$z/zutil.c" 2>"$out/cc.log" || { cat "$out/cc.log"; exit 1; }
"$out/mixed_coop_tables_test" || status=1
test_out_done $status
exit $status
