#ifndef SNAPSHOTS_BTREE_H
#define SNAPSHOTS_BTREE_H

#include "snapshots/transaction_manager.h"

/*----------------------------------------------------------------*/

/*
 * Manipulates B+ trees with 64bit keys and 64bit values.
 */

/* Set up an empty tree.  O(1). */
int btree_empty(struct transaction_manager *tm, block_t *root);

/* Delete a tree.  O(n) - this is the slow one! */
int btree_del(struct transaction_manager *tm, block_t root);

/* FIXME: we need 3 lookup variants: lookup_exact, lookup_le, lookup_ge */
/* O(ln(n)) */
int btree_lookup(struct transaction_manager *tm, uint64_t key, block_t root,
		 uint64_t *result_key, uint64_t *result_value);

/* Insert a new key, or over write an existing value. O(ln(n)) */
int btree_insert(struct transaction_manager *tm, uint64_t key, uint64_t value, block_t root, block_t *new_root);

/* Remove a key if present. O(ln(n)) */
int btree_remove(struct transaction_manager *tm, uint64_t key, block_t root);

/* Clone a tree. O(1) */
int btree_clone(struct transaction_manager *tm, block_t root, block_t *clone);

/*----------------------------------------------------------------*/

/*
 * Debug only
 */
#include "snapshots/space_map.h"

/*
 * Walks the complete trees incrementing the reference counts in the space
 * map.  You can then compare with the space map in the transaction
 * manager.  This assumes no mutators are running.
 *
 * The btree code doesn't know how the leaf values are to be interpreted,
 * are they blocks to be referenced or something else ?  So |leaf_fn| is
 * passed in to make this decision.
 */
typedef void (*leaf_fn)(uint64_t leaf_value, struct space_map *sm);
int btree_walk(struct transaction_manager *tm, leaf_fn lf,
	       block_t root, struct space_map *sm);

/*----------------------------------------------------------------*/

#endif
