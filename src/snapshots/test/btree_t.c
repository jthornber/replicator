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
	uint64_t key;
	uint64_t value;
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

#if 0
static void ignore_leaf(void *leaf, uint32_t *ref_counts)
{
}
#endif
void check_reference_counts_(struct btree_info *info,
			     block_t *roots, unsigned count,
			     uint32_t *ref_counts, block_t nr_blocks)
{
#if 0
	block_t b;
	struct transaction_manager *tm = info->tm;
        struct space_map *sm = tm_get_sm(tm);

	btree_walk(info, ignore_leaf, roots, count, ref_counts);
	sm_walk(sm, ref_counts);

	for (b = 0; b < nr_blocks; b++) {
		uint32_t count;
		if (!sm_get_count(sm, b, &count) || ref_counts[b] != count) {
			fprintf(stderr, "ref count mismatch for block %u, space map (%u), expected (%u)\n",
				(unsigned) b, count, ref_counts[b]);
			abort();
		}
	}
#endif
}

void check_reference_counts(struct btree_info *info,
			    block_t *roots, unsigned count)
{
	struct transaction_manager *tm = info->tm;
	block_t nr_blocks = bm_nr_blocks(tm_get_bm(tm));
	uint32_t *ref_counts = malloc(sizeof(*ref_counts) * nr_blocks);

	assert(ref_counts);
	memset(ref_counts, 0, sizeof(*ref_counts) * nr_blocks);
	check_reference_counts_(info, roots, count, ref_counts, nr_blocks);
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
	struct btree_info info;

	info.tm = tm;
	info.levels = 1;
	info.value_size = sizeof(uint64_t);
	info.adjust = value_is_meaningless;
	info.eq = NULL;

	tm_begin(tm);
	if (!btree_empty(&info, &root))
		abort();

	list_iterate_items (nl, &randoms_)
		if (!btree_insert(&info, root, &nl->key, &nl->value, &root))
			barf("insert");
	commit(tm, root);
	check_locks(tm);

	list_iterate_items (nl, &randoms_) {
		if (!btree_lookup_equal(&info, root, &nl->key, &value))
			barf("lookup");

		assert(value == nl->value);
	}
	check_locks(tm);

	check_reference_counts(&info, &root, 1);
}

static void check_multiple_commits(struct transaction_manager *tm)
{
	unsigned i;
	uint64_t value;
	struct number_list *nl;
	block_t root = 0;
	struct btree_info info;

	info.tm = tm;
	info.levels = 1;
	info.value_size = sizeof(uint64_t);
	info.adjust = value_is_meaningless;
	info.eq = NULL;

	struct block_manager *bm = tm_get_bm(tm);
	bm_start_io_trace(bm, "multiple_commits.trace");

	tm_begin(tm);
	if (!btree_empty(&info, &root))
		abort();

	i = 0;
	list_iterate_items (nl, &randoms_) {
		if (!btree_insert(&info, root, &nl->key, &nl->value, &root))
			barf("insert");
		if (i++ % 100 == 0) {
			bm_io_mark(bm, "commit");
			commit(tm, root);
			tm_begin(tm);
		}
	}

	bm_io_mark(bm, "commit");
	commit(tm, root);
	check_locks(tm);

	list_iterate_items (nl, &randoms_) {
		if (!btree_lookup_equal(&info, root, &nl->key, &value))
			barf("lookup");

		assert(value == nl->value);
	}
	check_locks(tm);

	check_reference_counts(&info, &root, 1);
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
	struct btree_info info;

	info.tm = tm;
	info.levels = 1;
	info.value_size = sizeof(uint64_t);
	info.adjust = value_is_meaningless;
	info.eq = NULL;

	tm_begin(tm);
	if (!btree_empty(&info, &root))
		abort();

	i = 0;
	key = 0;
	list_iterate_items (nl, &randoms_) {
		if (!btree_insert(&info, root, &key, &nl->value, &root))
			barf("insert");
		key++;
		if (i++ % 100 == 0) {
			commit(tm, root);
			tm_begin(tm);
		}
	}

	commit(tm, root);
	check_locks(tm);

	key = 0;
	list_iterate_items (nl, &randoms_) {
		if (!btree_lookup_equal(&info, root, &key, &value))
			barf("lookup");
		key++;
		assert(value == nl->value);
	}
	check_locks(tm);

	check_reference_counts(&info, &root, 1);
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
	struct btree_info info;

	info.tm = tm;
	info.levels = 4;
	info.value_size = sizeof(uint64_t);
	info.adjust = value_is_meaningless;
	info.eq = NULL;

	tm_begin(tm);
	if (!btree_empty(&info, &root))
		abort();

	for (i = 0; i < sizeof(table) / sizeof(*table); i++) {
		if (!btree_insert(&info, root, table[i], &table[i][4], &root))
			barf("insert");
	}
	commit(tm, root);
	check_locks(tm);

	for (i = 0; i < sizeof(table) / sizeof(*table); i++) {
		if (!btree_lookup_equal(&info, root, table[i], &value))
			barf("lookup");

		assert(value == table[i][4]);
	}
	check_locks(tm);

	/* check multiple transactions are ok */
	{
		uint64_t keys[4] = { 1, 1, 1, 4 }, value, v = 2112;

		tm_begin(tm);
		if (!btree_insert(&info, root, keys, &v, &root))
			barf("insert");
		commit(tm, root);
		check_locks(tm);

		if (!btree_lookup_equal(&info, root, keys, &value))
			barf("lookup");

		assert(value == 2112);
	}

	/* check overwrites */
	tm_begin(tm);
	for (i = 0; i < sizeof(overwrites) / sizeof(*overwrites); i++) {
		if (!btree_insert(&info, root, overwrites[i], &overwrites[i][4], &root))
			barf("insert");
	}
	commit(tm, root);
	check_locks(tm);

	for (i = 0; i < sizeof(overwrites) / sizeof(*overwrites); i++) {
		if (!btree_lookup_equal(&info, root, overwrites[i], &value))
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
	struct btree_info info;

	info.tm = tm;
	info.levels = 1;
	info.value_size = sizeof(uint64_t);
	info.adjust = value_is_meaningless;
	info.eq = NULL;

	tm_begin(tm);

	if (!btree_empty(&info, &root))
		abort();

	list_iterate_items (nl, &randoms_)
		if (!btree_insert(&info, root, &nl->key, &nl->value, &root))
			barf("insert");

	btree_clone(&info, root, &clone);
	assert(clone);

	commit(tm, clone);
	check_locks(tm);

	list_iterate_items (nl, &randoms_) {
		if (!btree_lookup_equal(&info, clone, &nl->key, &value))
			barf("lookup");

		assert(value == nl->value);
	}

	{
		block_t roots[2];
		roots[0] = root;
		roots[1] = clone;
		check_reference_counts(&info, roots, 2);
	}

	/* FIXME: try deleting |root| */
}

#define LBLOCKS 1000
static void check_leaf_ref_counts(struct transaction_manager *tm)
{
	uint64_t i;
	block_t root = 0, clone, blocks[LBLOCKS], extra_block, key = 47;
	struct btree_info info;

	info.tm = tm;
	info.levels = 1;
	info.value_size = sizeof(uint64_t);
	info.adjust = value_is_block;
	info.eq = NULL;

	tm_begin(tm);

	/* get some blocks */
	for (i = 0; i < LBLOCKS; i++)
		if (!tm_alloc_block(tm, blocks + i))
			abort();

	/* create a new btree */
	if (!btree_empty(&info, &root))
		abort();

	/* insert the blocks */
	for (i = 0; i < LBLOCKS; i++)
		if (!btree_insert(&info, root, &i, &blocks[i], &root))
			abort();

	/* now we clone, so there's sharing */
	if (!btree_clone(&info, root, &clone))
		abort();

	/*
	 * Finally we overwrite a value in the middle of a block.  This
	 * should force the sharing to occur at the leaves.  We expect
	 * reference counts in the space map to be 2 for blocks that share
	 * a leaf node with the new value.
	 */
	if (!tm_alloc_block(tm, &extra_block))
		abort();

	if (!btree_insert(&info, root, &key, &extra_block, &root))
		abort();

	commit(tm, root);

	{
		uint32_t count;
		struct space_map *sm = tm_get_sm(tm);

		assert(sm_get_count(sm, blocks[key], &count) && count == 0);
		assert(sm_get_count(sm, extra_block, &count) && count == 1);
		assert(sm_get_count(sm, blocks[key - 1], &count) && count == 2);
		assert(sm_get_count(sm, blocks[key + 1], &count) && count == 2);
	}
}

static void check_delete(struct transaction_manager *tm)
{
	struct number_list *nl;
	block_t root = 0;
	struct btree_info info;

	info.tm = tm;
	info.levels = 1;
	info.value_size = sizeof(uint64_t);
	info.adjust = value_is_meaningless;
	info.eq = NULL;

	tm_begin(tm);
	if (!btree_empty(&info, &root))
		abort();

	list_iterate_items (nl, &randoms_)
		if (!btree_insert(&info, root, &nl->key, &nl->value, &root))
			barf("insert");

	btree_del(&info, root);

	commit(tm, root);
	check_locks(tm);

	check_reference_counts(&info, NULL, 0);
}

static void check_delete_sep_trans(struct transaction_manager *tm)
{
	struct number_list *nl;
	block_t root = 0;
	struct btree_info info;

	info.tm = tm;
	info.levels = 1;
	info.value_size = sizeof(uint64_t);
	info.adjust = value_is_meaningless;
	info.eq = NULL;

	tm_begin(tm);
	if (!btree_empty(&info, &root))
		abort();

	list_iterate_items (nl, &randoms_)
		if (!btree_insert(&info, root, &nl->key, &nl->value, &root))
			barf("insert");

	commit(tm, root);
	check_locks(tm);

	tm_begin(tm);
	btree_del(&info, root);
	commit(tm, root);
	check_locks(tm);

	check_reference_counts(&info, NULL, 0);
}

static void check_delete_h(struct transaction_manager *tm)
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

	block_t root = 0;
	int i;
	struct btree_info info;

	info.tm = tm;
	info.levels = 4;
	info.value_size = sizeof(uint64_t);
	info.adjust = value_is_meaningless;
	info.eq = NULL;

	tm_begin(tm);
	if (!btree_empty(&info, &root))
		abort();

	for (i = 0; i < sizeof(table) / sizeof(*table); i++) {
		if (!btree_insert(&info, root, table[i], &table[i][4], &root))
			barf("insert");
	}
	btree_del(&info, root);

	commit(tm, root);
	check_locks(tm);

	check_reference_counts(&info, NULL, 0);
}

static void check_delete_clone(struct transaction_manager *tm)
{
	struct number_list *nl;
	block_t root = 0, clone;
	struct btree_info info;
	unsigned i;

	info.tm = tm;
	info.levels = 1;
	info.value_size = sizeof(uint64_t);
	info.adjust = value_is_meaningless;
	info.eq = NULL;

	tm_begin(tm);
	if (!btree_empty(&info, &root))
		abort();

	list_iterate_items (nl, &randoms_)
		if (!btree_insert(&info, root, &nl->key, &nl->value, &root))
			barf("insert");

	btree_clone(&info, root, &clone);

	/* overwrite some values so the two trees diverge nicely */
	i = 0;
	list_iterate_items (nl, &randoms_) {
		uint64_t zero;
		if (!btree_insert(&info, clone, &nl->key, &zero, &clone))
			abort();
		if (++i > 1000)
			break;
	}

	btree_del(&info, clone);

	commit(tm, root);
	check_locks(tm);

	check_reference_counts(&info, &root, 1);
}

static void check_value_size(struct transaction_manager *tm, size_t size)
{
	uint8_t value[1024], expected[1024];
	struct number_list *nl;
	block_t root = 0;
	struct btree_info info;
	int i;

	info.tm = tm;
	info.levels = 1;
	info.value_size = size;
	info.adjust = value_is_meaningless;
	info.eq = NULL;

	tm_begin(tm);
	if (!btree_empty(&info, &root))
		abort();

	i = 0;
	list_iterate_items (nl, &randoms_) {
		memset(value, i++, size);
		if (!btree_insert(&info, root, &nl->key, value, &root))
			barf("insert");
		if (i == 1000)
			break;
	}
	commit(tm, root);
	check_locks(tm);

	i = 0;
	list_iterate_items (nl, &randoms_) {
		if (!btree_lookup_equal(&info, root, &nl->key, &value))
			barf("lookup");

		memset(expected, i++, sizeof(expected));
		assert(!memcmp(expected, value, size));
		if (i == 1000)
			break;
	}
	check_locks(tm);

	check_reference_counts(&info, &root, 1);
}

static void check_value_size_1(struct transaction_manager *tm)
{
	check_value_size(tm, 1);
}

static void check_value_size_4(struct transaction_manager *tm)
{
	check_value_size(tm, 4);
}

static void check_value_size_7(struct transaction_manager *tm)
{
	check_value_size(tm, 7);
}

static void check_value_size_8(struct transaction_manager *tm)
{
	check_value_size(tm, 8);
}

static void check_value_size_433(struct transaction_manager *tm)
{
	check_value_size(tm, 433);
}

static void check_value_size_1024(struct transaction_manager *tm)
{
	check_value_size(tm, 1024);
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

	fsync(fd);
	close(fd);

	fd = open("./block_file", O_RDWR | O_DIRECT);
	if (fd < 0)
		barf("couldn't reopen block file");

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
		{ "check_insert_h", check_insert_h },
		{ "check leaf ref counts", check_leaf_ref_counts },
		{ "check delete", check_delete },
		{ "check delete in a separate transaction", check_delete_sep_trans },
		{ "check delete of a hierarchical btree", check_delete_h },
		{ "check delete of a clone", check_delete_clone },
		{ "check value size = 1", check_value_size_1 },
		{ "check value size = 4", check_value_size_4 },
		{ "check value size = 7", check_value_size_7 },
		{ "check value size = 8", check_value_size_8 },
		{ "check value size = 433", check_value_size_433 },
		{ "check value size = 1024", check_value_size_1024 }
	};

	int i;
	struct block_manager *bm;
	struct transaction_manager *tm;

	populate_randoms(10000);
	for (i = 0; i < sizeof(table_) / sizeof(*table_); i++) {
		printf("running %s\n", table_[i].name);

		bm = block_manager_create(open_file(), BLOCK_SIZE, NR_BLOCKS, 128);
		tm = tm_create(bm);

		table_[i].fn(tm);

		tm_destroy(tm);
		bm_dump(bm);
		block_manager_destroy(bm);
	}
	free_randoms(50);

	return 0;
}
