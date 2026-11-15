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
