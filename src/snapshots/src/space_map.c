#include "space_map.h"
#include "btree.h"

#include "datastruct/list.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FIXME: Look at Mikulas' radix trees, is this applicable to reference counts? */

/* FIXME: at the very least store ranges in the btree.  Value can have
 * 32bit reference count + 32 bit range.  Nope range implied by the keys.
 * Have to be 64bit for reference count :(
 */

/*----------------------------------------------------------------*/

/* we have a little hash table of the reference count changes */
struct delta_entry {
	struct list lru;
	struct list hash;
	block_t block;
	int32_t delta;
};

#define NR_BUCKETS 1024
#define MASK (NR_BUCKETS - 1)
#define PRIME 4294967291

struct space_map {
	struct transaction_manager *tm;
	block_t nr_blocks;
	int initialised;
	block_t root;
	block_t last_allocated;

	struct list deltas;
	struct list buckets[NR_BUCKETS];
};

/* FIXME: we're hashing blocks elsewhere, factor out the hash fn */
static unsigned hash_delta(block_t b)
{
	return (b * PRIME) & MASK;
}

static struct delta_entry *find_delta(struct space_map *sm, block_t b)
{
	struct list *l;
	list_iterate (l, sm->buckets + hash_delta(b)) {
		struct delta_entry *de = list_struct_base(l, struct delta_entry, hash);

		if (de->block == b)
			return de;
	}

	return NULL;
}

static int add_delta(struct space_map *sm, block_t b, int32_t delta)
{
	struct delta_entry *de = find_delta(sm, b);

	if (!de) {
		de = malloc(sizeof(*de));
		if (!de)
			return 0;

		list_init(&de->lru);
		list_add(sm->buckets + hash_delta(b), &de->hash);
		de->block = b;
		de->delta = 0;
	}

	list_move(&sm->deltas, &de->lru);
	de->delta += delta;
	return 1;
}

static int32_t get_delta(struct space_map *sm, block_t b)
{
	struct delta_entry *de = find_delta(sm, b);
	return de ? de->delta : 0;
}

static struct space_map *sm_alloc(struct transaction_manager *tm, block_t nr_blocks)
{
	unsigned i;
	struct space_map *sm = malloc(sizeof(*sm));

	if (!sm)
		return NULL;

	sm->tm = tm;
	sm->nr_blocks = nr_blocks;
	sm->initialised = 1;
	sm->last_allocated = 0;

	list_init(&sm->deltas);
	for (i = 0; i < NR_BUCKETS; i++)
		list_init(sm->buckets + i);

	return sm;
}

struct space_map *sm_new(struct transaction_manager *tm,
			 block_t nr_blocks)
{
	struct space_map *sm = sm_alloc(tm, nr_blocks);
	if (sm) {
		sm->initialised = 0;
		// sm_inc_block(sm, 0);	/* FIXME: reserve */
	}

	return sm;
}

struct space_map *sm_open(struct transaction_manager *tm,
			  block_t root, block_t nr_blocks)
{
	struct space_map *sm = sm_alloc(tm, nr_blocks);
	if (sm)
		sm->root = root;
	return sm;
}

void sm_close(struct space_map *sm)
{
	struct list *l, *tmp;
	list_iterate_safe (l, tmp, &sm->deltas) {
		struct delta_entry *de = list_struct_base(l, struct delta_entry, hash);
		free(de);
	}

	free(sm);
}

static void next_block(struct space_map *sm)
{
	sm->last_allocated++;
	if (sm->last_allocated == sm->nr_blocks)
		sm->last_allocated = 0;
}

int sm_new_block(struct space_map *sm, block_t *b)
{
	block_t i;

	for (i = 0; i < sm->nr_blocks; i++) {
		uint64_t ref_count = 0;
		int32_t delta;

		next_block(sm);
		delta = get_delta(sm, sm->last_allocated);
		if (delta > 0)
			continue;

		if (sm->initialised)
			if (btree_lookup_equal(sm->tm, sm->root,
					       sm->last_allocated, &ref_count) == LOOKUP_ERROR)
				return 0;

		if (ref_count + delta == 0) {
			if (!sm_inc_block(sm, sm->last_allocated))
				return 0;
			*b = sm->last_allocated;
			return 1;
		}
	}

	return 0;
}

int sm_inc_block(struct space_map *sm, block_t b)
{
	assert(b);
	return add_delta(sm, b, 1);
}

int sm_dec_block(struct space_map *sm, block_t b)
{
	return add_delta(sm, b, -1);
}

uint32_t sm_get_count(struct space_map *sm, block_t b)
{
	uint64_t rc = 0;

	if (sm->initialised && !btree_lookup_equal(sm->tm, sm->root, b, &rc))
		abort();

	return rc + get_delta(sm, b);
}

static int flush_once(struct space_map *sm)
{
	struct list head, *l;
	block_t root = sm->root;

	list_init(&head);
	list_splice(&head, &sm->deltas);

	list_iterate (l, &head) {
		struct delta_entry *de = list_struct_base(l, struct delta_entry, hash);
		uint32_t shadow = de->delta;
		uint64_t value;

		switch (btree_lookup_equal(sm->tm, root, de->block, &value)) {
		case LOOKUP_ERROR:
			return 0;

		case LOOKUP_NOT_FOUND:
			if (!btree_insert(sm->tm, root, de->block, shadow, &root))
				return 0;
			break;

		case LOOKUP_FOUND:
			if (!btree_insert(sm->tm, root, de->block, shadow + value, &root))
				return 0;
			break;
		}

		/*
		 * The delta may have increased as a result of the btree
		 * operations above.  So we subtract shadow, rather than
		 * setting to 0.
		 */
		de->delta -= shadow;
	}

	sm->root = root;
	return 1;
}

int sm_flush(struct space_map *sm, block_t *new_root)
{
	unsigned i;
	if (!sm->initialised) {
		if (!btree_empty(sm->tm, &sm->root))
			return 0;
		sm->initialised = 1;
	}

	while (!list_empty(&sm->deltas))
		if (!flush_once(sm))
			return 0;

	for (i = 0; i < NR_BUCKETS; i++) {
		struct list *l, *tmp;
		list_iterate_safe (l, tmp, sm->buckets + i) {
			struct delta_entry *de = list_struct_base(l, struct delta_entry, hash);
			free(de);
		}
		list_init(sm->buckets + i);
	}

	*new_root = sm->root;
	return 1;
}

#if 0
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
#endif
/*----------------------------------------------------------------*/
