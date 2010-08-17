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

#if 0
static void ignore_leaf(uint64_t leaf, uint32_t *ref_counts)
{
}
#endif
void check_reference_counts(struct transaction_manager *tm,
			    block_t *roots, unsigned count)
{
#if 0
	int i;
	block_t b, nr_blocks = bm_nr_blocks(tm_get_bm(tm));
	uint32_t *ref_counts = malloc(sizeof(*ref_counts) * nr_blocks);
        struct space_map *sm = tm_get_sm(tm);

	assert(ref_counts);

	memset(ref_counts, 0, sizeof(*ref_counts) * nr_blocks);
	for (i = 0; i < count; i++)
		btree_walk(tm, ignore_leaf, roots[i], ref_counts);
	sm_walk(sm, ref_counts);

	for (b = 0; b < nr_blocks; b++)
		if (ref_counts[b] != sm_get_count(sm, b))
			abort();
#endif
}

static void check_locks(struct transaction_manager *tm)
{
	unsigned rlocks = bm_read_locks_held(tm_get_bm(tm));
	unsigned wlocks = bm_write_locks_held(tm_get_bm(tm));

	if (rlocks)
		fprintf(stderr, "%u read locks remain\n", rlocks);

	if (wlocks)
		fprintf(stderr, "%u write locks remain\n", wlocks);

	if (rlocks || wlocks)
		abort();
}

static void check_insert(struct transaction_manager *tm)
{
	uint64_t value;
	struct number_list *nl;
	block_t root = 0;

	tm_begin(tm);
	if (!btree_empty(tm, &root))
		abort();

	list_iterate_items (nl, &randoms_)
		if (!btree_insert(tm, root, nl->key, nl->value, &root))
			barf("insert");
	tm_commit(tm, root);
	check_locks(tm);

	list_iterate_items (nl, &randoms_) {
		if (!btree_lookup_equal(tm, root, nl->key, &value))
			barf("lookup");

		assert(value == nl->value);
	}
	check_locks(tm);

	check_reference_counts(tm, &root, 1);
}

static void check_multiple_commits(struct transaction_manager *tm)
{
	unsigned i;
	uint64_t value;
	struct number_list *nl;
	block_t root = 0;

	struct block_manager *bm = tm_get_bm(tm);
	bm_start_io_trace(bm, "multiple_commits.trace");

	tm_begin(tm);
	if (!btree_empty(tm, &root))
		abort();

	i = 0;
	list_iterate_items (nl, &randoms_) {
		if (!btree_insert(tm, root, nl->key, nl->value, &root))
			barf("insert");
		if (i++ % 100 == 0) {
			bm_io_mark(bm, "commit");
			tm_commit(tm, root);
			tm_begin(tm);
		}
	}

	bm_io_mark(bm, "commit");
	tm_commit(tm, root);
	check_locks(tm);

	list_iterate_items (nl, &randoms_) {
		if (!btree_lookup_equal(tm, root, nl->key, &value))
			barf("lookup");

		assert(value == nl->value);
	}
	check_locks(tm);

	check_reference_counts(tm, &root, 1);
}

/*
 * The above routine generates a lot of writes, because the keys are
 * random.  This checks that contiguous keys create far fewer writes.
 */
static void check_multiple_commits_contiguous(struct transaction_manager *tm)
{
	int i;
	uint64_t key, value;
	struct number_list *nl;
	block_t root = 0;

	tm_begin(tm);
	if (!btree_empty(tm, &root))
		abort();

	i = 0;
	key = 0;
	list_iterate_items (nl, &randoms_) {
		if (!btree_insert(tm, root, key++, nl->value, &root))
			barf("insert");
		if (i++ % 100 == 0) {
			tm_commit(tm, root);
			tm_begin(tm);
		}
	}

	tm_commit(tm, root);
	check_locks(tm);

	key = 0;
	list_iterate_items (nl, &randoms_) {
		if (!btree_lookup_equal(tm, root, key++, &value))
			barf("lookup");

		assert(value == nl->value);
	}
	check_locks(tm);

	check_reference_counts(tm, &root, 1);
}

static void check_insert_h(struct transaction_manager *tm)
{
	typedef uint64_t table_entry[5];
	static table_entry table[] = {
		{ 1, 1, 1, 1, 100 },
		{ 1, 1, 1, 2, 101 },
		{ 1, 1, 1, 3, 102 },

		{ 1, 1, 2, 1, 200 },
		{ 1, 1, 2, 2, 201 },
		{ 1, 1, 2, 3, 202 },

		{ 2, 1, 1, 1, 301 },
		{ 2, 1, 1, 2, 302 },
		{ 2, 1, 1, 3, 303 }
	};

	static table_entry overwrites[] = {
		{ 1, 1, 1, 1, 1000 }
	};

	uint64_t value;
	block_t root = 0;
	int i;

	tm_begin(tm);
	if (!btree_empty(tm, &root))
		abort();

	for (i = 0; i < sizeof(table) / sizeof(*table); i++) {
		if (!btree_insert_h(tm, root, table[i], 4, table[i][4], &root))
			barf("insert");
	}
	tm_commit(tm, root);
	check_locks(tm);

	for (i = 0; i < sizeof(table) / sizeof(*table); i++) {
		if (!btree_lookup_equal_h(tm, root, table[i], 4, &value))
			barf("lookup");

		assert(value == table[i][4]);
	}
	check_locks(tm);

	/* check multiple transactions are ok */
	{
		uint64_t keys[4] = { 1, 1, 1, 4 }, value;

		tm_begin(tm);
		if (!btree_insert_h(tm, root, keys, 4, 2112, &root))
			barf("insert");
		tm_commit(tm, root);
		check_locks(tm);

		if (!btree_lookup_equal_h(tm, root, keys, 4, &value))
			barf("lookup");

		assert(value == 2112);
	}

	/* check overwrites */
	tm_begin(tm);
	for (i = 0; i < sizeof(overwrites) / sizeof(*overwrites); i++) {
		if (!btree_insert_h(tm, root, overwrites[i], 4, overwrites[i][4], &root))
			barf("insert");
	}
	tm_commit(tm, root);
	check_locks(tm);

	for (i = 0; i < sizeof(overwrites) / sizeof(*overwrites); i++) {
		if (!btree_lookup_equal_h(tm, root, overwrites[i], 4, &value))
			barf("lookup");

		assert(value == overwrites[i][4]);
	}
	check_locks(tm);
}

static void check_clone(struct transaction_manager *tm)
{
	uint64_t value;
	struct number_list *nl;
	block_t root, clone;

	tm_begin(tm);

	if (!btree_empty(tm, &root))
		abort();

	list_iterate_items (nl, &randoms_)
		if (!btree_insert(tm, root, nl->key, nl->value, &root))
			barf("insert");

	btree_clone(tm, root, &clone);
	assert(clone);

	tm_commit(tm, clone);
	check_locks(tm);

	list_iterate_items (nl, &randoms_) {
		if (!btree_lookup_equal(tm, clone, nl->key, &value))
			barf("lookup");

		assert(value == nl->value);
	}

	{
		block_t roots[2];
		roots[0] = root;
		roots[1] = clone;
		check_reference_counts(tm, roots, 2);
	}

	/* FIXME: try deleting |root| */
}

static int open_file()
{
	int i;
	unsigned char data[BLOCK_SIZE];
	int fd = open("./block_file", O_CREAT | O_TRUNC | O_RDWR | O_DIRECT, S_IRUSR | S_IWUSR);
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
		{ "check_multiple_commits", check_multiple_commits },
		{ "check_multiple_commits_contiguous", check_multiple_commits_contiguous },
		{ "check_clone", check_clone },
		{ "check_insert_h", check_insert_h }
	};

	int i;
	struct block_manager *bm;
	struct transaction_manager *tm;

	populate_randoms(10000);
	for (i = 0; i < sizeof(table_) / sizeof(*table_); i++) {
		printf("running %s()\n", table_[i].name);

		bm = block_manager_create(open_file(), BLOCK_SIZE, NR_BLOCKS, 1024);
		tm = tm_create(bm);

		table_[i].fn(tm);

		tm_destroy(tm);
		bm_dump(bm);
		block_manager_destroy(bm);
	}
	free_randoms(50);

	return 0;
}
