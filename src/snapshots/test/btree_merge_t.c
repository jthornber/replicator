#include "snapshots/btree_internal.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BLOCK_SIZE 4096
#define NR_BLOCKS 128

/*----------------------------------------------------------------*/

static void fail(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	abort();
}

/*
 * For these tests we're not interested in the space map root, so we roll
 * tm_pre_commit() and tm_commit() into one function.
 */
static void commit(struct transaction_manager *tm, block_t root)
{
	block_t sm_bitmap_root, sm_ref_count_root;
	tm_pre_commit(tm, &sm_bitmap_root, &sm_ref_count_root);
	tm_commit(tm, root);
}

static void test_no_current(struct btree_info *info)
{
	struct shadow_spine spine;

	init_shadow_spine(&spine, info);

	if (btree_merge(&spine, 0, info->value_size))
		abort();

	if (!exit_shadow_spine(&spine))
		abort();
}

static void test_no_parent(struct btree_info *info)
{
	int inc;
	struct shadow_spine spine;
	struct transaction_manager *tm = info->tm;
	block_t b;

	init_shadow_spine(&spine, info);
	tm_begin(tm);

	if (!tm_alloc_block(tm, &b))
		abort();

	if (!shadow_step(&spine, b, value_is_meaningless, &inc))
		abort();

	if (btree_merge(&spine, 0, info->value_size))
		abort();

	if (!exit_shadow_spine(&spine))
		abort();

	commit(tm, b);
}

static void init_header(struct node *n,
			struct btree_info *info,
			enum node_flags flags)
{
	n->header.flags = flags;
	n->header.nr_entries = 0;
	n->header.max_entries = flags & INTERNAL_NODE ?
		calc_max_entries(sizeof(uint64_t), BLOCK_SIZE) :
		calc_max_entries(info->value_size, BLOCK_SIZE);
	n->header.padding = 0;
}

/* used to make sure keys and values are different */
static uint64_t mystery(uint64_t n)
{
	return (n & 1) ? (3 * n + 1) : (n / 2);
}

static void insert_keys(struct node *n, struct btree_info *info,
			uint64_t key_begin, uint64_t key_end)
{
	uint64_t i;

	for (i = 0; i < key_end - key_begin; i++) {
		n->keys[i] = i + key_begin;
		*((uint64_t *) value_ptr(n, i, info->value_size)) = mystery(i + key_begin);
	}
	n->header.nr_entries = key_end - key_begin;
}

static void test_merge(struct btree_info *info,
		       unsigned nr_left,
		       unsigned nr_current,
		       unsigned nr_right)
{
	unsigned pindex, pentries;
	struct block_node pbn, lbn, cbn, rbn;

	tm_begin(info->tm);

	/*
	 * Build a fragment of btree according to the counts given in the
	 * params.
	 */
	if (!(new_block(info, &pbn) &&
	      new_block(info, &lbn) &&
	      new_block(info, &cbn) &&
	      new_block(info, &rbn)))
		abort();

	init_header(pbn.n, info, INTERNAL_NODE);
	init_header(lbn.n, info, LEAF_NODE);
	init_header(cbn.n, info, LEAF_NODE);
	init_header(rbn.n, info, LEAF_NODE);

	insert_keys(lbn.n, info, 0, nr_left);
	insert_keys(cbn.n, info, nr_left, nr_left + nr_current);
	insert_keys(rbn.n, info, nr_left + nr_current, nr_left + nr_current + nr_right);

	if (nr_left && nr_right) {
		insert_at(sizeof(uint64_t), pbn.n, 0, lbn.n->keys[0], &lbn.b);
		insert_at(sizeof(uint64_t), pbn.n, 1, cbn.n->keys[0], &cbn.b);
		insert_at(sizeof(uint64_t), pbn.n, 2, rbn.n->keys[0], &rbn.b);
		pindex = 1;
		pentries = 3;

	} else if (nr_left) {
		insert_at(sizeof(uint64_t), pbn.n, 0, lbn.n->keys[0], &lbn.b);
		insert_at(sizeof(uint64_t), pbn.n, 1, cbn.n->keys[0], &cbn.b);
		pindex = 1;
		pentries = 2;

	} else if (nr_right) {
		insert_at(sizeof(uint64_t), pbn.n, 0, cbn.n->keys[0], &cbn.b);
		insert_at(sizeof(uint64_t), pbn.n, 1, rbn.n->keys[0], &rbn.b);
		pindex = 0;
		pentries = 2;

	} else
		/* can't happen? */
		abort();

	unlock(info, &pbn);
	unlock(info, &lbn);
	unlock(info, &cbn);
	unlock(info, &rbn);

	/*
	 * Merge the node.
	 */
	{
		int inc;
		struct shadow_spine spine;
		init_shadow_spine(&spine, info);
		if (!shadow_step(&spine, pbn.b, value_is_block, &inc))
			abort();

		if (!shadow_step(&spine, cbn.b, value_is_meaningless, &inc))
			abort();

		if (!btree_merge(&spine, pindex, info->value_size))
			abort();

		if (!exit_shadow_spine(&spine))
			abort();
	}

	/*
	 * Check the tree is still legal, and contains all we'd expect.
	 */
	{
		int i;
		uint64_t key = 0, k;
		struct ro_spine spine;

		init_ro_spine(&spine, info);

		ro_step(&spine, pbn.b);
		for (i = 0; i < pbn.n->header.nr_entries; i++) {
			struct block_node child;

			bn_init(&child, value64(pbn.n, i));

			if (!read_lock(info, &child))
				abort();

			for (k = 0; k < child.n->header.nr_entries; k++, key++) {
				assert(child.n->keys[k] == key);
				assert(value64(child.n, k) == mystery(key));
			}

			if (!unlock(info, &child))
				abort();
		}

		assert(key == nr_left + nr_current + nr_right);
		assert(pentries > pbn.n->header.nr_entries);

		if (!exit_ro_spine(&spine))
			abort();
	}

	commit(info->tm, 0);
}

static int open_file()
{
	int i;
	unsigned char data[BLOCK_SIZE];
	int fd = open("./block_file", O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0)
		fail("couldn't open block file");

	memset(data, 0, sizeof(data));
	for (i = 0; i < NR_BLOCKS; i++)
		write(fd, data, sizeof(data));

	fsync(fd);
	close(fd);

	fd = open("./block_file", O_RDWR | O_DIRECT);
	if (fd < 0)
		fail("couldn't reopen block file");

	return fd;
}

#define THRESHOLD 127

static void test1(struct btree_info *info)
{
	test_merge(info, THRESHOLD, THRESHOLD, 0);
}

static void test2(struct btree_info *info)
{
	test_merge(info, 0, THRESHOLD, THRESHOLD);
}

static void test3(struct btree_info *info)
{
	test_merge(info, THRESHOLD, THRESHOLD, THRESHOLD);
}

int main(int argc, char **argv)
{
	static struct {
		const char *name;
		void (*fn)(struct btree_info *info);

	} table_[] = {
		{ "merge empty spine", test_no_current },
		{ "merge no parent", test_no_parent },
		{ "merge left (<=t) + current", test1 },
		{ "merge current + right (<=t)", test2 },
		{ "merge left (<=t) + current + right (<=t)", test3 }};
	// FIXME: add tests for > THRESHOLD

	int t;
	struct btree_info info;
	struct block_manager *bm;
	struct transaction_manager *tm;

	info.levels = 1;
	info.value_size = sizeof(uint64_t);
	info.adjust = value_is_meaningless;
	info.eq = NULL;

	for (t = 0; t < sizeof(table_) / sizeof(*table_); t++) {
		bm = block_manager_create(open_file(), BLOCK_SIZE, NR_BLOCKS, 16);
		tm = tm_create(bm);
		info.tm = tm;

		fprintf(stderr, "%s ... ", table_[t].name);
		table_[t].fn(&info);
		fprintf(stderr, "done\n");
	}

	return 0;
}
