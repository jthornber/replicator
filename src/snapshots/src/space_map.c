#include "snapshots/space_map.h"

#include "datastruct/list.h"
#include "mm/pool.h"

#include <stdlib.h>

/* FIXME: throw away implementation for now.  Look at Mikulas' radix trees */

/*----------------------------------------------------------------*/

struct block_detail {
	struct list list;
	block_t block;
	uint64_t reference_count;
};

struct space_map {
	struct pool *mem;
	block_t nr_blocks;
	block_t last_allocated;
	struct list blocks;
};

struct space_map *space_map_create(block_t nr_blocks)
{
	struct pool *mem = pool_create("", 1024);
	struct space_map *sm = pool_alloc(mem, sizeof(*sm));

	if (!sm) {
		pool_destroy(mem);
		return NULL;
	}

	sm->mem = mem;
	sm->nr_blocks = nr_blocks;
	sm->last_allocated = 0;
	list_init(&sm->blocks);
	return sm;
}

void space_map_destroy(struct space_map *sm)
{
	pool_destroy(sm->mem);
}

int sm_new_block(struct space_map *sm, block_t *b)
{
	if (sm->last_allocated == sm->nr_blocks - 1)
		/* out of space */
		return 0;

	*b = ++sm->last_allocated;
	return 1;
}

int sm_inc_block(struct space_map *sm, block_t b)
{
	return 0;
}

int sm_dec_block(struct space_map *sm, block_t b)
{
	return 0;
}

/*----------------------------------------------------------------*/
