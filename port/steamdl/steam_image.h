/*
STEAM_IMAGE.H

The Steam Workshop browser's pictures (docs/steam_download_security.md section 9): an item's preview picture, as
Steam names it in the item's details (preview_url, and a details page's extra pictures), fetched from Steam's own
image CDN and nothing else, made a card's picture.

- The URL is a rule, not a lookup (steam_image_url): https only, the host one of Steam's image CDN hosts
  (STEAM_IMAGE_HOSTS, exact), no port, no user, a path of /ugc/ and plain characters; anything else is refused before
  any connection. The CDN is asked for a picture of the size we draw (its own resize: imw/imh), TLS with the built-in
  authorities and the host's name checked as every Steam connection (steam_net.h), no cookie, no token, no
  redirect followed, the body at most STEAM_IMAGE_BYTES_MAXIMUM.
- The bytes are a JPEG or a PNG by their signature (the server's word for its type is not taken) and decoded by a
  bounded decoder (stb_image, port/third_party/stb: JPEG and PNG only, its size read and capped first -
  STEAM_IMAGE_SIDE_MAXIMUM, STEAM_IMAGE_PIXELS_MAXIMUM - every allocation within STEAM_IMAGE_DECODE_BYTES_MAXIMUM),
  then shrunk to the card's picture (B, G, R, A as the menus' textures).
- Only the made picture is kept: a small cache of them in the player's data folder (<root>/workshop/previews,
  files named by a hash of the URL - never by anything the server says - at most STEAM_IMAGE_CACHE_BYTES and
  STEAM_IMAGE_CACHE_FILES, the oldest let go), read back only if its header and size are exactly a picture's.

Plain C with no session (the decoder, the URL rule and the cache): the fuzzers and the tests use it alone. The
fetch is the job thread's (steam_job.c).
*/

#ifndef STEAM_IMAGE_H
#define STEAM_IMAGE_H

#include "steam_http.h"

#include <stddef.h>
#include <stdint.h>

/* the hosts a preview's URL may name: Steam's image CDN (images.steamusercontent.com since 2024, the older Akamai
name of the same pictures) */
#define STEAM_IMAGE_HOSTS { "images.steamusercontent.com", "steamuserimages-a.akamaihd.net" }
/* a URL's bytes */
#define STEAM_IMAGE_URL_MAXIMUM 320
/* a fetched picture's bytes (Steam takes previews of at most 1 MB; the CDN's resized ones are tens of KB) */
#define STEAM_IMAGE_BYTES_MAXIMUM (2u << 20)
/* a picture's sides and pixels (a 1920x1080 preview, should the CDN not resize it) */
#define STEAM_IMAGE_SIDE_MAXIMUM 4096
#define STEAM_IMAGE_PIXELS_MAXIMUM (1920u * 1200u)
/* what the decoder may hold at once (the pixels at 3 bytes each and its own planes) */
#define STEAM_IMAGE_DECODE_BYTES_MAXIMUM (16u << 20)

/* the pictures made: a card's and the details page's (powers of two: the menus' textures) */
#define STEAM_IMAGE_CARD_WIDTH 256
#define STEAM_IMAGE_CARD_HEIGHT 128
#define STEAM_IMAGE_LARGE_WIDTH 512
#define STEAM_IMAGE_LARGE_HEIGHT 256

/* the cache: its folder under the data root, its caps */
#define STEAM_IMAGE_CACHE_FOLDER "workshop/previews"
#define STEAM_IMAGE_CACHE_BYTES (12u << 20)
#define STEAM_IMAGE_CACHE_FILES 96

enum steam_image_fit
{
	/* the picture fills the whole size, its middle kept (a card's) */
	STEAM_IMAGE_FILL,
	/* the whole picture inside the size, the rest clear (the details page's) */
	STEAM_IMAGE_FIT,
};

/* a preview's URL as Steam gave it: 1 with the host to connect to and the path to ask (the CDN's resize to width x
height added), or 0 with why (not https, a host not Steam's image CDN's, a port, a user, an odd path) */
int steam_image_url(const char *url, int width, int height, char *host, size_t host_size, char *path,
	size_t path_size, char *why, size_t why_size);

/* a picture's bytes (a JPEG or a PNG, by their signature) made width x height B,G,R,A (width * height * 4 bytes):
1, or 0 with why (another type, too large, malformed, more memory than the cap) */
int steam_image_decode(const uint8_t *data, size_t size, int width, int height, int fit, uint8_t *bgra, char *why,
	size_t why_size);
/* the decoder's most held at once since the last call (for the memory measurement), and reset */
size_t steam_image_decode_peak(void);

/* ---------- the cache (<root>/STEAM_IMAGE_CACHE_FOLDER) */

/* the cache file of a URL's picture at a size: 1 */
int steam_image_cache_path(const char *root, const char *url, int width, int height, char *path, size_t size);
/* a cached picture of exactly that size: 1 */
int steam_image_cache_load(const char *path, int width, int height, uint8_t *bgra);
/* a picture kept (the folder made; written whole then renamed), then the cache trimmed to its caps: 1 */
int steam_image_cache_save(const char *root, const char *path, int width, int height, const uint8_t *bgra);
/* the oldest cache files let go until at most maximum_bytes and maximum_files remain; only the cache's own names
(16 hex digits, ".px") are looked at or removed: how many were removed */
int steam_image_cache_trim(const char *root, uint64_t maximum_bytes, int maximum_files);

/* ---------- the fetch (the job's thread) */

struct steam_image_fetcher
{
	struct steam_http http;
	char host[256];
	int open;
	/* the tests' image server instead of the URL's host (http://127.0.0.1:port; "" in the game: a test build's
	HALO_STEAM_TEST_IMAGES alone): the URL is still checked first */
	char test_base[128];
	/* fetched: pictures, bytes */
	unsigned int fetched;
	uint64_t fetched_bytes;
	/* the last steam_image_get's failure was the picture's own (its URL, its bytes refused), not the network's */
	int refused;
};

/* a preview's picture made width x height (the cache's if there, else fetched, decoded and kept): 1, or 0 with why */
int steam_image_get(struct steam_image_fetcher *fetcher, const char *root, const char *url, int width, int height,
	int fit, uint8_t *bgra, char *why, size_t why_size);
void steam_image_fetcher_close(struct steam_image_fetcher *fetcher);

#endif
