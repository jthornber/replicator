#ifndef SNAPSHOTS_SPACE_MAP_H
#define SNAPSHOTS_SPACE_MAP_H

#include "snapshots/types.h"
#include "snapshots/transaction_manager.h"

/*----------------------------------------------------------------*/

/*
 * This structure keeps a record of how many times each block in a device
 * is referenced.  It needs to be persisted to disk as part of the
 * transaction.
 *
 * Writing the space map is a challenge.  It is used extensively by the
 * transaction manager, but we'd also like to implement its on-disk format
 * using the standard data structures such as the btree.  So we can easily
 * get into cycles.  For example:
 *
 * snapshot -> btree -> tm_shadow -> space_map_alloc -> btree -> shadow -> space_map alloc
 *
 * How do we break this cycle?  Have 2 modes for the space map to operate
 * in: IN_CORE and then a FLUSH_TO_DISK.  This just defers the cycle,
 * instead of being triggered by the snapshot, it'll be hit when the flush
 * is done:
 *
 * sm_flush -> btree -> tm_shadow -> space_map_alloc
 *
 * We get round this by (internally) running flush to write the allocations
 * caused by the snap client.  Then again to write the allocations caused
 * by the first flush etc.  Then again to write allocations from second
 * flush etc.  A good on disk format will be one that minimises the number
 * of cycles (possibly not a btree).
 */

struct space_map_ops {
	void (*destroy)(void *context);

	/*
	 * These should not write to disk, just jot down the changes in
	 * ram.
	 */
	int (*new_block)(void *context, block_t *b);
	int (*inc_block)(void *context, block_t b);
	int (*dec_block)(void *context, block_t b);
	int (*get_count)(void *context, block_t b, uint32_t *result);

	/*
	 * Flushes all changes to disk.  Fills out the root of the new on
	 * disk space map, so the caller can make a note of it in their top
	 * level node.
	 */
	int (*flush)(void *context, block_t *new_root);
};

struct space_map {
	struct space_map_ops *ops;
	void *context;
};

/*----------------------------------------------------------------*/

static inline void sm_destroy(struct space_map *sm) {
	sm->ops->destroy(sm->context);
}

static inline int sm_new_block(struct space_map *sm, block_t *b) {
	return sm->ops->new_block(sm->context, b);
}

static inline int sm_inc_block(struct space_map *sm, block_t b) {
	return sm->ops->inc_block(sm->context, b);
}

static inline int sm_dec_block(struct space_map *sm, block_t b) {
	return sm->ops->dec_block(sm->context, b);
}

static inline int sm_get_count(struct space_map *sm, block_t b, uint32_t *result) {
	return sm->ops->get_count(sm->context, b, result);
}

static inline int sm_flush(struct space_map *sm, block_t *new_root) {
	return sm->ops->flush(sm->context, new_root);
}

/*----------------------------------------------------------------*/

/* combined sm specific stuff */
/* Create a new space smap */
struct space_map *sm_new(struct transaction_manager *tm,
			 block_t nr_blocks);

/* Open a pre-existing space map at the given root */
/* FIXME: remove the nr_blocks arg */
struct space_map *sm_open(struct transaction_manager *tm,
			  block_t root, block_t nr_blocks);

/*
 * These can access the disk structures, but only to read the existing
 * layout.  In in core journal notes what reference count deltas have been
 * made.  You must call flush if you want it to hit the disk.
 */

/*----------------------------------------------------------------*/

/* debug */
int sm_walk(struct space_map *sm, uint32_t *ref_counts);

/*----------------------------------------------------------------*/

#endif
