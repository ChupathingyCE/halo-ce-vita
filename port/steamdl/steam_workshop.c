/*
STEAM_WORKSHOP.C

steam_workshop.h: the Workshop's requests, its responses read, its names' rule and the install.
Field numbers (SteamDatabase/Protobufs, steammessages_publishedfile.steamclient.proto):
  CPublishedFile_QueryFiles_Request: 1 query_type, 2 page, 3 numperpage, 5 appid, 6 requiredtags, 11 search_text,
    14 days, 18 return_tags, 37 return_details; its response 1 total, 2 publishedfiledetails
  CPublishedFile_GetDetails_Request: 1 publishedfileids (fixed64), 2 includetags, 14 appid; its response
    1 publishedfiledetails
  PublishedFileDetails: 1 result, 2 publishedfileid, 5 consumer_appid, 8 file_size, 14 hcontent_file (fixed64),
    16 title, 20 time_updated, 21 visibility, 28 banned, 52 tags (Tag: 1 tag)
*/

#include "steam_workshop.h"

#include "steam_limits.h"
#include "steam_pb.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* a tag's text read (Steam's tags are short words) and the tags read of one item */
#define TAG_MAXIMUM 64
#define TAGS_MAXIMUM 64
/* the install's walk: folders deep, entries looked at */
#define WALK_DEPTH (STEAM_WORKSHOP_NAME_PARTS + 1)
#define WALK_ENTRIES 4096

/* ---------- requests */

size_t steam_workshop_query_request(uint8_t *buffer, size_t capacity, int page, enum steam_workshop_sort sort,
	const char *search)
{
	static const uint32_t query_types[STEAM_WORKSHOP_SORT_COUNT] = { 3, 1, 0, 21 };
	struct steam_pb_writer writer;
	int searching = search && search[0];

	if (page < 1 || page > 1000 || (unsigned int)sort >= STEAM_WORKSHOP_SORT_COUNT ||
		(searching && strlen(search) >= STEAM_WORKSHOP_SEARCH_SIZE))
	{
		return 0;
	}
	steam_pb_writer_init(&writer, buffer, capacity);
	/* (12: RankedByTextSearch) */
	steam_pb_put_varint(&writer, 1, searching ? 12 : query_types[sort]);
	steam_pb_put_varint(&writer, 2, (uint64_t)page);
	steam_pb_put_varint(&writer, 3, STEAM_WORKSHOP_PAGE_ITEMS);
	steam_pb_put_varint(&writer, 5, STEAM_WORKSHOP_APP);
	steam_pb_put_string(&writer, 6, "Halo1");
	if (searching)
		steam_pb_put_string(&writer, 11, search);
	if (!searching && sort == STEAM_WORKSHOP_SORT_TREND)
		steam_pb_put_varint(&writer, 14, 7);
	steam_pb_put_varint(&writer, 18, 1);
	steam_pb_put_varint(&writer, 37, 1);
	return writer.error ? 0 : writer.size;
}

size_t steam_workshop_details_request(uint8_t *buffer, size_t capacity, const uint64_t *ids, int count)
{
	struct steam_pb_writer writer;
	int index;

	if (count < 1 || count > STEAM_WORKSHOP_ITEMS_MAXIMUM)
		return 0;
	steam_pb_writer_init(&writer, buffer, capacity);
	for (index = 0; index < count; index++)
		steam_pb_put_fixed64(&writer, 1, ids[index]);
	steam_pb_put_varint(&writer, 2, 1);
	steam_pb_put_varint(&writer, 14, STEAM_WORKSHOP_APP);
	return writer.error ? 0 : writer.size;
}

/* ---------- responses */

/* the item's tags (Tag messages): STEAM_WORKSHOP_TAG_* */
static int read_tag(const struct steam_pb_field *field)
{
	struct steam_pb_reader reader;
	struct steam_pb_field inner;
	int result, tags = 0;

	steam_pb_reader_init(&reader, field->data, field->size);
	while ((result = steam_pb_next(&reader, &inner)) > 0)
	{
		char tag[TAG_MAXIMUM];

		if (inner.number != 1 || inner.wire != STEAM_PB_BYTES || inner.size >= sizeof(tag))
			continue;
		memcpy(tag, inner.data, inner.size);
		tag[inner.size] = 0;
		if (!strcmp(tag, "Halo1"))
			tags |= STEAM_WORKSHOP_TAG_HALO1;
		else if (!strcmp(tag, "Campaign"))
			tags |= STEAM_WORKSHOP_TAG_CAMPAIGN;
		else if (!strcmp(tag, "Multiplayer"))
			tags |= STEAM_WORKSHOP_TAG_MULTIPLAYER;
	}
	return result < 0 ? -1 : tags;
}

/* a title kept as printable ASCII (other bytes '?', runs of UTF-8 one '?'), cut to the size */
static void keep_title(char *title, const uint8_t *data, size_t size)
{
	size_t in, out = 0;

	for (in = 0; in < size && out + 1 < STEAM_WORKSHOP_TITLE_SIZE; in++)
	{
		uint8_t c = data[in];

		if (c >= 0x20 && c < 0x7f)
			title[out++] = (char)c;
		else if (c >= 0xc0 || (c < 0x80 && c != 0))
			title[out++] = c < 0x80 ? ' ' : '?';
		/* (continuation bytes 0x80-0xbf and NUL dropped) */
	}
	title[out] = 0;
}

/* one PublishedFileDetails: 1, or -1 if malformed */
static int read_item(const struct steam_pb_field *field, struct steam_workshop_item *item)
{
	struct steam_pb_reader reader;
	struct steam_pb_field inner;
	int result, tag_count = 0;

	memset(item, 0, sizeof(*item));
	steam_pb_reader_init(&reader, field->data, field->size);
	while ((result = steam_pb_next(&reader, &inner)) > 0)
	{
		switch (inner.number)
		{
		case 1:
			if (inner.wire == STEAM_PB_VARINT)
				item->result = (uint32_t)inner.value;
			break;
		case 2:
			if (inner.wire == STEAM_PB_VARINT)
				item->id = inner.value;
			break;
		case 5:
			if (inner.wire == STEAM_PB_VARINT)
				item->consumer_app = (uint32_t)inner.value;
			break;
		case 8:
			if (inner.wire == STEAM_PB_VARINT)
				item->file_size = inner.value;
			break;
		case 14:
			if (inner.wire == STEAM_PB_FIXED64)
				item->hcontent = inner.value;
			break;
		case 16:
			if (inner.wire == STEAM_PB_BYTES)
				keep_title(item->title, inner.data, inner.size);
			break;
		case 20:
			if (inner.wire == STEAM_PB_VARINT)
				item->time_updated = (uint32_t)inner.value;
			break;
		case 21:
			if (inner.wire == STEAM_PB_VARINT)
				item->visibility = (uint32_t)inner.value;
			break;
		case 28:
			if (inner.wire == STEAM_PB_VARINT)
				item->banned = inner.value != 0;
			break;
		case 52:
			if (inner.wire == STEAM_PB_BYTES)
			{
				int tags;

				if (++tag_count > TAGS_MAXIMUM)
					return -1;
				tags = read_tag(&inner);
				if (tags < 0)
					return -1;
				item->tags |= tags;
			}
			break;
		default:
			break;
		}
	}
	return result < 0 ? -1 : 1;
}

static int parse_items(const uint8_t *body, size_t size, uint32_t items_field, struct steam_workshop_item *items,
	int maximum, uint32_t *total)
{
	struct steam_pb_reader reader;
	struct steam_pb_field field;
	int result, count = 0;

	if (total)
		*total = 0;
	if (maximum < 0)
		return -1;
	if (maximum > STEAM_WORKSHOP_ITEMS_MAXIMUM)
		maximum = STEAM_WORKSHOP_ITEMS_MAXIMUM;
	steam_pb_reader_init(&reader, body, size);
	while ((result = steam_pb_next(&reader, &field)) > 0)
	{
		if (field.number == items_field && field.wire == STEAM_PB_BYTES)
		{
			struct steam_workshop_item item;

			if (read_item(&field, &item) < 0)
				return -1;
			if (count < maximum)
				items[count++] = item;
		}
		else if (total && items_field == 2 && field.number == 1 && field.wire == STEAM_PB_VARINT)
			*total = (uint32_t)field.value;
	}
	return result < 0 ? -1 : count;
}

int steam_workshop_parse_query(const uint8_t *body, size_t size, struct steam_workshop_item *items, int maximum,
	uint32_t *total)
{
	return parse_items(body, size, 2, items, maximum, total);
}

int steam_workshop_parse_details(const uint8_t *body, size_t size, struct steam_workshop_item *items, int maximum)
{
	return parse_items(body, size, 1, items, maximum, NULL);
}

int steam_workshop_id_text(uint64_t id, char *text, size_t size)
{
	char digits[24];
	int length = snprintf(digits, sizeof(digits), "%llu", (unsigned long long)id);

	if (!id || length <= 0 || length > STEAM_WORKSHOP_ID_DIGITS || (size_t)length >= size)
		return 0;
	memcpy(text, digits, (size_t)length + 1);
	return 1;
}

int steam_workshop_id_parse(const char *text, uint64_t *id)
{
	uint64_t value = 0;
	size_t index;

	if (!text[0] || text[0] == '0')
		return 0;
	for (index = 0; text[index]; index++)
	{
		if (index >= STEAM_WORKSHOP_ID_DIGITS || text[index] < '0' || text[index] > '9')
			return 0;
		value = value * 10 + (uint64_t)(text[index] - '0');
	}
	*id = value;
	return 1;
}

int steam_workshop_item_ok(const struct steam_workshop_item *item, char *why, size_t why_size)
{
	char text[24];
	const char *reason = NULL;

	/* (EResult 1: OK) */
	if (item->result != 1)
		reason = "Steam has no such item";
	else if (item->consumer_app != STEAM_WORKSHOP_APP || !(item->tags & STEAM_WORKSHOP_TAG_HALO1))
		reason = "not a Halo CE item of MCC's Workshop";
	else if (item->banned)
		reason = "Steam has banned this item";
	else if (!item->hcontent)
		reason = "the item has no files (a collection?)";
	else if (!item->file_size || item->file_size > STEAM_WORKSHOP_ITEM_BYTES_MAXIMUM)
		reason = "the item is too large";
	else if (!steam_workshop_id_text(item->id, text, sizeof(text)))
		reason = "the item's number is too long for the mods folder";
	if (reason)
		snprintf(why, why_size, "%s", reason);
	return reason == NULL;
}

/* ---------- names */

static int name_character_ok(unsigned char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' ||
		c == '-' || c == '.' || c == '(' || c == ')' || c == '+' || c == ',' || c == '\'' || c == '&' || c == '!';
}

static int ends_with(const char *text, size_t length, const char *suffix)
{
	size_t suffix_length = strlen(suffix), index;

	if (length < suffix_length)
		return 0;
	for (index = 0; index < suffix_length; index++)
	{
		char c = text[length - suffix_length + index];

		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		if (c != suffix[index])
			return 0;
	}
	return 1;
}

static int base_is(const char *relative, const char *base)
{
	const char *slash = strrchr(relative, '/');
	const char *name = slash ? slash + 1 : relative;
	size_t index;

	for (index = 0; name[index] && base[index]; index++)
	{
		char c = name[index];

		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		if (c != base[index])
			return 0;
	}
	return !name[index] && !base[index];
}

int steam_workshop_name(const char *name, char *relative, size_t capacity, enum steam_plan_kind *kind)
{
	char path[STEAM_NAME_MAXIMUM + 1];
	size_t length = strnlen(name, STEAM_NAME_MAXIMUM + 1), index, part = 0;
	int parts = 0, odd = 0;

	/* a name that tries to leave the item's folder refuses the item: empty, too long, a leading '/', a drive, ':',
	control bytes, empty, "." and ".." parts (steam_plan_name_plain, after '\' becomes '/') */
	if (!length || length > STEAM_NAME_MAXIMUM)
		return STEAM_WORKSHOP_NAME_REFUSE;
	for (index = 0; index < length; index++)
		path[index] = name[index] == '\\' ? '/' : name[index];
	path[length] = 0;
	if (!steam_plan_name_plain(path))
		return STEAM_WORKSHOP_NAME_REFUSE;
	/* a name merely odd is not written (skipped, the item kept): longer than ours, too deep, a part too long, hidden
	(".git", "campaign/.json"), ending in '.' or ' ' (which Windows and the Vita trim), a character not ours (UTF-8,
	'*', '?', '"', '<', '>', '|', '%') */
	if (length > STEAM_WORKSHOP_NAME_MAXIMUM)
		odd = 1;
	for (index = 0; index <= length && !odd; index++)
	{
		unsigned char c = (unsigned char)path[index];

		if (c == '/' || c == 0)
		{
			size_t part_length = index - part;

			if (part_length > STEAM_WORKSHOP_NAME_PART_MAXIMUM || ++parts > STEAM_WORKSHOP_NAME_PARTS ||
				path[part] == '.' || path[part] == ' ' || path[index - 1] == '.' || path[index - 1] == ' ')
			{
				odd = 1;
			}
			part = index + 1;
			continue;
		}
		if (!name_character_ok(c))
			odd = 1;
	}
	if (odd)
		return STEAM_WORKSHOP_NAME_SKIP;
	if (ends_with(path, length, ".map"))
		*kind = base_is(path, "bitmaps.map") || base_is(path, "sounds.map") || base_is(path, "loc.map") ?
			STEAM_KIND_RESOURCE : STEAM_KIND_MCC_MAP;
	else if (ends_with(path, length, ".json") || ends_with(path, length, ".txt"))
		*kind = STEAM_KIND_WORKSHOP_TEXT;
	else if (ends_with(path, length, ".png"))
		*kind = STEAM_KIND_WORKSHOP_PNG;
	else if (ends_with(path, length, ".bmp"))
		*kind = STEAM_KIND_WORKSHOP_BMP;
	else
		return STEAM_WORKSHOP_NAME_SKIP;
	if (length + 1 > capacity)
		return STEAM_WORKSHOP_NAME_SKIP;
	memcpy(relative, path, length + 1);
	return STEAM_WORKSHOP_NAME_TAKE;
}

int steam_workshop_names_collide(const char *a, const char *b)
{
	size_t index;

	for (index = 0;; index++)
	{
		char x = a[index], y = b[index];

		if (x >= 'A' && x <= 'Z')
			x = (char)(x - 'A' + 'a');
		if (y >= 'A' && y <= 'Z')
			y = (char)(y - 'A' + 'a');
		if (x != y)
			return 0;
		if (!x)
			return 1;
	}
}

/* ---------- the install */

int steam_workshop_paths(const char *root, uint64_t id, char *staging, char *gid_path, char *done_path, char *mods,
	size_t size)
{
	char text[24];

	if (!steam_workshop_id_text(id, text, sizeof(text)))
		return 0;
	if ((staging && (size_t)snprintf(staging, size, "%s/workshop/%s", root, text) >= size) ||
		(gid_path && (size_t)snprintf(gid_path, size, "%s/workshop/%s.gid", root, text) >= size) ||
		(done_path && (size_t)snprintf(done_path, size, "%s/workshop/%s.done", root, text) >= size) ||
		(mods && (size_t)snprintf(mods, size, "%s/mods/%s", root, text) >= size))
	{
		return 0;
	}
	return 1;
}

int steam_workshop_staged(const char *root, uint64_t *ids, int maximum)
{
	char folder[512];
	DIR *directory;
	struct dirent *entry;
	int count = 0, seen = 0;

	if ((size_t)snprintf(folder, sizeof(folder), "%s/workshop", root) >= sizeof(folder))
		return 0;
	directory = opendir(folder);
	if (!directory)
		return 0;
	while ((entry = readdir(directory)) != NULL && count < maximum && seen++ < WALK_ENTRIES)
	{
		char digits[STEAM_WORKSHOP_ID_DIGITS + 1];
		size_t length = strlen(entry->d_name);
		uint64_t id;

		if (length <= 5 || length - 5 > STEAM_WORKSHOP_ID_DIGITS || strcmp(entry->d_name + length - 5, ".done"))
			continue;
		memcpy(digits, entry->d_name, length - 5);
		digits[length - 5] = 0;
		if (steam_workshop_id_parse(digits, &id))
			ids[count++] = id;
	}
	closedir(directory);
	return count;
}

static int is_directory(const char *path, int *directory)
{
	struct stat info;

#ifdef S_ISLNK
	if (lstat(path, &info) != 0)
		return 0;
	if (S_ISLNK(info.st_mode))
		return 0;
#else
	if (stat(path, &info) != 0)
		return 0;
#endif
	*directory = S_ISDIR(info.st_mode) != 0;
	return 1;
}

static int remove_walk(const char *path, int depth)
{
	DIR *directory;
	struct dirent *entry;
	int is_folder, ok = 1, seen = 0;

	if (depth > WALK_DEPTH + 2 || !is_directory(path, &is_folder))
		return 0;
	if (!is_folder)
		return remove(path) == 0;
	directory = opendir(path);
	if (!directory)
		return 0;
	while ((entry = readdir(directory)) != NULL)
	{
		char child[1024];

		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		if (++seen > WALK_ENTRIES || strchr(entry->d_name, '/') ||
			(size_t)snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >= sizeof(child))
		{
			ok = 0;
			break;
		}
		if (!remove_walk(child, depth + 1))
			ok = 0;
	}
	closedir(directory);
	return rmdir(path) == 0 && ok;
}

int steam_workshop_remove_tree(const char *path)
{
	int folder;

	if (!is_directory(path, &folder))
		return 1;
	return remove_walk(path, 0);
}

struct install_walk
{
	steam_workshop_map_check check;
	void *context;
	char *why;
	size_t why_size;
	int files, maps;
	uint64_t bytes;
	char seen[STEAM_WORKSHOP_FILES_MAXIMUM][STEAM_WORKSHOP_NAME_MAXIMUM + 1];
};

/* every file of the staging folder: a name the rule takes (as written), of its kind, every map by the game's check */
static int install_walk(struct install_walk *walk, const char *base, const char *relative, int depth)
{
	char folder[1024];
	DIR *directory;
	struct dirent *entry;
	int ok = 1, seen = 0;

	if (depth > STEAM_WORKSHOP_NAME_PARTS)
	{
		snprintf(walk->why, walk->why_size, "a folder too deep");
		return 0;
	}
	if ((size_t)snprintf(folder, sizeof(folder), "%s%s%s", base, relative[0] ? "/" : "", relative) >= sizeof(folder))
		return 0;
	directory = opendir(folder);
	if (!directory)
	{
		snprintf(walk->why, walk->why_size, "the downloaded folder cannot be read");
		return 0;
	}
	while (ok && (entry = readdir(directory)) != NULL)
	{
		char name[STEAM_WORKSHOP_NAME_MAXIMUM + 2], written[STEAM_WORKSHOP_NAME_MAXIMUM + 1], path[1200];
		enum steam_plan_kind kind;
		int child_is_folder, index;
		struct stat info;

		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		if (++seen > WALK_ENTRIES ||
			(size_t)snprintf(name, sizeof(name), "%s%s%s", relative, relative[0] ? "/" : "", entry->d_name) >=
				sizeof(name) - 1 ||
			(size_t)snprintf(path, sizeof(path), "%s/%s", base, name) >= sizeof(path) ||
			!is_directory(path, &child_is_folder))
		{
			snprintf(walk->why, walk->why_size, "an odd entry in the download");
			ok = 0;
			break;
		}
		if (child_is_folder)
		{
			if (depth + 1 >= STEAM_WORKSHOP_NAME_PARTS)
			{
				snprintf(walk->why, walk->why_size, "a folder too deep");
				ok = 0;
				break;
			}
			ok = install_walk(walk, base, name, depth + 1);
			continue;
		}
		if (steam_workshop_name(name, written, sizeof(written), &kind) != STEAM_WORKSHOP_NAME_TAKE ||
			strcmp(written, name) || walk->files >= (int)STEAM_WORKSHOP_FILES_MAXIMUM || stat(path, &info) != 0)
		{
			/* (a .part, a journal, a file the download never writes: the item is not as it was checked) */
			snprintf(walk->why, walk->why_size, "a file the download does not make: %.60s", name);
			ok = 0;
			break;
		}
		for (index = 0; index < walk->files; index++)
			if (steam_workshop_names_collide(walk->seen[index], name))
			{
				snprintf(walk->why, walk->why_size, "two files of one name: %.60s", name);
				ok = 0;
				break;
			}
		if (!ok)
			break;
		snprintf(walk->seen[walk->files++], sizeof(walk->seen[0]), "%s", name);
		walk->bytes += (uint64_t)info.st_size;
		if (walk->bytes > STEAM_WORKSHOP_ITEM_BYTES_MAXIMUM)
		{
			snprintf(walk->why, walk->why_size, "the item is too large");
			ok = 0;
			break;
		}
		if (kind == STEAM_KIND_MCC_MAP)
		{
			char reason[160];

			walk->maps++;
			reason[0] = 0;
			if (!walk->check || !walk->check(walk->context, path, reason, sizeof(reason)))
			{
				snprintf(walk->why, walk->why_size, "%.60s refused: %.120s", name, reason[0] ? reason : "no check");
				ok = 0;
				break;
			}
		}
	}
	closedir(directory);
	return ok;
}

int steam_workshop_install(const char *root, uint64_t id, steam_workshop_map_check check, void *context, char *why,
	size_t why_size)
{
	char staging[512], gid_path[512], done_path[512], mods[512], mods_folder[512];
	struct install_walk *walk;
	int ok;

	why[0] = 0;
	if (!steam_workshop_paths(root, id, staging, gid_path, done_path, mods, sizeof(staging)))
	{
		snprintf(why, why_size, "the item's number is too long");
		return 0;
	}
	walk = calloc(1, sizeof(*walk));
	if (!walk)
	{
		snprintf(why, why_size, "out of memory");
		return 0;
	}
	walk->check = check;
	walk->context = context;
	walk->why = why;
	walk->why_size = why_size;
	ok = install_walk(walk, staging, "", 0);
	if (ok && !walk->maps)
	{
		snprintf(why, why_size, "the item has no maps");
		ok = 0;
	}
	free(walk);
	if (ok)
	{
		snprintf(mods_folder, sizeof(mods_folder), "%s/mods", root);
		mkdir(mods_folder, 0777);
		/* (an item there before - an earlier version - goes first; the new one takes its place) */
		if (!steam_workshop_remove_tree(mods) || rename(staging, mods) != 0)
		{
			snprintf(why, why_size, "the item could not be moved into the mods folder");
			ok = 0;
		}
	}
	if (!ok)
		steam_workshop_remove_tree(staging);
	remove(done_path);
	remove(gid_path);
	return ok;
}
