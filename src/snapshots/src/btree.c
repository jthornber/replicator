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

/* shadowing */

static int insert_shadow(struct btree *bt, block_t b, struct node *data)
{
	struct shadowed_block *tb = pool_alloc(bt->transaction->mem, sizeof(*tb));
	if (!tb) {
		/* FIXME: stuff */
		return 0;
	}

	tb->block = b;
	tb->data = data;
	list_add(&bt->transaction->shadowed_blocks, &tb->list);
	return 1;
}

/*
 * If this block hasn't already been shadowed in this transaction then:
 *
 * - allocate a new block from the in-core free space manager
 * - grab it with a write lock from the block manager
 * - grab a read lock on the original block
 * - copy the data from the original block to the new one
 * - store a record of this shadowing
 *
 * If it has already been shadowed then just return the previous shadowing.
 *
 * The transaction commit is responsible for unlocking shadowed pages.
 * FIXME: ???, who unlocks read locks then ?
 */
static int shadow_node_new(struct btree *bt, block_t original, block_t *new, struct node **data)
{
	block_t shadow;
	struct node *original_node, *shadow_node;

	if (!sm_new_block(bt->sm, &shadow))
		return 0;

	if (!block_lock(bt->bm, shadow, WRITE, (void **) &shadow_node))
		sm_dec_block(bt->sm, shadow);

	if (!block_lock(bt->bm, original, READ, (void **) &original_node)) {
		/* FIXME: stuff */
	}

	memcpy(shadow_node, original_node, BLOCK_SIZE);
	if (!block_unlock(bt->bm, original, 0)) {
		/* FIXME: stuff */
	}

	insert_shadow(bt, shadow, shadow_node);
	*new = shadow;
	*data = shadow_node;
	return 1;
}

static int shadow_node(struct btree *bt, block_t original, block_t *new, struct node **data)
{
	struct shadowed_block *sb;

	list_iterate_items (sb, &bt->transaction->shadowed_blocks)
		if (sb->block == original) {
			*new = original;
			*data = sb->data;
			return 1;
		}

	return shadow_node_new(bt, original, new, data);
}

/*
 * Like sm_new_block() except it inserts the block into the transactions
 * shadow list.
 */
static int new_block(struct btree *bt, block_t *result, struct node **node)
{
	if (!sm_new_block(bt->sm, result))
		barf("sm_new_block");

	if (!block_lock(bt->bm, *result, WRITE, (void **) node))
		barf("block_lock");

	return insert_shadow(bt, *result, *node);
}

/*----------------------------------------------------------------*/

static inline void block_unlock_safe(struct block_manager *bm,
				     block_t b, int dirty)
{
	if (b)
		block_unlock(bm, b, dirty);
}

static int init_leaf(struct btree *bt, block_t *b)
{
	struct node *node;

	/* Set up an empty tree */
	if (!new_block(bt, b, &node)) {
		/* FIXME: finish */
		return 0;
	}

	memset(node, 0, sizeof(*node));
	node->header.flags = LEAF_NODE;
	node->header.nr_entries = 0;

	return 1;
}

struct btree *btree_create(struct block_manager *bm)
{
	struct btree *bt = malloc(sizeof(*bt));
	if (!bt)
		return NULL;

	bt->bm = bm;
	bt->transaction = NULL;
	bt->sm = space_map_create(bm_nr_blocks(bm));
	if (!bt->sm) {
		free(bt);
		return NULL;
	}

	btree_begin(bt);
	init_leaf(bt, &bt->transaction->new_root);
	btree_commit(bt);

	return bt;
}

void btree_destroy(struct btree *bt)
{
	space_map_destroy(bt->sm);
	free(bt);
}

int btree_begin(struct btree *bt)
{
	struct pool *mem;
	struct transaction *t;

	if (!(mem = pool_create("", 1024)))
		barf("pool_create");

	if (!(t = pool_alloc(mem, sizeof(*t))))
		barf("pool_alloc");

	t->mem = mem;
	t->bt = bt;
	list_init(&t->shadowed_blocks);
	list_init(&t->free_blocks);
	t->new_root = bt->root;

	bt->transaction = t;
	return 1;
}

int btree_commit(struct btree *bt)
{
	struct transaction *t = bt->transaction;
	struct shadowed_block *sh;

	/* run through the shadow map committing everything */
	list_iterate_items (sh, &t->shadowed_blocks)
		block_unlock(bt->bm, sh->block, 1);

	/* swap in the root */
	bt->root = t->new_root;

	/* decrement the old root */
	/* FIXME: finish */

	/* destroy the transaction */
	pool_destroy(bt->transaction->mem);

	return 1;
}

int btree_rollback(struct btree *bt)
{
	/* FIXME: finish */
	return 0;
}

/*----------------------------------------------------------------*/

int lower_bound(struct node *n, uint64_t key)
{
	/* FIXME: use a binary search */
	int i;
	for (i = 0; i < n->header.nr_entries; i++)
		if (n->keys[i] > key)
			break;

	return i - 1;
}

int btree_lookup(struct btree *bt, uint64_t key,
		 uint64_t *key_result, uint64_t *result)
{
        int i;
        block_t node_block = bt->root, parent_block = 0;
        struct node *node;

	do {
		if (!block_lock(bt->bm, node_block, READ, (void **) &node)) {
			block_unlock_safe(bt->bm, parent_block, 0);
			return -1;
		}

		block_unlock_safe(bt->bm, parent_block, 0);
		i = lower_bound(node, key);
		assert(i >= 0);
		node_block = node->values[i];

        } while (!(node->header.flags & LEAF_NODE));

	*key_result = node->keys[i];
	*result = node->values[i];
	block_unlock(bt->bm, node_block, 0);

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
static int btree_split(struct btree *bt, block_t block, struct node *node,
		       block_t parent_block, struct node *parent_node, unsigned parent_index,
		       block_t *result, struct node **result_node)
{
	unsigned nr_left, nr_right;
	block_t left_block, right_block;
	struct node *left, *right;

	if (!new_block(bt, &left_block, &left))
		abort();

	if (!new_block(bt, &right_block, &right))
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
		if (!new_block(bt, &nb, &nn))
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

	return 1;
}

int btree_insert(struct btree *bt, uint64_t key, uint64_t value)
{
        int i, parent_index = 0;
        struct node *node, *parent_node = NULL;
	block_t *block, parent_block, new_root = 0, tmp_block;

	assert(bt->transaction->new_root);
	block = &bt->transaction->new_root;

	for (;;) {
		if (!shadow_node(bt, *block, block, &node)) {
			assert(0);
			/* FIXME: handle */
		}

		if (node->header.nr_entries == MAX_ENTRIES) {
			/* FIXME: horrible hack */
			tmp_block = *block;
			block = &tmp_block;
			if (!btree_split(bt, *block, node,
					 parent_block, parent_node, parent_index,
					 block, &node))
				/* FIXME: handle this */
				assert(0);
		}

		i = lower_bound(node, key);

		if (node->header.flags & LEAF_NODE)
			break;

		if (i < 0) {
			/* change the bounds on the lowest key */
			node->keys[0] = key;
			i = 0;
		}

		if (!new_root)
			new_root = *block;

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

	if (new_root)
		bt->transaction->new_root = new_root;
	check_keys(*block, node);

	return 1;
}

void btree_dump(struct btree *bt)
{
	bm_dump(bt->bm);
	sm_dump(bt->sm);
}

/*----------------------------------------------------------------*/

#if 0
int get_ref_count(struct btree *bt, block_t b)
{
	uint64_t key, ref_count;

	if (!btree_lookup(bt, bt->free_node_root, b, &key, &ref_count)) {
		assert(0);
		return -1;
	}

	return ref_count;
}

int inc_ref_count(struct btree *bt, block_t b)
{
        int r, parent_dirty;
        unsigned depth;
        block_t parent_block, current_block = bt->free_space_root;
        struct free_node *fn;

        if (!block_lock(bt->bm, bt->free_space_root, WRITE, &((void *) fn)))
                return -1;

        parent_dirty = 0;
        for (depth = 0; depth < ???; depth++) {
                if (depth > 0)
                        block_unlock(bt->bm, parent, parent_dirty);

                parent = current_block;

                if (fn->nr_entries == NR_INTERNAL_ENTRIES) {
                        /* split */

                        /* patch parent */

                }
        }
}
#endif
/*----------------------------------------------------------------*/


#if 0
struct transaction *trans_begin(struct btree *bt);

int trans_commit(struct transaction *t, block_t new_root);
int trans_rollback(struct transaction *t);

/*
 * Only need to shadow once per block, so has a small cache of shadowed
 * blocks.
 */
int trans_is_shadowed(struct transaction *t, block_t);
int trans_shadow(struct transaction *t, block_t b, block_t *new);

/*----------------------------------------------------------------*/

#endif
