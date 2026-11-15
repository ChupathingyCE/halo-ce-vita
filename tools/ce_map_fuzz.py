#!/usr/bin/env python3
"""Builds and runs the Custom Edition map fuzzer (tools/ce_map_fuzz.c).

The fuzzer is the tag validator's tool (tools/map_validate.c) as a libFuzzer
target, built from the objects `ninja build/linux/map_validate` makes - the
same sources and flags - compiled again under AddressSanitizer and
UndefinedBehaviourSanitizer into build/linux/ce_map_fuzz/. Needs a configured
tree (python3 configure.py) and clang with its 32-bit compiler-rt.

    tools/ce_map_fuzz.py build
    tools/ce_map_fuzz.py seeds DIR             (the tests' small maps, as files)
    tools/ce_map_fuzz.py run --resources DIR [--maps MAP...] [--seconds N]
                           [--corpus DIR] [--jobs N] [-- libFuzzer options]

With --maps the inputs are changes to those maps (real ones: 25-110 MB);
without, the inputs are map files (seeds from `seeds`). --resources is the
folder of Custom Edition's bitmaps.map, sounds.map and loc.map.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/linux/ce_map_fuzz"
BINARY = OUT / "ce_map_fuzz"
SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=undefined", "-fno-omit-frame-pointer"]


def build():
    subprocess.run(["ninja", "-C", str(ROOT), "build/linux/halo_msvc_semantics.h",
                    "build/linux/platform_msvc_semantics.h", "build/linux/map_validate"], check=True)
    commands = subprocess.run(["ninja", "-C", str(ROOT), "-t", "commands", "build/linux/map_validate"],
                              check=True, capture_output=True, text=True).stdout.splitlines()
    objects = []
    for line in commands:
        words = shlex.split(line)
        if "-c" not in words:
            continue
        source = words[words.index("-c") + 1]
        if source == "tools/map_validate.c":
            source = "tools/ce_map_fuzz.c"
        obj = OUT / "obj" / Path(source).with_suffix(".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        flags = []
        skip = 0
        for index, word in enumerate(words[1:], 1):
            if skip:
                skip -= 1
                continue
            if word in ("-MF", "-o", "-c"):
                skip = 1
                continue
            # (the build's link-time optimisation and profile are not the
            # fuzzer's: plain objects, linked as they are)
            if word == "-MMD" or word.startswith(("-flto", "-fprofile", "-fthinlto", "-Wl,--thinlto")):
                continue
            flags.append("-O1" if word == "-O2" else word)
        compile_line = [words[0], *flags, *SANITIZE, "-fsanitize=fuzzer-no-link", "-c", source, "-o", str(obj)]
        subprocess.run(compile_line, cwd=ROOT, check=True)
        objects.append(str(obj))
    subprocess.run(["clang", "--target=i686-linux-gnu", "-m32", "-no-pie", "-g", *SANITIZE, *libfuzzer(),
                    *objects, "-lm", "-o", str(BINARY)], cwd=ROOT, check=True)
    print(BINARY)


def libfuzzer():
    """libFuzzer for i386: as it is, or with its objects' COMDAT groups taken
    out (clang 22's archive refers to __x86.get_pc_thunk sections the linker
    discards; port/vita/tests/run_net_fuzz_test.sh does the same)"""
    probe = OUT / "probe.c"
    probe.write_text("int LLVMFuzzerTestOneInput(const char *d, unsigned long n) { return 0; }\n")
    if subprocess.run(["clang", "-m32", "-fsanitize=fuzzer", str(probe), "-o", str(OUT / "probe")],
                      capture_output=True).returncode == 0:
        return ["-fsanitize=fuzzer"]
    archive = subprocess.run(["clang", "-m32", "-print-file-name=libclang_rt.fuzzer-i386.a"], check=True,
                             capture_output=True, text=True).stdout.strip()
    work = OUT / "libfuzzer"
    work.mkdir(parents=True, exist_ok=True)
    subprocess.run(["ar", "x", archive], cwd=work, check=True)
    localize = [f"--localize-symbol=__x86.get_pc_thunk.{r}" for r in ("ax", "bx", "cx", "dx", "si", "di", "bp")]
    members = sorted(p.name for p in work.glob("*.o"))
    for member in members:
        subprocess.run(["objcopy", "--remove-section=.group", *localize, member], cwd=work, check=True)
    library = OUT / "libfuzzer32.a"
    library.unlink(missing_ok=True)
    subprocess.run(["ar", "rcs", str(library), *members], cwd=work, check=True)
    return [str(library), "-lstdc++"]


def seeds(directory):
    """the maps tools/test_cache_file_formats.py builds, written as files"""
    sys.path.insert(0, str(ROOT / "tools"))
    import test_cache_file_formats as tests  # noqa: E402

    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    count = 0
    for name in dir(tests):
        if not name.startswith("build_") or not callable(getattr(tests, name)):
            continue
        try:
            made = getattr(tests, name)()
        except TypeError:
            continue
        except Exception:  # (a builder that wants arguments of other kinds)
            continue
        if isinstance(made, (bytes, bytearray)) and len(made) >= 0x800:
            (directory / f"{name}.map").write_bytes(bytes(made))
            count += 1
    print(f"{count} maps in {directory}")


def run(args, extra):
    env = dict(os.environ)
    env["CE_MAP_FUZZ_RESOURCES"] = str(Path(args.resources).resolve())
    if args.maps:
        env["CE_MAP_FUZZ_SEEDS"] = ":".join(str(Path(m).resolve()) for m in args.maps)
    corpus = Path(args.corpus)
    corpus.mkdir(parents=True, exist_ok=True)
    artifacts = corpus.parent / (corpus.name + "-found")
    artifacts.mkdir(parents=True, exist_ok=True)
    # (32-bit ASan: the maps are held whole, so no RSS limit; leaks are found)
    command = [str(BINARY), f"-max_total_time={args.seconds}", "-rss_limit_mb=0", "-timeout=120",
               f"-artifact_prefix={artifacts}/", "-print_final_stats=1",
               "-max_len=" + ("289" if args.maps else str(4 << 20))]
    if args.jobs > 1:
        command += [f"-jobs={args.jobs}", f"-workers={args.jobs}"]
    command += [*extra, str(corpus), *args.seed_dirs]
    env.setdefault("ASAN_OPTIONS", "detect_leaks=1:allocator_may_return_null=1:handle_segv=1")
    env.setdefault("UBSAN_OPTIONS", "print_stacktrace=1:halt_on_error=1")
    return subprocess.run(command, env=env).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    sub.add_parser("build")
    seeds_parser = sub.add_parser("seeds")
    seeds_parser.add_argument("directory")
    run_parser = sub.add_parser("run")
    run_parser.add_argument("--resources", required=True)
    run_parser.add_argument("--maps", nargs="*", default=[])
    run_parser.add_argument("--seconds", type=int, default=600)
    run_parser.add_argument("--corpus", default=str(OUT / "corpus"))
    run_parser.add_argument("--jobs", type=int, default=1)
    run_parser.add_argument("--seed-dirs", nargs="*", default=[])
    argv = sys.argv[1:]
    extra = []
    if "--" in argv:
        extra = argv[argv.index("--") + 1:]
        argv = argv[:argv.index("--")]
    args = parser.parse_args(argv)
    if args.mode == "build":
        build()
    elif args.mode == "seeds":
        seeds(args.directory)
    else:
        sys.exit(run(args, extra))


if __name__ == "__main__":
    main()
