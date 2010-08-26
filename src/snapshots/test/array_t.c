#include "snapshots/array.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*----------------------------------------------------------------*/

#define BLOCK_SIZE 4096
#define NR_BLOCKS 10240

/*
 * For these tests we're not interested in the space map root, so we roll
 * tm_pre_commit() and tm_commit() into one function.
 */
static void commit(struct transaction_manager *tm, block_t root)
{
	block_t sm_root;
	tm_pre_commit(tm, &sm_root);
	tm_commit(tm, root);
}

static void check_create(struct transaction_manager *tm)
{
	block_t root;
	unsigned i, count = 10240 * 3;
	unsigned char data[3], expected[3];

	tm_begin(tm);

	if (!array_empty(tm, &root, sizeof(data)))
		abort();

	if (!array_set_size(tm, root, count, &root))
		abort();

	memset(expected, 0, sizeof(expected));
	for (i = 0; i < count; i++) {
		if (!array_get(tm, root, i, data))
			abort();
		assert(!memcmp(data, expected, sizeof(data)));
	}

	commit(tm, root);
}

static void check_set(struct transaction_manager *tm)
{
	block_t root;
	unsigned i, count = 10240 * 3;
	unsigned char data[3], expected[3];

	tm_begin(tm);

	if (!array_empty(tm, &root, sizeof(data)))
		abort();

	if (!array_set_size(tm, root, count, &root))
		abort();


	for (i = 0; i < count; i++) {
		memset(data, i, sizeof(data));
		if (!array_set(tm, root, i, data, &root))
			abort();
	}

	for (i = 0; i < count; i++) {
		memset(expected, i, sizeof(expected));
		if (!array_get(tm, root, i, data))
			abort();
		assert(!memcmp(data, expected, sizeof(data)));
	}

	commit(tm, root);
}

static int open_file()
{
	int i;
	unsigned char data[BLOCK_SIZE];
	int fd = open("./block_file", O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0)
		abort();

	memset(data, 0, sizeof(data));
	for (i = 0; i < NR_BLOCKS; i++)
		write(fd, data, sizeof(data));

	fsync(fd);
	close(fd);

	fd = open("./block_file", O_RDWR | O_DIRECT);
	if (fd < 0)
		abort();

	return fd;
}

int main(int argc, char **argv)
{
	static struct {
		const char *name;
		void (*fn)(struct transaction_manager *);
	} table_[] = {
		{ "create", check_create },
		{ "set", check_set }
	};

	int i;
	struct block_manager *bm;
	struct transaction_manager *tm;

	for (i = 0; i < sizeof(table_) / sizeof(*table_); i++) {
		printf("running %s()\n", table_[i].name);

		bm = block_manager_create(open_file(), BLOCK_SIZE, NR_BLOCKS, 64);
		tm = tm_create(bm);

		table_[i].fn(tm);

		tm_destroy(tm);
		bm_dump(bm);
		block_manager_destroy(bm);
	}

	return 0;
}

/*----------------------------------------------------------------*/
