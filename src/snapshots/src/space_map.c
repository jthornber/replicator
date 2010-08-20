#include "space_map.h"
#include "btree.h"

#include "datastruct/list.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* FIXME: Look at Mikulas' radix trees, is this applicable to reference counts? */

/* FIXME: at the very least store ranges in the btree.  Value can have
 * 32bit reference count + 32 bit range.  Nope range implied by the keys.
 * Have to be 64bit for reference count :(
 */

/*----------------------------------------------------------------*/

/* we have a little hash table of the reference count changes */
struct cache_entry {
	struct list lru;
	struct list hash;

	block_t block;
	uint32_t ref_count;	/* Ref count from last transaction */
	int32_t delta;		/* how this has changed within the current transaction */
	int32_t unwritten;      /* what still needs to be written to the current transaction */
};

#define NR_BUCKETS 1024
#define MASK (NR_BUCKETS - 1)
#define PRIME 4294967291

struct space_map_combined {
	block_t nr_blocks;
	int initialised;

	struct list deltas;
	struct list buckets[NR_BUCKETS];

	struct transaction_manager *tm;
	struct btree_info info;
	block_t root;
	block_t last_allocated;

	struct disk_format *disk;
};

/* FIXME: we're hashing blocks elsewhere, factor out the hash fn */
static unsigned hash_block(block_t b)
{
	return (b * PRIME) & MASK;
}

static struct cache_entry *find_entry(struct space_map_combined *sm, block_t b)
{
	struct list *l;
	list_iterate (l, sm->buckets + hash_block(b)) {
		struct cache_entry *ce = list_struct_base(l, struct cache_entry, hash);

		if (ce->block == b)
			return ce;
	}

	return NULL;
}

/*
 * Only call this if you know the entry is _not_ already present.
 */
static struct cache_entry *add_entry(struct space_map_combined *sm, block_t b, uint32_t ref_count)
{
	struct cache_entry *ce = malloc(sizeof(*ce));

	if (!ce)
		return NULL;

	list_init(&ce->lru);
	list_add(sm->buckets + hash_block(b), &ce->hash);
	ce->block = b;
	ce->ref_count = ref_count;
	ce->delta = 0;
	ce->unwritten = 0;
	return ce;
}

static int add_delta(struct space_map_combined *sm, block_t b, int32_t delta)
{
	struct cache_entry *ce = find_entry(sm, b);

	if (!ce) {
		uint64_t ref_count = 0;
		if (sm->initialised) {
			switch (btree_lookup_equal(&sm->info, sm->root, &b, &ref_count)) {
			case LOOKUP_ERROR:
				return 0;

			case LOOKUP_NOT_FOUND:
				ref_count = 0;
				break;

			case LOOKUP_FOUND:
				break;
			}
		}

		ce = add_entry(sm, b, ref_count);
		if (!ce)
			return 0;
	}

	ce->delta += delta;
	ce->unwritten += delta;

	if (ce->unwritten)
		list_move(&sm->deltas, &ce->lru);
	else {
		/* deltas have cancelled each other out */
		list_del(&ce->lru);
	}

#if 1
	if (ce->delta < 0)
		assert(ce->ref_count >= -ce->delta);
#endif

	return 1;
}

static struct space_map_combined *sm_alloc(struct transaction_manager *tm, block_t nr_blocks)
{
	unsigned i;
	struct space_map_combined *sm = malloc(sizeof(*sm));

	if (!sm)
		return NULL;

	sm->tm = tm;
	sm->nr_blocks = nr_blocks;
	sm->initialised = 1;
	sm->info.tm = tm;
	sm->info.levels = 1;
	sm->info.adjust = value_is_meaningless;
	sm->last_allocated = 0;

	list_init(&sm->deltas);
	for (i = 0; i < NR_BUCKETS; i++)
		list_init(sm->buckets + i);

	return sm;
}

static void destroy(void *context)
{
	struct space_map_combined *sm = (struct space_map_combined *) context;
	struct list *l, *tmp;
	list_iterate_safe (l, tmp, &sm->deltas) {
		struct cache_entry *ce = list_struct_base(l, struct cache_entry, lru);
		free(ce);
	}

	free(sm);
}

static void next_block(void *context)
{
	struct space_map_combined *sm = (struct space_map_combined *) context;
	sm->last_allocated++;
	if (sm->last_allocated == sm->nr_blocks)
		sm->last_allocated = 0;
}

static int new_block(void *context, block_t *b)
{
	struct space_map_combined *sm = (struct space_map_combined *) context;
	block_t i;

	for (i = 0; i < sm->nr_blocks; i++) {
		struct cache_entry *ce;

		next_block(sm);

		ce = find_entry(sm, sm->last_allocated);

		if (!ce) {
			uint64_t ref_count = 0;
			if (sm->initialised) {
				switch (btree_lookup_equal(&sm->info, sm->root,
							   &sm->last_allocated, &ref_count)) {
				case LOOKUP_ERROR:
					return 0;

				case LOOKUP_NOT_FOUND:
					ref_count = 0;
					break;

				case LOOKUP_FOUND:
					break;
				}
			}

			if (ref_count > 0)
				continue;

			ce = add_entry(sm, sm->last_allocated, 0);
		}

		if (ce->ref_count + ce->delta == 0) {
			list_move(&sm->deltas, &ce->lru);
			ce->delta++;
			ce->unwritten++;
			*b = sm->last_allocated;
			return 1;
		}
	}

	return 0;
}

static int inc_block(void *context, block_t b)
{
	struct space_map_combined *sm = (struct space_map_combined *) context;
	return add_delta(sm, b, 1);
}

static int dec_block(void *context, block_t b)
{
	struct space_map_combined *sm = (struct space_map_combined *) context;
	return add_delta(sm, b, -1);
}

static int get_count(void *context, block_t b, uint32_t *result)
{
	struct space_map_combined *sm = (struct space_map_combined *) context;
	struct cache_entry *ce = find_entry(sm, b);

	if (ce) {
		*result = ce->ref_count + ce->delta;
		return 1;
	} else {
		if (sm->initialised) {
			uint64_t rc = 0;

			switch (btree_lookup_equal(&sm->info, sm->root, &b, &rc)) {
			case LOOKUP_ERROR:
				return 0;

			case LOOKUP_NOT_FOUND:
				*result = 0;
				return 1;

			case LOOKUP_FOUND:
				*result = rc;
				return 1;
			}
		}
	}

	return 0;
}

static int flush_once(struct space_map_combined *sm, block_t *new_root)
{
	struct list head, *l, *tmp;

	list_init(&head);
	list_splice(&head, &sm->deltas);
	assert(list_empty(&sm->deltas));

	list_iterate_safe (l, tmp, &head) {
		struct cache_entry *ce = list_struct_base(l, struct cache_entry, lru);
		uint32_t shadow = ce->unwritten;

		assert(ce->unwritten);
		if (!btree_insert(&sm->info, *new_root,
				  &ce->block, ce->ref_count + shadow, new_root))
			abort();

		/*
		 * The |unwritten| value may have increased as a result of
		 * the btree insert above.  So we subtract |shadow|,
		 * rather than setting to 0.
		 */
		ce->unwritten -= shadow;
	}

	return 1;
}

static int flush(void *context, block_t *new_root)
{
	struct space_map_combined *sm = (struct space_map_combined *) context;
	unsigned i;
	if (!sm->initialised) {
		if (!btree_empty(&sm->info, &sm->root))
			return 0;
	}

	*new_root = sm->root;
	while (!list_empty(&sm->deltas))
		if (!flush_once(sm, new_root))
			return 0;

	/* wipe the cache completely */
	for (i = 0; i < NR_BUCKETS; i++) {
		struct list *l, *tmp;
		list_iterate_safe (l, tmp, sm->buckets + i) {
			struct cache_entry *ce = list_struct_base(l, struct cache_entry, hash);
			free(ce);
		}
		list_init(sm->buckets + i);
	}

	sm->root = *new_root;
	sm->initialised = 1;
	return 1;
}

/*----------------------------------------------------------------*/

static struct space_map_ops combined_ops_ = {
	.destroy = destroy,
	.new_block = new_block,
	.inc_block = inc_block,
	.dec_block = dec_block,
	.get_count = get_count,
	.flush = flush,
};

struct space_map *sm_new(struct transaction_manager *tm,
			 block_t nr_blocks)
{
	struct space_map *sm;
	struct space_map_combined *smc = sm_alloc(tm, nr_blocks);
	if (smc) {
		smc->initialised = 0;
		sm = malloc(sizeof(*sm));
		if (!sm)
			free(smc);
		else {
			sm->ops = &combined_ops_;
			sm->context = smc;
		}
	}

	return sm;
}

struct space_map *sm_open(struct transaction_manager *tm,
			  block_t root, block_t nr_blocks)
{
	struct space_map *sm = sm_new(tm, nr_blocks);
	if (sm) {
		struct space_map_combined *smc = (struct space_map_combined *) sm->context;
		smc->root = root;
	}

	return sm;
}

/*----------------------------------------------------------------*/

static void ignore_leaf(uint64_t leaf, uint32_t *ref_counts)
{
}

int sm_walk(struct space_map *sm, uint32_t *ref_counts)
{
	struct space_map_combined *smc = (struct space_map_combined *) sm->context;
	return btree_walk(smc->tm, ignore_leaf, &smc->root, 1, ref_counts);
}

/*----------------------------------------------------------------*/
