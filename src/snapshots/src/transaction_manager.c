#include "transaction_manager.h"
#include "space_map.h"
#include "types.h"

#include "datastruct/list.h"
#include "mm/pool.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/*----------------------------------------------------------------*/

/* FIXME: remove */
static void barf(const char *msg)
{
	printf("%s\n", msg);
	abort();
}

/* the transaction brackets all on-disk operations */
struct transaction {
        struct pool *mem;
        struct list new_blocks;
};

struct block_list {
	struct list list;
	block_t block;
};

struct transaction_manager {
	struct block_manager *bm;
	struct space_map *sm;
	struct transaction *t;
};

struct transaction_manager *tm_create(struct block_manager *bm)
{
	struct transaction_manager *tm = malloc(sizeof(*tm));
	if (tm) {
		tm->bm = bm;
		tm->sm = space_map_create(bm_nr_blocks(bm));
		tm->t = NULL;
		if (!tm->sm) {
			free(tm);
			tm = NULL;
		}
	}

	return tm;
}

void tm_destroy(struct transaction_manager *tm)
{
	assert(!tm->t);
	free(tm);
}

int tm_begin(struct transaction_manager *tm)
{
	struct pool *mem;
	struct transaction *t;

	if (!(mem = pool_create("", 1024)))
		barf("pool_create");

	if (!(t = pool_alloc(mem, sizeof(*t))))
		barf("pool_alloc");

	t->mem = mem;
	list_init(&t->new_blocks);
	tm->t = t;
	return 1;
}

int tm_commit(struct transaction_manager *tm, block_t root)
{
	bm_flush(tm->bm);
	bm_unlock(tm->bm, root, 1);
	bm_flush(tm->bm);

	/* destroy the transaction */
	pool_destroy(tm->t->mem);
	tm->t = NULL;
	return 1;
}

int tm_rollback(struct transaction_manager *tm)
{
	return 0;
}

static int insert_new_block(struct transaction *t, block_t b)
{
	struct block_list *bl = pool_alloc(t->mem, sizeof(*bl));
	if (!bl) {
		/* FIXME: stuff */
		return 0;
	}

	bl->block = b;
	list_add(&t->new_blocks, &bl->list);
	return 1;
}

static int is_new_block(struct transaction *t, block_t orig)
{
	struct block_list *bl;
	list_iterate_items (bl, &t->new_blocks)
		if (bl->block == orig)
			return 1;

	return 0;
}

int tm_new_block(struct transaction_manager *tm, block_t *new, void **data)
{
	if (!sm_new_block(tm->sm, new))
		barf("sm_new_block");

	if (!bm_lock(tm->bm, *new, WRITE, data))
		barf("block_lock");

	return insert_new_block(tm->t, *new);
}

int tm_shadow_block(struct transaction_manager *tm, block_t orig, block_t *copy, void **data)
{
	if (is_new_block(tm->t, orig)) {
		*copy = orig;
		return bm_lock(tm->bm, orig, WRITE, data);
	} else {
		block_t copy_;
		void *orig_data, *copy_data;

		if (!sm_new_block(tm->sm, &copy_))
			return 0;

		if (!bm_lock(tm->bm, copy_, WRITE, &copy_data)) {
			sm_dec_block(tm->sm, copy_);
			abort();
		}

		if (!bm_lock(tm->bm, orig, READ, (void **) &orig_data)) {
			abort();
		}

		memcpy(copy_data, orig_data, BLOCK_SIZE);
		if (!bm_unlock(tm->bm, orig, 0)) {
			/* FIXME: stuff */
			abort();
		}

		insert_new_block(tm->t, copy_);
		*copy = copy_;
		return 1;
	}
}

int tm_write_unlock(struct transaction_manager *tm, block_t b)
{
	return bm_unlock(tm->bm, b, 1);
}

int tm_read_lock(struct transaction_manager *tm, block_t b, void **data)
{
	return bm_lock(tm->bm, b, READ, data);
}

int tm_read_unlock(struct transaction_manager *tm, block_t b)
{
	return bm_unlock(tm->bm, b, 0);
}

void tm_inc(struct transaction_manager *tm, block_t b)
{
	sm_inc_block(tm->sm, b);
}

void tm_dec(struct transaction_manager *tm, block_t b)
{
	sm_dec_block(tm->sm, b);
}

uint32_t tm_ref(struct transaction_manager *tm, block_t b)
{
	return sm_get_count(tm->sm, b);
}

/*----------------------------------------------------------------*/