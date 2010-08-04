#include "snapshots/block_manager.h"

#include "datastruct/list.h"

#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

/*----------------------------------------------------------------*/

struct block {
	struct list lru;
	struct list hash;

	block_t where;
	enum block_lock type;
	void *data;

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
	struct list lru_list;

	/* hash table of cached blocks */
	unsigned cache_size;
	unsigned nr_buckets;
	struct list *buckets;
};

static struct block *alloc_block(size_t block_size)
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
	b->read_count = 0;
	b->write_count = 0;
	return b;
}

static void free_block(struct block *b)
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
}

static void lock_block(struct block *b, enum block_lock how)
{
	switch (how) {
	case LOCK_READ:
		b->read_count++;
		break;

	case LOCK_WRITE:
		b->write_count++;
		break;
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

static void lru_update(struct block_manager *bm, struct block *b)
{
	pthread_mutex_lock(&bm->lock);
	list_del(&b->lru);
	list_add_h(&bm->lru_list, &b->lru);
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
	pthread_mutex_init(&bm->lock, NULL);
	list_init(&bm->lru_list);

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
	free(bm->buckets);

	list_iterate_safe (l, tmp, &bm->lru_list) {
		struct block *b = list_struct_base(l, struct block, lru);
		free_block(b);
	}

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

int bm_lock(struct block_manager *bm, block_t block, enum block_lock how, void **data)
{
	struct block *b;

	b = find_block(bm, block);
	if (b)
		lock_block(b, how);
	else {
		b = alloc_block(bm->block_size);
		if (!b)
			return 0;

		if (lseek(bm->fd, block * bm->block_size, SEEK_SET) < 0) {
			free_block(b);
			return 0;
		}

		bm->read_count++; /* FIXME: unprotected */
		if (read(bm->fd, b->data, bm->block_size) < 0) {
			free_block(b);
			return 0;
		}

		b->where = block;
		b->type = how;
		*data = b->data;

		insert_block(bm, b);
		lru_update(bm, b);
	}

	return 1;
}

int bm_unlock(struct block_manager *bm, block_t block, int changed)
{
	struct block *b = find_block(bm, block);

	if (!b)
		return 0;

	if (changed) {
		if (b->type != LOCK_WRITE)
			return 0;

		bm->write_count++;
		if ((lseek(bm->fd, block * bm->block_size, SEEK_SET) < 0) ||
		    (write(bm->fd, b->data, bm->block_size) < 0))
			return 0;
	}
	free_block(b);
	return 1;
}

int bm_flush(struct block_manager *bm)
{
	return 1;
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

	list_iterate (l, &bm->lru_list) {
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

