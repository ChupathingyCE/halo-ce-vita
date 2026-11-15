#!/usr/bin/env python3
"""Compare two HALO_TICK_HASH runs (port/linux/game/tick_hash.c).

usage: tick_hash_compare.py A B   (A and B are the HALO_TICK_HASH paths)

Prints the first tick the whole-state hashes differ at (or that they agree
over the ticks both runs reached) and, from the per-allocation files, each
allocation that differs with the tick it first differs at, earliest first.
--ignore NAME (repeatable) leaves an allocation out of the verdict."""
import argparse
import struct
import sys


def clean(index, name):
    """An allocation's name as the game gave it; one whose name is not printable text (a name
    pointer into memory nobody set: a run's own bytes, different every run) is named by its
    place in the list instead, so two runs still compare."""
    try:
        text = name.decode("ascii")
    except UnicodeDecodeError:
        text = None
    if text is None or not text or not all(" " <= c <= "~" for c in text):
        return "<allocation %d, no printable name>" % index
    return text


def load(path):
    with open(path + ".names", "rb") as lines:
        names = [clean(index, line.rstrip(b"\r\n").rsplit(b" ", 1)[0]) for index, line in enumerate(lines)]
    raw = open(path + ".alloc", "rb").read()
    record = 4 + 8 * len(names)
    ticks = {}
    for offset in range(0, len(raw) - record + 1, record):
        time = struct.unpack_from("<I", raw, offset)[0]
        ticks[time] = struct.unpack_from("<%dQ" % len(names), raw, offset + 4)
    return names, ticks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("a")
    parser.add_argument("b")
    parser.add_argument("--ignore", action="append", default=[])
    args = parser.parse_args()
    names_a, ticks_a = load(args.a)
    names_b, ticks_b = load(args.b)
    if names_a != names_b:
        sys.exit("the runs allocated different game states")
    common = sorted(set(ticks_a) & set(ticks_b))
    first = {}
    for time in common:
        for index, (x, y) in enumerate(zip(ticks_a[time], ticks_b[time])):
            if x != y and index not in first:
                first[index] = time
    print("ticks compared: %d (%d..%d)" % (len(common), common[0] if common else 0, common[-1] if common else 0))
    verdict = [i for i in first if names_a[i] not in args.ignore]
    for index in sorted(first, key=lambda i: first[i]):
        print("  %-40s first differs at tick %d%s" % (names_a[index], first[index],
              " (ignored)" if names_a[index] in args.ignore else ""))
    print("IDENTICAL" if not verdict else "DIFFERENT")
    return 0 if not verdict else 1


if __name__ == "__main__":
    sys.exit(main())
