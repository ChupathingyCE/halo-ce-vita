/*
STEAM_CRYPTO.H

The Steam download's few primitives: SHA-1 (a chunk's id, a file's
sha_content), Steam's symmetric decryption (AES-256: the first block,
ECB-decrypted, is the IV of the rest, CBC with PKCS#7 padding; chunks and
encrypted file names), base64 (encrypted names), CRC-32 (the containers'
checks), Adler-32 from 0 (a chunk's crc in the manifest), and the wipe of
secrets. AES, SHA-1 and base64 are Mbed TLS's (port/third_party/mbedtls).
*/

#ifndef STEAM_CRYPTO_H
#define STEAM_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

void steam_sha1(const void *data, size_t size, uint8_t digest[20]);

/* SHA-1 piece by piece (a file read back in parts) */
struct steam_sha1_context
{
	/* (Mbed TLS's context, held opaquely: 64-bit aligned, large enough) */
	uint64_t opaque[16];
};
void steam_sha1_start(struct steam_sha1_context *context);
void steam_sha1_update(struct steam_sha1_context *context, const void *data, size_t size);
void steam_sha1_finish(struct steam_sha1_context *context, uint8_t digest[20]);

/* Steam's symmetric decryption of input (at least 32 bytes, whole blocks)
into output (input_size - 16 bytes of room): the plaintext's size, or -1
(bad sizes, bad padding) */
long steam_symmetric_decrypt(const uint8_t key[32], const uint8_t *input, size_t input_size, uint8_t *output);

/* base64 (RFC 4648, padded) into output: the bytes, or -1 */
long steam_base64_decode(const char *text, size_t length, uint8_t *output, size_t capacity);
/* output gets 4 * ceil(size / 3) + 1 bytes: the length, or -1 */
long steam_base64_encode(const uint8_t *data, size_t size, char *output, size_t capacity);

uint32_t steam_crc32(uint32_t crc, const void *data, size_t size);
/* Adler-32 starting from 0, not 1 (Steam's chunk checksum) */
uint32_t steam_adler32_zero(const void *data, size_t size);

/* 40 lowercase hex digits and a NUL */
void steam_hex(const uint8_t *data, size_t size, char *output);
/* hex digits into bytes: 1, or 0 if not exactly size bytes of them */
int steam_unhex(const char *text, size_t length, uint8_t *output, size_t size);

/* clears memory the compiler may not leave out (Mbed TLS's) */
void steam_wipe(void *data, size_t size);
/* equal without an early exit */
int steam_equal(const void *a, const void *b, size_t size);

/* random bytes from the system's generator (steam_platform.c) */
void steam_random(void *buffer, size_t size);

#endif
