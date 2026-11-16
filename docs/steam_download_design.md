# Halo CE + Halo 2 set up on the Vita itself (no PC)

> Copied Nov 8 2026 from feat/vita-steam-download (1a0b99182) for phase 1 of the Steam download
> (feat/vita-steam-download-2). Changed since: the Vita no longer builds Halo 2 levels (levels are made
> in memory at load from MCC's maps), so the download is files only, and the owner wants the campaigns
> too: depots 976733, 976734, 976735, 976736 (about 2.8 GB to download, measured from the local
> manifests). The rules are in docs/steam_download_security.md; the code is port/steamdl/.

Status: design + feasibility, Nov 3 2026. Branch feat/vita-steam-download (off 1.2.0 21b55916e).
No Steam login was made, no credentials used, no game content downloaded. Numbers marked
*measured* were measured this round; *estimate* says its basis.

## 0. The short answer

Two methods, both run by the Vita app, no PC:

1. **MCC from Steam**: the Vita logs in by Steam QR, downloads only the needed MCC files from the
   player's own depots, logs out and revokes the token, then prepares the levels.
2. **Xbox disc images**: the player puts their own XISO (and the Halo CE installer) into an import
   folder (FileZilla to the game's own upload-only FTP, or VitaShell), the Vita reads the images and
   does the rest.

Both are feasible. Most of the hard parts already exist in the tree:

- The Vita game already **builds Halo 2 multiplayer levels itself** (1.2.0, commit 530e5f06e: "A
  multiplayer level is still made on the Vita"; build heap h2_build_heap.c). Campaign levels are refused on the
  Vita today (Outskirts: 55.4 MB of 56 MB heap, 7+ minutes on the dev Vita, never finished).
- **The Xbox bloodgulch.map is not a blocker**: since 68c982a60 the kit is made from MCC's own
  bloodgulch.map (h2_level.c ~14121). The only cost is that the MCC kit is 3.6 MB bigger, so **Headlong and
  Desolation are left out** without the Xbox map (installer H2_MAPS_NEED_XBOX_KIT). So Steam-only players get
  Halo 2 multiplayer (21 of 23 maps).
- **TLS** (Mbed TLS 3.6.7 on the Vita, TLS 1.2/1.3, ISRG Root X1/X2 already built in), AES, SHA-1, HMAC,
  zlib, a C XDVDFS reader (port/linux/src/xiso.c, desktop only for now), and the **Custom Edition
  installer import on the Vita** (vita_ce_installer.c + libmspack, already in 1.1.2) are all in the tree.

What it costs: method 2 (XISO) is small, about 2-3k lines, and ships first (1.1.3). Method 1 (Steam)
is new protocol work, about 6-8k lines of our own C plus a vendored LZMA and zstd decoder, plus fuzzing.

Things that could make it not viable:
- **Steam** can change or block third-party clients at any time. This is not a ToS-sanctioned client
  (section 4).
- **Halo 2 campaign on the Vita** has no proven path: the heap is at its limit and the build is slow.
  The plan is multiplayer first; campaign stays "needs the PC" until a dedicated build mode is
  measured.
- **Runtime Bink movies**: whether the Vita's CPU keeps up is still unmeasured, with a planned fallback
  (section 9).

## 1. What files are needed (from the installer code), and where they are in Steam

Sources: tools/installer/halo_ce_vita_installer.py (mcc_plan ~981, h2_plan ~1361). The depot ids, file
lists and sizes come from **parsing the owner's own Steam depotcache manifests offline**
(~/.local/share/Steam/depotcache/<depot>_<gid>.manifest, the same binary format the CDN serves). That
also proves the manifest parser (a ~100-line hand-written protobuf reader). Throwaway script:
scratchpad steamdl/manifest_probe.py. The DLC app ids are from appmanifest_976730.acf. **Confidence:
high** for depot to file mapping and sizes at build 19905945. The DLC names were not checked on SteamDB.

| Depot | DLC app | Holds | Used for |
|---|---|---|---|
| 976731 | (base) | MCC launcher, Data, Engine, EAC | nothing |
| 976733 | 1064221 | halo1 campaign maps, bitmaps.map, ui.map, custom_edition/*, sound/pc | Halo CE |
| 976734 | 1080080 | halo1 MP maps (bloodgulch...), copies of bitmaps.map, custom_edition, sound | Halo CE MP, **H2 kit's bloodgulch.map** |
| 976735 | 1064270 | halo2 campaign maps, single_player_shared, shared, mainmenu, *.dat, h2_fonts | Halo 2 campaign |
| 976736 | 1097223 | halo2 MP maps, shared, mainmenu, *.dat, h2_fonts | Halo 2 MP |
| 976737 | 1097223 | groundhog (H2A MP) | nothing |
| 976738/9, 1064222-6 | ... | halo3, odst, halo4, reach | Halo 3 later (1.3.0) |

Files the game reads (sizes on disk; "download" = the sum of the chunks' compressed sizes from the
manifest):

| Group | Disk | Download | Chunks |
|---|---|---|---|
| CE: 10 campaign maps + bitmaps.map | 959 MB | 270 MB | 957 |
| CE: 19 MP maps | 296 MB | 76 MB | 306 |
| CE: ui.map | 3 MB | 0.3 MB | 3 |
| CE: custom_edition bitmaps/sounds/loc.map (online) | 166 MB | 57 MB | 161 |
| CE: sounds_adpcm.fsb + 5 lst | 88 MB | 63 MB | 90 |
| **Halo CE total** | **1.51 GB** | **0.47 GB** | |
| H2: 23 MP maps (District left out) | 102 MB | 92 MB | 108 |
| H2: shared + mainmenu | 36 MB | 30 MB | 35 |
| H2: sounds_neutral.dat + sounds_en.dat | 1136 MB | 922 MB | 1133 |
| H2: textures.dat (the builder only) | 565 MB | 479 MB | 573 |
| H2: font (font_table.txt + conduit-13) | 0.5 MB | 0.1 MB | 2 |
| **Halo 2 MP total** | **1.84 GB** | **1.52 GB** | |
| H2 campaign: 15 maps + single_player_shared (later) | 939 MB | 846 MB | 902 |

Card space for Halo 2 MP: MCC files kept (1.27 GB) + 23 levels at about 64 MB each (1.47 GB; zanzibar measured
60 MB) + kit 34 MB + the 800 MB of cache slots the game makes = **about 3.6 GB kept, about 4.2 GB at the
peak** (textures.dat + a 31 MB spill file while building). Halo CE: 1.5 GB. Both fit any 8 GB+ card;
the PCH-2000's 1 GB built-in storage takes neither.

Do we keep textures.dat after the build? Only the builder reads it. Recommendation: delete it after a
successful build, and re-download it on a rebuild (Repair from Steam) (owner question 3).

## 2. The Steam side

Everything below goes over the **CM servers' WebSocket** (wss://<cm>/cmsocket/, binary frames, each one
Steam message: EMsg | 0x80000000 for protobuf, header length, CMsgProtoBufHeader, body). Then plain
HTTPS to the CDN. Sources: SteamKit2 (SteamKit2/Steam/Authentication/*, Steam/CDN/*), DepotDownloader
(ContentDownloader.cs), SteamDatabase/Protobufs (steammessages_auth.steamclient.proto,
steammessages_contentsystem.steamclient.proto, steammessages_clientserver*.proto).

Checked this round without any login:
- `api.steampowered.com/ISteamDirectory/GetCMListForConnect/v1/?cellid=0&cmtype=websockets` answers
  with 36 WebSocket CMs (cmp1-ord1.steamserver.net:443 ...).
- `IContentServerDirectoryService/GetServersForSteamPipe/v1/?cell_id=0` answers with 21 CDN caches,
  `https_support: mandatory`.
- The CM, CDN and API chains end at Let's Encrypt **ISRG Root X1/X2**, both already in
  port/linux/src/update_roots.h.

The flow:

1. **QR login.** Authentication.BeginAuthSessionViaQR (a non-authed service call, EMsg
   ServiceMethodCallFromClientNonAuthed with job name "Authentication.BeginAuthSessionViaQR#1"), sent
   with device_friendly_name "Project Ark (PS Vita)", platform_type SteamClient (1) and device_details.
   It returns client_id, request_id, challenge_url (s.team/q/...) and a poll interval.
   - The Vita draws challenge_url as a QR (qrcodegen, MIT, about 1k lines).
   - It calls PollAuthSessionStatus(client_id, request_id) every `interval` seconds. new_challenge_url
     replaces the QR when Steam rotates it.
   - When the player approves in the Steam phone app, the poll returns refresh_token (a JWT) and
     access_token. The Vita never sees a password or a Steam Guard code.
2. **Logon.** CMsgClientLogon with the refresh token in access_token, should_remember_password=false.
   The ClientLogOnResponse gives the steamid and cell_id.
3. **What the player owns.** PICS: ClientPICSAccessTokenRequest, then ClientPICSProductInfoRequest
   for 976730. The appinfo KeyValues give the depots, their public-branch manifest gid and dlcappid.
   Ownership is enforced by Steam at step 4. We only ask for depots in our game list.
4. **Depot key.** CMsgClientGetDepotDecryptionKey(depot_id, app_id) returns a 32-byte AES key, refused
   if the account does not own the depot.
5. **Manifest.** ContentServerDirectory.GetManifestRequestCode(app 976730, depot, gid, branch
   "public") returns a 64-bit code that is short-lived (minutes).
   - GET https://<cdn>/depot/<depot>/manifest/<gid>/5/<code> returns a zip with the binary manifest
     (magics 0x71F617D0 payload, 0x1F4812BE metadata, 0x1B81B817 signature, 0x32C415AB end).
   - The CDN copy has filenames_encrypted=1: each name is base64 of AES-256 (ECB-encrypted IV, then
     CBC) under the depot key.
   - The local depotcache copies are already decrypted (enc=0), which is how we listed them.
6. **Chunks, filtered.** Only FileMappings whose name is in our filter. For each chunk
   (sha, offset, cb_original, cb_compressed): GET https://<cdn>/depot/<depot>/chunk/<sha hex>.
   - Decrypt (AES-256: first 16 bytes ECB give the IV, rest CBC with PKCS7).
   - Decompress by magic: "VZa" LZMA, "VSZa" zstd, "PK\3\4" deflate.
   - Check the length, then Adler-32 with seed 0 (DepotChunk.cs), and **also SHA-1 = the chunk id**.
   - Write at its offset. The whole file is checked against sha_content at the end.
   - We cannot see which compression MCC's chunks use without downloading one, so both LZMA and zstd
     are needed.
7. **Logout + revoke** (section 10): Authentication.RevokeToken(token=refresh_token,
   revoke_action=k_EAuthTokenRevokePermanent (1)), then ClientLogOff, then close the socket and wipe.

Message count: about 12 protobuf messages, each with a handful of fields. We write the field numbers
by hand (e.g. BeginAuthSessionViaQR_Request: 1 device_friendly_name, 2 platform_type, 3 device_details;
PollAuthSessionStatus_Response: 3 refresh_token, 4 access_token, 6 account_name).

## 3. Licences and the Steam agreement

- **SteamKit2**: LGPL-2.1 (the README says "LGPL-2.1"; whether it is "or later" is not stated). We
  could link it but it is C#. We use it as a protocol reference only.
- **DepotDownloader**: GPL-2.0. The GitHub page does not say "only" or "or later". If it is GPL-2.0-only
  it is **incompatible with our GPL-3.0-only**: use it for ideas only, copy no code.
- **SteamDatabase/Protobufs**: the repo says Unlicense, but the .proto files are dumps of Valve's
  client. We **do not vendor any .proto**. Field numbers and message shapes are interface facts, which
  we hand-write in C.
- **Recommendation: a clean-room C implementation**, written from the protocol as described above.
  No code copied from SteamKit2 or DepotDownloader. Vendored: the LZMA SDK's LzmaDec (public domain), zstd's
  decompressor (BSD-3, compatible), qrcodegen (MIT).
- **Steam Subscriber Agreement / ban risk, plainly**:
  - Valve does not authorise third-party download clients. DepotDownloader-style tools have been in
    public use for about 10 years, and we know of no account bans for downloading one's own depots.
  - The real risks: Valve can rate-limit or break the protocol (QR login, request codes, WebSocket CM)
    with no notice. Logins from an unusual client could trip their anti-abuse checks.
  - We only download what the account owns. Nothing goes through any server of ours, and nothing is
    redistributed.
  - The game must say before login: "unofficial; Valve may stop it working".

## 4. The Vita side (what to build for method 1)

| Piece | Have | Need | Size (our C) |
|---|---|---|---|
| Sockets, TLS 1.2/1.3, roots, deadlines | posix_https.c, posix_tls_* (Mbed TLS 3.6.7), ISRG roots | reuse | 0 |
| WebSocket client (RFC 6455, client masking, binary frames, ping) | - | new, over posix_tls | ~400 |
| Steam framing + job ids + service calls | - | new | ~500 |
| Protobuf | - | **hand-written minimal** varint/len writer + bounded reader (nanopb needs .proto + generator) | ~500 |
| Auth (QR, poll), logon, PICS, KeyValues text parser, depot key, request code, logoff, revoke | - | new | ~1,200 |
| AES-256 ECB/CBC, SHA-1, Adler-32, inflate | Mbed TLS, zlib | reuse | 0 |
| LZMA decode (VZa: header + LZMA1 props) | - | vendor LzmaDec.c (PD, ~1.2k) | ~150 glue |
| zstd decode (VSZa) | - | vendor decompress-only zstd (BSD, ~15k vendored) | ~100 glue |
| Manifest + zip + filename decrypt | probe parser (Python) | C port | ~500 |
| Chunk download, verify, write at offset, resume journal | posix_https_download (Range, caps) | new loop | ~700 |
| QR drawing | - | vendor qrcodegen (MIT) | ~100 glue |
| Screens (QR, progress, errors), sleep/Wi-Fi handling | updater's screens | new | ~800 |
| PC harness build + fuzz targets (configure.py --linux-fuzz) | yes | new targets | ~400 |

**About 5.5k lines of our own + ~18k vendored. Effort: 4-6 agent lanes over 3-5 weeks.** It is testable
on the PC harness for everything except the real login, which needs the owner's own account with
consent.

Device notes:
- **Memory**: one chunk is 1 MB, so about 3 MB in flight (compressed + decrypted + output) plus the
  LZMA dictionary of 1 MB or less. Everything streams to ux0 .part files and nothing is held whole.
  This matches posix_ce_installer.c's 2.7 MB.
- **Speed (estimate)**: Wi-Fi gives 1-3 MB/s. LZMA decode on a Cortex-A9 at 444 MHz should give
  about 5-8 MB/s of output (desktop LZMA is about 50-100 MB/s, scaled by 10-20x), and it runs on a
  second thread.
  - Halo CE: 0.47 GB in about 4-8 min.
  - Halo 2 MP: 1.5 GB in about 10-25 min.
  - Show the measured rate. Do not promise times.
- **Sleep/Wi-Fi**: hold the screen on and block auto-suspend while downloading
  (scePowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND), the PS button lock as the updater does).
  - Sockets die on suspend. A resume re-opens the CDN with the same keys while the session lives. If
    the CM session is gone, a **new QR** is needed (no token on the card).
  - The journal keeps finished chunks, so nothing is downloaded twice.
- **Battery**: refuse to start under 30% unless charging. Pause below 10%.

## 5. Halo CE on the Vita without a PC

What Setup does today:
- **Automatic (MCC)**: copies the files above with header checks (_mcc_file_ok: "daeh" + version 13;
  bitmaps.map 01 00 00 00; FSB5; lst = 1). There are no conversions. MCC has no Halo CE movies
  (MCC/Content/Movies are MCC's own .bk2 menu backgrounds), so MCC players have no intro movie today
  either: the game skips a missing movie.
- **Manual (Xbox)**: copies maps/*.map and default.xbe out of the XISO. It also converts bink/*.bik with
  ffmpeg to H.264 Baseline 3.1, 640 wide, AAC, MP4 (FFMPEG_MOVIE_ARGS). The Vita plays these with
  SceAvPlayer (vita_movie.c, bink_vita.c).

On the Vita:
- **Copies + checks**: run as they are, in C (the same header checks).
- **Halo PC files** (online): from MCC's custom_edition (method 1), or from the CE installer through the
  existing vita_ce_installer.c (method 2).
- **Movies**: see section 9.

## 6. Halo 2 levels built on the Vita

**Measured on the gxm-null harness** (configure.py --linux-d3d gxm-null --release, Setup's exact builder
environment: HALO_H2_BUILD_HEAP_MB=400, HALO_H2_WINDOW_LIMIT_KB=120000, kit from MCC's bloodgulch.map,
Ryzen 9 7950X; script ~/halo-test-out/steamdl-run/build-level.sh):

| Level | Build (log "made in") | Build heap peak | Memory window peak | Read / written by the build |
|---|---|---|---|---|
| zanzibar (+ kit) | 2.1 s | 34.9 MB | 30.2 MB | ~370 MB / ~150 MB |
| coagulation (+ kit) | 2.3 s | 30.6 MB | 29.7 MB | - |

Read/written is the uncached run's rchar/wchar minus the cached run's (507 vs 138 MB read, 209 vs 60 MB
written), and includes the 31 MB spill file. Whole process RSS was 140 MB (the game and menus,
not the build).

**Vita estimate**:
- **CPU**: 40-60x the harness, about 85-140 s. Basis: a 7950X core at 5.7 GHz against a Cortex-A9 at
  444 MHz is 13x on clock times a 3-4x IPC and memory gap. This is consistent with the one Vita data
  point: Outskirts did not finish in 7 minutes, against a Setup estimate of ~22 s on the PC, so the
  factor is above 19x.
- **I/O**: about 370 MB read at about 20 MB/s plus 150 MB written at about 10 MB/s, so about 35 s.
- **Total: about 2-3 min per multiplayer map, about 45-70 min for all 23.** This is not measured on hardware
  (the dev Vita is down). It is the first thing to measure.

**Memory: it fits for multiplayer.** The build heap is 30-35 MB. The Vita gives free user RAM minus
8 MB (h2_build_heap.c: 56 MB measured with the menus up). Campaign is at the edge (55.4 of 56 MB) and
three missions are over the 40 MB tag cache anyway.

Blockers and what to change:
- On the Vita today a level is built on the fly when loading it (inside the load screen, with present
  gaps). Add a **"Preparing Halo 2 levels" screen**: one level after another, with progress
  "map n of 23", cancel, and resume. It needs no new builder code. Run it from the Game Manager or
  right after a download/import, ideally with the menus' textures unloaded so more RAM is free for the
  heap. That free RAM is the lever that may let campaign fit later (open question).
- The file layout is already the Vita's (halo2/maps, halo2/levels, h2_fonts; H2_MAPS_FOLDER). The
  download writes straight there. The .made stamp and h2-manifest.txt (Game Manager) mark what was
  built and from what.
- The **kit**: MCC's bloodgulch.map (depot 976734) works for 21 maps. With the player's Xbox Halo CE
  XISO imported, the Xbox bloodgulch.map is used and **Headlong + Desolation come back**. So method 2
  (CE) improves method 1 (H2).

**Halo 2 from an Xbox Halo 2 disc image** (owner's order: MCC > Vista > Xbox, so this is a file source,
not a different game):
- h2_cache.c reads retail Xbox caches (version 8) beside MCC's (version 13). bitmap_pixels takes
  textures from "its textures.dat entry (MCC) or the Xbox map".
- **Not wired: sound.** h2_sound.c gives sounds only from MCC's sounds_neutral.dat and language dats
  through the gestalt. Xbox Halo 2 keeps its sound in the maps/shared.map.
- HUD and menu makers were not checked against Xbox sources. There is no Xbox Halo 2 image here to
  test with.
- The disc has the 15 missions, shared, single_player_shared and mainmenu, and the 12 launch MP maps
  (Ascension, Beaver Creek, Burial Mounds, Coagulation, Colossus, Foundation, Headlong, Ivory Tower,
  Lockout, Midship, Waterworks, Zanzibar). The map packs were downloads. *This list is from public
  sources and should be checked against a disc listing.*
- **Recommendation: later, not in the first plan.** Effort is about 1-2 lanes: Xbox sound read path,
  an end-to-end harness run per map, then a fit test. It needs an Xbox Halo 2 image in the private lab.

## 7. Method 2: Xbox disc images on the card

**Flow** (the owner's FileZilla video):
1. The player copies `halo.iso` (any name) and `halocesetup*.exe` (any name: recognised by content)
   into **`ux0:data/haloce-vita/import/`** (1.2.0: `ux0:data/project-ark/import/`). They can use the
   game's own FTP (below), VitaShell FTP or USB, or any app.
2. They start the game. It finds files in import/ and shows "Import". There is no other step.
3. The import:
   - **Recognise by content**: an XISO has "MICROSOFT*XBOX*MEDIA" at 0x10000 (plus the whole-disc
     offsets, as xiso.c). A CE installer is a PE with an MSCF cabinet (posix_ce_installer.c).
   - **Free-space check first** (below).
   - **Extract** maps/*.map, default.xbe and bink/*.bik with xiso.c's checks: name_is_plain (no "/",
     "\\", ":", "..", controls), a bounded tree walk (depth 64, 4096 entries), size caps, maps.partial,
     then rename. Then the installer's header checks (xbox_map_ok "daeh" v5 "toof"; required
     XBOX_REQUIRED_MAPS).
   - **Resumable**: files at their full size and checked are skipped. A .partial file restarts.
   - Then the CE installer gives the three Halo PC resource maps (existing code). Halo 2 adds its own
     preparation step later.
4. **Delete prompt** (asked, never automatic, default **Keep**). Only the images and installers in
   import/ that were used are deleted. The prompt says: "Delete the disc image and installer (X.X GB)?
   You will need them again to re-import or repair from disc."

**Peak space per game** (the image + what is extracted, both at once):
- **Halo CE**: Xbox image 3.5-3.7 GB *measured* (the three USA revisions here) + maps 1.86 GB (24 files) +
  default.xbe 2.5 MB + bink 0.65 GB (if movies are played from the .bik) + CE installer 0.17 GB + the resource
  maps it gives (0.17 GB). That is **about 6.5 GB at the peak, about 2.7 GB kept** once the image and
  installer are deleted.
- **Halo 2** (if done later): image about 4-6 GB plus the levels made.
- Check free space before starting. If short, say "Free N.N GB on the memory card, or delete
  something, then press X".

**The game's own FTP (import screen only)**:
- **Recommend writing our own upload-only server, about 800 lines.** The alternative, vitasdk's
  libftpvita (MIT, the base of VitaShell's FTP), serves the whole file system and has no
  passwords. Cutting it down is more work and more risk than a small server that only knows the import
  folder. We use libftpvita as a behaviour reference.
- Commands: USER, PASS, SYST, FEAT, OPTS UTF8, PWD, CWD / only, TYPE A/I, PASV, EPSV, LIST and NLST
  (import/ only), SIZE, MDTM, REST + STOR (resume of big ISOs), APPE, ABOR, NOOP, QUIT. Everything else
  gets 502.
- Rules: passive mode only, one client at a time, private/LAN peers only, a one-time password. The full
  list is in section 10.
- **VitaShell FTP/USB still triggers the import**: the game looks in import/ at start and on the
  import screen, whoever put the files there.

## 8. Movies (Halo CE, both 1.1.3 and 1.2.0)

**Measured** with the private XISO here (read-only header probe):
- bink/intro, attract1-3 and credits are all **Bink 1 revision "i" (BIKi), 640x480, 29.97 fps**, one
  audio track each.
- 16,290 frames, about 9 min, 647 MB.

Options:
- **A. Play the .bik at runtime with FFmpeg's Bink decoders** (bink.c, binkdsp.c, binkaudio.c and
  their bitstream/DCT helpers).
  - FFmpeg is LGPL-2.1-or-later, which may be used in our GPL-3.0-only game (LGPL-2.1 section 3 lets
    us apply the GPL). The tree already does this with port/third_party/ffmpeg-wma.
  - About 4-5k vendored lines + about 500 of glue into bink_vita.c's existing frame ring and blit. Frames
    go to the existing YUV-to-RGB blit, sound to the BGM port as today.
  - CPU, *measured* on the PC: 0.82 ms/frame single-threaded on the 7950X (attract3, 2042 frames
    in 1.66 s). Audio decode is negligible.
  - CPU, *estimate*: 33-49 ms per frame on one A9 core, above the 33 ms budget at 30 fps. Revision
    "i" stores each plane's size, so **Y on one core, U+V on another** gives about 22-33 ms. That is
    borderline but plausible with 3 cores free while a movie plays. If behind, drop frames and keep
    the audio clock.
- **B. Convert once on the Vita to the MP4 the player already plays.**
  - vitasdk has **no H.264 encoder** (the firmware has one but no headers or stubs). Software x264 (GPL-2+) at
    444 MHz would be about 10 fps: 30-60 min of conversion plus an AAC encoder and an MP4 muxer.
    That is far more code.
  - B also needs A's decoder.
- **C. Fallback: convert once to MJPEG** with the Vita's hardware JPEG encoder (sceJpegEnc, in vitasdk),
  and play it with the hardware MJPEG decoder (sceJpegDecodeMJpeg). Decoding is not real-time-bound,
  so it always works. Cost: about 650 MB on the card and about 600 lines on top of A.

**Recommendation: A**, with a hardware gate first. Step 0 is a test build that decodes intro.bik on the
dev Vita and logs ms/frame with 1 and 2 threads. If 2 threads hold 30 fps, ship A. If not, ship C (same
decoder, one-time conversion). An existing PC-made MP4 is still preferred when present (no change for
current players).

## 9. Game list as data, Repair, adding games

- The download side takes a **game list table** (C table or a signed text file in app0). Each entry
  has: game id, Steam app 976730, depots (976733/976734 for CE; 976736 + 976734 for H2 MP; 976735 for
  H2 campaign), filename filters (exact paths, as mcc_plan/h2_plan list them), the destination
  folder, the header check per kind, and an optional "prepare" step id (H2: build levels).
- Halo 3 (1.3.0) is a new entry with depots 976738/976739 (halo3) and its own prepare step, not a new flow.
- **Repair from Steam**:
  1. A fresh QR session.
  2. Fetch the current manifests.
  3. Hash each card file chunk by chunk (SHA-1 per chunk range: the manifest's chunk shas, and
     sha_content for the file).
  4. Download only the chunks that differ.
  5. Log out and revoke.
  The Game Manager's health check (game_manager.c: header + size, H2 SHA-256 manifest) decides when to
  offer it. A changed manifest gid (an MCC update) shows as "update available".
- **Re-import from disc**: the same extractor over the image. It needs the image on the card again,
  which the delete prompt says.

## 10. Security review

**Steam session lifecycle** (the owner's requirement):
- The refresh and access tokens live **only in RAM**, in one struct, from PollAuthSessionStatus to the
  end of the session. They are never written to ux0 or ur0, **never logged** (the logger gets
  "token: <n> bytes" at most), and never put in a URL. Keys and request codes are in RAM too.
- **When the download finishes, is cancelled, or fails**:
  1. Authentication.RevokeToken(token=<our refresh token>, revoke_action=k_EAuthTokenRevokePermanent).
  2. ClientLogOff.
  3. Close the WebSocket.
  4. mbedtls_platform_zeroize the token, access token, depot keys and request codes, then free.
  Nothing is kept, so the next session needs a new QR.
- **Can a client revoke its own token?** The proto has RevokeToken(token, revoke_action) and
  RevokeRefreshToken(token_id, steamid, revoke_action, signature). RevokeToken taking the token
  itself is the natural call for "this device logs out". RevokeRefreshToken needs a signature we
  cannot make, and is what Steam's own UI uses for other devices. **Unverified**: this needs one
  consented test with the owner's account (start, approve, revoke, then check Steam's "Authorized
  devices"). If RevokeToken is refused, the fallback is the manual removal below.
- **Power or Wi-Fi lost mid-download**: the token dies with the process (RAM only). Server-side it
  stays valid until it expires (refresh tokens last months), unused because nobody has it. The session
  appears in the player's **Steam app > Steam Guard / Authorized devices** (and the web "Manage Steam
  Guard" page) as "Project Ark (PS Vita)", where they can remove it.
  - The game writes a **no-secret marker** at session start (ux0:.../steam-session.txt: the start
    time and the device name, no token) and deletes it after a successful revoke.
  - At the next start, if the marker is there: "A Steam sign-in from <date> was not signed out.
    Remove 'Project Ark (PS Vita)' in the Steam app under Authorized devices." A later session could
    also call EnumerateTokens to find it (revoking it needs the signature, so it may stay manual).
- **MITM**: TLS 1.2/1.3 only, chains checked against built-in roots (ISRG X1/X2) and the host name.
  Nothing is sent before the check. WebSocket over the same TLS.
- **Malicious CM/CDN/manifest data** (parser bugs on the Vita): every parser is bounded.
  - Protobuf reader: lengths checked against the buffer, depth limit, max message 1 MB.
  - Manifest: at most 64 MB zipped, 100k files, 1M chunks.
  - Chunk: cb_original at most 16 MB, the output length must equal cb_original.
  - KeyValues: depth 32, size cap.
  - The LZMA and zstd decoders get exact output sizes.
  - Every parser runs under the PC fuzzer (--linux-fuzz) before it ships. A chunk that fails
    SHA-1/Adler is discarded and retried, never written.
- **Path traversal**: depot file names are only **compared** against our filter's exact paths. We
  never build a path from a depot name, so a hostile name can only fail to match. The same rule holds
  for the ISO (xiso.c name_is_plain) and the CE cabinet (the paths written are our own).
- **Disk fill**: check free space against the planned bytes plus 5% before starting. Every file is
  capped at the manifest/plan size. Writes stop at the size. .part files are kept for resume and
  removed on cancel-and-discard.
- **Phishing/QR**: the QR is made on the device. The approval screen on the phone shows our device
  name. The game says "approve only if you just started this on your Vita".

**The import-screen FTP server** (1.1.3):
- It runs **only while the import screen is open**. It closes when the import starts or the screen is
  left. It never runs in play or online.
- **Upload-only into import/**: LIST and NLST only of import/. No RETR, no DELE/RMD/MKD/RNFR/SITE/CHMOD.
  Every name passes name_is_plain, and CWD only goes to "/" (= import/).
- **One-time random password** (6-8 characters from the Vita's RNG, user "halo"), shown on the screen
  with IP:port.
- **Private and link-local peers only** (10/8, 172.16/12, 192.168/16, 169.254/16). Others are dropped
  at accept. The PASV data connection must come from the control peer's address.
- **Passive mode only**, one client at a time, idle timeout 5 minutes, 3 wrong passwords then 30 s
  lockout.
- **Accepted files**: an STOR is only allowed for a name ending .iso/.xiso/.exe (the content is checked
  later anyway) and is capped at 9 GB for an image, 512 MB for an installer. The total is capped at the
  free space.
- FTP is plaintext: the password is visible on the LAN. That is acceptable for a one-time password
  that only allows uploads into a folder whose contents are checked before use. It is never exposed
  beyond the LAN.

## 11. 1.1.3: Halo CE from an Xbox disc image on the Vita

Target: release-1.1.2 (app HCEV00001, ux0:data/haloce-vita).

Smallest shippable scope:

1. **Import (host side, before the game runs).** Today, no ui.map gives vita_main.c's
   show_missing_data text screen. That becomes the **import screen**: framebuffer text, and no menus
   because there is no ui.map. It shows:
   - what is in import/, the free space, "Press X to import";
   - the FTP line "FileZilla: ftp://192.168.x.x:1337 user halo password k7m2qp";
   - then progress and the delete prompt.
2. **XDVDFS in C**: port/linux/src/xiso.c already exists, with the same checks as the installer's
   XisoImage and extract-xiso's BSD licence. Build it on the Vita host. Extend it to default.xbe and
   bink/*.bik. Change it from "maps folder only" to a plan of files with size caps. About 300 changed
   lines.
3. **CE installer**: vita_ce_installer.c already imports halocesetup*.exe from the data or maps folder.
   Look in import/ too and **recognise it by content** (PE + MSCF). About 80 lines.
4. **Game Manager backport** (the in-game home for Repair / Re-import once the game runs).
   - 1.2.0 has game_manager.c (653 lines) + .h (66), about 140 lines in menu_functions.c, and the
     main.xml entry.
   - Carries as-is: the threaded header + size check of the Halo CE maps and the details/wording flow.
   - Removed: Halo 2, Halo 3 and the H2 manifest (about 40%).
   - Changed: the 1.1.2 menus differ (no collection menu), so the Mod Manager entry is renamed GAME
     MANAGER with Games + Mods tabs. Mods is today's custom_edition_mods UI unchanged. Games lists
     maps, default.xbe, movies and the Halo PC files with Good / Needs attention, plus "Import from
     disc image" and "Repair".
   - Repair restarts into the import screen, since the game needs to be out of the maps to rewrite
     them.
   - About 900 lines in all.
5. **Upload-only FTP server**: about 800 lines (section 7).
6. **Movies**: Option A (runtime Bink), gated on the step-0 Vita measurement, else C (section 8). If
   neither is ready in time, 1.1.3 still ships the import, and movies play only from PC-made MP4s as
   today.

**Size: about 2.3k lines of our own** (+ about 4.5k vendored Bink decoder).

**Risk for a hotfix line: medium.**
- Low risk: the import screen runs before the game (a new start-up path, but isolated) and reuses
  proven code (xiso.c, posix_ce_installer.c, game_manager.c).
- Highest risk: the movie decoder's CPU (gated) and the FTP server (new network code: fuzz and
  ASan it on the PC).
- Everything carries into 1.2.0's Game Manager. The import code is shared, and only the folder
  changes (project-ark/import/).

## 12. Step plans

**Method 2 (XISO), build first.** It needs no Steam, helps every disc player now, and its pieces
(import screen, prepare-levels screen, Game Manager hooks) are reused by method 1.

| Step | Useful alone because | Size |
|---|---|---|
| 2.0 Vita measurement build: Bink intro decode ms/frame (1/2 threads), one H2 MP level built on device (time, heap) | decides movies A vs C; replaces the 40-60x estimate | ~200 |
| 2.1 Import screen + xiso.c on the Vita + CE installer by content + free space + delete prompt (1.1.3) | disc players install with no PC | ~700 |
| 2.2 Upload-only FTP on the import screen (1.1.3) | no VitaShell step | ~800 |
| 2.3 Movies A or C (1.1.3) | intro and credits | ~500 + vendored |
| 2.4 Game Manager backport, Repair/Re-import (1.1.3) | health check + repair | ~900 |
| 2.5 1.2.0: same code, project-ark/import, "Preparing Halo 2 levels" screen (MP, resumable) | phone-FTP players who copied MCC files by hand get Halo 2 with no PC builder | ~500 |
| 2.6 Later: Halo 2 from an Xbox Halo 2 image (sound path) | disc-only H2 players | 1-2 lanes |

**Method 1 (Steam), after 2.5.**

| Step | Useful alone because | Size |
|---|---|---|
| 1.1 Manifest + chunk pipeline in C (parse, decrypt, decompress, verify, write, resume) on the PC harness, fed with depotcache files + synthetic chunks; fuzz targets | the riskiest parser code proven without any login | ~1.5k + vendored |
| 1.2 WebSocket + framing + protobuf + QR login + logon + logoff + revoke, on the PC harness; one consented owner-account test of revoke | answers the revoke question | ~2.5k |
| 1.3 PICS, depot keys, request codes, CDN download of the game list (CE first), on the PC harness | a PC test of the full flow | ~800 |
| 1.4 Vita screens (QR, progress, sleep/battery), then Halo CE download | Halo CE with no PC and no disc | ~800 |
| 1.5 Halo 2 MP download + the 2.5 prepare screen; Repair from Steam | Halo 2 with no PC | ~300 |
| 1.6 Halo 3 entry (1.3.0) | data + its prepare step | - |

## 13. Open questions for the owner

1. Is it OK to say plainly in-game, before login, that Steam download is unofficial and Valve may stop
   it working?
2. One consented test with your own Steam account for the revoke call (step 1.2)? Without it, logout
   is "best effort + remove it in the Steam app".
3. Delete textures.dat (565 MB) after the Halo 2 build, and re-download or re-import it for a rebuild?
4. Halo 2 campaign: keep it "PC Setup only" for now, or fund a measured on-device attempt (dedicated
   build mode with the menus unloaded; 3 missions are out anyway at 40 MB)?
5. Import folder name: `import` (ux0:data/haloce-vita/import/) as proposed for the video?
6. Movies: is a 650 MB MJPEG copy acceptable if runtime Bink is too slow (fallback C)?
7. Halo 2 from an Xbox Halo 2 disc: later (as proposed) or needed for 1.2.0?
8. FTP port: a fixed 1337, or any free port shown on screen?
