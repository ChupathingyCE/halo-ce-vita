#!/bin/sh
# Builds and runs the desktop test of the settings panel's multiplayer page
# (port/vita/host/vita_settings.c; vita_settings_test.c), with the port's
# translations (port/linux/src/lang.c, port/vita/app0/lang), and again as a
# release build (the Update channel's Stable offered), then how the panel is
# drawn (vita_panel_draw_test.c, vita_gxm.c's menu_build; HALO_TEST_SHOTS=FOLDER:
# its pictures there). Only the SDK's psp2 headers are used, for the types
# (the C library is the host's).
#   VITASDK   the SDK (default ~/vitasdk)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
sdk=${VITASDK:-$HOME/vitasdk}
out=${TMPDIR:-/tmp}/vita_settings_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out/include"
ln -s "$sdk/arm-vita-eabi/include/psp2" "$out/include/psp2"
ln -s "$sdk/arm-vita-eabi/include/psp2common" "$out/include/psp2common"
ln -s "$sdk/arm-vita-eabi/include/vitasdk" "$out/include/vitasdk" 2>/dev/null || true
cc=${CC:-gcc}
build() { # OUTPUT [FLAGS...]
	output=$1
	shift
	$cc -m32 -g -O1 -Wall -Wno-unused-function -D_GNU_SOURCE -I"$out/include" -I"$root/port/linux/src" \
		-I"$root/port/vita/include" "$@" "$here/vita_settings_test.c" "$root/port/vita/host/vita_controls.c" \
		"$root/port/linux/src/lang.c" "$root/port/linux/src/update_check.c" \
		"$root/port/linux/src/crossplay_guard.c" "$root/port/linux/src/mcc_paths.c" "$root/port/vita/host/vita_folder.c" \
		-lpthread -o "$output"
}
build "$out/vita_settings_test"
# (the language files, where the Vita has them: app0:lang)
ln -s "$root/port/vita/app0/lang" "$out/app0:lang"
status=0
(cd "$out" && ./vita_settings_test) || status=$?
# (again as a release build, whose Update channel offers Stable: in a
# folder of its own)
mkdir -p "$out/release"
build "$out/release/vita_settings_test" '-DHALO_VITA_VERSION="1.1.0"'
ln -s "$root/port/vita/app0/lang" "$out/release/app0:lang"
echo "--- as a release build (1.1.0)"
(cd "$out/release" && ./vita_settings_test) || status=$?
# (again as a Steam Workshop build, configure.py --steam-workshop - which implies the Steam download -: Modded
# maps' Download from Steam and Steam Workshop rows)
mkdir -p "$out/workshop"
build "$out/workshop/vita_settings_test" -DHALO_STEAM_DOWNLOAD=1 -DHALO_STEAM_WORKSHOP=1
ln -s "$root/port/vita/app0/lang" "$out/workshop/app0:lang"
echo "--- as a Steam Workshop build"
(cd "$out/workshop" && ./vita_settings_test) || status=$?
# (how the panel is drawn, vita_panel_draw_test.c: the shipped Menu style row's panel text - the Custom Edition
# files missing, Game > Menus, the ElDewrito and Classic looks, English and Spanish; Classic+ with the files there -
# drawn by vita_gxm.c's menu_build into pictures and checked; HALO_TEST_SHOTS=FOLDER: the pictures copied there. That
# build first checks the shipped row's classic panel: every page, line and value fits, its help 70 characters)
mkdir -p "$out/panel/text" "$out/panel/gen"
build "$out/panel/vita_settings_test" -DVITA_SETTINGS_TEST_SHIPPED_STYLE -DHALO_STEAM_DOWNLOAD=1 -DHALO_STEAM_WORKSHOP=1
ln -s "$root/port/vita/app0/lang" "$out/panel/app0:lang"
echo "--- the panel drawn (vita_panel_draw_test)"
(cd "$out/panel" && VITA_SETTINGS_PANEL_TEXT="$out/panel/text" ./vita_settings_test) || status=$?
echo '#define VSHC_GENERATOR_ID 0ULL' > "$out/panel/gen/vita_shader_generator_id.h"
# (vita_gxm.c whole, for menu_build: what it calls of GXM and the Vita's kernel left out with the sections
# nothing reaches)
$cc -m32 -g -O1 -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-deprecated-declarations \
	-D_GNU_SOURCE \
	-ffunction-sections -fdata-sections -I"$out/include" -I"$out/panel/gen" -I"$root/port/linux/src" \
	-I"$root/port/vita/include" -I"$root/port/third_party/tlsf" "$here/vita_panel_draw_test.c" \
	"$root/port/vita/host/vita_overlay_font.c" "$root/port/vita/host/vita_controls.c" "$root/port/linux/src/lang.c" \
	-Wl,--gc-sections -lm \
	-o "$out/panel/vita_panel_draw_test"
(cd "$out/panel" && ./vita_panel_draw_test "$out/panel/text" "$root/port/vita/app0/menus/halo3") || status=$?
# (the system keyboard's hold on the touches, vita_ime_test.c: vita_ime.c whole, 1.1.3-insider.8v)
echo "--- the keyboard's hold on the touches (vita_ime_test)"
$cc -m32 -g -O1 -Wall -D_GNU_SOURCE -I"$out/include" -I"$root/port/vita/include" "$here/vita_ime_test.c" \
	-o "$out/vita_ime_test" && "$out/vita_ime_test" || status=1
if [ -n "${HALO_TEST_SHOTS:-}" ]; then
	mkdir -p "$HALO_TEST_SHOTS"
	cp "$out/panel/text/"*.png "$HALO_TEST_SHOTS/" 2>/dev/null || true
fi
test_out_done $status
exit $status
