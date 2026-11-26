"""A copy of a Halo: CE Anniversary map (MCC's version 13 cache) made into one this build cannot run, for the menus'
tests (run_menus_test.sh wsunplayable; never in the repository: the map is the player's own MCC's, patched at test
time in the test's folder).

  script-parameter <map>  the first script syntax node in use made one naming a script's parameter (flags 0x15, as
                          MCC's compiler writes them): the game refuses such a map - "the scenario's scripts take
                          parameters" (cache_file_formats.c) - and, since 1.1.3's device test of Nov 25, the mods
                          folder's scan finds it first (mcc_cea_cache_runnable) and never lists it
  bsp-signature <map>     the first structure BSP's header signature spoiled: the scan's checks pass it (they read no
                          BSP), its load fails ("a structure BSP header is not valid") - a map that still fails at the
                          load, for the menus' way back from it

The file is changed in place. Usage: python3 -I mcc_map_patch.py <what> <map>
"""
import struct
import sys

BASE = 0x50000000


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def scenario_offset(data):
    tag_data = u32(data, 0x10)
    scenario_handle = u32(data, tag_data + 0x04)
    instance = tag_data + 0x28 + (scenario_handle & 0xFFFF) * 0x20
    return tag_data, tag_data + u32(data, instance + 0x14) - BASE


def script_parameter(data):
    tag_data, scenario = scenario_offset(data)
    size, address = u32(data, scenario + 0x474), u32(data, scenario + 0x474 + 12)
    array = tag_data + address - BASE
    count = struct.unpack_from("<h", data, array + 0x2E)[0]
    nodes = array + 0x38
    for index in range(count):
        node = nodes + index * 20
        if struct.unpack_from("<H", data, node)[0]:
            struct.pack_into("<H", data, node + 6, 0x15)
            return "node %d of %d (syntax data %d bytes)" % (index, count, size)
    raise SystemExit("no script syntax node in use")


def bsp_signature(data):
    tag_data, scenario = scenario_offset(data)
    count, address = u32(data, scenario + 0x5A4), u32(data, scenario + 0x5A4 + 4)
    if not count:
        raise SystemExit("no structure BSP")
    reference = tag_data + address - BASE
    file_offset = u32(data, reference)
    struct.pack_into("<I", data, file_offset + 0x14, 0x6E6F7065)
    return "structure BSP at 0x%x" % file_offset


def main():
    what, path = sys.argv[1], sys.argv[2]
    with open(path, "rb") as stream:
        data = bytearray(stream.read())
    if data[0:4] != b"daeh" or u32(data, 4) != 13:
        raise SystemExit("not a version 13 cache")
    said = {"script-parameter": script_parameter, "bsp-signature": bsp_signature}[what](data)
    with open(path, "wb") as stream:
        stream.write(data)
    print("%s: %s %s" % (path, what, said))


if __name__ == "__main__":
    main()
