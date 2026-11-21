#!/usr/bin/env python3
"""The game's embedded assets (chupathingyce/tools/embed_assets.py) with the
Vita's own menu files in the game's place (tools/vita_menus.py).

    python3 tools/vita_embed_assets.py OUTPUT.c

embed_assets.py reads the menus' files from the game's port/assets/menus,
by the list there (menus.json). This runs it with that folder's files as
they are but the ones tools/vita_menus.py writes, which take the game's
place under the same names, so the Vita's rows are in the program and
nothing has to be copied to the memory card.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "chupathingyce"

sys.path.insert(0, str(GAME / "tools"))
sys.path.insert(0, str(ROOT / "tools"))
import embed_assets  # noqa: E402
import vita_menus  # noqa: E402


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit("usage: vita_embed_assets.py OUTPUT.c")
    output = Path(sys.argv[1])
    game_menus = GAME / embed_assets.MENU_ASSETS
    merged = output.parent / "menus"
    vita_files = {name: "\n".join(lines) + ("" if lines[-1] == "" else "\n")
                  for name, lines in vita_menus.files().items()}
    listed = embed_assets.menu_files()
    for name in vita_files:
        if name not in listed:
            sys.exit(f"{name}: not one of the game's menu files (menus.json)")
    for name in [*listed, "menus.json"]:
        path = merged / name
        path.parent.mkdir(parents=True, exist_ok=True)
        data = vita_files[name].encode() if name in vita_files else (game_menus / name).read_bytes()
        if not path.is_file() or path.read_bytes() != data:
            path.write_bytes(data)
    # (an absolute folder: the module joins it to the game's root, which
    # leaves it as it is)
    embed_assets.MENU_ASSETS = merged.resolve()
    embed_assets.MENU_LIST = embed_assets.MENU_ASSETS / "menus.json"
    embed_assets.main()
    # each HUD sheet's scale over its bitmap (8 or 4, the sheet's own), in
    # the embedded table's order and with its guards, which the table does
    # not keep: the Vita decodes a map's sheets ahead at the size it will
    # want, twice the bitmap (platform/vita_textures.c's hud_ahead)
    lines = ["", "/* (tools/vita_embed_assets.py) each sheet's scale over its bitmap */",
             "const unsigned char vita_hud_embedded_scale[] =", "{"]
    for folder, asset, title in embed_assets.textures():
        mcc = int(folder == embed_assets.MCC_HUD_ASSETS)
        choice = ["#ifdef HALO_TITLE_CHOICE"] if title == 2 else ["#if HALO_MCC"] if mcc else []
        lines += [*choice, f"\t{int(asset['scale'])},", *(["#endif"] if choice else [])]
    lines += ["\t0,", "};", ""]
    with open(output, "a") as file:
        file.write("\n".join(lines))


if __name__ == "__main__":
    main()
