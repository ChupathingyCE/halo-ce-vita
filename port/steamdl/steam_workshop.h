/*
STEAM_WORKSHOP.H

Halo: MCC's Steam Workshop items for Halo CE (app 976730, tag Halo1), listed and downloaded on the Vita through the
same signed-in Steam session as the depot download (docs/steam_download_security.md section 7): the list is
PublishedFile.QueryFiles over the CM (protobuf, no web page read), each chosen item's details PublishedFile.GetDetails,
its content the app's own Workshop depot (976730) - the depot key, the manifest's request code and the CDN as for
any depot, the manifest's gid the item's hcontent_file.

The download sequence follows what ChupathingyCE's Steam content client does (Milenko x Horrible Dev, MIT); the code
is ours, the field numbers Steam's published protobufs (SteamDatabase/Protobufs: steammessages_publishedfile).

Unlike a depot of the game's own files, a Workshop item's file names become paths: every name goes through
steam_workshop_name (a short list of folders' worth of plain names, five kinds of file, nothing else written), and
every finished item is checked again, map by map, by the game's own map check before it is moved into the mods
folder (steam_workshop_install). Nothing here holds a secret.

This half is plain C with no session (the parsers, the name rule, the request bodies, the install): the fuzzers and
the tests use it alone.
*/

#ifndef STEAM_WORKSHOP_H
#define STEAM_WORKSHOP_H

#include "steam_image.h"
#include "steam_plan.h"

#include <stddef.h>
#include <stdint.h>

/* the app whose Workshop it is, and the depot its items' content is in (the same number) */
#define STEAM_WORKSHOP_APP 976730u
#define STEAM_WORKSHOP_DEPOT 976730u

/* a page of the list (the browser's grid: four cards by two), and the most items one response is read for */
#define STEAM_WORKSHOP_PAGE_ITEMS 8
#define STEAM_WORKSHOP_ITEMS_MAXIMUM 50
/* the items one session downloads */
#define STEAM_WORKSHOP_CHOSEN_MAXIMUM 8
/* a title kept (steam_workshop_text: our font's characters, UTF-8; longer ones cut) and a search's text */
#define STEAM_WORKSHOP_TITLE_SIZE 128
#define STEAM_WORKSHOP_SEARCH_SIZE 64
/* an item: its files, its bytes in all, one text file's and one picture's */
#define STEAM_WORKSHOP_FILES_MAXIMUM 512u
#define STEAM_WORKSHOP_ITEM_BYTES_MAXIMUM (4ull << 30)
#define STEAM_WORKSHOP_TEXT_BYTES_MAXIMUM (1u << 20)
#define STEAM_WORKSHOP_PICTURE_BYTES_MAXIMUM (16u << 20)
/* a written name: its parts, one part's bytes, the whole */
#define STEAM_WORKSHOP_NAME_PARTS 4
#define STEAM_WORKSHOP_NAME_PART_MAXIMUM 64u
#define STEAM_WORKSHOP_NAME_MAXIMUM 160u
/* an item id: decimal, at most this many digits (the mods folder's key is at most 12 characters) */
#define STEAM_WORKSHOP_ID_DIGITS 12

/* the browser's text: an author's name, a description, the extra pictures of a details page, the maps named */
#define STEAM_WORKSHOP_AUTHOR_SIZE 48
#define STEAM_WORKSHOP_DESCRIPTION_SIZE 1600
#define STEAM_WORKSHOP_PREVIEWS 5
#define STEAM_WORKSHOP_MAP_NAMES 12
#define STEAM_WORKSHOP_MAP_NAME_SIZE 48
/* a text field read (a longer one is cut there first) */
#define STEAM_WORKSHOP_FIELD_MAXIMUM 16384u

/* the item's Steam tags we read */
#define STEAM_WORKSHOP_TAG_HALO1 1
#define STEAM_WORKSHOP_TAG_CAMPAIGN 2
#define STEAM_WORKSHOP_TAG_MULTIPLAYER 4

/* the list's order (EPublishedFileQueryType's: trend, newest, most votes, last updated); a search is by its text */
enum steam_workshop_sort
{
	STEAM_WORKSHOP_SORT_TREND,
	STEAM_WORKSHOP_SORT_NEW,
	STEAM_WORKSHOP_SORT_TOP,
	STEAM_WORKSHOP_SORT_UPDATED,
	STEAM_WORKSHOP_SORT_COUNT,
};
/* (the browser's fourth tab: the items the signed-in player subscribed to, on a PC or the Steam app - read only;
PublishedFile.GetUserFiles of the session's own account) */
#define STEAM_WORKSHOP_LIST_SUBSCRIBED STEAM_WORKSHOP_SORT_COUNT

struct steam_workshop_item
{
	uint64_t id;
	/* the item's content: its manifest's gid in depot 976730 (0: none, a collection) */
	uint64_t hcontent;
	uint64_t file_size;
	uint32_t result, consumer_app, time_updated, visibility;
	int banned;
	/* STEAM_WORKSHOP_TAG_* */
	int tags;
	char title[STEAM_WORKSHOP_TITLE_SIZE];
	/* (the browser's) the author's account, when made, the votes (score 0-100: Steam's, -1 none), the preview's URL
	as Steam gave it (checked only when fetched: steam_image_url) */
	uint64_t creator;
	uint32_t time_created, votes_up, votes_down;
	int score;
	char preview_url[STEAM_IMAGE_URL_MAXIMUM + 1];
};

/* an item's details page: its item, its description (steam_workshop_text, lines kept), its extra pictures' URLs (the
preview first) */
struct steam_workshop_details
{
	struct steam_workshop_item item;
	char description[STEAM_WORKSHOP_DESCRIPTION_SIZE];
	int preview_count;
	char previews[STEAM_WORKSHOP_PREVIEWS][STEAM_IMAGE_URL_MAXIMUM + 1];
};

/* (a details page) what an item's manifest holds, by the name rule (nothing downloaded but the manifest): its maps
(their names, the first STEAM_WORKSHOP_MAP_NAMES), the files taken and skipped, their bytes, or why it would be
refused */
struct steam_workshop_contents
{
	int maps, files, skipped, refused;
	uint64_t bytes;
	char map_names[STEAM_WORKSHOP_MAP_NAMES][STEAM_WORKSHOP_MAP_NAME_SIZE];
	char why[160];
};

/* ---------- requests (the bodies; their size, 0 if they do not fit) */

/* PublishedFile.QueryFiles#1: app 976730, tag Halo1, page (1 the first), STEAM_WORKSHOP_PAGE_ITEMS a page, details and
tags; a search (text not empty) ranks by its text */
size_t steam_workshop_query_request(uint8_t *buffer, size_t capacity, int page, enum steam_workshop_sort sort,
	const char *search);
/* PublishedFile.GetDetails#1 of these ids, with their tags */
size_t steam_workshop_details_request(uint8_t *buffer, size_t capacity, const uint64_t *ids, int count);
/* (a details page) PublishedFile.GetDetails#1 of one id: tags, votes, the extra pictures, the description without its
BBCode */
size_t steam_workshop_page_request(uint8_t *buffer, size_t capacity, uint64_t id);
/* (SUBSCRIBED) PublishedFile.GetUserFiles#1: the signed-in account's own (steamid: the session's, never typed) Halo
CE subscriptions, page (1 the first) of STEAM_WORKSHOP_PAGE_ITEMS, newest subscription first, with tags and votes */
size_t steam_workshop_subscribed_request(uint8_t *buffer, size_t capacity, uint64_t steamid, int page);
/* (the authors) Player.GetPlayerLinkDetails#1 of these accounts */
size_t steam_workshop_names_request(uint8_t *buffer, size_t capacity, const uint64_t *steamids, int count);

/* ---------- responses (hostile bytes: bounded) */

/* a QueryFiles (field 2, total in field 1) or GetDetails (field 1) response body: the items read (at most maximum),
or -1 if malformed. total may be NULL */
int steam_workshop_parse_query(const uint8_t *body, size_t size, struct steam_workshop_item *items, int maximum,
	uint32_t *total);
int steam_workshop_parse_details(const uint8_t *body, size_t size, struct steam_workshop_item *items, int maximum);
/* a GetUserFiles response (total in field 1, items in field 3) */
int steam_workshop_parse_subscribed(const uint8_t *body, size_t size, struct steam_workshop_item *items, int maximum,
	uint32_t *total);
/* a details page's GetDetails response: 1 with the item of that id, 0 if it is not there, -1 if malformed */
int steam_workshop_parse_page(const uint8_t *body, size_t size, uint64_t id, struct steam_workshop_details *details);
/* a GetPlayerLinkDetails response: each of these accounts' public name (steam_workshop_text; "" if none came):
the names found, -1 if malformed */
int steam_workshop_parse_names(const uint8_t *body, size_t size, const uint64_t *steamids, int count,
	char (*names)[STEAM_WORKSHOP_AUTHOR_SIZE]);

/* Steam's text (a title, a name, a description: anyone's bytes) made our font's: UTF-8 decoded strictly (a bad
sequence '?'), ASCII's printable characters and Latin-1's letters kept, typographic quotes, dashes and the ellipsis
made ASCII, control and format characters (bidi, zero-width) dropped, other characters '?' (a run of them one),
the game's text code '|' made '/', BBCode's [tags] left out (their words kept), blanks made one space; with lines,
a paragraph break at most one blank line; cut at a character's end to fit size. Never markup: nothing in it is
interpreted. The bytes written */
size_t steam_workshop_text(char *out, size_t size, const uint8_t *data, size_t length, int lines);

/* an item may be downloaded: 1, or 0 with the player's reason (Halo CE's Workshop, not banned, has content, not too
large, its id fits the mods folder's key) */
int steam_workshop_item_ok(const struct steam_workshop_item *item, char *why, size_t why_size);
/* the item's id as the mods folder's name: 1, 0 if it does not fit */
int steam_workshop_id_text(uint64_t id, char *text, size_t size);
/* a decimal id of 1 to STEAM_WORKSHOP_ID_DIGITS digits, no leading zero: 1 and the id */
int steam_workshop_id_parse(const char *text, uint64_t *id);

/* ---------- names */

enum
{
	/* a whole item is refused for a name that tries to leave its folder */
	STEAM_WORKSHOP_NAME_REFUSE = -1,
	/* a file of another kind: not written (logged) */
	STEAM_WORKSHOP_NAME_SKIP = 0,
	STEAM_WORKSHOP_NAME_TAKE = 1,
};

/* a manifest's file name (as it is, '\' or '/'): TAKE with the relative path to write ('/'-separated, the same bytes)
and its header check (STEAM_KIND_MCC_MAP for a map, STEAM_KIND_RESOURCE for bitmaps/sounds/loc.map,
STEAM_KIND_WORKSHOP_TEXT, _PNG, _BMP); SKIP (not written) for a file of another kind or a name merely odd (deeper
than STEAM_WORKSHOP_NAME_PARTS, a part longer than STEAM_WORKSHOP_NAME_PART_MAXIMUM, the whole longer than
STEAM_WORKSHOP_NAME_MAXIMUM, hidden, ending in '.' or ' ', a character not of [A-Za-z0-9 _.-()+,'&!]); REFUSE (the
whole item) for a name that tries to leave the folder: empty parts, ".", "..", a leading '/', a drive, ':', control
bytes. (Real items have names of 70 characters and "campaign/.json": skipped, not refused.) */
int steam_workshop_name(const char *name, char *relative, size_t capacity, enum steam_plan_kind *kind);
/* two names the same to a case-blind file system (the Vita's): 1 */
int steam_workshop_names_collide(const char *a, const char *b);

/* ---------- the install (the game's thread, after the session ended) */

/* the game's own check of a map file (custom_edition_cache_file_check): 1 if it may load, else 0 with why */
typedef int (*steam_workshop_map_check)(void *context, const char *path, char *why, size_t why_size);

/* the staging folder (<root>/workshop/<id>) and its marks: <id>.gid (the manifest being downloaded) and <id>.done
(every file in and checked); the mods folder's place <root>/mods/<id> */
int steam_workshop_paths(const char *root, uint64_t id, char *staging, char *gid_path, char *done_path, char *mods,
	size_t size);
/* the downloaded items waiting for their install (their .done marks): how many ids were written */
int steam_workshop_staged(const char *root, uint64_t *ids, int maximum);
/* the item checked again (every file's name; every map by check) and moved to <root>/mods/<id> (one there before
removed): 1, or 0 with the player's reason (the staging folder is then removed: a refused item is never kept) */
int steam_workshop_install(const char *root, uint64_t id, steam_workshop_map_check check, void *context, char *why,
	size_t why_size);
/* a folder and everything in it removed (only plain names; never follows a link): 1 */
int steam_workshop_remove_tree(const char *path);

/* (the browser) the item is in the mods folder (mods/<id>, a folder): 1 with the version the Workshop installed
(<root>/workshop/installed/<id>.gid: the manifest's gid; 0 if the folder came another way) */
int steam_workshop_installed(const char *root, uint64_t id, uint64_t *gid);
/* (the browser's REMOVE) the item's mods folder and its record removed: 1 (nothing there: 1 too) */
int steam_workshop_uninstall(const char *root, uint64_t id);

#endif
