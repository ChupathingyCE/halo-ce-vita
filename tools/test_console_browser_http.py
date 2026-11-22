"""Builds and runs the host test of the game list's requests over the
consoles' HTTPS (port/console/https/console_browser_http.c, through
port/console/tests/console_browser_http_test.c), with the consoles' Mbed
TLS configuration, against a local TLS server on the loopback address that
answers each request with its request line, Host, User-Agent, Content-Type
and body (a 6000-byte body for /big, 404 for /missing).

Needs a C compiler. Usage: test_console_browser_http.py [--cc clang] [--keep DIR]
"""

import argparse
import re
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_console_https as https_test  # noqa: E402

SRC = ROOT / "port/linux/src"


class Server:
    def __init__(self, chain, key):
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.load_cert_chain(str(chain), str(key))
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(4)
        self.port = self.listener.getsockname()[1]
        threading.Thread(target=self.serve, daemon=True).start()

    def serve(self):
        while True:
            try:
                connection, _ = self.listener.accept()
            except OSError:
                return
            connection.settimeout(10)
            try:
                with self.context.wrap_socket(connection, server_side=True) as tls:
                    data = b""
                    while b"\r\n\r\n" not in data:
                        chunk = tls.recv(4096)
                        if not chunk:
                            break
                        data += chunk
                    head, _, body = data.partition(b"\r\n\r\n")
                    length = re.search(rb"Content-Length: (\d+)", head)
                    while length and len(body) < int(length.group(1)):
                        body += tls.recv(4096)
                    lines = head.decode().split("\r\n")
                    headers = dict(line.split(": ", 1) for line in lines[1:] if ": " in line)
                    path = lines[0].split(" ")[1]
                    status = b"200 OK"
                    if path == "/big":
                        answer = b"x" * 6000
                    elif path == "/missing":
                        status, answer = b"404 Not Found", b"none"
                    else:
                        answer = "|".join((lines[0].rsplit(" ", 1)[0], headers.get("Host", ""),
                                           headers.get("User-Agent", ""), headers.get("Content-Type", ""),
                                           body.decode())).encode()
                    tls.sendall(b"HTTP/1.0 %s\r\nContent-Length: %d\r\n\r\n%s" % (status, len(answer), answer))
            except (ssl.SSLError, OSError):
                pass
            finally:
                connection.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default=shutil.which("clang") or "cc")
    parser.add_argument("--keep", type=Path)
    args = parser.parse_args()
    folder = args.keep or Path(tempfile.mkdtemp(prefix="console_browser_http_"))
    folder.mkdir(parents=True, exist_ok=True)
    try:
        console_flags = ['-DMBEDTLS_CONFIG_FILE="console_mbedtls_config.h"', "-DCONSOLE_HTTPS_TESTS",
                         f"-I{https_test.HTTPS}"]
        default = https_test.build_archive(args.cc, folder / "default", [])
        console = https_test.build_archive(args.cc, folder / "console", console_flags)
        certs = folder / "console_https_certs"
        subprocess.run([args.cc, "-std=c99", "-O1", "-w", f"-I{https_test.MBEDTLS / 'include'}",
                        str(https_test.TESTS / "console_https_certs.c"), str(default), "-o", str(certs)], check=True)
        client = folder / "console_browser_http_test"
        subprocess.run([args.cc, "-std=c99", "-D_DEFAULT_SOURCE", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-DHALO_GAME_BROWSER", "-DHALO_CONSOLE_BROWSER_HTTPS", *console_flags, f"-I{SRC}",
                        f"-I{https_test.MBEDTLS / 'include'}", f"-I{https_test.MONOCYPHER}",
                        str(https_test.TESTS / "console_browser_http_test.c"),
                        str(https_test.HTTPS / "console_browser_http.c"), str(https_test.HTTPS / "console_https.c"),
                        str(https_test.HTTPS / "console_https_trust.c"), str(https_test.HTTPS / "console_https_roots.c"),
                        str(https_test.HTTPS / "console_pins.c"), str(console),
                        str(https_test.MONOCYPHER / "monocypher.c"), str(https_test.MONOCYPHER / "monocypher-ed25519.c"),
                        "-o", str(client)], check=True)
        keys = https_test.Keys(certs, folder)
        ca_key, key = keys.key("ca"), keys.key("site")
        ca_name, site_name = "CN=Test Root", "CN=" + https_test.SITE
        ca = keys.cert("ca", ca_key, ca_key, ca_name, ca_name, "", True, https_test.NOW)
        site = keys.cert("site", key, ca_key, ca_name, site_name, https_test.SITE, False, https_test.NOW)
        chain = folder / "chain.pem"
        chain.write_text(site.read_text() + ca.read_text())
        server = Server(chain, key)
        result = subprocess.run([str(client), str(server.port), keys.pin(key)], capture_output=True, text=True,
                                timeout=120)
        print(result.stdout, end="")
        if result.returncode:
            print(result.stderr, end="", file=sys.stderr)
            print(f"FAILED: {result.returncode} check(s)")
            return 1
        print("console browser HTTP: all checks passed")
        return 0
    finally:
        if not args.keep:
            shutil.rmtree(folder, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
