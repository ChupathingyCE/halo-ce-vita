# Steam download on the Vita: security rules

Status: rules for phase 1 (Nov 8 2026, branch feat/vita-steam-download-2, 1.2.0). Written before any login code.
**1.1.x backport (Nov 15 2026, branch feat/11x-steam-download, a prototype behind configure.py --steam-download):
Halo CE alone, depots 976733 and 976734, the files into ux0:data/haloce-vita; the device Steam shows is "Halo CE
Vita (PS Vita)". Section 6 is the 1.1.x surface's review.**
Every rule below is a MUST. A change that breaks one is a security bug, not a style question.
Design and feasibility: docs/steam_download_design.md (Nov 3). The code: port/steamdl/. Its tests: port/vita/tests/run_steam_download_test.sh.

The feature: the game downloads the player's **own** Halo CE files from Steam (MCC app 976730, depots 976733
and 976734; 1.2.0 adds Halo 2's 976735 and 976736) straight to the Vita, after the player signs in by QR code with
the Steam phone app. No server of ours is in between, nothing is cached or shared for anyone else.

## 1. What we protect (assets)

| Asset | Why it matters | Where it may live |
|---|---|---|
| The player's Steam account | Months of access if a refresh token leaks | Nowhere of ours |
| The QR auth session (client_id, request_id, challenge URL) | Whoever polls it first gets the tokens once approved | RAM, for the QR screen only |
| Refresh token (JWT) and access token | Full account sign-in until revoked or expired | **RAM only**, one struct, wiped after use |
| Depot keys (32-byte AES), manifest request codes, PICS access tokens | Let anyone with the CDN address read the depot | **RAM only**, wiped after use |
| The memory card | A hostile server must not write outside our folders or fill it | Only the planned files, at their planned sizes |
| The Vita process | Parsers read hostile bytes from the network | Every parser bounded and fuzzed |

## 2. Who attacks (threat model)

1. **A hostile network / man in the middle** (public Wi-Fi, a rogue access point, DNS spoofing). Wants the
   tokens, or to feed us a fake QR or fake files.
2. **A hostile or broken Steam-side server** (a CM or CDN impostor that got past TLS, a compromised cache,
   a protocol change). Sends oversized, truncated, contradictory or malicious messages, manifests and chunks.
3. **A phishing party** who shows the player a QR of their own, so the player approves a session that is
   not on their Vita.
4. **Someone with the memory card later** (a shared or sold Vita, a backup copied off the card). Must find
   no secret there.
5. **Our own mistakes**: logging a token, a crash dump with tokens in it, a resume file with a key in it.

Not in scope: an attacker with code execution on the Vita already (they own the process), and Valve itself.

## 3. The rules

### 3.1 Sign-in

- **QR sign-in only.** There is no password field, no Steam Guard code field, anywhere. The Vita never sees a
  password. (Authentication.BeginAuthSessionViaQR + PollAuthSessionStatus.)
- The QR is made on the Vita from the challenge URL Steam returned over TLS. The screen says: approve only if
  you just started this on your Vita. The device name Steam shows the player is "Halo CE Vita (PS Vita)".
- The game says before sign-in, plainly: this is unofficial, Valve may stop it working at any time. (Phase 2: the
  screen.)
- The QR session is dropped (client_id and request_id wiped) as soon as it ends: approved, refused, expired,
  cancelled, or the screen left.

### 3.2 Tokens, keys and codes

- The refresh token, access token, depot keys, PICS access tokens and manifest request codes live **only in
  RAM**, inside `struct steam_secrets` (port/steamdl/steam_session.h). Transient copies are wiped as soon as
  they are done with: the message being built and its masked WebSocket frame (after the send), the reply
  copied out of a received message (after it is read), the request code's URL (after the request), and the
  WebSocket's receive buffers and the secrets struct at the session's end. (Mbed TLS wipes its own record
  buffers when the connection is freed.)
- They are **never written** to ux0:, ur0: or any file, **never logged** (the log gets "token: N bytes" at most),
  never put in a URL we log, never put on screen, never in an error string.
- The manifest request code does go in the CDN URL (that is the protocol); the URL is never logged with it.
- **After the download, on cancel, on any failure and on every exit path** (including the PS button exit and
  a power warning):
  1. Authentication.RevokeToken(token = our refresh token, revoke_action = permanent);
  2. ClientLogOff;
  3. close the WebSocket;
  4. `mbedtls_platform_zeroize` every secret, then free.
  `steam_session_end()` does all four and is safe to call twice and from every error path. Nothing is kept,
  so the next session needs a new QR.
- If the process dies before the revoke (power loss, crash), the token dies with the RAM. The game writes a
  **no-secret marker** at sign-in (`steam-session.txt`: the date and the device name, nothing else) and removes
  it after a successful revoke. If the marker is there at the next start, the game tells the player to remove
  "Halo CE Vita (PS Vita)" in the Steam app under Authorized devices.
- A crash dump on the Vita (psp2core) would hold RAM. Secrets are held for as short a time as possible and
  wiped right after use; the crash-dump-first workflow must never ask a tester to share a dump taken during a
  Steam sign-in (phase 2: the game's crash text says so for this screen).

### 3.3 Transport

- **TLS always**, 1.2 or 1.3, with the certificate chain checked against the roots built into the game
  (port/linux/src/update_roots.h: ISRG Root X1/X2, which the CM, CDN and API chains end at) and the host name
  checked. No system store, no "accept anyway", no fallback to plain HTTP.
- Nothing is sent before the handshake and its checks complete.
- The WebSocket runs inside that TLS session (wss://). A plain ws:// or http:// URL is refused unless the host
  is a loopback address (127.0.0.0/8, ::1), which only the PC tests' fake server uses.
- CM and CDN host names come from Steam's directory over TLS and must be plain DNS names (letters, digits,
  '-', '.'), at most 253 characters; anything else is skipped.
- Every network wait has a deadline. A stalled server cannot hold the screen forever.

### 3.4 Everything a server sends is bounded

Every count, length and offset from the network is checked against the bytes actually there and against a fixed
cap **before** it is used. The caps (port/steamdl/steam_limits.h):

| Input | Cap |
|---|---|
| WebSocket frame payload | 4 MB; control frames 125 bytes; a fragmented message 4 MB total |
| One CM message (after Multi unwrapping) | 4 MB; a Multi's unzipped size 8 MB; Multi nesting depth 1 |
| Protobuf | lengths checked against the buffer; varints at most 10 bytes; nested messages read one level at a time by the caller, depth at most 8 |
| KeyValues text (PICS appinfo) | 1 MB, depth 32, a key or value 4096 bytes |
| Manifest | zipped 64 MB, unzipped 64 MB; 100,000 files; 1,000,000 chunks in all; 65,536 chunks in one file; a file name 1,024 bytes |
| One chunk | compressed 16 MB, decompressed (cb_original) 16 MB; the decompressed length must equal cb_original exactly |
| A file | its manifest size, and at most 2 GB - 1 (the Vita's signed 32-bit stdio offsets); chunks must lie inside the file, not overlap, and cover it exactly |
| Card space | checked before starting: the planned bytes plus 5% |

The LZMA and zstd decoders are given the exact output size and never write past it. Every parser has a
libFuzzer + AddressSanitizer + UBSan target, run for at least 10 minutes with no finding before it ships
(section 5).

### 3.5 Files and paths

- **A depot file name never becomes a path.** The plan (port/steamdl/steam_plan.c) is our own table of exact
  depot paths (from the Setup installer's mcc_plan and h2_plan) and our own destination paths. A manifest name
  is only **compared** with the table, case-insensitively, after `\` becomes `/`. A hostile name can only fail
  to match.
- Names that are not plain are rejected before the comparison anyway: no `..` part, no leading `/` or `\`, no
  drive letter or `:`, no control characters, no empty parts.
- (1.2.0 only) The only names not in the table are Halo 2's font files, which font_table.txt lists; those must
  be plain file names (no `/`, `\`, `:`, `..`), go only into halo2/h2_fonts, and must also be in the manifest.
  1.1.x has no such exception: every written path is the table's.
- Only files inside the target folder are written (1.1.x: ux0:data/haloce-vita/mcc/halo1/...; 1.2.0:
  ux0:data/project-ark/mcc/... for Halo CE and project-ark[-insider]/halo2/... for Halo 2), as `<name>.part` first,
  renamed over the real name only after the whole file checks out (3.6).
- No symlinks are followed or made (the manifest's linktarget is ignored; symlink entries are skipped).

### 3.6 Data integrity

- Each chunk is decrypted (AES-256: the first 16 bytes ECB give the IV, the rest CBC with PKCS#7 padding,
  padding checked), decompressed by its magic (VZa LZMA, VSZa zstd, PK zip/deflate), and then checked:
  length == cb_original, Adler-32 (seed 0) == the manifest's crc, **and** SHA-1 == the chunk id. The
  container's own CRC-32 is checked too. Only then is it written at its offset. A chunk that fails is thrown
  away and fetched again (at most 3 times), never written.
- When a file is complete, its SHA-1 is checked against the manifest's sha_content, then the header check
  Setup uses (MCC map: "daeh" + version 13; bitmaps.map: 01 00 00 00; FSB5; lst version 1; Halo 2 map
  header), then it is renamed into place.
- The manifest itself comes over TLS from the CDN with a request code from the authenticated CM session;
  its names are decrypted with the depot key Steam gave this account. (Valve's RSA signature on the manifest
  is not checked: we do not have Valve's key. TLS + the depot key carry the trust; the chunk SHA-1s carry it
  from the manifest to the bytes.) The manifest's metadata must name the depot and gid asked for, and its
  crc_clear (CRC-32 of the payload's length then the payload) must match.

### 3.7 Resume journal

- The journal (`<file>.part.journal`) holds the depot id, the manifest gid, the file size and the SHA-1s of
  the chunks already written and checked. **No token, key, request code, account name or steamid.**
- It is a hint only: on resume, every chunk it names is read back from the .part file and its SHA-1 checked
  again before it counts as done. A damaged or edited journal can only cost a re-download.

### 3.8 Account and Steam rules

- Only depots in our table, only for app 976730, only on the public branch, only what the account owns
  (Steam refuses a depot key otherwise). No ownership is faked or bypassed. The sound banks (the player's
  choice, STEAM_GAME_HALO_CE_BANKS) are files of depot 976733 like the maps: no extra depot, no extra right.
- One sign-in, one download, sign out. No background sessions, no saved logins, no "remember me".
- Nothing goes through a server of ours, nothing is uploaded, nothing is shared with other players.

## 4. What phase 1 does and does not do

Phase 1 (this branch) writes and tests the pieces on the PC harness **without any Steam sign-in**: the
protobuf reader and writer, the manifest parser (checked against the owner's local depotcache manifests,
read-only), the chunk pipeline, the WebSocket client, CM framing, KeyValues, and the QR / logon / PICS / depot
key / request code / CDN / revoke / log-off steps against a **local fake server** only. No real Steam login,
no Steam CDN download. The first real sign-in needs the owner's say, with his own account.

## 5. Checks before it ships

- Fuzz targets (port/vita/tests/steam_*_fuzz.c): protobuf, manifest (raw and zipped), KeyValues, WebSocket
  frames, CM messages (with Multi), chunk processing (headers, LZMA, zstd, zip). Each runs >= 10 minutes under
  libFuzzer + ASan + UBSan with no finding.
- The fake-server test checks: the session ends with RevokeToken then ClientLogOff on success **and** on every
  injected failure; no token bytes appear in the log, in any file written, or in the journal; plain ws:// to a
  non-loopback host is refused; a hostile manifest name never leaves the target folder.
- On the Vita (phase 2): a consented sign-in with the owner's account to confirm RevokeToken removes the device
  from Authorized devices.

## 6. The 1.1.x surface (feat/11x-steam-download, Nov 15 2026): review

What was reviewed: the backported port/steamdl (phase 1 + phase 2 of 1.2.0, Halo 2 and its fonts removed), the
GAME MANAGER screen (menu_functions.c h3_steam_*), the Vita device side (vita_steam.c), the build switches, the
tests. The protocol's field numbers were checked, read-only, against Valve's published .proto dumps
(SteamDatabase/Protobufs: steammessages_auth, _clientserver_login, _clientserver_appinfo): ClientLogon 1/3/6/8/50/96/108,
BeginAuthSessionViaQR 1-4 and its response 1-4, PollAuthSessionStatus 1-2 and its response 1-6, PICS access
token 2 / 3.1-2, product info 2.1-2 + 7 (single_response) / 1.1,1.5 + 6, RevokeToken (CAuthentication_Token_Revoke_Request
1 token, 2 revoke_action = 1 permanent) all match the code. No .proto is vendored.

**What is trusted**
- TLS to Steam's own hosts, chains checked against the roots built into every 1.1.x build (update_roots.h: ISRG
  Root X1 and X2 are there since the updater moved to Let's Encrypt), the host name checked. The CM list comes
  from api.steampowered.com over the same TLS (posix_https_get), the CDN list from the authenticated CM session.
- The depot key Steam gives this account (it decrypts the manifest's names and every chunk), the manifest's
  SHA-1 per chunk and per file. Valve's RSA signature on the manifest is not checked (no key of Valve's in the
  tree).
- Nothing else: every byte from a CM or CDN goes through a bounded parser.

**Every parser fuzzed** (port/vita/tests/steam_fuzz.c, 11 targets: protobuf, manifest, manifest_zip, kv, ws_frames,
ws_upgrade, cm, http, chunk, journal, cdn_list) under libFuzzer + ASan + UBSan, 32-bit as the Vita where the
compiler can. The 1.2.0 run of 610 s each found nothing after the qsort(NULL, 0) fix; the 1.1.x run is in
~/halo-test-out/steam11x-runs/NOTES.md. The one parser 1.1.x adds is none: the plan table shrank.

**Build switch**: configure.py --steam-download compiles port/steamdl, the screen and vita_steam.c in
(HALO_STEAM_DOWNLOAD). Without it (1.1.x's default, every release and insider build so far) nothing of the Steam
download is in the binary: the GAME MANAGER row is not shown, halo3/steam.xml is parsed and never opened,
mods.xml's open= leads nowhere (h3_mods_toggle answers FALSE on GAMES). --steam-test-ca (test builds only;
configure.py refuses it with --release) adds the fake server's authority and the env.txt hooks
(HALO_STEAM_TEST_CM/_CDN/_ROOT): a game build never has them, and run_menus_test.sh's h3steam case checks the
binary for the hook's name before it uses them.

**Credentials and tokens**: never persisted. The only file the sign-in writes besides the game's files is the
no-secret marker steam-session.txt (a date and the device name). The journal (`.part.journal`) holds depot id,
gid, size and chunk SHA-1s. The fake-server flows grep every file written and the log for the fake tokens
(refresh and access) after each run.

**Logs scrubbed**: the log hears sizes and results ("token: N bytes", "depot N key received", "QR N up"), never the
challenge URL, a token, a key or a request code. The CDN URL that carries the request code is never logged. The
PC test's "flow" mode writes the fake's challenge URL to its own log on purpose (a made-up URL); the "live" mode
(the owner's test) does not.

**Findings of this review** (fixed on the branch unless said otherwise)
1. **32-bit millisecond clocks**: steam_ws.c, steam_http.c and steam_session.c kept `(long)tv_sec * 1000` of
   CLOCK_MONOTONIC. On a 32-bit build (the Vita, the -m32 harness) that overflows after 24.8 days of uptime, and
   UBSan caught it on this PC (uptime 29 days): every deadline, heartbeat and timeout would go wrong on a Vita
   that has not been rebooted for 25 days. Widened to long long. **1.2.0 has the same bug** (port/steamdl at
   5739e4463): to port.
2. **PICS over HTTP** (not fixed, flagged for the live test): CMsgClientPICSProductInfoResponse may leave the
   appinfo buffer empty and name http_host + sha when the appinfo is larger than http_min_size. The code then
   fails with "no app info for 976730 (or it is offered over HTTP)", revokes and logs off - safe, but the
   download does not run. MCC's appinfo is large (60+ depots). The live test tells; if it bites, the fix is one
   HTTPS GET of the appinfo text (gzip) from http_host, through the same bounded KeyValues parser.
3. **Logon fields** (flagged): CMsgClientLogon goes without client_os_type (7) and machine_id (30); SteamKit sends
   both. Steam may accept the logon without them (steam-user and DepotDownloader work with defaults) or refuse
   it (an EResult in the logon response, which the code reports and the live test shows). No secret is at
   stake either way: the refresh token is revoked on the failure path.
4. **The sound banks' size**: the four CELT banks are 1.5 GB of the 2 GB (the music alone 957 MB). They are the
   player's choice on the screen (ON by default, since the game's dialogue and music are silent without them),
   so a player on a small card or slow Wi-Fi can leave them out. Not a security point: noted because the
   free-space check runs per depot (the planned bytes + 5%).
5. **vita_steam.c's power callback** aborts the job with a 4 s budget on a suspend warning; the Vita gives about
   that. If the revoke does not make it, the marker stays and the next start says to remove the device in the
   Steam app (rule 3.2). Unchanged from 1.2.0; the owner's live test on a Vita is the only proof.
6. **The test's `flow` mode logs the fake challenge URL** ("QR: ..."): test-only, the fake's. The live mode and the
   game never do. Left as it is, noted so nobody copies the line into the game.

## 7. The Steam Workshop (feat/11x-steam-workshop, Nov 16 2026, for 1.1.3: off by default)

Build switch: configure.py --steam-workshop (implies --steam-download; HALO_STEAM_WORKSHOP). Without it the binary
has no STEAM WORKSHOP row or screen; port/steamdl/steam_workshop.c is compiled with the Steam download (the job's
Workshop path is then unreachable: nothing calls steam_job_start_workshop).

What it does: DOWNLOAD FROM STEAM's third row opens STEAM WORKSHOP (menus/halo3/workshop.xml). The player signs in
by QR exactly as for the depots (3.1-3.3 hold unchanged: tokens and keys in RAM only, revoke + log off on every path,
the marker, TLS always). Signed in, the list is PublishedFile.QueryFiles#1 over the same CM session (app 976730,
required tag Halo1, ten a page, details and tags returned); the player marks up to 8 items; their details are
PublishedFile.GetDetails#1; each item's content is depot 976730's manifest whose gid is the item's hcontent_file
(depot key of 976730, request code, CDN), chunk-checked as any depot (3.6). Then the session ends. Only after that,
on the game's thread, each item is checked again and moved into the mods folder (steam_workshop_install).

Provenance: written from Steam's published protobufs (SteamDatabase/Protobufs, steammessages_publishedfile) and the
UGC download sequence ChupathingyCE's MIT Steam content client shows (Milenko x Horrible Dev); no code text of
ChupathingyCE or OpenCE (~/halo-test-out/workshop-runs/NOTES.md has the per-file table).

**Rules (MUSTs) the Workshop adds**

- **No web page, no site, no third party.** The list comes only from Steam's CM over the signed-in TLS session; no
  steamcommunity.com page is read, no list of anyone else's, no image fetched. Nothing is downloaded from another
  player, ever (a joiner who lacks a map is never sent one by a host: it may download the item from Steam itself,
  section 8).
- **What Steam says about an item is checked** (steam_workshop_item_ok): result OK, consumer app 976730, tag Halo1,
  not banned, has content (hcontent_file != 0), size at most 4 GB, an id of at most 12 digits (the mods folder's key).
  The manifest must name depot 976730 and that gid, its crc_clear must match.
- **Names become paths here, so a name is a rule, not a lookup** (steam_workshop_name). A name that tries to leave the
  item's folder - an empty, "." or ".." part, a leading '/', a drive, ':', a control byte, longer than 1024 - refuses
  the WHOLE item. A name merely odd is skipped (never written, the item kept): deeper than 4 parts, a part over 64
  bytes, the whole over 160, hidden (leading '.'), ending in '.' or ' ' (trimmed by Windows and the Vita's file
  system), a character outside [A-Za-z0-9 _.-()+,'&!]. Only five kinds are ever written: .map, .json/.txt (1 MB),
  .png/.bmp (16 MB); everything else (a DLL, an EXE, a script) is skipped. Symlinks and directory entries are never
  made. Two names one file on a case-blind card refuse the item; at most 512 files. (Checked against the 147 real
  Workshop manifests in this PC's Steam depotcache: 144 have no refused name; the 3 others only skipped names - a
  73-byte screenshot name, "campaign/.json".)
- **Header checks** before a file is renamed into place: a .map must be "daeh" + version 13 (bitmaps/sounds/loc.map:
  the resource header, by name), a PNG its signature, a BMP "BM", text no NUL in its head.
- **Staging, then the game's own check, then the mods folder.** Files go to <data root>/workshop/<id>/ (.part +
  journal, resumable); <id>.gid names the version (another version starts over); <id>.done is written only when every
  file is in and checked. The install walks the staged folder again (every name by the same rule, as written, no
  stray .part, journal or link, no collision, the total size) and runs every map through
  custom_edition_cache_file_check - the same check the mods folder's loader makes, with the CE/MCC loader caps of
  sec/114 once merged - and needs at least one map. Only then is the folder renamed to mods/<id> (an older version
  there removed first). A refused or failed item's staging is deleted: nothing of it is kept. A cancelled or lost
  session keeps it for a resume.
- **What plays**: the mods folder's MCC mods - Halo: CE Anniversary caches of solo scenarios, offline, for a game of
  this machine's own (docs/mcc_cea_maps.md "Offline only"): unchanged. Multiplayer items are downloaded too and
  their maps are the multiplayer lists', offline and online (section 8).

**Fuzzed**: steam_fuzz.c ws_list (QueryFiles/GetDetails responses: titles printable ASCII, counts capped) and
ws_names (the name rule: a taken name is plain, ours, inside the folder). **Fake-server flows**: ws-items (a campaign
and a multiplayer item installed; names leaving the folder, two names one file, another game's, banned, a version 7
map refused; a map refused by the map check), ws-names (a drive, a control byte, an item without maps), ws-corrupt,
ws-cancel then ws-resume, ws-update (installed over), ws-not-owned, ws-wrong-manifest, ws-hostile-list (malformed),
each ending revoke then log off, no token in the log or any file. **Menus (headless)**: run_menus_test.sh h3workshop
(English and Spanish): sign-in, QR, pages, a typed search, the order, two items marked and downloaded, one real MCC
map installed by the game's own check (its bytes compared), one made-up map refused.

**Open for the live test** (docs/steam_download_live_test.md "The Workshop"): whether an account that owns MCC is
given depot 976730's key and request codes for Workshop content (Chupathingy's client does exactly this); whether
QueryFiles' required tag "Halo1" and its paging behave as the fake does; real items' sizes and names.

**Findings of this review**
1. Chupathingy keeps the refresh token on disk (a secret store or a file) to stay "linked": not ported (3.2).
2. Chupathingy falls back to plain-HTTP CDN servers when none offers HTTPS: not ported (3.3: TLS always).
3. Chupathingy lists from Steam's public browse page (HTML) or a site's list: not ported (no third-party list, no
   HTML parser on the Vita).
4. Real Workshop items carry DLLs ("modded halo3.dll (REQUIRED)/halo3.dll" in this PC's items): never written here.
5. A name of a real item would have refused the whole item under a first, stricter rule (any odd name refused): the
   rule now refuses only names that leave the folder and skips the rest.

## 8. Steam Workshop multiplayer maps online (feat/11x-steam-workshop-mp, Nov 16 2026: off by default, same switch)

Owner decisions (Nov 16): Workshop multiplayer maps play offline (bots, split screen) and online; a joiner who lacks
the host's map is asked to download it from Steam - **never from the host: no game files between players, ever**; the
Workshop is reachable from the settings overlay. Same build switch as section 7 (configure.py --steam-workshop).

### 8.1 What is played, and under which name

A Workshop item the game installed (section 7) is the mods folder's `mods/<id>`. Its `maps/*.map` files that are
Halo: CE Anniversary caches of **multiplayer** scenarios (custom_edition_cache_mod_map_kind: the cache loader's own
header and tag-index check) are the multiplayer maps `ws<id>_<map>` (port/linux/game/workshop_maps.c), level
`levels\test\ws<id>_<map>\ws<id>_<map>`, at most 25 characters like every custom map (a long file name keeps its first
characters, '-' and four hex digits of FNV-1a of the whole: `ws2900000002_coldsna-8866`; two maps of one item that
would share a name are both left out, so every machine resolves a name to the same file or none). They are listed in
the multiplayer level list after the maps folder's (custom_edition_maps.c), in every menu style that lists custom
maps, whatever MCC mods (game.mcc_mods) and PC maps are; a mod turned off in the mod manager is left out. The pattern
is reserved: a maps-folder file of that form is never listed or loaded, and the name is only ever looked up in the
mods folder (custom_edition_map_path), never in the maps folder where map sharing writes. Loading reuses the mod-map
path (from_mods): sound cache in `mods/<id>/cache`, resource maps beside the map, else the item's own
`halo1/maps/bitmaps.map` / `sounds.map` (where real Workshop multiplayer items keep them: seen in this PC's depotcache
manifests, item manifests 4520562000989635516 and 4896837317637153445), else the player's own MCC's
(`d:\mcc\halo1\maps\`, read only). Then the same loader checks as every CE/MCC map (the release-1.1.4-int hardening:
tag check, geometry bounds, model conversion bounds; merged into this branch).

### 8.2 The network fields: none added

| Field | Who sends it | Bounds | What this build does with it |
|---|---|---|---|
| `network_game.map.name` (0x80 bytes, existing) | the host's game settings | already ended at 0x80 and refused with ".." (network_client_manager.c); the Workshop parser reads at most 0x80, needs the terminator inside, exactly `levels\test\N\N`, N = "ws" + 1-12 digits (no leading zero) + '_' + 1+ of [a-z0-9_-], <= 25 | the item id (<= 12 digits, < 10^12) and the map name; anything else is "not a Workshop map" and the existing checks decide |
| `network_game.map.version` (32 bits, existing) | the host | any value | the host's copy's identity (header checksum + length, custom_edition_cache_map_identity); compared to this machine's copy's |

There is **no new message, no new field, no capability bit, no version bump**. The item identity is the name's id plus
the copy's identity; the fingerprint is 32 bits (the same the custom-map check has always used). A collision cannot
hurt a joiner: whatever copy it plays is its own, installed through section 7's checks and loaded through the map
loader's own checks; a mismatch only means the two machines' tags differ (the existing "different copy" refusal).

### 8.3 The joiner's decisions (network_game_client_map_check, map_share.c)

Pure decision: `workshop_map_join_verdict` (plain C, unit-tested and fuzzed). In order, before the MCC/custom-map
checks and after the crossplay branch (a PC host's game is unchanged: section 8.6):
1. not a Workshop name -> the old checks;
2. host identity 0 -> refused ("isn't the same version"); a host of this build always sends one, so 0 is a host of
   another build or a modified one - **never** a download prompt;
3. no copy here -> `_network_game_client_map_workshop_missing`: in a Workshop build, the question "This map is from the
   Steam Workshop - download it from Steam?" (YES/NO, the item id and the map name in it; a public game adds that the
   host only named the item). Without the Workshop build: a plain refusal naming the item. Nothing is ever asked of
   the host (no map-share query is sent for a Workshop name);
4. a different copy -> refused, told to download the item again (Steam serves the current version; the host may need
   to update too). Never a download prompt (no loop of downloads a host can trigger), never the host's copy.
5. YES -> the Workshop job for that one item (`menu_functions_workshop_open_item`: steam_job_start_workshop, the id
   chosen at once - no list), its STEAM WORKSHOP screen over the lobby for the QR and progress. Everything of
   sections 3 and 7 applies unchanged: QR sign-in to the player's own account, TLS, revoke + log off on every path,
   GetDetails' checks (Halo1, app 976730, not banned, size), the name rule, header checks, staging, the game's own map
   check per map, then mods/<id>. Only after the session ended (game thread): install, look again, the identity
   compared again; equal -> precached and the join goes on; else the joiner leaves, told why (stopped, refused, still
   not the host's). The joiner adds its players to the lobby meanwhile (the host's start waits for its precache at
   most 180 s: a3c520d2e); a host that starts first ends the download (map_share_client_game_starting -> fail ->
   steam_job_cancel: revoked, logged off, what is in kept for a resume). Leaving the lobby cancels it the same way.
6. NO -> told, back at the menus; no Steam connection is opened.

A host can make a joiner's player see one question per join naming any 12-digit item id. The answer is the player's;
the item must pass every section 7 check; the download goes only to the player's own staging folder and is never
played unless it passes the map check. The question names the id so the player can look it up. No auto-answer in
release builds (HALO_MAP_SHARE_ANSWER is a test hook compiled out with HALO_RELEASE, as map sharing's).

### 8.4 The host's side: nothing is sent

`custom_edition_maps_shareable` is false for a Workshop name (both by the reserved pattern and because the map is a
mod map), so a map-share query from any joiner - an older build that takes it for a custom map it lacks - is answered
`_map_share_refusal_not_shared`, never an offer, never a byte. The dedicated server never looks in the mods folder,
so it cannot host one.

### 8.5 Older and other builds (crossplay)

- 1.1.2 and the 1.1.3 candidate without this branch: the name is a custom map they lack; they ask the host (map
  share), are refused "not shared", and leave with their usual message. Measured: run_netns_online_test.sh
  workshopold with build release-1.1.2 and release-1.1.4-int (no Workshop): see 8.7.
- Builds before map sharing: the old "custom map isn't in your maps folder" refusal.
- OpenCE / ChupathingyCE / GulchCE (the crossplay wire): this build hosts the crossplay wire only with Who can join
  Everyone; there the name is sent unchanged (no `custom_maps\` form: it is not a Custom Edition cache) and the
  version is 0 (network_identity's crossplay branch), so a PC client sees a map it lacks and our host serves no map
  share on that wire; Chupathingy's own Workshop scheme (an id in the name's tail, a sidecar) is not used or read.
  Our joiner on a PC host's game takes the crossplay branch first, which refuses a name that is not a PC map's.
  Workshop maps are therefore for Vita / Vita PC games; on an Everyone game PC players are refused cleanly. (This
  bullet is read from the code - network_server_manager.c names only Custom Edition caches custom_maps\; no OpenCE
  build was run against a Workshop map this lane.)
- A newer build later adding fields would need its own review; this one parses nothing new.

### 8.6 Rate limits and abuse

One question per join (the state machine holds one download; a map change cancels it). No retry loop: a failed or
refused download leaves the game. The Steam side keeps section 3/7's limits (8 items a session, sizes, idle 600 s).
The joiner never opens a connection to anything but Steam's CM/CDN for this.

### 8.7 Tests (Nov 16 2026, headless, RX 7900; the harness binary of this branch, --steam-workshop --steam-test-ca)

- run_workshop_maps_test.sh: 33 checks (names made/read, level names, verdicts, ids, unended record bytes).
- run_net_fuzz_test.sh target `workshop` (net_fuzz_workshop.c: the record's 0x80 name bytes, both identities, any
  id/stem): 11 seeds, 60 s libFuzzer, 11.7 M runs, 0 findings (with crossplay_maps 7.1 M and map_share 5.3 M, 0).
- run_netns_online_test.sh (netns_workshop_cases.sh), all PASS: `workshop` (both have it: 91 s played together, the
  map loaded from each machine's mods folder, no map share query); `workshopget` (asked, answered yes; the joiner's
  Steam job against steam_fake_server.py in its own namespace: QR, the one item, 15 MB, revoked and logged off; the
  item installed through the game's map check, its bytes the map's, its identity the host's, precached; 81 s played;
  no token in a log or file; nothing asked of or sent by the host; the pixels from the joiner's own MCC, the item
  having no bitmaps.map); `workshopno` (told, no Steam session, nothing installed); `workshopdiff` (refused "isn't the
  same version", no question, its own copy untouched); `workshopold` (release-1.1.2 and the 1.1.3 candidate without
  the Workshop: each told "the host does not share that map", the host's map share refused it, nothing received,
  each up to the end of the run).
- Offline: a local game with 3 bots on ws2900000002_coldsna-8866 (MCC Blood Gulch as a Workshop item): loaded from
  mods/2900000002, bots' nav built, 85 s of play, kills.

### 8.8 Findings of this review

1. Real Workshop multiplayer items keep their bitmaps.map/sounds.map in `halo1/maps/`, not beside the maps: section
   7's loader refused them ("a resource map that was not supplied"). Fixed for every mod map (8.1). Campaign items
   with the same layout (e.g. manifest 2683077323621946101) benefit too. Unverified against a real item download.
2. A host can name any item id in a question (8.3): accepted, by design (the player decides, every check applies).
3. The 32-bit identity is not a security boundary (8.2); it is the same as custom maps'.
4. No new parser on the wire; the one new parser (the name) is bounded, fuzzed, and reads only the existing field.
