/*
STEAM_FUZZ.C

libFuzzer targets for every parser of the Steam download (port/steamdl),
one per run, picked by STEAM_FUZZ_TARGET (run_steam_download_test.sh with
STEAM_FUZZ_SECONDS, under AddressSanitizer and UBSan):
  protobuf      the reader, nested fields walked 8 deep
  manifest      an unzipped manifest (names plain, or encrypted under a
                fixed key), each file through the plan's match
  manifest_zip  a manifest as the CDN sends it (a zip of one file)
  kv            KeyValues text, then the depot lookups
  ws_frames     a server's WebSocket byte stream (frames, fragments,
                control frames)
  ws_upgrade    the server's upgrade response
  cm            a CM message (header, body), a Multi (gzipped or not) and
                the bodies the session reads (CDN list, auth, logon)
  http          a CDN's HTTP response (headers, Content-Length, chunked)
  chunk         a decrypted chunk's container (4-byte size, then VZa, VSZa
                or zip) into exactly that size
  journal       a resume journal against three chunks
  cdn_list      byte 0 even: a GetServersForSteamPipe body; odd: CM list JSON
  ws_list       the Workshop's QueryFiles and GetDetails responses (steam_workshop.c), each item checked
  ws_names      a Workshop item's file names (NUL-separated) through the name rule: a taken name must be plain, of
                our characters, inside the item's folder, and two taken names that collide caught
Every input must only be taken or refused: no crash, no leak, no read or
write out of bounds.
*/

#include "steam_chunk.h"
#include "steam_cm.h"
#include "steam_crypto.h"
#include "steam_file.h"
#include "steam_http.h"
#include "steam_kv.h"
#include "steam_manifest.h"
#include "steam_net.h"
#include "steam_pb.h"
#include "steam_plan.h"
#include "steam_session.h"
#include "steam_workshop.h"
#include "steam_ws.h"
#include "steam_zip.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (no network in the fuzzer) */
void steam_random(void *buffer, size_t size)
{
	memset(buffer, 0x5a, size);
}

int steam_net_open(struct steam_stream *stream, const char *url, char *host, size_t host_capacity, char *path,
	size_t path_capacity, char *error, int error_size)
{
	(void)stream, (void)url, (void)host, (void)host_capacity, (void)path, (void)path_capacity, (void)error;
	(void)error_size;
	return 0;
}

int steam_net_http_open(struct steam_http *http, const char *base_url, char *error, int error_size)
{
	(void)http, (void)base_url, (void)error, (void)error_size;
	return 0;
}

/* (steam_job.c's ask of Steam's directory: nothing answers) */
int posix_https_get(const char *url, const char *user_agent, char *body, int capacity, char *error, int error_size)
{
	(void)url, (void)user_agent, (void)body, (void)capacity;
	snprintf(error, (size_t)error_size, "no network in the fuzzer");
	return -1;
}

static const uint8_t fuzz_key[32] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
	24, 25, 26, 27, 28, 29, 30, 31, 32 };

static void walk(const uint8_t *data, size_t size, int depth)
{
	struct steam_pb_reader reader;
	struct steam_pb_field field;

	steam_pb_reader_init(&reader, data, size);
	while (steam_pb_next(&reader, &field) > 0)
		if (field.wire == STEAM_PB_BYTES && depth < STEAM_PB_DEPTH_MAXIMUM)
			walk(field.data, field.size, depth + 1);
}

static int visit(void *context, const struct steam_manifest_file *file)
{
	uint32_t index;
	uint64_t end = 0;

	(void)context;
	if (strlen(file->name) > STEAM_NAME_MAXIMUM)
		abort();
	steam_plan_match(STEAM_GAMES_ALL, STEAM_DEPOT_HALO1_CAMPAIGN, file->name);
	steam_plan_match(STEAM_GAMES_ALL, STEAM_DEPOT_HALO1_MULTIPLAYER, file->name);
	/* (the parser's promises: sorted, inside the file, not overlapping) */
	for (index = 0; index < file->chunk_count; index++)
	{
		if (file->chunks[index].offset < end || file->chunks[index].offset + file->chunks[index].size > file->size ||
			!file->chunks[index].size)
		{
			abort();
		}
		end = file->chunks[index].offset + file->chunks[index].size;
	}
	return 0;
}

static void fuzz_manifest(const uint8_t *data, size_t size)
{
	struct steam_manifest_metadata metadata;
	char error[200];

	if (steam_manifest_parse(data, size, fuzz_key, &metadata, visit, NULL, error, sizeof(error)) == 0)
		steam_manifest_crc_ok(data, size, &metadata);
}

static int cm_visit(void *context, const struct steam_cm_message *message)
{
	(void)context;
	walk(message->body, message->body_size, 0);
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static const char *target;

	if (!target)
	{
		target = getenv("STEAM_FUZZ_TARGET");
		if (!target)
			target = "protobuf";
	}
	if (!strcmp(target, "protobuf"))
		walk(data, size, 0);
	else if (!strcmp(target, "manifest"))
		fuzz_manifest(data, size);
	else if (!strcmp(target, "manifest_zip"))
	{
		size_t entry;

		if (steam_zip_entry_size(data, size, 1u << 22, &entry))
		{
			uint8_t *out = malloc(entry + 1);
			char error[128];

			if (out && steam_zip_extract(data, size, out, entry, error, sizeof(error)))
				fuzz_manifest(out, entry);
			free(out);
		}
	}
	else if (!strcmp(target, "kv"))
	{
		struct steam_kv kv;
		char error[128];
		uint64_t gid;

		if (steam_kv_parse((const char *)data, size, &kv, error, sizeof(error)))
		{
			steam_kv_depot_manifest(&kv, 976733, "public", &gid);
			steam_kv_path(&kv, "appinfo/depots/976736/manifests/public/gid");
			steam_kv_free(&kv);
		}
	}
	else if (!strcmp(target, "ws_frames"))
		steam_ws_feed_test(data, size);
	else if (!strcmp(target, "ws_upgrade"))
		steam_ws_response_ok((const char *)data, size, "dGhlIHNhbXBsZSBub25jZQ==");
	else if (!strcmp(target, "cm"))
	{
		struct steam_cm_message message;
		struct steam_cdn_server servers[4];

		if (steam_cm_parse(data, size, &message))
		{
			walk(message.body, message.body_size, 0);
			if (message.emsg == STEAM_EMSG_MULTI)
				steam_cm_multi(message.body, message.body_size, cm_visit, NULL);
			steam_parse_cdn_servers(message.body, message.body_size, 976730, servers, 4);
		}
	}
	else if (!strcmp(target, "http"))
	{
		struct steam_http_head head;

		steam_http_head_parse((const char *)data, size, &head);
		steam_http_feed_test(data, size);
	}
	else if (!strcmp(target, "chunk"))
	{
		struct steam_chunk_work work;
		uint32_t wanted;
		uint8_t *out;
		char error[128];

		if (size < 4)
			return 0;
		wanted = (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)(data[3] & 0x0f) << 24;
		if (!wanted || wanted > (1u << 20))
			return 0;
		out = malloc(wanted);
		steam_chunk_work_init(&work);
		if (out)
			steam_chunk_decompress(&work, data + 4, size - 4, out, wanted, error, sizeof(error));
		steam_chunk_work_free(&work);
		free(out);
	}
	else if (!strcmp(target, "journal"))
	{
		struct steam_chunk_info chunks[3];
		uint8_t marks[3] = { 0 };

		memset(chunks, 0, sizeof(chunks));
		memset(chunks[2].sha, 0xaa, 20);
		chunks[1].offset = 1000, chunks[2].offset = 2000;
		chunks[0].size = chunks[1].size = chunks[2].size = 1000;
		if (steam_file_journal_parse((const char *)data, size, 1, 2, 3000, 3, chunks, marks) > 3)
			abort();
	}
	else if (!strcmp(target, "ws_list"))
	{
		struct steam_workshop_item items[STEAM_WORKSHOP_ITEMS_MAXIMUM];
		uint32_t total;
		char why[160];
		int count, index;

		count = steam_workshop_parse_query(data, size, items, STEAM_WORKSHOP_ITEMS_MAXIMUM, &total);
		if (count > STEAM_WORKSHOP_ITEMS_MAXIMUM)
			abort();
		for (index = 0; index < count; index++)
		{
			size_t length = strlen(items[index].title), at;

			if (length >= STEAM_WORKSHOP_TITLE_SIZE)
				abort();
			for (at = 0; at < length; at++)
				if (items[index].title[at] < 0x20 || items[index].title[at] > 0x7e)
					abort();
			steam_workshop_item_ok(&items[index], why, sizeof(why));
		}
		count = steam_workshop_parse_details(data, size, items, 3);
		if (count > 3)
			abort();
	}
	else if (!strcmp(target, "ws_names"))
	{
		char taken[8][STEAM_WORKSHOP_NAME_MAXIMUM + 1], name[1100];
		size_t at = 0;
		int count = 0, index;

		while (at < size && count < 8)
		{
			size_t length = 0;
			enum steam_plan_kind kind;
			int verdict;

			while (at < size && data[at] && length < sizeof(name) - 1)
				name[length++] = (char)data[at++];
			name[length] = 0;
			at++;
			verdict = steam_workshop_name(name, taken[count], sizeof(taken[count]), &kind);
			if (verdict != STEAM_WORKSHOP_NAME_TAKE)
				continue;
			/* (a taken name: plain, no '\', no "..", relative, short, ours) */
			{
				const char *written = taken[count];
				size_t written_length = strlen(written), index2;

				if (!written_length || written_length > STEAM_WORKSHOP_NAME_MAXIMUM || written[0] == '/' ||
					strstr(written, "..") == written || strstr(written, "/..") || strstr(written, "//") ||
					strchr(written, '\\') || strchr(written, ':') || !steam_plan_name_plain(written))
				{
					abort();
				}
				for (index2 = 0; index2 < written_length; index2++)
					if ((unsigned char)written[index2] < 0x20 || (unsigned char)written[index2] >= 0x7f)
						abort();
			}
			for (index = 0; index < count; index++)
				steam_workshop_names_collide(taken[index], taken[count]);
			count++;
		}
	}
	else if (!strcmp(target, "cdn_list"))
	{
		if (size && !(data[0] & 1))
		{
			struct steam_cdn_server servers[8];
			int count = steam_parse_cdn_servers(data + 1, size - 1, 976730, servers, 8), index;

			for (index = 0; index < count; index++)
				if (!steam_host_ok(servers[index].host, strlen(servers[index].host)))
					abort();
		}
		else if (size)
		{
			char urls[8][STEAM_HOST_MAXIMUM + 32];

			steam_parse_cm_list((const char *)data + 1, size - 1, urls, 8);
		}
	}
	return 0;
}
