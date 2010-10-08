#ifndef SNAPSHOTS_BTREE_INTERNAL_H
#define SNAPSHOTS_BTREE_INTERNAL_H

#include "datastruct/list.h"

/*----------------------------------------------------------------*/

/* FIXME: move all this into btree.c */

enum node_flags {
        INTERNAL_NODE = 1,
        LEAF_NODE = 1 << 1
};

/*
 * To ease coding I'm packing all the different node types into one
 * structure.  We can optimise later.
 */
struct node_header {
        uint32_t flags;
        uint32_t nr_entries;
	uint32_t max_entries;

	uint32_t padding;
};

typedef uint64_t snap_t;

struct node {
	struct node_header header;
	uint64_t keys[0];
};


/*
 * Based on the ideas in ["B-trees, Shadowing, and Clones" Ohad Rodeh]
 *
 * The btree is layered, at the top layer we have a node that points to the
 * block_map, and a tree of snapshots.
 *
 * The block map has internal nodes that are indexed by block, and
 */

/* FIXME: for now I've completely ignored endian issues in the disk format */
/* FIXME: enable close packing for on disk structures */

struct btree {
	struct block_manager *bm;
	struct transaction *transaction;
	struct space_map *sm;
};

void inc_children(struct btree_info *info, struct node *n, count_adjust_fn fn);

/*
 * The block_node struct ties together a block from the block manager and a node.
 */
enum bn_state {
	BN_UNLOCKED,
	BN_READ_LOCKED,
	BN_WRITE_LOCKED
};

struct block_node {
	enum bn_state state;
	block_t b;
	struct node *n;
};

int read_lock(struct btree_info *info, struct block_node *bn);
int shadow(struct btree_info *info, struct block_node *bn, count_adjust_fn fn, int *inc);
int new_block(struct btree_info *info, struct block_node *bn);
int unlock(struct btree_info *info, struct block_node *bn);

/*
 * Spines keep track of the rolling locks.  There are 2 variants, read-only
 * and one that uses shadowing.  These are separate structs to allow the
 * type checker to spot misuse, for example accidentally calling read_lock
 * on a shadow spine.
 */
struct ro_spine {
	struct btree_info *info;

	int count;
	struct block_node nodes[2];
};

void init_ro_spine(struct ro_spine *s, struct btree_info *info);
int exit_ro_spine(struct ro_spine *s);
int ro_step(struct ro_spine *s, block_t new_child);
struct node *ro_node(struct ro_spine *s);

struct shadow_spine {
	struct btree_info *info;

	int count;
	struct block_node nodes[2];

	block_t root;
};

void init_shadow_spine(struct shadow_spine *s, struct btree_info *info);
int exit_shadow_spine(struct shadow_spine *s);
int shadow_step(struct shadow_spine *s, block_t b, count_adjust_fn fn, int *inc);
struct block_node *shadow_current(struct shadow_spine *s);
struct block_node *shadow_parent(struct shadow_spine *s);
int shadow_root(struct shadow_spine *s);

/*----------------------------------------------------------------*/

#endif
