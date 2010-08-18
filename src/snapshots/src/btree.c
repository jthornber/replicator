#include "btree.h"
#include "btree_internal.h"

#include "mm/pool.h"
#include "snapshots/space_map.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*----------------------------------------------------------------*/

/* junk */

/* FIXME: substitute for real error handling */
static void barf(const char *msg)
{
	fprintf(stderr, "%s", msg);
	abort();
}

/* a little sanity check */
static void check_keys(block_t b, struct node *n)
{
	unsigned i;

	for (i = 0; i < n->header.nr_entries; i++) {
		if (i > 0)
			assert(n->keys[i] > n->keys[i - 1]);

		/* check for cycles */
		if (n->header.flags & INTERNAL_NODE)
			assert(n->values[i] != b);
	}
}

/* FIXME: use a binary search for these three */
static int lower_bound(struct node *n, uint64_t key)
{
	int i;
	for (i = n->header.nr_entries - 1; i >= 0; i--)
		if (n->keys[i] <= key)
			break;

	return i;
}

static int upper_bound(struct node *n, uint64_t key)
{
	int i;
	for (i = 0; i < n->header.nr_entries; i++)
		if (n->keys[i] >= key)
			break;

	return i;
}

static void inc_children(struct transaction_manager *tm, struct node *n, count_adjust_fn fn)
{
	unsigned i;
	if (n->header.flags & INTERNAL_NODE)
		for (i = 0; i < n->header.nr_entries; i++)
			tm_inc(tm, n->values[i]);
	else
		for (i = 0; i < n->header.nr_entries; i++)
			fn(tm, n->values[i], 1);
}

static void insert_at(struct node *node, unsigned index, uint64_t key, uint64_t value)
{
	if (index > node->header.nr_entries || index >= MAX_ENTRIES)
		abort();

	if ((node->header.nr_entries > 0) && (index < node->header.nr_entries)) {
		memmove(node->keys + index + 1, node->keys + index,
			(node->header.nr_entries - index) * sizeof(node->keys[0]));
		memmove(node->values + index + 1, node->values + index,
			(node->header.nr_entries - index) * sizeof(node->values[0]));
	}

	node->keys[index] = key;
	node->values[index] = value;
	node->header.nr_entries++;
}

/*----------------------------------------------------------------*/

int btree_empty(struct btree_info *info, block_t *root)
{
	struct node *node;

	if (!tm_new_block(info->tm, root, (void **) &node)) {
		/* FIXME: finish */
		return 0;
	}

	memset(node, 0, sizeof(*node));
	node->header.flags = LEAF_NODE;
	node->header.nr_entries = 0;

	tm_write_unlock(info->tm, *root);

	return 1;
}

static enum lookup_result
btree_lookup_raw(struct btree_info *info, block_t root,
		 uint64_t key, int (*search_fn)(struct node *, uint64_t),
		 uint64_t *result_key, uint64_t *value, block_t *leaf_block)
{
        int i;
        block_t block = root, parent = 0;
        struct node *node;

	do {
		if (!tm_read_lock(info->tm, block, (void **) &node)) {
			if (parent)
				tm_read_unlock(info->tm, parent);
			return LOOKUP_ERROR;
		}

		if (parent)
			tm_read_unlock(info->tm, parent);

		i = search_fn(node, key);
		if (i < 0 || i >= node->header.nr_entries) {
			tm_read_unlock(info->tm, block);
			return LOOKUP_NOT_FOUND;
		}

		parent = block;
		block = node->values[i];

        } while (!(node->header.flags & LEAF_NODE));

	*result_key = node->keys[i];
	*value = node->values[i];
	*leaf_block = parent;
	return LOOKUP_FOUND;
}

enum lookup_result
btree_lookup_equal(struct btree_info *info,
		   block_t root, uint64_t *keys,
		   uint64_t *value)
{
	unsigned level;
	enum lookup_result r;
	uint64_t rkey;
	block_t leaf, old_leaf = 0;

	for (level = 0; level < info->levels; level++) {
		r = btree_lookup_raw(info, root, keys[level], lower_bound, &rkey, value, &leaf);

		if (old_leaf)
			tm_read_unlock(info->tm, old_leaf);

		if (r == LOOKUP_FOUND) {
			if (rkey != keys[level]) {
				tm_read_unlock(info->tm, leaf);
				return LOOKUP_NOT_FOUND;
			}
		} else
			return r;

		old_leaf = leaf;
		root = *value;
	}

	tm_read_unlock(info->tm, leaf);
	return r;
}

enum lookup_result
btree_lookup_le(struct btree_info *info,
		block_t root, uint64_t *keys,
		uint64_t *key, uint64_t *value)
{
	unsigned level;
	enum lookup_result r;
	block_t leaf, old_leaf = 0;

	for (level = 0; level < info->levels; level++) {
		r = btree_lookup_raw(info, root, keys[level], lower_bound, key, value, &leaf);

		if (old_leaf)
			tm_read_unlock(info->tm, old_leaf);

		if (r != LOOKUP_FOUND)
			return r;

		old_leaf = leaf;
		root = *value;
	}

	tm_read_unlock(info->tm, old_leaf);
	return r;
}

enum lookup_result
btree_lookup_ge(struct btree_info *info,
		block_t root, uint64_t *keys,
		uint64_t *key, uint64_t *value)
{
	unsigned level;
	enum lookup_result r;
	block_t leaf, old_leaf = 0;

	for (level = 0; level < info->levels; level++) {
		r = btree_lookup_raw(info, root, keys[level], upper_bound, key, value, &leaf);

		if (old_leaf)
			tm_read_unlock(info->tm, old_leaf);

		if (r != LOOKUP_FOUND)
			return r;

		old_leaf = leaf;
		root = *value;
	}

	tm_read_unlock(info->tm, old_leaf);
	return r;
}

/*
 * Splits a full node.  Returning the desired side, indicated by |key|, in
 * |node_block| and |node|.
 */
static int btree_split(struct transaction_manager *tm, block_t block, count_adjust_fn fn,
		       struct node *node,
		       block_t parent_block, struct node *parent_node, unsigned parent_index,
		       block_t *result, struct node **result_node)
{
	int inc;
	unsigned nr_left, nr_right;
	block_t left_block, right_block;
	struct node *left, *right;

	/* the parent still has a write lock, so it's safe to drop this lock */
	tm_write_unlock(tm, block);

	if (!tm_shadow_block(tm, block, &left_block, (void **) &left, &inc))
		abort();

	if (inc)
		inc_children(tm, left, fn);

	if (!tm_new_block(tm, &right_block, (void **) &right))
		abort();

	nr_left = node->header.nr_entries / 2;
	nr_right = node->header.nr_entries - nr_left;

	left->header.nr_entries = nr_left;

	right->header.flags = node->header.flags;
	right->header.nr_entries = nr_right;
	memcpy(right->keys, node->keys + nr_left, nr_right * sizeof(right->keys[0]));
	memcpy(right->values, node->values + nr_left, nr_right * sizeof(right->values[0]));

	/* Patch up the parent */
	if (parent_node) {
		assert(parent_node->values[parent_index] == block);
		parent_node->values[parent_index] = left_block;

		memmove(parent_node->keys + parent_index + 1,
			parent_node->keys + parent_index,
			(parent_node->header.nr_entries - parent_index) * sizeof(parent_node->keys[0]));
		memmove(parent_node->values + parent_index + 1,
			parent_node->values + parent_index,
			(parent_node->header.nr_entries - parent_index) * sizeof(parent_node->values[0]));

		parent_node->keys[parent_index + 1] = right->keys[0];
		parent_node->values[parent_index + 1] = right_block;
		parent_node->header.nr_entries++;
		check_keys(parent_block, parent_node);

		*result = parent_block;
		*result_node = parent_node;

	} else {
		/* we need to create a new parent */
		block_t nb;
		struct node *nn;
		if (!tm_new_block(tm, &nb, (void **) &nn))
			barf("new node");

		nn->header.flags = INTERNAL_NODE;
		nn->header.nr_entries = 2;
		nn->keys[0] = left->keys[0];
		nn->values[0] = left_block;
		nn->keys[1] = right->keys[0];
		nn->values[1] = right_block;
		check_keys(nb, nn);

		*result = nb;
		*result_node = nn;
	}

	tm_write_unlock(tm, left_block);
	tm_write_unlock(tm, right_block);

	return 1;
}

static int btree_insert_raw(struct transaction_manager *tm, block_t root, count_adjust_fn fn, uint64_t key,
			    block_t *new_root, block_t *leaf,
			    struct node **leaf_node, unsigned *index)
{
        int i, parent_index = 0, inc;
        struct node *node, *parent_node = NULL;
	block_t *block, parent_block = 0, new_root_ = 0, tmp_block, dup_block;

	*new_root = root;
	block = new_root;

	for (;;) {
		if (!tm_shadow_block(tm, *block, block, (void **) &node, &inc)) {
			assert(0);
			/* FIXME: handle */
		}

		if (inc)
			inc_children(tm, node, fn);

		if (node->header.nr_entries == MAX_ENTRIES) {
			/* FIXME: horrible hack */
			tmp_block = *block;
			block = &tmp_block;
			if (!btree_split(tm, *block, fn, node,
					 parent_block, parent_node, parent_index,
					 block, &node))
				/* FIXME: handle this */
				assert(0);
		}

		dup_block = *block;
		if (parent_block && parent_block != *block) /* FIXME: hack */
			tm_write_unlock(tm, parent_block);

		i = lower_bound(node, key);

		if (node->header.flags & LEAF_NODE)
			break;

		if (i < 0) {
			/* change the bounds on the lowest key */
			node->keys[0] = key;
			i = 0;
		}

		if (!new_root_)
			new_root_ = *block;

		parent_index = i;
		parent_block = dup_block;
		parent_node = node;
		block = node->values + i;
        }

	/*
	 * We can't refer to *block here, since the parent_block has been
	 * unlocked (Valgrind spots this).  Hence the |dup_block| hack.
	 */
	if (new_root_)
		*new_root = new_root_;
	*leaf = dup_block;
	*leaf_node = node;

	if (i < 0 || node->keys[i] != key)
		i++;

	/* we're about to overwrite this value, so undo the increment for it */
	if (node->keys[i] == key && inc)
		fn(tm, node->values[i], -1);

	*index = i;
	return 1;
}

int btree_insert(struct btree_info *info, block_t root,
		 uint64_t *keys, uint64_t value,
		 block_t *new_root)
{
	int need_insert;
	unsigned level, index, last_level = info->levels - 1;
	block_t leaf, old_leaf = 0, *block;
	struct node *leaf_node;

	*new_root = root;
	block = new_root;

	for (level = 0; level < info->levels; level++) {
		if (!btree_insert_raw(info->tm, *block,
				      level == last_level ? info->adjust : value_is_block,
				      keys[level], block, &leaf, &leaf_node, &index))
			abort();

		if (old_leaf)
			tm_write_unlock(info->tm, old_leaf);

		need_insert = ((index >= leaf_node->header.nr_entries) ||
			       (leaf_node->keys[index] != keys[level]));

		if (level == last_level) {
			if (need_insert)
				insert_at(leaf_node, index, keys[level], value);
			else {
				info->adjust(info->tm, leaf_node->values[index], -1);
				leaf_node->values[index] = value;
			}
		} else {
			if (need_insert) {
				block_t new_root;
				if (!btree_empty(info, &new_root))
					abort();

				insert_at(leaf_node, index, keys[level], new_root);
			}
		}

		old_leaf = leaf;
		block = leaf_node->values + index;
	}

	tm_write_unlock(info->tm, leaf);
	return 1;
}

int btree_clone(struct btree_info *info, block_t root, block_t *clone)
{
	struct node *clone_node, *orig_node;

	/* Copy the root node */
	if (!tm_new_block(info->tm, clone, (void **) &clone_node))
		return 0;

	if (!tm_read_lock(info->tm, root, (void **) &orig_node))
		abort();

	memcpy(clone_node, orig_node, sizeof(*clone_node));
	tm_read_unlock(info->tm, root);
	inc_children(info->tm, clone_node, info->adjust);
	tm_write_unlock(info->tm, *clone);

	return 1;
}

void value_is_block(struct transaction_manager *tm, uint64_t value, int32_t delta)
{
	while (delta < 0) {
		tm_dec(tm, value);
		delta++;
	}

	while (delta > 0) {
		tm_inc(tm, value);
		delta--;
	}
}

void value_is_meaningless(struct transaction_manager *tm, uint64_t value, int32_t delta)
{
}

/*----------------------------------------------------------------*/

struct block_list {
	struct list list;
	block_t b;
};

static int btree_walk_(struct transaction_manager *tm, leaf_fn lf,
		       block_t root, unsigned levels, uint32_t *ref_counts, struct list *seen,
		       struct pool *mem)
{
	unsigned i;
	struct node *n;
	struct list *l;

	/*
	 * We increment even if we've seen this node, however we don't
	 * walk its children.
	 */
	ref_counts[root]++;
	list_iterate (l, seen) {
		struct block_list *bl = list_struct_base(l, struct block_list, list);
		if (bl->b == root)
			return 1;
	}

	{
		struct block_list *bl = pool_alloc(mem, sizeof(*bl));
		if (!bl)
			return 0;

		bl->b = root;
		list_add(seen, &bl->list);
	}

	if (!tm_read_lock(tm, root, (void **) &n)) {
		abort();
	}

	if (n->header.flags & INTERNAL_NODE)
		for (i = 0; i < n->header.nr_entries; i++)
			btree_walk_(tm, lf, n->values[i], levels, ref_counts, seen, mem);
	else {
		if (levels == 1)
			for (i = 0; i < n->header.nr_entries; i++)
				lf(n->values[i], ref_counts);
		else
			for (i = 0; i < n->header.nr_entries; i++)
				btree_walk_(tm, lf, n->values[i], levels - 1, ref_counts,
					    seen, mem);
	}

	tm_read_unlock(tm, root);

	return 1;
}

int btree_walk(struct transaction_manager *tm, leaf_fn lf,
	       block_t *roots, unsigned count, uint32_t *ref_counts)
{
	int r;
	unsigned i;
	struct list seen;
	struct pool *mem = pool_create("", 1024);

	if (!mem)
		return 0;

	list_init(&seen);
	for (i = 0; i < count; i++)
		r = btree_walk_(tm, lf, roots[i], 1, ref_counts, &seen, mem);
	pool_destroy(mem);
	return r;
}


int btree_walk_h(struct transaction_manager *tm, leaf_fn lf,
		 block_t *roots, unsigned count,
		 unsigned levels, uint32_t *ref_counts)
{
	int r;
	unsigned i;
	struct list seen;
	struct pool *mem = pool_create("", 1024);

	if (!mem)
		return 0;

	list_init(&seen);
	for (i = 0; i < count; i++)
		r = btree_walk_(tm, lf, roots[i], levels, ref_counts, &seen, mem);
	pool_destroy(mem);
	return r;
}

/*----------------------------------------------------------------*/
