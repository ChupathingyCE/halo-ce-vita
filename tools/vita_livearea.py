#!/usr/bin/env python3
"""The Vita's bubble and LiveArea pictures (port/vita/sce_sys), from the
Warthog icon (port/vita/art/icon-160.png, the Warthog ports' own): icon0.png
128x128, livearea bg.png 840x500 and startup.png 280x158, as the system
wants them, 8-bit palette PNGs.

    python3 tools/vita_livearea.py
"""

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ART = ROOT / "port/vita/art/icon-160.png"
OUT = ROOT / "port/vita/sce_sys"


def save(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT).save(path, optimize=True)


def main() -> None:
    icon = Image.open(ART).convert("RGB")
    background = icon.getpixel((4, 4))
    save(icon.resize((128, 128), Image.LANCZOS), OUT / "icon0.png")
    for name, size, side in (("bg.png", (840, 500), 300), ("startup.png", (280, 158), 140)):
        canvas = Image.new("RGB", size, background)
        picture = icon.resize((side, side), Image.LANCZOS)
        canvas.paste(picture, ((size[0] - side) // 2, (size[1] - side) // 2))
        save(canvas, OUT / "livearea" / "contents" / name)


if __name__ == "__main__":
    main()
