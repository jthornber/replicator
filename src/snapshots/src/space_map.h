#ifndef SNAPSHOTS_SPACE_MAP_H
#define SNAPSHOTS_SPACE_MAP_H

#include "snapshots/types.h"

/*----------------------------------------------------------------*/

/*
 * This in-core data structure keeps a record of how many times each block
 * in a device is referenced.
 */
struct space_map;

struct space_map *space_map_create(block_t nr_blocks);
void space_map_destroy(struct space_map *sm);

int sm_new_block(struct space_map *sm, block_t *b);
int sm_inc_block(struct space_map *sm, block_t b);
int sm_dec_block(struct space_map *sm, block_t b);

void sm_dump(struct space_map *sm);

/*----------------------------------------------------------------*/

#endif
