#ifndef SNAPSHOTS_BTREE_H
#define SNAPSHOTS_BTREE_H

struct btree;

struct btree *btree_create(struct block_io *bio);
void btree_destroy(struct btree *bt);

/*
 * FIXME: we need to be able to change the type of the keys at different levels.
 * Or to put it another way - we need recursive btrees (btrees of btrees).
 */

int lookup(struct btree *bt, ???);
int insert(struct btree *bt, void *key, size_t len, void *value);   /* this could just be a block number */
int remove(struct btree *bt)

int clone(struct btree *bt, block_t root);
int delete(struct btree *bt, block_t root);  // but is deleting the same as removing a sub tree from an internal node down ?

/* we need to access the free space allocator directly in order to find somewhere for our cow data. */
/* FIXME: this isn't going to work with the lazy freeing scheme */
int allocate_block(struct btree *bt, block_t *b);
int free_block(struct btree *bt)

#endif