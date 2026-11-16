#!/usr/bin/env python3
"""A LOCAL FAKE of the Steam side the download talks to, for the PC tests only
(run_steam_download_test.sh): never Steam, never a real account, nothing of
Valve's or Microsoft's. On 127.0.0.1 it serves

  a CM over a WebSocket (ws://127.0.0.1:<port>/cmsocket/): ClientHello, the QR
    sign-in (BeginAuthSessionViaQR, PollAuthSessionStatus: waiting, a rotated
    QR, then made-up tokens), ClientLogon (the made-up refresh token checked),
    heartbeats, PICS (an access token, then the app info in two responses, the
    first pending), depot keys, manifest request codes, CDN servers,
    RevokeToken and ClientLogOff, some replies inside gzipped Multi messages;
  a CDN over HTTP/1.1 kept alive (http://127.0.0.1:<port>): each depot's
    manifest (a zip, names encrypted under the depot's made-up key) and its
    chunks (made-up bytes; VZa LZMA, VSZa zstd and zip deflate in turn; some
    responses chunked).

The files are made up: Halo-like names with made-up contents that pass the
installer's header checks ("daeh" + 13 and so on), a few chunks each. The
manifests also list hostile names (../, absolute, drive letters) that the
client must never write.

Every event goes to <work>/transcript.txt (the tests read the order of revoke
and log-off). --mode picks a failure to act out:
  normal, corrupt_once (one chunk damaged the first time it is sent),
  drop_once (the connection cut mid-body once), logon_refuse, qr_refuse,
  not_owned (no key for 976734), kick (the server logs the client off during
  PICS), badmulti (a malformed Multi first), slow (each CDN answer 0.25 s late:
  the client's heartbeats must go on during the download), losesession (the CM
  connection cut 1.5 s after the first logon (outage: the CDN gone for 8 s mid-download: the client waits
  and goes on),, the CDN slow: the job revokes on a
  new connection, signs in again with a new QR and resumes).

--host (a LAN address for the dev Vita), --chunk-kb (Steam's are 1024) and --scale (the made-up maps' size,
times) shape a bigger test. --cert/--key serve both over TLS (the test's own certificates).

--workshop-map <file>: Workshop item 2900000001's maps are that one file (a map of the player's own MCC, for a test
of the game's own map check; never sent beyond 127.0.0.1). --workshop-mp-map <file> [--workshop-mp-name <name>]: item
2900000002 (multiplayer) is that one map, named so (a joiner's download of the host's Workshop map: the netns tests).

The Workshop (app 976730's own depot): PublishedFile.QueryFiles#1 (ten a page, a search by title), GetDetails#1,
depot 976730's key, each item's manifest and chunks; made-up items, some hostile (names that leave the folder, two
names one file, another game's, banned, a map not version 13, one the game's check refuses, a drive, a control
byte); modes ws_not_owned, ws_wrong_manifest, ws_hostile_list.

--fixtures <dir> writes the chunk fixtures the C unit tests read and exits.

The first line it prints is JSON: {"cm": port, "cdn": port, "token": ...}.
"""
import argparse
import base64
import gzip
import hashlib
import io
import json
import lzma
import os
import random
import socket
import ssl
import struct
import sys
import threading
import time
import zipfile
import zlib
from compression import zstd

from cryptography.hazmat.primitives import padding
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

APP = 976730
DEVICE = "Halo CE Vita (PS Vita)"
WS_GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

# ------------------------------------------------------------------ protobuf


def varint(value):
    value &= (1 << 64) - 1
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def pb_varint(number, value):
    return varint(number << 3) + varint(value)


def pb_bytes(number, data):
    if isinstance(data, str):
        data = data.encode()
    return varint(number << 3 | 2) + varint(len(data)) + data


def pb_fixed64(number, value):
    return varint(number << 3 | 1) + struct.pack("<Q", value)


def pb_fixed32(number, value):
    return varint(number << 3 | 5) + struct.pack("<I", value)


def pb_float(number, value):
    return varint(number << 3 | 5) + struct.pack("<f", value)


def read_varint(data, i):
    result = shift = 0
    while True:
        byte = data[i]
        i += 1
        result |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            return result, i


def pb_fields(data):
    i = 0
    out = []
    while i < len(data):
        key, i = read_varint(data, i)
        number, wire = key >> 3, key & 7
        if wire == 0:
            value, i = read_varint(data, i)
        elif wire == 1:
            value = struct.unpack_from("<Q", data, i)[0]
            i += 8
        elif wire == 5:
            value = struct.unpack_from("<I", data, i)[0]
            i += 4
        elif wire == 2:
            length, i = read_varint(data, i)
            value = data[i:i + length]
            i += length
        else:
            raise ValueError("wire type")
        out.append((number, value))
    return out


def first(fields, number, default=None):
    for n, v in fields:
        if n == number:
            return v
    return default

# ------------------------------------------------------------------ crypto


def symmetric_encrypt(key, plain):
    iv = os.urandom(16)
    ecb = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
    encrypted_iv = ecb.update(iv) + ecb.finalize()
    padder = padding.PKCS7(128).padder()
    padded = padder.update(plain) + padder.finalize()
    cbc = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
    return encrypted_iv + cbc.update(padded) + cbc.finalize()


def adler_zero(data):
    return zlib.adler32(data, 0) & 0xFFFFFFFF

# ------------------------------------------------------------------ chunk containers


def vzip(data):
    props = {"id": lzma.FILTER_LZMA1, "dict_size": 1 << 20, "lc": 3, "lp": 0, "pb": 2}
    raw = lzma.compress(data, format=lzma.FORMAT_RAW, filters=[props])
    prop_byte = (2 * 5 + 0) * 9 + 3
    crc = zlib.crc32(data) & 0xFFFFFFFF
    return (b"VZa" + struct.pack("<I", crc) + bytes([prop_byte]) + struct.pack("<I", 1 << 20) + raw +
            struct.pack("<II", crc, len(data)) + b"zv")


def vzstd(data):
    crc = zlib.crc32(data) & 0xFFFFFFFF
    frame = zstd.compress(data)
    return b"VSZa" + struct.pack("<I", crc) + frame + struct.pack("<II", crc, len(data)) + b"\0\0\0\0" + b"zsv"


def pkzip(data, name="z"):
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr(name, data)
    return out.getvalue()


CONTAINERS = (vzip, vzstd, pkzip)

# ------------------------------------------------------------------ the made-up depots

CE_CAMPAIGN = ["a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40"]
H2_MP = ["ascension", "backwash", "beavercreek", "burial_mounds", "coagulation", "colossus", "containment",
         "cyclotron", "deltatap", "derelict", "dune", "elongation", "foundation", "gemini", "headlong", "highplains",
         "lockout", "midship", "needle", "street_sweeper", "triplicate", "turf", "warlock", "waterworks", "zanzibar"]
HOSTILE = ["..\\..\\evil.map", "/tmp/evil.map", "C:\\evil.map", "halo1\\maps\\..\\..\\..\\evil.map",
           "halo1\\maps\\a10.map\\..\\..\\evil.map"]
CHUNK = 48 * 1024


SCALE = 1


def map_bytes(rng, size):
    """made-up map contents: "daeh" + 13, then noise and runs (compressible)"""
    size *= SCALE
    body = bytearray(b"daeh" + struct.pack("<I", 13))
    while len(body) < size:
        if rng.random() < 0.5:
            body += bytes([rng.randrange(256)]) * rng.randrange(16, 2048)
        else:
            body += rng.randbytes(rng.randrange(16, 512))
    return bytes(body[:size])


def depot_files(depot, rng):
    files = {}
    if depot == 976733:
        for stem in CE_CAMPAIGN:
            files["halo1\\maps\\%s.map" % stem] = map_bytes(rng, rng.randrange(60000, 140000))
        files["halo1\\maps\\bitmaps.map"] = b"\1\0\0\0" + rng.randbytes(70000)
        files["halo1\\maps\\ui.map"] = map_bytes(rng, 30000)
        files["halo1\\sound\\pc\\sounds_adpcm.fsb"] = b"FSB5" + rng.randbytes(40000)
        files["halo1\\sound\\pc\\lst\\sounds_adpcm.lst.bin"] = struct.pack("<I", 1) + rng.randbytes(3000)
        # (1.1.x: the CELT banks the player may take, STEAM_GAME_HALO_CE_BANKS; music left out of the fake: "not in
        # the depot" is a note, not a failure)
        for name in ("sounds_dialogs", "sounds_stream", "sounds_debug"):
            files["halo1\\sound\\pc\\%s.fsb" % name] = b"FSB5" + rng.randbytes(rng.randrange(20000, 50000))
            files["halo1\\sound\\pc\\lst\\%s.lst.bin" % name] = struct.pack("<I", 1) + rng.randbytes(2000)
        # (custom_edition's loc.map: a resource map header, type 3)
        loc = struct.pack("<IIIi", 3, 16, 16, 0) + rng.randbytes(1000)
        files["halo1\\maps\\custom_edition\\loc.map"] = loc
        files["halo1\\readme.txt"] = b"not planned"
    elif depot == 976734:
        for stem in ("bloodgulch", "wizard"):
            files["halo1\\maps\\%s.map" % stem] = map_bytes(rng, rng.randrange(50000, 90000))
        files["halo1\\maps\\bitmaps.map"] = b"\1\0\0\0" + rng.randbytes(1000)
    elif depot == 976736:
        # (Halo 2's depot, kept as a depot the 1.1.x plan never asks for)
        for stem in H2_MP[:3]:
            files["halo2\\h2_maps_win64_dx11\\%s.map" % stem] = map_bytes(rng, rng.randrange(20000, 60000))
    for name in HOSTILE:
        files[name] = b"daeh" + struct.pack("<I", 13) + b"hostile" * 100
    return files


class Depot:
    def __init__(self, depot, seed, encrypt=True, files=None, gid=None, key=None, symlinks=()):
        rng = random.Random(seed * 7919 + depot + (gid or 0))
        self.depot = depot
        self.key = key or rng.randbytes(32)
        self.gid = gid or rng.randrange(1, 1 << 63)
        self.files = files if files is not None else depot_files(depot, rng)
        self.symlinks = set(symlinks)
        self.chunks = {}  # sha hex -> encrypted bytes
        mappings = b""
        unique = 0
        for index, (name, data) in enumerate(sorted(self.files.items())):
            chunk_fields = []
            for offset in range(0, len(data), CHUNK):
                piece = data[offset:offset + CHUNK]
                sha = hashlib.sha1(piece).digest()
                container = CONTAINERS[(index + offset // CHUNK) % len(CONTAINERS)]
                encrypted = symmetric_encrypt(self.key, container(piece))
                if sha.hex() not in self.chunks:
                    unique += 1
                self.chunks[sha.hex()] = encrypted
                chunk_fields.append(pb_bytes(1, sha) + pb_fixed32(2, adler_zero(piece)) + pb_varint(3, offset) +
                                    pb_varint(4, len(piece)) + pb_varint(5, len(encrypted)))
            name_bytes = name.encode()
            encrypted_name = base64.b64encode(symmetric_encrypt(self.key, name_bytes + b"\0")) if encrypt else name_bytes
            flags = 512 if name in self.symlinks else 0
            mapping = (pb_bytes(1, encrypted_name) + pb_varint(2, len(data)) + pb_varint(3, flags) +
                       pb_bytes(4, hashlib.sha1(name.replace("/", "\\").lower().encode()).digest()) +
                       pb_bytes(5, hashlib.sha1(data).digest()) + b"".join(pb_bytes(6, c) for c in chunk_fields))
            mappings += pb_bytes(1, mapping)
        payload = mappings
        crc_clear = zlib.crc32(struct.pack("<I", len(payload)) + payload) & 0xFFFFFFFF
        metadata = (pb_varint(1, depot) + pb_varint(2, self.gid) + pb_varint(3, 1700000000) + pb_varint(4, int(encrypt)) +
                    pb_varint(7, unique) + pb_varint(9, crc_clear))
        manifest = (struct.pack("<II", 0x71F617D0, len(payload)) + payload +
                    struct.pack("<II", 0x1F4812BE, len(metadata)) + metadata +
                    struct.pack("<II", 0x1B81B817, 0) + struct.pack("<I", 0x32C415AB))
        self.manifest = manifest
        self.manifest_zip = pkzip(manifest)

# ------------------------------------------------------------------ the made-up Workshop (app 976730's own depot)

WORKSHOP_DEPOT = 976730


def workshop_png(rng):
    return b"\x89PNG\r\n\x1a\n" + rng.randbytes(300)


def workshop_items(seed):
    """the made-up Workshop items: (id, title, tags, consumer app, banned, files, symlinks); the ids are made up"""
    rng = random.Random(seed * 31 + 7)
    v13 = lambda size: map_bytes(rng, size)
    campaign_info = json.dumps({"Title": {"Neutral": "Fake Campaign"}, "Missions": ["a10", "a30"]}).encode()
    items = [
        # (a campaign: two maps, its JSON and pictures, a file of another kind left out, a link skipped)
        (2900000001, "Fake Rebalanced Campaign \u2013 \U0001F600", ["Halo1", "Campaign"], APP, False, {
            "maps\\a10.map": v13(70000), "maps\\a30.map": v13(50000),
            "CampaignInfo.json": campaign_info,
            "campaign\\a10.json": b'{"Title":{"Neutral":"A10"},"ScenarioFile":"a10"}',
            "assets\\a10.png": workshop_png(rng), "ModInfo.json": b'{"Title":{"Neutral":"Fake"}}',
            "tools\\setup.exe": b"MZ not taken", "maps\\link.map": b"daeh" + struct.pack("<I", 13) + b"x" * 64,
            # (names real items have, skipped - not written - with the item kept)
            "images\\Halo  The Master Chief Collection Screenshot 2022.02.16 - 23.03.12.18.png": workshop_png(rng),
            "campaign\\.json": b"{}",
        }, ["maps\\link.map"]),
        # (multiplayer)
        (2900000002, "Fake Gulch Remix", ["Halo1", "Multiplayer"], APP, False,
         {"maps\\fakegulch.map": v13(40000), "ModInfo.json": b"{}"}, []),
        # (hostile: a name that leaves the folder)
        (2900000003, "Fake Escape", ["Halo1", "Campaign"], APP, False,
         {"maps\\a10.map": v13(20000), "..\\..\\evil.map": v13(1000)}, []),
        # (two names one file on the Vita's card)
        (2900000004, "Fake Twins", ["Halo1", "Campaign"], APP, False,
         {"maps\\A10.map": v13(20000), "maps\\a10.map": v13(21000)}, []),
        # (another game's item)
        (2900000005, "Fake Halo 2 Item", ["Halo2"], APP, False, {"maps\\x.map": v13(9000)}, []),
        # (banned)
        (2900000006, "Fake Banned", ["Halo1", "Campaign"], APP, True, {"maps\\a10.map": v13(9000)}, []),
        # (a .map that is not a version 13 cache: the header check)
        (2900000007, "Fake Old Map", ["Halo1", "Campaign"], APP, False,
         {"maps\\a10.map": b"daeh" + struct.pack("<I", 7) + b"x" * 5000}, []),
        # (a map the game's own check refuses: the test's check refuses "badcheck")
        (2900000008, "Fake Bad Check", ["Halo1", "Campaign"], APP, False, {"maps\\badcheck.map": v13(9000)}, []),
        # (a drive letter, a control byte)
        (2900000009, "Fake Drive", ["Halo1", "Campaign"], APP, False,
         {"maps\\a10.map": v13(9000), "C:\\evil.map": v13(1000)}, []),
        (2900000010, "Fake Control", ["Halo1", "Campaign"], APP, False,
         {"maps\\a10.map": v13(9000), "maps\\a\x01.map": v13(1000)}, []),
        # (an item with no maps: only text)
        (2900000011, "Fake Notes", ["Halo1", "Campaign"], APP, False, {"readme.txt": b"hello"}, []),
    ]
    return items


class WorkshopItem:
    def __init__(self, entry, seed, key):
        self.id, self.title, self.tags, self.app, self.banned, files, links = entry
        self.depot = Depot(WORKSHOP_DEPOT, seed, files=files, gid=(self.id * 7) & ((1 << 63) - 1), key=key,
                           symlinks=links)
        self.size = sum(len(d) for d in files.values())

    def details(self):
        tags = b"".join(pb_bytes(52, pb_bytes(1, t)) for t in self.tags)
        return (pb_varint(1, 1) + pb_varint(2, self.id) + pb_fixed64(3, 76561198000000001) + pb_varint(4, APP) +
                pb_varint(5, self.app) + pb_varint(8, self.size) + pb_fixed64(14, self.depot.gid) +
                pb_bytes(16, self.title.encode()) + pb_varint(20, 1760000000) + pb_varint(21, 0) +
                pb_varint(28, int(self.banned)) + tags + pb_bytes(17, "a description, not read"))


# ------------------------------------------------------------------ the server


class State:
    def __init__(self, args):
        self.args = args
        self.mode = args.mode
        self.lock = threading.Lock()
        self.transcript = open(os.path.join(args.work, "transcript.txt"), "a", buffering=1)
        self.refresh_token = "eyJ0ZXN0.FAKE-REFRESH-" + os.urandom(12).hex()
        self.access_token = "eyJ0ZXN0.FAKE-ACCESS-" + os.urandom(12).hex()
        self.depots = {d: Depot(d, args.seed) for d in (976733, 976734, 976736)}
        workshop_key = random.Random(args.seed * 13).randbytes(32)
        entries = workshop_items(args.seed)
        if args.workshop_map:
            # (a real map of the player's own, read here and never sent beyond 127.0.0.1: item 2900000001's maps are
            # then one the game's own map check takes - run_menus_test.sh h3workshop)
            with open(args.workshop_map, "rb") as real:
                data = real.read()
            files = {k: v for k, v in entries[0][5].items() if not k.startswith("maps\\")}
            files["maps\\" + os.path.basename(args.workshop_map)] = data
            entries[0] = entries[0][:5] + (files,) + entries[0][6:]
        if args.workshop_mp_map:
            # (a real multiplayer map of the player's own MCC, as Workshop item 2900000002's one map (its file's name
            # as given): the joiner's download in run_netns_online_test.sh's workshop cases; never beyond 127.0.0.1)
            with open(args.workshop_mp_map, "rb") as real:
                data = real.read()
            name = args.workshop_mp_name or os.path.basename(args.workshop_mp_map)
            index = next(i for i, e in enumerate(entries) if e[0] == 2900000002)
            entries[index] = entries[index][:5] + ({"maps\\" + name: data, "ModInfo.json": b"{}"},) + entries[index][6:]
        self.workshop = [WorkshopItem(entry, args.seed, workshop_key) for entry in entries]
        self.workshop_key = workshop_key
        self.workshop_chunks = {}
        for item in self.workshop:
            self.workshop_chunks.update(item.depot.chunks)
        self.codes = {}
        self.tls = None
        if args.cert:
            # (TLS with a certificate the test made: from the test's own authority, or one of its own)
            self.tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            self.tls.load_cert_chain(args.cert, args.key)
        self.corrupted = set()
        self.dropped = False
        self.lost = False
        self.outage_start = None

    def log(self, line):
        with self.lock:
            self.transcript.write(line + "\n")


def cm_message(emsg, body, job_target=None, eresult=None, steamid=0, session=0):
    header = b""
    if steamid:
        header += pb_fixed64(1, steamid)
    if session:
        header += pb_varint(2, session)
    if job_target is not None:
        header += pb_fixed64(11, job_target)
    if eresult is not None:
        header += pb_varint(13, eresult & 0xFFFFFFFFFFFFFFFF)
    return struct.pack("<II", emsg | 0x80000000, len(header)) + header + body


def multi(messages, zipped=True):
    payload = b"".join(struct.pack("<I", len(m)) + m for m in messages)
    if zipped:
        return cm_message(1, pb_varint(1, len(payload)) + pb_bytes(2, gzip.compress(payload)))
    return cm_message(1, pb_bytes(2, payload))


def ws_send(conn, payload, opcode=2):
    header = bytes([0x80 | opcode])
    if len(payload) < 126:
        header += bytes([len(payload)])
    elif len(payload) <= 0xFFFF:
        header += bytes([126]) + struct.pack(">H", len(payload))
    else:
        header += bytes([127]) + struct.pack(">Q", len(payload))
    conn.sendall(header + payload)


def recv_exact(conn, count):
    data = b""
    while len(data) < count:
        piece = conn.recv(count - len(data))
        if not piece:
            raise ConnectionError("closed")
        data += piece
    return data


def ws_recv(conn):
    head = recv_exact(conn, 2)
    opcode = head[0] & 15
    if not head[1] & 0x80:
        raise ConnectionError("client frame not masked")
    length = head[1] & 127
    if length == 126:
        length = struct.unpack(">H", recv_exact(conn, 2))[0]
    elif length == 127:
        length = struct.unpack(">Q", recv_exact(conn, 8))[0]
    mask = recv_exact(conn, 4)
    data = bytes(b ^ mask[i & 3] for i, b in enumerate(recv_exact(conn, length)))
    return opcode, data


def cm_client(state, conn):
    request = b""
    while b"\r\n\r\n" not in request:
        piece = conn.recv(4096)
        if not piece:
            return
        request += piece
    key = None
    for line in request.split(b"\r\n"):
        if line.lower().startswith(b"sec-websocket-key:"):
            key = line.split(b":", 1)[1].strip()
    accept = base64.b64encode(hashlib.sha1(key + WS_GUID).digest())
    conn.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                 b"Sec-WebSocket-Accept: " + accept + b"\r\n\r\n")
    state.log("cm: connected")
    polls = 0
    logged_on = False
    steamid = 76561198000000000
    if state.mode == "badmulti":
        ws_send(conn, cm_message(1, pb_varint(1, 50) + pb_bytes(2, b"\x1f\x8bnot gzip at all")))
    while True:
        try:
            opcode, data = ws_recv(conn)
        except (ConnectionError, OSError):
            state.log("cm: closed")
            return
        if opcode == 8:
            state.log("cm: close frame")
            try:
                ws_send(conn, data[:2], 8)
            except OSError:
                pass
            return
        if opcode != 2:
            continue
        raw, header_size = struct.unpack_from("<II", data)
        emsg = raw & 0x7FFFFFFF
        header = pb_fields(data[8:8 + header_size])
        body = pb_fields(data[8 + header_size:])
        job = first(header, 10)
        name = first(header, 12, b"").decode()
        if emsg == 9805:
            state.log("cm: hello %d" % first(body, 1, 0))
        elif emsg in (9804, 151):
            state.log("cm: call %s%s" % (name, " (signed in)" if emsg == 151 else ""))
            reply, result = b"", 1
            if name == "Authentication.BeginAuthSessionViaQR#1":
                assert first(body, 1) == DEVICE.encode()
                reply = (pb_varint(1, 1234567) + pb_bytes(2, "https://s.team/q/1/FAKEQR1") +
                         pb_bytes(3, b"fake-request-id") + pb_float(4, 1.0) + pb_varint(6, 1))
            elif name == "Authentication.PollAuthSessionStatus#1":
                assert first(body, 1) == 1234567 and first(body, 2) == b"fake-request-id"
                polls += 1
                if state.mode == "qr_refuse" and polls >= 2:
                    result = 9
                elif polls == 2:
                    reply = pb_bytes(2, "https://s.team/q/1/FAKEQR2")
                elif polls >= 3:
                    reply = (pb_bytes(3, state.refresh_token) + pb_bytes(4, state.access_token) +
                             pb_bytes(6, "fakeplayer"))
            elif name == "ContentServerDirectory.GetManifestRequestCode#1":
                assert logged_on and emsg == 151
                depot, gid = first(body, 2), first(body, 3)
                code = random.randrange(1, 1 << 63)
                state.codes[depot] = (gid, code)
                state.codes[(depot, gid)] = code
                reply = pb_varint(1, code)
            elif name == "PublishedFile.QueryFiles#1":
                assert logged_on and emsg == 151
                page, per_page, search = first(body, 2, 1), first(body, 3, 1), first(body, 11, b"").decode()
                assert first(body, 5) == APP and first(body, 6) == b"Halo1" and first(body, 37) == 1
                state.log("cm: workshop query page %d sort %d search '%s'" % (page, first(body, 1, 0), search))
                listed = [i for i in state.workshop if search.lower() in i.title.lower()]
                if state.mode == "ws_hostile_list":
                    # (a hostile list: an item whose tag runs past its message is malformed)
                    reply = pb_varint(1, 3) + pb_bytes(2, pb_varint(2, 1) + b"\xa2\x03\x7f\x0a")
                else:
                    shown = listed[(page - 1) * per_page:page * per_page]
                    reply = pb_varint(1, len(listed)) + b"".join(pb_bytes(2, i.details()) for i in shown)
            elif name == "PublishedFile.GetDetails#1":
                assert logged_on and emsg == 151
                ids = [v for n, v in body if n == 1]
                state.log("cm: workshop details %s" % " ".join(str(i) for i in ids))
                reply = b"".join(pb_bytes(1, i.details()) for i in state.workshop if i.id in ids)
            elif name == "ContentServerDirectory.GetServersForSteamPipe#1":
                server = (pb_bytes(1, "SteamCache") + pb_varint(3, 7) + pb_bytes(8, "cache1-fake.steamcontent.test") +
                          pb_bytes(9, "cache1-fake.steamcontent.test") + pb_bytes(12, "mandatory"))
                plain = pb_bytes(1, "CDN") + pb_bytes(8, "plain.fake.test") + pb_bytes(12, "unavailable")
                reply = pb_bytes(1, server) + pb_bytes(1, plain)
            elif name == "Authentication.RevokeToken#1":
                token = first(body, 1, b"").decode()
                ok = token == state.refresh_token and first(body, 2) == 1
                state.log("cm: revoke %s" % ("ok" if ok else "WRONG TOKEN"))
                result = 1 if ok else 8
            else:
                result = 9
            ws_send(conn, cm_message(147, reply, job_target=job, eresult=result,
                                     steamid=steamid if logged_on else 0, session=42 if logged_on else 0))
        elif emsg == 5514:
            ok = (first(body, 108, b"").decode() == state.refresh_token and first(body, 50) == b"fakeplayer" and
                  first(body, 8, 0) == 0 and first(body, 1) == 65580)
            if state.mode == "logon_refuse":
                ok = False
            state.log("cm: logon %s" % ("ok" if ok else "refused"))
            response = cm_message(751, pb_varint(1, 1 if ok else 5) + pb_varint(3, 5) + pb_varint(7, 7),
                                  steamid=steamid if ok else 0, session=42 if ok else 0)
            logged_on = ok
            if ok and state.mode == "losesession" and not state.lost:
                # (the first session dies under the download: the client must revoke on a new connection and sign in
                # again with a new QR)
                state.lost = True
                threading.Timer(1.5, lambda c=conn: (state.log("cm: connection dropped"),
                                                      c.shutdown(socket.SHUT_RDWR))).start()
            # (the response inside a gzipped Multi, beside two others)
            ws_send(conn, multi([cm_message(768, pb_bytes(1, "fake")), response, cm_message(780, b"")]))
        elif emsg == 703:
            state.log("cm: heartbeat")
        elif emsg == 8905:
            assert logged_on
            if state.mode == "kick":
                ws_send(conn, cm_message(757, pb_varint(1, 6)))
                state.log("cm: kicked")
                logged_on = False
                continue
            reply = pb_bytes(3, pb_varint(1, APP) + pb_varint(2, 555))
            ws_send(conn, cm_message(8906, reply, job_target=job))
        elif emsg == 8903:
            app = pb_fields(first(body, 2))
            assert first(app, 1) == APP and first(app, 2) == 555
            kv = ['"appinfo"\n{\n\t"appid"\t\t"%d"\n\t"depots"\n\t{\n' % APP]
            for depot, d in state.depots.items():
                kv.append('\t\t"%d"\n\t\t{\n\t\t\t"manifests"\n\t\t\t{\n\t\t\t\t"public"\n\t\t\t\t{\n'
                          '\t\t\t\t\t"gid"\t\t"%d"\n\t\t\t\t\t"size"\t\t"1"\n\t\t\t\t}\n\t\t\t}\n'
                          '\t\t\t// a comment\n\t\t\t"dlcappid"\t\t"1"\n\t\t}\n' % (depot, d.gid))
            kv.append('\t}\n}\n')
            text = "".join(kv).encode() + b"\0"
            ws_send(conn, cm_message(8904, pb_varint(6, 1), job_target=job))
            info = pb_varint(1, APP) + pb_varint(2, 99) + pb_bytes(5, text)
            ws_send(conn, multi([cm_message(8904, pb_bytes(1, info), job_target=job)], zipped=False))
        elif emsg == 5438:
            depot = first(body, 1)
            if depot == WORKSHOP_DEPOT and state.mode != "ws_not_owned":
                reply = pb_varint(1, 1) + pb_varint(2, depot) + pb_bytes(3, state.workshop_key)
            elif state.mode == "not_owned" and depot == 976734 or depot not in state.depots:
                reply = pb_varint(1, 2) + pb_varint(2, depot)
            else:
                reply = pb_varint(1, 1) + pb_varint(2, depot) + pb_bytes(3, state.depots[depot].key)
            state.log("cm: depot key %d" % depot)
            ws_send(conn, cm_message(5439, reply, job_target=job))
        elif emsg == 706:
            state.log("cm: logoff")
            ws_send(conn, cm_message(757, pb_varint(1, 1)))
            logged_on = False
        else:
            state.log("cm: emsg %d ignored" % emsg)


def cdn_client(state, conn):
    buffer = b""
    count = 0
    while True:
        while b"\r\n\r\n" not in buffer:
            try:
                piece = conn.recv(65536)
            except OSError:
                return
            if not piece:
                return
            buffer += piece
        head, buffer = buffer.split(b"\r\n\r\n", 1)
        line = head.split(b"\r\n")[0].decode()
        path = line.split(" ")[1]
        parts = path.strip("/").split("/")
        count += 1
        status, body = 404, b"not found"
        try:
            depot = int(parts[1])
            if parts[0] == "depot" and depot == WORKSHOP_DEPOT and parts[2] == "manifest":
                gid = int(parts[3])
                item = next((i for i in state.workshop if i.depot.gid == gid), None)
                if item and state.codes.get((depot, gid)) == int(parts[5]):
                    status, body = 200, item.depot.manifest_zip
                    if state.mode == "ws_wrong_manifest":
                        # (another item's manifest under this one's gid)
                        body = next(i for i in state.workshop if i is not item).depot.manifest_zip
                    state.log("cdn: workshop manifest %d" % item.id)
                else:
                    status, body = 403, b"bad code"
                    state.log("cdn: workshop manifest refused")
            elif parts[0] == "depot" and depot == WORKSHOP_DEPOT and parts[2] == "chunk" and \
                    parts[3] in state.workshop_chunks:
                status, body = 200, state.workshop_chunks[parts[3]]
                if state.mode == "corrupt_once" and parts[3] not in state.corrupted and len(state.corrupted) < 3:
                    state.corrupted.add(parts[3])
                    body = body[:40] + bytes([body[40] ^ 0x55]) + body[41:]
                    state.log("cdn: chunk corrupted")
            elif parts[0] == "depot" and parts[2] == "manifest" and depot in state.depots:
                gid, code = state.codes.get(depot, (None, None))
                if int(parts[3]) == gid and int(parts[5]) == code:
                    status, body = 200, state.depots[depot].manifest_zip
                    state.log("cdn: manifest %d" % depot)
                else:
                    status, body = 403, b"bad code"
                    state.log("cdn: manifest %d refused" % depot)
            elif parts[0] == "depot" and parts[2] == "chunk" and parts[3] in state.depots[depot].chunks:
                status, body = 200, state.depots[depot].chunks[parts[3]]
                if state.mode == "corrupt_once" and parts[3] not in state.corrupted and len(state.corrupted) < 3:
                    state.corrupted.add(parts[3])
                    body = body[:40] + bytes([body[40] ^ 0x55]) + body[41:]
                    state.log("cdn: chunk corrupted")
        except (IndexError, ValueError, KeyError):
            pass
        if state.mode == "outage" and status == 200 and parts[2] == "chunk":
            # (the Wi-Fi gone for 8 s after the fourth chunk: every connection cut at once meanwhile)
            if state.outage_start is None and count > 3:
                state.outage_start = time.time()
                state.log("cdn: outage begins")
            if state.outage_start is not None and time.time() - state.outage_start < 8:
                conn.close()
                return
        if state.mode == "drop_once" and not state.dropped and status == 200 and parts[2] == "chunk" and count > 3:
            state.dropped = True
            conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n" % len(body) + body[:len(body) // 2])
            state.log("cdn: dropped")
            conn.close()
            return
        if state.mode in ("slow", "losesession"):
            time.sleep(0.25)
        reason = {200: "OK", 403: "Forbidden", 404: "Not Found"}[status]
        if count % 3 == 0:
            # (chunked, in two pieces)
            half = len(body) // 2
            payload = b"%x\r\n%s\r\n%x\r\n%s\r\n0\r\n\r\n" % (half, body[:half], len(body) - half, body[half:])
            conn.sendall(b"HTTP/1.1 %d %s\r\nTransfer-Encoding: chunked\r\nConnection: keep-alive\r\n\r\n" %
                         (status, reason.encode()) + payload)
        else:
            conn.sendall(b"HTTP/1.1 %d %s\r\nContent-Length: %d\r\nConnection: keep-alive\r\n\r\n" %
                         (status, reason.encode(), len(body)) + body)


def serve(listener, handler, state):
    def run(conn):
        try:
            if state.tls:
                conn = state.tls.wrap_socket(conn, server_side=True)
            handler(state, conn)
        except (ssl.SSLError, OSError) as error:
            state.log("tls: refused (%s)" % type(error).__name__)
        finally:
            conn.close()

    while True:
        conn, _ = listener.accept()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        threading.Thread(target=run, args=(conn,), daemon=True).start()


def write_fixtures(folder, seed):
    """chunks for the C unit tests: <name>.bin (encrypted), with a line in
    fixtures.txt: name sha adler size compressed_size key"""
    os.makedirs(folder, exist_ok=True)
    rng = random.Random(seed)
    key = rng.randbytes(32)
    lines = []
    for index, container in enumerate(CONTAINERS * 2):
        piece = map_bytes(rng, rng.randrange(1000, 200000)) if index < 3 else rng.randbytes(rng.randrange(16, 5000))
        encrypted = symmetric_encrypt(key, container(piece))
        name = "chunk%d_%s" % (index, container.__name__)
        with open(os.path.join(folder, name + ".bin"), "wb") as out:
            out.write(encrypted)
        lines.append("%s %s %08x %d %d %s" % (name, hashlib.sha1(piece).hexdigest(), adler_zero(piece), len(piece),
                                               len(encrypted), key.hex()))
    with open(os.path.join(folder, "fixtures.txt"), "w") as out:
        out.write("\n".join(lines) + "\n")
    # a manifest (encrypted names) and its key, for the parser's test
    depot = Depot(976736, seed)
    with open(os.path.join(folder, "manifest.zip"), "wb") as out:
        out.write(depot.manifest_zip)
    with open(os.path.join(folder, "manifest.key"), "w") as out:
        out.write(depot.key.hex() + "\n%d\n%d\n" % (len(depot.files), depot.gid))
    # the fuzz targets' seeds (steam_fuzz.c's input forms)
    seeds = os.path.join(folder, "seeds")
    plain = Depot(976733, seed, encrypt=False)

    def seed_file(target, name, data):
        os.makedirs(os.path.join(seeds, target), exist_ok=True)
        with open(os.path.join(seeds, target, name), "wb") as out:
            out.write(data)

    seed_file("manifest", "plain", plain.manifest)
    seed_file("manifest_zip", "plain", plain.manifest_zip)
    seed_file("protobuf", "payload", plain.manifest[8:8 + 4000])
    for index, container in enumerate(CONTAINERS):
        piece = map_bytes(rng, 3000 + index * 1000)
        seed_file("chunk", "c%d" % index, struct.pack("<I", len(piece)) + container(piece))
    kv = ('"appinfo"\n{\n\t"depots"\n\t{\n\t\t"976733"\n\t\t{\n\t\t\t"manifests"\n\t\t\t{\n'
          '\t\t\t\t"public"\n\t\t\t\t{\n\t\t\t\t\t"gid"\t"886692270880710564"\n\t\t\t\t}\n'
          '\t\t\t}\n\t\t}\n\t\t"x" "a\\"b" [$WIN32] // c\n\t}\n}\n\0')
    seed_file("kv", "appinfo", kv.encode())
    seed_file("ws_frames", "stream", bytes([0x02, 0x02, 0x61, 0x62, 0x89, 0x00, 0x80, 0x01, 0x63, 0x82, 0x7e, 0x00,
                                            0x80]) + b"x" * 128 + bytes([0x88, 0x02, 0x03, 0xe8]))
    seed_file("ws_upgrade", "ok", b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
              b"Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n")
    seed_file("http", "length", b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello")
    seed_file("http", "chunked", b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n2\r\nde\r\n0\r\n\r\n")
    inner = [cm_message(751, pb_varint(1, 1) + pb_varint(3, 9), steamid=76561198000000000, session=42),
             cm_message(147, pb_bytes(2, "https://s.team/q/1/x"), job_target=3, eresult=1)]
    seed_file("cm", "multi_gzip", multi(inner))
    seed_file("cm", "multi_plain", multi(inner, zipped=False))
    seed_file("cm", "single", inner[1])
    server = pb_bytes(1, "SteamCache") + pb_bytes(8, "cache1.steamcontent.com") + pb_bytes(12, "mandatory") + \
        pb_bytes(13, varint(976730) + varint(5))
    seed_file("cdn_list", "servers", b"\0" + pb_bytes(1, server))
    seed_file("cdn_list", "cms", b"\1" + b'{"response":{"serverlist":[{"endpoint":"cmp1-ord1.steamserver.net:443"}]}}')
    seed_file("journal", "j", b"steamdl-journal 1\ndepot 1\ngid 2\nsize 3000\nchunks 3\n0 " + b"0" * 40 + b"\n2 " +
              b"a" * 40 + b"\n")
    workshop_key = random.Random(seed * 13).randbytes(32)
    items = [WorkshopItem(entry, seed, workshop_key) for entry in workshop_items(seed)]
    seed_file("ws_list", "query", pb_varint(1, len(items)) + b"".join(pb_bytes(2, i.details()) for i in items[:10]))
    seed_file("ws_list", "details", b"".join(pb_bytes(1, i.details()) for i in items[:4]))
    names = [name.encode("utf-8", "surrogateescape") for i in items for name in i.depot.files]
    seed_file("ws_names", "items", b"\0".join(names[:8]))
    seed_file("ws_names", "real", b"\0".join([b"maps\\AvalancheAIBattleground.map", b"ModInfo.json",
                                              b"modded halo3.dll (REQUIRED)\\halo3.dll", b"images\\thumbnail.png",
                                              b"multiplayer\\AvalancheAIBattleground.json"]))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--work", default=".")
    parser.add_argument("--mode", default="normal")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--fixtures")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--chunk-kb", type=int, default=48)
    parser.add_argument("--scale", type=int, default=1)
    parser.add_argument("--cert")
    parser.add_argument("--key")
    parser.add_argument("--workshop-map", help="a real MCC map file as Workshop item 2900000001's map (local tests)")
    parser.add_argument("--workshop-mp-map", help="a real MCC multiplayer map as Workshop item 2900000002's (local tests)")
    parser.add_argument("--workshop-mp-name", help="its file name in the item (default: the file's own)")
    args = parser.parse_args()
    if args.fixtures:
        write_fixtures(args.fixtures, args.seed)
        return
    global CHUNK, SCALE
    CHUNK = args.chunk_kb * 1024
    SCALE = args.scale
    state = State(args)
    listeners = []
    for handler in (cm_client, cdn_client):
        listener = socket.socket()
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((args.host, 0))
        listener.listen(8)
        listeners.append(listener)
        threading.Thread(target=serve, args=(listener, handler, state), daemon=True).start()
    print(json.dumps({"cm": listeners[0].getsockname()[1], "cdn": listeners[1].getsockname()[1],
                      "token": state.refresh_token, "access": state.access_token}), flush=True)
    sys.stdin.read()


if __name__ == "__main__":
    main()
