#include "snapshots/block_manager.h"

#include "datastruct/list.h"

#include <aio.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * The block manager implementation is split into three parts:
 * 1) a low level block manager that handles allocation and locking of blocks
 * 2) an async io library
 * 3) the top level code to tie these parts together
 */

/*
 * FIXME: use the 64bit variant of aio */

/*----------------------------------------------------------------*/

/*
 * Low level block management.
 */
struct block {
	struct list list;	/* lru, locked */
	struct list hash;

	block_t where;
	enum block_lock type;

	/* aio buffers must be page aligned */
	/* FIXME: don't be so wasteful with memory */
	void *alloc;
	void *data;

	int dirty;
	unsigned read_count;
	unsigned write_count;

	struct aiocb aiocb;
};

/*----------------------------------------------------------------*/

/* FIXME: locking is ignored for now */
struct block_manager {
	int fd;
	size_t block_size;
	block_t nr_blocks;

	unsigned read_count;
	unsigned write_count;

	unsigned blocks_allocated;

	/* FIXME: draw a state diagram showing how the blocks move between
	 * these three lists and the hash. */
	struct list lru_list;
	struct list locked_list;
	struct list io_list;

	/* hash table of cached blocks */
	unsigned cache_size;
	unsigned nr_buckets;
	struct list *buckets;

	/* debug tracing */
	FILE *trace;
};

static void *align(void *data, size_t a)
{
	/* FIXME: 64 bit issues */
	size_t mask = a - 1;
	size_t offset = ((size_t) data) & mask;
	return offset ? data + (a - offset) : data;
}

static struct block *alloc_block_(struct block_manager *bm)
{
	struct block *b = malloc(sizeof(*b));
	if (!b)
		return NULL;

	list_init(&b->list);
	list_init(&b->hash);
	b->alloc = malloc(bm->block_size + getpagesize());
	b->data = align(b->alloc, getpagesize());
	if (!b->data) {
		free(b);
		return NULL;
	}
	b->dirty = 0;
	b->read_count = 0;
	b->write_count = 0;

	bm->blocks_allocated++;
	if (bm->trace)
		fprintf(bm->trace, "cache size %u\n", bm->blocks_allocated);

	return b;
}

static int read_block(struct block_manager *bm, struct block *b)
{
	memset(&b->aiocb, 0, sizeof(b->aiocb));
	b->aiocb.aio_fildes = bm->fd;
	b->aiocb.aio_offset = b->where * bm->block_size;
	b->aiocb.aio_buf = b->data;
	b->aiocb.aio_nbytes = bm->block_size;

	list_move(&bm->io_list, &b->list);
	if (aio_read(&b->aiocb) < 0) {
		abort();
		return 0;
	}

	bm->read_count++;

	if (bm->trace)
		fprintf(bm->trace, "read %u\n", (unsigned) b->where);

	return 1;
}

static int write_block(struct block_manager *bm, struct block *b)
{
	memset(&b->aiocb, 0, sizeof(b->aiocb));
	b->aiocb.aio_fildes = bm->fd;
	b->aiocb.aio_offset = b->where * bm->block_size;
	b->aiocb.aio_buf = b->data;
	b->aiocb.aio_nbytes = bm->block_size;

	list_move(&bm->io_list, &b->list);
	if (aio_write(&b->aiocb) < 0) {
		abort();
		return 0;
	}

	bm->write_count++;

	if (bm->trace)
		fprintf(bm->trace, "write %u\n", (unsigned) b->where);

	return 1;
}

static void complete_io(struct block_manager *bm, struct block *b)
{
	list_move(&bm->lru_list, &b->list);
	b->dirty = 0;

	if (bm->trace)
		fprintf(bm->trace, "io complete %u\n", (unsigned) b->where);
}

static void fail_io(struct block_manager *bm, struct block *b)
{
	/* We just try again */
	/* FIXME: add a retry limit */
	if (b->dirty)
		aio_write(&b->aiocb);
	else
		aio_read(&b->aiocb);
}

static unsigned check_io(struct block_manager *bm)
{
	unsigned count = 0;
	struct block *b, *tmp;

	list_iterate_items_safe (b, tmp, &bm->io_list) {
		switch (aio_error(&b->aiocb)) {
		case 0:
			complete_io(bm, b);
			count++;
			break;

		case EINPROGRESS:
			break;

		default:
			fail_io(bm, b);
		}
	}

	return count;
}

static int wait_io(struct block_manager *bm, struct block *b, int trace)
{
	int r;
	struct aiocb const *ptrs[1];

	if (trace && bm->trace)
		fprintf(bm->trace, "wait %u {\n", (unsigned) b->where);

	ptrs[0] = &b->aiocb;

	do {
		r = aio_suspend(ptrs, 1, NULL);
	} while (r == EAGAIN || r == EINTR);

	if (r < 0)
		return 0;

	r = aio_error(&b->aiocb);

	{
		int r = aio_return(&b->aiocb);
		if (r != BLOCK_SIZE)
			fail_io(bm, b);
	}

	complete_io(bm, b);

	if (trace && bm->trace)
		fprintf(bm->trace, "}\n");

	return r == 0;
}

static int wait_all_writes(struct block_manager *bm)
{
	struct block *b, *tmp;

	if (bm->trace)
		fprintf(bm->trace, "wait all writes {\n");

	list_iterate_items_safe (b, tmp, &bm->io_list) {
		if (b->dirty) {
			if (!wait_io(bm, b, 0))
				return 0;
		}
	}

	if (bm->trace)
		fprintf(bm->trace, "}\n");
	return 1;
}

static unsigned wait_any_io(struct block_manager *bm)
{
	int r;
	struct block *b;
	unsigned i, io_count = list_size(&bm->io_list);
	struct aiocb const **cbs = malloc(sizeof(*cbs) * io_count);

	if (!cbs)
		return 0;

	if (bm->trace)
		fprintf(bm->trace, "wait any {\n");

	i = 0;
	list_iterate_items (b, &bm->io_list)
		cbs[i++] = &b->aiocb;

	do {
		r = aio_suspend(cbs, io_count, NULL);
	} while (r == EAGAIN || r == EINTR);

	if (r < 0)
		abort();

	r = check_io(bm);

	if (bm->trace)
		fprintf(bm->trace, "}\n");

	return r;
}

static void free_block(struct block_manager *bm, struct block *b)
{
	/*
	 * No locks should be held at this point.
	 */
	if (b->read_count || b->write_count)
		abort();

	list_del(&b->list);
	list_del(&b->hash);
	free(b->alloc);
	free(b);
	bm->blocks_allocated--;

	if (bm->trace)
		fprintf(bm->trace, "cache size %u\n", bm->blocks_allocated);
}

static struct block *alloc_block(struct block_manager *bm)
{
	if (bm->blocks_allocated > bm->cache_size && !list_empty(&bm->lru_list)) {
		struct list *l, *tmp;

		do {
			/* reuse the head of the lru list */
			list_iterate_safe (l, tmp, &bm->lru_list) {
				struct block *b = list_struct_base(l, struct block, list);

				if (b->dirty)
					write_block(bm, b);
				else {
					list_del(&b->list);
					list_del(&b->hash);
					list_init(&b->list);
					list_init(&b->hash);
					b->dirty = 0;
					b->read_count = 0;
					b->write_count = 0;

					if (bm->blocks_allocated > bm->cache_size)
						free_block(bm, b);
					else
						return b;
				}
			}

		} while (wait_any_io(bm) > 0);
	}

	return alloc_block_(bm);
}

#if 0
static void reap_lru(struct block_manager *bm)
{
	struct list *l, *tmp;
	list_iterate_safe (l, tmp, &bm->lru_list) {
		struct block *b;

		if (bm->blocks_allocated <= bm->cache_size)
			break;

		b = list_struct_base(l, struct block, list);
		if (b->dirty)
			write_block(bm, b);
	}
}
#endif
static void add_to_lru(struct block_manager *bm, struct block *b)
{
	assert(b->read_count == 0);
	assert(b->write_count == 0);
	list_move(&bm->lru_list, &b->list);
}

static void add_to_locked(struct block_manager *bm, struct block *b)
{
	list_move(&bm->locked_list, &b->list);
}

static void lock_block(struct block_manager *bm, struct block *b, enum block_lock how)
{
	switch (how) {
	case BM_LOCK_READ:
		assert(!b->write_count);
		b->read_count++;
		break;

	case BM_LOCK_WRITE:
		assert(!b->read_count);
		assert(!b->write_count);
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
	return find_block_(bm, b);
}

static void insert_block(struct block_manager *bm, struct block *b)
{
	unsigned bucket = hash_block(bm, b->where);
	list_add_h(bm->buckets + bucket, &b->hash);
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

	assert((block_size & (getpagesize() - 1)) == 0);

	bm->fd = fd;
	bm->block_size = block_size;
	bm->nr_blocks = nr_blocks;
	bm->read_count = 0;
	bm->write_count = 0;
	bm->blocks_allocated = 0;
	list_init(&bm->lru_list);
	list_init(&bm->locked_list);
	list_init(&bm->io_list);

	bm->cache_size = cache_size;
	bm->nr_buckets = next_power_of_2(cache_size);
	bm->buckets = malloc(sizeof(*bm->buckets) * bm->nr_buckets);
	if (!bm->buckets) {
		free(bm);
		return NULL;
	}
	for (i = 0; i < bm->nr_buckets; i++)
		list_init(bm->buckets + i);

	bm->trace = NULL;

	return bm;
}

void block_manager_destroy(struct block_manager *bm)
{
	struct list *l, *tmp;

	list_iterate_safe (l, tmp, &bm->lru_list) {
		struct block *b = list_struct_base(l, struct block, list);
		free_block(bm, b);
	}

	if (bm->trace)
		fclose(bm->trace);

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
		if (bm->trace)
			fprintf(bm->trace, "cache hit %u\n", (unsigned) block);
		*data = b->data;
		lock_block(bm, b, how);
	} else {
		b = alloc_block(bm);
		if (!b)
			return 0;

		b->where = block;

		if (need_read) {
			if (!read_block(bm, b) || !wait_io(bm, b, 1)) {
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
	if (how != BM_LOCK_WRITE)
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

	switch (b->type) {
	case BM_LOCK_WRITE:
		if (changed)
			b->dirty |= 1;
		break;

	case BM_LOCK_READ:
		if (changed)
			return 0;
		break;
	}
	unlock_block(bm, b);
	return 1;
}

int bm_flush(struct block_manager *bm, int block)
{
	/*
	 * Run through the lru list writing any dirty blocks.  We ignore
	 * locked blocks since they're still being updated.
	 */
	int r = 1;
	struct list *l, *tmp;

	list_iterate_safe (l, tmp, &bm->lru_list) {
		struct block *b = list_struct_base(l, struct block, list);
		if (b->dirty) {
			if (!write_block(bm, b))
				r = 0;
		}
	}

	wait_all_writes(bm);

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
		struct block *b = list_struct_base(l, struct block, list);
		if (b->type == t)
			n++;
	}

	return n;
}

unsigned bm_read_locks_held(struct block_manager *bm)
{
	return count_locks(bm, BM_LOCK_READ);
}

unsigned bm_write_locks_held(struct block_manager *bm)
{
	return count_locks(bm, BM_LOCK_WRITE);
}

int bm_start_io_trace(struct block_manager *bm, const char *file)
{
	assert(!bm->trace);
	bm->trace = fopen(file, "w");
	assert(bm->trace);
	fprintf(bm->trace, "nr_blocks %u\n", (unsigned) bm->nr_blocks);
	return 1;
}

int bm_io_mark(struct block_manager *bm, const char *token)
{
	fprintf(bm->trace, "mark \"%s\"\n", token);
	return 1;
}

/*----------------------------------------------------------------*/

