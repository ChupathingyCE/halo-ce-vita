#!/usr/bin/env python3
"""The setup screens' faces (port/vita/setup/setup_draw.c) as C data: the
Chupathingy menus' title and text faces (the game's port/assets/fonts:
Chupathingy Titles, Cyborg Interface Gothic, both SIL OFL 1.1) and the
PlayStation's buttons from Kenney's Input Prompts (the game's
port/linux/ui/fonts, CC0), in setup_draw.h's SETUP_FONT_* order.

    python3 tools/vita_setup_fonts.py GAME_ROOT OUTPUT.c
"""

import sys
from pathlib import Path

FONTS = (
    ("title", Path("port/assets/fonts/ChupathingyTitles-Regular.ttf")),
    ("text", Path("port/assets/fonts/CyborgInterfaceGothic-Regular.ttf")),
    ("prompts", Path("port/linux/ui/fonts/input_playstation.ttf")),
)


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit("usage: vita_setup_fonts.py GAME_ROOT OUTPUT.c")
    game = Path(sys.argv[1])
    lines = ["/* the setup screens' faces (tools/vita_setup_fonts.py): made by the build */", ""]
    for name, path in FONTS:
        data = (game / path).read_bytes()
        lines.append(f"/* {path.name}, {len(data)} bytes */")
        lines.append(f"static const unsigned char font_{name}[] __attribute__((aligned(4))) = {{")
        for start in range(0, len(data), 24):
            lines.append("\t" + ",".join(str(byte) for byte in data[start:start + 24]) + ",")
        lines.append("};")
        lines.append("")
    lines.append("const unsigned char *const setup_font_files[] = { "
                 + ", ".join(f"font_{name}" for name, _ in FONTS) + " };")
    output = Path(sys.argv[2])
    text = "\n".join(lines) + "\n"
    if not output.is_file() or output.read_text() != text:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text)


if __name__ == "__main__":
    main()
