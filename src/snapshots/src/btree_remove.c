#include "btree.h"
#include "btree_internal.h"

#include <assert.h>
#include <string.h>

/*----------------------------------------------------------------*/

static void delete_at(struct node *n, unsigned index, size_t value_size)
{
	unsigned nr_to_copy = n->header.nr_entries - index - 1;

	if (nr_to_copy) {
		memmove(key_ptr(n, index),
			key_ptr(n, index + 1),
			nr_to_copy);

		memmove(value_ptr(n, index, value_size),
			value_ptr(n, index + 1, value_size),
			nr_to_copy);
	}

	n->header.nr_entries--;
}

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

static int get_entry_count(struct btree_info *info,
			   struct node *parent, int index, struct maybe_count *c)
{
	struct block_node nb;

	if ((index < 0) ||
	    (index > parent->header.nr_entries - 1)) {
		nothing(c);
		return 1;
	}

	nb.state = BN_UNLOCKED;
	nb.b = value64(parent, index);
	if (!read_lock(info, &nb))
		return 0;

	just(c, nb.n->header.nr_entries);
	if (!unlock(info, &nb))
		return 0;

	return 1;
}

/*
 * Moves |count| entries from |l| into |c|
 */
static void merge_from_left(struct node *c, struct node *l, unsigned count, size_t value_size)
{
	unsigned left_index = l->header.nr_entries - count;

	memmove(key_ptr(c, count),
		key_ptr(c, 0),
		c->header.nr_entries * sizeof(uint64_t));
	memcpy(key_ptr(c, 0),
	       key_ptr(l, left_index),
	       count * sizeof(uint64_t));

	memmove(value_ptr(c, count, value_size),
		value_ptr(c, 0, value_size),
		c->header.nr_entries * value_size);
	memcpy(value_ptr(c, 0, value_size),
	       value_ptr(l, left_index, value_size),
	       count * value_size);

	c->header.nr_entries += count;
}

/*
 * Moves |count| entries from |r| into |c|
 */
static void merge_from_right(struct node *c, struct node *r, unsigned count, size_t value_size)
{
	memcpy(key_ptr(c, c->header.nr_entries),
	       key_ptr(r, 0),
	       count * sizeof(uint64_t));

	memcpy(value_ptr(c, c->header.nr_entries, value_size),
	       value_ptr(r, 0, value_size),
	       count * value_size);

	c->header.nr_entries += count;
}

static unsigned merge_threshold(struct node *n)
{
	return n->header.max_entries / 2;
}

int btree_merge(struct shadow_spine *s, unsigned parent_index, size_t value_size)
{
	struct node *p, *c;
	struct maybe_count left_count, right_count;

	/* you can't merge the root node */
	if (!shadow_parent(s) || !shadow_current(s))
		return 0;

	p = shadow_parent(s)->n;
	c = shadow_current(s)->n;

	/* look at the neighbouring nodes */
	if (!get_entry_count(s->info, p, ((int) parent_index) - 1, &left_count) ||
	    !get_entry_count(s->info, p, ((int) parent_index) + 1, &right_count))
		return 0;

	/* merge scenarios:
	 * 1) left + current into a single node
	 * 2) current + right into 1 node
	 * 3) move some of left and right into current
	 * 4) move some of left into current
	 * 5) move some of right into current
	 */
	if (is_just(&left_count) && (left_count.value < merge_threshold(c))) {
		/* scenario 1 */
		struct block_node left;
		bn_init(&left, value64(p, parent_index - 1));
		if (!read_lock(s->info, &left)) {
			return 0;
		}

		merge_from_left(c, left.n, left_count.value, value_size);
		*key_ptr(p, parent_index) = *key_ptr(c, 0);
		unlock(s->info, &left);

		// patch up the parent
		tm_dec(s->info->tm, left.b);
		delete_at(p, parent_index - 1, value_size);

	} else if (is_just(&right_count) && (right_count.value < merge_threshold(c))) {
		/* scenario 2 */
		struct block_node right;
		bn_init(&right, value64(p, parent_index + 1));
		if (!read_lock(s->info, &right)) {
			return 0;
		}

		merge_from_right(c, right.n, right_count.value, value_size);
		unlock(s->info, &right);

		// patch up the parent
		tm_dec(s->info->tm, right.b);
		delete_at(p, parent_index + 1, value_size);

	} else if (is_just(&left_count) && is_just(&right_count)) {
		/* scenario 3 */
		unsigned total_entries = left_count.value +
			right_count.value +
			c->header.nr_entries;
		unsigned per_node = total_entries / 3;

		assert(per_node >= merge_threshold(c));

		struct block_node left, right;
		bn_init(&left, value64(p, parent_index - 1));
		if (!read_lock(s->info, &left)) {
			return 0;
		}

		bn_init(&right, value64(p, parent_index + 1));
		if (!read_lock(s->info, &right)) {
			unlock(s->info, &left);
			return 0;
		}

		merge_from_left(c, left.n, left_count.value - per_node, value_size);
		merge_from_right(c, right.n, total_entries - (2 * per_node), value_size);
		*key_ptr(p, parent_index) = *key_ptr(c, 0);
		*key_ptr(p, parent_index + 1) = *key_ptr(right.n, 0);

		unlock(s->info, &left);
		unlock(s->info, &right);

	} else if (is_just(&left_count)) {
		/* scenario 4 */
		unsigned per_node = (left_count.value + c->header.nr_entries) / 2;
		struct block_node left;
		bn_init(&left, value64(p, parent_index - 1));
		if (!read_lock(s->info, &left)) {
			return 0;
		}

		merge_from_left(c, left.n, left_count.value - per_node, value_size);
		*key_ptr(p, parent_index) = *key_ptr(c, 0);

		unlock(s->info, &left);

	} else {
		/* scenario 5 */
		unsigned per_node = (right_count.value + c->header.nr_entries) / 2;

		struct block_node right;
		bn_init(&right, value64(p, parent_index + 1));
		if (!read_lock(s->info, &right)) {
			return 0;
		}

		merge_from_right(c, right.n, right_count.value - per_node, value_size);
		*key_ptr(p, parent_index + 1) = *key_ptr(right.n, 0);

		unlock(s->info, &right);
	}

	/*
	 * if p now has a single entry, and c still has <= merge_threshold
	 * entries.  Then we copy c to p.
	 */
	/* FIXME: finish */

	return 1;
}

#if 0
/* FIXME: single layer for now */
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
			size_t value_size = node->header.flags & NODE_INTERNAL ? sizeof(uint64_t) : info->value_size;
			if (!btree_merge(&spine, parent_index, value_size))
				abort();

			/* possibly patch up root */
			// FIXME: think about this
		}

		dup_block = *block;
	}
}
#endif

/*----------------------------------------------------------------*/
