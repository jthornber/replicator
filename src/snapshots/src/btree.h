#ifndef SNAPSHOTS_BTREE_H
#define SNAPSHOTS_BTREE_H

#include "snapshots/transaction_manager.h"

/*----------------------------------------------------------------*/

/*
 * Manipulates B+ trees with 64bit keys and 64bit values.  No assumptions
 * about the meanings of the keys or values are made.
 */

/* Set up an empty tree.  O(1). */
int btree_empty(struct transaction_manager *tm, block_t *root);

/* Delete a tree.  O(n) - this is the slow one! */
int btree_del(struct transaction_manager *tm, block_t root);

enum lookup_result {
	LOOKUP_ERROR,
	LOOKUP_NOT_FOUND,
	LOOKUP_FOUND
};

/* Tries to find a key that matches exactly.  O(ln(n)) */
enum lookup_result
btree_lookup_equal(struct transaction_manager *tm, block_t root,
		   uint64_t key, uint64_t *value);

/*
 * Find the greatest key that is less than or equal to that requested.  A
 * LOOKUP_NOT_FOUND result indicates the key would appear in front of all
 * (possibly zero) entries.  O(ln(n))
 */
enum lookup_result
btree_lookup_le(struct transaction_manager *tm, block_t root,
		uint64_t key, uint64_t *rkey, uint64_t *value);

/*
 * Find the least key that is greater than or equal to that requested.
 * LOOKUP_NOT_FOUND indicates all the keys are below.  O(ln(n))
 */
enum lookup_result
btree_lookup_ge(struct transaction_manager *tm, block_t root,
		uint64_t key, uint64_t *rkey, uint64_t *value);

/*
 * The btree has no idea what the values stored actually are.  This leads
 * to problems with reference counts when sharing is occuring.  So we have
 * to pass in a function to adjust reference counts to many of the btree
 * functions.  Often the leaves are simply block ids, so here's a standard
 * function for that case.
 */
typedef void (*count_adjust_fn)(struct transaction_manager *tm, uint64_t value, int32_t count_delta);
void value_is_block(struct transaction_manager *tm, uint64_t value, int32_t delta);
void value_is_meaningless(struct transaction_manager *tm, uint64_t value, int32_t delta);

/*
 * Insertion (or overwrite an existing value).
 * O(ln(n))
 */
int btree_insert(struct transaction_manager *tm, block_t root, count_adjust_fn fn,
		 uint64_t key, uint64_t value,
		 block_t *new_root);

/* Remove a key if present. O(ln(n)) */
int btree_remove(struct transaction_manager *tm, block_t root, count_adjust_fn fn,
		 uint64_t key, block_t *new_root);

/* Clone a tree. O(1) */
int btree_clone(struct transaction_manager *tm, block_t root, count_adjust_fn fn,
		block_t *clone);

/*
 * Hierarchical btrees.  We often have btrees of btrees, these utilities
 * make it a bit easier to manipulate them.  |keys| is an array with an
 * entry for each level of the hierarchy.
 */
int btree_lookup_equal_h(struct transaction_manager *tm, block_t root,
			 uint64_t *keys, unsigned levels,
			 uint64_t *value);

int btree_lookup_le_h(struct transaction_manager *tm, block_t root,
		      uint64_t *keys, unsigned levels,
		      uint64_t *key, uint64_t *value);

int btree_lookup_ge_h(struct transaction_manager *tm, block_t root,
		      uint64_t *keys, unsigned levels,
		      uint64_t *key, uint64_t *value);

int btree_insert_h(struct transaction_manager *tm, block_t root, count_adjust_fn fn,
		   uint64_t *keys, unsigned levels,
		   uint64_t value, block_t *new_root);

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
 * If there is any sharing between btrees you must ensure these roots are
 * walked within a single call to btree_walk.
 *
 * The btree code doesn't know how the leaf values are to be interpreted,
 * are they blocks to be referenced or something else ?  So |leaf_fn| is
 * passed in to make this decision.
 */
typedef void (*leaf_fn)(uint64_t leaf_value, uint32_t *ref_counts);
int btree_walk(struct transaction_manager *tm, leaf_fn lf,
	       block_t *roots, unsigned count, uint32_t *ref_counts);

int btree_walk_h(struct transaction_manager *tm, leaf_fn lf,
		 block_t *roots, unsigned count,
		 unsigned levels, uint32_t *ref_counts);

/*----------------------------------------------------------------*/

#endif
