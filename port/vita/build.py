"""Ninja rules for the PlayStation Vita build (``ninja vita``): the port's
build.py, which the game's configure.py loads (the game's
tools/console_build.py). From this repository's root, with the game in
chupathingyce/ (a submodule):

    python3 chupathingyce/configure.py --console-port port/vita
    ninja vita

Target: the Vita's Cortex-A9 (ARMv7, 32-bit, little-endian), producing
build/vita/warthog.vpk.

Two compilers, as the Vita build has always had them. The game, the
platform layer it shares with the desktop (the game's port/linux/src) and
the port's own units in the platform layer's terms (port/vita/platform) are
compiled by clang for ARMv7 with the game's MSVC-compatible ABI: 32-bit
enums, 16-bit wchar_t, signed char, and no alignment assumed for pointer
accesses (the game reads floats from 2-byte aligned buffers, which the
Cortex-A9's VLDR faults on). The Vita side (port/vita/host) is compiled by
VitaSDK's GCC with the SDK's own ABI (small enums, 32-bit wchar_t), because
the Sce headers' structures depend on it; the two halves meet only through
port/vita/include/vita_host.h, whose parameters are 32-bit scalars and
pointers. VitaSDK's GCC links the whole, against its newlib, SDL3 and the
system's stub libraries.
"""

import os
import re
import shlex
from pathlib import Path
from typing import Any, List, Optional, Set

from tools.console_build import ConsolePort, console_defines, console_https_flags, console_https_sources
from tools.ninja_syntax import Writer
from tools.version import base_version, version

# the game's own folders, under the game's directory
XDK_INCLUDE = Path("port/include/xdk")
LINUX_INCLUDE = Path("port/linux/include")
LINUX_SRC = Path("port/linux/src")
LINUX_GAME = Path("port/linux/game")
CONSOLE_INCLUDE = Path("port/console/include")
CONSOLE_GAME = Path("port/console/game")
CONSOLE_LINK = Path("port/console/link")
TOML_DIR = Path("port/third_party/tomlc17")
EXPAT_DIR = Path("port/third_party/expat")
EXPAT_SOURCES = ("xmlparse.c", "xmlrole.c", "xmltok.c")
KCP_DIR = Path("port/third_party/kcp")
QRCODEGEN_DIR = Path("port/third_party/qrcodegen")
MONOCYPHER_DIR = Path("port/third_party/monocypher")
MUSL_MATH_DIR = Path("port/third_party/musl-math")
STB_DIR = Path("port/third_party/stb")
ZLIB_DIR = Path("port/third_party/zlib")
ZLIB_SOURCES = ("adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "uncompr.c", "zutil.c")
ZLIB_DEFINES = ("-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg")

# The app's title ID and data folder. A release build (configure.py
# --release) is the release's: WRTV00001 and ux0:data/warthog-vita. Any other
# build is a dev build, WRTD00001 and ux0:data/warthog-vita-dev, so a tester
# keeps the release and its saves beside it. Neither is Halo CE Vita's
# (HCEV00001, ux0:data/haloce-vita). --vita-title-id and --vita-data-folder
# choose others.
RELEASE_TITLE_ID = "WRTV00001"
RELEASE_DATA_DIRECTORY = "ux0:data/warthog-vita"
DEV_TITLE_ID = "WRTD00001"
DEV_DATA_DIRECTORY = "ux0:data/warthog-vita-dev"
TITLE = "Warthog"
DEV_TITLE = "Warthog Dev"

# the CPU and the game's MSVC ABI on 32-bit ARM (clang)
TARGET_FLAGS = [
    "--target=armv7a-none-eabihf",
    "-mcpu=cortex-a9",
    "-mfpu=neon",
    "-mfloat-abi=hard",
    "-mthumb",
    "-D__vita__",
]

ABI_FLAGS = [
    # sqrtf and friends inline as VFP instructions instead of libm calls that
    # would set errno (the results are the same: IEEE exact)
    "-fno-math-errno",
    "-fms-extensions",
    "-fshort-wchar",
    "-fno-short-enums",
    "-fsigned-char",
    # the game reads floats through pointers of no particular alignment
    # (2-byte aligned tag data, byte arrays standing in for structures): the
    # Cortex-A9 faults on an unaligned VLDR, so clang assumes none
    "-fmax-type-align=1",
    "-femulated-tls",
    "-fcommon",
    "-fno-strict-aliasing",
    "-fwrapv",
    "-fno-delete-null-pointer-checks",
    "-fno-omit-frame-pointer",
    # the same floating point results on every port: no fused multiply-adds
    "-ffp-contract=off",
    "-ffunction-sections",
    "-fdata-sections",
    "-O2",
    "-g",
    # newlib's wide string functions assume a 32-bit wchar_t
    *(f"-fno-builtin-{name}" for name in (
        "wcslen", "wcsnlen", "wcschr", "wcsrchr", "wcscmp", "wcsncmp", "wcscpy",
        "wcsncpy", "wcscat", "wcsncat", "wmemchr", "wmemcmp", "wmemcpy",
        "wmemmove", "wmemset",
    )),
]

# what the Vita asks of the shared code (the game's
# port/linux/include/halo_console_features.h lists each capability)
CONSOLE_FEATURES = [
    "-DHALO_CONSOLE",
    "-DHALO_VITA_CONSOLE",
    # (the Vita side's own switch, as Halo CE Vita's code has it: the game
    # does not test it)
    "-DHALO_VITA=1",
    # the Xbox 360's layout: Xbox addresses translated into a window wherever
    # the kernel puts it (the Vita's program and heap are where the Xbox's
    # window was linked); the game's halo_console_features.h sizes it for
    # the Vita (HALO_CONSOLE_WINDOW_LOW)
    "-DHALO_64BIT",
    "-DHALO_WINDOW_TRANSLATED",
    # Delta Peer as a client, the Vita's platform number (11, the registry's)
    "-DHALO_CONSOLE_DELTA",
    "-DHALO_DELTA_PLATFORM=_delta_platform_vita",
    # the menus' map list and the Xbox map packs
    "-DHALO_MAP_LIST",
    # no page protection for a user application
    "-DHALO_CONSOLE_NO_WRITE_WATCH",
    "-DHALO_CONSOLE_PARTICLE_TICKS",
    "-DHALO_CONSOLE_LIGHT_LIMITS",
    "-DHALO_CONSOLE_LOG_THROTTLE",
    # a Direct3D device of its own (platform/d3d8_gxm.c): the menus' art and the
    # high-res HUD read by it, not uploaded through OpenGL
    "-DHALO_CONSOLE_HOST_ART",
    # the Chupathingy menus' rows marked for a console (no QUIT, no duct
    # tape), and the Vita's own where a menu file names it
    "'-DHALO_CONSOLE_MENU_PLATFORM=\"vita\"'",
    # config.toml in the data folder (HALO_DATA_ROOT, host/vita_main.c)
    "-DHALO_CONSOLE_CONFIG_IN_DATA_ROOT",
]

# the game's C89, as MSVC took it
GAME_FLAGS = [
    "-std=gnu89",
    "-D__STRICT_ANSI__",
    "-w",
    "-Wno-error=incompatible-pointer-types",
    "-Wno-error=incompatible-function-pointer-types",
    "-Wno-error=int-conversion",
    "-Wno-error=implicit-function-declaration",
    "-Wno-error=implicit-int",
    "-Wno-error=return-type",
]

PLATFORM_FLAGS = [
    "-std=gnu11",
    "-D_GNU_SOURCE",
    "-DHALO_LINUX_PLATFORM_LAYER",
    "-w",
    "-Werror=incompatible-pointer-types",
    "-Werror=int-conversion",
    "-Werror=implicit-function-declaration",
]

# the Vita side: VitaSDK's GCC with the SDK's ABI
HOST_FLAGS = [
    "-mcpu=cortex-a9",
    "-mfpu=neon",
    "-mthumb",
    "-O2",
    "-g",
    "-Wall",
    "-Wno-unused-function",
    "-ffunction-sections",
    "-fdata-sections",
    "-D__vita__",
    "-DHALO_VITA_CONSOLE",
    "-DHALO_VITA=1",
]

# the desktop platform layer's units the Vita does its own way, or has no
# use for
REPLACED_PLATFORM_SOURCES = {
    # the window, the input and the touch screen (platform/vita_platform.c,
    # vita_input.c; dsound_sdl.c's mixer stays, over VitaSDK's SDL3)
    "sdl_platform.c", "xinput_sdl.c", "touch_input.c", "touch_menu.c",
    # the window and the OpenGL renderer: the Vita's own Direct3D device on
    # GXM (platform/d3d8_gxm.c and its texture and shader units)
    "d3d8_gl.c", "gl_functions.c", "nv2a_vsh.c", "nv2a_psh.c", "xbox_textures.c",
    # page protection and its fault handler (platform/vita_memory_watch.c)
    "memory_watch.c",
    # the sockets (host/vita_net.c)
    "posix_net.c",
    # the desktop crash reports and the self-updater
    "posix_crash.c", "posix_update.c", "updater.c", "update_signature.c",
    # UPnP and the desktop game browser's HTTPS
    "posix_upnp.c", "posix_browser.c",
}

VITA_LIBRARIES = [
    "SDL3", "SceGxm_stub", "SceDisplay_stub", "SceCtrl_stub", "SceAudio_stub", "SceAudioIn_stub",
    "SceSysmodule_stub", "SceShellSvc_stub", "SceHid_stub", "SceTouch_stub", "SceMotion_stub", "ScePower_stub",
    "SceAppUtil_stub", "SceAppMgr_stub", "SceCommonDialog_stub", "SceIme_stub", "SceKernelDmacMgr_stub",
    "SceShaccCg_stub", "SceAvPlayer_stub", "SceCamera_stub", "SceRtc_stub", "SceNet_stub", "SceNetCtl_stub", "ScePspnetAdhoc_stub",
    "SceLibKernel_stub", "SceKernelThreadMgr_stub", "pthread", "m", "c",
]


def _quote(path: Any) -> str:
    return shlex.quote(str(path).replace(os.sep, "/"))


def _vitasdk(sln: Any) -> Optional[Path]:
    for candidate in (getattr(sln.args, "vitasdk", None), os.environ.get("VITASDK"), Path.home() / "vitasdk"):
        if candidate and (Path(candidate) / "bin" / "arm-vita-eabi-gcc").is_file():
            return Path(candidate).expanduser().resolve()
    return None


def add_arguments(parser: Any) -> None:
    """the port's own configure.py options"""
    parser.add_argument("--vitasdk", metavar="DIR", type=Path,
                        help="VitaSDK for `ninja vita` (default: VITASDK or ~/vitasdk)")
    parser.add_argument("--vita-cc", metavar="BINARY", default="clang",
                        help="the clang that compiles the game for the Vita (default: clang)")
    parser.add_argument("--vita-title-id", metavar="ID",
                        help=f"the VPK's title ID (default {RELEASE_TITLE_ID} with --release, else {DEV_TITLE_ID})")
    parser.add_argument("--vita-data-folder", metavar="PATH",
                        help=f"the app's data folder on the card (default {RELEASE_DATA_DIRECTORY} with --release, "
                             f"else {DEV_DATA_DIRECTORY})")
    parser.add_argument("--vita-build-number", metavar="N", type=int, default=1,
                        help="the Vita's build count after the game's version, \"0.9.2 (build N)\" (default 1)")


def configure_inputs(port: ConsolePort) -> List[Path]:
    """Files whose change must re-run configure.py."""
    # (every folder of the port's: a file added in a subfolder of overrides/
    # changes that subfolder's time, not overrides/'s)
    folders = [port.path(name) for name in ("host", "platform", "include", "game", "overrides", "app0", "sce_sys")]
    folders += [path for folder in folders if folder.is_dir() for path in folder.rglob("*") if path.is_dir()]
    return [*folders, port.path("build.py"),
            port.game_path(LINUX_SRC), port.game_path(LINUX_GAME), port.game_path(CONSOLE_GAME),
            port.game_path("port", "linux", "port.json")]


def _missing(n: Writer, text: str) -> None:
    n.comment("PlayStation Vita build (ninja vita): not available")
    n.rule(name="vita_missing", command=f"echo {shlex.quote('ninja vita needs: ' + text)} && exit 1",
           description="VITA not configured")
    n.build(outputs="vita", rule="vita_missing")
    n.newline()


def vita_version(sln: Any) -> str:
    """the version the Vita says it is: the game's VERSION and the Vita's
    build count after it, as the other Warthog ports say theirs ("0.9.1a
    (build 7)" on the Switch)"""
    number = getattr(sln.args, "vita_build_number", 1)
    return f"{base_version()} (build {number})"


def generate(n: Writer, sln: Any, port: ConsolePort) -> None:
    sdk = _vitasdk(sln)
    if not sdk:
        _missing(n, "VitaSDK (--vitasdk DIR or VITASDK; port/vita/README.md)")
        return
    game = port.game
    port_dir = port.root
    TLSF_DIR = port_dir / "third_party" / "tlsf"
    sysroot = sdk / "arm-vita-eabi"
    build_dir: Path = sln.build_dir / "vita"
    obj_dir = build_dir / "obj"
    gen_dir = build_dir / "gen"
    prefix_header = port_dir / "include" / "halo_vita_prefix.h"
    semantics_header = gen_dir / "halo_msvc_semantics.h"
    platform_semantics_header = gen_dir / "platform_msvc_semantics.h"
    release = bool(getattr(sln, "port_release", False))
    title_id = getattr(sln.args, "vita_title_id", None) or (RELEASE_TITLE_ID if release else DEV_TITLE_ID)
    if not re.fullmatch(r"[A-Z]{4}\d{5}", title_id):
        raise SystemExit(f"--vita-title-id {title_id!r}: four capital letters and five digits")
    data_directory = getattr(sln.args, "vita_data_folder", None) or (
        RELEASE_DATA_DIRECTORY if release else DEV_DATA_DIRECTORY)
    if not re.fullmatch(r"ux0:data/[A-Za-z0-9_.-]{1,48}", data_directory):
        raise SystemExit(f"--vita-data-folder {data_directory!r}: a folder in ux0:data")
    title = TITLE if release else DEV_TITLE
    n.comment(f"Vita app: {title_id} ({title}), data in {data_directory}")

    n.comment("PlayStation Vita build (ninja vita); see port/vita/README.md")
    n.variable("vita_cc", getattr(sln.args, "vita_cc", "clang"))
    n.variable("vita_sdk_bin", _quote(sdk / "bin"))
    n.rule(name="vita_cc", command="$vita_cc -MMD -MF $out.d $cflags -c $in -o $out",
           description="VITA CC $out", depfile="$out.d", deps="gcc")
    n.rule(name="vita_host_cc", command="$vita_sdk_bin/arm-vita-eabi-gcc -MMD -MF $out.d $cflags -c $in -o $out",
           description="VITA HOST CC $out", depfile="$out.d", deps="gcc")
    n.rule(name="vita_msvc_semantics",
           command=f"$python {_quote(game / 'tools' / 'linux_msvc_semantics.py')} --output $out $scan",
           description="VITA MSVC SEMANTICS $out", restat=True)
    n.rule(name="vita_embed_assets", command=f"$python {_quote(game / 'tools' / 'embed_assets.py')} $out",
           description="VITA EMBED $out")

    game_headers = port.game_headers()
    xdk_headers = sorted((game / XDK_INCLUDE).glob("*.h"))
    n.build(outputs=semantics_header, rule="vita_msvc_semantics",
            implicit=[game / "tools" / "linux_msvc_semantics.py", *xdk_headers, *game_headers],
            variables={"scan": f"--all-inlines --tags {_quote(game / 'source')} --inlines {_quote(game / 'source')} "
                               f"--inlines {_quote(game / XDK_INCLUDE)}"})
    n.build(outputs=platform_semantics_header, rule="vita_msvc_semantics",
            implicit=[game / "tools" / "linux_msvc_semantics.py", *xdk_headers],
            variables={"scan": f"--inlines {_quote(game / XDK_INCLUDE)}"})
    hud_assets = gen_dir / "hud_hires_assets.c"
    n.build(outputs=hud_assets, rule="vita_embed_assets", implicit=[game / "tools" / "embed_assets.py"])

    # The port's overrides of the game's files (port/vita/overrides, laid out
    # as the game is), compiled in the game file's place with its flags, so
    # that the submodule stays at upstream's commit; overrides/README.md
    # says what each is for
    overrides_dir = port_dir / "overrides"
    overrides_left: Set[str] = {path.relative_to(overrides_dir).as_posix() for path in overrides_dir.rglob("*")
                                if path.is_file() and path.suffix in (".c", ".h")} if overrides_dir.is_dir() else set()
    override_includes = " ".join(f"-I{_quote(overrides_dir / where)}" for where in
                                 ("source/cseries", "port/linux/src", "port/linux/include", "port/linux/game")
                                 if (overrides_dir / where).is_dir())
    additions = {name for name in overrides_left if name.startswith("port/linux/game/") and name.endswith(".c")
                 and not (game / name).is_file()}

    def override_of(source: Path) -> Path:
        name = port.game_relative(source)
        if name in overrides_left:
            overrides_left.discard(name)
            return overrides_dir / name
        return source

    objects: List[Path] = []
    clang_implicit = [prefix_header, semantics_header, platform_semantics_header, *xdk_headers]

    def add(source: Path, rule: str, cflags: str, implicit: List[Path] = ()) -> None:
        obj = (obj_dir / source.relative_to(build_dir) if build_dir in source.parents
               else port.output_path(obj_dir, source)).with_suffix(".o")
        objects.append(obj)
        n.build(outputs=obj, rule=rule, inputs=source,
                implicit=(clang_implicit if rule == "vita_cc" else []) + list(implicit),
                variables={"cflags": cflags})

    defines = [*console_defines(sln), *CONSOLE_FEATURES]
    sysroot_flags = [f"--sysroot={_quote(sysroot)}", f"-isystem {_quote(sysroot / 'include')}"]
    abi = " ".join([*TARGET_FLAGS, *ABI_FLAGS, *sysroot_flags, *defines])
    # (the game's own version, which its network and Delta read, and the
    # Vita's, which its screens and halo.log say)
    version_define = (f"'-DHALO_VERSION=\"{version()}\"' "
                      f"'-DHALO_VITA_VERSION_TEXT=\"{vita_version(sln)}\"'")
    file_names = f"-fmacro-prefix-map={_quote(game.as_posix() + '/')}=" if game != Path(".") else ""
    sdk_flags = f"-idirafter {_quote(game / XDK_INCLUDE)}"
    config = port.load_port_config()

    # ---------- the game
    game_cflags = " ".join([
        abi, " ".join(GAME_FLAGS), file_names,
        f"-include {_quote(prefix_header)}", f"-include {_quote(semantics_header)}",
        override_includes,
        f"-iquote {_quote(game)}",
        f"-I{_quote(port_dir / 'include')}",
        f"-I{_quote(game / LINUX_INCLUDE)}",
        f"-I{_quote(game / CONSOLE_INCLUDE)}",
        f"-iquote {_quote(game / LINUX_GAME)}",
        port.game_defines_and_includes(config, _quote),
        sdk_flags,
    ])
    for source, flags in port.game_units(sln, config):
        cflags = " ".join([game_cflags, *flags])
        # the process's main is the host's (port/vita/host/vita_main.c)
        if port.game_relative(source) == "source/shell/shell_xbox.c":
            cflags += " -Dmain=halo_main"
        add(override_of(source), "vita_cc", cflags)
    for source in sorted((game / LINUX_GAME).glob("*.c")):
        add(override_of(source), "vita_cc", game_cflags)
    for name in sorted(additions):
        overrides_left.discard(name)
        add(overrides_dir / name, "vita_cc", game_cflags)
    for source in sorted((port_dir / "game").glob("*.c")):
        add(source, "vita_cc", f"{game_cflags} -I{_quote(game / CONSOLE_LINK)}")
    add(game / CONSOLE_LINK / "console_link_panel.c", "vita_cc", f"{game_cflags} -I{_quote(game / CONSOLE_LINK)}")

    # ---------- the platform layer shared with the desktop
    platform_cflags = " ".join([
        abi, " ".join(PLATFORM_FLAGS), version_define, file_names,
        f"-include {_quote(prefix_header)}", f"-include {_quote(platform_semantics_header)}",
        override_includes,
        f"-iquote {_quote(game)}",
        f"-I{_quote(game / LINUX_SRC)}",
        f"-I{_quote(port_dir / 'include')}",
        f"-I{_quote(game / LINUX_INCLUDE)}",
        f"-I{_quote(game / CONSOLE_INCLUDE)}",
        f"-I{_quote(game / TOML_DIR)}", f"-I{_quote(game / EXPAT_DIR)}", f"-I{_quote(game / KCP_DIR)}",
        f"-I{_quote(game / QRCODEGEN_DIR)}", f"-I{_quote(game / MONOCYPHER_DIR)}", f"-I{_quote(game / ZLIB_DIR)}",
        f"-I{_quote(game / STB_DIR)}",
        f"-I{_quote(game / 'source')} -I{_quote(game / 'source' / 'cseries')}",
        sdk_flags,
        f"-idirafter {_quote(port_dir / 'include' / 'posix')}",
    ])
    platform_cflags += f" '-DVITA_DATA_DIRECTORY=\"{data_directory}\"'"
    https_defines = " ".join(["-DHALO_CONSOLE_HTTPS", *console_https_flags(game)])
    posix_cflags = " ".join(HOST_FLAGS + [
        *defines, "-std=gnu11", "-D_GNU_SOURCE", "-fsigned-char", "-ffp-contract=off", version_define,
        f"-I{_quote(game / LINUX_SRC)}", f"-I{_quote(game / STB_DIR)}",
        f"-idirafter {_quote(port_dir / 'include' / 'posix')}",
        f"-include {_quote(port_dir / 'include' / 'posix' / 'vita_posix.h')}",
    ])
    for source in sorted((game / LINUX_SRC).glob("*.c")):
        if source.name in REPLACED_PLATFORM_SOURCES:
            continue
        if source.name.startswith("posix_"):
            # (posix_*.c talk to the C library alone: the SDK's ABI, as the
            # Vita side has it, without the game's headers)
            add(override_of(source), "vita_host_cc", f"{posix_cflags}")
            continue
        profile_id = " -DHALO_CONSOLE_PROFILE_ID" if source.name == "delta_peer_game.c" else ""
        extra = f" {https_defines}" if source.name in ("delta.c", "delta_peer_game.c") else ""
        add(override_of(source), "vita_cc", platform_cflags + extra + profile_id)
    add(game / CONSOLE_LINK / "console_link.c", "vita_cc", f"{platform_cflags} -I{_quote(game / CONSOLE_LINK)}")
    add(hud_assets, "vita_cc", platform_cflags)
    # the port's units in the platform layer's terms: the Vita's Direct3D
    # device on GXM and its Cg translators, which read the game's xgpu.h
    # with the Vita's own pixel shader key (include/vita_xgpu_game.h)
    for source in sorted((port_dir / "platform").glob("*.c")):
        add(source, "vita_cc", f"{platform_cflags} -I{_quote(port_dir / 'host')} "
                               f"-include {_quote(port_dir / 'include' / 'vita_xgpu_game.h')} "
                               f"-include {_quote(port_dir / 'include' / 'vita_glue.h')}")

    # ---------- third-party libraries, with the platform layer's ABI
    third_party = " ".join([abi, "-std=gnu11", "-w"])
    https_cflags = " ".join([*TARGET_FLAGS, *sysroot_flags, "-std=gnu11", "-w", "-Os", *console_https_flags(game)])
    for source in console_https_sources():
        add(game / source, "vita_cc", https_cflags)
    add(game / TOML_DIR / "tomlc17.c", "vita_cc", third_party)
    for name in EXPAT_SOURCES:
        add(game / EXPAT_DIR / name, "vita_cc", f"{third_party} -I{_quote(game / EXPAT_DIR)}")
    add(game / KCP_DIR / "ikcp.c", "vita_cc", third_party)
    add(game / QRCODEGEN_DIR / "qrcodegen.c", "vita_cc", third_party)
    # (Monocypher's own word-aligned loads: the game's -fmax-type-align=1
    # would make Argon2 crawl)
    for name in ("monocypher.c", "monocypher-ed25519.c"):
        add(game / MONOCYPHER_DIR / name, "vita_cc", third_party.replace("-fmax-type-align=1", ""))
    for name in ZLIB_SOURCES:
        add(game / ZLIB_DIR / name, "vita_cc", " ".join([third_party, *ZLIB_DEFINES]))
    musl_math = " ".join([third_party, f"-I{_quote(game / MUSL_MATH_DIR / 'include')}",
                          f"-include {_quote(game / MUSL_MATH_DIR / 'include' / 'libm.h')}"])
    for source in sorted((game / MUSL_MATH_DIR / "src").glob("*.c")):
        add(source, "vita_cc", musl_math)

    # ---------- the Vita side, with the SDK's ABI
    host_cflags = " ".join(HOST_FLAGS + [
        version_define,
        f"'-DVITA_TITLE_ID=\"{title_id}\"'",
        f"'-DVITA_DATA_DIRECTORY=\"{data_directory}\"'",
        f"-I{_quote(port_dir / 'include')}",
        f"-I{_quote(port_dir / 'host')}",
        f"-I{_quote(game / LINUX_SRC)}",
        f"-I{_quote(game / MONOCYPHER_DIR)}",
    ])
    # the shader generator id (vita_gxm.c's shader cache): a hash of the
    # sources that write the Cg (tools/vita_shader_generator_id.py)
    generator_header = gen_dir / "vita_shader_generator_id.h"
    generator_tool = port_dir.parent.parent / "tools" / "vita_shader_generator_id.py"
    n.rule(name="vita_shader_generator_id", command=f"$python {_quote(generator_tool)} --header $out",
           description="VITA SHADER GENERATOR ID $out", restat=True)
    n.build(outputs=generator_header, rule="vita_shader_generator_id",
            implicit=[generator_tool, port_dir / "platform" / "nv2a_psh_cg.c", port_dir / "platform" / "nv2a_vsh_cg.c",
                      port_dir / "include" / "vita_xgpu.h"])
    host_cflags += f" -I{_quote(gen_dir)} -I{_quote(TLSF_DIR)}"
    for source in sorted((port_dir / "host").glob("*.c")):
        add(source, "vita_host_cc", host_cflags, [generator_header] if source.name == "vita_gxm.c" else [])
    # the shader compiler's heap (vita_gxm.c): TLSF (third_party/tlsf, BSD)
    add(TLSF_DIR / "tlsf.c", "vita_host_cc", " ".join(HOST_FLAGS + ["-DNDEBUG", "-w"]))

    if overrides_left:
        raise SystemExit("Vita: overrides of no game file: " + ", ".join(sorted(overrides_left)))

    # ---------- the program and the package
    elf = build_dir / "warthog.elf"
    velf = build_dir / "warthog.velf"
    eboot = build_dir / "eboot.bin"
    sfo = build_dir / "param.sfo"
    vpk = build_dir / "warthog.vpk"
    n.rule(name="vita_link",
           command=("$vita_sdk_bin/arm-vita-eabi-gcc -Wl,-q,--defsym=__sce_headroom=0x1000 "
                    "-Wl,--no-enum-size-warning -Wl,--no-wchar-size-warning -Wl,--gc-sections -Wl,--wrap=exit,--wrap=abort,--wrap=_exit,--wrap=fork,--wrap=fopen,--wrap=open,--wrap=opendir,--wrap=stat,--wrap=sceKernelCreateThread,--wrap=sceKernelExitProcess,--wrap=D3DTexture_LockRect "
                    f"-Wl,-Map,{_quote(build_dir / 'warthog.map')} -o $out @$out.rsp $libs"),
           description="VITA LINK $out", rspfile="$out.rsp", rspfile_content="$in_newline")
    n.rule(name="vita_velf", command="$vita_sdk_bin/vita-elf-create $in $out", description="VITA VELF $out")
    n.rule(name="vita_eboot", command="$vita_sdk_bin/vita-make-fself -s -c $in $out", description="VITA EBOOT $out")
    app_version = "00.92"
    n.rule(name="vita_sfo",
           command=(f"$vita_sdk_bin/vita-mksfoex -s TITLE_ID={title_id} -s APP_VER={app_version} "
                    f"-d ATTRIBUTE2=12 {shlex.quote(title)} $out"),
           description="VITA SFO $out")
    n.rule(name="vita_vpk", command="$vita_sdk_bin/vita-pack-vpk -s $sfo -b $eboot $assets $out",
           description="VITA VPK $out")
    n.build(outputs=elf, rule="vita_link", inputs=objects, implicit_outputs=[build_dir / "warthog.map"],
            variables={"libs": " ".join(f"-l{lib}" for lib in VITA_LIBRARIES)})
    n.build(outputs=velf, rule="vita_velf", inputs=elf)
    n.build(outputs=eboot, rule="vita_eboot", inputs=velf)
    n.build(outputs=sfo, rule="vita_sfo", implicit=[port_dir / "build.py"])
    assets = []
    asset_files = []
    for asset in sorted((port_dir / "sce_sys").rglob("*")) if (port_dir / "sce_sys").is_dir() else []:
        if asset.is_file():
            assets.append(f"-a {_quote(asset)}={_quote(asset.relative_to(port_dir))}")
            asset_files.append(asset)
    # files at app0:'s root: the shipped shader programs (app0/shaders.pak,
    # tools/vita_shader_pack.py), stamped with this build's generator id
    pack_tool = port_dir.parent.parent / "tools" / "vita_shader_pack.py"
    n.rule(name="vita_shader_pack_stamp",
           command=f"$python {_quote(pack_tool)} --stamp $in --generator-header {_quote(generator_header)} --output $out",
           description="VITA SHADER PACK $out", restat=True)
    for asset in sorted((port_dir / "app0").rglob("*")) if (port_dir / "app0").is_dir() else []:
        if asset.is_file():
            source = asset
            if asset.name == "shaders.pak":
                source = build_dir / "app0" / asset.relative_to(port_dir / "app0")
                n.build(outputs=source, rule="vita_shader_pack_stamp", inputs=asset, implicit=[generator_header, pack_tool])
            assets.append(f"-a {_quote(source)}={_quote(asset.relative_to(port_dir / 'app0'))}")
            asset_files.append(source)
    n.build(outputs=vpk, rule="vita_vpk", inputs=[eboot, sfo], implicit=asset_files,
            variables={"sfo": _quote(sfo), "eboot": _quote(eboot), "assets": " ".join(assets)})
    n.build(outputs="vita", rule="phony", inputs=[vpk])
    n.build(outputs="vita_objects", rule="phony", inputs=objects)
    n.newline()
