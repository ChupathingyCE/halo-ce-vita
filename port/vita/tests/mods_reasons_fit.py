#!/usr/bin/env python3
"""The mod manager's reasons fit their buffers, in English and in every shipped language (1.1.3 8v audit: the
reason for a Custom Edition map's missing bitmaps.map, sounds.map and loc.map was 111 bytes with its "Not supported on
PS Vita: " - 106 in Spanish -, in custom_edition_mods.c's 96-byte buffers: "in the maps folder" was lost).

  - custom_edition_cache.c's mods reasons (between "/* mods reasons: */" and "/* :mods reasons */"): each at most 69
    bytes, and with the "Not supported on PS Vita: %s" before it at most 95 (MOD_PROBLEM_BYTES 96, its terminator);
  - custom_edition_mods.c's mod_problem(mod, T("...")) words: at most 95 bytes.

Run by run_lang_test.sh: mods_reasons_fit.py --root <tree>."""
import argparse
import glob
import os
import re
import sys

STRING = r'"((?:[^"\\]|\\.)*)"'


def unescape(text):
    return bytes(text, "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")


def language(path):
    table = {}
    for line in open(path, encoding="utf-8"):
        match = re.match(r'^' + STRING + r' = ' + STRING + r'\s*$', line)
        if match:
            table[unescape(match.group(1))] = unescape(match.group(2))
    return table


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    args = parser.parse_args()
    game = os.path.join(args.root, "port", "linux", "game")
    cache = open(os.path.join(game, "custom_edition_cache.c"), encoding="utf-8").read()
    mods = open(os.path.join(game, "custom_edition_mods.c"), encoding="utf-8").read()
    block = re.search(r"/\* mods reasons: \*/(.*?)/\* :mods reasons \*/", cache, re.S)
    if not block:
        print("FAIL: no mods reasons table in custom_edition_cache.c")
        return 1
    reasons = [unescape(s) for s in re.findall(r"N_\(" + STRING + r"\)", block.group(1))]
    problems = [unescape(s) for s in re.findall(r"mod_problem\(mod, T\(" + STRING + r"\)\)", mods)]
    prefix = "Not supported on PS Vita: %s"
    if prefix not in mods or len(reasons) < 5 or len(problems) < 5:
        print("FAIL: the reasons (%d), the problems (%d) or %r not found" % (len(reasons), len(problems), prefix))
        return 1
    languages = {"en": {}}
    for path in sorted(glob.glob(os.path.join(args.root, "port", "vita", "app0", "lang", "*.txt"))):
        languages[os.path.basename(path)[:-4]] = language(path)
    failed = 0
    for name, table in languages.items():
        said = lambda english: table.get(english, english)
        for reason in reasons:
            text = said(reason)
            whole = said(prefix).replace("%s", text, 1)
            if len(text.encode()) > 69 or len(whole.encode()) > 95:
                print("FAIL %s: %d bytes (%d with the prefix): %s" % (name, len(text.encode()), len(whole.encode()), text))
                failed += 1
        for problem in problems:
            text = said(problem)
            if len(text.encode()) > 95:
                print("FAIL %s: a problem of %d bytes: %s" % (name, len(text.encode()), text))
                failed += 1
    print("%s mods reasons: %d reasons, %d problems, %s: %d too long" % ("PASS" if not failed else "FAIL", len(reasons),
          len(problems), ", ".join(languages), failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
