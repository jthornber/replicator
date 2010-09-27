#include "btree.h"
#include "btree_internal.h"

#include "mm/pool.h"
#include "snapshots/space_map.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*----------------------------------------------------------------*/

/* array manipulation */

static void array_insert(void *base, size_t elt_size, unsigned nr_elts,
			 unsigned index, void *elt)
{
	if (index < nr_elts)
		memmove(base + (elt_size * (index + 1)),
			base + (elt_size * index),
			(nr_elts - index) * elt_size);
	memcpy(base + (elt_size * index), elt, elt_size);
}

/*----------------------------------------------------------------*/

static void *value_base(struct node *n)
{
	return &n->keys[n->header.max_entries];
}

static void *value_ptr(struct node *n, uint32_t index, size_t value_size)
{
	return value_base(n) + (value_size * index);
}

/* assumes the values are suitably aligned */
static uint64_t value64(struct node *n, uint32_t index)
{
	uint64_t *values = value_base(n);
	return values[index];
}

/* a little sanity check */
static void check_keys(struct btree_info *info, block_t b, struct node *n)
{
	unsigned i;

	for (i = 0; i < n->header.nr_entries; i++) {
		if (i > 0)
			assert(n->keys[i] > n->keys[i - 1]);

		/* check for cycles */
		if (n->header.flags & INTERNAL_NODE)
			assert(value64(n, i) != b);
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

static void inc_children(struct btree_info *info, struct node *n, count_adjust_fn fn)
{
	unsigned i;
	if (n->header.flags & INTERNAL_NODE)
		for (i = 0; i < n->header.nr_entries; i++)
			tm_inc(info->tm, value64(n, i));
	else
		for (i = 0; i < n->header.nr_entries; i++)
			fn(info->tm, value_ptr(n, i, info->value_size), 1);
}

static void insert_at(size_t value_size,
		      struct node *node, unsigned index, uint64_t key, void *value)
{
	if (index > node->header.nr_entries || index >= node->header.max_entries)
		abort();

	array_insert(node->keys, sizeof(*node->keys), node->header.nr_entries, index, &key);
	array_insert(value_base(node), value_size, node->header.nr_entries, index, value);
	node->header.nr_entries++;
}

/*----------------------------------------------------------------*/

enum bn_state {
	BN_UNLOCKED,
	BN_READ_LOCKED,
	BN_WRITE_LOCKED
};

struct block_node {
	enum bn_state state;
	block_t b;
	struct node *n;
};

static int read_lock(struct btree_info *info, struct block_node *bn)
{
	int r;

	assert(bn->state == BN_UNLOCKED);
	r = tm_read_lock(info->tm, bn->b, (void **) &bn->n);
	if (r)
		bn->state = BN_READ_LOCKED;

	return 1;
}

static int shadow(struct btree_info *info, struct block_node *bn, count_adjust_fn fn, int *inc)
{
	int r;

	assert(bn->state == BN_UNLOCKED);
	r = tm_shadow_block(info->tm, bn->b, &bn->b, (void **) &bn->n, inc);
	if (r && *inc)
		inc_children(info, bn->n, fn);

	bn->state = BN_WRITE_LOCKED;
	return 1;
}

static int new_block(struct btree_info *info, struct block_node *bn)
{
	int r = tm_new_block(info->tm, &bn->b, (void **) &bn->n);
	bn->state = r ? BN_WRITE_LOCKED : BN_UNLOCKED;
	return r;
}

static int unlock(struct btree_info *info, struct block_node *bn)
{
	switch (bn->state) {
	case BN_READ_LOCKED:
		return tm_read_unlock(info->tm, bn->b);

	case BN_WRITE_LOCKED:
		return tm_write_unlock(info->tm, bn->b);

	default:
		abort();
	}

	/* never get here */
	return 0;
}

/*----------------------------------------------------------------*/

struct ro_spine {
	struct btree_info *info;

	int count;
	struct block_node nodes[2];
};

static void init_ro_spine(struct ro_spine *s, struct btree_info *info)
{
	s->info = info;
	s->count = 0;
	s->nodes[0].state = BN_UNLOCKED;
	s->nodes[1].state = BN_UNLOCKED;
}

static int exit_ro_spine(struct ro_spine *s)
{
	int r = 1, i;

	for (i = 0; i < s->count; i++)
		r |= unlock(s->info, s->nodes + i);

	return r;
}

static int ro_step(struct ro_spine *s, block_t new_child)
{
	int r;
	struct block_node *n;

	if (s->count == 2) {
		if (!unlock(s->info, s->nodes))
			return 0;
		memcpy(s->nodes, s->nodes + 1, sizeof(*s->nodes));
		s->nodes[1].state = BN_UNLOCKED;
		s->count--;
	}

	n = s->nodes + s->count;
	n->b = new_child;

	r = read_lock(s->info, n);
	if (r)
		s->count++;

	return r;
}

static struct node *ro_node(struct ro_spine *s)
{
	struct block_node *n;
	assert(s->count);
	n = s->nodes + (s->count - 1);
	return n->n;
}

/*----------------------------------------------------------------*/

struct shadow_spine {
	struct btree_info *info;

	int count;
	struct block_node nodes[2];

	block_t root;
};

#if 0
static void display_bn(struct block_node *bn)
{
	fprintf(stderr, "bn(%u, %p)", (unsigned) bn->b, bn->n);
}

static void display_root(struct shadow_spine *s)
{
	fprintf(stderr, "root(%u)", (unsigned) s->root);
}

static void display_shadow_spine(struct shadow_spine *s)
{
	fprintf(stderr, "shadow spine: ");

	switch (s->count) {
	case 0:
		fprintf(stderr, "no entries");
		break;

	case 1:
		display_bn(s->nodes);
		fprintf(stderr, ", ");
		display_root(s);
		break;

	case 2:
		display_bn(s->nodes);
		fprintf(stderr, ", ");
		display_bn(s->nodes + 1);
		fprintf(stderr, ", ");
		display_root(s);
		break;
	}

	fprintf(stderr, "\n");
}
#endif

static void init_shadow_spine(struct shadow_spine *s, struct btree_info *info)
{
	s->info = info;
	s->count = 0;
	s->nodes[0].state = BN_UNLOCKED;
	s->nodes[1].state = BN_UNLOCKED;
}

static int exit_shadow_spine(struct shadow_spine *s)
{
	int r = 1, i;

	for (i = 0; i < s->count; i++)
		r |= unlock(s->info, s->nodes + i);

	return r;
}

static int shadow_step(struct shadow_spine *s, block_t b, count_adjust_fn fn, int *inc)
{
	int r;
	struct block_node *n;

	if (s->count == 2) {
		if (!unlock(s->info, s->nodes))
			return 0;
		memcpy(s->nodes, s->nodes + 1, sizeof(*s->nodes));
		s->nodes[1].state = BN_UNLOCKED;
		s->count--;

	}

	n = s->nodes + s->count;
	n->b = b;

	r = shadow(s->info, n, fn, inc);

	if (s->count == 0)
		s->root = n->b;

	if (r)
		s->count++;

	return r;
}

static struct block_node *shadow_current(struct shadow_spine *s)
{
	return s->nodes + (s->count - 1);
}

static struct block_node *shadow_parent(struct shadow_spine *s)
{
	return s->count == 2 ? s->nodes : NULL;
}

static int shadow_root(struct shadow_spine *s)
{
	return s->root;
}

/*----------------------------------------------------------------*/

static uint32_t calc_max_entries(size_t value_size, size_t block_size)
{
	uint32_t n;
	size_t elt_size = sizeof(uint64_t) + value_size;
	block_size -= sizeof(struct node_header);
	n = block_size / elt_size;
	if (n % 2 == 0)
		--n;
	return n;
}

int btree_empty(struct btree_info *info, block_t *root)
{
	struct node *node;

	if (!tm_new_block(info->tm, root, (void **) &node)) {
		/* FIXME: finish */
		return 0;
	}

	memset(node, 0, BLOCK_SIZE);
	node->header.flags = LEAF_NODE;
	node->header.nr_entries = 0;
	node->header.max_entries = calc_max_entries(info->value_size, BLOCK_SIZE);

	tm_write_unlock(info->tm, *root);

	return 1;
}

/*
 * A simple recursive implementation of tree deletion, we'll need to use an
 * iterative walk before we move this into the kernel.
 */
int btree_del_(struct btree_info *info, block_t root, unsigned level)
{
	struct node *n;
	uint32_t ref_count = tm_ref(info->tm, root);

	if (ref_count == 1) {
		unsigned i;

		/*
		 * We know this node isn't shared, so we can get away with
		 * just a read lock.
		 */
		if (!tm_read_lock(info->tm, root, (void **) &n))
			abort();

		if (n->header.flags & INTERNAL_NODE) {
			for (i = 0; i < n->header.nr_entries; i++)
				if (!btree_del_(info, value64(n, i), level))
					return 0;

		} else if (level < (info->levels - 1)) {
			for (i = 0; i < n->header.nr_entries; i++)
				if (!btree_del_(info, value64(n, i), level + 1))
					return 0;

		} else
			for (i = 0; i < n->header.nr_entries; i++)
				info->adjust(info->tm, value_ptr(n, i, info->value_size), -1);

		if (!tm_read_unlock(info->tm, root))
			abort();
	}

	tm_dec(info->tm, root);
	return 1;
}

int btree_del(struct btree_info *info, block_t root)
{
	return btree_del_(info, root, 0);
}

static enum lookup_result
btree_lookup_raw(struct ro_spine *s, block_t block, uint64_t key, int (*search_fn)(struct node *, uint64_t),
		 uint64_t *result_key, void *v, size_t value_size)
{
	int i;

	do {
		if (!ro_step(s, block)) {
			exit_ro_spine(s);
			return LOOKUP_ERROR;
		}

		i = search_fn(ro_node(s), key);
		if (i < 0 || i >= ro_node(s)->header.nr_entries) {
			exit_ro_spine(s);
			return LOOKUP_NOT_FOUND;
		}

		if (ro_node(s)->header.flags & INTERNAL_NODE)
			block = value64(ro_node(s), i);

        } while (!(ro_node(s)->header.flags & LEAF_NODE));

	*result_key = ro_node(s)->keys[i];
	memcpy(v, value_ptr(ro_node(s), i, value_size), value_size);
	return LOOKUP_FOUND;
}

enum lookup_result
btree_lookup_equal(struct btree_info *info,
		   block_t root, uint64_t *keys,
		   void *value)
{
	unsigned level, last_level = info->levels - 1;
	enum lookup_result r;
	uint64_t rkey, internal_value;
	struct ro_spine spine;

	init_ro_spine(&spine, info);
	for (level = 0; level < info->levels; level++) {
		r = btree_lookup_raw(&spine, root, keys[level], lower_bound, &rkey,
				     level == last_level ? value : &internal_value,
				     level == last_level ? info->value_size : sizeof(uint64_t));

		if (r == LOOKUP_FOUND) {
			if (rkey != keys[level]) {
				exit_ro_spine(&spine);
				return LOOKUP_NOT_FOUND;
			}
		} else {
			exit_ro_spine(&spine);
			return r;
		}

		root = internal_value;
	}

	exit_ro_spine(&spine);
	return r;
}

enum lookup_result
btree_lookup_le(struct btree_info *info,
		block_t root, uint64_t *keys,
		uint64_t *key, void *value)
{
	unsigned level, last_level = info->levels - 1;
	enum lookup_result r;
	uint64_t internal_value;

	struct ro_spine spine;

	init_ro_spine(&spine, info);
	for (level = 0; level < info->levels; level++) {
		r = btree_lookup_raw(&spine, root, keys[level], lower_bound, key,
				     level == last_level ? value : &internal_value,
				     level == last_level ? info->value_size : sizeof(uint64_t));

		if (r != LOOKUP_FOUND) {
			exit_ro_spine(&spine);
			return r;
		}

		root = internal_value;
	}

	exit_ro_spine(&spine);
	return r;
}

enum lookup_result
btree_lookup_ge(struct btree_info *info,
		block_t root, uint64_t *keys,
		uint64_t *key, void *value)
{
	unsigned level, last_level = info->levels - 1;
	enum lookup_result r;
	uint64_t internal_value;
	struct ro_spine spine;

	init_ro_spine(&spine, info);
	for (level = 0; level < info->levels; level++) {
		r = btree_lookup_raw(&spine, root, keys[level], upper_bound, key,
				     level == last_level ? value : &internal_value,
				     level == last_level ? info->value_size : sizeof(uint64_t));
		if (r != LOOKUP_FOUND) {
			exit_ro_spine(&spine);
			return r;
		}

		root = internal_value;
	}

	exit_ro_spine(&spine);
	return r;
}

/*
 * Splits a full node.  Has knowledge of the shadow_spine structure.
 */
static int btree_split(struct shadow_spine *s, block_t root, count_adjust_fn fn,
		       unsigned parent_index, uint64_t key)
{
	size_t size;
	unsigned nr_left, nr_right;
	struct block_node *left, *right, *parent, nnb;
	struct node *l, *r;

	left = shadow_current(s);
	assert(left);

	if (!new_block(s->info, &nnb))
		return 0;

	right = &nnb;

	l = left->n;
	r = right->n;

	nr_left = l->header.nr_entries / 2;
	nr_right = l->header.nr_entries - nr_left;

	l->header.nr_entries = nr_left;

	r->header.flags = l->header.flags;
	r->header.nr_entries = nr_right;
	r->header.max_entries = l->header.max_entries;
	memcpy(r->keys, l->keys + nr_left, nr_right * sizeof(r->keys[0]));

	size = l->header.flags & INTERNAL_NODE ? sizeof(uint64_t) : s->info->value_size;
	memcpy(value_ptr(r, 0, size), value_ptr(l, nr_left, size), size * nr_right);

	/* Patch up the parent */
	parent = shadow_parent(s);
	if (parent) {
		struct node *p = parent->n;
		memcpy(value_ptr(p, parent_index, sizeof(uint64_t)),
		       &left->b, sizeof(uint64_t));

		insert_at(sizeof(uint64_t), p, parent_index + 1, r->keys[0], &right->b);
		check_keys(s->info, parent->b, p);

	} else {
		/* we need to create a new parent */
		struct block_node nbn;
		struct node *nn;

		if (!new_block(s->info, &nbn))
			abort();

		nn = nbn.n;

		memset(nn, 0, BLOCK_SIZE);
		nn->header.flags = INTERNAL_NODE;
		nn->header.nr_entries = 0;
		nn->header.max_entries = calc_max_entries(sizeof(uint64_t), BLOCK_SIZE);
		insert_at(sizeof(uint64_t), nn, 0, l->keys[0], &left->b);
		insert_at(sizeof(uint64_t), nn, 1, r->keys[0], &right->b);
		check_keys(s->info, nbn.b, nn);

		/* rejig the spine */
		memcpy(s->nodes + 1, s->nodes, sizeof(*s->nodes));
		memcpy(s->nodes, &nbn, sizeof(nbn));
		s->count++;
		s->root = nbn.b;
	}

	if (key < r->keys[0]) {
		unlock(s->info, right);
	} else {
		unlock(s->info, shadow_current(s));
		memcpy(shadow_current(s), right, sizeof(*right));
	}

	return 1;
}

static int btree_insert_raw(struct shadow_spine *s,
			    block_t root, count_adjust_fn fn, uint64_t key,
			    unsigned *index)
{
        int i = -1, inc;
	struct node *node;

	for (;;) {
		if (!shadow_step(s, root, fn, &inc)) {
			abort();
			/* FIXME: handle */
		}

		/* We have to patch up the parent node, ugly, but I don't
		 * see a way to do this automatically as part of the spine
		 * op. */
		if (shadow_parent(s) && i >= 0)
			memcpy(value_ptr(shadow_parent(s)->n, i, sizeof(uint64_t)),
			       &shadow_current(s)->b,
			       sizeof(uint64_t));

		assert(shadow_current(s));
		node = shadow_current(s)->n;

		if (node->header.nr_entries == node->header.max_entries &&
		    !btree_split(s, root, fn, i, key))
			abort();

		assert(shadow_current(s));
		node = shadow_current(s)->n;

		i = lower_bound(node, key);

		if (node->header.flags & LEAF_NODE)
			break;

		if (i < 0) {
			/* change the bounds on the lowest key */
			node->keys[0] = key;
			i = 0;
		}

		root = value64(node, i);
        }

	if (i < 0 || node->keys[i] != key)
		i++;

	/* we're about to overwrite this value, so undo the increment for it */
	/* FIXME: shame that inc information is leaking outside the spine.
	 * Plusinc is just plain wrong in the event of a split */
	if (node->keys[i] == key && inc)
		fn(s->info->tm, value_ptr(node, i, s->info->value_size), -1);


	*index = i;
	return 1;
}

int btree_insert(struct btree_info *info, block_t root,
		 uint64_t *keys, void *value,
		 block_t *new_root)
{
	int need_insert;
	unsigned level, index, last_level = info->levels - 1;
	block_t *block = &root;
	struct shadow_spine spine;
	struct node *n;

	init_shadow_spine(&spine, info);

	for (level = 0; level < info->levels; level++) {
		if (!btree_insert_raw(&spine, *block,
				      level == last_level ? info->adjust : value_is_block,
				      keys[level], &index)) {
			exit_shadow_spine(&spine);
			abort();
		}

		assert(shadow_current(&spine));
		n = shadow_current(&spine)->n;
		need_insert = ((index >= n->header.nr_entries) ||
			       (n->keys[index] != keys[level]));

		*block = shadow_current(&spine)->b;

		if (level == last_level) {
			if (need_insert)
				insert_at(info->value_size, n, index, keys[level], value);
			else {
				if (!info->eq || !info->eq(value_ptr(n, index, info->value_size),
							   value))
					info->adjust(info->tm,
						     value_ptr(n, index, info->value_size), -1);
				memcpy(value_ptr(n, index, info->value_size),
				       value, info->value_size);
			}
		} else {
			if (need_insert) {
				block_t new_tree;
				if (!btree_empty(info, &new_tree)) {
					exit_shadow_spine(&spine);
					abort();
				}

				insert_at(sizeof(uint64_t), n, index, keys[level], &new_tree);
			}
		}

		if (level < last_level)
			block = value_ptr(n, index, sizeof(uint64_t));
	}

	*new_root = shadow_root(&spine);
	exit_shadow_spine(&spine);
	return 1;
}

#if 0
/* FIXME: move this all to a separate file ? */
struct maybe_count {
	int has_value;
	unsigned value;
};

static void nothing(struct maybe_count *c)
{
	c->has_value = 0;
}

static void just(struct maybe_count *c, unsigned v)
{
	c->has_value = 1;
	c->value = v;
}

static int is_just(struct maybe_count *c)
{
	return c->has_value;
}

static int get_entry_count(struct node *parent, int index, struct maybe_count *c)
{
	struct node_block nb;

	if ((index < 0) ||
	    (index > parent->header.nr_entries - 1)) {
		nothing(c);
		return 1;
	}

	nb.b = value64(p->n, parent_index - 1);
	if (!read_lock(&nb))
		return 0;

	just(c, nb.n->header.nr_entries);
	if (!unlock(&nb))
		return 0;

	return 1;
}

static int btree_merge(struct shadow_spine *s, unsigned parent_index, size_t value_size)
{
	struct node *p = shadow_parent(s), *c = shadow_current(s);
	struct maybe_count left_count, right_count;

	/* you can't merge the root node */
	if (!p)
		return 0;

	/* look at the neighbouring nodes */
	if (!get_entry_count(p->n, ((int) parent_index) - 1, &left_count) ||
	    !get_entry_count(p->n, ((int) parent_index) + 1, &right_count))
		return 0;

	/* merge scenarios:
	 * 1) left + current into a single node
	 * 2) current + right into 1 node
	 * 3) move some of left and right into current
	 * 4) move some of left into current
	 * 5) move some of right into current
	 */
	if (is_just(&left_count)) {
		if (left_count.value < merge_threshold(c->n)) {
			/* scenario 1 */
			struct block_node left;
			left.b = value64(p->n, parent_index - 1);
			if (!read_lock(&left)) {
				return 0;
			}

			memmove(c->n->keys + (sizeof(uint64_t) * left_count.value),
				c->n->keys,
				c->n->header.nr_entries * sizeof(uint64_t));
			memcpy(c->n->keys,
			       left.n->keys,
			       left_count.value * sizeof(uint64_t));

			memmove(value_ptr(c->n, left_count.value, value_size),
				value_ptr(c->n, 0, value_size),
				c->n->header.nr_entries * );
		}
	}

	if (is_just(&right_count)) {
		if (right_count.value < merge_threshold(c->n)) {
			/* scenario 2 */
		}
	}

	if (is_just(&left_count) && is_just(&right_count)) {
		/* scenario 3 */
	}

	if (is_just(&left_count)) {
		/* scenario 4 */

	} else {
		/* scenario 5 */
	}

	return 1;
}

int btree_remove(struct btree_info *info,
		 block_t root, uint64_t *keys,
		 block_t *new_root)
{
	struct shadow_spine spine;
	struct node *n;

	init_shadow_spine(&spine, info);

	*new_root = root;
	block = new_root;

	for (;;) {
		if (!tm_shadow_block(tm, *block, block, (void **) &node, &inc)) {
			assert(0);
		}

		if (inc)
			inc_children(info, node, fn);

		if (node->header.nr_entries < node->header.max_entries / 2) {
			/* fixup code here */

			/* patch up parent */

			/* possibly patch up root */
		}

		dup_block = *block;


}
#endif
int btree_clone(struct btree_info *info, block_t root, block_t *clone)
{
	struct node *clone_node, *orig_node;

	/* Copy the root node */
	if (!tm_new_block(info->tm, clone, (void **) &clone_node))
		return 0;

	if (!tm_read_lock(info->tm, root, (void **) &orig_node))
		abort();

	memcpy(clone_node, orig_node, BLOCK_SIZE);
	tm_read_unlock(info->tm, root);
	inc_children(info, clone_node, info->adjust);
	tm_write_unlock(info->tm, *clone);

	return 1;
}

void value_is_block(struct transaction_manager *tm, void *value, int32_t delta)
{
	while (delta < 0) {
		tm_dec(tm, *((uint64_t *) value));
		delta++;
	}

	while (delta > 0) {
		tm_inc(tm, *((uint64_t *) value));
		delta--;
	}
}

void value_is_meaningless(struct transaction_manager *tm, void *value, int32_t delta)
{
}

/*----------------------------------------------------------------*/

struct block_list {
	struct list list;
	block_t b;
};

static int btree_walk_(struct btree_info *info, leaf_fn lf,
		       block_t root, unsigned levels, uint32_t *ref_counts, struct list *seen,
		       struct pool *mem)
{
	unsigned i;
	struct node *n;
	struct list *l;
	struct transaction_manager *tm = info->tm;

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
			btree_walk_(info, lf, value64(n, i), levels, ref_counts, seen, mem);
	else {
		if (levels == 1)
			for (i = 0; i < n->header.nr_entries; i++)
				lf(value_ptr(n, i, info->value_size), ref_counts);
		else
			for (i = 0; i < n->header.nr_entries; i++)
				btree_walk_(info, lf, value64(n, i), levels - 1, ref_counts,
					    seen, mem);
	}

	tm_read_unlock(tm, root);

	return 1;
}

int btree_walk(struct btree_info *info, leaf_fn lf,
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
		r = btree_walk_(info, lf, roots[i], 1, ref_counts, &seen, mem);
	pool_destroy(mem);
	return r;
}


int btree_walk_h(struct btree_info *info, leaf_fn lf,
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
		r = btree_walk_(info, lf, roots[i], levels, ref_counts, &seen, mem);
	pool_destroy(mem);
	return r;
}

/*----------------------------------------------------------------*/
