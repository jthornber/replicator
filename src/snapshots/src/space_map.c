#include "snapshots/space_map.h"

#include "datastruct/list.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FIXME: throw away implementation for now.  Look at Mikulas' radix trees */

/*----------------------------------------------------------------*/

struct space_map {
	block_t nr_blocks;
	block_t last_allocated;
	uint32_t *ref_count;
};

struct space_map *space_map_create(block_t nr_blocks)
{
	size_t len;
	struct space_map *sm = malloc(sizeof(*sm));

	if (!sm)
		return NULL;

	sm->nr_blocks = nr_blocks;
	sm->last_allocated = 1;	/* You can't allocate block 0 */

	len = sizeof(*sm->ref_count) * nr_blocks;
	sm->ref_count = malloc(len);
	if (!sm->ref_count) {
		free(sm);
		return NULL;
	}
	memset(sm->ref_count, 0, len);

	return sm;
}

void space_map_destroy(struct space_map *sm)
{
	free(sm->ref_count);
	free(sm);
}

int sm_new_block(struct space_map *sm, block_t *b)
{
	uint32_t i;

	/* FIXME: duplicate code in these loops */
	for (i = sm->last_allocated; i < sm->nr_blocks; i++)
		if (sm->ref_count[i] == 0) {
			sm->ref_count[i] = 1;
			*b = sm->last_allocated = i;
			return 1;
		}

	for (i = 1; i < sm->last_allocated; i++)
		if (sm->ref_count[i] == 0) {
			sm->ref_count[i] = 1;
			*b = sm->last_allocated = i;
			return 1;
		}

	return 0;
}

int sm_inc_block(struct space_map *sm, block_t b)
{
	sm->ref_count[b]++;
	return 1;
}

int sm_dec_block(struct space_map *sm, block_t b)
{
	assert(sm->ref_count[b]);
	sm->ref_count[b]--;
	return 1;
}

uint32_t sm_get_count(struct space_map *sm, block_t b)
{
	return sm->ref_count[b];
}

void sm_dump(struct space_map *sm)
{
	size_t len = sm->nr_blocks * sizeof(uint32_t);
	uint32_t *bins = malloc(len), i;

	assert(bins);

	memset(bins, 0, len);
	for (i = 0; i < sm->nr_blocks; i++)
		bins[sm->ref_count[i]]++;


	printf("space map reference count counts:\n");
	for (i = 0; i < sm->nr_blocks; i++)
		if (bins[i])
			printf("    %u: %u\n", i, bins[i]);
}

/*----------------------------------------------------------------*/
