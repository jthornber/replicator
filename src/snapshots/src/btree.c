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

/*----------------------------------------------------------------*/

int btree_empty(struct transaction_manager *tm, block_t *root)
{
	struct node *node;

	if (!tm_new_block(tm, root, (void **) &node)) {
		/* FIXME: finish */
		return 0;
	}

	memset(node, 0, sizeof(*node));
	node->header.flags = LEAF_NODE;
	node->header.nr_entries = 0;

	tm_write_unlock(tm, *root);

	return 1;
}

static int lower_bound(struct node *n, uint64_t key)
{
	/* FIXME: use a binary search */
	int i;
	for (i = 0; i < n->header.nr_entries; i++)
		if (n->keys[i] > key)
			break;

	return i - 1;
}

int btree_lookup(struct transaction_manager *tm, uint64_t key, block_t root,
		 uint64_t *key_result, uint64_t *result)
{
        int i;
        block_t node_block = root, parent_block = 0;
        struct node *node;

	do {
		if (!tm_read_lock(tm, node_block, (void **) &node)) {
			if (parent_block)
				tm_read_unlock(tm, parent_block);
			return 0;
		}

		if (parent_block)
			tm_read_unlock(tm, parent_block);

		i = lower_bound(node, key);
		assert(i >= 0);
		node_block = node->values[i];

        } while (!(node->header.flags & LEAF_NODE));

	*key_result = node->keys[i];
	*result = node->values[i];
	tm_read_unlock(tm, node_block);

	return 1;
}

/*
 * Splits a full node.  Returning the desired side, indicated by |key|, in
 * |node_block| and |node|.
 *
 * FIXME: we should be decrementing blocks reference count, but we're not
 * yet incrementing them as part of the shadowing either.  So leave for
 * now.
 */
static int btree_split(struct transaction_manager *tm, block_t block, struct node *node,
		       block_t parent_block, struct node *parent_node, unsigned parent_index,
		       block_t *result, struct node **result_node)
{
	unsigned nr_left, nr_right;
	block_t left_block, right_block;
	struct node *left, *right;

	if (!tm_new_block(tm, &left_block, (void **) &left))
		abort();

	if (!tm_new_block(tm, &right_block, (void **) &right))
		abort();

	nr_left = node->header.nr_entries / 2;
	nr_right = node->header.nr_entries - nr_left;

	left->header.flags = node->header.flags;
	left->header.nr_entries = nr_left;
	memcpy(left->keys, node->keys, nr_left * sizeof(left->keys[0]));
	memcpy(left->values, node->values, nr_left * sizeof(left->values[0]));

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

static void inc_children(struct transaction_manager *tm, struct node *n)
{
	unsigned i;
	if (n->header.flags & INTERNAL_NODE)
		for (i = 0; i < n->header.nr_entries; i++)
			tm_inc(tm, n->values[i]);
}

int btree_insert(struct transaction_manager *tm, uint64_t key, uint64_t value,
		 block_t root, block_t *new_root)
{
        int i, parent_index = 0, duplicated;
        struct node *node, *parent_node = NULL;
	block_t *block, parent_block = 0, new_root_ = 0, tmp_block;

	*new_root = root;	/* The root block may already be a shadow */
	block = &root;

	for (;;) {
		if (!tm_shadow_block(tm, *block, block, (void **) &node, &duplicated)) {
			assert(0);
			/* FIXME: handle */
		}

		if (duplicated)
			inc_children(tm, node);

		if (node->header.nr_entries == MAX_ENTRIES) {
			/* FIXME: horrible hack */
			tmp_block = *block;
			block = &tmp_block;
			if (!btree_split(tm, *block, node,
					 parent_block, parent_node, parent_index,
					 block, &node))
				/* FIXME: handle this */
				assert(0);
		}

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
		parent_block = *block;
		parent_node = node;
		block = node->values + i;
        }

	/* so now we've got to a leaf node */
	if (i < 0) {
		/* insert at the start */
		memmove(node->keys + 1, node->keys,
			node->header.nr_entries * sizeof(node->keys[0]));
		memmove(node->values + 1, node->values,
			node->header.nr_entries * sizeof(node->values[0]));
		node->keys[0] = key;
		node->values[0] = value;

	} else if (node->keys[i] == key)
		node->values[i] = value; /* mutate the existing value */

	else {
		/* insert the new value _after_ i */
		i++;
		memmove(node->keys + i + 1, node->keys + i,
			(node->header.nr_entries - i) * sizeof(node->keys[0]));
		memmove(node->values + i + 1, node->values + i,
			(node->header.nr_entries - i) * sizeof(node->values[0]));
		node->keys[i] = key;
		node->values[i] = value;
	}
	node->header.nr_entries++;
	if (new_root_)
		*new_root = new_root_;

	check_keys(*block, node);
	tm_write_unlock(tm, *block);

	return 1;
}

int btree_clone(struct transaction_manager *tm, block_t root, block_t *clone)
{
	struct node *clone_node, *orig_node;

	/* Copy the root node */
	if (!tm_new_block(tm, clone, (void **) &clone_node))
		return 0;

	if (!tm_read_lock(tm, root, (void **) &orig_node))
		abort();

	memcpy(clone_node, orig_node, sizeof(*clone_node));
	inc_children(tm, clone_node);

	return 1;
}

/*----------------------------------------------------------------*/
