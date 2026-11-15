#!/usr/bin/env python3
"""The Nov 3 manifest probe (DESIGN.md section 1), kept as the reference the C
manifest parser (port/steamdl/steam_manifest.c) is checked against: an
independent, hand-written protobuf reader over a Steam depotcache manifest
(<depot>_<gid>.manifest, not encrypted), printing a "#" line, then each file as
name<TAB>size<TAB>flags<TAB>chunks<TAB>compressed bytes, the form
port/vita/tests/steam_manifest_dump.c prints (its first five columns).
run_steam_download_test.sh diffs the two for every local manifest of the
depots Project Ark downloads. Reads only; prints no secret."""
import struct
import sys

PAYLOAD, METADATA, SIGNATURE, END = 0x71F617D0, 0x1F4812BE, 0x1B81B817, 0x32C415AB


def varint(b, i):
    r = s = 0
    while True:
        c = b[i]
        i += 1
        r |= (c & 0x7F) << s
        s += 7
        if not c & 0x80:
            return r, i


def fields(b):
    i, n = 0, len(b)
    while i < n:
        key, i = varint(b, i)
        f, t = key >> 3, key & 7
        if t == 0:
            v, i = varint(b, i)
        elif t == 1:
            v = struct.unpack_from("<Q", b, i)[0]
            i += 8
        elif t == 5:
            v = struct.unpack_from("<I", b, i)[0]
            i += 4
        elif t == 2:
            ln, i = varint(b, i)
            if i + ln > n:
                raise ValueError("length past end")
            v = b[i:i + ln]
            i += ln
        else:
            raise ValueError("wire type %d" % t)
        yield f, v


def sections(data):
    i = 0
    while i + 4 <= len(data):
        magic = struct.unpack_from("<I", data, i)[0]
        i += 4
        if magic == END:
            return
        ln = struct.unpack_from("<I", data, i)[0]
        i += 4
        yield magic, data[i:i + ln]
        i += ln


def parse(path):
    with open(path, "rb") as reader:
        data = reader.read()
    files, meta = [], {}
    for magic, body in sections(data):
        if magic == METADATA:
            meta = dict(fields(body))
        elif magic == PAYLOAD:
            for f, v in fields(body):
                if f != 1:
                    continue
                name, size, flags, chunks, comp = "", 0, 0, 0, 0
                for g, w in fields(v):
                    if g == 1:
                        name = w.rstrip(b"\0").decode("utf-8", "replace")
                    elif g == 2:
                        size = w
                    elif g == 3:
                        flags = w
                    elif g == 6:
                        chunks += 1
                        comp += dict(fields(w)).get(5, 0)
                files.append((name.replace("\\", "/"), size, flags, chunks, comp))
    return meta, files


def main(paths):
    for path in paths:
        meta, files = parse(path)
        if meta.get(4):
            sys.exit("%s: names encrypted (not a depotcache manifest)" % path)
        print("#")
        for name, size, flags, chunks, comp in files:
            print("%s\t%d\t%d\t%d\t%d" % (name, size, flags, chunks, comp))


if __name__ == "__main__":
    main(sys.argv[1:])
