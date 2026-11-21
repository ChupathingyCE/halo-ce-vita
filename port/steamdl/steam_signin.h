/*
STEAM_SIGNIN.H

The Steam sign-in kept on the device (owner, Nov 20 2026: a QR scan about once every two weeks, not at every download):
the QR sign-in's refresh token, the account's name and when it was saved - nothing else, never a password - in one
file in the app's own folder (steam-signin.bin), sealed with AES-256-GCM under a key derived (HKDF-SHA256, a random
salt per save) from a secret of this console's own that is not on the memory card: the Vita's OpenPSID
(sceKernelGetOpenPsId; the platform's steam_job_platform.device_secret). The header (magic, version, when saved, salt,
nonce, length) is the cipher's associated data: a change anywhere fails the tag.

  - kept 14 days at most (STEAM_SIGNIN_SECONDS), checked at each load against the clock; a date in the future (the
    clock set back, a file from elsewhere) counts as expired. An expired one is revoked with Steam at the next
    connection (steam_download.c) and wiped
  - any failure to read it - the tag, the format, the sizes, another console's file - wipes it: the player scans a
    new QR, nothing else happens
  - SIGN OUT (the Steam panel) wipes it first, then revokes the token with Steam
  - its contents are never logged (the log says "kept", "expired", "unreadable", a size at most), never in
    settings.txt, never in a crash report's text; in RAM only while a session uses it (struct steam_secrets, wiped
    at the session's end)

What the console binding protects, and what it does not: docs/steam_download_security.md section 10.
*/

#ifndef STEAM_SIGNIN_H
#define STEAM_SIGNIN_H

#include "steam_limits.h"

#include <stddef.h>
#include <stdint.h>

/* 14 days */
#define STEAM_SIGNIN_SECONDS (14 * 24 * 60 * 60)
#define STEAM_SIGNIN_FILE "steam-signin.bin"
/* the file's whole size at most (a larger one is not read: wiped) */
#define STEAM_SIGNIN_FILE_MAXIMUM 4096u
/* the header: magic, version, flags, 2 reserved, saved-at (int64), salt 16, nonce 12, the sealed part's length (u16),
2 reserved */
#define STEAM_SIGNIN_HEADER_SIZE 48u
#define STEAM_SIGNIN_TAG_SIZE 16u
/* the device secret's size at least and at most */
#define STEAM_SIGNIN_SECRET_MINIMUM 16u
#define STEAM_SIGNIN_SECRET_MAXIMUM 64u

struct steam_signin
{
	char refresh_token[STEAM_TOKEN_MAXIMUM + 1];
	char account_name[STEAM_ACCOUNT_MAXIMUM + 1];
	/* seconds since 1970 */
	int64_t saved_at;
};

enum steam_signin_state
{
	STEAM_SIGNIN_NONE = 0,
	STEAM_SIGNIN_KEPT = 1,
	/* older than 14 days or dated in the future: to revoke and wipe */
	STEAM_SIGNIN_EXPIRED = 2,
	/* not readable (wiped by steam_signin_load) */
	STEAM_SIGNIN_UNREADABLE = 3,
};

/* the sealed file's bytes for a sign-in (salt and nonce given: steam_signin_save draws them): its size, or -1 */
long steam_signin_seal(const uint8_t *secret, size_t secret_size, const struct steam_signin *signin,
	const uint8_t salt[16], const uint8_t nonce[12], uint8_t *output, size_t capacity);
/* a sealed file's bytes opened (hostile input: the fuzzers'): STEAM_SIGNIN_KEPT or _EXPIRED with signin filled, or
STEAM_SIGNIN_UNREADABLE with signin wiped */
int steam_signin_open(const uint8_t *data, size_t size, const uint8_t *secret, size_t secret_size, int64_t now,
	struct steam_signin *signin);
/* the header alone, no secret (the panel's row: kept, how old): a state, *saved_at when KEPT or EXPIRED. Nothing is
decrypted and nothing wiped: a header that reads but a body that does not is found by steam_signin_load */
int steam_signin_peek(const char *path, int64_t now, int64_t *saved_at);

/* saved (a new salt and nonce; written beside, then renamed over): 1, or 0 (nothing left behind) */
int steam_signin_save(const char *path, const uint8_t *secret, size_t secret_size, const struct steam_signin *signin);
/* read: STEAM_SIGNIN_NONE (no file), _KEPT (signin filled), _EXPIRED (signin filled - to revoke - and the file wiped),
_UNREADABLE (the file wiped) */
int steam_signin_load(const char *path, const uint8_t *secret, size_t secret_size, int64_t now,
	struct steam_signin *signin);
/* the file overwritten with zeros, then removed (and its ".new" if one was left) */
void steam_signin_wipe(const char *path);

#endif
