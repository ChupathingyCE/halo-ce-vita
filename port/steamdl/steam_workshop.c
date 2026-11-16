/*
STEAM_WORKSHOP.C

steam_workshop.h: the Workshop's requests, its responses read, its names' rule and the install.
Field numbers (SteamDatabase/Protobufs, steammessages_publishedfile.steamclient.proto):
  CPublishedFile_QueryFiles_Request: 1 query_type, 2 page, 3 numperpage, 5 appid, 6 requiredtags, 11 search_text,
    14 days, 18 return_tags, 37 return_details; its response 1 total, 2 publishedfiledetails
  CPublishedFile_GetDetails_Request: 1 publishedfileids (fixed64), 2 includetags, 14 appid; its response
    1 publishedfiledetails
  PublishedFileDetails: 1 result, 2 publishedfileid, 3 creator (fixed64), 5 consumer_appid, 8 file_size,
    11 preview_url, 14 hcontent_file (fixed64), 16 title, 17 file_description, 19 time_created, 20 time_updated,
    21 visibility, 28 banned, 51 previews (Preview: 3 url, 7 preview_type, 0 an image), 52 tags (Tag: 1 tag),
    55 vote_data (VoteData: 1 score (float), 2 votes_up, 3 votes_down)
  (the browser, docs/steam_download_security.md section 9) QueryFiles' 17 return_vote_data; GetDetails' 3
    includeadditionalpreviews, 6 includevotes, 15 strip_description_bbcode; CPublishedFile_GetUserFiles_Request:
    1 steamid (fixed64), 2 appid, 4 page, 5 numperpage, 6 type ("mysubscriptions"), 7 sortmethod, 10 requiredtags,
    19 return_vote_data, 20 return_tags; its response 1 total, 3 publishedfiledetails
  CPlayer_GetPlayerLinkDetails_Request: 1 steamids (repeated uint64); its response 1 accounts (PlayerLinkDetails:
    1 public_data (AccountPublicData: 1 steamid (fixed64), 10 persona_name))
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
	steam_pb_put_varint(&writer, 17, 1);
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

size_t steam_workshop_page_request(uint8_t *buffer, size_t capacity, uint64_t id)
{
	struct steam_pb_writer writer;

	if (!id)
		return 0;
	steam_pb_writer_init(&writer, buffer, capacity);
	steam_pb_put_fixed64(&writer, 1, id);
	steam_pb_put_varint(&writer, 2, 1);
	steam_pb_put_varint(&writer, 3, 1);
	steam_pb_put_varint(&writer, 6, 1);
	steam_pb_put_varint(&writer, 14, STEAM_WORKSHOP_APP);
	steam_pb_put_varint(&writer, 15, 1);
	return writer.error ? 0 : writer.size;
}

size_t steam_workshop_subscribed_request(uint8_t *buffer, size_t capacity, uint64_t steamid, int page)
{
	struct steam_pb_writer writer;

	if (!steamid || page < 1 || page > 1000)
		return 0;
	steam_pb_writer_init(&writer, buffer, capacity);
	steam_pb_put_fixed64(&writer, 1, steamid);
	steam_pb_put_varint(&writer, 2, STEAM_WORKSHOP_APP);
	steam_pb_put_varint(&writer, 4, (uint64_t)page);
	steam_pb_put_varint(&writer, 5, STEAM_WORKSHOP_PAGE_ITEMS);
	steam_pb_put_string(&writer, 6, "mysubscriptions");
	steam_pb_put_string(&writer, 7, "subscriptiondate");
	steam_pb_put_string(&writer, 10, "Halo1");
	steam_pb_put_varint(&writer, 19, 1);
	steam_pb_put_varint(&writer, 20, 1);
	return writer.error ? 0 : writer.size;
}

size_t steam_workshop_names_request(uint8_t *buffer, size_t capacity, const uint64_t *steamids, int count)
{
	struct steam_pb_writer writer;
	int index;

	if (count < 1 || count > STEAM_WORKSHOP_ITEMS_MAXIMUM)
		return 0;
	steam_pb_writer_init(&writer, buffer, capacity);
	for (index = 0; index < count; index++)
		steam_pb_put_varint(&writer, 1, steamids[index]);
	return writer.error ? 0 : writer.size;
}

/* ---------- Steam's text made ours */

/* the next character of strict UTF-8 at data[*at] (*at moved past it): the code point, or 0xFFFD for a bad sequence
(one byte taken) */
static uint32_t next_character(const uint8_t *data, size_t length, size_t *at)
{
	uint8_t c = data[*at];
	uint32_t value, minimum;
	size_t need, index;

	if (c < 0x80)
	{
		(*at)++;
		return c;
	}
	if (c >= 0xc2 && c <= 0xdf)
		need = 1, value = c & 0x1f, minimum = 0x80;
	else if (c >= 0xe0 && c <= 0xef)
		need = 2, value = c & 0x0f, minimum = 0x800;
	else if (c >= 0xf0 && c <= 0xf4)
		need = 3, value = c & 0x07, minimum = 0x10000;
	else
	{
		(*at)++;
		return 0xfffd;
	}
	if (*at + need >= length)
	{
		(*at)++;
		return 0xfffd;
	}
	for (index = 1; index <= need; index++)
	{
		uint8_t next = data[*at + index];

		if ((next & 0xc0) != 0x80)
		{
			(*at)++;
			return 0xfffd;
		}
		value = (value << 6) | (next & 0x3f);
	}
	if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
	{
		(*at)++;
		return 0xfffd;
	}
	*at += need + 1;
	return value;
}

/* a BBCode tag at data[at] ('[' name [=...] ']' or '[/' name ']', the name of letters, digits or '*', at most 12; the
whole at most 160 bytes): its length, else 0 */
static size_t bbcode_tag(const uint8_t *data, size_t length, size_t at)
{
	size_t index = at + 1, name = 0;

	if (index < length && data[index] == '/')
		index++;
	while (index < length && name < 12 && ((data[index] >= 'a' && data[index] <= 'z') ||
		(data[index] >= 'A' && data[index] <= 'Z') || (data[index] >= '0' && data[index] <= '9') || data[index] == '*'))
	{
		index++;
		name++;
	}
	if (!name || index >= length)
		return 0;
	if (data[index] == '=')
	{
		while (index < length && data[index] != ']' && data[index] != '[' && data[index] != '\n' && index - at < 160)
			index++;
	}
	return index < length && data[index] == ']' ? index - at + 1 : 0;
}

size_t steam_workshop_text(char *out, size_t size, const uint8_t *data, size_t length, int lines)
{
	size_t at = 0, used = 0;
	/* what is waiting to be written before the next visible character: a space, or line breaks */
	int space = 0, breaks = 0, unknown = 0;

	if (!size)
		return 0;
	out[0] = 0;
	if (!data)
		return 0;
	if (length > STEAM_WORKSHOP_FIELD_MAXIMUM)
		length = STEAM_WORKSHOP_FIELD_MAXIMUM;
	while (at < length)
	{
		char piece[4];
		size_t piece_length = 0, tag;
		uint32_t c;

		if (data[at] == '[' && (tag = bbcode_tag(data, length, at)) != 0)
		{
			at += tag;
			continue;
		}
		c = next_character(data, length, &at);
		if (c == '\n' && lines)
		{
			if (used)
				breaks = breaks < 2 ? breaks + 1 : 2;
			space = 0;
			unknown = 0;
			continue;
		}
		if (c == ' ' || c == '\t' || c == '\n' || c == 0xa0 || c == 0x3000 || (c >= 0x2000 && c <= 0x200a))
		{
			if (used)
				space = 1;
			unknown = 0;
			continue;
		}
		/* (control characters, C1, format characters: zero-width, bidi, the byte-order mark) */
		if (c < 0x20 || (c >= 0x7f && c < 0xa0) || c == 0xad || (c >= 0x200b && c <= 0x200f) ||
			(c >= 0x2028 && c <= 0x202f) || (c >= 0x2060 && c <= 0x206f) || c == 0xfeff || (c >= 0xfe00 && c <= 0xfe0f) ||
			(c >= 0xe0000 && c <= 0xe007f))
		{
			continue;
		}
		if (c == '|')
			c = '/';
		if (c < 0x80)
			piece[piece_length++] = (char)c;
		else if (c <= 0xff)
		{
			piece[piece_length++] = (char)(0xc0 | (c >> 6));
			piece[piece_length++] = (char)(0x80 | (c & 0x3f));
		}
		else if (c == 0x2018 || c == 0x2019 || c == 0x201a || c == 0x2032)
			piece[piece_length++] = '\'';
		else if (c == 0x201c || c == 0x201d || c == 0x201e || c == 0x2033)
			piece[piece_length++] = '"';
		else if (c == 0x2010 || c == 0x2011 || c == 0x2012 || c == 0x2013 || c == 0x2014 || c == 0x2212)
			piece[piece_length++] = '-';
		else if (c == 0x2026)
			piece[piece_length++] = '.', piece[piece_length++] = '.', piece[piece_length++] = '.';
		else if (c == 0x2022)
			piece[piece_length++] = (char)0xc2, piece[piece_length++] = (char)0xb7;
		else
		{
			/* (a character our font has not: one '?' for a run of them) */
			if (unknown)
				continue;
			unknown = 1;
			piece[piece_length++] = '?';
		}
		if (piece[0] != '?' || piece_length != 1)
			unknown = 0;
		if (used + (breaks ? (size_t)breaks : (size_t)space) + piece_length + 1 > size)
			break;
		while (breaks)
		{
			out[used++] = '\n';
			breaks--;
			space = 0;
		}
		if (space)
			out[used++] = ' ';
		space = 0;
		memcpy(out + used, piece, piece_length);
		used += piece_length;
	}
	out[used] = 0;
	return used;
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

/* a URL kept as Steam gave it (printable ASCII, no blank; else none): steam_image_url checks it when it is used */
static void keep_url(char *url, const uint8_t *data, size_t size)
{
	size_t index;

	url[0] = 0;
	if (size > STEAM_IMAGE_URL_MAXIMUM)
		return;
	for (index = 0; index < size; index++)
		if (data[index] <= 0x20 || data[index] >= 0x7f)
			return;
	memcpy(url, data, size);
	url[size] = 0;
}

/* VoteData: the score (a float 0-1, Steam's) as 0-100, the votes */
static int read_votes(const struct steam_pb_field *field, struct steam_workshop_item *item)
{
	struct steam_pb_reader reader;
	struct steam_pb_field inner;
	int result;

	steam_pb_reader_init(&reader, field->data, field->size);
	while ((result = steam_pb_next(&reader, &inner)) > 0)
	{
		if (inner.number == 1 && inner.wire == STEAM_PB_FIXED32)
		{
			uint32_t bits = (uint32_t)inner.value;
			float score;

			memcpy(&score, &bits, sizeof(score));
			/* (NaN and anything outside 0-1: none) */
			item->score = score >= 0.0f && score <= 1.0f ? (int)(score * 100.0f + 0.5f) : -1;
		}
		else if (inner.number == 2 && inner.wire == STEAM_PB_VARINT)
			item->votes_up = inner.value > 0xffffffffu ? 0xffffffffu : (uint32_t)inner.value;
		else if (inner.number == 3 && inner.wire == STEAM_PB_VARINT)
			item->votes_down = inner.value > 0xffffffffu ? 0xffffffffu : (uint32_t)inner.value;
	}
	return result < 0 ? -1 : 1;
}

/* a Preview: an image's URL (preview_type 0) into the details' list */
static int read_preview(const struct steam_pb_field *field, struct steam_workshop_details *details)
{
	struct steam_pb_reader reader;
	struct steam_pb_field inner;
	char url[STEAM_IMAGE_URL_MAXIMUM + 1];
	uint64_t type = 0;
	int result;

	url[0] = 0;
	steam_pb_reader_init(&reader, field->data, field->size);
	while ((result = steam_pb_next(&reader, &inner)) > 0)
	{
		if (inner.number == 3 && inner.wire == STEAM_PB_BYTES)
			keep_url(url, inner.data, inner.size);
		else if (inner.number == 7 && inner.wire == STEAM_PB_VARINT)
			type = inner.value;
	}
	if (result < 0)
		return -1;
	if (!type && url[0] && details->preview_count < STEAM_WORKSHOP_PREVIEWS)
	{
		int index;

		for (index = 0; index < details->preview_count; index++)
			if (!strcmp(details->previews[index], url))
				return 1;
		snprintf(details->previews[details->preview_count++], sizeof(details->previews[0]), "%s", url);
	}
	return 1;
}

/* one PublishedFileDetails: 1, or -1 if malformed; with details (a details page's), the description and the extra
pictures too */
static int read_item_with(const struct steam_pb_field *field, struct steam_workshop_item *item,
	struct steam_workshop_details *details)
{
	struct steam_pb_reader reader;
	struct steam_pb_field inner;
	int result, tag_count = 0, preview_count = 0;

	memset(item, 0, sizeof(*item));
	item->score = -1;
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
		case 3:
			if (inner.wire == STEAM_PB_FIXED64)
				item->creator = inner.value;
			break;
		case 5:
			if (inner.wire == STEAM_PB_VARINT)
				item->consumer_app = (uint32_t)inner.value;
			break;
		case 11:
			if (inner.wire == STEAM_PB_BYTES)
				keep_url(item->preview_url, inner.data, inner.size);
			break;
		case 17:
			if (inner.wire == STEAM_PB_BYTES && details)
				steam_workshop_text(details->description, sizeof(details->description), inner.data, inner.size, 1);
			break;
		case 19:
			if (inner.wire == STEAM_PB_VARINT)
				item->time_created = (uint32_t)inner.value;
			break;
		case 51:
			if (inner.wire == STEAM_PB_BYTES)
			{
				if (++preview_count > TAGS_MAXIMUM)
					return -1;
				if (details && read_preview(&inner, details) < 0)
					return -1;
			}
			break;
		case 55:
			if (inner.wire == STEAM_PB_BYTES && read_votes(&inner, item) < 0)
				return -1;
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
				steam_workshop_text(item->title, sizeof(item->title), inner.data, inner.size, 0);
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

static int read_item(const struct steam_pb_field *field, struct steam_workshop_item *item)
{
	return read_item_with(field, item, NULL);
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
		else if (total && items_field != 1 && field.number == 1 && field.wire == STEAM_PB_VARINT)
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

int steam_workshop_parse_subscribed(const uint8_t *body, size_t size, struct steam_workshop_item *items, int maximum,
	uint32_t *total)
{
	return parse_items(body, size, 3, items, maximum, total);
}

int steam_workshop_parse_page(const uint8_t *body, size_t size, uint64_t id, struct steam_workshop_details *details)
{
	struct steam_pb_reader reader;
	struct steam_pb_field field;
	int result, found = 0, read = 0;

	memset(details, 0, sizeof(*details));
	steam_pb_reader_init(&reader, body, size);
	while ((result = steam_pb_next(&reader, &field)) > 0)
	{
		struct steam_workshop_details *candidate;

		if (field.number != 1 || field.wire != STEAM_PB_BYTES)
			continue;
		if (++read > STEAM_WORKSHOP_ITEMS_MAXIMUM)
			return -1;
		candidate = calloc(1, sizeof(*candidate));
		if (!candidate)
			return -1;
		if (read_item_with(&field, &candidate->item, candidate) < 0)
		{
			free(candidate);
			return -1;
		}
		if (!found && candidate->item.id == id)
		{
			*details = *candidate;
			found = 1;
		}
		free(candidate);
	}
	if (result < 0)
	{
		memset(details, 0, sizeof(*details));
		return -1;
	}
	/* (the preview first among the pictures, once) */
	if (found && details->item.preview_url[0])
	{
		int index, at = -1;

		for (index = 0; index < details->preview_count; index++)
			if (!strcmp(details->previews[index], details->item.preview_url))
				at = index;
		if (at < 0)
		{
			if (details->preview_count == STEAM_WORKSHOP_PREVIEWS)
				details->preview_count--;
			memmove(details->previews[1], details->previews[0], (size_t)details->preview_count * sizeof(details->previews[0]));
			details->preview_count++;
		}
		else
			memmove(details->previews[1], details->previews[0], (size_t)at * sizeof(details->previews[0]));
		snprintf(details->previews[0], sizeof(details->previews[0]), "%s", details->item.preview_url);
	}
	return found;
}

/* an AccountPublicData: its account and name */
static int read_account(const struct steam_pb_field *field, uint64_t *steamid, char *name, size_t name_size)
{
	struct steam_pb_reader reader;
	struct steam_pb_field inner;
	int result;

	*steamid = 0;
	name[0] = 0;
	steam_pb_reader_init(&reader, field->data, field->size);
	while ((result = steam_pb_next(&reader, &inner)) > 0)
	{
		if (inner.number == 1 && (inner.wire == STEAM_PB_FIXED64 || inner.wire == STEAM_PB_VARINT))
			*steamid = inner.value;
		else if (inner.number == 10 && inner.wire == STEAM_PB_BYTES)
			steam_workshop_text(name, name_size, inner.data, inner.size, 0);
	}
	return result < 0 ? -1 : 1;
}

int steam_workshop_parse_names(const uint8_t *body, size_t size, const uint64_t *steamids, int count,
	char (*names)[STEAM_WORKSHOP_AUTHOR_SIZE])
{
	struct steam_pb_reader reader;
	struct steam_pb_field field;
	int result, index, found = 0, accounts = 0;

	for (index = 0; index < count; index++)
		names[index][0] = 0;
	steam_pb_reader_init(&reader, body, size);
	while ((result = steam_pb_next(&reader, &field)) > 0)
	{
		struct steam_pb_reader inner_reader;
		struct steam_pb_field inner;
		int inner_result;

		if (field.number != 1 || field.wire != STEAM_PB_BYTES)
			continue;
		if (++accounts > STEAM_WORKSHOP_ITEMS_MAXIMUM)
			return -1;
		steam_pb_reader_init(&inner_reader, field.data, field.size);
		while ((inner_result = steam_pb_next(&inner_reader, &inner)) > 0)
		{
			char name[STEAM_WORKSHOP_AUTHOR_SIZE];
			uint64_t steamid;

			if (inner.number != 1 || inner.wire != STEAM_PB_BYTES)
				continue;
			if (read_account(&inner, &steamid, name, sizeof(name)) < 0)
				return -1;
			for (index = 0; index < count; index++)
				if (steamid && steamids[index] == steamid && !names[index][0] && name[0])
				{
					snprintf(names[index], sizeof(names[index]), "%s", name);
					found++;
				}
		}
		if (inner_result < 0)
			return -1;
	}
	return result < 0 ? -1 : found;
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
	if (ok)
	{
		/* (the version installed, for the browser's UPDATE: the gid the .done mark names) */
		char record[512], folder[512], text[40];
		FILE *file = fopen(done_path, "rb");
		size_t length = 0;

		if (file)
		{
			length = fread(text, 1, sizeof(text) - 1, file);
			fclose(file);
		}
		text[length] = 0;
		snprintf(folder, sizeof(folder), "%s/workshop/installed", root);
		mkdir(folder, 0777);
		if (steam_workshop_paths(root, id, NULL, NULL, NULL, NULL, sizeof(record)) &&
			(size_t)snprintf(record, sizeof(record), "%s/%llu.gid", folder, (unsigned long long)id) < sizeof(record))
		{
			uint64_t gid = strtoull(text, NULL, 10);

			file = fopen(record, "wb");
			if (file)
			{
				fprintf(file, "%llu\n", (unsigned long long)gid);
				fclose(file);
			}
		}
	}
	if (!ok)
		steam_workshop_remove_tree(staging);
	remove(done_path);
	remove(gid_path);
	return ok;
}

static int record_path(const char *root, uint64_t id, char *path, size_t size)
{
	char text[24];

	return steam_workshop_id_text(id, text, sizeof(text)) &&
		(size_t)snprintf(path, size, "%s/workshop/installed/%s.gid", root, text) < size;
}

int steam_workshop_installed(const char *root, uint64_t id, uint64_t *gid)
{
	char mods[512], record[512], text[40];
	int folder;
	FILE *file;
	size_t length;

	*gid = 0;
	if (!steam_workshop_paths(root, id, NULL, NULL, NULL, mods, sizeof(mods)) || !is_directory(mods, &folder) ||
		!folder)
	{
		return 0;
	}
	if (record_path(root, id, record, sizeof(record)) && (file = fopen(record, "rb")) != NULL)
	{
		length = fread(text, 1, sizeof(text) - 1, file);
		fclose(file);
		text[length] = 0;
		*gid = strtoull(text, NULL, 10);
	}
	return 1;
}

int steam_workshop_uninstall(const char *root, uint64_t id)
{
	char mods[512], record[512];
	int ok;

	if (!steam_workshop_paths(root, id, NULL, NULL, NULL, mods, sizeof(mods)))
		return 0;
	ok = steam_workshop_remove_tree(mods);
	if (record_path(root, id, record, sizeof(record)))
		remove(record);
	return ok;
}
