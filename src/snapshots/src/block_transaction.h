#ifndef SNAPSHOTS_BLOCK_TRANSACTION_H
#define SNAPSHOTS_BLOCK_TRANSACTION_H

/*----------------------------------------------------------------*/

/*
 * The block allocator manages provides a transactional interface to
 * the underlying device.  It does this by maintaining a mapping between
 * physical blocks, and logical blocks.
 */
struct block_allocator;

/* Separate structure so the type-checker can distinguish between physical and logical blocks */
struct logical_block {
	uint64_t block;
};

struct block_allocator *ba_create(dev_t dev, uint32_t block_size);
void ba_destroy(struct block_allocator *ba);

int ba_commit(struct block_allocator *ba);
int ba_rollback(struct block_allocator *ba);
int ba_begin(struct block_allocator *ba);

int ba_allocate(struct block_allocator *ba, struct logical_block *result);

/*
 * Try and write only the actual data that has changed, this gives the allocator
 * more freedom when it comes to optimising the commit.  eg, it may just
 * journal the delta rather than doing a copy and update of the block.
 */
int ba_write(struct block_allocator *ba, struct logical_block b,
             size_t offset, size_t len, const void *data);
int ba_read(struct block_allocator *ba, struct logical_block b,
      	    size_t offset, size_t len, void *data);

/*----------------------------------------------------------------*/

#endif
