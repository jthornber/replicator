#include "snapshots/block_manager.h"

#include "datastruct/list.h"

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

/*----------------------------------------------------------------*/

struct block {
	struct list list;
	block_t where;
	enum block_lock type;
	void *data;
};

struct block *alloc_block(size_t block_size)
{
	struct block *b = malloc(sizeof(*b));
	if (!b)
		return NULL;

	b->data = malloc(block_size);
	if (!b->data) {
		free(b);
		return NULL;
	}

	return b;
}

void free_block(struct block *b)
{
	list_del(&b->list);
	free(b->data);
	free(b);
}

struct block *find_block(struct list *head, block_t b)
{
	struct block *bl;
	list_iterate_items (bl, head)
		if (bl->where == b)
			return bl;

	return NULL;
}

/*----------------------------------------------------------------*/

/* FIXME: locking is ignored for now, as is caching */
struct block_manager {
	int fd;
	size_t block_size;
	block_t nr_blocks;

	unsigned read_count;
	unsigned write_count;

	struct list blocks;
};

struct block_manager *
block_manager_create(int fd, size_t block_size, block_t nr_blocks)
{
	struct block_manager *bm = malloc(sizeof(*bm));
	if (!bm)
		return NULL;

	bm->fd = fd;
	bm->block_size = block_size;
	bm->nr_blocks = nr_blocks;
	list_init(&bm->blocks);

	return bm;
}

void block_manager_destroy(struct block_manager *bm)
{
	struct block *b, *tmp;
	list_iterate_items_safe (b, tmp, &bm->blocks)
		free_block(b);

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

	b = find_block(&bm->blocks, block);
	if (b) {
		fprintf(stderr, "block %u already locked for %s\n",
			(unsigned) block, b->type == LOCK_READ ? "read" : "write");
		abort();
	}

	b = alloc_block(bm->block_size);
	if (!b)
		return 0;

	if (lseek(bm->fd, block * bm->block_size, SEEK_SET) < 0) {
		free_block(b);
		return 0;
	}

	bm->read_count++;
	if (read(bm->fd, b->data, bm->block_size) < 0) {
		free_block(b);
		return 0;
	}

	b->where = block;
	b->type = how;
	list_add(&bm->blocks, &b->list);
	*data = b->data;

	return 1;
}

int bm_unlock(struct block_manager *bm, block_t block, int changed)
{
	struct block *b, *tmp;

	list_iterate_items_safe (b, tmp, &bm->blocks) {
		if (b->where == block) {
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
	}

	return 0;
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
	struct block *b;
	unsigned n = 0;

	list_iterate_items (b, &bm->blocks)
		if (b->type == t)
			n++;

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

