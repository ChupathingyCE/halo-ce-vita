/*
VITA_MBEDTLS_CONFIG.H

The Vita's Mbed TLS: the consoles' (console_mbedtls_config.h: the site's
and GitHub's HTTPS) and, for the Workshop's Steam content client
(configure.py --vita-mcc: port/linux/src/steam_content), AES in CBC mode
(Steam depots' chunks and file names are AES-CBC), SHA-1 (the chunks'
checks) and Base64, and certificate files (its SSL_CERT_FILE, never set on
a Vita).
*/

#ifndef VITA_MBEDTLS_CONFIG_H
#define VITA_MBEDTLS_CONFIG_H

#include "console_mbedtls_config.h"

#ifdef VITA_STEAM_CONTENT
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_FS_IO
#define MBEDTLS_SHA1_C
#define MBEDTLS_BASE64_C
#endif

#endif
