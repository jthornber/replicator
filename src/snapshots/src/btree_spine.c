#include "btree.h"
#include "btree_internal.h"

#include <assert.h>
#include <string.h>

/*----------------------------------------------------------------*/

int read_lock(struct btree_info *info, struct block_node *bn)
{
	int r;

	assert(bn->state == BN_UNLOCKED);
	r = tm_read_lock(info->tm, bn->b, (void **) &bn->n);
	if (r)
		bn->state = BN_READ_LOCKED;

	return 1;
}

int shadow(struct btree_info *info, struct block_node *bn, count_adjust_fn fn, int *inc)
{
	int r;

	assert(bn->state == BN_UNLOCKED);
	r = tm_shadow_block(info->tm, bn->b, &bn->b, (void **) &bn->n, inc);
	if (r && *inc)
		inc_children(info, bn->n, fn);

	bn->state = BN_WRITE_LOCKED;
	return 1;
}

int new_block(struct btree_info *info, struct block_node *bn)
{
	int r = tm_new_block(info->tm, &bn->b, (void **) &bn->n);
	bn->state = r ? BN_WRITE_LOCKED : BN_UNLOCKED;
	return r;
}

int unlock(struct btree_info *info, struct block_node *bn)
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

void init_ro_spine(struct ro_spine *s, struct btree_info *info)
{
	s->info = info;
	s->count = 0;
	s->nodes[0].state = BN_UNLOCKED;
	s->nodes[1].state = BN_UNLOCKED;
}

int exit_ro_spine(struct ro_spine *s)
{
	int r = 1, i;

	for (i = 0; i < s->count; i++)
		r |= unlock(s->info, s->nodes + i);

	return r;
}

int ro_step(struct ro_spine *s, block_t new_child)
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

struct node *ro_node(struct ro_spine *s)
{
	struct block_node *n;
	assert(s->count);
	n = s->nodes + (s->count - 1);
	return n->n;
}

/*----------------------------------------------------------------*/

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

void init_shadow_spine(struct shadow_spine *s, struct btree_info *info)
{
	s->info = info;
	s->count = 0;
	s->nodes[0].state = BN_UNLOCKED;
	s->nodes[1].state = BN_UNLOCKED;
}

int exit_shadow_spine(struct shadow_spine *s)
{
	int r = 1, i;

	for (i = 0; i < s->count; i++)
		r |= unlock(s->info, s->nodes + i);

	return r;
}

int shadow_step(struct shadow_spine *s, block_t b, count_adjust_fn fn, int *inc)
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

struct block_node *shadow_current(struct shadow_spine *s)
{
	return s->nodes + (s->count - 1);
}

struct block_node *shadow_parent(struct shadow_spine *s)
{
	return s->count == 2 ? s->nodes : NULL;
}

int shadow_root(struct shadow_spine *s)
{
	return s->root;
}

/*----------------------------------------------------------------*/
