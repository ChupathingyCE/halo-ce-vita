/*
MIXED_COOP_TABLES.C

(1.1.3) The built-in tables of mixed play with a host that knows nothing of it
(mixed_coop_tables.h): mixed_coop_tables.inc, made by tools/mixed_tables.py,
each level's streams deflated (zlib).
*/

#include "mixed_coop_tables.h"
#include "../../third_party/zlib/zlib_prefixed.h"

#include <stddef.h>
#include <stdlib.h>

struct mixed_coop_table
{
	/* mixed_coop_level_index's level */
	int level;
	unsigned char const *dictionary;
	unsigned long dictionary_bytes;
	unsigned long dictionary_length;
	unsigned char const *layout;
	unsigned long layout_bytes;
	unsigned long layout_length;
};

#include "mixed_coop_tables.inc"

static struct mixed_coop_table const *mixed_coop_table_find(
	int level)
{
	size_t index;

	for (index = 0; index < sizeof(mixed_coop_tables) / sizeof(mixed_coop_tables[0]); index++)
	{
		if (mixed_coop_tables[index].level == level)
			return &mixed_coop_tables[index];
	}
	return NULL;
}

static int mixed_coop_table_inflate(
	unsigned char const *deflated,
	unsigned long deflated_bytes,
	unsigned long length,
	uint8_t **bytes,
	uint32_t *out_length)
{
	uLongf inflated = (uLongf)length;
	uint8_t *buffer;

	*bytes = NULL;
	*out_length = 0;
	if (!deflated || !deflated_bytes || !length || length > 0x100000)
		return 0;
	buffer = malloc(length);
	if (!buffer)
		return 0;
	if (uncompress(buffer, &inflated, deflated, (uLong)deflated_bytes) != Z_OK || inflated != (uLongf)length)
	{
		free(buffer);
		return 0;
	}
	*bytes = buffer;
	*out_length = (uint32_t)length;
	return 1;
}

int mixed_coop_table_present(
	int level)
{
	return mixed_coop_table_find(level) != NULL;
}

int mixed_coop_table_dictionary(
	int level,
	uint8_t **bytes,
	uint32_t *length)
{
	struct mixed_coop_table const *table = mixed_coop_table_find(level);

	*bytes = NULL;
	*length = 0;
	return table && mixed_coop_table_inflate(table->dictionary, table->dictionary_bytes, table->dictionary_length, bytes,
		length);
}

int mixed_coop_table_layout(
	int level,
	uint8_t **bytes,
	uint32_t *length)
{
	struct mixed_coop_table const *table = mixed_coop_table_find(level);

	*bytes = NULL;
	*length = 0;
	return table && table->layout && mixed_coop_table_inflate(table->layout, table->layout_bytes, table->layout_length, bytes,
		length);
}

void mixed_coop_table_free(
	void *bytes)
{
	free(bytes);
}
