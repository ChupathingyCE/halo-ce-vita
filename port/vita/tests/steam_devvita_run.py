#!/usr/bin/env python3
"""The Steam download on the dev Vita against steam_fake_server.py on this PC (never Steam): a TEST build
(configure.py --vita-variant insider --steam-test-ca CA.pem; never --release) is run with
~/halo-test-out/devvita's `vita run` (it takes the rig's lock, deploys, sets env.txt, follows halo.log and puts
env.txt back after); the fake serves the CM and the CDN over TLS on this PC's LAN address with a certificate of
the test's authority; the downloaded made-up files go to a scratch folder (ux0:data/steamdl-test), never the game's.
The buttons are pressed through vitacompanion (free of the lock) as the log reaches each screen, and the screen is
grabbed from the udcd_uvc webcam at each step.

  steam_devvita_run.py VPK ELF CERT KEY OUT [--host 192.168.0.61] [--scale 8] [--chunk-kb 1024]

What it checks, from halo.log: the screen opened, the QR up, the download done, revoked and logged off; the
measurements (the CM connection's time, the decoders' MB/s, the largest chunk buffer) and the write past the end
of a .part file and its rename on ux0 (the files there at their size, the run's own end check)."""
import argparse
import json
import os
import re
import subprocess
import sys
import threading
import time

DEVVITA = os.path.expanduser("~/halo-test-out/devvita")
HERE = os.path.dirname(os.path.abspath(__file__))


def press(button, hold_ms=150):
    subprocess.run([os.path.join(DEVVITA, "vita"), "cmd", "press %s; wait %dms; release %s" % (button, hold_ms, button)],
                   capture_output=True, text=True, timeout=30)


def grab(out, name):
    node = None
    listing = subprocess.run(["v4l2-ctl", "--list-devices"], capture_output=True, text=True).stdout
    block = False
    for line in listing.splitlines():
        if line and not line.startswith("\t"):
            block = "PSVita" in line
        elif block and "/dev/video" in line:
            node = line.strip()
            break
    if not node:
        print("no webcam node: no picture of %s" % name, flush=True)
        return
    path = os.path.join(out, name + ".png")
    env = {k: v for k, v in os.environ.items() if k != "DISPLAY"}
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "v4l2", "-video_size", "960x544", "-i", node, "-vf",
                    "select=gte(n\\,12)", "-fps_mode", "passthrough", "-frames:v", "1", "-update", "1", path],
                   env=env, capture_output=True, timeout=30)
    print("picture: %s" % path, flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("vpk")
    parser.add_argument("elf")
    parser.add_argument("cert")
    parser.add_argument("key")
    parser.add_argument("out")
    parser.add_argument("--host", default="192.168.0.61")
    parser.add_argument("--scale", type=int, default=8)
    parser.add_argument("--chunk-kb", type=int, default=1024)
    parser.add_argument("--language", default="")
    # (start at once, no menus: the device's measurements alone; the menus need the card's Halo PC files)
    parser.add_argument("--autostart", type=int, default=0)
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    fake = subprocess.Popen([sys.executable, "-I", os.path.join(HERE, "steam_fake_server.py"), "--work", args.out,
                             "--mode", "normal", "--host", args.host, "--scale", str(args.scale), "--chunk-kb",
                             str(args.chunk_kb), "--cert", args.cert, "--key", args.key],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    ports = json.loads(fake.stdout.readline())
    env = os.path.join(args.out, "env.txt")
    with open(env, "w") as out:
        out.write("HALO_UI_LOG=1\nHALO_MENU_STYLE=halo3\n")
        out.write("HALO_STEAM_TEST_CM=wss://%s:%d/cmsocket/\n" % (args.host, ports["cm"]))
        out.write("HALO_STEAM_TEST_CDN=https://%s:%d\n" % (args.host, ports["cdn"]))
        out.write("HALO_STEAM_TEST_ROOT=ux0:data/steamdl-test\n")
        if args.autostart:
            out.write("HALO_STEAM_TEST_AUTOSTART=%d\n" % args.autostart)
        if args.language:
            out.write("HALO_LANGUAGE=%s\n" % args.language)
    run = subprocess.Popen([os.path.join(DEVVITA, "vita"), "run", args.vpk, env, "--elf", args.elf, "--timeout", "900",
                            "--hang", "240", "--follow", "--done", r"steam job: CM connection"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log = open(os.path.join(args.out, "follow.log"), "w")
    steps = [
        # (the log line, then what is done)
        (r"ui: screen pc\\h3\\main\\screen", lambda: (time.sleep(6), press("start"), time.sleep(3), grab(args.out, "1-start"),
                                                      press("down"), press("down"), press("down"), press("cross"))),
        (r"ui: screen pc\\h3\\mods\\screen", lambda: (time.sleep(4), press("down"), press("down"), press("down"),
                                                      time.sleep(1), grab(args.out, "2-game-manager"), press("cross"))),
        (r"ui: screen pc\\h3\\steam\\screen", lambda: (time.sleep(3), grab(args.out, "3-notice"), press("down"),
                                                       press("cross"), press("down"), press("cross"), press("down"),
                                                       time.sleep(1), grab(args.out, "4-games"), press("cross"))),
        (r"steam job: QR 1 up", lambda: (time.sleep(0.5), grab(args.out, "5-qr"))),
        (r"done \(", lambda: (time.sleep(3), grab(args.out, "6-progress"))),
    ]
    if args.autostart:
        steps = [(r"steam job: QR 1 up", lambda: (time.sleep(0.5), grab(args.out, "5-qr"))),
                 (r"done \(", lambda: (time.sleep(3), grab(args.out, "6-progress")))]
    step = 0
    seen = ""
    for line in run.stdout:
        log.write(line)
        log.flush()
        seen += line
        if step < len(steps) and re.search(steps[step][0], line):
            action = steps[step][1]
            threading.Thread(target=action, daemon=True).start()
            step += 1
    run.wait()
    log.close()
    grab(args.out, "7-end")
    fake.stdin.close()
    fake.wait()
    print("vita run exit %d; steps reached %d of %d" % (run.returncode, step, len(steps)))
    for pattern in (r"steam: the CM connection took \d+ ms", r"memory.*steam download.*", r"depot \d+: .*", r"steam job: .*MB/s\)", r"steam job: CM connection.*",
                    r"steam job: sign-in \d ended: .*", r"steam: PS button .*"):
        for match in re.findall(pattern, seen):
            print("  " + match)


if __name__ == "__main__":
    main()
