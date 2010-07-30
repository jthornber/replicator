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
#define NR_BLOCKS 102400

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

static void check_insert(struct transaction_manager *tm)
{
	uint64_t key, value;
	struct number_list *nl;
	block_t root = 0;

	tm_begin(tm);
	if (!btree_empty(tm, &root))
		abort();

	list_iterate_items (nl, &randoms_)
		if (!btree_insert(tm, nl->key, nl->value, root, &root))
			barf("insert");
	tm_commit(tm, root);

	list_iterate_items (nl, &randoms_) {
		if (!btree_lookup(tm, nl->key, root, &key, &value))
			barf("lookup");

		assert(key == nl->key);
		assert(value == nl->value);
	}
}

static void check_clone(struct transaction_manager *tm)
{
	uint64_t key, value;
	struct number_list *nl;
	block_t root, clone;

	tm_begin(tm);

	if (!btree_empty(tm, &root))
		abort();

	list_iterate_items (nl, &randoms_)
		if (!btree_insert(tm, nl->key, nl->value, root, &root))
			barf("insert");

	btree_clone(tm, root, &clone);
	assert(clone);

	tm_commit(tm, clone);

	list_iterate_items (nl, &randoms_) {
		if (!btree_lookup(tm, nl->key, clone, &key, &value))
			barf("lookup");

		assert(key == nl->key);
		assert(value == nl->value);
	}

	/* FIXME: try deleting |root| */
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
	static struct {
		const char *name;
		void (*fn)(struct transaction_manager *);
	} table_[] = {
		{ "check_insert", check_insert },
		{ "check_clone", check_clone }
	};

	int i;
	struct block_manager *bm;
	struct transaction_manager *tm;

	populate_randoms(10000);
	for (i = 0; i < sizeof(table_) / sizeof(*table_); i++) {
		printf("running %s()\n", table_[i].name);

		bm = block_manager_create(open_file(), BLOCK_SIZE, NR_BLOCKS);
		tm = tm_create(bm);

		table_[i].fn(tm);

		tm_destroy(tm);
		block_manager_destroy(bm);
	}
	free_randoms(50);

	return 0;
}
