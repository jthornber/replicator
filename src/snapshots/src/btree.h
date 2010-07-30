#ifndef SNAPSHOTS_BTREE_H
#define SNAPSHOTS_BTREE_H

#include "snapshots/block_manager.h"

/*----------------------------------------------------------------*/

struct btree;

/* FIXME: move the next few functions to transaction manager */
struct btree *btree_create(struct block_manager *bm);
void btree_destroy(struct btree *bt);

int btree_begin(struct btree *bt);
int btree_commit(struct btree *bt);
int btree_rollback(struct btree *bt);
void btree_dump(struct btree *bt);

/* FIXME: these can stay */
int btree_new(struct btree *bt, block_t *new_root);
int btree_lookup(struct btree *bt, uint64_t key, block_t root,
		 uint64_t *result_key, uint64_t *result_value);

int btree_insert(struct btree *bt, uint64_t key, uint64_t value, block_t root, block_t *new_root);
int btree_clone(struct btree *bt, block_t root, block_t *clone);

/*----------------------------------------------------------------*/

#endif





#if 0
int remove(struct btree *bt)

int clone(struct btree *bt, block_t root);
int delete(struct btree *bt, block_t root); /* but is deleting the same as removing a sub tree from an internal node down ? */

/* we need to access the free space allocator directly in order to find somewhere for our cow data. */
/* FIXME: this isn't going to work with the lazy freeing scheme */
int allocate_block(struct btree *bt, block_t *b);
int free_block(struct btree *bt)
#endif
