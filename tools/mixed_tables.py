#!/usr/bin/env python3
"""Mixed play's built-in tables (port/linux/game/mixed_coop_tables.h) -> port/linux/game/mixed_coop_tables.inc.

Input: a folder of hex dumps a Linux build writes with HALO_MIXED_COOP_FORCE=1 HALO_MIXED_TABLE_OUT=1, one level
loaded alone each (port/vita/tests/run_mixed_tables.sh): <level>.xbox.hex from the Xbox's maps (the dictionary an
Xbox-files host sends, without the netgame flags' places, and for Blood Gulch, Battle Creek and Sidewinder its layout
once placed), <level>.mcc.hex from MCC's (those three maps' layouts as a client finds them).

Nothing of a map goes in but what a host's dictionary already carries (hashes, digests, datums) and, for the three
layout maps, the Xbox's placements' structure with their places zeroed and, per placement, the index of the MCC
placement whose place it takes (matched here by tag and nearest place). No coordinates are written.

Usage: mixed_tables.py HEXDIR OUT.inc
"""
import math
import os
import struct
import sys
import zlib

STEMS = ["a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
         "chillout", "longest", "prisoner", "putput", "ratrace", "wizard", "boardingaction", "damnation",
         "carousel", "hangemhigh", "bloodgulch", "beavercreek", "sidewinder"]
LAYOUT_MAPS = {"bloodgulch", "beavercreek", "sidewinder"}
FNV_BASIS, FNV_PRIME = 0x811C9DC5, 0x01000193
LAYOUT_MAGIC, TABLE_LAYOUT_MAGIC = 0x314C584D, 0x31534C4D
PALETTE_BYTES = 8 + 128
# (mixed_coop_protocol.h's MIXED_COOP_LAYOUT_*) palette and placement kinds by block
SCENERY_PALETTE, SCENERY, VEHICLE_PALETTE, VEHICLES, NETGAME_FLAGS = 1, 2, 3, 4, 5
# a placement's place and turn in the scenario's element (scenario_object_datum: position at 8, rotation at 20)
PLACE_FIRST, PLACE_END = 8, 32
# how near a placement of the same tag must be to take its place (world units): scenery near; a vehicle any of its
# tag (the host's objects: the host sends where each is, and a vehicle of another tag must never stand at its index)
MATCH_DISTANCE = {2: 4.0, 4: 1000.0}


def fnv(data, h=FNV_BASIS):
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & 0xFFFFFFFF
    return h


def read_hex(path):
    """{(what, family): bytes} from 'what level family offset hex' lines"""
    parts = {}
    with open(path) as f:
        for line in f:
            fields = line.split()
            if len(fields) != 5:
                continue
            what, _level, family, offset, data = fields
            parts.setdefault((what, family), []).append((int(offset), bytes.fromhex(data)))
    out = {}
    for key, pieces in parts.items():
        pieces.sort()
        blob = b"".join(p for _, p in pieces)
        if blob[-4:] != struct.pack("<I", fnv(blob[:-4])):
            raise SystemExit(f"{path}: {key}'s check is wrong")
        out[key] = blob
    return out


def parse_layout(blob):
    magic, version, level, count = struct.unpack_from("<IHBB", blob, 0)
    assert magic == LAYOUT_MAGIC and version == 1
    at, blocks = 8, {}
    for _ in range(count):
        kind, _zero, size, n = struct.unpack_from("<BBHI", blob, at)
        at += 8
        blocks[kind] = (size, [blob[at + i * size: at + (i + 1) * size] for i in range(n)])
        at += size * n
    return level, blocks


def place(element):
    return struct.unpack_from("<3f", element, PLACE_FIRST)


def write_layout(level, blocks):
    out = bytearray(struct.pack("<IHBB", LAYOUT_MAGIC, 1, level, len(blocks)))
    for kind in sorted(blocks):
        size, elements = blocks[kind]
        out += struct.pack("<BBHI", kind, 0, size, len(elements))
        for e in elements:
            out += e
    out += struct.pack("<I", fnv(bytes(out)))
    return bytes(out)


def layout_table(stem, xbox, mcc):
    level, xb = parse_layout(xbox)
    _, mb = parse_layout(mcc)
    blocks, sources, report = {}, [], []
    for palette_kind, kind in ((SCENERY_PALETTE, SCENERY), (VEHICLE_PALETTE, VEHICLES)):
        psize, xpal = xb[palette_kind]
        size, xs = xb[kind]
        _, mpal = mb[palette_kind]
        msize, ms = mb[kind]
        assert size == msize, (stem, kind, size, msize)
        # (palettes: group and path hash; the path's text left out)
        blocks[palette_kind] = (psize, [e[:8] + bytes(PALETTE_BYTES - 8) for e in xpal])
        xhash = [struct.unpack_from("<II", e, 0) for e in xpal]
        mhash = [struct.unpack_from("<II", e, 0) for e in mpal]
        used, out = set(), []
        # each Xbox placement: the nearest unused MCC placement of the same tag within MATCH_DISTANCE; else the
        # nearest MCC placement of any tag stands in (its own tag at its place)
        pairs = []
        for xi, e in enumerate(xs):
            xp = struct.unpack_from("<h", e, 0)[0]
            for mi, m in enumerate(ms):
                mp = struct.unpack_from("<h", m, 0)[0]
                if xp >= 0 and mp >= 0 and xhash[xp] == mhash[mp]:
                    d = math.dist(place(e), place(m))
                    if d <= MATCH_DISTANCE[kind]:
                        pairs.append((d, xi, mi))
        pairs.sort()
        match = {}
        for d, xi, mi in pairs:
            if xi not in match and mi not in used:
                match[xi] = (mi, d)
                used.add(mi)
        for xi, e in enumerate(xs):
            zeroed = e[:PLACE_FIRST] + bytes(PLACE_END - PLACE_FIRST) + e[PLACE_END:]
            out.append(zeroed)
            if xi in match:
                sources.append((match[xi][0], 0))
                continue
            # (stand-in: the nearest MCC placement of any tag, by the Xbox's place, which is not written)
            best = min(range(len(ms)), key=lambda mi: math.dist(place(e), place(ms[mi])))
            sources.append((best, 1))
            report.append(f"{stem}: block {kind} placement {xi}: no MCC placement of its tag near, "
                          f"MCC placement {best} stands in ({math.dist(place(e), place(ms[best])):.2f} wu)")
        blocks[kind] = (size, out)
        moved = [d for (mi, d) in match.values()]
        report.append(f"{stem}: block {kind}: {len(xs)} Xbox placements, {len(match)} matched "
                      f"(farthest {max(moved) if moved else 0:.2f} wu), {len(xs) - len(match)} stood in; MCC has {len(ms)}")
    skeleton = write_layout(level, blocks)
    table = struct.pack("<II", TABLE_LAYOUT_MAGIC, len(skeleton)) + skeleton
    for mi, kind in sources:
        table += struct.pack("<HBB", mi, kind, 0)
    return table, report


def c_bytes(name, data):
    lines = [f"static unsigned char const {name}[{len(data)}] =", "{"]
    for i in range(0, len(data), 24):
        lines.append("\t" + ",".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    hexdir, out = sys.argv[1], sys.argv[2]
    parts, rows, total = [], [], 0
    for level, stem in enumerate(STEMS):
        xpath = os.path.join(hexdir, f"{stem}.xbox.hex")
        if not os.path.exists(xpath):
            print(f"{stem}: no dump, left out")
            continue
        x = read_hex(xpath)
        dictionary = x[("table", "xbox")]
        d = zlib.compress(dictionary, 9)
        parts.append(c_bytes(f"mixed_coop_table_{stem}", d))
        layout = "NULL, 0, 0"
        size = len(d)
        if stem in LAYOUT_MAPS:
            m = read_hex(os.path.join(hexdir, f"{stem}.mcc.hex"))
            table, report = layout_table(stem, x[("layout", "xbox")], m[("layout", "mcc")])
            print("\n".join(report))
            lz = zlib.compress(table, 9)
            parts.append(c_bytes(f"mixed_coop_table_{stem}_layout", lz))
            layout = f"mixed_coop_table_{stem}_layout, {len(lz)}, {len(table)}"
            size += len(lz)
        rows.append(f"\t{{ {level}, mixed_coop_table_{stem}, {len(d)}, {len(dictionary)}, {layout} }},")
        total += size
        print(f"{stem}: dictionary {len(dictionary)} bytes ({len(d)} deflated)")
    with open(out, "w") as f:
        f.write("/* MIXED_COOP_TABLES.INC: made by tools/mixed_tables.py from the Xbox's maps and MCC's (hashes, digests,\n"
                "datums and placement structure only: mixed_coop_tables.h). Do not edit. */\n\n")
        f.write("\n\n".join(parts))
        f.write("\n\nstatic struct mixed_coop_table const mixed_coop_tables[] =\n{\n" + "\n".join(rows) + "\n};\n")
    print(f"{len(rows)} levels, {total} bytes deflated")


if __name__ == "__main__":
    main()
