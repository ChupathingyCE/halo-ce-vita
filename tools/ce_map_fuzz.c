/*
CE_MAP_FUZZ.C

A libFuzzer target over the Custom Edition map loader and its conversions
(port/linux/game/cache_file_formats.c), as the game runs them on a map a
host shares (port/linux/game/custom_edition_cache.c): the map loaded and
checked into a tag cache with an unmapped page at each end, a Halo: CE
Anniversary map's tags made this build's (mcc_cea_cache_fix), the resource
offsets combined, the tags converted in place, moved from where the map
links them to where the tag cache is (port/linux/src/tag_relocate.c, as on
the Vita, where no tag cache is where a map links it), checked against the
schemas (tag_validate.c), then each structure BSP read as the game reads one
- only as its load checked it (custom_edition_cache_structure_bsp_checked) -
moved, and checked. Built under AddressSanitizer and UndefinedBehaviour-
Sanitizer by tools/ce_map_fuzz.py, which also runs it.

Two kinds of input:
- with CE_MAP_FUZZ_SEEDS (map files, colon-separated): changes to those
  maps. The first byte picks the map; each 9 bytes after it are a change:
  a kind byte, a 32-bit index and a 32-bit value. The kind's low two bits
  say where (0, 1: a word the tags' check claimed - the tags' roots, blocks
  and data; 2: anywhere in the tag data; 3: anywhere in the file, for a
  Halo: CE Anniversary map half the time in its structure BSPs and their
  vertices), bit 2 makes the value a place in the tag cache, bit 3 a small
  number, bit 4 writes 16 bits instead of 32. Real maps are 25-110 MB, too
  large for libFuzzer to change byte by byte.
- without: the input is a map file (the small maps the tests build,
  tools/test_cache_file_formats.py).
The resource maps (bitmaps.map, sounds.map, loc.map) are those in
CE_MAP_FUZZ_RESOURCES.

A refusal is what the game does with a damaged map, and passes; a crash,
hang, leak or sanitizer report is a finding.
*/

#define main map_validate_main
#include "map_validate.c"
#undef main

#include <stdint.h>

/* ---------- constants */

enum
{
	MAXIMUM_FUZZ_MAPS = 48,
	MAXIMUM_FUZZ_CHANGES = 32,
	FUZZ_CHANGE_BYTES = 9,
};

/* ---------- structures */

struct fuzz_map
{
	char name[96];
	struct memory_file map;
	struct cache_file_identity identity;
	unsigned long tag_cache_size;
	unsigned long link_address;
	unsigned long *claimed;
	long claimed_count;
	struct mcc_cea_structure_bsp structure_bsps[CACHE_FILE_MAXIMUM_STRUCTURE_BSPS];
	long structure_bsp_count;
};

/* ---------- globals */

static struct fuzz_map fuzz_maps[MAXIMUM_FUZZ_MAPS];
static long fuzz_map_count;
static struct custom_edition_resources fuzz_resources;
/* the tag cache: an unmapped page, the largest tag cache, an unmapped page;
each run's tag cache ends against the second */
static unsigned char *fuzz_region;
static unsigned long fuzz_region_bytes;
static unsigned long fuzz_page;

/* ---------- code */

/* (the loader's reads with the CRC hook: the checksum is not what is fuzzed,
and the model data's, 20 MB a run, is left to the game) */
static int fuzz_read_crc(void *context, uint32_t offset, uint32_t size, void *buffer, uint32_t *crc)
{
	*crc = 0;
	return memory_file_read(context, offset, size, buffer);
}

/* the tag cache of `size` bytes for this run, ending against the unmapped
page after it */
static unsigned char *fuzz_tag_cache(unsigned long size)
{
	unsigned long rounded = (size + fuzz_page - 1) & ~(fuzz_page - 1);
	unsigned char *top = fuzz_region + fuzz_region_bytes - fuzz_page;

	if (rounded > fuzz_region_bytes - 2 * fuzz_page)
		return NULL;
	mprotect(fuzz_region + fuzz_page, fuzz_region_bytes - 2 * fuzz_page, PROT_NONE);
	if (mprotect(top - rounded, rounded, PROT_READ | PROT_WRITE))
		abort();
	return top - size;
}

/* the map loaded, converted, moved and checked as the game does it, then its
structure BSPs: 0 if it plays, 1 if refused; *report as its load left it */
static int fuzz_check(struct memory_file *map, char const *name, struct custom_edition_load_report *report,
	unsigned char **tag_cache_used, unsigned long *loaded_bytes)
{
	struct custom_edition_conversion_report conversion;
	struct custom_edition_load_hooks hooks;
	struct tag_validate_file_range ranges[3];
	struct mcc_cea_fix_report fix;
	struct cache_file_identity identity;
	enum cache_file_status status;
	unsigned char *tag_cache;
	unsigned long size;
	struct tag_header *tags;
	struct tag_block *bsps;
	short range_count = 0;
	short scenario_index;
	long bsp_count;
	long index;
	int mcc_cea;

	*tag_cache_used = NULL;
	*loaded_bytes = 0;
	memset(report, 0, sizeof(*report));
	if (cache_file_identify(&map->source, &identity) != _cache_file_status_ok)
		return 1;
	size = custom_edition_tag_cache_bytes(&identity);
	tag_cache = fuzz_tag_cache(size);
	if (!tag_cache)
		return 1;
	*tag_cache_used = tag_cache;
	memset(tag_cache, 0xCD, size);
	custom_edition_tag_cache_size = size;
	tag_validate_structure_vertices_in_file(0);
	memset(&hooks, 0, sizeof(hooks));
	hooks.source = &map->source;
	hooks.read_crc = fuzz_read_crc;
	hooks.defer_model_checksum = 1;
	custom_edition_cache_load_hooks(&hooks);
	status = custom_edition_cache_load(&map->source, fuzz_resources.maps, tag_cache, size, 1, report);
	custom_edition_cache_load_hooks(NULL);
	mcc_cea = report->identity.format == _cache_file_format_mcc_cea_cache;
	if (status == _cache_file_status_ok && mcc_cea)
		status = mcc_cea_cache_fix(tag_cache, report->tag_data_bytes, &fix);
	if (status != _cache_file_status_ok)
		return 1;
	*loaded_bytes = report->tag_data_bytes + report->resource_tag_bytes;
	custom_edition_cache_combine_resource_offsets(tag_cache, *loaded_bytes, CUSTOM_EDITION_BITMAPS_OFFSET,
		CUSTOM_EDITION_SOUNDS_OFFSET);
	if (custom_edition_cache_convert(tag_cache, *loaded_bytes, report->identity.name, &conversion) !=
		_cache_file_status_ok)
		return 1;
	if (!halo_tag_relocate_linked_tags(tag_cache, *loaded_bytes, custom_edition_cache_linked_address(),
		report->tag_cache_bytes))
		return 1;
	custom_edition_cache_tags_moved((uint32_t)(unsigned long)tag_cache);
	tag_validate_structure_vertices_in_file(mcc_cea);
	ranges[range_count].offset = 0;
	ranges[range_count++].size = report->identity.file_length;
	if (fuzz_resources.maps[_resource_map_bitmaps])
	{
		ranges[range_count].offset = CUSTOM_EDITION_BITMAPS_OFFSET;
		ranges[range_count++].size = fuzz_resources.files[_resource_map_bitmaps].size;
	}
	if (fuzz_resources.maps[_resource_map_sounds])
	{
		ranges[range_count].offset = CUSTOM_EDITION_SOUNDS_OFFSET;
		ranges[range_count++].size = fuzz_resources.files[_resource_map_sounds].size;
	}
	if (!tag_validate_custom_edition_tags(tag_cache, (long)*loaded_bytes, report->tag_cache_bytes, ranges, range_count,
		name))
		return 1;

	tags = (struct tag_header *)tag_cache;
	scenario_index = (short)tags->scenario_tag_index;
	if (scenario_index < 0 || scenario_index >= tags->tag_count ||
		tags->instances[scenario_index].group_tag != SCENARIO_GROUP_TAG)
		return 1;
	bsps = (struct tag_block *)((unsigned char *)tags->instances[scenario_index].base_address +
		SCENARIO_STRUCTURE_BSPS_OFFSET);
	bsp_count = bsps->count < MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO ? bsps->count : MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO;
	for (index = 0; index < bsp_count; index++)
	{
		struct scenario_structure_bsp_reference *reference =
			(struct scenario_structure_bsp_reference *)bsps->address + index;
		struct mcc_cea_structure_bsp const *checked = custom_edition_cache_structure_bsp_checked(report,
			(uint32_t)reference->file_offset, (uint32_t)reference->file_size);
		unsigned long place;
		unsigned char *bsp;
		unsigned long structure;

		/* (cache_files.c, custom_edition_cache_structure_bsp_reference_checked:
		read only as checked, where checked) */
		if (!checked)
			return 1;
		place = mcc_cea ? report->mcc_cea_structure_bsp_address : checked->linked_address;
		bsp = tag_cache + (place - custom_edition_cache_linked_address());
		if (reference->base_address != bsp)
			return 1;
		memset(tag_cache + *loaded_bytes, 0xCD, report->tag_cache_bytes - *loaded_bytes);
		memcpy(bsp, map->bytes + checked->file_offset, checked->size);
		/* (custom_edition_cache_structure_bsp_moved) */
		if (mcc_cea)
		{
			unsigned long vertices_size;

			memcpy(&vertices_size, bsp + 4, 4);
			if (checked->vertices_size && vertices_size != checked->vertices_size)
				return 1;
			memset(bsp + 4, 0, 16);
			halo_tag_relocate_linked_structure_bsp_rebase(bsp, checked->size, checked->linked_address,
				report->mcc_cea_structure_bsp_address);
		}
		halo_tag_relocate_linked_structure_bsp(tag_cache, bsp, checked->size);
		memcpy(&structure, bsp, 4);
		if (structure < (unsigned long)bsp || structure - (unsigned long)bsp >= checked->size)
			return 1;
		if (!tag_validate_structure_bsp(reference->tag_index, bsp, (long)checked->size))
			return 1;
	}

	return 0;
}

/* the words of the tags the check claimed, where most changes go */
static void fuzz_claims_find(struct fuzz_map *fuzz)
{
	struct custom_edition_load_report report;
	struct tag_validate_file_range everything = { 0, 0xFFFFFFFFUL };
	unsigned char *tag_cache;
	unsigned long loaded_bytes;
	unsigned long offset;

	if (fuzz_check(&fuzz->map, fuzz->name, &report, &tag_cache, &loaded_bytes))
	{
		fprintf(stderr, "ce_map_fuzz: %s does not load as it is; its changes go anywhere\n", fuzz->name);
		return;
	}
	fuzz->structure_bsp_count = report.structure_bsp_count;
	memcpy(fuzz->structure_bsps, report.mcc_cea_structure_bsps, sizeof(fuzz->structure_bsps));
	tag_validate_keep_claims(1);
	tag_validate_custom_edition_tags(tag_cache, (long)loaded_bytes, fuzz->tag_cache_size, &everything, 1, fuzz->name);
	tag_validate_keep_claims(0);
	fuzz->claimed = malloc(sizeof(*fuzz->claimed) * (fuzz->identity.tag_data_size / 4 + 1));
	for (offset = 0; fuzz->claimed && offset + 4 <= fuzz->identity.tag_data_size; offset += 4)
	{
		if (tag_validate_claimed(tag_cache + offset))
			fuzz->claimed[fuzz->claimed_count++] = fuzz->identity.tag_data_offset + offset;
	}
	fprintf(stderr, "ce_map_fuzz: %s: %ld claimed words, %ld structure bsps\n", fuzz->name, fuzz->claimed_count,
		fuzz->structure_bsp_count);
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	char const *resources = getenv("CE_MAP_FUZZ_RESOURCES");
	char const *seeds = getenv("CE_MAP_FUZZ_SEEDS");

	(void)argc;
	(void)argv;
	quiet = 1;
	fuzz_page = (unsigned long)sysconf(_SC_PAGESIZE);
	fuzz_region_bytes = ((CUSTOM_EDITION_TAG_CACHE_BYTES_UPGRADED + fuzz_page - 1) & ~(fuzz_page - 1)) + 2 * fuzz_page;
	fuzz_region = mmap(NULL, fuzz_region_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (fuzz_region == MAP_FAILED)
	{
		fprintf(stderr, "ce_map_fuzz: no room for the tag cache\n");
		exit(2);
	}
	if (resources)
	{
		char path[1024];

		/* (custom_edition_resources_open looks beside the map it is given) */
		snprintf(path, sizeof(path), "%s/none.map", resources);
		custom_edition_resources_open(&fuzz_resources, path);
	}
	while (seeds && *seeds && fuzz_map_count < MAXIMUM_FUZZ_MAPS)
	{
		char const *end = strchr(seeds, ':');
		size_t length = end ? (size_t)(end - seeds) : strlen(seeds);
		struct fuzz_map *fuzz = &fuzz_maps[fuzz_map_count];
		char path[1024];

		snprintf(path, sizeof(path), "%.*s", (int)length, seeds);
		seeds += length + (end ? 1 : 0);
		snprintf(fuzz->name, sizeof(fuzz->name), "%s", strrchr(path, '/') ? strrchr(path, '/') + 1 : path);
		if (!memory_file_open(&fuzz->map, path) ||
			cache_file_identify(&fuzz->map.source, &fuzz->identity) != _cache_file_status_ok)
		{
			fprintf(stderr, "ce_map_fuzz: %s is not a map this build loads\n", path);
			memory_file_close(&fuzz->map);
			continue;
		}
		fuzz->tag_cache_size = custom_edition_tag_cache_bytes(&fuzz->identity);
		fuzz->link_address = fuzz->identity.format == _cache_file_format_mcc_cea_cache ?
			MCC_CEA_TAG_CACHE_ADDRESS : CUSTOM_EDITION_TAG_CACHE_ADDRESS;
		fuzz_claims_find(fuzz);
		fuzz_map_count++;
	}

	return 0;
}

static uint32_t fuzz_u32(uint8_t const *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

int LLVMFuzzerTestOneInput(uint8_t const *data, size_t size)
{
	struct custom_edition_load_report report;
	unsigned char *tag_cache;
	unsigned long loaded_bytes;

	if (!fuzz_map_count)
	{
		/* the input is a map file */
		struct memory_file map;

		if (size < CACHE_FILE_HEADER_SIZE || size > 0x4000000)
			return 0;
		memset(&map, 0, sizeof(map));
		map.bytes = malloc(size);
		if (!map.bytes)
			return 0;
		memcpy(map.bytes, data, size);
		map.size = size;
		map.source.context = &map;
		map.source.read = memory_file_read;
		map.source.size = (uint32_t)size;
		fuzz_check(&map, "input", &report, &tag_cache, &loaded_bytes);
		halo_tag_relocate_linked_release();
		free(map.bytes);
		return 0;
	}
	if (size < 1)
		return 0;
	{
		struct fuzz_map *fuzz = &fuzz_maps[data[0] % fuzz_map_count];
		unsigned long offsets[MAXIMUM_FUZZ_CHANGES];
		unsigned char saved[MAXIMUM_FUZZ_CHANGES][4];
		unsigned long widths[MAXIMUM_FUZZ_CHANGES];
		long changes = (long)((size - 1) / FUZZ_CHANGE_BYTES);
		long change;

		if (changes > MAXIMUM_FUZZ_CHANGES)
			changes = MAXIMUM_FUZZ_CHANGES;
		for (change = 0; change < changes; change++)
		{
			uint8_t const *record = data + 1 + change * FUZZ_CHANGE_BYTES;
			unsigned kind = record[0];
			uint32_t index = fuzz_u32(record + 1);
			uint32_t value = fuzz_u32(record + 5);
			unsigned long offset;

			switch (kind & 3)
			{
			case 0:
			case 1:
				offset = fuzz->claimed_count ? fuzz->claimed[index % fuzz->claimed_count] :
					fuzz->identity.tag_data_offset + index % fuzz->identity.tag_data_size;
				break;
			case 2:
				offset = fuzz->identity.tag_data_offset + index % fuzz->identity.tag_data_size;
				break;
			default:
				if (fuzz->structure_bsp_count && (kind & 0x20))
				{
					struct mcc_cea_structure_bsp const *bsp = &fuzz->structure_bsps[index % fuzz->structure_bsp_count];

					offset = (kind & 0x40) && bsp->vertices_size ?
						bsp->vertices_offset + (index >> 5) % bsp->vertices_size :
						bsp->file_offset + (index >> 5) % bsp->size;
				}
				else
				{
					offset = index % fuzz->map.size;
				}
				break;
			}
			if (kind & 4)
				value = (uint32_t)(fuzz->link_address + value % fuzz->tag_cache_size);
			else if (kind & 8)
				value %= 300;
			widths[change] = (kind & 0x10) ? 2 : 4;
			offset &= ~(widths[change] - 1);
			if (offset + 4 > fuzz->map.size)
				offset = fuzz->identity.tag_data_offset;
			offsets[change] = offset;
			memcpy(saved[change], fuzz->map.bytes + offset, 4);
			memcpy(fuzz->map.bytes + offset, &value, widths[change]);
			/* (CE_MAP_FUZZ_PRINT: the changes, to make a finding a test) */
			if (getenv("CE_MAP_FUZZ_PRINT"))
				fprintf(stderr, "ce_map_fuzz: %s: %lu bytes at 0x%lx: 0x%08lx\n", fuzz->name, widths[change], offset,
					(unsigned long)value);
		}
		fuzz_check(&fuzz->map, fuzz->name, &report, &tag_cache, &loaded_bytes);
		halo_tag_relocate_linked_release();
		for (change = changes - 1; change >= 0; change--)
			memcpy(fuzz->map.bytes + offsets[change], saved[change], 4);
	}

	return 0;
}
