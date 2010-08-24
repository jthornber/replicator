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

struct disk_io {
	struct transaction_manager *tm;
	struct btree_info info;
	block_t root;
	block_t new_root;
};

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

struct sm {
	block_t nr_blocks;
	int initialised;

	struct list deltas;
	struct list buckets[NR_BUCKETS];

	struct disk_io io;
	block_t last_allocated;

	struct disk_format *disk;
};

/*----------------------------------------------------------------*/

/* on disk format */
static int io_init(struct disk_io *io)
{
	return btree_empty(&io->info, &io->root);
}

static int io_lookup(struct disk_io *io, block_t b, uint32_t *result)
{
	uint64_t ref_count;
	switch (btree_lookup_equal(&io->info, io->root, &b, &ref_count)) {
	case LOOKUP_ERROR:
		return 0;

	case LOOKUP_NOT_FOUND:
		*result = 0;
		break;

	case LOOKUP_FOUND:
		*result = ref_count;
		break;
	}

	return 1;
}

/* FIXME: differentiate between not found and errors */
static enum lookup_result
io_find_unused(struct disk_io *io, block_t begin, block_t end, block_t *result)
{
	block_t b;
	uint32_t ref_count;

	for (b = begin; b != end; b++) {
		if (!io_lookup(io, b, &ref_count))
			return LOOKUP_ERROR;

		if (ref_count == 0) {
			*result = b;
			return LOOKUP_FOUND;
		}
	}

	return LOOKUP_NOT_FOUND;
}

static void io_begin(struct disk_io *io)
{
	io->new_root = io->root;
}

static int io_insert(struct disk_io *io, block_t b, uint32_t ref_count)
{
	return btree_insert(&io->info, io->new_root,
			    &b, ref_count, &io->new_root);
}

static void io_commit(struct disk_io *io)
{
	io->root = io->new_root;
}

/*----------------------------------------------------------------*/

/* in core hash */

/* FIXME: we're hashing blocks elsewhere, factor out the hash fn */
static unsigned hash_block(block_t b)
{
	return (b * PRIME) & MASK;
}

static struct cache_entry *find_entry(struct sm *sm, block_t b)
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
static struct cache_entry *add_entry(struct sm *sm, block_t b, uint32_t ref_count)
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

static int add_delta(struct sm *sm, block_t b, int32_t delta)
{
	struct cache_entry *ce = find_entry(sm, b);

	if (!ce) {
		uint32_t ref_count = 0;
		if (sm->initialised) {
			if (!io_lookup(&sm->io, b, &ref_count))
				return 0;
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
		list_init(&ce->lru);
	}

#if 1
	if (ce->delta < 0)
		assert(ce->ref_count >= -ce->delta);
#endif

	return 1;
}

static struct sm *sm_alloc(struct transaction_manager *tm, block_t nr_blocks)
{
	unsigned i;
	struct sm *sm = malloc(sizeof(*sm));

	if (!sm)
		return NULL;

	sm->io.tm = tm;
	sm->nr_blocks = nr_blocks;
	sm->initialised = 1;
	sm->io.info.tm = tm;
	sm->io.info.levels = 1;
	sm->io.info.adjust = value_is_meaningless;
	sm->last_allocated = 0;

	list_init(&sm->deltas);
	for (i = 0; i < NR_BUCKETS; i++)
		list_init(sm->buckets + i);

	return sm;
}

static void destroy(void *context)
{
	struct sm *sm = (struct sm *) context;
	struct list *l, *tmp;
	list_iterate_safe (l, tmp, &sm->deltas) {
		struct cache_entry *ce = list_struct_base(l, struct cache_entry, lru);
		free(ce);
	}

	free(sm);
}

static void inc_entry(struct sm *sm, struct cache_entry *ce)
{
	list_move(&sm->deltas, &ce->lru);
	ce->delta++;
	ce->unwritten++;
}

static int new_entry(struct sm *sm, block_t b)
{
	struct cache_entry *ce = add_entry(sm, b, 0);
	if (!ce)
		return 0;

	inc_entry(sm, ce);
	return 1;
}

static int new_block_uninitialised(struct sm *sm, block_t *found)
{
	block_t i, b;
	for (i = 0; i < sm->nr_blocks; i++) {
		struct cache_entry *ce;
		b = (sm->last_allocated + i) % sm->nr_blocks;

		ce = find_entry(sm, b);

		if (!ce) {
			if (!new_entry(sm, b))
				return 0;

			*found = sm->last_allocated = b;
			return 1;
		}

		if (ce->ref_count + ce->delta == 0) {
			inc_entry(sm, ce);
			*found = sm->last_allocated = b;
			return 1;
		}
	}

	return 0;
}

static enum lookup_result
new_block_initialised_range(struct sm *sm,
			    block_t begin, block_t end,
			    block_t *found)
{
	struct cache_entry *ce;

	while (begin < end) {
		switch (io_find_unused(&sm->io, begin, end, found)) {
		case LOOKUP_ERROR:
			return LOOKUP_ERROR;

		case LOOKUP_NOT_FOUND:
			return 0;

		default:
			break;
		}

		ce = find_entry(sm, *found);
		if (!ce) {
			if (!new_entry(sm, *found))
				return 0;
			sm->last_allocated = *found;
			return LOOKUP_FOUND;

		} else if (ce->ref_count + ce->delta == 0) {
			inc_entry(sm, ce);
			sm->last_allocated = *found;
			return LOOKUP_FOUND;
		}

		begin = *found + 1;
	}

	return LOOKUP_NOT_FOUND;
}

static int new_block_initialised(struct sm *sm, block_t *b)
{
	switch (new_block_initialised_range(sm, sm->last_allocated + 1, sm->nr_blocks, b)) {
	case LOOKUP_ERROR:
		return 0;

	case LOOKUP_NOT_FOUND:
		break;

	case LOOKUP_FOUND:
		return 1;
	}

	switch (new_block_initialised_range(sm, 0, sm->last_allocated + 1, b)) {
	case LOOKUP_ERROR:
	case LOOKUP_NOT_FOUND:
		return 0;

	case LOOKUP_FOUND:
		return 1;
	}

	return 0;
}

static int new_block(void *context, block_t *b)
{
	struct sm *sm = (struct sm *) context;
	return sm->initialised ? new_block_initialised(sm, b) : new_block_uninitialised(sm, b);
}

static int inc_block(void *context, block_t b)
{
	struct sm *sm = (struct sm *) context;
	return add_delta(sm, b, 1);
}

static int dec_block(void *context, block_t b)
{
	struct sm *sm = (struct sm *) context;
	return add_delta(sm, b, -1);
}

static int get_count(void *context, block_t b, uint32_t *result)
{
	struct sm *sm = (struct sm *) context;
	struct cache_entry *ce = find_entry(sm, b);

	if (ce) {
		*result = ce->ref_count + ce->delta;
		return 1;
	} else {
		if (sm->initialised)
			return io_lookup(&sm->io, b, result);
		else {
			*result = 0;
			return 1;
		}
	}

	return 0;
}

static int flush_once(struct sm *sm)
{
	struct list head, *l, *tmp;

	list_init(&head);
	list_splice(&head, &sm->deltas);
	assert(list_empty(&sm->deltas));

	list_iterate_safe (l, tmp, &head) {
		struct cache_entry *ce = list_struct_base(l, struct cache_entry, lru);
		uint32_t shadow = ce->unwritten;

		assert(ce->unwritten);
		if (!io_insert(&sm->io, ce->block, ce->ref_count + shadow))
			abort();

		/*
		 * The |unwritten| value may have increased as a result of
		 * the insert above.  So we subtract |shadow|, rather than
		 * setting to 0.
		 */
		ce->unwritten -= shadow;
	}

	return 1;
}

static int flush(void *context, block_t *new_root)
{
	struct sm *sm = (struct sm *) context;
	unsigned i;
	if (!sm->initialised)
		if (!io_init(&sm->io))
			return 0;

	io_begin(&sm->io);
	while (!list_empty(&sm->deltas))
		if (!flush_once(sm))
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
	io_commit(&sm->io);
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
	struct sm *smc = sm_alloc(tm, nr_blocks);
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
		struct sm *smc = (struct sm *) sm->context;
		smc->io.root = root;
	}

	return sm;
}

/*----------------------------------------------------------------*/

static void ignore_leaf(uint64_t leaf, uint32_t *ref_counts)
{
}

int sm_walk(struct space_map *sm, uint32_t *ref_counts)
{
	struct sm *smc = (struct sm *) sm->context;
	return btree_walk(smc->io.tm, ignore_leaf, &smc->io.root, 1, ref_counts);
}

/*----------------------------------------------------------------*/
