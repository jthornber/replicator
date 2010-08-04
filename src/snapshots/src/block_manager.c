#include "snapshots/block_manager.h"

#include "datastruct/list.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/*----------------------------------------------------------------*/

struct block {
	struct list lru;
	struct list hash;

	block_t where;
	enum block_lock type;
	void *data;
	int dirty;

	/* debug fields */
	unsigned read_count;
	unsigned write_count;
};

/* FIXME: locking is ignored for now */
struct block_manager {
	int fd;
	size_t block_size;
	block_t nr_blocks;

	unsigned read_count;
	unsigned write_count;

	pthread_mutex_t lock;	/* protects both lru and hash */
	unsigned blocks_allocated;
	struct list lru_list;
	struct list locked_list;

	/* hash table of cached blocks */
	unsigned cache_size;
	unsigned nr_buckets;
	struct list *buckets;
};

static struct block *alloc_block(struct block_manager *bm, size_t block_size)
{
	struct block *b = malloc(sizeof(*b));
	if (!b)
		return NULL;

	list_init(&b->lru);
	list_init(&b->hash);
	b->data = malloc(block_size);
	if (!b->data) {
		free(b);
		return NULL;
	}
	b->dirty = 0;
	b->read_count = 0;
	b->write_count = 0;

	bm->blocks_allocated++;  /* FIXME: unprotected */
	return b;
}

static void free_block(struct block_manager *bm, struct block *b)
{
	/*
	 * No locks should be held at this point.
	 */
	if (b->read_count || b->write_count)
		abort();

	list_del(&b->lru);
	list_del(&b->hash);
	free(b->data);
	free(b);
	bm->blocks_allocated--;  /* FIXME: unprotected */
}

static void add_to_lru(struct block_manager *bm, struct block *b)
{
	assert(b->read_count == 0);
	assert(b->write_count == 0);

	pthread_mutex_lock(&bm->lock);
	list_del(&b->lru);
	list_add_h(&bm->lru_list, &b->lru);
	pthread_mutex_unlock(&bm->lock);
}

static void add_to_locked(struct block_manager *bm, struct block *b)
{
	pthread_mutex_lock(&bm->lock);
	list_del(&b->lru);
	list_add_h(&bm->locked_list, &b->lru);
	pthread_mutex_unlock(&bm->lock);
}

static void lock_block(struct block_manager *bm, struct block *b, enum block_lock how)
{
	switch (how) {
	case LOCK_READ:
		b->read_count++;
		break;

	case LOCK_WRITE:
		if (b->write_count)
			abort();
		b->write_count++;
		break;
	}
	b->type = how;
	add_to_locked(bm, b);
}

static void unlock_block(struct block_manager *bm, struct block *b)
{
	if (b->read_count) {
		b->read_count--;
		if (!b->read_count)
			add_to_lru(bm, b);
	} else {
		b->write_count--;
		assert(b->write_count == 0);
		add_to_lru(bm, b);
	}
}

static unsigned hash_block(struct block_manager *bm, block_t b)
{
	const unsigned BIG_PRIME = 4294967291;
	return (((unsigned) b) * BIG_PRIME) & (bm->nr_buckets - 1);
}

static struct block *find_block_(struct block_manager *bm, block_t b)
{
	unsigned bucket = hash_block(bm, b);
	struct list *l;

	list_iterate (l, bm->buckets + bucket) {
		struct block *bl = list_struct_base(l, struct block, hash);
		if (bl->where == b)
			return bl;
	}

	return NULL;
}

static struct block *find_block(struct block_manager *bm, block_t b)
{
	struct block *bl;
	pthread_mutex_lock(&bm->lock);
	bl = find_block_(bm, b);
	pthread_mutex_unlock(&bm->lock);
	return bl;
}

static void insert_block(struct block_manager *bm, struct block *b)
{
	unsigned bucket = hash_block(bm, b->where);
	pthread_mutex_lock(&bm->lock);
	list_add_h(bm->buckets + bucket, &b->hash);
	pthread_mutex_unlock(&bm->lock);
}

static unsigned next_power_of_2(unsigned n)
{
	/* FIXME: there's a bit twiddling way of doing this */
	unsigned p = 1;
	while (p < n)
		p <<= 1;

	return p;
}

static int read_block(struct block_manager *bm, struct block *b)
{
	if ((lseek(bm->fd, b->where * bm->block_size, SEEK_SET) < 0) ||
	    (read(bm->fd, b->data, bm->block_size) < 0))
		return 0;

	bm->read_count++; /* FIXME: unprotected */
	return 1;
}

static int write_block(struct block_manager *bm, struct block *b)
{
	if ((lseek(bm->fd, b->where * bm->block_size, SEEK_SET) < 0) ||
	    (write(bm->fd, b->data, bm->block_size) < 0))
		return 0;

	bm->write_count++;	/* FIXME: unprotected */
	return 1;
}

/*----------------------------------------------------------------*/

struct block_manager *
block_manager_create(int fd, size_t block_size, block_t nr_blocks, unsigned cache_size)
{
	unsigned i;
	struct block_manager *bm = malloc(sizeof(*bm));
	if (!bm)
		return NULL;

	bm->fd = fd;
	bm->block_size = block_size;
	bm->nr_blocks = nr_blocks;
	bm->read_count = 0;
	bm->write_count = 0;
	pthread_mutex_init(&bm->lock, NULL);
	bm->blocks_allocated = 0;
	list_init(&bm->lru_list);
	list_init(&bm->locked_list);

	bm->cache_size = cache_size;
	bm->nr_buckets = next_power_of_2(cache_size);
	bm->buckets = malloc(sizeof(*bm->buckets) * bm->nr_buckets);
	if (!bm->buckets) {
		free(bm);
		return NULL;
	}
	for (i = 0; i < bm->nr_buckets; i++)
		list_init(bm->buckets + i);

	return bm;
}

void block_manager_destroy(struct block_manager *bm)
{
	struct list *l, *tmp;

	pthread_mutex_destroy(&bm->lock);

	list_iterate_safe (l, tmp, &bm->lru_list) {
		struct block *b = list_struct_base(l, struct block, lru);
		free_block(bm, b);
	}

	free(bm->buckets);
	free(bm);
}

size_t bm_block_size(struct block_manager *bm)
{
	return bm->block_size;
}

block_t bm_nr_blocks(struct block_manager *bm)
{
	return bm->nr_blocks;
}

static int bm_lock_(struct block_manager *bm, block_t block, enum block_lock how, int need_read, void **data)
{
	struct block *b;

	b = find_block(bm, block);
	if (b) {
		*data = b->data;
		lock_block(bm, b, how);
	} else {
		b = alloc_block(bm, bm->block_size);
		if (!b)
			return 0;

		b->where = block;

		if (need_read) {
			if (!read_block(bm, b)) {
				free_block(bm, b);
				return 0;
			}
		} else {
			memset(b->data, 0, bm->block_size);
		}

		*data = b->data;
		insert_block(bm, b);
		lock_block(bm, b, how);
	}

	return 1;
}

int bm_lock(struct block_manager *bm, block_t block, enum block_lock how, void **data)
{
	return bm_lock_(bm, block, how, 1, data);
}

int bm_lock_no_read(struct block_manager *bm, block_t b, enum block_lock how, void **data)
{
	if (how != LOCK_WRITE)
		return 0;

	return bm_lock_(bm, b, how, 0, data);
}


int bm_unlock(struct block_manager *bm, block_t block, int changed)
{
	struct block *b = find_block(bm, block);

	if (!b)
		return 0;

	if (!b->read_count && !b->write_count)
		return 0;

	if (changed) {
		if (b->type != LOCK_WRITE)
			return 0;
		b->dirty = 1;
	}
	unlock_block(bm, b);
	return 1;
}

int bm_flush(struct block_manager *bm)
{
	/*
	 * Run through the lru list writing any dirty blocks.  We ignore
	 * locked blocks since they're still being updated.
	 */
	int r = 1;
	struct list *l, *tmp;
	pthread_mutex_lock(&bm->lock);
	list_iterate_safe (l, tmp, &bm->lru_list) {
		struct block *b = list_struct_base(l, struct block, lru);
		if (b->dirty) {
			if (write_block(bm, b))
				b->dirty = 0;
			else
				r = 0;
		}
	}
	pthread_mutex_unlock(&bm->lock);

	return r;
}

void bm_dump(struct block_manager *bm)
{
	printf("Block manager: %u reads, %u writes\n",
	       bm->read_count, bm->write_count);
}

static unsigned count_locks(struct block_manager *bm, enum block_lock t)
{
	struct list *l;
	unsigned n = 0;

	list_iterate (l, &bm->locked_list) {
		struct block *b = list_struct_base(l, struct block, lru);
		if (b->type == t)
			n++;
	}

	return n;
}

unsigned bm_read_locks_held(struct block_manager *bm)
{
	return count_locks(bm, LOCK_READ);
}

unsigned bm_write_locks_held(struct block_manager *bm)
{
	return count_locks(bm, LOCK_WRITE);
}


/*----------------------------------------------------------------*/

