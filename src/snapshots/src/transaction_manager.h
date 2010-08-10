#ifndef SNAPSHOTS_TRANSACTION_MANAGER_H
#define SNAPSHOTS_TRANSACTION_MANAGER_H

#include "snapshots/block_manager.h"

/*----------------------------------------------------------------*/

/*
 * This manages the scope of a transaction.  It also enforces immutability
 * of the on-disk data structures by limiting access to writeable blocks.
 *
 * Clients should not fiddle with the block manager directly.
 */
struct transaction_manager;

struct transaction_manager *tm_create(struct block_manager *bm);
void tm_destroy(struct transaction_manager *tm);

/*
 * The client may want to manage some blocks directly (eg, the
 * superblocks).  Call this immediately after construction to reserve
 * blocks.
 */
int tm_reserve_block(struct transaction_manager *tm, block_t b);

int tm_begin(struct transaction_manager *tm);

/*
 * We use a 2 phase commit here.
 *
 * i) In the first phase the block manager is told to start flushing, and
 * the changes to the space map are written to disk.  The new root block
 * for the space map is returned for inclusion in the clients superblock.
 *
 * ii) |root| will be committed last.  You shouldn't use more than the
 * first 512 bytes of |root| if you wish to survive a power failure.  You
 * *must* have a write lock held on |root|.  The commit will drop the write
 * lock.
 */
int tm_pre_commit(struct transaction_manager *tm, block_t *space_map_root);
int tm_commit(struct transaction_manager *tm, block_t root);

/* FIXME: not implemented, possibly not needed. */
int tm_rollback(struct transaction_manager *tm);

/*
 * These methods are the only way to get hold of a writeable block.
 *
 * tm_new_block() is pretty self explanatory.  Make sure you do actually
 * write to the whole of |data| before you unlock, otherwise you could get
 * a data leak.  (The other option is for tm_new_block() to zero new blocks
 * before handing them out, which will be redundant in most if not all
 * cases).
 *
 * tm_shadow_block() will allocate a new block and copy the data from orig
 * to it.  Because the tm knows the scope of the transaction it can
 * optimise requests for a shadow of a shadow to a no-op.
 *
 * The |inc_children| flag is used to tell the caller whether they need to
 * adjust reference counts for children.
 */
int tm_alloc_block(struct transaction_manager *tm, block_t *new);
int tm_new_block(struct transaction_manager *tm, block_t *new, void **data);
int tm_shadow_block(struct transaction_manager *tm, block_t orig,
		    block_t *copy, void **data, int *inc_children);
int tm_write_unlock(struct transaction_manager *tm, block_t block);

/*
 * Read access.  You can lock any block you want, if there's a write lock
 * on it outstanding then it'll block.
 */
int tm_read_lock(struct transaction_manager *tm, block_t b, void **data);
int tm_read_unlock(struct transaction_manager *tm, block_t b);

/*
 * Functions for altering the reference count of a block.
 */
void tm_inc(struct transaction_manager *tm, block_t b);
void tm_dec(struct transaction_manager *tm, block_t b);
uint32_t tm_ref(struct transaction_manager *tm, block_t b);

/*----------------------------------------------------------------*/

/*
 * Debug code.
 */
#include "snapshots/space_map.h"

struct space_map *tm_get_sm(struct transaction_manager *tm);
struct block_manager *tm_get_bm(struct transaction_manager *tm);

/*----------------------------------------------------------------*/

#endif
