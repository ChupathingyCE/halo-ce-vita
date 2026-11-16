# Steam download: the LIVE test on the PC (the owner's hand alone)

Status: prepared Nov 15 2026 on feat/11x-steam-download; **not run**. Everything before this was proven against a
local fake Steam (port/vita/tests/run_steam_download_test.sh). The first contact with Steam's own servers and a
real account is the owner's decision and the owner's doing: no agent signs in to any account, and no agent runs
the command below.

## What the test does

`steam_download_test live <folder> [games]` runs the same job the Vita's GAME MANAGER runs
(port/steamdl/steam_job.c), on the PC, against Steam's own CM (from api.steampowered.com's directory, over TLS with
the game's built-in roots) and Steam's CDN:

1. ClientHello, then Authentication.BeginAuthSessionViaQR: a QR is drawn **in the terminal**.
2. The owner scans it with the Steam app on his phone (Steam Guard > scan a QR code) and approves
   **"Halo CE Vita (PS Vita)"**. The PC never sees a password or a Steam Guard code.
3. ClientLogon with the refresh token (should_remember_password false), PICS for app 976730, the depot key of
   976733 (and 976734), the manifest request codes, the manifests from the CDN, the planned files chunk by
   chunk (decrypted, decompressed, SHA-1 + Adler-32 checked, written at their offsets), each file SHA-1 and
   header checked, renamed into `<folder>/haloce-vita/mcc/halo1/...`.
4. Authentication.RevokeToken (permanent) on the refresh token, ClientLogOff, the socket closed, every secret
   wiped from RAM. Nothing of the account is on disk; the log holds no secret.

games 1 = Halo CE's maps, bitmaps.map, ui.map, the three custom_edition resource maps, sounds_adpcm.fsb and the
five lists (about 470 MB to download, 1.5 GB on disk). games 3 adds the four CELT sound banks (about 2 GB to
download, 3.1 GB on disk).

## What the owner does (and nothing else needs doing)

1. Build the binary (an agent may do this step; it runs nothing against Steam):

       sh port/vita/tests/steam_live_pc.sh

   It prints the binary's path (default ~/halo-test-out/steam11x-runs/live/steam_download_test) and refuses to
   finish if the binary carries any test hook.

2. In a terminal (tall window, dark theme reads the QR best), with the Steam phone app at hand:

       ~/halo-test-out/steam11x-runs/live/steam_download_test live ~/halo-test-out/steam11x-runs/live/run1 1

3. When the QR appears: Steam app > Steam Guard > **scan a QR code** > approve. The app names the device
   "Halo CE Vita (PS Vita)". Approve only this one; if the terminal did not just show it, approve nothing.
   Steam rotates the code every few minutes: a new one is drawn if the old one expired.

4. Watch the terminal: "== preparing", then "== downloading" with MB and MB/s lines, then "== done" (or FAILED
   with the reason). **Enter** cancels cleanly at any time (revoke + log off; the next run resumes). Ctrl-C does
   not sign out: then remove the device by hand (step 5).

5. Afterwards, in the Steam app: Steam Guard > **Authorized devices**. "Halo CE Vita (PS Vita)" must NOT be
   listed. This is the one fact no fake can give us: whether RevokeToken removes the device. If it is listed,
   remove it there, and tell the lane: the fallback is then RevokeRefreshToken (needs the signature) or the
   manual removal the game already tells the player about.

6. Send the lane `<folder>/live-log.txt` (it holds no secret: sizes, results, host names) and the terminal's
   last lines. Do not send a core dump or a memory dump of the process.

## What the test answers

- Whether Steam accepts our ClientLogon as it is (no machine_id, no client_os_type): "signed in (cell N)" or
  "logon refused (result N)".
- Whether MCC's appinfo comes inline or over HTTP ("no app info ... offered over HTTP" means the HTTP path must
  be added before anything else).
- Whether the depot key, request code and CDN path work for a real depot, and the chunk containers MCC uses
  (VZa, VSZa or zip: the log's decoder lines say).
- Whether RevokeToken removes the device (step 5).
- The real download rate on this connection (the Vita's Wi-Fi will be slower).

## What it does not do

It does not touch a Vita, does not write under the game's own folders, keeps nothing after the revoke but the
downloaded Halo CE files (the owner's own, under the folder he chose) and the log.

## The Workshop (feat/11x-steam-workshop, prepared Nov 16 2026; **not run**)

Built by the same script (`sh port/vita/tests/steam_live_pc.sh`). The owner, and only the owner, runs:

    ~/halo-test-out/steam11x-runs/live/steam_download_test live-workshop ~/halo-test-out/steam11x-runs/live/ws1

1. The QR, as above: approve "Halo CE Vita (PS Vita)" in the Steam app.
2. The first page of Halo CE's Workshop items is printed (ten rows: number, item id, MB, title, campaign or
   multiplayer, or why it is not offered). Type `n` / `p` for the next / previous page, `o` for the next order
   (trending, newest, most liked, last updated), `s TEXT` to search, `m N` to mark row N (campaign items; up to 8),
   `d` to download the marked ones, `q` to sign out without downloading.
3. A small campaign item first (Ruby's Rebalanced Halo: CE, 2915693904, is about 1 GB; a smaller one is better for
   the first run). Watch for "== downloading", then "== done".
4. The items land in `ws1/data/workshop/<id>/` (the game's staging folder). This test does not install them: the
   game does, with its own map check, the next time its STEAM WORKSHOP screen opens. To try one in the PC build,
   copy `ws1/data/workshop/<id>.done` and the folder `<id>` into the PC build's data root's `workshop/`, open GAME
   MANAGER > DOWNLOAD FROM STEAM > STEAM WORKSHOP (a --steam-workshop build): it is checked and moved to mods/.
5. Steam app > Steam Guard > Authorized devices: the device must be gone (as above).
6. Send the lane `ws1/live-log.txt` (no secret) and the terminal's last lines.

What it answers: whether QueryFiles with the required tag Halo1 lists Halo CE's items for a normal account; whether
an MCC owner gets depot 976730's key and the request code for an item's hcontent_file (Chupathingy's client relies
on both); the real items' names through our name rule (the log says "N files of other kinds left out"); the rate.

## Workshop multiplayer maps (feat/11x-steam-workshop-mp, prepared Nov 16 2026; **not run**)

Two machines of a --steam-workshop build (two PC builds on one LAN, or the PC build and a Vita with the test build):
the HOST and the JOINER. Only the owner signs in to Steam. Docs: docs/steam_download_security.md section 8.

1. On the HOST: GAME MANAGER > DOWNLOAD FROM STEAM > STEAM WORKSHOP (or, in the Classic menus, the settings panel's
   Multiplayer > Modded maps > Steam Workshop). Sign in with the QR. Pick a small Halo CE **multiplayer** item (the
   row says "multiplayer"; for example one of the items this PC already has in Steam's depotcache: 4520562000989635516's
   item or 4896837317637153445's - find their item ids in the Steam client's Workshop page). Download it; the screen
   says "Installed in the mods folder: 1".
2. On the HOST: MULTIPLAYER (any style): the map list shows the item's maps after the custom maps (named after their
   files; the description says "Steam Workshop item <id>"). Play one offline with bots for a minute: textures and
   sounds right? (If its bitmaps.map is in the item's halo1/maps, that one is used: halo.log says "takes its bitmaps
   from".) Then host a System Link / Online game on it.
3. On the JOINER, which does NOT have the item: join the HOST's game. Expect the question "This map is from the Steam
   Workshop - download it from Steam?" naming the map and the item id. Answer YES: the STEAM WORKSHOP screen opens over
   the lobby with a QR; sign in (the owner's own account again is fine); the item downloads, is checked and installed;
   the joiner precaches the map and plays the HOST's game. Steam app > Authorized devices: the device is gone again.
4. On the JOINER, delete the item (MOD MANAGER, or mods/<id>), join again and answer NO: told, back at the menus, no
   Steam sign-in.
5. Mismatch (optional): on the JOINER, replace mods/<id>/maps/<file>.map with another map of the same name; join:
   refused "isn't the same version as yours"; no question.
6. An older build (1.1.2) joining the HOST's Workshop game: refused cleanly (its usual "custom map" message, the host
   "doesn't share" it); nothing downloaded from the host.
7. Send the lane both machines' halo.log (no secret: the logs never hold a token; check by searching for "eyJ").

What it answers: real multiplayer items' layout (their resource maps in halo1/maps) and their maps loading with the
right pixels; the online join with the download from Steam in the middle of a lobby (does the QR on the lobby screen
read well; does the host's 180 s start hold suffice for a real download); the identity matching across two real
downloads of one item.
