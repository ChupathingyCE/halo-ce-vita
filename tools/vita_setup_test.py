#!/usr/bin/env python3
"""The first start's flow and screens (port/vita/setup) on Linux: the
harness (port/vita/tests/setup_test.c) built with the build machine's C
compiler and run through each case, its pictures kept as PNG files.

    python3 tools/vita_setup_test.py WORK SHOTS [--maps FOLDER]

WORK is a scratch folder of the test's own (its disc images and data
folders are made there and deleted as each case ends); SHOTS gets the
pictures. --maps is a folder of the Xbox maps (read only) for the cases
that copy a whole disc's maps; without it they use made-up maps.

Cases: nothing found; another app's maps folder chosen and kept; a disc
image copied (the loading picture kept as xbox_loading.tga); a copy
cancelled part way; too little space; discs that aren't Halo's Xbox disc
(another title, Halo PC's maps, a CD image); the update screens; Japan's
buttons (circle confirms).
"""

import argparse
import filecmp
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "chupathingyce"
OVERRIDES = ROOT / "port" / "vita" / "overrides" / "port" / "linux" / "src"
XISO_TOOL = ROOT / "tools" / "vita_test_xiso.py"

FAILURES = []


def check(condition, what):
    print(("ok    " if condition else "FAIL  ") + what)
    if not condition:
        FAILURES.append(what)


def build(work):
    folder = work / "build"
    folder.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(ROOT / "tools" / "vita_setup_fonts.py"), str(GAME),
                    str(folder / "setup_fonts.c")], check=True)
    program = folder / "setup_test"
    subprocess.run(["cc", "-std=gnu11", "-D_GNU_SOURCE", "-DHALO_EXTRACTOR_STANDALONE", "-O1", "-g", "-w",
                    "-I", str(ROOT / "port/vita/setup"), "-I", str(OVERRIDES), "-I", str(GAME / "port/linux/src"),
                    "-I", str(GAME / "port/third_party/stb"),
                    str(ROOT / "port/vita/tests/setup_test.c"), str(ROOT / "port/vita/setup/setup_flow.c"),
                    str(ROOT / "port/vita/setup/setup_draw.c"), str(folder / "setup_fonts.c"),
                    str(OVERRIDES / "xiso.c"), str(GAME / "port/linux/src/posix_files.c"),
                    "-Wl,--wrap=xiso_extract_maps_with", "-lm", "-lpthread", "-o", str(program)], check=True)
    return program


def make_image(path, *arguments):
    subprocess.run([sys.executable, str(XISO_TOOL), str(path), *arguments], check=True)
    return path


class Case:
    def __init__(self, program, work, shots, name):
        self.program = program
        self.shots = shots
        self.name = name
        self.folder = work / name
        if self.folder.exists():
            shutil.rmtree(self.folder)
        self.data = self.folder / "warthog-vita"
        self.card = self.folder / "card"
        self.data.mkdir(parents=True)
        self.card.mkdir()

    def run(self, script, maps=(), images=None, environment=None, mode="maps"):
        shots = self.folder / "shots"
        shots.mkdir(exist_ok=True)
        command = [str(self.program), mode]
        if mode == "maps":
            command += [str(self.data), str(shots), script]
            for folder, label in maps:
                command += ["--maps", str(folder), label]
            for folder in (images if images is not None else [self.data, self.card]):
                command += ["--images", str(folder)]
        else:
            command += [str(shots), script]
        env = dict(os.environ)
        env.update(environment or {})
        result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=900)
        (self.folder / "log.txt").write_text(result.stderr)
        for picture in shots.glob("*.png"):
            shutil.copy(picture, self.shots / f"linux-{self.name}-{picture.name}")
        lines = dict(line.split(" ", 1) for line in result.stdout.splitlines() if " " in line)
        self.log = result.stderr
        return lines

    def written(self):
        return sorted(str(path.relative_to(self.data)) for path in self.data.rglob("*"))

    def finish(self):
        shutil.rmtree(self.folder)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("work", type=Path)
    parser.add_argument("shots", type=Path)
    parser.add_argument("--maps", type=Path)
    arguments = parser.parse_args()
    work = arguments.work.resolve()
    shots = arguments.shots.resolve()
    shots.mkdir(parents=True, exist_ok=True)
    program = build(work)
    xbox_maps = arguments.maps.resolve() if arguments.maps else None

    # nothing on the card: the maps-not-found screen, looked at again, quit
    case = Case(program, work, shots, "nothing")
    result = case.run("wait:150,shot:missing,press:triangle,wait:150,press:circle")
    check(result.get("result") == "quit", "nothing found: the missing maps screen, then quit")
    check("setup: looking again" in case.log, "nothing found: Triangle looks again")
    check(case.written() == [], "nothing found: nothing written")
    case.finish()

    # Halo CE Vita's maps where they are: chosen, kept, then no screen
    case = Case(program, work, shots, "elsewhere")
    other = case.card / "haloce-vita" / "maps"
    other.mkdir(parents=True)
    if xbox_maps:
        for name in ("ui.map", "bloodgulch.map"):
            os.symlink(xbox_maps / name, other / name)
    else:
        subprocess.run([sys.executable, "-c", "import sys; sys.path.insert(0, sys.argv[1]); import vita_test_xiso as x; "
                        "open(sys.argv[2], 'wb').write(x.fake_map('ui'))", str(ROOT / "tools"),
                        str(other / "ui.map")], check=True)
    maps = [(other, "Halo CE Vita's maps")]
    result = case.run("", maps)
    check(result.get("result") == f"elsewhere {other}" and "the only maps on the card" in case.log,
          "another app's maps alone: read where they are, no screen")
    check(case.written() == [], "another app's maps alone: nothing written")
    # with a disc image beside them: the choice, kept
    make_image(case.card / "halo.iso", "--fake")
    result = case.run("wait:150,shot:choices,press:cross", maps)
    check(result.get("result") == f"elsewhere {other}", "another app's maps: chosen, read where they are")
    check((case.data / "maps_source.txt").read_text().strip() == str(other), "another app's maps: the choice kept")
    result = case.run("", maps)
    check(result.get("result") == f"elsewhere {other}" and "the player's choice" in case.log,
          "another app's maps: no screen the next time")
    check(case.written() == ["maps_source.txt"], "another app's maps: only the choice written")
    case.finish()

    # a disc image copied
    case = Case(program, work, shots, "copy")
    if xbox_maps:
        image = make_image(case.card / "halo.iso", "--maps", str(xbox_maps), "--loading-picture")
    else:
        image = make_image(case.card / "halo.iso", "--fake", "--fake-size", str(64 << 20), "--loading-picture")
    result = case.run("wait:150,shot:choices,press:cross,waitpartial:200,wait:400,shot:copying,"
                      "waitlog:the maps copied,wait:300,shot:done,press:cross",
                      maps=[(case.card / "haloce-vita" / "maps", "Halo CE Vita's maps")],
                      environment={"SETUP_TEST_MB_PER_S": "400", "SETUP_TEST_FREE_MB": "64000"})
    check(result.get("result") == "own", "disc image: copied, the game reads its own maps")
    names = case.written()
    check("maps/ui.map" in names and "maps/xbox_loading.tga" in names and "maps/loading.tga" not in names,
          "disc image: maps and the loading picture as xbox_loading.tga")
    check(not any(name.startswith("maps.partial") for name in names), "disc image: no maps.partial left")
    if xbox_maps:
        same = all(filecmp.cmp(xbox_maps / name, case.data / "maps" / name, shallow=False)
                   for name in sorted(path.name for path in xbox_maps.iterdir()))
        check(same, "disc image: every map the same as the disc's")
    check("busy 1" in case.log and "busy 0" in case.log, "disc image: the system held awake while copying")
    result = case.run("")
    check(result.get("result") == "own" and "looking on the memory card" not in case.log,
          "disc image: no screen the next time")
    case.finish()

    # a copy cancelled part way: nothing left
    case = Case(program, work, shots, "cancel")
    if xbox_maps:
        make_image(case.card / "halo.iso", "--maps", str(xbox_maps))
    else:
        make_image(case.card / "halo.iso", "--fake", "--fake-size", str(256 << 20))
    result = case.run("wait:150,press:cross,waitpartial:300,press:circle,wait:200,shot:cancel-ask,press:cross,"
                      "waitlog:the copy stopped,wait:300,shot:cancelled,press:circle",
                      environment={"SETUP_TEST_MB_PER_S": "150"})
    check(result.get("result") == "quit", "cancel: back to the choices, then quit")
    check("Cancelled." in case.log, "cancel: the copy stopped")
    check(case.written() == [], "cancel: nothing left in the data folder")
    case.finish()

    # too little space
    case = Case(program, work, shots, "space")
    make_image(case.card / "halo.iso", "--fake", "--fake-size", str(300 << 20))
    result = case.run("wait:150,press:cross,wait:200,shot:no-space,press:cross,wait:150,press:circle",
                      environment={"SETUP_TEST_FREE_MB": "500"})
    check(result.get("result") == "quit" and "and it has 500 MB free" in case.log, "no space: said, then back")
    check("copying the maps" not in case.log, "no space: no copy started")
    check(case.written() == [], "no space: nothing written")
    case.finish()

    # discs that aren't Halo's Xbox disc
    case = Case(program, work, shots, "refused")
    make_image(case.card / "another-game.iso", "--fake", "--title-id", "0x4D530064")
    make_image(case.card / "halo-pc-maps.xiso", "--fake", "--ui-version", "7")
    cd = case.card / "music-cd.iso"
    cd.write_bytes(b"\0" * 0x8000 + b"\x01CD001\x01" + b"\0" * 0x20000)
    result = case.run("wait:150,shot:missing-refused,press:circle")
    check(result.get("result") == "quit", "refused: the missing maps screen")
    check("another-game.iso: This isn't a Halo: Combat Evolved disc for the original Xbox." in case.log,
          "refused: another title's disc")
    check("halo-pc-maps.xiso: The disc image's maps are Halo PC's, not the Xbox's." in case.log,
          "refused: Halo PC's maps")
    check("music-cd.iso: This is not an Xbox disc image." in case.log, "refused: not an Xbox disc image")
    # with Halo's disc beside them: the list, the others dimmed
    make_image(case.card / "halo.iso", "--fake")
    result = case.run("wait:150,shot:choices-mixed,press:up,press:up,wait:100,press:cross,wait:200,"
                      "shot:cant-use,press:cross,wait:100,press:circle")
    check(result.get("result") == "quit", "refused: listed dimmed beside Halo's disc")
    check(case.written() == [], "refused: nothing written")
    case.finish()

    # the update screens (the hook: no download)
    case = Case(program, work, shots, "update")
    result = case.run("wait:150,shot:question,press:circle,shot:progress,wait:100,shot:notice,press:cross",
                      mode="update")
    check(result.get("answer") == "0", "update: Circle is not now")
    case.finish()

    # Japan's buttons: circle confirms
    case = Case(program, work, shots, "circle-confirms")
    make_image(case.card / "halo.iso", "--fake")
    result = case.run("wait:150,shot:choices,press:cross", environment={"SETUP_TEST_CIRCLE_CONFIRMS": "1"})
    check(result.get("result") == "quit", "circle confirms: cross is back")
    case.finish()

    shutil.rmtree(work / "build")
    print(f"{len(FAILURES)} failed" if FAILURES else "all passed")
    sys.exit(1 if FAILURES else 0)


if __name__ == "__main__":
    main()
