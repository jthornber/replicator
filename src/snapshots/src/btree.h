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
/*
 * A successful lookup will retain a read lock on the leaf node.  Make sure
 * you release it.
 */
int btree_lookup_exact(struct transaction_manager *tm,
		       block_t root, uint64_t key,
		       uint64_t *value);

int btree_lookup_le(struct transaction_manager *tm,
		    block_t root, uint64_t key,
		    uint64_t *key, uint64_t *value);

int btree_lookup_ge(struct transaction_manager *tm,
		    block_t root, uint64_t key,
		    uint64_t *key, uint64_t *value);

#if 0
int btree_lookup(struct transaction_manager *tm, uint64_t key, block_t root,
		 uint64_t *result_key, uint64_t *result_value);
#endif

/* Insert a new key, or over write an existing value. O(ln(n)) */
/* FIXME: make param position of |root| consistent */
int btree_insert(struct transaction_manager *tm, uint64_t key, uint64_t value,
		 block_t root, block_t *new_root);

/* Remove a key if present. O(ln(n)) */
int btree_remove(struct transaction_manager *tm, uint64_t key, block_t root);

/* Clone a tree. O(1) */
int btree_clone(struct transaction_manager *tm, block_t root, block_t *clone);

/*
 * Hierarchical btrees.  We often have btrees of btrees, these utilities
 * make it a bit easier to manipulate them.
 */
int btree_insert_h(struct transaction_manager *tm, block_t root,
		   uint64_t *keys, unsigned levels,
		   uint64_t value, block_t *new_root);

int btree_lookup_exact_h(struct transaction_manager *tm,
			 block_t root, uint64_t *keys, unsigned levels,
			 uint64_t *value);

int btree_lookup_le_h(struct transaction_manager *tm,
		      block_t root, uint64_t *keys, unsigned levels,
		      uint64_t *key, uint64_t *value);

int btree_lookup_ge_h(struct transaction_manager *tm,
		      block_t root, uint64_t *keys, unsigned levels,
		      uint64_t *key, uint64_t *value);

/*----------------------------------------------------------------*/

/*
 * Debug only
 */
#include "snapshots/space_map.h"

/*
 * Walks the complete tree incrementing the reference counts in the space
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

int btree_walk_h(struct transaction_manager *tm, leaf_fn lf,
		 block_t root, unsigned count, struct space_map *sm);

/*----------------------------------------------------------------*/

#endif
