/*
STEAM_REAL_PROBE.C

The Steam download's parsers against Steam's REAL servers with NO account (run_steam_download_test.sh's "real" stage,
STEAM_REAL=1; never part of a game build): an anonymous Steam user, as SteamKit2's LogOnAnonymous and
DepotDownloader's -username-less runs sign in, reads a FREE app's depot (default: app 1007 Steamworks SDK Redist,
depot 1006, which any anonymous user may read) through the same code the Vita's download uses:

  steam_real_probe <app> <depot> <out folder>

  the CM from Steam's directory, the logon (steam_session_logon_anonymous: built with STEAM_SESSION_ANONYMOUS alone),
  the Multis that follow it (steam_cm_multi_facts: their old-style messages skipped, said in the log), the app's
  info (PICS), the depot's key, the manifest's request code, the CDN's servers, the manifest from the CDN, then
  steam_manifest_parse with the key and the checks steam_download.c makes (depot, gid, steam_manifest_crc_ok); the
  manifest's zip is kept as <out>/<depot>_<gid>.zip for the units (test_real_manifests). Prints one line per step and
  "REAL OK" or "REAL FAIL <why>". No owner's account, no token, no QR: nothing of anyone's is used or kept.
*/

#include "steam_chunk.h"
#include "steam_cm.h"
#include "steam_crypto.h"
#include "steam_http.h"
#include "steam_kv.h"
#include "steam_manifest.h"
#include "steam_net.h"
#include "steam_session.h"
#include "steam_zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int posix_https_get(const char *url, const char *user_agent, char *body, int capacity, char *error, int error_size);

static void log_line(void *context, const char *line)
{
	(void)context;
	printf("  log: %s\n", line);
	fflush(stdout);
}

struct counted
{
	unsigned int files, said;
	unsigned long long bytes;
	/* the smallest chunk seen (fetched and decoded after: the chunk path too) */
	int have_chunk;
	struct steam_chunk_info chunk;
};

static int count_file(void *context, const struct steam_manifest_file *file)
{
	struct counted *counted = context;

	counted->files++;
	counted->bytes += file->size;
	if (file->chunk_count && (!counted->have_chunk || file->chunks[0].size < counted->chunk.size))
	{
		counted->chunk = file->chunks[0];
		counted->have_chunk = 1;
	}
	if (counted->said++ < 4)
		printf("  file: %s (%llu bytes, %u chunks)\n", file->name, (unsigned long long)file->size,
			(unsigned int)file->chunk_count);
	return 0;
}

static int fail(struct steam_session *session, const char *why)
{
	printf("REAL FAIL %s\n", why);
	steam_session_end(session);
	return 1;
}

int main(int argc, char **argv)
{
	struct steam_session session;
	struct steam_kv info;
	struct steam_cdn_server servers[8];
	struct steam_manifest_metadata metadata;
	struct counted counted;
	char *directory, urls[4][STEAM_HOST_MAXIMUM + 32], error[256], path[160], base[STEAM_HOST_MAXIMUM + 16];
	uint32_t app, depot, crc = 0;
	uint64_t gid = 0, code = 0;
	const uint8_t *key;
	uint8_t *zipped = NULL, *manifest = NULL;
	size_t zipped_size = 0, manifest_size = 0;
	int length, count, index, status = 0;

	if (argc < 4)
	{
		fprintf(stderr, "usage: steam_real_probe <app> <depot> <out folder>\n");
		return 2;
	}
	app = (uint32_t)strtoul(argv[1], NULL, 10);
	depot = (uint32_t)strtoul(argv[2], NULL, 10);
	directory = malloc(1u << 18);
	if (!directory || !steam_session_init(&session, log_line, NULL, NULL))
		return 1;
	length = posix_https_get("https://api.steampowered.com/ISteamDirectory/GetCMListForConnect/v1/?cellid=0&cmtype=websockets",
		"steamdl-test", directory, 1 << 18, error, sizeof(error));
	count = length > 0 ? steam_parse_cm_list(directory, (size_t)length, urls, 4) : 0;
	free(directory);
	if (count <= 0)
		return fail(&session, "no CM from Steam's directory");
	printf("cm: %d listed\n", count);
	for (index = 0; index < count; index++)
		if (steam_session_connect(&session, urls[index], error, sizeof(error)))
			break;
	if (index == count)
		return fail(&session, error);
	if (!steam_session_logon_anonymous(&session, error, sizeof(error)))
		return fail(&session, error);
	/* (the Multis that follow a logon: read here, their facts in the log) */
	steam_session_pump(&session, 3000);
	printf("multi: %u old-style messages skipped\n", session.skipped_messages);
	if (!steam_session_app_info(&session, app, &info, error, sizeof(error)))
		return fail(&session, error);
	if (!steam_kv_depot_manifest(&info, depot, "public", &gid))
	{
		steam_kv_free(&info);
		return fail(&session, "the depot has no public manifest in the app's info");
	}
	steam_kv_free(&info);
	printf("app info: depot %u public gid %llu\n", (unsigned int)depot, (unsigned long long)gid);
	key = steam_session_depot_key(&session, app, depot, error, sizeof(error));
	if (!key)
		return fail(&session, error);
	if (!steam_session_manifest_code(&session, app, depot, gid, "public", &code, error, sizeof(error)))
		return fail(&session, error);
	printf("request code: received (not printed)\n");
	count = steam_session_cdn_servers_for(&session, app, servers, 8, error, sizeof(error));
	if (count <= 0)
		return fail(&session, error);
	snprintf(path, sizeof(path), "/depot/%u/manifest/%llu/5/%llu", (unsigned int)depot, (unsigned long long)gid,
		(unsigned long long)code);
	for (index = 0; index < count && !zipped; index++)
	{
		struct steam_http http;

		snprintf(base, sizeof(base), "https://%s", servers[index].host);
		if (!steam_net_http_open(&http, base, error, sizeof(error)))
			continue;
		zipped = steam_http_get(&http, path, STEAM_MANIFEST_ZIPPED_MAXIMUM, &zipped_size, &status, 60000, error,
			sizeof(error));
		steam_http_close(&http);
		printf("cdn: %s HTTP %d, %u bytes\n", servers[index].host, status, (unsigned int)zipped_size);
	}
	steam_wipe(path, sizeof(path));
	if (!zipped)
		return fail(&session, "the manifest did not come");
	snprintf(path, sizeof(path), "%s/%u_%llu.zip", argv[3], (unsigned int)depot, (unsigned long long)gid);
	{
		FILE *file = fopen(path, "wb");

		if (file)
		{
			fwrite(zipped, 1, zipped_size, file);
			fclose(file);
			printf("kept: %s\n", path);
		}
	}
	if (!steam_zip_entry_size(zipped, zipped_size, STEAM_MANIFEST_MAXIMUM, &manifest_size) ||
		!(manifest = malloc(manifest_size + 1)) ||
		!steam_zip_extract(zipped, zipped_size, manifest, manifest_size, error, sizeof(error)))
	{
		free(zipped);
		return fail(&session, "the manifest is not a zip of one file");
	}
	free(zipped);
	memset(&counted, 0, sizeof(counted));
	if (steam_manifest_parse(manifest, manifest_size, key, &metadata, count_file, &counted, error, sizeof(error)) != 0)
	{
		free(manifest);
		return fail(&session, error);
	}
	steam_manifest_crc(manifest, manifest_size, &crc);
	printf("manifest: depot %u gid %llu names %s, %u files %llu bytes; payload crc %08x crc_encrypted %08x crc_clear "
		"%08x\n", (unsigned int)metadata.depot_id, (unsigned long long)metadata.gid,
		metadata.filenames_encrypted ? "encrypted" : "clear", counted.files, counted.bytes, (unsigned int)crc,
		(unsigned int)metadata.crc_encrypted, (unsigned int)metadata.crc_clear);
	if (metadata.depot_id != depot || metadata.gid != gid)
	{
		free(manifest);
		return fail(&session, "another depot's or version's manifest");
	}
	if (!steam_manifest_crc_ok(manifest, manifest_size, &metadata))
	{
		free(manifest);
		return fail(&session, "steam_manifest_crc_ok refused it");
	}
	free(manifest);
	/* one chunk through steam_chunk_process (decrypt, the container's decoder, Adler-32, SHA-1) */
	if (counted.have_chunk)
	{
		struct steam_chunk_work work;
		uint8_t *output = malloc(counted.chunk.size + 1), *data = NULL;
		size_t size = 0;
		char hex[41];
		int ok;

		steam_hex(counted.chunk.sha, 20, hex);
		snprintf(path, sizeof(path), "/depot/%u/chunk/%s", (unsigned int)depot, hex);
		for (index = 0; index < count && !data; index++)
		{
			struct steam_http http;

			snprintf(base, sizeof(base), "https://%s", servers[index].host);
			if (!steam_net_http_open(&http, base, error, sizeof(error)))
				continue;
			data = steam_http_get(&http, path, STEAM_CHUNK_COMPRESSED_MAXIMUM, &size, &status, 60000, error,
				sizeof(error));
			steam_http_close(&http);
		}
		if (!data || !output)
		{
			free(data);
			free(output);
			return fail(&session, "the chunk did not come");
		}
		steam_chunk_work_init(&work);
		ok = steam_chunk_process(&work, key, &counted.chunk, data, size, output, error, sizeof(error));
		printf("chunk: %u bytes in, %u out, %s (containers: VZa %u, VSZa %u, zip %u)\n", (unsigned int)size,
			(unsigned int)counted.chunk.size, ok ? "checked" : error, work.decoded_chunks[0], work.decoded_chunks[1],
			work.decoded_chunks[2]);
		steam_chunk_work_free(&work);
		free(data);
		free(output);
		if (!ok)
			return fail(&session, "the chunk was refused");
	}
	steam_session_end(&session);
	printf("REAL OK\n");
	return 0;
}
