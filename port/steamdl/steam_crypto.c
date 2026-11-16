/*
STEAM_CRYPTO.C

steam_crypto.h, over Mbed TLS.
*/

#include "steam_crypto.h"

#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha1.h"

#include <string.h>

typedef char steam_sha1_context_fits[sizeof(mbedtls_sha1_context) <= sizeof(struct steam_sha1_context) ? 1 : -1];

void steam_sha1(const void *data, size_t size, uint8_t digest[20])
{
	mbedtls_sha1(data, size, digest);
}

void steam_sha1_start(struct steam_sha1_context *context)
{
	mbedtls_sha1_context *sha = (mbedtls_sha1_context *)context->opaque;

	mbedtls_sha1_init(sha);
	mbedtls_sha1_starts(sha);
}

void steam_sha1_update(struct steam_sha1_context *context, const void *data, size_t size)
{
	mbedtls_sha1_update((mbedtls_sha1_context *)context->opaque, data, size);
}

void steam_sha1_finish(struct steam_sha1_context *context, uint8_t digest[20])
{
	mbedtls_sha1_context *sha = (mbedtls_sha1_context *)context->opaque;

	mbedtls_sha1_finish(sha, digest);
	mbedtls_sha1_free(sha);
}

long steam_symmetric_decrypt(const uint8_t key[32], const uint8_t *input, size_t input_size, uint8_t *output)
{
	mbedtls_aes_context aes;
	uint8_t iv[16];
	size_t size, index;
	unsigned int padding;
	long result = -1;

	if (input_size < 32 || input_size % 16 || input_size > 0x7fffffff)
		return -1;
	mbedtls_aes_init(&aes);
	if (mbedtls_aes_setkey_dec(&aes, key, 256) != 0 || mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, input, iv) != 0)
		goto done;
	size = input_size - 16;
	if (mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, size, iv, input + 16, output) != 0)
		goto done;
	/* PKCS#7: 1 to 16 bytes, each the count */
	padding = output[size - 1];
	if (padding == 0 || padding > 16)
		goto done;
	{
		unsigned int bad = 0;

		for (index = 0; index < padding; index++)
			bad |= output[size - 1 - index] ^ padding;
		if (bad)
			goto done;
	}
	result = (long)(size - padding);
done:
	mbedtls_aes_free(&aes);
	steam_wipe(iv, sizeof(iv));
	return result;
}

long steam_base64_decode(const char *text, size_t length, uint8_t *output, size_t capacity)
{
	size_t written = 0;

	if (mbedtls_base64_decode(output, capacity, &written, (const unsigned char *)text, length) != 0)
		return -1;
	return (long)written;
}

long steam_base64_encode(const uint8_t *data, size_t size, char *output, size_t capacity)
{
	size_t written = 0;

	if (mbedtls_base64_encode((unsigned char *)output, capacity, &written, data, size) != 0)
		return -1;
	return (long)written;
}

uint32_t steam_crc32(uint32_t crc, const void *data, size_t size)
{
	static uint32_t table[256];
	static int ready;
	const uint8_t *bytes = data;

	if (!ready)
	{
		uint32_t index;

		for (index = 0; index < 256; index++)
		{
			uint32_t value = index;
			int bit;

			for (bit = 0; bit < 8; bit++)
				value = value & 1 ? 0xedb88320u ^ (value >> 1) : value >> 1;
			table[index] = value;
		}
		ready = 1;
	}
	crc = ~crc;
	while (size--)
		crc = table[(crc ^ *bytes++) & 0xff] ^ (crc >> 8);
	return ~crc;
}

uint32_t steam_adler32_zero(const void *data, size_t size)
{
	const uint8_t *bytes = data;
	uint32_t a = 0, b = 0;

	while (size)
	{
		/* (5552: the most bytes before the sums can overflow 32 bits) */
		size_t run = size < 5552 ? size : 5552;

		size -= run;
		while (run--)
		{
			a += *bytes++;
			b += a;
		}
		a %= 65521;
		b %= 65521;
	}
	return b << 16 | a;
}

void steam_hex(const uint8_t *data, size_t size, char *output)
{
	static const char digits[] = "0123456789abcdef";
	size_t index;

	for (index = 0; index < size; index++)
	{
		output[index * 2] = digits[data[index] >> 4];
		output[index * 2 + 1] = digits[data[index] & 15];
	}
	output[size * 2] = 0;
}

static int hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

int steam_unhex(const char *text, size_t length, uint8_t *output, size_t size)
{
	size_t index;

	if (length != size * 2)
		return 0;
	for (index = 0; index < size; index++)
	{
		int high = hex_digit(text[index * 2]), low = hex_digit(text[index * 2 + 1]);

		if (high < 0 || low < 0)
			return 0;
		output[index] = (uint8_t)(high << 4 | low);
	}
	return 1;
}

void steam_wipe(void *data, size_t size)
{
	if (data && size)
		mbedtls_platform_zeroize(data, size);
}

int steam_equal(const void *a, const void *b, size_t size)
{
	const uint8_t *x = a, *y = b;
	uint8_t difference = 0;
	size_t index;

	for (index = 0; index < size; index++)
		difference |= x[index] ^ y[index];
	return difference == 0;
}
