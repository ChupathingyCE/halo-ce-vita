#!/usr/bin/env python3
"""The tag validator's alignment of a map's tags and blocks
(port/linux/game/tag_validate.c, aligned_for), run by tools/map_validate.c.

The game reads a map's tags through its structures, and the Vita's processor
faults on a misaligned doubleword, multiple or floating-point load (LDRD,
LDM, VLDR) that the compiler makes of adjacent fields, so a tag or block at
an address no tool lays one out at stops the game: a tag's root and a block
whose elements are whole words must be on a word, and the instances table
too. The Custom Edition map fuzzer (tools/ce_map_fuzz.c) found a Halo: CE
Anniversary map whose bitmap tag, moved one byte off a word, passed the
check: the test makes that change to a copy of MCC's bloodgulch.map.

No map is in the repository: HALO_TEST_MCC_MAPS names MCC's halo1/maps
folder (the map and its bitmaps.map; default the dev Vita's
setup files), and the test skips without it or with another version of the
map. Every retail Xbox map, Custom Edition map and Halo: CE Anniversary map
at hand passes the check as it did (tools/map_validate.c, Nov 2026).
"""

import hashlib
import os
import struct
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
BLOODGULCH_SHA1 = "149bdb7824b2c09d1c5d309b1f8f2a1898272e73"
# the instance of its 'weapons\sniper rifle\bitmaps\sr decal' (bitm): its
# root's address, in the file
SR_DECAL_ROOT_ADDRESS = 0x899AA8


def paths():
    validator = Path(os.environ.get("HALO_TEST_MAP_VALIDATE", ROOT / "build/linux/map_validate"))
    maps = Path(os.environ.get("HALO_TEST_MCC_MAPS",
                               Path.home() / "halo-test-out/devvita/setup-files/mcc/halo1/maps"))
    if not validator.is_file():
        pytest.skip(f"no {validator} (ninja linux)")
    game_map = maps / "bloodgulch.map"
    if not all((maps / f"{name}.map").is_file() for name in ("bloodgulch", "bitmaps")):
        pytest.skip("no MCC halo1 maps (HALO_TEST_MCC_MAPS)")
    if hashlib.sha1(game_map.read_bytes()).hexdigest() != BLOODGULCH_SHA1:
        pytest.skip("another version of MCC's bloodgulch.map")
    return validator, maps


def validate(validator, path):
    result = subprocess.run([str(validator), str(path)], capture_output=True, text=True, timeout=600)
    return result.returncode, result.stdout + result.stderr


def copy_with(maps, folder, changes):
    for name in ("bitmaps", "sounds"):
        if (maps / f"{name}.map").is_file():
            (folder / f"{name}.map").symlink_to(maps / f"{name}.map")
    data = bytearray((maps / "bloodgulch.map").read_bytes())
    for offset, value in changes:
        struct.pack_into("<I", data, offset, value)
    path = folder / "bloodgulch.map"
    path.write_bytes(data)
    return path


def test_the_map_as_it_is_plays(tmp_path):
    validator, maps = paths()
    returncode, output = validate(validator, copy_with(maps, tmp_path, []))
    assert returncode == 0, output
    assert output.rstrip().endswith("bloodgulch.map: 0 corrections")


def test_a_tag_off_a_word_is_refused(tmp_path):
    validator, maps = paths()
    original = struct.unpack_from("<I", (maps / "bloodgulch.map").read_bytes(), SR_DECAL_ROOT_ADDRESS)[0]
    assert original % 4 == 0
    # (the place the fuzzer moved it to: in the tags, overlapping nothing)
    returncode, output = validate(validator, copy_with(maps, tmp_path, [(SR_DECAL_ROOT_ADDRESS, 0x50545C65)]))
    assert returncode == 1, output
    assert "sr decal' (bitm):  has its data at" in output and "refused" in output
