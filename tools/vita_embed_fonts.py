#!/usr/bin/env python3
"""The overlay's fonts and button icons (chupathingyce/tools/embed_assets.py
--fonts: posix_ui_font.c's, for Online Games and the Delta Guide) sized for
the Vita's memory, where they are part of the program.

    python3 tools/vita_embed_fonts.py OUTPUT.c

What differs from the game's: one Noto Sans face for both of its places
(Bold: the overlay draws its screens in the menus' faces, and Noto Sans only
stands in for a character those lack), cut to the scripts a player's name
or a Delta profile is likely written in (Latin, Greek, Cyrillic and their
punctuation; fontTools' subsetter, when it is installed: else the whole
face); no prompt fonts of other devices; and the button icons at half
their size (64 pixels: the Vita's prompts are the PlayStation font's, the
icons stand in only for an Xbox pad's). About 1.4 MB less than the game's.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "chupathingyce"

sys.path.insert(0, str(GAME / "tools"))
import embed_assets  # noqa: E402

# (Basic Latin to Latin Extended-B, IPA and spacing marks, combining marks,
# Greek, Cyrillic, general punctuation, currency, letterlike symbols,
# arrows, the replacement character)
UNICODES = ("U+0000-024F,U+0250-02FF,U+0300-036F,U+0370-03FF,U+0400-04FF,U+2000-206F,U+20A0-20CF,"
            "U+2100-214F,U+2190-21FF,U+FFFD")


def subset(source: Path, output: Path) -> Path:
    try:
        from fontTools import subset as fonttools_subset
    except ImportError:
        return source
    options = fonttools_subset.Options()
    # (stb_truetype reads the outlines, the character map and the kerning
    # table alone: no layout tables, no hinting)
    options.layout_features = []
    options.hinting = False
    options.name_IDs = ["*"]
    options.notdef_outline = True
    font = fonttools_subset.load_font(str(source), options)
    subsetter = fonttools_subset.Subsetter(options)
    subsetter.populate(unicodes=fonttools_subset.parse_unicodes(UNICODES))
    subsetter.subset(font)
    fonttools_subset.save_font(font, str(output), options)
    return output


def half(pixels: bytes, width: int, height: int) -> bytes:
    """RGBA rows at half the size, each texel the mean of four"""
    out = bytearray((width // 2) * (height // 2) * 4)
    for y in range(height // 2):
        for x in range(width // 2):
            for channel in range(4):
                total = 0
                for dy in (0, 1):
                    for dx in (0, 1):
                        total += pixels[((2 * y + dy) * width + 2 * x + dx) * 4 + channel]
                out[(y * (width // 2) + x) * 4 + channel] = (total + 2) // 4
    return bytes(out)


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit("usage: vita_embed_fonts.py OUTPUT.c")
    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    regular = embed_assets.UI_FONTS / "NotoSans-Regular.ttf"
    bold = embed_assets.UI_FONTS / "NotoSans-Bold.ttf"
    cut = subset(GAME / bold, output.parent / "NotoSans-Bold-vita.ttf").resolve()
    embed_assets.UI_FONT_FILES = [cut if name in (regular, bold) else name for name in embed_assets.UI_FONT_FILES]
    original = embed_assets.png_rgba

    def png_rgba_half(data: bytes, name: str):
        width, height, pixels = original(data, name)
        return width // 2, height // 2, half(pixels, width, height)

    embed_assets.png_rgba = png_rgba_half
    embed_assets.overlay_fonts(output, [])


if __name__ == "__main__":
    main()
