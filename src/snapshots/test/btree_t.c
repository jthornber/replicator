#include "snapshots/btree.h"

#include "datastruct/list.h"
#include "mm/pool.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BLOCK_SIZE 4096
#define NR_BLOCKS 10240

static struct pool *mem_;
static struct list randoms_;

struct number_list {
	struct list list;
	unsigned key;
	unsigned value;
};

static void barf(const char *msg)
{
	fprintf(stderr, "%s", msg);
	abort();
}

static void populate_randoms(unsigned count)
{
	unsigned i;

	mem_ = pool_create("", 1024);

	list_init(&randoms_);
	for (i = 0; i < count; i++) {
		struct number_list *nl = pool_alloc(mem_, sizeof(*nl));
		nl->key = random();
		nl->value = random();
		list_add(&randoms_, &nl->list);
	}
}

static void free_randoms()
{
	pool_destroy(mem_);
}

static void check_insert(struct btree *bt)
{
	uint64_t key, value;
	struct number_list *nl;

	btree_begin(bt);
	list_iterate_items (nl, &randoms_)
		if (!btree_insert(bt, nl->key, nl->value))
			barf("insert");
	btree_commit(bt);

	list_iterate_items (nl, &randoms_) {
		if (!btree_lookup(bt, nl->key, &key, &value))
			barf("lookup");

		assert(key == nl->key);
		assert(value == nl->value);
	}
}

static int open_file()
{
	int i;
	unsigned char data[BLOCK_SIZE];
	int fd = open("./block_file", O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0)
		barf("couldn't open block file");

	memset(data, 0, sizeof(data));
	for (i = 0; i < NR_BLOCKS; i++)
		write(fd, data, sizeof(data));

	return fd;
}

int main(int argc, char **argv)
{
	struct block_manager *bm = block_manager_create(open_file(), BLOCK_SIZE, NR_BLOCKS);
	struct btree *bt = btree_create(bm);

	populate_randoms(10000);
	btree_dump(bt);
	printf("running check_insert()\n");
	check_insert(bt);
	btree_dump(bt);
	btree_destroy(bt);
	block_manager_destroy(bm);
	free_randoms(50);

	return 0;
}
