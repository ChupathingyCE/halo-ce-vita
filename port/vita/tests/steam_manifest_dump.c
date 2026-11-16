/*
STEAM_MANIFEST_DUMP.C

Lists a depot manifest file (an unzipped one, as Steam's depotcache keeps
them, or a zip of one) with port/steamdl/steam_manifest.c: its metadata,
then each file - name, size, flags, chunk count, compressed bytes - in the
form tools/steam_manifest_probe.py prints, which run_steam_download_test.sh
compares. With -p, the files the plan takes from it instead
(steam_plan.c). With -w (a Steam Workshop item's manifest, depot 976730), each file's verdict by the Workshop's
name rule (steam_workshop_name: TAKE and the kind, SKIP, REFUSE) and the item's total. Prints no secret: the local
manifests are not encrypted.
*/

#include "steam_manifest.h"
#include "steam_plan.h"
#include "steam_workshop.h"
#include "steam_zip.h"
#include "steam_limits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int plan_mode, workshop_mode;
static unsigned int workshop_counts[3];
static unsigned long long workshop_bytes;
static unsigned long long plan_bytes, plan_download, plan_chunks;
static unsigned int plan_files;
static uint32_t depot;

static int visit(void *context, const struct steam_manifest_file *file)
{
	(void)context;
	if (workshop_mode)
	{
		static const char *const verdicts[] = { "REFUSE", "SKIP", "TAKE" };
		char relative[STEAM_WORKSHOP_NAME_MAXIMUM + 1];
		enum steam_plan_kind kind = STEAM_KIND_WORKSHOP_TEXT;
		int verdict;

		if (file->flags & (STEAM_FILE_FLAG_DIRECTORY | STEAM_FILE_FLAG_SYMLINK))
			return 0;
		verdict = steam_workshop_name(file->name, relative, sizeof(relative), &kind);
		workshop_counts[verdict + 1]++;
		if (verdict == STEAM_WORKSHOP_NAME_TAKE)
			workshop_bytes += file->size;
		printf("  %-6s %-60s %12llu%s\n", verdicts[verdict + 1], file->name, (unsigned long long)file->size,
			verdict == STEAM_WORKSHOP_NAME_TAKE ? (kind == STEAM_KIND_MCC_MAP ? " map" : kind == STEAM_KIND_RESOURCE ?
				" resource" : kind == STEAM_KIND_WORKSHOP_TEXT ? " text" : " picture") : "");
		return 0;
	}
	if (plan_mode)
	{
		const struct steam_plan_file *planned = steam_plan_match(STEAM_GAMES_ALL, depot, file->name);

		if (planned)
		{
			printf("  %-52s -> %-44s %12llu chunks=%5u comp=%12llu%s\n", file->name, planned->destination,
				(unsigned long long)file->size, (unsigned int)file->chunk_count,
				(unsigned long long)file->compressed_size, file->covered ? "" : " NOT-COVERED");
			plan_bytes += file->size;
			plan_download += file->compressed_size;
			plan_chunks += file->chunk_count;
			plan_files++;
		}
		return 0;
	}
	printf("%s\t%llu\t%u\t%u\t%llu\t%s%s\n", file->name, (unsigned long long)file->size, (unsigned int)file->flags,
		(unsigned int)file->chunk_count, (unsigned long long)file->compressed_size, file->covered ? "covered" : "gap",
		file->name_hash_checked ? (file->name_hash_ok ? " namehash-ok" : " namehash-BAD") : "");
	return 0;
}

int main(int argc, char **argv)
{
	struct steam_manifest_metadata metadata;
	unsigned char *data, *unzipped = NULL;
	char error[256];
	long size;
	size_t entry;
	FILE *file;
	int argi = 1;

	if (argc > 1 && !strcmp(argv[1], "-p"))
		plan_mode = 1, argi++;
	else if (argc > 1 && !strcmp(argv[1], "-w"))
		workshop_mode = 1, argi++;
	if (argi + 1 != argc)
	{
		fprintf(stderr, "usage: steam_manifest_dump [-p|-w] <manifest>\n");
		return 2;
	}
	file = fopen(argv[argi], "rb");
	if (!file || fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 || size > (long)STEAM_MANIFEST_ZIPPED_MAXIMUM ||
		fseek(file, 0, SEEK_SET))
	{
		fprintf(stderr, "cannot read %s\n", argv[argi]);
		return 1;
	}
	data = malloc((size_t)size + 1);
	if (!data || fread(data, 1, (size_t)size, file) != (size_t)size)
		return 1;
	fclose(file);
	if (steam_zip_entry_size(data, (size_t)size, STEAM_MANIFEST_MAXIMUM, &entry))
	{
		unzipped = malloc(entry + 1);
		if (!unzipped || !steam_zip_extract(data, (size_t)size, unzipped, entry, error, sizeof(error)))
		{
			fprintf(stderr, "%s\n", error);
			return 1;
		}
		free(data);
		data = unzipped;
		size = (long)entry;
	}
	{
		/* (the depot from the metadata, before the files) */
		struct steam_manifest_metadata first;

		if (steam_manifest_parse(data, (size_t)size, NULL, &first, NULL, NULL, error, sizeof(error)) != 0)
		{
			fprintf(stderr, "refused: %s\n", error);
			return 1;
		}
		depot = first.depot_id;
	}
	if (!plan_mode && !workshop_mode)
		printf("#\n");
	if (steam_manifest_parse(data, (size_t)size, NULL, &metadata, visit, NULL, error, sizeof(error)) != 0)
	{
		fprintf(stderr, "refused: %s\n", error);
		return 1;
	}
	if (workshop_mode)
		printf("WORKSHOP gid %llu: %u taken (%.1f MB), %u skipped, %u refused\n", (unsigned long long)metadata.gid,
			workshop_counts[2], workshop_bytes / 1e6, workshop_counts[1], workshop_counts[0]);
	else if (plan_mode)
		printf("DEPOT %u gid %llu: %u planned files, %.1f MB on disk, %.1f MB download, %llu chunks\n",
			(unsigned int)metadata.depot_id, (unsigned long long)metadata.gid, plan_files, plan_bytes / 1e6,
			plan_download / 1e6, plan_chunks);
	else
		fprintf(stderr, "depot %u gid %llu encrypted %d unique_chunks %u crc_clear %s\n", (unsigned int)metadata.depot_id,
			(unsigned long long)metadata.gid, metadata.filenames_encrypted, (unsigned int)metadata.unique_chunks,
			steam_manifest_crc_ok(data, (size_t)size, &metadata) ? "ok" : "differs");
	free(data);
	return 0;
}
