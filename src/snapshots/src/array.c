#include "array.h"

#include "snapshots/btree.h"

#include <string.h>

/*----------------------------------------------------------------*/

/*
 * For now, arrays are a thin wrapper around a btree.  Later I want to
 * investigate using some of the ideas from the 'Numerical Representations'
 * chapter of Okasaki (though these may be more suitable for situations
 * where we need catenation).
 */
struct array {
	uint32_t element_size;
	uint32_t elements_per_block;
	uint64_t nr_elements;
	block_t btree;
};

static void init_info(struct transaction_manager *tm, struct btree_info *info)
{
	info->tm = tm;
	info->levels = 1;
	info->adjust = value_is_block;
	info->eq = NULL;
}

int array_empty(struct transaction_manager *tm,
		block_t *new_root,
		uint32_t element_size)
{
	struct array *a;
	struct btree_info info;
	init_info(tm, &info);

	if (!tm_new_block(tm, new_root, (void **) &a))
		return 0;

	a->element_size = element_size;
	a->elements_per_block = BLOCK_SIZE / element_size;
	a->nr_elements = 0;

	if (!btree_empty(&info, &a->btree)) {
		tm_write_unlock(tm, *new_root);
		tm_dec(tm, *new_root);
		return 0;
	}

	if (!tm_write_unlock(tm, *new_root)) {
		btree_del(&info, a->btree);
		tm_dec(tm, *new_root);
		return 0;
	}

	return 1;
}

int array_del(struct transaction_manager *tm, block_t root)
{
	int r;
	struct array *a;
	struct btree_info info;
	init_info(tm, &info);

	if (!tm_read_lock(tm, root, (void **) &a))
		return 0;

	r = btree_del(&info, a->btree);

	if (!tm_read_unlock(tm, root))
		return 0;

	return r;
}

int array_get_element_size(struct transaction_manager *tm,
			   block_t root, uint32_t *result)
{
	struct array *a;

	if (!tm_read_lock(tm, root, (void **) &a))
		return 0;

	*result = a->element_size;

	if (!tm_read_unlock(tm, root))
		return 0;

	return 1;
}

int array_get_size(struct transaction_manager *tm,
		   block_t root, uint32_t *len)
{
	struct array *a;

	if (!tm_read_lock(tm, root, (void **) &a))
		return 0;

	*len = a->nr_elements;

	if (!tm_read_unlock(tm, root))
		return 0;

	return 1;
}

static void div_mod(uint64_t v, uint64_t n, uint64_t *d, uint64_t *r)
{
	*d = v / n;
	*r = v % n;
}

static uint64_t div_up(uint64_t v, uint64_t n)
{
	uint64_t d, r;

	div_mod(v, n, &d, &r);
	return r ? d + 1 : d;
}

static int extend_(struct transaction_manager *tm,
		   struct array *a, uint64_t len)
{
	uint64_t old_blocks = div_up(a->nr_elements, a->elements_per_block);
	uint64_t new_blocks = div_up(len, a->elements_per_block);

	uint64_t i;
	void *data;
	struct btree_info info;
	init_info(tm, &info);

	for (i = old_blocks; i < new_blocks; i++) {
		block_t b;
		if (!tm_new_block(tm, &b, &data))
			abort();

		memset(data, 0, BLOCK_SIZE);

		if (!tm_write_unlock(tm, b))
			abort();

		if (!btree_insert(&info, a->btree, &i, &b, &a->btree))
			abort();
	}
	a->nr_elements = len;

	return 1;
}

static int reduce_(struct transaction_manager *tm,
		   struct array *a, uint64_t len)
{
	uint64_t old_blocks = div_up(a->nr_elements, a->elements_per_block);
	uint64_t new_blocks = div_up(len, a->elements_per_block);

	uint64_t i;
	struct btree_info info;
	init_info(tm, &info);

	for (i = new_blocks; i < old_blocks; i++) {
		block_t b;

		if (!btree_lookup_equal(&info, a->btree, &i, &b))
			abort();
#if 0
		if (!btree_remove(&info, a->btree, &i, &a->btree))
			abort();
#endif
	}
	a->nr_elements = len;

	return 1;
}

int array_set_size(struct transaction_manager *tm,
		   block_t root, uint64_t len, block_t *new_root)
{
	int r, inc;
	struct array *a;

	if (!tm_shadow_block(tm, root, new_root, (void **) &a, &inc))
		return 0;

	if (inc)
		tm_inc(tm, a->btree);

	if (len > a->nr_elements)
		r = extend_(tm, a, len);

	else if (len < a->nr_elements)
		r = reduce_(tm, a, len);

	if (!tm_write_unlock(tm, *new_root))
		return 0;

	return r;
}

int array_get(struct transaction_manager *tm,
	      block_t root, uint32_t index, void *value)
{
	struct array *a;
	uint64_t bi, i, b;
	void *data;
	struct btree_info info;
	init_info(tm, &info);

	if (!tm_read_lock(tm, root, (void **) &a))
		abort();

	if (index >= a->nr_elements) {
		if (!tm_read_unlock(tm, root))
			abort();
		return 0;
	}

	div_mod(index, a->elements_per_block, &bi, &i);

	if (!btree_lookup_equal(&info, a->btree, &bi, &b)) {
		tm_read_unlock(tm, root);
		return 0;
	}

	if (!tm_read_lock(tm, b, &data)) {
		tm_read_unlock(tm, root);
		return 0;
	}

	if (!tm_read_unlock(tm, root)) {
		tm_read_unlock(tm, b);
		return 0;
	}

	memcpy(value, data + a->element_size * i, a->element_size);

	if (!tm_read_unlock(tm, b))
		return 0;

	return 1;
}

int array_set(struct transaction_manager *tm,
	      block_t root, uint32_t index, void *value,
	      block_t *new_root)
{
	struct array *a;
	uint64_t bi, i, b, nb;
	void *data;
	int inc;
	struct btree_info info;
	init_info(tm, &info);

	if (!tm_shadow_block(tm, root, new_root, (void **) &a, &inc))
		return 0;

	if (inc)
		tm_inc(tm, a->btree);

	if (index >= a->nr_elements) {
		tm_write_unlock(tm, *new_root);
		return 0;
	}

	div_mod(index, a->elements_per_block, &bi, &i);

	if (!btree_lookup_equal(&info, a->btree, &bi, &b)) {
		tm_write_unlock(tm, root);
		return 0;
	}

	/* we ignore inc */
	if (!tm_shadow_block(tm, b, &nb, &data, &inc)) {
		tm_write_unlock(tm, root);
		return 0;
	}

	memcpy(data + a->element_size * i, value, a->element_size);

	if (!tm_write_unlock(tm, nb))
		abort();

	/* FIXME: Hmm, we have a problem here, the btree should only
	 * decrement if nb is a new shadow.  I wonder if we can hide that
	 * in tm_dec() ?
	 */
	if (!inc)		/* weird */
		tm_inc(tm, b);

	if (!btree_insert(&info, a->btree, &bi, &nb, &a->btree)) {
		tm_write_unlock(tm, root);
		return 0;
	}

	if (!tm_write_unlock(tm, root))
		return 0;

	return 1;
}

/*----------------------------------------------------------------*/
