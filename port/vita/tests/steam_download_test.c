/*
STEAM_DOWNLOAD_TEST.C

The Steam download's tests on the PC (run_steam_download_test.sh), no Steam:
  units <fixtures>   each piece alone: protobuf, the crypto helpers, zip and
                     gzip, KeyValues, WebSocket frames and the upgrade, CM
                     framing and Multi, the plan (hostile names), chunks
                     (steam_fake_server.py's fixtures: VZa, VSZa, zip; each
                     damaged), a manifest with encrypted names, the file
                     writer and its journal (resume, a damaged .part)
  workshop <cm port> <cdn port> <out> <id,...> [cancel after n progress calls]
                     the Steam Workshop against the fake: the list (a page,
                     a search), the chosen items downloaded, then each staged
                     item installed into the mods folder by
                     steam_workshop_install with the test's map check
  browse <cm port> <cdn port> <out>
                     the Workshop browser's job against the fake (steam_job.c as
                     the STEAM WORKSHOP screen drives it): the pages, the tabs
                     (SUBSCRIBED too), the authors, the cards' pictures (the
                     fake's hostile ones refused), a details page (its pictures,
                     what its files are), the memory held, then signed out with
                     nothing chosen; prints one result line
  flow <cm port> <cdn port> <out> <games> [cancel after n progress calls]
                     the whole download against steam_fake_server.py on
                     127.0.0.1; prints one result line; the log goes to
                     <out>/log.txt (the script checks no token is in it)
*/

#include "steam_chunk.h"
#include "steam_cm.h"
#include "steam_crypto.h"
#include "steam_download.h"
#include "steam_file.h"
#include "steam_image.h"
#include "steam_job.h"
#include "steam_kv.h"
#include "steam_manifest.h"
#include "steam_net.h"
#include "steam_pb.h"
#include "steam_plan.h"
#include "steam_session.h"
#include "steam_workshop.h"
#include "steam_ws.h"
#include "steam_zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int failures, checks;

#define CHECK(condition) \
	do \
	{ \
		checks++; \
		if (!(condition)) \
		{ \
			failures++; \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

static uint8_t *read_file(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	uint8_t *data;
	long length;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)length + 1);
	if (data && fread(data, 1, (size_t)length, file) != (size_t)length)
	{
		free(data);
		data = NULL;
	}
	fclose(file);
	*size = (size_t)length;
	return data;
}

/* ---------- protobuf */

static void test_pb(void)
{
	uint8_t buffer[256];
	struct steam_pb_writer writer;
	struct steam_pb_reader reader;
	struct steam_pb_field field;
	size_t nested;

	steam_pb_writer_init(&writer, buffer, sizeof(buffer));
	steam_pb_put_varint(&writer, 1, 300);
	steam_pb_put_int32(&writer, 2, -5);
	steam_pb_put_fixed64(&writer, 3, 0x0110000100000000ull);
	nested = steam_pb_begin(&writer, 4);
	steam_pb_put_string(&writer, 1, "inner");
	steam_pb_end(&writer, nested);
	steam_pb_put_fixed32(&writer, 536870911, 7);
	CHECK(!writer.error);
	steam_pb_reader_init(&reader, buffer, writer.size);
	CHECK(steam_pb_next(&reader, &field) == 1 && field.number == 1 && field.value == 300);
	CHECK(steam_pb_next(&reader, &field) == 1 && field.number == 2 && steam_pb_int32(&field) == -5);
	CHECK(steam_pb_next(&reader, &field) == 1 && field.number == 3 && field.value == 0x0110000100000000ull);
	CHECK(steam_pb_next(&reader, &field) == 1 && field.number == 4 && field.size == 7);
	{
		struct steam_pb_reader inner;
		struct steam_pb_field item;

		steam_pb_reader_init(&inner, field.data, field.size);
		CHECK(steam_pb_next(&inner, &item) == 1 && item.size == 5 && !memcmp(item.data, "inner", 5));
		CHECK(steam_pb_next(&inner, &item) == 0);
	}
	CHECK(steam_pb_next(&reader, &field) == 1 && field.number == 536870911 && field.value == 7);
	CHECK(steam_pb_next(&reader, &field) == 0);
	/* refused: past the end, an 11-byte varint, field 0, a group */
	{
		static const uint8_t past[] = { 0x0a, 0x05, 'a' };
		static const uint8_t long_varint[] = { 0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01 };
		static const uint8_t tenth[] = { 0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x02 };
		static const uint8_t zero[] = { 0x00, 0x01 };
		static const uint8_t group[] = { 0x0b, 0x0c };

		steam_pb_reader_init(&reader, past, sizeof(past));
		CHECK(steam_pb_next(&reader, &field) == -1 && steam_pb_next(&reader, &field) == -1);
		steam_pb_reader_init(&reader, long_varint, sizeof(long_varint));
		CHECK(steam_pb_next(&reader, &field) == -1);
		steam_pb_reader_init(&reader, tenth, sizeof(tenth));
		CHECK(steam_pb_next(&reader, &field) == -1);
		steam_pb_reader_init(&reader, zero, sizeof(zero));
		CHECK(steam_pb_next(&reader, &field) == -1);
		steam_pb_reader_init(&reader, group, sizeof(group));
		CHECK(steam_pb_next(&reader, &field) == -1);
	}
	/* a writer that does not fit writes nothing past its buffer */
	steam_pb_writer_init(&writer, buffer, 4);
	steam_pb_put_string(&writer, 1, "too long for four bytes");
	CHECK(writer.error && writer.size <= 4);
}

/* ---------- crypto, zip */

static void test_crypto(void)
{
	uint8_t digest[20], out[64];
	char hex[41];

	steam_sha1("abc", 3, digest);
	steam_hex(digest, 20, hex);
	CHECK(!strcmp(hex, "a9993e364706816aba3e25717850c26c9cd0d89d"));
	CHECK(steam_crc32(0, "123456789", 9) == 0xcbf43926u);
	/* (Adler-32 from 0: zlib's adler32(0, "abc") ) */
	CHECK(steam_adler32_zero("abc", 3) == ((uint32_t)(97 + 195 + 294) << 16 | 294));
	CHECK(steam_base64_decode("aGVsbG8=", 8, out, sizeof(out)) == 5 && !memcmp(out, "hello", 5));
	CHECK(steam_base64_decode("a@==", 4, out, sizeof(out)) < 0);
	{
		uint8_t key[32] = { 0 }, data[48] = { 0 };

		/* (whole blocks of zeros under a zero key: the padding is wrong) */
		CHECK(steam_symmetric_decrypt(key, data, sizeof(data), out) < 0);
		CHECK(steam_symmetric_decrypt(key, data, 31, out) < 0);
	}
}

static void test_zip(const char *fixtures)
{
	char path[600];
	size_t size, entry;
	uint8_t *zip, *out;
	char error[128];

	snprintf(path, sizeof(path), "%s/manifest.zip", fixtures);
	zip = read_file(path, &size);
	CHECK(zip != NULL);
	if (!zip)
		return;
	CHECK(steam_zip_entry_size(zip, size, 64u << 20, &entry));
	out = malloc(entry + 1);
	CHECK(steam_zip_extract(zip, size, out, entry, error, sizeof(error)));
	/* a byte of the data changed: the CRC refuses it (or it does not inflate) */
	zip[40] ^= 0x10;
	CHECK(!steam_zip_extract(zip, size, out, entry, error, sizeof(error)));
	zip[40] ^= 0x10;
	/* cut short */
	CHECK(!steam_zip_entry_size(zip, size - 1, 64u << 20, &entry) || !steam_zip_extract(zip, size - 1, out, entry, error,
		sizeof(error)));
	free(out);
	free(zip);
}

/* ---------- KeyValues */

static void test_kv(void)
{
	static const char text[] =
		"\"appinfo\"\n{\n\t\"appid\" \"976730\"\n\t\"depots\"\n\t{\n\t\t\"976733\"\n\t\t{\n\t\t\t\"manifests\"\n\t\t\t{\n"
		"\t\t\t\t\"public\"\n\t\t\t\t{\n\t\t\t\t\t\"gid\"\t\"886692270880710564\"\n\t\t\t\t}\n\t\t\t}\n\t\t}\n"
		"\t\t\"976734\" { \"manifests\" { \"public\" \"7232036684651310417\" } } // old form\n"
		"\t\t\"escaped\\\"key\" \"a\\\\b\" [$WIN32]\n\t}\n}\n";
	struct steam_kv kv;
	char error[128];
	uint64_t gid = 0;

	CHECK(steam_kv_parse(text, sizeof(text), &kv, error, sizeof(error)));
	CHECK(steam_kv_depot_manifest(&kv, 976733, "public", &gid) && gid == 886692270880710564ull);
	CHECK(steam_kv_depot_manifest(&kv, 976734, "PUBLIC", &gid) && gid == 7232036684651310417ull);
	CHECK(!steam_kv_depot_manifest(&kv, 976735, "public", &gid));
	{
		const struct steam_kv_node *node = steam_kv_path(&kv, "appinfo/depots/escaped\"key");

		CHECK(node && node->value && !strcmp(node->value, "a\\b"));
	}
	steam_kv_free(&kv);
	CHECK(!steam_kv_parse("\"a\" { \"b\" \"c\"", 14, &kv, error, sizeof(error)));
	CHECK(!steam_kv_parse("}", 1, &kv, error, sizeof(error)));
	CHECK(!steam_kv_parse("\"a\"", 3, &kv, error, sizeof(error)));
	{
		/* 40 levels: refused */
		char deep[400];
		int level;

		deep[0] = 0;
		for (level = 0; level < 40; level++)
			strcat(deep, "a{");
		for (level = 0; level < 40; level++)
			strcat(deep, "}");
		CHECK(!steam_kv_parse(deep, strlen(deep), &kv, error, sizeof(error)));
	}
	/* a gid past 64 bits: not a gid */
	{
		static const char big[] = "\"appinfo\" { \"depots\" { \"1\" { \"manifests\" { \"public\" \"18446744073709551616\" } } } }";

		CHECK(steam_kv_parse(big, sizeof(big) - 1, &kv, error, sizeof(error)));
		CHECK(!steam_kv_depot_manifest(&kv, 1, "public", &gid));
		steam_kv_free(&kv);
	}
}

/* ---------- WebSocket */

static void test_ws(void)
{
	struct steam_ws_frame frame;
	static const uint8_t binary[] = { 0x82, 0x03, 1, 2, 3 };
	static const uint8_t masked[] = { 0x82, 0x83, 0, 0, 0, 0, 1, 2, 3 };
	static const uint8_t long_ping[] = { 0x89, 0x7e, 0x00, 0x80 };
	static const uint8_t short_form[] = { 0x82, 0x7e, 0x00, 0x10 };
	static const uint8_t huge[] = { 0x82, 0x7f, 0, 0, 0, 0, 0x10, 0, 0, 0 };
	static const uint8_t rsv[] = { 0xc2, 0x00 };
	static const uint8_t opcode[] = { 0x83, 0x00 };
	char response[512];

	CHECK(steam_ws_frame_parse(binary, sizeof(binary), &frame) == 2 && frame.payload_size == 3);
	CHECK(steam_ws_frame_parse(binary, 1, &frame) == 0);
	CHECK(steam_ws_frame_parse(masked, sizeof(masked), &frame) == -1);
	CHECK(steam_ws_frame_parse(long_ping, sizeof(long_ping), &frame) == -1);
	CHECK(steam_ws_frame_parse(short_form, sizeof(short_form), &frame) == -1);
	CHECK(steam_ws_frame_parse(huge, sizeof(huge), &frame) == -1);
	CHECK(steam_ws_frame_parse(rsv, sizeof(rsv), &frame) == -1);
	CHECK(steam_ws_frame_parse(opcode, sizeof(opcode), &frame) == -1);
	/* RFC 6455's example key */
	snprintf(response, sizeof(response), "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
		"Connection: Upgrade\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n");
	CHECK(steam_ws_response_ok(response, strlen(response), "dGhlIHNhbXBsZSBub25jZQ=="));
	CHECK(!steam_ws_response_ok(response, strlen(response), "AAAAAAAAAAAAAAAAAAAAAA=="));
	CHECK(!steam_ws_response_ok("HTTP/1.1 200 OK\r\n\r\n", 19, "dGhlIHNhbXBsZSBub25jZQ=="));
	{
		/* a fragmented message, a ping between, then a close */
		static const uint8_t stream[] = { 0x02, 0x02, 'a', 'b', 0x89, 0x00, 0x80, 0x01, 'c', 0x82, 0x00, 0x88, 0x00 };
		static const uint8_t stray[] = { 0x80, 0x01, 'x' };

		CHECK(steam_ws_feed_test(stream, sizeof(stream)) == 2);
		CHECK(steam_ws_feed_test(stray, sizeof(stray)) == -1);
	}
}

/* ---------- CM */

static int multi_seen;

static int count_multi(void *context, const struct steam_cm_message *message)
{
	(void)context;
	if (message->emsg == 751 && message->header.job_target == 9)
		multi_seen++;
	return 0;
}

static void test_cm(void)
{
	uint8_t buffer[512], body[300];
	struct steam_cm_header header;
	struct steam_cm_message message;
	struct steam_pb_writer writer;
	long length;

	memset(&header, 0, sizeof(header));
	header.steamid = 76561198000000000ull;
	header.session_id = 42;
	header.job_source = 7;
	header.job_target = STEAM_JOB_NONE;
	snprintf(header.job_name, sizeof(header.job_name), "Authentication.RevokeToken#1");
	length = steam_cm_build(buffer, sizeof(buffer), STEAM_EMSG_SERVICE_METHOD_CALL, &header, "xy", 2);
	CHECK(length > 0);
	CHECK(steam_cm_parse(buffer, (size_t)length, &message) && message.emsg == STEAM_EMSG_SERVICE_METHOD_CALL &&
		message.header.steamid == header.steamid && message.header.session_id == 42 && message.header.job_source == 7 &&
		message.header.job_target == STEAM_JOB_NONE && !strcmp(message.header.job_name, header.job_name) &&
		message.body_size == 2 && !memcmp(message.body, "xy", 2));
	CHECK(!steam_cm_parse(buffer, 7, &message));
	buffer[3] &= 0x7f;
	CHECK(!steam_cm_parse(buffer, (size_t)length, &message));
	/* a Multi (not zipped) of two messages, one with job target 9 */
	{
		uint8_t inner[64], payload[160];
		long a, b;
		size_t used = 0;

		header.job_source = STEAM_JOB_NONE;
		header.job_target = 9;
		header.job_name[0] = 0;
		a = steam_cm_build(inner, sizeof(inner), 751, &header, "", 0);
		payload[0] = (uint8_t)a, payload[1] = payload[2] = payload[3] = 0;
		memcpy(payload + 4, inner, (size_t)a);
		used = 4 + (size_t)a;
		header.job_target = STEAM_JOB_NONE;
		b = steam_cm_build(inner, sizeof(inner), 768, &header, "", 0);
		payload[used] = (uint8_t)b, payload[used + 1] = payload[used + 2] = payload[used + 3] = 0;
		memcpy(payload + used + 4, inner, (size_t)b);
		used += 4 + (size_t)b;
		steam_pb_writer_init(&writer, body, sizeof(body));
		steam_pb_put_bytes(&writer, 2, payload, used);
		CHECK(steam_cm_multi(body, writer.size, count_multi, NULL) == 2 && multi_seen == 1);
		/* a length past the end */
		payload[0] = 200;
		steam_pb_writer_init(&writer, body, sizeof(body));
		steam_pb_put_bytes(&writer, 2, payload, used);
		CHECK(steam_cm_multi(body, writer.size, NULL, NULL) == -1);
		/* a size_unzipped with no gzip */
		steam_pb_writer_init(&writer, body, sizeof(body));
		steam_pb_put_varint(&writer, 1, 100);
		steam_pb_put_bytes(&writer, 2, payload, used);
		CHECK(steam_cm_multi(body, writer.size, NULL, NULL) == -1);
		steam_pb_writer_init(&writer, body, sizeof(body));
		steam_pb_put_varint(&writer, 1, 1u << 30);
		steam_pb_put_bytes(&writer, 2, payload, used);
		CHECK(steam_cm_multi(body, writer.size, NULL, NULL) == -1);
	}
	/* the CDN list: a mandatory-HTTPS cache taken, plain and proxies not */
	{
		uint8_t list[400], server[200];
		struct steam_pb_writer item;
		struct steam_cdn_server servers[4];

		steam_pb_writer_init(&writer, list, sizeof(list));
		steam_pb_writer_init(&item, server, sizeof(server));
		steam_pb_put_string(&item, 1, "SteamCache");
		steam_pb_put_string(&item, 8, "cache1-iad1.steamcontent.com");
		steam_pb_put_string(&item, 12, "mandatory");
		steam_pb_put_bytes(&writer, 1, server, item.size);
		steam_pb_writer_init(&item, server, sizeof(server));
		steam_pb_put_string(&item, 1, "CDN");
		steam_pb_put_string(&item, 8, "plain.example");
		steam_pb_put_string(&item, 12, "unavailable");
		steam_pb_put_bytes(&writer, 1, server, item.size);
		steam_pb_writer_init(&item, server, sizeof(server));
		steam_pb_put_string(&item, 1, "SteamCache");
		steam_pb_put_string(&item, 8, "evil host/../x");
		steam_pb_put_string(&item, 12, "mandatory");
		steam_pb_put_bytes(&writer, 1, server, item.size);
		CHECK(steam_parse_cdn_servers(list, writer.size, 976730, servers, 4) == 1 &&
			!strcmp(servers[0].host, "cache1-iad1.steamcontent.com"));
	}
	{
		static const char json[] = "{\"response\":{\"serverlist\":[{\"endpoint\":\"cmp1-ord1.steamserver.net:443\","
			"\"type\":\"websockets\"},{\"endpoint\":\"bad host:443\"},{\"endpoint\":\"x.net:99999\"},"
			"{\"endpoint\":\"cmp2-sea1.steamserver.net:27021\"}]}}";
		char urls[4][STEAM_HOST_MAXIMUM + 32];

		CHECK(steam_parse_cm_list(json, sizeof(json) - 1, urls, 4) == 2 &&
			!strcmp(urls[0], "wss://cmp1-ord1.steamserver.net:443/cmsocket/"));
	}
}

/* ---------- the plan */

static void test_plan(void)
{
	char path[300];
	const struct steam_plan_file *file;

	file = steam_plan_match(STEAM_GAMES_ALL, STEAM_DEPOT_HALO1_CAMPAIGN, "halo1/maps/A10.map");
	CHECK(file && !strcmp(file->destination, "mcc/halo1/maps/a10.map") && file->root == STEAM_ROOT_CE);
	CHECK(steam_plan_path(file, "ux0:data/haloce-vita", "ux0:data/haloce-vita", path, sizeof(path)) &&
		!strcmp(path, "ux0:data/haloce-vita/mcc/halo1/maps/a10.map"));
	CHECK(!steam_plan_match(STEAM_GAMES_ALL, STEAM_DEPOT_HALO1_MULTIPLAYER, "halo1/maps/a10.map"));
	/* bitmaps.map from 976733 when Halo CE is chosen, never twice */
	CHECK(steam_plan_match(STEAM_GAME_HALO_CE, STEAM_DEPOT_HALO1_CAMPAIGN, "halo1/maps/bitmaps.map"));
	CHECK(!steam_plan_match(STEAM_GAME_HALO_CE, STEAM_DEPOT_HALO1_MULTIPLAYER, "halo1/maps/bitmaps.map"));
	/* the CELT banks only with STEAM_GAME_HALO_CE_BANKS, from 976733; sounds_adpcm.fsb and the lists with Halo CE */
	CHECK(steam_plan_match(STEAM_GAMES_ALL, STEAM_DEPOT_HALO1_CAMPAIGN, "halo1/sound/pc/sounds_music.fsb"));
	CHECK(steam_plan_match(STEAM_GAME_HALO_CE_BANKS, STEAM_DEPOT_HALO1_CAMPAIGN, "halo1/sound/pc/sounds_dialogs.fsb"));
	CHECK(!steam_plan_match(STEAM_GAME_HALO_CE, STEAM_DEPOT_HALO1_CAMPAIGN, "halo1/sound/pc/sounds_music.fsb"));
	CHECK(!steam_plan_match(STEAM_GAME_HALO_CE_BANKS, STEAM_DEPOT_HALO1_CAMPAIGN, "halo1/sound/pc/sounds_adpcm.fsb"));
	CHECK(!steam_plan_match(STEAM_GAME_HALO_CE_BANKS, STEAM_DEPOT_HALO1_MULTIPLAYER, "halo1/sound/pc/sounds_music.fsb"));
	file = steam_plan_match(STEAM_GAME_HALO_CE, STEAM_DEPOT_HALO1_CAMPAIGN, "halo1/sound/pc/lst/sounds_music.lst.bin");
	CHECK(file && !strcmp(file->destination, "mcc/halo1/sound/pc/lst/sounds_music.lst.bin") && !file->required);
	/* the depots of each choice: the banks alone need 976733 only */
	{
		uint32_t depots[4];

		CHECK(steam_plan_depots(STEAM_GAME_HALO_CE, depots) == 2 && depots[0] == STEAM_DEPOT_HALO1_CAMPAIGN &&
			depots[1] == STEAM_DEPOT_HALO1_MULTIPLAYER);
		CHECK(steam_plan_depots(STEAM_GAME_HALO_CE_BANKS, depots) == 1 && depots[0] == STEAM_DEPOT_HALO1_CAMPAIGN);
		CHECK(steam_plan_depots(STEAM_GAMES_ALL, depots) == 2);
	}
	/* hostile names never match */
	CHECK(!steam_plan_match(STEAM_GAMES_ALL, STEAM_DEPOT_HALO1_CAMPAIGN, "halo1/maps/../maps/a10.map"));
	CHECK(!steam_plan_match(STEAM_GAMES_ALL, STEAM_DEPOT_HALO1_CAMPAIGN, "/halo1/maps/a10.map"));
	CHECK(!steam_plan_name_plain("halo1//maps"));
	CHECK(!steam_plan_name_plain("c:evil"));
	CHECK(!steam_plan_name_plain("a/./b"));
	CHECK(steam_plan_name_plain("halo1/maps/a10.map"));
	{
		uint8_t head[16] = { 'd', 'a', 'e', 'h', 13, 0, 0, 0 };

		CHECK(steam_plan_header_ok(STEAM_KIND_MCC_MAP, "x.map", head, 16, 1000));
		head[4] = 7;
		CHECK(!steam_plan_header_ok(STEAM_KIND_MCC_MAP, "x.map", head, 16, 1000));
		CHECK(steam_plan_header_ok(STEAM_KIND_BANK, "x.fsb", (const uint8_t *)"FSB5....", 8, 1000));
		CHECK(!steam_plan_header_ok(STEAM_KIND_BANK, "x.fsb", (const uint8_t *)"FSB4....", 8, 1000));
	}
}

/* ---------- the Workshop's pieces */

static void write_test_file(const char *path, const void *data, size_t size)
{
	FILE *file = fopen(path, "wb");

	if (file)
	{
		fwrite(data, 1, size, file);
		fclose(file);
	}
}

static int test_map_check(void *context, const char *path, char *why, size_t why_size)
{
	uint8_t head[8];
	FILE *file = fopen(path, "rb");
	size_t got;

	(void)context;
	if (!file)
	{
		snprintf(why, why_size, "cannot open");
		return 0;
	}
	got = fread(head, 1, sizeof(head), file);
	fclose(file);
	if (got < 8 || memcmp(head, "daeh", 4) || head[4] != 13)
	{
		snprintf(why, why_size, "not a version 13 cache");
		return 0;
	}
	if (strstr(path, "badcheck"))
	{
		snprintf(why, why_size, "the test's check refused it");
		return 0;
	}
	return 1;
}

static void test_workshop(const char *folder)
{
	char relative[200], root[600], path[1200], why[200];
	enum steam_plan_kind kind;
	uint64_t id, ids[4];
	uint8_t body[512];
	size_t size;

	/* names: taken as written, '\\' made '/'; kinds by extension */
	CHECK(steam_workshop_name("maps\\a10.map", relative, sizeof(relative), &kind) == STEAM_WORKSHOP_NAME_TAKE &&
		!strcmp(relative, "maps/a10.map") && kind == STEAM_KIND_MCC_MAP);
	CHECK(steam_workshop_name("maps/Bitmaps.MAP", relative, sizeof(relative), &kind) == STEAM_WORKSHOP_NAME_TAKE &&
		kind == STEAM_KIND_RESOURCE);
	CHECK(steam_workshop_name("CampaignInfo.json", relative, sizeof(relative), &kind) == STEAM_WORKSHOP_NAME_TAKE &&
		kind == STEAM_KIND_WORKSHOP_TEXT);
	CHECK(steam_workshop_name("assets/Ruby's (1).png", relative, sizeof(relative), &kind) == STEAM_WORKSHOP_NAME_TAKE &&
		kind == STEAM_KIND_WORKSHOP_PNG);
	CHECK(steam_workshop_name("tools/setup.exe", relative, sizeof(relative), &kind) == STEAM_WORKSHOP_NAME_SKIP);
	CHECK(steam_workshop_name("maps/a10.map.part", relative, sizeof(relative), &kind) == STEAM_WORKSHOP_NAME_SKIP);
	/* names refused (the whole item): leaving the folder, absolute, drives, control bytes, empty parts */
	{
		static const char *const refused[] = { "../evil.map", "..\\..\\evil.map", "/tmp/evil.map", "\\evil.map",
			"C:\\evil.map", "c:evil.map", "maps/../../evil.map", "maps//a.map", "maps/./a.map", "maps/a\x01.map", "",
			"maps/", "maps/a.map\x7f" };
		/* names skipped (not written, the item kept): hidden, trimmed ends, odd characters, too deep, too long */
		static const char *const skipped[] = { ".hidden/a.map", "maps/.a.map", "campaign/.json", "maps/a.map.",
			"maps/a.map ", " maps/a.map", "maps/a%2e.map", "maps/a*.map", "maps/a?.map", "maps/a\"b.map", "maps/a<b.map",
			"maps/a|b.map", "a/b/c/d/e.map", "maps/\xc3\xa9.map" };
		size_t index;

		for (index = 0; index < sizeof(refused) / sizeof(refused[0]); index++)
		{
			int verdict = steam_workshop_name(refused[index], relative, sizeof(relative), &kind);

			CHECK(verdict == STEAM_WORKSHOP_NAME_REFUSE);
			if (verdict != STEAM_WORKSHOP_NAME_REFUSE)
				fprintf(stderr, "  (refused name %zu: %d)\n", index, verdict);
		}
		for (index = 0; index < sizeof(skipped) / sizeof(skipped[0]); index++)
		{
			int verdict = steam_workshop_name(skipped[index], relative, sizeof(relative), &kind);

			CHECK(verdict == STEAM_WORKSHOP_NAME_SKIP);
			if (verdict != STEAM_WORKSHOP_NAME_SKIP)
				fprintf(stderr, "  (skipped name %zu: %d)\n", index, verdict);
		}
		/* (a real item's picture: "images/Halo  The Master Chief Collection Screenshot 2022.02.16 - ....png", 73) */
		memset(path, 'a', 200);
		memcpy(path + 196, ".map", 5);
		CHECK(steam_workshop_name(path, relative, sizeof(relative), &kind) == STEAM_WORKSHOP_NAME_SKIP);
		memset(path, 'a', 70);
		memcpy(path + 66, ".map", 5);
		CHECK(steam_workshop_name(path, relative, sizeof(relative), &kind) == STEAM_WORKSHOP_NAME_SKIP);
		memset(path, 'a', 1100);
		path[1100] = 0;
		CHECK(steam_workshop_name(path, relative, sizeof(relative), &kind) == STEAM_WORKSHOP_NAME_REFUSE);
		CHECK(steam_workshop_name("a/b/c/d.map", relative, sizeof(relative), &kind) == STEAM_WORKSHOP_NAME_TAKE);
		CHECK(steam_workshop_name("modded halo3.dll (REQUIRED)\\halo3.dll", relative, sizeof(relative), &kind) ==
			STEAM_WORKSHOP_NAME_SKIP);
	}
	CHECK(steam_workshop_names_collide("maps/A10.map", "maps/a10.MAP"));
	CHECK(!steam_workshop_names_collide("maps/a10.map", "maps/a100.map"));
	/* ids: the mods folder's key */
	CHECK(steam_workshop_id_parse("2915693904", &id) && id == 2915693904ull);
	CHECK(!steam_workshop_id_parse("0123", &id) && !steam_workshop_id_parse("12a", &id) &&
		!steam_workshop_id_parse("1234567890123", &id) && !steam_workshop_id_parse("", &id));
	CHECK(steam_workshop_id_text(2915693904ull, relative, sizeof(relative)) && !strcmp(relative, "2915693904"));
	CHECK(!steam_workshop_id_text(1234567890123ull, relative, sizeof(relative)) &&
		!steam_workshop_id_text(0, relative, sizeof(relative)));
	/* requests: the fields Steam's protobufs give */
	size = steam_workshop_query_request(body, sizeof(body), 2, STEAM_WORKSHOP_SORT_NEW, "");
	CHECK(size > 0);
	{
		struct steam_pb_reader reader;
		struct steam_pb_field field;
		int seen = 0;

		steam_pb_reader_init(&reader, body, size);
		while (steam_pb_next(&reader, &field) > 0)
		{
			if (field.number == 1)
				seen |= field.value == 1 ? 1 : 0;
			if (field.number == 2)
				seen |= field.value == 2 ? 2 : 0;
			if (field.number == 5)
				seen |= field.value == 976730 ? 4 : 0;
			if (field.number == 6)
				seen |= field.size == 5 && !memcmp(field.data, "Halo1", 5) ? 8 : 0;
			if (field.number == 37)
				seen |= field.value == 1 ? 16 : 0;
		}
		CHECK(seen == 31);
	}
	CHECK(steam_workshop_query_request(body, sizeof(body), 0, STEAM_WORKSHOP_SORT_NEW, "") == 0);
	memset(path, 'x', 100);
	path[100] = 0;
	CHECK(steam_workshop_query_request(body, sizeof(body), 1, STEAM_WORKSHOP_SORT_NEW, path) == 0);
	ids[0] = 1;
	ids[1] = 2;
	CHECK(steam_workshop_details_request(body, sizeof(body), ids, 2) > 0);
	CHECK(steam_workshop_details_request(body, sizeof(body), ids, 0) == 0);
	/* responses: an item read, a title's bytes made printable, a malformed one refused */
	{
		struct steam_pb_writer writer, detail;
		uint8_t item_bytes[256], tag[16];
		struct steam_workshop_item items[4];
		uint32_t total = 0;
		size_t tag_size;
		struct steam_pb_writer tag_writer;

		steam_pb_writer_init(&tag_writer, tag, sizeof(tag));
		steam_pb_put_string(&tag_writer, 1, "Halo1");
		tag_size = tag_writer.size;
		steam_pb_writer_init(&detail, item_bytes, sizeof(item_bytes));
		steam_pb_put_varint(&detail, 1, 1);
		steam_pb_put_varint(&detail, 2, 2915693904ull);
		steam_pb_put_varint(&detail, 5, 976730);
		steam_pb_put_varint(&detail, 8, 123456);
		steam_pb_put_fixed64(&detail, 14, 777);
		steam_pb_put_bytes(&detail, 16, "Ruby\xe2\x80\x93s\x01\x7f", 9);
		steam_pb_put_bytes(&detail, 52, tag, tag_size);
		steam_pb_writer_init(&writer, body, sizeof(body));
		steam_pb_put_varint(&writer, 1, 33);
		steam_pb_put_bytes(&writer, 2, item_bytes, detail.size);
		CHECK(steam_workshop_parse_query(body, writer.size, items, 4, &total) == 1 && total == 33 &&
			items[0].id == 2915693904ull && items[0].hcontent == 777 && items[0].tags == STEAM_WORKSHOP_TAG_HALO1 &&
			items[0].consumer_app == 976730 && !strcmp(items[0].title, "Ruby-s"));
		CHECK(steam_workshop_item_ok(&items[0], why, sizeof(why)));
		items[1] = items[0];
		items[1].consumer_app = 976731;
		CHECK(!steam_workshop_item_ok(&items[1], why, sizeof(why)));
		items[1] = items[0];
		items[1].banned = 1;
		CHECK(!steam_workshop_item_ok(&items[1], why, sizeof(why)));
		items[1] = items[0];
		items[1].hcontent = 0;
		CHECK(!steam_workshop_item_ok(&items[1], why, sizeof(why)));
		items[1] = items[0];
		items[1].file_size = 5ull << 30;
		CHECK(!steam_workshop_item_ok(&items[1], why, sizeof(why)));
		items[1] = items[0];
		items[1].tags = STEAM_WORKSHOP_TAG_CAMPAIGN;
		CHECK(!steam_workshop_item_ok(&items[1], why, sizeof(why)));
		/* (a details response reads field 1; a truncated one is malformed) */
		{
			uint8_t details[300];
			struct steam_pb_writer details_writer;

			steam_pb_writer_init(&details_writer, details, sizeof(details));
			steam_pb_put_bytes(&details_writer, 1, item_bytes, detail.size);
			steam_pb_put_bytes(&details_writer, 1, item_bytes, detail.size);
			CHECK(steam_workshop_parse_details(details, details_writer.size, items, 4) == 2 && items[1].hcontent == 777);
			CHECK(steam_workshop_parse_details(details, details_writer.size, items, 1) == 1);
		}
		CHECK(steam_workshop_parse_query(body, writer.size - 3, items, 4, &total) == -1);
	}
	/* the install: a staged item checked and moved; one with a stray file or a refused map removed */
	snprintf(root, sizeof(root), "%s/workshop-install", folder);
	mkdir(root, 0777);
	snprintf(path, sizeof(path), "%s/workshop", root);
	mkdir(path, 0777);
	{
		static const uint8_t map[16] = { 'd', 'a', 'e', 'h', 13 };
		uint64_t staged[8];
		int count;

		snprintf(path, sizeof(path), "%s/workshop/2900000001", root);
		mkdir(path, 0777);
		snprintf(path, sizeof(path), "%s/workshop/2900000001/maps", root);
		mkdir(path, 0777);
		snprintf(path, sizeof(path), "%s/workshop/2900000001/maps/a10.map", root);
		write_test_file(path, map, sizeof(map));
		snprintf(path, sizeof(path), "%s/workshop/2900000001/CampaignInfo.json", root);
		write_test_file(path, "{}", 2);
		snprintf(path, sizeof(path), "%s/workshop/2900000001.done", root);
		write_test_file(path, "1\n", 2);
		snprintf(path, sizeof(path), "%s/workshop/2900000002/maps", root);
		mkdir(path, 0777);
		snprintf(path, sizeof(path), "%s/workshop/2900000002", root);
		mkdir(path, 0777);
		snprintf(path, sizeof(path), "%s/workshop/2900000002/maps", root);
		mkdir(path, 0777);
		snprintf(path, sizeof(path), "%s/workshop/2900000002/maps/a10.map", root);
		write_test_file(path, map, sizeof(map));
		snprintf(path, sizeof(path), "%s/workshop/2900000002/maps/a10.map.part", root);
		write_test_file(path, map, sizeof(map));
		snprintf(path, sizeof(path), "%s/workshop/2900000002.done", root);
		write_test_file(path, "1\n", 2);
		snprintf(path, sizeof(path), "%s/workshop/0123.done", root);
		write_test_file(path, "1\n", 2);
		count = steam_workshop_staged(root, staged, 8);
		CHECK(count == 2);
		CHECK(steam_workshop_install(root, 2900000001ull, test_map_check, NULL, why, sizeof(why)));
		snprintf(path, sizeof(path), "%s/mods/2900000001/maps/a10.map", root);
		CHECK(access(path, F_OK) == 0);
		snprintf(path, sizeof(path), "%s/workshop/2900000001", root);
		CHECK(access(path, F_OK) != 0);
		CHECK(!steam_workshop_install(root, 2900000002ull, test_map_check, NULL, why, sizeof(why)) &&
			strstr(why, "a10.map.part"));
		snprintf(path, sizeof(path), "%s/workshop/2900000002", root);
		CHECK(access(path, F_OK) != 0);
		snprintf(path, sizeof(path), "%s/mods/2900000002", root);
		CHECK(access(path, F_OK) != 0);
		CHECK(steam_workshop_staged(root, staged, 8) == 0);
		snprintf(path, sizeof(path), "%s/workshop/0123.done", root);
		remove(path);
		snprintf(path, sizeof(path), "%s", root);
		CHECK(steam_workshop_remove_tree(path) && access(path, F_OK) != 0);
	}
}

/* ---------- chunks */

/* ---------- the browser (steam_image.c, steam_workshop.c's text and parsers; docs/steam_download_security.md 9) */

static int text_is(const char *raw, int lines, const char *expected)
{
	char out[256];

	steam_workshop_text(out, sizeof(out), (const uint8_t *)raw, strlen(raw), lines);
	if (strcmp(out, expected))
		fprintf(stderr, "  text: \"%s\" gave \"%s\", not \"%s\"\n", raw, out, expected);
	return !strcmp(out, expected);
}

static int url_ok(const char *url)
{
	char host[256], path[512], why[160];

	return steam_image_url(url, 256, 144, host, sizeof(host), path, sizeof(path), why, sizeof(why));
}

static int picture_ok(const char *fixtures, const char *kind, int fit, char *why, size_t why_size)
{
	char path[600];
	size_t size = 0;
	uint8_t *data, *pixels = malloc(STEAM_IMAGE_LARGE_WIDTH * STEAM_IMAGE_LARGE_HEIGHT * 4);
	int ok;

	snprintf(path, sizeof(path), "%s/pictures/%s", fixtures, kind);
	data = read_file(path, &size);
	ok = data && pixels && steam_image_decode(data, size, fit == STEAM_IMAGE_FILL ? STEAM_IMAGE_CARD_WIDTH :
		STEAM_IMAGE_LARGE_WIDTH, fit == STEAM_IMAGE_FILL ? STEAM_IMAGE_CARD_HEIGHT : STEAM_IMAGE_LARGE_HEIGHT, fit, pixels,
		why, why_size);
	/* (a picture made: opaque and not all black in its middle) */
	if (ok)
	{
		int width = fit == STEAM_IMAGE_FILL ? STEAM_IMAGE_CARD_WIDTH : STEAM_IMAGE_LARGE_WIDTH;
		int height = fit == STEAM_IMAGE_FILL ? STEAM_IMAGE_CARD_HEIGHT : STEAM_IMAGE_LARGE_HEIGHT;
		const uint8_t *middle = pixels + ((size_t)(height / 2) * (size_t)width + (size_t)(width / 2)) * 4;

		ok = middle[3] == 0xff && (middle[0] | middle[1] | middle[2]) != 0;
	}
	free(data);
	free(pixels);
	return ok;
}

static void test_browser(const char *fixtures)
{
	static const char *const good_pictures[] = { "jpeg", "progressive", "png" };
	static const char *const bad_pictures[] = { "huge", "toomany", "png16", "truncated", "gif", "html", "big" };
	char why[160], root[600], path[700], cache[700];
	uint8_t body[1024], *pixels;
	size_t size;
	int index;

	/* Steam's text made ours */
	CHECK(text_is("Plain title", 0, "Plain title"));
	CHECK(text_is("  spaced \t\t out  ", 0, "spaced out"));
	CHECK(text_is("a|nb", 0, "a/nb"));
	CHECK(text_is("\xc3\x91" "and\xc3\xba \xc2\xbfs\xc3\xad?", 0, "\xc3\x91" "and\xc3\xba \xc2\xbfs\xc3\xad?"));
	CHECK(text_is("\xe2\x80\x9cq\xe2\x80\x9d \xe2\x80\x93 x\xe2\x80\xa6", 0, "\"q\" - x..."));
	CHECK(text_is("\xe2\x80\xae" "evil\xe2\x80\xac zero\xe2\x80\x8bwidth \x07" "bell", 0, "evil zerowidth bell"));
	CHECK(text_is("emoji \xf0\x9f\x98\x80\xf0\x9f\x9a\x80 end", 0, "emoji ? end"));
	CHECK(text_is("\xe6\x97\xa5\xe6\x9c\xac", 0, "?"));
	CHECK(text_is("bad \xc3 \xff\xfe utf8 \xed\xa0\x80 \xc0\xaf", 0, "bad ? ? utf8 ? ?"));
	CHECK(text_is("[b]bold[/b] [url=https://x.test/a]link[/url] [notatag here]", 0, "bold link [notatag here]"));
	CHECK(text_is("[list][*]a[*]b[/list]after", 1, "a\nb\nafter"));
	CHECK(text_is("[list][*]a[*]b[/list]after", 0, "a b after"));
	CHECK(text_is("one\n\n\n\ntwo\r\nthree", 1, "one\n\ntwo\nthree"));
	CHECK(text_is("one\ntwo", 0, "one two"));
	CHECK(text_is("\n\nlead", 1, "lead"));
	{
		char small[7];

		/* (cut at a character's end: never half a UTF-8 sequence) */
		CHECK(steam_workshop_text(small, sizeof(small), (const uint8_t *)"abcde\xc3\x91" "f", 9, 0) == 5 &&
			!strcmp(small, "abcde"));
		CHECK(steam_workshop_text(small, 1, (const uint8_t *)"abc", 3, 0) == 0 && !small[0]);
	}
	/* the pictures' URL rule */
	CHECK(url_ok("https://images.steamusercontent.com/ugc/5115557831229079503/731D34F896397D581799E92441C11FFC211F6A2C/"));
	CHECK(url_ok("https://steamuserimages-a.akamaihd.net/ugc/1/ABC/"));
	CHECK(url_ok("https://images.steamusercontent.com/ugc/1/ABC/?imw=5000&x=1"));
	{
		static const char *const refused[] = {
			"http://images.steamusercontent.com/ugc/1/ABC/",
			"https://images.steamusercontent.com:8443/ugc/1/ABC/",
			"https://u@images.steamusercontent.com/ugc/1/ABC/",
			"https://images.steamusercontent.com.evil.test/ugc/1/ABC/",
			"https://evilimages.steamusercontent.com/ugc/1/ABC/",
			"https://IMAGES.steamusercontent.com/ugc/1/ABC/",
			"https://images.steamusercontent.com/ugc/../../etc/passwd",
			"https://images.steamusercontent.com/ugc/1//ABC/",
			"https://images.steamusercontent.com/other/1/ABC/",
			"https://images.steamusercontent.com/ugc/1/A%2e%2e/",
			"https://images.steamusercontent.com/ugc/1/A B/",
			"https://images.steamusercontent.com",
			"https://evil.example.com/ugc/1/ABC/",
			"file:///etc/passwd",
			"",
		};
		for (index = 0; index < (int)(sizeof(refused) / sizeof(refused[0])); index++)
		{
			if (url_ok(refused[index]))
				fprintf(stderr, "  url taken: %s\n", refused[index]);
			CHECK(!url_ok(refused[index]));
		}
		memset(path, 'a', sizeof(path));
		memcpy(path, "https://images.steamusercontent.com/ugc/", 40);
		path[400] = 0;
		CHECK(!url_ok(path));
	}
	{
		char host[256], asked[512];

		CHECK(steam_image_url("https://images.steamusercontent.com/ugc/1/ABC/?evil=1", 512, 288, host, sizeof(host), asked,
			sizeof(asked), why, sizeof(why)) && !strcmp(host, "images.steamusercontent.com") &&
			!strncmp(asked, "/ugc/1/ABC/?imw=512&imh=288&", 28) && !strstr(asked, "evil"));
	}
	/* the decode: a JPEG (baseline, progressive) and a PNG made; the hostile ones refused */
	for (index = 0; index < (int)(sizeof(good_pictures) / sizeof(good_pictures[0])); index++)
	{
		CHECK(picture_ok(fixtures, good_pictures[index], STEAM_IMAGE_FILL, why, sizeof(why)));
		CHECK(picture_ok(fixtures, good_pictures[index], STEAM_IMAGE_FIT, why, sizeof(why)));
	}
	for (index = 0; index < (int)(sizeof(bad_pictures) / sizeof(bad_pictures[0])); index++)
	{
		int ok = picture_ok(fixtures, bad_pictures[index], STEAM_IMAGE_FILL, why, sizeof(why));

		fprintf(stderr, "  picture %s: %s\n", bad_pictures[index], ok ? "MADE" : why);
		CHECK(!ok);
	}
	CHECK(steam_image_decode_peak() <= STEAM_IMAGE_DECODE_BYTES_MAXIMUM);
	/* the cache: kept, read back only at its size, trimmed to its caps (the oldest first), other names left */
	pixels = malloc(STEAM_IMAGE_CARD_WIDTH * STEAM_IMAGE_CARD_HEIGHT * 4);
	snprintf(root, sizeof(root), "%s/browser-cache", fixtures);
	mkdir(root, 0777);
	memset(pixels, 0x5a, STEAM_IMAGE_CARD_WIDTH * STEAM_IMAGE_CARD_HEIGHT * 4);
	CHECK(steam_image_cache_path(root, "https://images.steamusercontent.com/ugc/1/A/", 256, 128, cache, sizeof(cache)));
	CHECK(steam_image_cache_save(root, cache, 256, 128, pixels));
	memset(pixels, 0, STEAM_IMAGE_CARD_WIDTH * STEAM_IMAGE_CARD_HEIGHT * 4);
	CHECK(steam_image_cache_load(cache, 256, 128, pixels) && pixels[1000] == 0x5a);
	CHECK(!steam_image_cache_load(cache, 128, 128, pixels));
	snprintf(path, sizeof(path), "%s/workshop/previews/keep-me.txt", root);
	write_test_file(path, "x", 1);
	for (index = 0; index < 6; index++)
	{
		char url[128];

		snprintf(url, sizeof(url), "https://images.steamusercontent.com/ugc/%d/A/", index + 2);
		CHECK(steam_image_cache_path(root, url, 256, 128, cache, sizeof(cache)));
		CHECK(steam_image_cache_save(root, cache, 256, 128, pixels));
	}
	CHECK(steam_image_cache_trim(root, 3u * (12 + 256 * 128 * 4), 100) == 4);
	CHECK(access(path, F_OK) == 0);
	{
		/* (a cache file cut short is not taken) */
		FILE *file = fopen(cache, "r+b");

		if (file)
		{
			CHECK(ftruncate(fileno(file), 100) == 0);
			fclose(file);
		}
		CHECK(!steam_image_cache_load(cache, 256, 128, pixels));
	}
	free(pixels);
	/* the requests */
	size = steam_workshop_subscribed_request(body, sizeof(body), 76561198000000000ull, 2);
	CHECK(size > 0 && memmem(body, size, "mysubscriptions", 15) && memmem(body, size, "Halo1", 5));
	CHECK(steam_workshop_subscribed_request(body, sizeof(body), 0, 1) == 0);
	{
		uint64_t ids[2] = { 76561198000000001ull, 76561198000000002ull };

		CHECK(steam_workshop_names_request(body, sizeof(body), ids, 2) > 0);
		CHECK(steam_workshop_names_request(body, sizeof(body), ids, 0) == 0);
	}
	CHECK(steam_workshop_page_request(body, sizeof(body), 2900000001ull) > 0);
	/* a details page, the names, a subscribed list: built here, read back */
	{
		struct steam_pb_writer writer, item, votes, preview;
		uint8_t item_bytes[2048], votes_bytes[64], preview_bytes[512], reply[4096];
		struct steam_workshop_details *details = calloc(1, sizeof(*details));
		struct steam_workshop_item items[2];
		char names[3][STEAM_WORKSHOP_AUTHOR_SIZE];
		uint64_t steamids[3] = { 11, 22, 33 };
		uint32_t total = 0;
		float score = 0.75f;
		uint32_t bits;

		memcpy(&bits, &score, sizeof(bits));
		steam_pb_writer_init(&votes, votes_bytes, sizeof(votes_bytes));
		steam_pb_put_fixed32(&votes, 1, bits);
		steam_pb_put_varint(&votes, 2, 30);
		steam_pb_put_varint(&votes, 3, 10);
		steam_pb_writer_init(&item, item_bytes, sizeof(item_bytes));
		steam_pb_put_varint(&item, 1, 1);
		steam_pb_put_varint(&item, 2, 2900000001ull);
		steam_pb_put_fixed64(&item, 3, 11);
		steam_pb_put_varint(&item, 5, STEAM_WORKSHOP_APP);
		steam_pb_put_string(&item, 11, "https://images.steamusercontent.com/ugc/1/A/");
		steam_pb_put_string(&item, 16, "T|nitle");
		steam_pb_put_string(&item, 17, "[b]Hi[/b]\n\n\n\nthere");
		steam_pb_put_bytes(&item, 55, votes_bytes, votes.size);
		/* (pictures: a video's entry left out, a URL repeated once, the preview first) */
		steam_pb_writer_init(&preview, preview_bytes, sizeof(preview_bytes));
		steam_pb_put_string(&preview, 3, "https://images.steamusercontent.com/ugc/1/B/");
		steam_pb_put_varint(&preview, 7, 0);
		steam_pb_put_bytes(&item, 51, preview_bytes, preview.size);
		steam_pb_put_bytes(&item, 51, preview_bytes, preview.size);
		steam_pb_writer_init(&preview, preview_bytes, sizeof(preview_bytes));
		steam_pb_put_string(&preview, 3, "https://www.youtube.com/watch?v=x");
		steam_pb_put_varint(&preview, 7, 1);
		steam_pb_put_bytes(&item, 51, preview_bytes, preview.size);
		steam_pb_writer_init(&writer, reply, sizeof(reply));
		steam_pb_put_bytes(&writer, 1, item_bytes, item.size);
		CHECK(details && steam_workshop_parse_page(reply, writer.size, 2900000001ull, details) == 1);
		if (details)
		{
			CHECK(details->item.score == 75 && details->item.votes_up == 30 && details->item.votes_down == 10 &&
				details->item.creator == 11 && !strcmp(details->item.title, "T/nitle"));
			CHECK(!strcmp(details->description, "Hi\n\nthere"));
			CHECK(details->preview_count == 2 && !strcmp(details->previews[0], "https://images.steamusercontent.com/ugc/1/A/") &&
				!strcmp(details->previews[1], "https://images.steamusercontent.com/ugc/1/B/"));
			CHECK(steam_workshop_parse_page(reply, writer.size, 5, details) == 0);
			CHECK(steam_workshop_parse_page(reply, writer.size - 2, 2900000001ull, details) == -1);
		}
		free(details);
		/* GetUserFiles: items in field 3 */
		steam_pb_writer_init(&writer, reply, sizeof(reply));
		steam_pb_put_varint(&writer, 1, 3);
		steam_pb_put_varint(&writer, 2, 0);
		steam_pb_put_bytes(&writer, 3, item_bytes, item.size);
		CHECK(steam_workshop_parse_subscribed(reply, writer.size, items, 2, &total) == 1 && total == 3 &&
			items[0].id == 2900000001ull);
		/* names: a public name read through the text rule; an account not asked for ignored */
		steam_pb_writer_init(&writer, reply, sizeof(reply));
		{
			struct steam_pb_writer account, details_writer;
			uint8_t account_bytes[256], details_bytes[300];
			int which;

			for (which = 0; which < 2; which++)
			{
				steam_pb_writer_init(&account, account_bytes, sizeof(account_bytes));
				steam_pb_put_fixed64(&account, 1, which ? 99 : 22);
				steam_pb_put_string(&account, 10, which ? "Not asked" : "Ev|nil\xe2\x80\xae");
				steam_pb_writer_init(&details_writer, details_bytes, sizeof(details_bytes));
				steam_pb_put_bytes(&details_writer, 1, account_bytes, account.size);
				steam_pb_put_bytes(&writer, 1, details_bytes, details_writer.size);
			}
		}
		CHECK(steam_workshop_parse_names(reply, writer.size, steamids, 3, names) == 1 && !names[0][0] &&
			!strcmp(names[1], "Ev/nil") && !names[2][0]);
		CHECK(steam_workshop_parse_names(reply, writer.size - 1, steamids, 3, names) == -1);
	}
	/* installed, its version, removed */
	{
		uint64_t gid = 0;

		snprintf(root, sizeof(root), "%s/browser-installed", fixtures);
		mkdir(root, 0777);
		snprintf(path, sizeof(path), "%s/mods", root);
		mkdir(path, 0777);
		CHECK(!steam_workshop_installed(root, 2900000001ull, &gid));
		snprintf(path, sizeof(path), "%s/mods/2900000001", root);
		mkdir(path, 0777);
		snprintf(path, sizeof(path), "%s/workshop", root);
		mkdir(path, 0777);
		snprintf(path, sizeof(path), "%s/workshop/installed", root);
		mkdir(path, 0777);
		snprintf(path, sizeof(path), "%s/workshop/installed/2900000001.gid", root);
		write_test_file(path, "777\n", 4);
		CHECK(steam_workshop_installed(root, 2900000001ull, &gid) && gid == 777);
		CHECK(steam_workshop_uninstall(root, 2900000001ull) && !steam_workshop_installed(root, 2900000001ull, &gid) &&
			access(path, F_OK) != 0);
		CHECK(!steam_workshop_uninstall(root, 1234567890123ull));
	}
}

static void test_chunks(const char *fixtures)
{
	char path[600], line[512];
	FILE *list;
	struct steam_chunk_work work;
	int seen = 0;

	snprintf(path, sizeof(path), "%s/fixtures.txt", fixtures);
	list = fopen(path, "r");
	CHECK(list != NULL);
	if (!list)
		return;
	steam_chunk_work_init(&work);
	while (fgets(line, sizeof(line), list))
	{
		char name[64], sha[41], key_hex[65], error[128];
		unsigned int checksum, size, compressed;
		struct steam_chunk_info info;
		uint8_t key[32], *data, *output;
		size_t data_size;

		if (sscanf(line, "%63s %40s %x %u %u %64s", name, sha, &checksum, &size, &compressed, key_hex) != 6)
			continue;
		memset(&info, 0, sizeof(info));
		steam_unhex(sha, 40, info.sha, 20);
		steam_unhex(key_hex, 64, key, 32);
		info.checksum = checksum;
		info.size = size;
		info.compressed_size = compressed;
		snprintf(path, sizeof(path), "%s/%s.bin", fixtures, name);
		data = read_file(path, &data_size);
		output = malloc(size);
		CHECK(data && output);
		if (!data || !output)
			continue;
		CHECK(steam_chunk_process(&work, key, &info, data, data_size, output, error, sizeof(error)));
		if (error[0])
			fprintf(stderr, "  %s: %s\n", name, error);
		/* each byte region damaged: refused, never handed back as good */
		{
			size_t at;

			for (at = 0; at < data_size; at += data_size / 7 + 1)
			{
				data[at] ^= 0x41;
				CHECK(!steam_chunk_process(&work, key, &info, data, data_size, output, error, sizeof(error)));
				data[at] ^= 0x41;
			}
		}
		/* the wrong key, the wrong size, the wrong SHA-1 */
		key[0] ^= 1;
		CHECK(!steam_chunk_process(&work, key, &info, data, data_size, output, error, sizeof(error)));
		key[0] ^= 1;
		info.size--;
		CHECK(!steam_chunk_process(&work, key, &info, data, data_size, output, error, sizeof(error)));
		info.size++;
		info.sha[3] ^= 1;
		CHECK(!steam_chunk_process(&work, key, &info, data, data_size, output, error, sizeof(error)));
		CHECK(!steam_chunk_process(&work, key, &info, data, data_size - 16, output, error, sizeof(error)));
		free(data);
		free(output);
		seen++;
	}
	fclose(list);
	steam_chunk_work_free(&work);
	CHECK(seen == 6);
}

/* ---------- a manifest with encrypted names */

static int hostile_written, files_seen, names_ok;

static int manifest_visit(void *context, const struct steam_manifest_file *file)
{
	(void)context;
	files_seen++;
	names_ok += file->name_hash_checked && file->name_hash_ok;
	if (steam_plan_match(STEAM_GAMES_ALL, STEAM_DEPOT_HALO1_CAMPAIGN, file->name) && strstr(file->name, ".."))
		hostile_written++;
	return 0;
}

static void test_manifest(const char *fixtures)
{
	char path[600], key_hex[80], error[160];
	uint8_t key[32], *zip, *manifest;
	size_t size = 0, entry = 0;
	struct steam_manifest_metadata metadata;
	unsigned int expected_files = 0;
	unsigned long long gid = 0;
	FILE *file;

	snprintf(path, sizeof(path), "%s/manifest.key", fixtures);
	file = fopen(path, "r");
	CHECK(file && fscanf(file, "%64s %u %llu", key_hex, &expected_files, &gid) == 3);
	if (file)
		fclose(file);
	steam_unhex(key_hex, 64, key, 32);
	snprintf(path, sizeof(path), "%s/manifest.zip", fixtures);
	zip = read_file(path, &size);
	CHECK(zip && steam_zip_entry_size(zip, size, 64u << 20, &entry));
	manifest = malloc(entry + 1);
	CHECK(steam_zip_extract(zip, size, manifest, entry, error, sizeof(error)));
	CHECK(steam_manifest_parse(manifest, entry, key, &metadata, manifest_visit, NULL, error, sizeof(error)) == 0);
	CHECK(metadata.filenames_encrypted && metadata.gid == gid && files_seen == (int)expected_files &&
		names_ok == files_seen && hostile_written == 0);
	CHECK(steam_manifest_crc_ok(manifest, entry, &metadata));
	/* no key: refused; the wrong key: refused */
	CHECK(steam_manifest_parse(manifest, entry, NULL, &metadata, NULL, NULL, error, sizeof(error)) != 0);
	key[5] ^= 1;
	CHECK(steam_manifest_parse(manifest, entry, key, &metadata, NULL, NULL, error, sizeof(error)) != 0);
	/* every truncation: refused or parsed, never a crash */
	{
		size_t cut;

		for (cut = 0; cut < entry; cut += entry / 97 + 1)
			steam_manifest_parse(manifest, cut, NULL, &metadata, NULL, NULL, error, sizeof(error));
	}
	free(zip);
	free(manifest);
}

/* ---------- the file writer and journal */

/* ---------- connections: plain only to a loopback address */

static void test_net(void)
{
	struct steam_stream stream;
	char host[300], path[300], error[200];

	CHECK(!steam_net_open(&stream, "ws://10.1.2.3/cmsocket/", host, sizeof(host), path, sizeof(path), error,
		sizeof(error)) && strstr(error, "loopback"));
	CHECK(!steam_net_open(&stream, "http://cm.example.com/x", host, sizeof(host), path, sizeof(path), error,
		sizeof(error)) && strstr(error, "loopback"));
	CHECK(!steam_net_open(&stream, "ftp://127.0.0.1/x", host, sizeof(host), path, sizeof(path), error, sizeof(error)));
	CHECK(!steam_net_open(&stream, "wss://bad host/x", host, sizeof(host), path, sizeof(path), error, sizeof(error)));
}

static void test_file(const char *folder)
{
	uint8_t data[3 * 1000];
	struct steam_chunk_info chunks[3];
	struct steam_manifest_file file;
	struct steam_file_job job;
	char path[600], error[200];
	unsigned int index;

	memset(&file, 0, sizeof(file));
	memcpy(data, "daeh\x0d\0\0\0", 8);
	for (index = 8; index < sizeof(data); index++)
		data[index] = (uint8_t)(index * 7);
	for (index = 0; index < 3; index++)
	{
		memset(&chunks[index], 0, sizeof(chunks[index]));
		chunks[index].offset = index * 1000;
		chunks[index].size = 1000;
		steam_sha1(data + index * 1000, 1000, chunks[index].sha);
	}
	snprintf(file.name, sizeof(file.name), "halo1/maps/a10.map");
	file.size = sizeof(data);
	file.chunk_count = 3;
	file.chunks = chunks;
	file.covered = 1;
	file.has_sha_content = 1;
	steam_sha1(data, sizeof(data), file.sha_content);
	snprintf(path, sizeof(path), "%s/file-test.map", folder);
	remove(path);
	CHECK(steam_file_open(&job, path, 1, 2, &file, STEAM_KIND_MCC_MAP, "x.map", error, sizeof(error)) == STEAM_FILE_STARTED);
	/* the last chunk first, then stop */
	CHECK(steam_file_write(&job, 2, data + 2000, error, sizeof(error)));
	CHECK(steam_file_write(&job, 0, data, error, sizeof(error)));
	steam_file_close(&job);
	/* damage chunk 0 in the .part: the resume takes chunk 2 alone */
	{
		char part[620];
		FILE *damaged;

		snprintf(part, sizeof(part), "%s.part", path);
		damaged = fopen(part, "r+b");
		CHECK(damaged != NULL);
		if (damaged)
		{
			fseek(damaged, 10, SEEK_SET);
			fputc('X', damaged);
			fclose(damaged);
		}
	}
	CHECK(steam_file_open(&job, path, 1, 2, &file, STEAM_KIND_MCC_MAP, "x.map", error, sizeof(error)) == STEAM_FILE_STARTED);
	CHECK(job.done_count == 1 && job.done[2] && !job.done[0]);
	CHECK(!steam_file_finish(&job, error, sizeof(error)) || 0);
	steam_file_close(&job);
	CHECK(steam_file_open(&job, path, 1, 2, &file, STEAM_KIND_MCC_MAP, "x.map", error, sizeof(error)) == STEAM_FILE_STARTED);
	CHECK(steam_file_write(&job, 0, data, error, sizeof(error)));
	CHECK(steam_file_write(&job, 1, data + 1000, error, sizeof(error)));
	CHECK(steam_file_finish(&job, error, sizeof(error)));
	{
		size_t size;
		uint8_t *written = read_file(path, &size);

		CHECK(written && size == sizeof(data) && !memcmp(written, data, size));
		free(written);
	}
	/* there and whole: already */
	CHECK(steam_file_open(&job, path, 1, 2, &file, STEAM_KIND_MCC_MAP, "x.map", error, sizeof(error)) == STEAM_FILE_ALREADY);
	/* a journal for another gid: ignored (nothing taken from it) */
	{
		uint8_t marks[3] = { 0 };
		static const char other[] = "steamdl-journal 1\ndepot 1\ngid 3\nsize 3000\nchunks 3\n0 0000\n";

		CHECK(steam_file_journal_parse(other, sizeof(other) - 1, 1, 2, 3000, 3, chunks, marks) == -1);
	}
	remove(path);
}

/* ---------- the flow against the fake server */

static FILE *log_file;
static int cancel_after = -1, progress_calls;

static void log_line(void *context, const char *line)
{
	(void)context;
	fprintf(log_file, "%s\n", line);
	fflush(log_file);
}

static int show_qr(void *context, const char *url)
{
	(void)context;
	fprintf(log_file, "QR: %s\n", url);
	return 0;
}

static int progress(void *context, uint64_t done, uint64_t total, const char *file)
{
	(void)context;
	(void)done;
	(void)total;
	(void)file;
	return cancel_after >= 0 && ++progress_calls > cancel_after;
}

static int64_t free_space(void *context, const char *root)
{
	(void)context;
	(void)root;
	return 64ll << 30;
}

static int flow(int argc, char **argv)
{
	struct steam_download_options options;
	struct steam_download_callbacks callbacks;
	struct steam_download_result result;
	char cm[128], cdn[128], log_path[600], marker[600], ce_root[600], app_root[600];
	const char *cdn_list[1];

	if (argc < 6)
		return 2;
	/* (STEAM_TEST_TLS=1: wss and https to localhost, the test's own authority built in) */
	if (getenv("STEAM_TEST_TLS"))
	{
		snprintf(cm, sizeof(cm), "wss://localhost:%s/cmsocket/", argv[2]);
		snprintf(cdn, sizeof(cdn), "https://localhost:%s", argv[3]);
	}
	else
	{
		snprintf(cm, sizeof(cm), "ws://127.0.0.1:%s/cmsocket/", argv[2]);
		snprintf(cdn, sizeof(cdn), "http://127.0.0.1:%s", argv[3]);
	}
	snprintf(log_path, sizeof(log_path), "%s/log.txt", argv[4]);
	snprintf(marker, sizeof(marker), "%s/steam-session.txt", argv[4]);
	snprintf(ce_root, sizeof(ce_root), "%s/card/haloce-vita", argv[4]);
	snprintf(app_root, sizeof(app_root), "%s/card/haloce-vita", argv[4]);
	if (argc > 6)
		cancel_after = atoi(argv[6]);
	log_file = fopen(log_path, "a");
	if (!log_file)
		return 2;
	cdn_list[0] = cdn;
	memset(&options, 0, sizeof(options));
	options.games = (uint32_t)strtoul(argv[5], NULL, 0);
	options.cm_url = cm;
	options.cdn_override = cdn_list;
	options.cdn_override_count = 1;
	options.ce_root = ce_root;
	options.app_root = app_root;
	options.marker_path = marker;
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.show_qr = show_qr;
	callbacks.progress = progress;
	callbacks.log = log_line;
	callbacks.free_space = free_space;
	steam_download_run(&options, &callbacks, &result);
	printf("result ok=%d cancelled=%d files=%u already=%u downloaded=%llu written=%llu retried=%u revoked=%d "
		"logged_off=%d error=%s\n", result.ok, result.cancelled, result.files_done, result.files_already,
		(unsigned long long)result.bytes_downloaded, (unsigned long long)result.bytes_written, result.chunks_retried,
		result.revoked, result.logged_off, result.error[0] ? result.error : "-");
	fclose(log_file);
	return 0;
}

/* ---------- the Workshop against the fake server */

static uint64_t chosen_ids[STEAM_WORKSHOP_CHOSEN_MAXIMUM];
static int chosen_count;

static int workshop_choose(void *context, struct steam_session *session, uint64_t *ids, int maximum)
{
	struct steam_workshop_item items[STEAM_WORKSHOP_PAGE_ITEMS];
	uint32_t total = 0;
	char error[256];
	int count, index;

	(void)context;
	/* (the player's pages: the first, the second, a search) */
	count = steam_download_workshop_query(session, 1, STEAM_WORKSHOP_SORT_TREND, "", items, STEAM_WORKSHOP_PAGE_ITEMS,
		&total, error, sizeof(error));
	fprintf(log_file, "workshop list: page 1: %d of %u%s%s\n", count, (unsigned int)total, count < 0 ? ": " : "",
		count < 0 ? error : "");
	if (count < 0)
		return 0;
	for (index = 0; index < count; index++)
		fprintf(log_file, "workshop list: %llu tags %d %s\n", (unsigned long long)items[index].id, items[index].tags,
			items[index].title);
	count = steam_download_workshop_query(session, 2, STEAM_WORKSHOP_SORT_TREND, "", items, STEAM_WORKSHOP_PAGE_ITEMS,
		&total, error, sizeof(error));
	fprintf(log_file, "workshop list: page 2: %d\n", count);
	count = steam_download_workshop_query(session, 1, STEAM_WORKSHOP_SORT_TREND, "rebalanced", items,
		STEAM_WORKSHOP_PAGE_ITEMS, &total, error, sizeof(error));
	fprintf(log_file, "workshop list: search: %d (%s)\n", count, count > 0 ? items[0].title : "-");
	steam_session_pump(session, 50);
	for (index = 0; index < chosen_count && index < maximum; index++)
		ids[index] = chosen_ids[index];
	return index;
}

static void item_end(void *context, const struct steam_workshop_item *item, int ok, const char *why)
{
	(void)context;
	fprintf(log_file, "workshop item %llu: %s%s\n", (unsigned long long)item->id, ok ? "downloaded" : "failed: ",
		ok ? "" : why);
}

static int workshop(int argc, char **argv)
{
	struct steam_download_options options;
	struct steam_download_callbacks callbacks;
	struct steam_download_result result;
	char cm[128], cdn[128], log_path[600], marker[600], ce_root[600], why[256];
	const char *cdn_list[1];
	char *list, *part;
	uint64_t staged[16];
	int count, index, installed = 0, refused = 0;

	if (argc < 6)
		return 2;
	snprintf(cm, sizeof(cm), "ws://127.0.0.1:%s/cmsocket/", argv[2]);
	snprintf(cdn, sizeof(cdn), "http://127.0.0.1:%s", argv[3]);
	snprintf(log_path, sizeof(log_path), "%s/log.txt", argv[4]);
	snprintf(marker, sizeof(marker), "%s/steam-session.txt", argv[4]);
	snprintf(ce_root, sizeof(ce_root), "%s/card/haloce-vita", argv[4]);
	if (argc > 6)
		cancel_after = atoi(argv[6]);
	list = strdup(argv[5]);
	for (part = strtok(list, ","); part && chosen_count < STEAM_WORKSHOP_CHOSEN_MAXIMUM; part = strtok(NULL, ","))
		chosen_ids[chosen_count++] = strtoull(part, NULL, 10);
	free(list);
	log_file = fopen(log_path, "a");
	if (!log_file)
		return 2;
	cdn_list[0] = cdn;
	memset(&options, 0, sizeof(options));
	options.workshop = 1;
	options.cm_url = cm;
	options.cdn_override = cdn_list;
	options.cdn_override_count = 1;
	options.ce_root = ce_root;
	options.app_root = ce_root;
	options.marker_path = marker;
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.show_qr = show_qr;
	callbacks.progress = progress;
	callbacks.log = log_line;
	callbacks.free_space = free_space;
	callbacks.workshop_choose = workshop_choose;
	callbacks.item_end = item_end;
	steam_download_run(&options, &callbacks, &result);
	/* (the game's part, after the session: each staged item checked and moved into the mods folder) */
	count = steam_workshop_staged(ce_root, staged, 16);
	for (index = 0; index < count; index++)
	{
		if (steam_workshop_install(ce_root, staged[index], test_map_check, NULL, why, sizeof(why)))
		{
			installed++;
			fprintf(log_file, "workshop install %llu: installed\n", (unsigned long long)staged[index]);
		}
		else
		{
			refused++;
			fprintf(log_file, "workshop install %llu: refused: %s\n", (unsigned long long)staged[index], why);
		}
	}
	printf("result ok=%d cancelled=%d items=%u failed=%u installed=%d refused=%d files=%u revoked=%d logged_off=%d "
		"error=%s\n", result.ok, result.cancelled, result.items_done, result.items_failed, installed, refused,
		result.files_done, result.revoked, result.logged_off, result.error[0] ? result.error : "-");
	fclose(log_file);
	return 0;
}

/* ---------- the job (steam_job.c) against the fake server */

static void job_log(const char *line)
{
	fprintf(log_file, "%s\n", line);
	fflush(log_file);
}

static int job(int argc, char **argv)
{
	struct steam_job_options options;
	struct steam_job_platform hooks;
	struct steam_job_status status;
	char log_path[600];
	unsigned int qr_serial = 0;
	int qr_size = 0, phases = 0, last_phase = -1, polls = 0, cancel_at = argc > 6 ? atoi(argv[6]) : -1;

	if (argc < 6)
		return 2;
	snprintf(log_path, sizeof(log_path), "%s/log.txt", argv[4]);
	log_file = fopen(log_path, "a");
	if (!log_file)
		return 2;
	memset(&hooks, 0, sizeof(hooks));
	hooks.log = job_log;
	steam_job_set_platform(&hooks);
	memset(&options, 0, sizeof(options));
	options.games = atoi(argv[5]);
	if (getenv("STEAM_TEST_TLS"))
	{
		snprintf(options.cm_url, sizeof(options.cm_url), "wss://localhost:%s/cmsocket/", argv[2]);
		snprintf(options.cdn_override, sizeof(options.cdn_override), "https://localhost:%s", argv[3]);
	}
	else
	{
		snprintf(options.cm_url, sizeof(options.cm_url), "ws://127.0.0.1:%s/cmsocket/", argv[2]);
		snprintf(options.cdn_override, sizeof(options.cdn_override), "http://127.0.0.1:%s", argv[3]);
	}
	snprintf(options.ce_root, sizeof(options.ce_root), "%s/card/haloce-vita", argv[4]);
	snprintf(options.app_root, sizeof(options.app_root), "%s/card/haloce-vita", argv[4]);
	snprintf(options.marker_path, sizeof(options.marker_path), "%s/steam-session.txt", argv[4]);
	if (!steam_job_start(&options) || steam_job_start(&options))
	{
		printf("result job did not start (or started twice)\n");
		return 1;
	}
	for (;;)
	{
		struct timespec step = { 0, 20 * 1000 * 1000 };

		steam_job_get(&status);
		if (status.phase != last_phase)
		{
			phases++;
			last_phase = status.phase;
			fprintf(log_file, "phase %d\n", status.phase);
		}
		if (status.qr_size)
			qr_size = status.qr_size;
		if (status.qr_serial > qr_serial)
			qr_serial = status.qr_serial;
		if (status.phase == STEAM_JOB_DOWNLOADING && cancel_at >= 0 && ++polls > cancel_at)
		{
			steam_job_cancel();
			cancel_at = -1;
		}
		if (status.phase == STEAM_JOB_DONE || status.phase == STEAM_JOB_FAILED || status.phase == STEAM_JOB_CANCELLED)
			break;
		nanosleep(&step, NULL);
	}
	printf("result ok=%d cancelled=%d files=%u already=%u qr=%d serials=%u attempts=%d revoked=%d logged_off=%d "
		"session_lost=%d waits=%u phases=%d error=%s\n", status.phase == STEAM_JOB_DONE,
		status.phase == STEAM_JOB_CANCELLED, status.files_done, status.files_already, qr_size, qr_serial, status.attempts,
		status.revoked, status.logged_off, status.session_lost, status.network_waits, phases,
		status.error[0] ? status.error : "-");
	CHECK(steam_job_forget());
	fclose(log_file);
	return 0;
}

/* ---------- BROWSE: the browser's job against the fake */

static int browse_wait(int (*ready)(void), int seconds)
{
	int step;

	for (step = 0; step < seconds * 50; step++)
	{
		struct timespec wait = { 0, 20 * 1000 * 1000 };

		if (ready())
			return 1;
		nanosleep(&wait, NULL);
	}
	return 0;
}

static int browse_page_here(void)
{
	struct steam_job_workshop_view ws;

	steam_job_workshop_view(&ws);
	return ws.phase == STEAM_JOB_BROWSING && !ws.loading && ws.serial > 0;
}

static int browse_pictures_here(void)
{
	struct steam_job_workshop_view ws;
	int index;

	steam_job_workshop_view(&ws);
	for (index = 0; index < ws.count; index++)
		if (!ws.rows[index].picture)
			return 0;
	return !ws.loading;
}

static int browse_details_here(void)
{
	struct steam_job_workshop_details details;

	steam_job_workshop_details(&details);
	return details.state == -1 || (details.state == 2 && details.contents && details.picture);
}

static int browse_ended(void)
{
	struct steam_job_status status;

	steam_job_get(&status);
	return status.phase == STEAM_JOB_DONE || status.phase == STEAM_JOB_FAILED || status.phase == STEAM_JOB_CANCELLED;
}

static void browse_print_page(const char *name, int *made, int *refused, unsigned int *most_held)
{
	struct steam_job_workshop_view ws;
	unsigned int held = 0, peak = 0;
	int index;

	steam_job_workshop_view(&ws);
	steam_job_workshop_memory(&held, &peak);
	if (held > *most_held)
		*most_held = held;
	fprintf(log_file, "browse %s: page %d list %d: %d of %d%s%s\n", name, ws.page, ws.sort, ws.count, ws.total,
		ws.error[0] ? ": " : "", ws.error);
	for (index = 0; index < ws.count; index++)
	{
		const struct steam_job_workshop_row *row = &ws.rows[index];
		unsigned char *pixels = malloc(STEAM_IMAGE_CARD_WIDTH * STEAM_IMAGE_CARD_HEIGHT * 4);
		int width = 0, height = 0;
		unsigned int serial = steam_job_workshop_picture(index, pixels, STEAM_IMAGE_CARD_WIDTH * STEAM_IMAGE_CARD_HEIGHT * 4,
			&width, &height);

		fprintf(log_file, "browse row %s \"%s\" by \"%s\" score %d votes %u picture %d serial %u %dx%d\n", row->id,
			row->title, row->author, row->score, row->votes, row->picture, serial, width, height);
		CHECK(row->picture != 1 || (serial && width == STEAM_IMAGE_CARD_WIDTH && height == STEAM_IMAGE_CARD_HEIGHT));
		CHECK(!strchr(row->title, '|') && !strchr(row->author, '|'));
		if (row->picture == 1)
			(*made)++;
		else if (row->picture == -1)
			(*refused)++;
		free(pixels);
	}
	fprintf(log_file, "browse memory: %u bytes of pictures, decoder peak %u\n", held, peak);
	fflush(log_file);
}

static int browse(int argc, char **argv)
{
	struct steam_job_options options;
	struct steam_job_platform hooks;
	struct steam_job_status status;
	struct steam_job_workshop_details details;
	char log_path[600];
	int made = 0, refused = 0, pages = 0, subscribed = 0, details_ok = 0, maps = 0, details_pictures = 0;
	unsigned int most_held = 0;

	if (argc < 5)
		return 2;
	snprintf(log_path, sizeof(log_path), "%s/log.txt", argv[4]);
	log_file = fopen(log_path, "a");
	if (!log_file)
		return 2;
	memset(&hooks, 0, sizeof(hooks));
	hooks.log = job_log;
	steam_job_set_platform(&hooks);
	memset(&options, 0, sizeof(options));
	options.workshop = 1;
	snprintf(options.cm_url, sizeof(options.cm_url), "ws://127.0.0.1:%s/cmsocket/", argv[2]);
	snprintf(options.cdn_override, sizeof(options.cdn_override), "http://127.0.0.1:%s", argv[3]);
	snprintf(options.images_override, sizeof(options.images_override), "http://127.0.0.1:%s", argv[3]);
	snprintf(options.ce_root, sizeof(options.ce_root), "%s/card/haloce-vita", argv[4]);
	snprintf(options.app_root, sizeof(options.app_root), "%s/card/haloce-vita", argv[4]);
	snprintf(options.marker_path, sizeof(options.marker_path), "%s/steam-session.txt", argv[4]);
	snprintf(log_path, sizeof(log_path), "%s/card", argv[4]);
	mkdir(log_path, 0777);
	mkdir(options.ce_root, 0777);
	if (!steam_job_start(&options))
	{
		printf("result the job did not start\n");
		return 1;
	}
	if (browse_wait(browse_page_here, 60))
	{
		int page, list;

		/* POPULAR's pages, then NEWEST, TOP RATED, SUBSCRIBED: each page's pictures waited for */
		for (list = 0; list <= STEAM_WORKSHOP_LIST_SUBSCRIBED; list++)
		{
			if (list == STEAM_WORKSHOP_SORT_UPDATED)
				continue;
			for (page = 1; page <= (list == 0 ? 3 : 1); page++)
			{
				char name[32];

				if (list || page > 1)
				{
					steam_job_workshop_ask(page, list, "");
					browse_wait(browse_page_here, 30);
				}
				browse_wait(browse_pictures_here, 60);
				snprintf(name, sizeof(name), "list%d-page%d", list, page);
				browse_print_page(name, &made, &refused, &most_held);
				pages++;
				if (list == STEAM_WORKSHOP_LIST_SUBSCRIBED)
				{
					struct steam_job_workshop_view ws;

					steam_job_workshop_view(&ws);
					subscribed = ws.count;
				}
			}
		}
		/* a details page: its pictures one after another (the video's entry and the other host's not among them),
		what its files are */
		steam_job_workshop_details_ask("2900000001");
		if (browse_wait(browse_details_here, 60))
		{
			unsigned char *pixels = malloc(STEAM_IMAGE_LARGE_WIDTH * STEAM_IMAGE_LARGE_HEIGHT * 4);
			int index;

			steam_job_workshop_details(&details);
			details_ok = details.state == 2;
			maps = details.maps;
			fprintf(log_file, "browse details: \"%s\" by \"%s\" %u KB score %d pictures %d maps %d files %d skipped %d "
				"refused %d: %s %s\n", details.title, details.author, details.size_kb, details.score, details.pictures,
				details.maps, details.files, details.skipped, details.refused, details.map_names[0], details.map_names[1]);
			fprintf(log_file, "browse description: %s\n", details.description);
			for (index = 0; index < details.pictures; index++)
			{
				int width = 0, height = 0;

				if (index)
				{
					steam_job_workshop_details_picture(index);
					browse_wait(browse_details_here, 30);
				}
				steam_job_workshop_details(&details);
				if (details.picture == 1 && steam_job_workshop_picture(-1, pixels,
					STEAM_IMAGE_LARGE_WIDTH * STEAM_IMAGE_LARGE_HEIGHT * 4, &width, &height) && width == STEAM_IMAGE_LARGE_WIDTH)
				{
					details_pictures++;
				}
				fprintf(log_file, "browse details picture %d: %d\n", index, details.picture);
			}
			free(pixels);
			{
				unsigned int held = 0, peak = 0;

				steam_job_workshop_memory(&held, &peak);
				if (held > most_held)
					most_held = held;
			}
		}
		/* a hostile one's page: its pictures all refused */
		steam_job_workshop_details_ask("2900000015");
		browse_wait(browse_details_here, 60);
		steam_job_workshop_details(&details);
		fprintf(log_file, "browse details 2900000015: state %d pictures %d picture %d\n", details.state, details.pictures,
			details.picture);
		CHECK(details.state == 2 && details.picture == -1);
		steam_job_workshop_details_ask("");
	}
	/* nothing chosen: the session just ends */
	steam_job_workshop_choose(NULL, 0);
	browse_wait(browse_ended, 60);
	steam_job_get(&status);
	{
		unsigned int held = 0, peak = 0;

		steam_job_workshop_memory(&held, &peak);
		printf("result ok=%d cancelled=%d pages=%d pictures=%d refused=%d subscribed=%d details=%d maps=%d "
			"details_pictures=%d held_after=%u most_held=%u revoked=%d logged_off=%d error=%s\n",
			status.phase == STEAM_JOB_DONE, status.phase == STEAM_JOB_CANCELLED, pages, made, refused, subscribed,
			details_ok, maps, details_pictures, held, most_held, status.revoked, status.logged_off,
			status.error[0] ? status.error : "-");
	}
	CHECK(steam_job_forget());
	fclose(log_file);
	return failures != 0;
}

/* ---------- LIVE: the real Steam, the owner's own account, by his hand alone (docs/steam_download_live_test.md)

steam_download_test live <folder> [games]: the job as the Vita runs it, against Steam's own CM (from ISteamDirectory)
and CDN: the QR drawn here in the terminal for the Steam app to scan, the progress, the end; Halo CE's files to
<folder>/haloce-vita/mcc/halo1/..., the job's log (no secret in it) to <folder>/live-log.txt. games: 1 Halo CE (about
470 MB), 3 with the sound banks (about 2 GB). Never run by an agent: the owner starts it and scans the QR himself */

static FILE *live_log;

static void live_log_line(const char *line)
{
	fprintf(live_log, "%s\n", line);
	fflush(live_log);
	fprintf(stderr, "  %s\n", line);
}

static void live_qr_print(const struct steam_job_status *status)
{
	int x, y, size = status->qr_size;

	/* (two modules a character row, the quiet zone around; dark on a light terminal and light on a dark one both read:
	the finder patterns tell the app which is which) */
	printf("\n");
	for (y = -2; y < size + 2; y += 2)
	{
		printf("    ");
		for (x = -2; x < size + 2; x++)
		{
			int top = y >= 0 && y < size && x >= 0 && x < size && status->qr[y * size + x];
			int bottom = y + 1 >= 0 && y + 1 < size && x >= 0 && x < size && status->qr[(y + 1) * size + x];

			printf("%s", top && bottom ? "\xe2\x96\x88" : top ? "\xe2\x96\x80" : bottom ? "\xe2\x96\x84" : " ");
		}
		printf("\n");
	}
	printf("\n");
	fflush(stdout);
}

static const char *live_phase_name(int phase)
{
	static const char *const names[] = { "idle", "connecting", "QR", "preparing", "browsing the Workshop", "downloading",
		"paused", "ending", "done", "FAILED", "cancelled" };

	return phase >= 0 && phase < (int)(sizeof(names) / sizeof(names[0])) ? names[phase] : "?";
}

static int live(int argc, char **argv)
{
	struct steam_job_options options;
	struct steam_job_platform hooks;
	struct steam_job_status status;
	char log_path[600];
	unsigned int qr_serial = 0;
	int last_phase = -1, games = argc > 3 ? atoi(argv[3]) : 1;
	uint64_t shown = 0;

	if (argc < 3 || games <= 0 || games > 3)
	{
		fprintf(stderr, "usage: steam_download_test live <folder> [games: 1 Halo CE, 3 with the sound banks]\n");
		return 2;
	}
	mkdir(argv[2], 0777);
	snprintf(log_path, sizeof(log_path), "%s/live-log.txt", argv[2]);
	live_log = fopen(log_path, "a");
	if (!live_log)
	{
		fprintf(stderr, "cannot write %s\n", log_path);
		return 2;
	}
	memset(&hooks, 0, sizeof(hooks));
	hooks.log = live_log_line;
	steam_job_set_platform(&hooks);
	memset(&options, 0, sizeof(options));
	options.games = games;
	/* (cm_url "" asks Steam's directory; no CDN override: Steam's own servers) */
	snprintf(options.ce_root, sizeof(options.ce_root), "%s/haloce-vita", argv[2]);
	snprintf(options.app_root, sizeof(options.app_root), "%s/haloce-vita", argv[2]);
	snprintf(options.marker_path, sizeof(options.marker_path), "%s/haloce-vita/steam-session.txt", argv[2]);
	printf("Steam download LIVE test: Halo CE%s from YOUR OWN Steam account into %s/haloce-vita\n"
		"Unofficial. The QR below is for the Steam app on your phone (Steam Guard > scan). Approve only this one.\n"
		"Ctrl-C ends it the hard way (the token would stay valid until it expires: remove \"%s\" under Authorized\n"
		"devices in the Steam app); press Enter instead to cancel cleanly (revoke, log off).\n\n",
		games & STEAM_GAME_HALO_CE_BANKS ? " with the sound banks" : "", argv[2], STEAM_DEVICE_NAME);
	if (!steam_job_start(&options))
	{
		printf("the job did not start\n");
		return 1;
	}
	{
		/* (stdin: Enter cancels) */
		fd_set set;
		struct timeval wait;

		for (;;)
		{
			FD_ZERO(&set);
			FD_SET(0, &set);
			wait.tv_sec = 0;
			wait.tv_usec = 200 * 1000;
			if (select(1, &set, NULL, NULL, &wait) > 0)
			{
				char line[16];

				if (fgets(line, sizeof(line), stdin))
				{
					printf("cancelling: signing out...\n");
					steam_job_cancel();
				}
			}
			steam_job_get(&status);
			if (status.phase != last_phase)
			{
				last_phase = status.phase;
				printf("== %s\n", live_phase_name(status.phase));
				fflush(stdout);
			}
			if (status.phase == STEAM_JOB_QR && status.qr_size && status.qr_serial != qr_serial)
			{
				qr_serial = status.qr_serial;
				printf("Scan this with the Steam app (Steam Guard > scan a QR code)%s:\n",
					qr_serial > 1 ? " - a new code, the last one expired" : "");
				live_qr_print(&status);
			}
			if (status.phase == STEAM_JOB_DOWNLOADING && status.done - shown >= (8u << 20))
			{
				shown = status.done;
				printf("  %.1f of %.1f MB (%.2f MB/s, part %d of %d) %s\n", status.done / 1e6, status.total / 1e6,
					status.rate / 1e6, status.depot_index + 1, status.depot_count, status.file);
				fflush(stdout);
			}
			if (status.phase == STEAM_JOB_DONE || status.phase == STEAM_JOB_FAILED || status.phase == STEAM_JOB_CANCELLED)
				break;
		}
	}
	printf("\nresult: %s; files downloaded %u, already there %u; token revoked %d, logged off %d, sign-ins %d%s%s\n"
		"Check the Steam app: Steam Guard > Authorized devices should NOT list \"%s\" any more.\n"
		"The log (no secrets): %s\n", status.phase == STEAM_JOB_DONE ? "DONE" : status.phase == STEAM_JOB_CANCELLED ?
		"cancelled" : "FAILED", status.files_done, status.files_already, status.revoked, status.logged_off,
		status.attempts, status.error[0] ? "; error: " : "", status.error, STEAM_DEVICE_NAME, log_path);
	steam_job_forget();
	fclose(live_log);
	return status.phase == STEAM_JOB_DONE ? 0 : 1;
}

/* ---------- LIVE WORKSHOP: the real Steam Workshop, the owner's own account, by his hand alone
(docs/steam_download_live_test.md, "The Workshop")

steam_download_test live-workshop <folder>: the Workshop job as the Vita's STEAM WORKSHOP screen runs it, against
Steam's own CM and CDN: the QR in the terminal, then the list - Halo CE's Workshop items, ten a page - driven by typed
commands (n, p: page; o: the list - trending, newest, most liked, updated, SUBSCRIBED; s TEXT: search; i N: row N's
details page - its author, votes, pictures made, description, the maps its manifest holds; m N: mark row N; d:
download the marked; q: sign out); the items into
<folder>/data/workshop/<id> (the game's staging folder: the game checks every map and moves them into mods/ the next
time its STEAM WORKSHOP screen opens); the log (no secret) to <folder>/live-log.txt. Never run by an agent. */

static void live_workshop_page(const struct steam_job_workshop_view *ws)
{
	int index;

	printf("\n-- page %d (order %d%s%s%s), %d items in all\n", ws->page, ws->sort, ws->search[0] ? ", search \"" : "",
		ws->search, ws->search[0] ? "\"" : "", ws->total);
	if (ws->error[0])
		printf("   error: %s\n", ws->error);
	for (index = 0; index < ws->count; index++)
	{
		const struct steam_job_workshop_row *row = &ws->rows[index];

		printf("  %2d %-12s %8.1f MB  %-30.30s by %-16.16s %3d%% pic %2d %s%s%s\n", index + 1, row->id,
			row->size_kb / 1024.0, row->title, row->author, row->score, row->picture,
			(row->tags & STEAM_WORKSHOP_TAG_CAMPAIGN) ? "campaign " : "",
			(row->tags & STEAM_WORKSHOP_TAG_MULTIPLAYER) ? "multiplayer " : "",
			row->downloadable ? "" : row->reason);
	}
	printf("commands: n next, p previous, o list, s TEXT search, i N details, m N mark row N, d download the marked, "
		"q sign out\n");
	fflush(stdout);
}

static int live_workshop(int argc, char **argv)
{
	struct steam_job_platform hooks;
	struct steam_job_status status;
	struct steam_job_workshop_view ws;
	char log_path[600], data_root[600], marked[STEAM_WORKSHOP_CHOSEN_MAXIMUM][24];
	unsigned int qr_serial = 0;
	int last_phase = -1, last_serial = -1, marked_count = 0, sort = 0;

	if (argc < 3)
	{
		fprintf(stderr, "usage: steam_download_test live-workshop <folder>\n");
		return 2;
	}
	mkdir(argv[2], 0777);
	snprintf(data_root, sizeof(data_root), "%s/data", argv[2]);
	mkdir(data_root, 0777);
	snprintf(log_path, sizeof(log_path), "%s/live-log.txt", argv[2]);
	live_log = fopen(log_path, "a");
	if (!live_log)
	{
		fprintf(stderr, "cannot write %s\n", log_path);
		return 2;
	}
	memset(&hooks, 0, sizeof(hooks));
	hooks.log = live_log_line;
	steam_job_set_platform(&hooks);
	printf("Steam Workshop LIVE test: Halo CE's Workshop items, with YOUR OWN Steam account, into %s/workshop\n"
		"Unofficial. The QR below is for the Steam app on your phone (Steam Guard > scan). Approve only this one.\n"
		"q (or Enter alone while downloading) signs out cleanly; Ctrl-C does not (then remove \"%s\" under\n"
		"Authorized devices in the Steam app).\n\n", data_root, STEAM_DEVICE_NAME);
	if (steam_job_start_workshop(data_root) <= 0)
	{
		printf("the job did not start\n");
		return 1;
	}
	for (;;)
	{
		fd_set set;
		struct timeval wait;

		FD_ZERO(&set);
		FD_SET(0, &set);
		wait.tv_sec = 0;
		wait.tv_usec = 200 * 1000;
		steam_job_get(&status);
		steam_job_workshop_view(&ws);
		if (select(1, &set, NULL, NULL, &wait) > 0)
		{
			char line[128];

			if (fgets(line, sizeof(line), stdin))
			{
				line[strcspn(line, "\r\n")] = 0;
				if (status.phase != STEAM_JOB_BROWSING)
				{
					printf("cancelling: signing out...\n");
					steam_job_cancel();
				}
				else if (!strcmp(line, "q"))
					steam_job_workshop_choose(NULL, 0);
				else if (!strcmp(line, "n") || !strcmp(line, "p"))
					steam_job_workshop_ask(ws.page + (line[0] == 'n' ? 1 : -1), ws.sort, ws.search);
				else if (!strcmp(line, "o"))
				{
					sort = (sort + 1) % (STEAM_WORKSHOP_LIST_SUBSCRIBED + 1);
					steam_job_workshop_ask(1, sort, "");
				}
				else if (line[0] == 's' && line[1] == ' ')
					steam_job_workshop_ask(1, sort, line + 2);
				else if (line[0] == 'm' && line[1] == ' ')
				{
					int row = atoi(line + 2) - 1;

					if (row >= 0 && row < ws.count && ws.rows[row].downloadable &&
						marked_count < STEAM_WORKSHOP_CHOSEN_MAXIMUM)
					{
						snprintf(marked[marked_count++], sizeof(marked[0]), "%s", ws.rows[row].id);
						printf("marked %s (%d)\n", ws.rows[row].id, marked_count);
					}
					else
						printf("not that row\n");
				}
				else if (line[0] == 'i' && line[1] == ' ')
				{
					int row = atoi(line + 2) - 1;

					if (row >= 0 && row < ws.count)
					{
						struct steam_job_workshop_details details;
						int step;

						steam_job_workshop_details_ask(ws.rows[row].id);
						for (step = 0; step < 600; step++)
						{
							struct timespec pause = { 0, 50 * 1000 * 1000 };

							steam_job_workshop_details(&details);
							if (details.state < 0 || (details.state == 2 && details.contents && details.picture))
								break;
							nanosleep(&pause, NULL);
						}
						printf("-- %s by %s, score %d (%u up, %u down), %u KB, %d pictures (the first: %s)\n%s\n"
							"   maps %d (%s %s ...), files %d, skipped %d, refused %d %s\n", details.title, details.author,
							details.score, details.votes_up, details.votes_down, details.size_kb, details.pictures,
							details.picture == 1 ? "made" : "not made", details.description, details.maps,
							details.map_names[0], details.map_names[1], details.files, details.skipped, details.refused,
							details.state < 0 ? details.error : details.contents_why);
						steam_job_workshop_details_ask("");
					}
				}
				else if (!strcmp(line, "d") && marked_count)
					steam_job_workshop_choose((const char (*)[24])marked, marked_count);
				else
					steam_job_workshop_touch();
			}
		}
		if (status.phase != last_phase)
		{
			last_phase = status.phase;
			printf("== %s\n", live_phase_name(status.phase));
			fflush(stdout);
		}
		if (status.phase == STEAM_JOB_QR && status.qr_size && status.qr_serial != qr_serial)
		{
			qr_serial = status.qr_serial;
			printf("Scan this with the Steam app (Steam Guard > scan a QR code)%s:\n",
				qr_serial > 1 ? " - a new code, the last one expired" : "");
			live_qr_print(&status);
		}
		if (status.phase == STEAM_JOB_BROWSING && !ws.loading && ws.serial != last_serial)
		{
			last_serial = ws.serial;
			live_workshop_page(&ws);
		}
		if (status.phase == STEAM_JOB_DONE || status.phase == STEAM_JOB_FAILED || status.phase == STEAM_JOB_CANCELLED)
			break;
	}
	steam_job_workshop_view(&ws);
	{
		uint64_t staged[16];
		int count = steam_workshop_staged(data_root, staged, 16), index;

		printf("\nresult: %s; items downloaded %d, failed %d%s%s; token revoked %d, logged off %d\n",
			status.phase == STEAM_JOB_DONE ? "DONE" : status.phase == STEAM_JOB_CANCELLED ? "cancelled" : "FAILED",
			ws.items_done, ws.items_failed, ws.failure[0] ? " (" : "", ws.failure[0] ? ws.failure : "", status.revoked,
			status.logged_off);
		for (index = 0; index < count; index++)
			printf("  staged: %s/workshop/%llu (the game checks and installs it)\n", data_root,
				(unsigned long long)staged[index]);
	}
	printf("Check the Steam app: Steam Guard > Authorized devices should NOT list \"%s\" any more.\n"
		"The log (no secrets): %s\n", STEAM_DEVICE_NAME, log_path);
	steam_job_forget();
	fclose(live_log);
	return status.phase == STEAM_JOB_DONE ? 0 : 1;
}

static void test_qr(void)
{
	uint8_t modules[57 * 57];
	int size = steam_job_qr_encode("https://s.team/q/1/1234567890123456789", modules, 57);

	/* (version 3, medium: 29 modules; the finder's corner dark, its ring light) */
	CHECK(size == 29 && modules[0] == 1 && modules[1 * 29 + 1] == 0 && modules[3 * 29 + 3] == 1);
	CHECK(steam_job_qr_encode("https://s.team/q/1/x", modules, 20) == 0);
}

int main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "job"))
		return job(argc, argv);
	if (argc >= 2 && !strcmp(argv[1], "browse"))
		return browse(argc, argv);
	if (argc >= 2 && !strcmp(argv[1], "live"))
		return live(argc, argv);
	if (argc >= 2 && !strcmp(argv[1], "live-workshop"))
		return live_workshop(argc, argv);
	if (argc >= 3 && !strcmp(argv[1], "units"))
	{
		test_pb();
		test_crypto();
		test_zip(argv[2]);
		test_kv();
		test_ws();
		test_cm();
		test_plan();
		test_chunks(argv[2]);
		test_manifest(argv[2]);
		test_file(argv[2]);
		test_net();
		test_qr();
		test_workshop(argv[2]);
		test_browser(argv[2]);
		printf("-- %d of %d checks failed\n", failures, checks);
		return failures != 0;
	}
	if (argc >= 2 && !strcmp(argv[1], "flow"))
		return flow(argc, argv);
	if (argc >= 2 && !strcmp(argv[1], "workshop"))
		return workshop(argc, argv);
	fprintf(stderr, "usage: steam_download_test units <fixtures> | flow <cm port> <cdn port> <out> <games> [cancel] | "
		"job ... | live <folder> [games] | live-workshop <folder>\n");
	return 2;
}
