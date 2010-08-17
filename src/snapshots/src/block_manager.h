#ifndef	SNAPSHOTS_BLOCK_MANAGER_H
#define SNAPSHOTS_BLOCK_MANAGER_H

#include "snapshots/types.h"

#include <stdlib.h>

/*----------------------------------------------------------------*/

struct block_manager;

/*
 * |fd| must have been opened with the O_DIRECT flag set.
 */
struct block_manager *
block_manager_create(int fd, size_t block_size, block_t nr_blocks, unsigned cache_size);
void block_manager_destroy(struct block_manager *bm);

size_t bm_block_size(struct block_manager *bm);
block_t bm_nr_blocks(struct block_manager *bm);

enum block_lock {
	BM_LOCK_READ,
	BM_LOCK_WRITE
};

int bm_lock(struct block_manager *bm, block_t b, enum block_lock how, void **data);
int bm_lock_no_read(struct block_manager *bm, block_t b, enum block_lock how, void **data);
int bm_unlock(struct block_manager *bm, block_t b, int changed);
int bm_flush(struct block_manager *bm, int should_block);

/*
 * Debug
 */

/* Dumps some statistics to stdout */
void bm_dump(struct block_manager *bm);

unsigned bm_read_locks_held(struct block_manager *bm);
unsigned bm_write_locks_held(struct block_manager *bm);

int bm_start_io_trace(struct block_manager *bm, const char *file);
int bm_io_mark(struct block_manager *bm, const char *token);

/*----------------------------------------------------------------*/

#endif
