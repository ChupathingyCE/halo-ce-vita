/*
STEAM_KV.C

steam_kv.h: KeyValues text into a bounded tree.
*/

#include "steam_kv.h"

#include "steam_limits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct lexer
{
	const char *text;
	size_t size, cursor;
	char *strings;
	size_t strings_used, strings_capacity;
};

enum
{
	TOKEN_END,
	TOKEN_STRING,
	TOKEN_OPEN,
	TOKEN_CLOSE,
	TOKEN_ERROR,
};

static void skip_space(struct lexer *lexer)
{
	for (;;)
	{
		while (lexer->cursor < lexer->size && (lexer->text[lexer->cursor] == ' ' || lexer->text[lexer->cursor] == '\t' ||
			lexer->text[lexer->cursor] == '\r' || lexer->text[lexer->cursor] == '\n'))
		{
			lexer->cursor++;
		}
		if (lexer->cursor + 1 < lexer->size && lexer->text[lexer->cursor] == '/' && lexer->text[lexer->cursor + 1] == '/')
		{
			while (lexer->cursor < lexer->size && lexer->text[lexer->cursor] != '\n')
				lexer->cursor++;
			continue;
		}
		return;
	}
}

/* the next token; a string is copied (unescaped) into the strings */
static int next_token(struct lexer *lexer, const char **string)
{
	size_t start, length = 0;
	char c;

	skip_space(lexer);
	/* (the text may end with a NUL) */
	if (lexer->cursor >= lexer->size || !lexer->text[lexer->cursor])
		return TOKEN_END;
	c = lexer->text[lexer->cursor];
	if (c == '{' || c == '}')
	{
		lexer->cursor++;
		return c == '{' ? TOKEN_OPEN : TOKEN_CLOSE;
	}
	start = lexer->strings_used;
	if (c == '"')
	{
		lexer->cursor++;
		for (;;)
		{
			if (lexer->cursor >= lexer->size || !lexer->text[lexer->cursor])
				return TOKEN_ERROR;
			c = lexer->text[lexer->cursor++];
			if (c == '"')
				break;
			if (c == '\\')
			{
				if (lexer->cursor >= lexer->size)
					return TOKEN_ERROR;
				c = lexer->text[lexer->cursor++];
				c = c == 'n' ? '\n' : c == 't' ? '\t' : c;
			}
			if (length >= STEAM_KV_TOKEN_MAXIMUM || lexer->strings_used + 1 >= lexer->strings_capacity)
				return TOKEN_ERROR;
			lexer->strings[lexer->strings_used++] = c;
			length++;
		}
	}
	else
	{
		while (lexer->cursor < lexer->size)
		{
			c = lexer->text[lexer->cursor];
			if (!c || c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '{' || c == '}' || c == '"')
				break;
			if (length >= STEAM_KV_TOKEN_MAXIMUM || lexer->strings_used + 1 >= lexer->strings_capacity)
				return TOKEN_ERROR;
			lexer->strings[lexer->strings_used++] = c;
			length++;
			lexer->cursor++;
		}
	}
	if (lexer->strings_used >= lexer->strings_capacity)
		return TOKEN_ERROR;
	lexer->strings[lexer->strings_used++] = 0;
	*string = lexer->strings + start;
	return TOKEN_STRING;
}

/* a [$CONDITION] after a token: skipped */
static void skip_condition(struct lexer *lexer)
{
	skip_space(lexer);
	if (lexer->cursor < lexer->size && lexer->text[lexer->cursor] == '[')
	{
		size_t end = lexer->cursor;

		while (end < lexer->size && lexer->text[end] != ']' && lexer->text[end] != '\n' && end - lexer->cursor < 256)
			end++;
		if (end < lexer->size && lexer->text[end] == ']')
			lexer->cursor = end + 1;
	}
}

int steam_kv_parse(const char *text, size_t size, struct steam_kv *kv, char *error, int error_size)
{
	struct lexer lexer;
	uint32_t parents[STEAM_KV_DEPTH_MAXIMUM + 1], last_child[STEAM_KV_DEPTH_MAXIMUM + 1];
	uint32_t capacity;
	int depth = 0;

	memset(kv, 0, sizeof(*kv));
	error[0] = 0;
	if (!text || size > STEAM_KV_TEXT_MAXIMUM)
	{
		snprintf(error, (size_t)error_size, "KeyValues text too large");
		return 0;
	}
	memset(&lexer, 0, sizeof(lexer));
	lexer.text = text;
	lexer.size = size;
	/* (every string fits in the text's length, plus a NUL for each token) */
	lexer.strings_capacity = size * 2 + 2;
	lexer.strings = malloc(lexer.strings_capacity);
	/* (a node is at least 2 bytes of text, "a{": never more than size / 2 + 2) */
	capacity = (uint32_t)(size / 2 + 2 < STEAM_KV_NODES_MAXIMUM ? size / 2 + 2 : STEAM_KV_NODES_MAXIMUM);
	kv->nodes = calloc(capacity, sizeof(*kv->nodes));
	kv->strings = lexer.strings;
	if (!lexer.strings || !kv->nodes)
	{
		snprintf(error, (size_t)error_size, "out of memory");
		steam_kv_free(kv);
		return 0;
	}
	kv->count = 1;
	parents[0] = 0;
	last_child[0] = 0;
	for (;;)
	{
		const char *key = NULL, *value = NULL;
		int token = next_token(&lexer, &key);
		uint32_t node;

		if (token == TOKEN_END)
		{
			if (depth)
				goto bad;
			return 1;
		}
		if (token == TOKEN_CLOSE)
		{
			if (!depth)
				goto bad;
			depth--;
			continue;
		}
		if (token != TOKEN_STRING)
			goto bad;
		skip_condition(&lexer);
		token = next_token(&lexer, &value);
		if (token != TOKEN_STRING && token != TOKEN_OPEN)
			goto bad;
		if (kv->count >= capacity)
		{
			snprintf(error, (size_t)error_size, "KeyValues text has too many entries");
			steam_kv_free(kv);
			return 0;
		}
		node = kv->count++;
		kv->nodes[node].key = key;
		kv->nodes[node].value = token == TOKEN_STRING ? value : NULL;
		if (last_child[depth])
			kv->nodes[last_child[depth]].next = node;
		else
			kv->nodes[parents[depth]].first_child = node;
		last_child[depth] = node;
		if (token == TOKEN_STRING)
			skip_condition(&lexer);
		else
		{
			if (depth >= STEAM_KV_DEPTH_MAXIMUM)
			{
				snprintf(error, (size_t)error_size, "KeyValues text nested too deep");
				steam_kv_free(kv);
				return 0;
			}
			depth++;
			parents[depth] = node;
			last_child[depth] = 0;
		}
	}
bad:
	snprintf(error, (size_t)error_size, "KeyValues text malformed");
	steam_kv_free(kv);
	return 0;
}

void steam_kv_free(struct steam_kv *kv)
{
	free(kv->nodes);
	free(kv->strings);
	memset(kv, 0, sizeof(*kv));
}

static int same_key(const char *a, const char *b)
{
	for (;; a++, b++)
	{
		char x = *a, y = *b;

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

const struct steam_kv_node *steam_kv_child(const struct steam_kv *kv, const struct steam_kv_node *node, const char *key)
{
	uint32_t index, steps = 0;

	if (!kv->nodes || !node || node->value)
		return NULL;
	for (index = node->first_child; index && index < kv->count && steps < kv->count; index = kv->nodes[index].next, steps++)
		if (same_key(kv->nodes[index].key, key))
			return &kv->nodes[index];
	return NULL;
}

const struct steam_kv_node *steam_kv_path(const struct steam_kv *kv, const char *path)
{
	const struct steam_kv_node *node = kv->nodes ? &kv->nodes[0] : NULL;
	char part[128];

	while (node && *path)
	{
		size_t length = strcspn(path, "/");

		if (length >= sizeof(part))
			return NULL;
		memcpy(part, path, length);
		part[length] = 0;
		node = steam_kv_child(kv, node, part);
		path += length;
		if (*path == '/')
			path++;
	}
	return node;
}

static int parse_u64(const char *text, uint64_t *value)
{
	uint64_t result = 0;
	int digits = 0;

	for (; *text; text++, digits++)
	{
		if (*text < '0' || *text > '9' || digits >= 20 || result > (UINT64_MAX - (uint64_t)(*text - '0')) / 10)
			return 0;
		result = result * 10 + (uint64_t)(*text - '0');
	}
	*value = result;
	return digits > 0;
}

int steam_kv_depot_manifest(const struct steam_kv *kv, uint32_t depot, const char *branch, uint64_t *gid)
{
	const struct steam_kv_node *manifest;
	char path[128];

	snprintf(path, sizeof(path), "appinfo/depots/%u/manifests/%s", (unsigned int)depot, branch);
	manifest = steam_kv_path(kv, path);
	if (!manifest)
		return 0;
	if (manifest->value)
		return parse_u64(manifest->value, gid);
	manifest = steam_kv_child(kv, manifest, "gid");
	return manifest && manifest->value && parse_u64(manifest->value, gid);
}
