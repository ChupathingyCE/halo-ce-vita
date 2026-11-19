#!/usr/bin/env python3
"""The Vita's bubble and LiveArea pictures (port/vita/sce_sys), from
Chupathingy's icon (the game's port/art/icon.svg: the helmet with tusks, in
Chupathingy red), as the system wants them: 8-bit palette PNGs.

    icon0.png                     128x128  the icon
    livearea/contents/startup.png 280x158  the icon on red (the start gate)
    livearea/contents/bg.png      840x500  plain red: a placeholder
    pic0.png                      960x544  plain red: a placeholder

The LiveArea background and the start picture have no art of their own yet
(placeholders in Chupathingy red, #A3201C). Needs rsvg-convert (librsvg) and
Python's PIL.

    python3 tools/vita_livearea.py
"""

import subprocess
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ICON = ROOT / "chupathingyce/port/art/icon.svg"
OUT = ROOT / "port/vita/sce_sys"
RED = (0xA3, 0x20, 0x1C)


def render(size: int) -> Image.Image:
    with tempfile.TemporaryDirectory() as folder:
        png = Path(folder) / "icon.png"
        subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), "-o", str(png), str(ICON)], check=True)
        return Image.open(png).convert("RGB")


def save(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    # (8-bit indexed, as the LiveArea takes them: PIL writes fewer bits for a
    # palette of few colors unless told)
    image.convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT).save(path, bits=8)


def main() -> None:
    save(render(128), OUT / "icon0.png")
    startup = Image.new("RGB", (280, 158), RED)
    startup.paste(render(158), ((280 - 158) // 2, 0))
    save(startup, OUT / "livearea" / "contents" / "startup.png")
    save(Image.new("RGB", (840, 500), RED), OUT / "livearea" / "contents" / "bg.png")
    save(Image.new("RGB", (960, 544), RED), OUT / "pic0.png")


if __name__ == "__main__":
    main()
