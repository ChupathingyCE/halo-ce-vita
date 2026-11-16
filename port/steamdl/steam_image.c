/*
STEAM_IMAGE.C

steam_image.h: the Workshop browser's pictures - the URL rule, the bounded decode and the shrink, the cache, the
fetch. The decoder is stb_image (port/third_party/stb, public domain / MIT), JPEG and PNG only, every allocation it
makes through budget_malloc below.
*/

#include "steam_image.h"

#include "steam_net.h"

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ---------- the decoder's allocations: a budget held at once (one decode at a time: decode_lock) */

static pthread_mutex_t decode_lock = PTHREAD_MUTEX_INITIALIZER;
static size_t budget_used, budget_peak;
static int budget_refused;

/* (a block's size in front of it, kept 16-byte aligned) */
#define BUDGET_HEADER 16

static void *budget_malloc(size_t size)
{
	unsigned char *block;

	if (size > STEAM_IMAGE_DECODE_BYTES_MAXIMUM || budget_used + size > STEAM_IMAGE_DECODE_BYTES_MAXIMUM)
	{
		budget_refused = 1;
		return NULL;
	}
	block = malloc(size + BUDGET_HEADER);
	if (!block)
		return NULL;
	memcpy(block, &size, sizeof(size));
	budget_used += size;
	if (budget_used > budget_peak)
		budget_peak = budget_used;
	return block + BUDGET_HEADER;
}

static void budget_free(void *pointer)
{
	unsigned char *block;
	size_t size;

	if (!pointer)
		return;
	block = (unsigned char *)pointer - BUDGET_HEADER;
	memcpy(&size, block, sizeof(size));
	budget_used -= size;
	free(block);
}

static void *budget_realloc(void *pointer, size_t old_size, size_t new_size)
{
	void *grown;

	(void)old_size;
	if (!pointer)
		return budget_malloc(new_size);
	grown = budget_malloc(new_size);
	if (grown)
	{
		size_t size;

		memcpy(&size, (unsigned char *)pointer - BUDGET_HEADER, sizeof(size));
		memcpy(grown, pointer, size < new_size ? size : new_size);
		budget_free(pointer);
	}
	return grown;
}

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_FAILURE_STRINGS
#define STBI_MAX_DIMENSIONS STEAM_IMAGE_SIDE_MAXIMUM
#define STBI_ASSERT(x) ((void)0)
#define STBI_MALLOC(size) budget_malloc(size)
#define STBI_REALLOC_SIZED(pointer, old_size, new_size) budget_realloc(pointer, old_size, new_size)
#define STBI_FREE(pointer) budget_free(pointer)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wunused-value"
#endif
#include "../third_party/stb/stb_image.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
/* (stb_image's declarations of what JPEG and PNG alone leave out: never defined, never used) */
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

/* ---------- the URL rule */

static int path_character_ok(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '_' ||
		c == '-' || c == '.';
}

int steam_image_url(const char *url, int width, int height, char *host, size_t host_size, char *path,
	size_t path_size, char *why, size_t why_size)
{
	static const char *const hosts[] = STEAM_IMAGE_HOSTS;
	const char *rest, *slash, *at;
	size_t length, host_length, path_length, index;
	int known = 0;

	host[0] = path[0] = 0;
	if (!url || (length = strnlen(url, STEAM_IMAGE_URL_MAXIMUM + 1)) > STEAM_IMAGE_URL_MAXIMUM || !length)
	{
		snprintf(why, why_size, "no picture, or its address too long");
		return 0;
	}
	if (strncmp(url, "https://", 8))
	{
		snprintf(why, why_size, "a picture's address that is not https");
		return 0;
	}
	rest = url + 8;
	slash = strchr(rest, '/');
	if (!slash)
	{
		snprintf(why, why_size, "a picture's address with no path");
		return 0;
	}
	host_length = (size_t)(slash - rest);
	for (index = 0; index < sizeof(hosts) / sizeof(hosts[0]); index++)
		if (strlen(hosts[index]) == host_length && !memcmp(rest, hosts[index], host_length))
			known = 1;
	/* (a port, a user, another host - also one that merely ends like Steam's - is not Steam's image CDN) */
	if (!known || host_length + 1 > host_size)
	{
		snprintf(why, why_size, "a picture from a host that is not Steam's image CDN");
		return 0;
	}
	/* the path: /ugc/..., plain characters, no "..", no "//"; Steam's own query (if any) left off: ours is added */
	path_length = strcspn(slash, "?#");
	if (path_length < 6 || path_length > 200 || strncmp(slash, "/ugc/", 5))
	{
		snprintf(why, why_size, "a picture's path that is not Steam's");
		return 0;
	}
	for (index = 0; index < path_length; index++)
	{
		char c = slash[index];

		if (!path_character_ok(c) || (c == '.' && index + 1 < path_length && slash[index + 1] == '.') ||
			(c == '/' && index + 1 < path_length && slash[index + 1] == '/'))
		{
			snprintf(why, why_size, "a picture's path with odd characters");
			return 0;
		}
	}
	at = slash + path_length;
	if (*at && *at != '?' && *at != '#')
	{
		snprintf(why, why_size, "a picture's path with odd characters");
		return 0;
	}
	if (width < 16 || width > 1024 || height < 16 || height > 1024 ||
		(size_t)snprintf(path, path_size, "%.*s?imw=%d&imh=%d&ima=fit&impolicy=Letterbox&imcolor=%%23000000&letterbox=false",
			(int)path_length, slash, width, height) >= path_size)
	{
		path[0] = 0;
		snprintf(why, why_size, "a picture's address too long");
		return 0;
	}
	memcpy(host, rest, host_length);
	host[host_length] = 0;
	return 1;
}

/* ---------- the decode and the shrink */

/* rgb (width x height, 3 bytes a pixel), its part from (left, top) of crop_width x crop_height, averaged into the
rectangle (x0, y0, x1, y1) of out (out_width wide, B, G, R, A) */
static void shrink(const uint8_t *rgb, int width, int left, int top, int crop_width, int crop_height, uint8_t *out,
	int out_width, int x0, int y0, int x1, int y1)
{
	int x, y, w = x1 - x0, h = y1 - y0;

	for (y = 0; y < h; y++)
	{
		int sy0 = top + (int)((int64_t)y * crop_height / h), sy1 = top + (int)((int64_t)(y + 1) * crop_height / h);

		if (sy1 <= sy0)
			sy1 = sy0 + 1;
		for (x = 0; x < w; x++)
		{
			int sx0 = left + (int)((int64_t)x * crop_width / w), sx1 = left + (int)((int64_t)(x + 1) * crop_width / w);
			uint32_t sum[3] = { 0, 0, 0 }, count = 0;
			uint8_t *pixel = out + ((size_t)(y0 + y) * (size_t)out_width + (size_t)(x0 + x)) * 4;
			int sx, sy;

			if (sx1 <= sx0)
				sx1 = sx0 + 1;
			for (sy = sy0; sy < sy1; sy++)
			{
				const uint8_t *row = rgb + ((size_t)sy * (size_t)width + (size_t)sx0) * 3;

				for (sx = sx0; sx < sx1; sx++, row += 3)
				{
					sum[0] += row[0];
					sum[1] += row[1];
					sum[2] += row[2];
					count++;
				}
			}
			pixel[0] = (uint8_t)((sum[2] + count / 2) / count);
			pixel[1] = (uint8_t)((sum[1] + count / 2) / count);
			pixel[2] = (uint8_t)((sum[0] + count / 2) / count);
			pixel[3] = 0xff;
		}
	}
}

int steam_image_decode(const uint8_t *data, size_t size, int width, int height, int fit, uint8_t *bgra, char *why,
	size_t why_size)
{
	static const uint8_t png[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	int picture_width = 0, picture_height = 0, components = 0, ok = 0;
	uint8_t *rgb;

	if (width <= 0 || height <= 0 || width > 1024 || height > 1024)
	{
		snprintf(why, why_size, "a picture size we never make");
		return 0;
	}
	memset(bgra, 0, (size_t)width * (size_t)height * 4);
	if (!data || size < 16 || size > STEAM_IMAGE_BYTES_MAXIMUM)
	{
		snprintf(why, why_size, "the picture is empty or too large");
		return 0;
	}
	/* (the type by its signature alone: what the server says it is does not matter) */
	if (!(data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff) && memcmp(data, png, sizeof(png)))
	{
		snprintf(why, why_size, "the picture is not a JPEG or a PNG");
		return 0;
	}
	/* (a picture cut short - a fetch that ended early - is refused whole, not shown half grey: a JPEG ends in its
	EOI marker, a PNG in its IEND chunk) */
	{
		size_t index, tail = size < 32 ? size : 32;
		int ended = 0;

		for (index = size - tail; index + 1 < size && !ended; index++)
			ended = data[0] == 0xff ? data[index] == 0xff && data[index + 1] == 0xd9 :
				index + 4 <= size && !memcmp(data + index, "IEND", 4);
		if (!ended)
		{
			snprintf(why, why_size, "the picture is cut short");
			return 0;
		}
	}
	pthread_mutex_lock(&decode_lock);
	budget_refused = 0;
	if (!stbi_info_from_memory(data, (int)size, &picture_width, &picture_height, &components) ||
		picture_width <= 0 || picture_height <= 0)
	{
		pthread_mutex_unlock(&decode_lock);
		snprintf(why, why_size, "the picture is malformed");
		return 0;
	}
	if (picture_width > STEAM_IMAGE_SIDE_MAXIMUM || picture_height > STEAM_IMAGE_SIDE_MAXIMUM ||
		(uint64_t)picture_width * (uint64_t)picture_height > STEAM_IMAGE_PIXELS_MAXIMUM)
	{
		pthread_mutex_unlock(&decode_lock);
		snprintf(why, why_size, "the picture is too large (%dx%d)", picture_width, picture_height);
		return 0;
	}
	/* (16 bits a channel: twice the memory, never a preview's) */
	if (!memcmp(data, png, sizeof(png)) && stbi_is_16_bit_from_memory(data, (int)size))
	{
		pthread_mutex_unlock(&decode_lock);
		snprintf(why, why_size, "a 16-bit PNG");
		return 0;
	}
	{
		int decoded_width = 0, decoded_height = 0, decoded_components = 0;

		rgb = stbi_load_from_memory(data, (int)size, &decoded_width, &decoded_height, &decoded_components, 3);
		if (rgb && (decoded_width != picture_width || decoded_height != picture_height))
		{
			budget_free(rgb);
			rgb = NULL;
		}
	}
	if (!rgb)
	{
		snprintf(why, why_size, budget_refused ? "the picture needs more memory than allowed" : "the picture is malformed");
		pthread_mutex_unlock(&decode_lock);
		return 0;
	}
	if (fit == STEAM_IMAGE_FILL)
	{
		/* (the middle of it, of the size's shape) */
		int crop_width = picture_width, crop_height = picture_height;

		if ((int64_t)picture_width * height > (int64_t)picture_height * width)
			crop_width = (int)((int64_t)picture_height * width / height);
		else
			crop_height = (int)((int64_t)picture_width * height / width);
		if (crop_width < 1)
			crop_width = 1;
		if (crop_height < 1)
			crop_height = 1;
		shrink(rgb, picture_width, (picture_width - crop_width) / 2, (picture_height - crop_height) / 2, crop_width,
			crop_height, bgra, width, 0, 0, width, height);
	}
	else
	{
		/* (all of it, in the middle; the rest clear) */
		int out_width = width, out_height = height;

		if ((int64_t)picture_width * height > (int64_t)picture_height * width)
			out_height = (int)((int64_t)picture_height * width / picture_width);
		else
			out_width = (int)((int64_t)picture_width * height / picture_height);
		if (out_width < 1)
			out_width = 1;
		if (out_height < 1)
			out_height = 1;
		shrink(rgb, picture_width, 0, 0, picture_width, picture_height, bgra, width, (width - out_width) / 2,
			(height - out_height) / 2, (width - out_width) / 2 + out_width, (height - out_height) / 2 + out_height);
	}
	budget_free(rgb);
	ok = 1;
	pthread_mutex_unlock(&decode_lock);
	return ok;
}

size_t steam_image_decode_peak(void)
{
	size_t peak;

	pthread_mutex_lock(&decode_lock);
	peak = budget_peak;
	budget_peak = 0;
	pthread_mutex_unlock(&decode_lock);
	return peak;
}

/* ---------- the cache */

#define CACHE_MAGIC "XVWP"
#define CACHE_HEADER 12
#define CACHE_LIST_MAXIMUM 512

static uint64_t fnv64(const char *text, uint64_t hash)
{
	for (; *text; text++)
	{
		hash ^= (uint8_t)*text;
		hash *= 1099511628211ull;
	}
	return hash;
}

int steam_image_cache_path(const char *root, const char *url, int width, int height, char *path, size_t size)
{
	char shape[32];
	uint64_t hash = fnv64(url, 14695981039346656037ull);

	snprintf(shape, sizeof(shape), "|%dx%d", width, height);
	hash = fnv64(shape, hash);
	return (size_t)snprintf(path, size, "%s/%s/%016llx.px", root, STEAM_IMAGE_CACHE_FOLDER, (unsigned long long)hash) <
		size;
}

int steam_image_cache_load(const char *path, int width, int height, uint8_t *bgra)
{
	uint8_t header[CACHE_HEADER];
	size_t pixels = (size_t)width * (size_t)height * 4;
	FILE *file = fopen(path, "rb");
	int ok;

	if (!file)
		return 0;
	ok = fread(header, 1, sizeof(header), file) == sizeof(header) && !memcmp(header, CACHE_MAGIC, 4) &&
		header[4] == (uint8_t)width && header[5] == (uint8_t)(width >> 8) && header[6] == (uint8_t)height &&
		header[7] == (uint8_t)(height >> 8) && header[8] == 1 && fread(bgra, 1, pixels, file) == pixels &&
		fgetc(file) == EOF;
	fclose(file);
	if (!ok)
		memset(bgra, 0, pixels);
	return ok;
}

static int make_folder(const char *root)
{
	char folder[600];

	if ((size_t)snprintf(folder, sizeof(folder), "%s/workshop", root) >= sizeof(folder))
		return 0;
	mkdir(folder, 0777);
	snprintf(folder, sizeof(folder), "%s/%s", root, STEAM_IMAGE_CACHE_FOLDER);
	mkdir(folder, 0777);
	return 1;
}

int steam_image_cache_save(const char *root, const char *path, int width, int height, const uint8_t *bgra)
{
	uint8_t header[CACHE_HEADER] = { 'X', 'V', 'W', 'P', 0, 0, 0, 0, 1, 0, 0, 0 };
	size_t pixels = (size_t)width * (size_t)height * 4;
	char temporary[640];
	FILE *file;
	int ok;

	if (!make_folder(root) || (size_t)snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= sizeof(temporary))
		return 0;
	header[4] = (uint8_t)width;
	header[5] = (uint8_t)(width >> 8);
	header[6] = (uint8_t)height;
	header[7] = (uint8_t)(height >> 8);
	file = fopen(temporary, "wb");
	if (!file)
		return 0;
	ok = fwrite(header, 1, sizeof(header), file) == sizeof(header) && fwrite(bgra, 1, pixels, file) == pixels;
	ok = fclose(file) == 0 && ok;
	if (ok)
	{
		remove(path);
		ok = rename(temporary, path) == 0;
	}
	if (!ok)
		remove(temporary);
	steam_image_cache_trim(root, STEAM_IMAGE_CACHE_BYTES, STEAM_IMAGE_CACHE_FILES);
	return ok;
}

/* a name of the cache's own: 16 lower-case hex digits and ".px" (".px.tmp" too: a write cut short) */
static int cache_name(const char *name)
{
	size_t index;

	for (index = 0; index < 16; index++)
		if (!((name[index] >= '0' && name[index] <= '9') || (name[index] >= 'a' && name[index] <= 'f')))
			return 0;
	return !strcmp(name + 16, ".px") || !strcmp(name + 16, ".px.tmp");
}

struct cache_entry
{
	char name[24];
	long long time;
	long long bytes;
};

static int older_first(const void *a, const void *b)
{
	const struct cache_entry *left = a, *right = b;

	return left->time < right->time ? -1 : left->time > right->time ? 1 : strcmp(left->name, right->name);
}

int steam_image_cache_trim(const char *root, uint64_t maximum_bytes, int maximum_files)
{
	char folder[600], path[640];
	struct cache_entry *entries;
	struct dirent *entry;
	DIR *directory;
	uint64_t total = 0;
	int count = 0, removed = 0, index, seen = 0;

	if ((size_t)snprintf(folder, sizeof(folder), "%s/%s", root, STEAM_IMAGE_CACHE_FOLDER) >= sizeof(folder))
		return 0;
	directory = opendir(folder);
	if (!directory)
		return 0;
	entries = calloc(CACHE_LIST_MAXIMUM, sizeof(*entries));
	if (!entries)
	{
		closedir(directory);
		return 0;
	}
	while ((entry = readdir(directory)) != NULL && seen++ < 4 * CACHE_LIST_MAXIMUM)
	{
		struct stat info;

		if (strlen(entry->d_name) >= sizeof(entries[0].name) || !cache_name(entry->d_name) ||
			(size_t)snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name) >= sizeof(path) || stat(path, &info) != 0 ||
			!S_ISREG(info.st_mode))
		{
			continue;
		}
		if (count == CACHE_LIST_MAXIMUM)
		{
			/* (more than a cache this size ever holds: this one goes at once) */
			remove(path);
			removed++;
			continue;
		}
		snprintf(entries[count].name, sizeof(entries[count].name), "%s", entry->d_name);
		entries[count].time = (long long)info.st_mtime;
		entries[count].bytes = (long long)info.st_size;
		total += (uint64_t)info.st_size;
		count++;
	}
	closedir(directory);
	qsort(entries, (size_t)count, sizeof(*entries), older_first);
	for (index = 0; index < count && (total > maximum_bytes || count - index > maximum_files); index++)
	{
		snprintf(path, sizeof(path), "%s/%s", folder, entries[index].name);
		if (remove(path) == 0)
			removed++;
		total -= (uint64_t)entries[index].bytes;
	}
	free(entries);
	return removed;
}

/* ---------- the fetch */

#define FETCH_TIMEOUT_MS 10000

void steam_image_fetcher_close(struct steam_image_fetcher *fetcher)
{
	if (fetcher->open)
		steam_http_close(&fetcher->http);
	fetcher->open = 0;
	fetcher->host[0] = 0;
}

static uint8_t *fetch(struct steam_image_fetcher *fetcher, const char *host, const char *path, size_t *size, char *why,
	size_t why_size)
{
	char error[160];
	int attempt;

	for (attempt = 0; attempt < 2; attempt++)
	{
		uint8_t *body;
		int status = 0, reused = fetcher->open;

		if (fetcher->open && strcmp(fetcher->host, host))
			steam_image_fetcher_close(fetcher);
		if (!fetcher->open)
		{
			char base[300];

			/* (the tests' server, a test build's alone; the URL was checked as ever) */
			if (fetcher->test_base[0])
				snprintf(base, sizeof(base), "%s", fetcher->test_base);
			else
				snprintf(base, sizeof(base), "https://%s", host);
			if (!steam_net_http_open(&fetcher->http, base, error, sizeof(error)))
			{
				snprintf(why, why_size, "the picture's server could not be reached");
				return NULL;
			}
			fetcher->open = 1;
			snprintf(fetcher->host, sizeof(fetcher->host), "%s", host);
			reused = 0;
		}
		body = steam_http_get(&fetcher->http, path, STEAM_IMAGE_BYTES_MAXIMUM, size, &status, FETCH_TIMEOUT_MS, error,
			sizeof(error));
		if (body)
			return body;
		/* (a 200 whose body was refused - more than STEAM_IMAGE_BYTES_MAXIMUM -, a 4xx: the picture's own failure) */
		fetcher->refused = status == 200 || (status >= 400 && status < 500);
		steam_image_fetcher_close(fetcher);
		/* (a kept-alive connection the server had closed: once more on a new one) */
		if (!reused || status)
		{
			snprintf(why, why_size, status == 200 ? "the picture is too large or malformed" : status ?
				"the picture's server said no (%d)" : "the picture did not come", status);
			return NULL;
		}
	}
	snprintf(why, why_size, "the picture did not come");
	return NULL;
}

int steam_image_get(struct steam_image_fetcher *fetcher, const char *root, const char *url, int width, int height,
	int fit, uint8_t *bgra, char *why, size_t why_size)
{
	char host[256], path[512], cache[640];
	uint8_t *body;
	size_t size = 0;
	int ok;

	fetcher->refused = 1;
	if (!steam_image_url(url, width, height, host, sizeof(host), path, sizeof(path), why, why_size))
		return 0;
	fetcher->refused = 0;
	if (root && root[0] && steam_image_cache_path(root, url, width, height, cache, sizeof(cache)) &&
		steam_image_cache_load(cache, width, height, bgra))
	{
		return 1;
	}
	body = fetch(fetcher, host, path, &size, why, why_size);
	if (!body)
		return 0;
	fetcher->fetched++;
	fetcher->fetched_bytes += size;
	ok = steam_image_decode(body, size, width, height, fit, bgra, why, why_size);
	fetcher->refused = !ok;
	free(body);
	if (ok && root && root[0] && steam_image_cache_path(root, url, width, height, cache, sizeof(cache)))
		steam_image_cache_save(root, cache, width, height, bgra);
	return ok;
}
