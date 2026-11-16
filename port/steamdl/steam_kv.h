/*
STEAM_KV.H

Valve's KeyValues text (PICS appinfo: what an app's depots and their public
manifests are): "key" "value" pairs and "key" { ... } blocks, quoted or bare
tokens, \" \\ \n \t escapes, // comments, [$CONDITION] tags (skipped).
Bounded: at most 1 MB of text, 32 levels, 65,536 nodes, 4096-byte tokens;
anything else is refused whole.
*/

#ifndef STEAM_KV_H
#define STEAM_KV_H

#include <stddef.h>
#include <stdint.h>

struct steam_kv_node
{
	const char *key;
	/* NULL for a block */
	const char *value;
	uint32_t first_child, next; /* 0: none (node 0 is the root, which holds the top level) */
};

struct steam_kv
{
	struct steam_kv_node *nodes;
	uint32_t count;
	char *strings;
};

/* 1, or 0 with a reason */
int steam_kv_parse(const char *text, size_t size, struct steam_kv *kv, char *error, int error_size);
void steam_kv_free(struct steam_kv *kv);

/* the node at a '/'-separated path of keys (any case) from the root, or
NULL */
const struct steam_kv_node *steam_kv_path(const struct steam_kv *kv, const char *path);
/* a child of a node by key (any case), or NULL */
const struct steam_kv_node *steam_kv_child(const struct steam_kv *kv, const struct steam_kv_node *node, const char *key);

/* PICS appinfo: a depot's manifest gid on a branch ("public"), from
appinfo/depots/<depot>/manifests/<branch>/gid (or the older
manifests/<branch> = "<gid>"): 1, or 0 */
int steam_kv_depot_manifest(const struct steam_kv *kv, uint32_t depot, const char *branch, uint64_t *gid);

#endif
