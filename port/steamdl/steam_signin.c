/*
STEAM_SIGNIN.C

steam_signin.h: the kept Steam sign-in, sealed to this console. AES-256-GCM and HKDF-SHA256 are Mbed TLS's
(port/third_party/mbedtls).
*/

#include "steam_signin.h"

#include "steam_crypto.h"

#include "mbedtls/gcm.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static const uint8_t magic[4] = { 'X', 'V', 'S', 'I' };
#define VERSION 1
/* (the key's purpose in the derivation: a key for anything else from the same secret differs) */
static const char info[] = "Halo CE Vita steam_signin v1: refresh token at rest";

static void put_le64(uint8_t *p, int64_t value)
{
	uint64_t bits = (uint64_t)value;
	int index;

	for (index = 0; index < 8; index++)
		p[index] = (uint8_t)(bits >> (8 * index));
}

static int64_t le64(const uint8_t *p)
{
	uint64_t bits = 0;
	int index;

	for (index = 7; index >= 0; index--)
		bits = bits << 8 | p[index];
	return (int64_t)bits;
}

static int derive(const uint8_t *secret, size_t secret_size, const uint8_t salt[16], uint8_t key[32])
{
	const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

	if (!secret || secret_size < STEAM_SIGNIN_SECRET_MINIMUM || secret_size > STEAM_SIGNIN_SECRET_MAXIMUM || !sha256)
		return 0;
	return mbedtls_hkdf(sha256, salt, 16, secret, secret_size, (const unsigned char *)info, sizeof(info) - 1, key, 32) == 0;
}

/* a name Steam gives: 1-64 of letters, digits and _ (Steam's account names) */
static int account_ok(const char *name, size_t length)
{
	size_t index;

	if (!length || length > STEAM_ACCOUNT_MAXIMUM)
		return 0;
	for (index = 0; index < length; index++)
	{
		char c = name[index];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
			c == '.' || c == '@'))
			return 0;
	}
	return 1;
}

/* a token: printable ASCII, no space (a JWT: base64url and dots) */
static int token_ok(const char *token, size_t length)
{
	size_t index;

	if (length < 16 || length > STEAM_TOKEN_MAXIMUM)
		return 0;
	for (index = 0; index < length; index++)
		if ((unsigned char)token[index] <= ' ' || (unsigned char)token[index] >= 0x7f)
			return 0;
	return 1;
}

static void header_write(uint8_t *header, int64_t saved_at, const uint8_t salt[16], const uint8_t nonce[12],
	size_t sealed)
{
	memset(header, 0, STEAM_SIGNIN_HEADER_SIZE);
	memcpy(header, magic, 4);
	header[4] = VERSION;
	put_le64(header + 8, saved_at);
	memcpy(header + 16, salt, 16);
	memcpy(header + 32, nonce, 12);
	header[44] = (uint8_t)sealed;
	header[45] = (uint8_t)(sealed >> 8);
}

/* a header's fields: 1 if it is ours (magic, version, zero flags and reserved bytes, a length and a date) */
static int header_fields(const uint8_t *header, int64_t *saved_at, size_t *sealed)
{
	if (memcmp(header, magic, 4) || header[4] != VERSION || header[5] || header[6] || header[7] || header[46] ||
		header[47])
	{
		return 0;
	}
	*saved_at = le64(header + 8);
	*sealed = (size_t)header[44] | (size_t)header[45] << 8;
	return *sealed > 0 && *saved_at > 0;
}

/* the header's fields: 1 if it is ours and its length is the data's */
static int header_read(const uint8_t *data, size_t size, int64_t *saved_at, size_t *sealed)
{
	if (!data || size < STEAM_SIGNIN_HEADER_SIZE + STEAM_SIGNIN_TAG_SIZE || size > STEAM_SIGNIN_FILE_MAXIMUM ||
		!header_fields(data, saved_at, sealed))
	{
		return 0;
	}
	return *sealed == size - STEAM_SIGNIN_HEADER_SIZE - STEAM_SIGNIN_TAG_SIZE;
}

static int expired(int64_t saved_at, int64_t now)
{
	/* (a date more than a day ahead: the clock set back, or a file not made here) */
	return now - saved_at > STEAM_SIGNIN_SECONDS || saved_at - now > 24 * 60 * 60;
}

long steam_signin_seal(const uint8_t *secret, size_t secret_size, const struct steam_signin *signin,
	const uint8_t salt[16], const uint8_t nonce[12], uint8_t *output, size_t capacity)
{
	uint8_t plain[8 + 1 + STEAM_ACCOUNT_MAXIMUM + 2 + STEAM_TOKEN_MAXIMUM], key[32];
	size_t account_length = strnlen(signin->account_name, sizeof(signin->account_name));
	size_t token_length = strnlen(signin->refresh_token, sizeof(signin->refresh_token));
	size_t plain_size, total;
	mbedtls_gcm_context gcm;
	int ok;

	if (!account_ok(signin->account_name, account_length) || !token_ok(signin->refresh_token, token_length) ||
		signin->saved_at <= 0)
	{
		return -1;
	}
	plain_size = 8 + 1 + account_length + 2 + token_length;
	total = STEAM_SIGNIN_HEADER_SIZE + plain_size + STEAM_SIGNIN_TAG_SIZE;
	if (total > capacity || total > STEAM_SIGNIN_FILE_MAXIMUM || !derive(secret, secret_size, salt, key))
		return -1;
	put_le64(plain, signin->saved_at);
	plain[8] = (uint8_t)account_length;
	memcpy(plain + 9, signin->account_name, account_length);
	plain[9 + account_length] = (uint8_t)token_length;
	plain[10 + account_length] = (uint8_t)(token_length >> 8);
	memcpy(plain + 11 + account_length, signin->refresh_token, token_length);
	header_write(output, signin->saved_at, salt, nonce, plain_size);
	mbedtls_gcm_init(&gcm);
	ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
		mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plain_size, nonce, 12, output, STEAM_SIGNIN_HEADER_SIZE,
			plain, output + STEAM_SIGNIN_HEADER_SIZE, STEAM_SIGNIN_TAG_SIZE,
			output + STEAM_SIGNIN_HEADER_SIZE + plain_size) == 0;
	mbedtls_gcm_free(&gcm);
	steam_wipe(plain, sizeof(plain));
	steam_wipe(key, sizeof(key));
	if (!ok)
	{
		steam_wipe(output, total);
		return -1;
	}
	return (long)total;
}

int steam_signin_open(const uint8_t *data, size_t size, const uint8_t *secret, size_t secret_size, int64_t now,
	struct steam_signin *signin)
{
	uint8_t plain[STEAM_SIGNIN_FILE_MAXIMUM], key[32];
	size_t sealed = 0, account_length, token_length;
	int64_t saved_at = 0;
	mbedtls_gcm_context gcm;
	int ok;

	memset(signin, 0, sizeof(*signin));
	if (!header_read(data, size, &saved_at, &sealed) || saved_at <= 0 || !derive(secret, secret_size, data + 16, key))
		return STEAM_SIGNIN_UNREADABLE;
	mbedtls_gcm_init(&gcm);
	ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
		mbedtls_gcm_auth_decrypt(&gcm, sealed, data + 32, 12, data, STEAM_SIGNIN_HEADER_SIZE,
			data + STEAM_SIGNIN_HEADER_SIZE + sealed, STEAM_SIGNIN_TAG_SIZE, data + STEAM_SIGNIN_HEADER_SIZE, plain) == 0;
	mbedtls_gcm_free(&gcm);
	steam_wipe(key, sizeof(key));
	/* (the tag checked: the plaintext is ours, its form still checked as any input) */
	ok = ok && sealed >= 8 + 1 + 1 + 2 + 16 && le64(plain) == saved_at;
	if (ok)
	{
		account_length = plain[8];
		ok = 9 + account_length + 2 <= sealed;
		if (ok)
		{
			token_length = (size_t)plain[9 + account_length] | (size_t)plain[10 + account_length] << 8;
			ok = 11 + account_length + token_length == sealed &&
				account_ok((const char *)plain + 9, account_length) &&
				token_ok((const char *)plain + 11 + account_length, token_length);
			if (ok)
			{
				memcpy(signin->account_name, plain + 9, account_length);
				memcpy(signin->refresh_token, plain + 11 + account_length, token_length);
				signin->saved_at = saved_at;
			}
		}
	}
	steam_wipe(plain, sizeof(plain));
	if (!ok)
	{
		steam_wipe(signin, sizeof(*signin));
		return STEAM_SIGNIN_UNREADABLE;
	}
	return expired(saved_at, now) ? STEAM_SIGNIN_EXPIRED : STEAM_SIGNIN_KEPT;
}

/* the file's bytes (at most the maximum + 1, so a larger one is seen): its size, or -1 if none */
static long read_small(const char *path, uint8_t *buffer, size_t capacity)
{
	FILE *file = fopen(path, "rb");
	size_t length;

	if (!file)
		return -1;
	length = fread(buffer, 1, capacity, file);
	fclose(file);
	return (long)length;
}

int steam_signin_peek(const char *path, int64_t now, int64_t *saved_at)
{
	uint8_t header[STEAM_SIGNIN_HEADER_SIZE];
	FILE *file;
	size_t length, sealed = 0;
	long whole;

	*saved_at = 0;
	file = fopen(path, "rb");
	if (!file)
		return STEAM_SIGNIN_NONE;
	length = fread(header, 1, sizeof(header), file);
	whole = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
	fclose(file);
	if (length != sizeof(header) || whole <= 0 || (unsigned long)whole > STEAM_SIGNIN_FILE_MAXIMUM ||
		!header_fields(header, saved_at, &sealed) ||
		(size_t)whole != STEAM_SIGNIN_HEADER_SIZE + sealed + STEAM_SIGNIN_TAG_SIZE)
	{
		*saved_at = 0;
		return STEAM_SIGNIN_UNREADABLE;
	}
	return expired(*saved_at, now) ? STEAM_SIGNIN_EXPIRED : STEAM_SIGNIN_KEPT;
}

static void wipe_one(const char *path)
{
	FILE *file = fopen(path, "r+b");

	if (file)
	{
		uint8_t zeros[256];
		long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;

		memset(zeros, 0, sizeof(zeros));
		if (size > 0 && size <= 1 << 20 && fseek(file, 0, SEEK_SET) == 0)
		{
			while (size > 0)
			{
				size_t part = size > (long)sizeof(zeros) ? sizeof(zeros) : (size_t)size;

				if (fwrite(zeros, 1, part, file) != part)
					break;
				size -= (long)part;
			}
			fflush(file);
		}
		fclose(file);
	}
	remove(path);
}

void steam_signin_wipe(const char *path)
{
	char temporary[600];

	if (!path || !path[0])
		return;
	wipe_one(path);
	snprintf(temporary, sizeof(temporary), "%s.new", path);
	wipe_one(temporary);
}

int steam_signin_save(const char *path, const uint8_t *secret, size_t secret_size, const struct steam_signin *signin)
{
	uint8_t sealed[STEAM_SIGNIN_FILE_MAXIMUM], salt[16], nonce[12];
	char temporary[600];
	FILE *file;
	long size;
	int ok;

	if (!path || !path[0] || strlen(path) + 5 > sizeof(temporary))
		return 0;
	steam_random(salt, sizeof(salt));
	steam_random(nonce, sizeof(nonce));
	size = steam_signin_seal(secret, secret_size, signin, salt, nonce, sealed, sizeof(sealed));
	if (size <= 0)
		return 0;
	snprintf(temporary, sizeof(temporary), "%s.new", path);
	/* (its folders - the app's own - made if they are not there yet; "ux0:" itself is not one) */
	{
		size_t index;

		for (index = 1; temporary[index]; index++)
		{
			if (temporary[index] != '/' || temporary[index - 1] == ':')
				continue;
			temporary[index] = 0;
			mkdir(temporary, 0777);
			temporary[index] = '/';
		}
	}
	file = fopen(temporary, "wb");
	ok = file && fwrite(sealed, 1, (size_t)size, file) == (size_t)size;
	if (file)
		ok = fflush(file) == 0 && ok, ok = fclose(file) == 0 && ok;
	steam_wipe(sealed, sizeof(sealed));
	/* (the Vita's rename does not replace: the old one wiped first) */
	if (ok)
	{
		wipe_one(path);
		ok = rename(temporary, path) == 0;
	}
	if (!ok)
		steam_signin_wipe(path);
	return ok;
}

int steam_signin_load(const char *path, const uint8_t *secret, size_t secret_size, int64_t now,
	struct steam_signin *signin)
{
	uint8_t data[STEAM_SIGNIN_FILE_MAXIMUM + 1];
	long size;
	int state;

	memset(signin, 0, sizeof(*signin));
	if (!path || !path[0])
		return STEAM_SIGNIN_NONE;
	size = read_small(path, data, sizeof(data));
	if (size < 0)
		return STEAM_SIGNIN_NONE;
	state = steam_signin_open(data, (size_t)size, secret, secret_size, now, signin);
	steam_wipe(data, sizeof(data));
	if (state != STEAM_SIGNIN_KEPT)
		steam_signin_wipe(path);
	return state;
}
