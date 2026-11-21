#!/usr/bin/env python3
"""The Vita's own menu files, embedded in the program in the game's place.

    python3 tools/vita_menus.py --out build/vita/menus

The game's menus are XML files embedded in it (chupathingyce/port/assets/
menus, tools/embed_assets.py). The Vita keeps to the Chupathingy menus and
the console rows the game marks for every console (platform="console"), but
writes the three files it needs its own of, with the game's generator
(chupathingyce/tools/port_settings.py) and its rows, as the Switch's port
does (its tools/switch_menus.py). port/vita/build.py embeds them in place
of the game's (tools/vita_embed_assets.py), so nothing has to be copied to
the memory card; a file of the same name in the data folder's menus/ still
replaces one, as on every platform.

- The settings pages (chupathingy/settings_pages.xml): each page with the
  rows the Vita has, packed with no gaps, and Video Setup with its own:
  RESOLUTION (display.resolution_limit, 480 or 544 lines) and DYNAMIC
  RESOLUTION (display.dynamic_resolution), which the game declares for the
  Vita (port_config.c's HALO_VITA_CONSOLE block) and port/vita/platform/
  d3d8_gxm.c reads. SHADOW RESOLUTION and PER-PIXEL LIGHTING are left out:
  the Vita's renderer has no shadow map to size (vita_glue.c's
  halo_shadow_map_scale) and lights models as the Xbox did whatever the
  setting says (OpenGL's per-pixel lighting is d3d8_gl.c's). Anti-aliasing
  is the desktop's and Android's alone already. SOFT SHADOWS stays: the
  game's own convolution draws it. The pages place rows whose widgets the
  PC version's screen files define, so Video Setup's file
  (ce/...video_settings.xml) is written too.
- Controls Setup's controller gets a second page under SHOW, TOUCH AND
  GYRO: the touch controls on or off (input.touch_controls), the rear touch
  pad's guard (input.rear_touch_guard), gyro aim always, only on zoom or off
  (input.gyro_aim), its sensitivity and its vertical invert (port/vita/host/
  vita_input.c and vita_controls.c read them). It is the list's second view,
  the one the desktop's button rows are in (menu_functions.c's
  controls_page_view: the page after the first), which the consoles leave
  empty (HALO_CONSOLE_NO_CONTROLS). VIBRATION is left out: the Vita has no
  motor. The pages place rows whose widgets the PC version's Controls Setup
  file defines, so that file (ce/...controls_setup.xml) is written too.
"""

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "chupathingyce"
MENUS = GAME / "port" / "assets" / "menus"

sys.path.insert(0, str(GAME / "tools"))
import port_settings  # noqa: E402

CONTROLS_NAME = f"{port_settings.PE}/{port_settings.CONTROLS_SCREEN}".replace("/", ".") + ".xml"
CONTROLS = f"ce/{CONTROLS_NAME}"
SETTINGS_PAGES = "chupathingy/settings_pages.xml"
VIDEO_NAME = f"{port_settings.PE}/video_settings".replace("/", ".") + ".xml"
VIDEO = f"ce/{VIDEO_NAME}"

# the Vita's rows of Video Setup, after the rows it shares with the others
# (label, setting, choices, help, platform): the settings are the game's
# (HALO_VITA_CONSOLE), their values read by port/vita/platform/d3d8_gxm.c
VITA_VIDEO_ROWS = [
    ("RESOLUTION:", "display.resolution_limit", [("480 (XBOX)", "480"), ("544 (VITA)", "544")],
     "How many lines the game draws.\nEither fills the screen.", None),
    ("DYNAMIC RESOLUTION:", "display.dynamic_resolution", port_settings.ON_OFF,
     "Draw fewer pixels when the graphics chip falls\nbehind, and more again once it keeps up.", None),
]
# the controller's second page, TOUCH AND GYRO (the gyro rows' widgets named
# apart from the desktop's GYRO page's, which the file defines too)
GYRO_SENSITIVITIES = [(f"{value:g}", f"{value:g}") for value in
                      (0.25, 0.5, 0.75, 1, 1.25, 1.5, 2, 2.5, 3, 4, 5, 6, 7, 8, 9, 10)]
VITA_CONTROLLER_ROWS = [
    ("TOUCH CONTROLS:", "input.touch_controls", [("ON", "on"), ("OFF", "off")],
     "The touch screen's and the rear touch pad's buttons.", None),
    ("REAR TOUCH GUARD:", "input.rear_touch_guard",
     [("OFF", "off"), ("LIGHT", "light"), ("NORMAL", "normal"), ("STRONG", "strong")],
     "Ignore the rear pad's edges, where your\nfingers rest while you hold the Vita.", None),
    ("GYRO AIM:", "input.gyro_aim", port_settings.ON_OFF + [("ONLY ON ZOOM", "zoom")],
     "Turn the view by turning the Vita,\nalways, or only while zoomed in.", None, "vita_gyro_aim"),
    ("GYRO SENSITIVITY:", "input.gyro_sensitivity_horizontal", GYRO_SENSITIVITIES,
     "How far the view turns.\n1 is the Vita's own turn.", None, "vita_gyro_sensitivity"),
    ("GYRO INVERT VERTICAL:", "input.gyro_invert_vertical", port_settings.YES_NO,
     "Tilting the Vita up looks down.", None, "vita_gyro_invert"),
]
VITA_PAGES = ["SETTINGS", "TOUCH AND GYRO"]
# rows of every platform's or every console's the Vita leaves out: no shadow
# map to size, no per-pixel lighting, no rumble (the module's text)
ROWS_LEFT_OUT = {"display.shadow_resolution", "display.per_pixel_lighting", "profile.vibration"}


def vita_rows(rows: list) -> list:
    """the rows the Vita shows, with no platform: a row's platform names the
    platforms it is on, separated by spaces (port_settings.py). The Vita
    keeps the rows for every platform and those for the consoles"""
    kept = []
    for row in rows:
        label, setting, choices, help_text, platform, *named = row
        if setting in ROWS_LEFT_OUT:
            continue
        names = platform.split() if platform else None
        if names is not None and port_settings.CONSOLE not in names:
            continue
        if isinstance(row, port_settings.Action):
            kept.append(port_settings.Action(label, named[0], choices, help_text, None))
        else:
            kept.append((label, setting, choices, help_text, None, *named))
    return kept


def vita_spec(spec: dict, extra_rows: list) -> dict:
    """a screen's spec with the Vita's rows: the others' pared down and its
    own added, with no row taking another's place, and no nested list left
    with no rows. (A shallow copy: the rows are tuples, replaced.)"""
    spec = dict(spec)
    spec["rows"] = vita_rows(spec["rows"]) + extra_rows
    spec["same_place"] = []
    views = [(name, label, help_text, vita_rows(rows)) for name, label, help_text, rows in spec.get("views", [])]
    spec["views"] = [view for view in views if view[3]]
    return spec


def controls_rows_for_vita(original):
    """Controls Setup's rows and widgets for the Vita: SHOW with its two
    pages at the top, the controller's settings below it (BUTTON LAYOUT and
    the rows every platform has) on the first, TOUCH AND GYRO's rows on the
    second, in the view the desktop's button rows have. The keyboard's and
    the mouse's rows, and DEVICE, are left out, as on every console"""
    def rows_and_extra():
        base = f"{port_settings.PE}/{port_settings.CONTROLS_SCREEN}"
        rows, extra = original()
        gamepad = [f"{base}/op_layout"] + [f"{base}/op_{port_settings._row_key(row)}"
                                           for row in port_settings.GAMEPAD_ROWS]
        packed = [(f"{base}/view_gamepad_buttons", None, 0), (f"{base}/op_page", None, 0),
                  (f"{base}/view_gamepad", None, 0)]
        packed += [(widget, None, place) for place, widget in enumerate(gamepad, 1)]
        packed.append((f"{base}/view_buttons", None, 0))
        added = port_settings._mark(base, "gamepad_buttons")
        start = extra.index(f'\t<strings name="{base}/labels">')
        end = extra.index("\t</strings>", start)
        first_label = end - start - 1
        for place, row in enumerate(VITA_CONTROLLER_ROWS, 1):
            key = port_settings._row_key(row)
            packed.append((f"{base}/op_{key}", None, place))
            added += port_settings._setting_row(base, row, key, first_label + place - 1)
        # (SHOW's two pages: the generator's chooser also names the
        # desktop's GYRO page, which the Vita has not)
        shown = "|".join(port_settings.CONTROLLER_PAGES)
        extra = [line.replace(f'strings="{shown}|GYRO"', f'strings="{shown}"') for line in extra]
        labels_at = extra.index(f'\t<strings name="{base}/labels">')
        extra = extra[:labels_at] + added + extra[labels_at:]
        for name, texts in (("labels", [label for label, *_ in VITA_CONTROLLER_ROWS]),
                            ("help_strings", [row[3].replace("\n", "\\n") for row in VITA_CONTROLLER_ROWS])):
            start = extra.index(f'\t<strings name="{base}/{name}">')
            end = extra.index("\t</strings>", start)
            extra[end:end] = [f"\t\t<string{port_settings.attributes([('text', text)])}/>" for text in texts]
        return packed, extra
    return rows_and_extra


def files() -> dict:
    """the files, by their paths below the menus folder"""
    port_settings.CONTROLLER_PAGES = list(VITA_PAGES)
    port_settings.GAMEPAD_ROWS = vita_rows(port_settings.GAMEPAD_ROWS)
    port_settings._controls_rows = controls_rows_for_vita(port_settings._controls_rows)
    for folder, spec in port_settings.SCREENS.items():
        port_settings.SCREENS[folder] = vita_spec(spec, VITA_VIDEO_ROWS if folder == "video_settings" else [])
    return {
        SETTINGS_PAGES: port_settings.chupathingy_settings_pages(),
        VIDEO: port_settings.settings_files()[VIDEO_NAME],
        CONTROLS: port_settings.settings_files()[CONTROLS_NAME],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "vita" / "menus",
                        help="the folder to write (default: build/vita/menus)")
    args = parser.parse_args()
    written = files()
    for name, lines in written.items():
        if not (MENUS / name).is_file():
            print(f"{name}: not a file the game embeds, so it would replace nothing", file=sys.stderr)
            return 1
        path = args.out / name
        path.parent.mkdir(parents=True, exist_ok=True)
        text = "\n".join(lines)
        if not text.endswith("\n"):
            text += "\n"
        if not path.is_file() or path.read_text() != text:
            path.write_text(text)
    for stale in sorted(args.out.rglob("*.xml")):
        if stale.relative_to(args.out).as_posix() not in written:
            stale.unlink()
    print(f"{args.out}: {len(written)} menu files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
