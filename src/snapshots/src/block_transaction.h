#ifndef SNAPSHOTS_BLOCK_TRANSACTION_H
#define SNAPSHOTS_BLOCK_TRANSACTION_H

/*----------------------------------------------------------------*/

struct block_allocator;

struct block_allocator *block_allocator_create(dev_t dev, uint32_t block_size);
void block_allocator_destroy(struct block_allocator *ba);

int block_allocator_begin(struct block_allocator *ba);

int block_allocator_new_block(struct block_allocator *ba, block_t *result);

/*
 * Often we just want to make a small tweak to the data in a block.
 * Because we're using persistent data structures we have to allocate a new block, copy the data including the 


int block_allocator_mutate_block(struct block_allocator *ba, block_t old_block);

int commit_transaction();

/*----------------------------------------------------------------*/

#endif
