#!/bin/sh
# (1.1.3 8v audit) The game's text parser (source/text/draw_string.c parse_unicode_string) on strings ending
# in '|', its formatting mark: draw_string_parse_test.c, the game's file included whole, built with the game's
# own flags (build.ninja's, as run_net_fuzz_test.sh takes them) and AddressSanitizer; the rest of the game is
# never called, its symbols left unresolved. Needs a configured Linux build (build.ninja).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/draw_string_parse_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
cd "$root"
# (linux_cc's value, its "$" line continuations joined)
clang=${CLANG:-$(awk '/^linux_cc =/ { line = 1 } line { printf "%s ", $0; if ($0 !~ /\$$/) exit }' build.ninja |
	sed 's/^linux_cc = //; s/\$ */ /g; s/^ *//; s/ *$//')}
[ -n "$clang" ] || clang=clang
game_flags=$(awk '/^build build\/linux\/obj\/source\/text\/draw_string.o:/ { found = 1 }
	found && /^  cflags =/ { line = 1 } line { printf "%s ", $0; if ($0 !~ /\$$/) exit }' build.ninja |
	sed 's/^ *cflags = //; s/\$ */ /g; s/-O2/-O1/; s/-march=native//')
[ -n "$game_flags" ] || { echo "FAIL no Linux build configured in $root (build.ninja)"; exit 1; }
status=0
eval "\"$clang\" $game_flags -fsanitize=address -fno-omit-frame-pointer -no-pie -fno-pic \
	\"$here/draw_string_parse_test.c\" -o \"$out/draw_string_parse_test\" -Wl,--unresolved-symbols=ignore-all" || status=1
[ $status = 0 ] && { "$out/draw_string_parse_test" || status=1; }
test_out_done $status
exit $status
