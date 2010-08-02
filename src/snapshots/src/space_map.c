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

int sm_equal(struct space_map *sm1, struct space_map *sm2)
{
	unsigned i;

	if (sm1->nr_blocks != sm2->nr_blocks)
		return 0;

	for (i = 0; i < sm1->nr_blocks; i++)
		if (sm1->ref_count[i] != sm2->ref_count[i])
			return 0;

	return 1;
}

void sm_dump_comparison(struct space_map *sm1, struct space_map *sm2)
{
	unsigned i;

	if (sm1->nr_blocks != sm2->nr_blocks) {
		printf("number of block differ %u/%u\n",
		       (unsigned) sm1->nr_blocks, (unsigned) sm2->nr_blocks);
		return;
	}

	for (i = 0; i < sm1->nr_blocks; i++)
		if (sm1->ref_count[i] != sm2->ref_count[i])
			printf("counts for block %u differ %u/%u\n",
			       i, (unsigned) sm1->ref_count[i], (unsigned) sm2->ref_count[i]);
}

/*----------------------------------------------------------------*/
