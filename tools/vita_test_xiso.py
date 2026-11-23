#!/usr/bin/env python3
"""Xbox disc images (XDVDFS, as xiso tools make them) for testing the first
start's import (port/vita/setup, the game's port/linux/src/xiso.c). Nothing
of the game's is in them unless a maps folder is given: default.xbe is a
made-up one with Halo's title ID (or another's), and without --maps the maps
are made-up headers.

    vita_test_xiso.py OUTPUT.iso --maps FOLDER [--loading-picture]
    vita_test_xiso.py OUTPUT.iso --fake [--title-id 0x4D530064] [--ui-version 7]

Each directory table chains its entries through their right subtrees (the
reader follows the tree; it needn't be balanced). Files are copied in, a
megabyte at a time, so a whole maps folder needs no memory. The image is
for tests only: delete it after.
"""

import argparse
import struct
import sys
from pathlib import Path

SECTOR = 2048
HALO_TITLE_ID = 0x4D530004


def fake_xbe(title_id):
    image = bytearray(0x400)
    image[0:4] = b"XBEH"
    struct.pack_into("<I", image, 0x104, 0x10000)
    struct.pack_into("<I", image, 0x118, 0x10200)
    struct.pack_into("<I", image, 0x208, title_id)
    return bytes(image)


def fake_map(name, version=5, size=0x800):
    header = bytearray(0x800)
    header[0:4] = b"daeh"
    struct.pack_into("<i", header, 4, version)
    header[0x20:0x20 + len(name)] = name.encode()
    header[0x40:0x40 + 13] = b"01.10.12.2276"
    header[0x7FC:0x800] = b"toof"
    return bytes(header) + bytes(max(0, size - 0x800))


def write_image(output, tree):
    """tree: name -> bytes, a Path (a file copied in) or a dict (a folder)"""
    next_sector = [33]
    placed = []  # (sector, bytes or Path)

    def allocate(size):
        sector = next_sector[0]
        next_sector[0] += max(1, (size + SECTOR - 1) // SECTOR)
        return sector

    def place(node):
        if isinstance(node, (bytes, Path)):
            size = len(node) if isinstance(node, bytes) else node.stat().st_size
            sector = allocate(size)
            placed.append((sector, node))
            return sector, size, 0
        names = sorted(node, key=str.lower)
        sizes = [(14 + len(name) + 3) & ~3 for name in names]
        table_size = sum(sizes)
        table_sector = allocate(table_size)
        table = bytearray(b"\xff" * (((table_size + SECTOR - 1) // SECTOR) * SECTOR))
        offset = 0
        for index, name in enumerate(names):
            sector, size, attributes = place(node[name])
            right = (offset + sizes[index]) // 4 if index + 1 < len(names) else 0
            struct.pack_into("<HHIIBB", table, offset, 0, right, sector, size, attributes, len(name))
            table[offset + 14:offset + 14 + len(name)] = name.encode()
            offset += sizes[index]
        placed.append((table_sector, bytes(table)))
        return table_sector, table_size, 0x10

    root_sector, root_size, _ = place(tree)
    descriptor = bytearray(SECTOR)
    descriptor[0:20] = b"MICROSOFT*XBOX*MEDIA"
    struct.pack_into("<II", descriptor, 20, root_sector, root_size)
    descriptor[0x7EC:0x800] = b"MICROSOFT*XBOX*MEDIA"
    with open(output, "wb") as image:
        image.seek(32 * SECTOR)
        image.write(descriptor)
        for sector, node in sorted(placed, key=lambda item: item[0]):
            image.seek(sector * SECTOR)
            if isinstance(node, bytes):
                image.write(node)
            else:
                with open(node, "rb") as source:
                    while True:
                        block = source.read(1 << 20)
                        if not block:
                            break
                        image.write(block)
        image.truncate(next_sector[0] * SECTOR)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", type=Path)
    parser.add_argument("--maps", type=Path, help="a maps folder copied in as the disc's (its files, not folders)")
    parser.add_argument("--only", nargs="*", help="with --maps: only these files")
    parser.add_argument("--loading-picture", action="store_true", help="a made-up loading.tga in maps")
    parser.add_argument("--fake", action="store_true", help="made-up maps (headers only)")
    parser.add_argument("--fake-size", type=int, default=0x800, help="each made-up map's size in bytes")
    parser.add_argument("--title-id", type=lambda text: int(text, 0), default=HALO_TITLE_ID)
    parser.add_argument("--ui-version", type=int, default=5)
    parser.add_argument("--no-xbe", action="store_true")
    arguments = parser.parse_args()

    maps = {}
    if arguments.maps:
        for path in sorted(arguments.maps.iterdir()):
            if path.is_file() and (not arguments.only or path.name in arguments.only):
                maps[path.name] = path.resolve()
    if arguments.fake:
        for name in ("ui", "a10", "bloodgulch"):
            maps[name + ".map"] = fake_map(name, arguments.ui_version if name == "ui" else 5,
                                           arguments.fake_size)
    if not maps:
        sys.exit("vita_test_xiso.py: --maps or --fake")
    if arguments.loading_picture:
        maps["loading.tga"] = b"\0\0\x02" + b"\0" * 9 + struct.pack("<HH", 2, 2) + b"\x20\x20" + b"\x80" * 16
    tree = {"maps": maps}
    if not arguments.no_xbe:
        tree["default.xbe"] = fake_xbe(arguments.title_id)
    write_image(arguments.output, tree)


if __name__ == "__main__":
    main()
