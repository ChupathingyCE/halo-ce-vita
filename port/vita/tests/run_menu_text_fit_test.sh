#!/bin/sh
# The menus' text fit (port/linux/src/menu_text_fit.c, ChupathingyCE's;
# menu_text_fit_test.c): the port's strings fitted to the game's text boxes,
# a word wider than a box cut with "...". Built twice - with the C library's
# wchar_t and with the game's 16-bit one (-fshort-wchar) -, with ASan and
# UBSan. Needs a C compiler (CC, default cc) with the sanitizers.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/menu_text_fit_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
cc=${CC:-cc}
status=0
for wide in "" -fshort-wchar; do
	$cc -g -O1 -std=c11 -Wall -Wextra -Werror $wide -fsanitize=address,undefined -fno-sanitize-recover=undefined \
		-o "$out/menu_text_fit_test" "$here/menu_text_fit_test.c" "$root/port/linux/src/menu_text_fit.c"
	echo "--- menu text fit ${wide:-(the C library's wchar_t)}"
	"$out/menu_text_fit_test" || status=1
done
test_out_done $status
exit $status
