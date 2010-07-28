#ifndef	SNAPSHOTS_BLOCK_MANAGER_H
#define SNAPSHOTS_BLOCK_MANAGER_H

#include "snapshots/types.h"

#include <stdlib.h>

/*----------------------------------------------------------------*/

struct block_manager;

struct block_manager *block_manager_create(int fd, size_t block_size, block_t nr_blocks);
void block_manager_destroy(struct block_manager *bm);

enum block_lock {
	READ,
	WRITE
};

int block_lock(struct block_manager *bm, block_t b, enum block_lock how, void **data);
int block_unlock(struct block_manager *bm, block_t b, int changed);

/*----------------------------------------------------------------*/

#endif
