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
};

typedef uint64_t snap_t;


/* MAX_ENTRIES must be an odd number */
#define MAX_ENTRIES 255

struct node {
	struct node_header header;

	uint64_t keys[MAX_ENTRIES];
	uint64_t values[MAX_ENTRIES];
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


/*----------------------------------------------------------------*/

#endif