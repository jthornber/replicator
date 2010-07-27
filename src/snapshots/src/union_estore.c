#include "snapshots/union_estore.h"

#include "datastruct/list.h"
#include "log/log.h"

#include <string.h>

/*----------------------------------------------------------------*/

struct dev_list {
	struct list list;
	dev_t dev;
	struct exception_store *es;
};

struct snap_tree {
	/* FIXME: speed up by using hash tables */
	struct list snaps;
	struct list origins;
};

static struct snap_tree *st_create()
{
	struct snap_tree *st = malloc(sizeof(*st));
	if (st) {
		list_init(&st->snaps);
		list_init(&st->origins);
	}

	return st;
}

static void st_destroy(struct snap_tree *st)
{
	struct dev_list *dl, *tmp;

	list_iterate_items_safe(dl, tmp, &st->snaps)
		free(dl);

	list_iterate_items_safe(dl, tmp, &st->origins)
		free(dl);

	free(st);
}

static struct dev_list *new_node(dev_t dev, struct exception_store *es)
{
	struct dev_list *dl = malloc(sizeof(*dl));
	if (dl) {
		dl->dev = dev;
		dl->es = es;
	}

	return dl;
}

/* FIXME: get atomic semantics on failure (failures are ignored atm) */
static int st_add_details(struct snap_tree *st, struct exception_store *es)
{
	unsigned count, i;

	count = estore_get_snapshot_count(es);
	for (i = 0; i < count; i++) {
		struct snapshot_detail detail;
		if (estore_get_snapshot_detail(es, i, &detail)) {
			struct dev_list *n = new_node(detail.snap, es);
			if (n)
				list_add(&st->snaps, &n->list);

			n = new_node(detail.origin, es);
			if (n)
				list_add(&st->origins, &n->list);
		}
	}

	return 1;
}

static struct exception_store *st_which_store_contains_snap(struct snap_tree *st, dev_t snap)
{
	struct dev_list *dl;

	list_iterate_items (dl, &st->snaps)
		if (dl->dev == snap)
			return dl->es;

	return NULL;
}

static int st_which_stores_use_origin(struct snap_tree *st, dev_t origin,
				      struct exception_store **stores_out, unsigned *count)
{
	struct dev_list *dl;
	unsigned i;

	list_iterate_items (dl, &st->origins) {
		if (i >= *count)
			return 0;

		if (dl->dev == origin)
			stores_out[i++] = dl->es;
	}

	return 1;
}

/*----------------------------------------------------------------*/

struct store_list {
	struct list list;
	struct exception_store *es;

	unsigned snapshot_count;
};

/*
 * FIXME: we should add some explicit locking functions to csp, even if
 * they are no-ops until we preemptive.
 */
struct callback_entry {
	struct list list;
	unsigned reference_count;
	io_complete_fn original_fn;
	void *original_context;
};

struct union_store {
	struct list stores;
	struct list callbacks;
	struct snap_tree *stree;
	unsigned snapshot_count;
};

void destroy(void *context)
{
	struct union_store *us = (struct union_store *) context;
	struct store_list *sl, *tmp;

	list_iterate_items_safe(sl, tmp, &us->stores)
		estore_destroy(sl->es);

	/* FIXME: wait for these to complete */
	if (!list_empty(&us->callbacks))
		error("still outstanding callbacks");

	free(us);
}

static uint32_t get_block_size(void *context)
{
	/* FIXME: hardcoded */
	return 4 * 1024;
}

static unsigned get_snapshot_count(void *context)
{
	struct union_store *us = (struct union_store *) context;
	return us->snapshot_count;
}

static int
get_snapshot_detail(void *context, unsigned index, struct snapshot_detail *result)
{
	struct union_store *us = (struct union_store *) context;
	struct store_list *sl;

	/* slow, but I don't think it matters */
	list_iterate_items (sl, &us->stores) {
		if (estore_get_snapshot_detail(sl->es, index, result))
			return 1;

		index -= estore_get_snapshot_count(sl->es);
	}

	return 0;
}

static enum io_result snapshot_map(void *context,
				   struct location *from,
				   enum io_direction io_type,
				   struct location *result,
				   io_complete_fn fn,
				   void *fn_context)
{
	struct union_store *us = (struct union_store *) context;
	struct exception_store *es = st_which_store_contains_snap(us->stree, from->dev);

	if (!es)
		return IO_BAD_PARAM;

	return estore_snapshot_map(es, from, io_type, result, fn, fn_context);
}

static void dec_callback(void *context, enum io_result r, struct location *loc)
{
	struct callback_entry *ce = (struct callback_entry *) context;
	if (--ce->reference_count == 0)
		ce->original_fn(ce->original_context, r, loc);
	list_del(&ce->list);
	free(ce);
}

static enum io_result
combine_result(enum io_result acc, enum io_result r, struct callback_entry *ce)
{
	switch (r) {
	case IO_BAD_PARAM:
		/* FIXME: ignored for now */
		break;

	case IO_ERROR:
		/* FIXME: ignored for now */
		break;

	case IO_SUCCESS:
		/* FIXME: ignored for now */
		break;

	case IO_DEFER:
		acc = IO_DEFER;
		ce->reference_count++;
		break;
	}

	return acc;
}


static enum io_result origin_write(void *context,
				   struct location *from,
				   io_complete_fn fn,
				   void *fn_context)
{
	struct union_store *us = (struct union_store *) context;
	struct callback_entry *ce = malloc(sizeof(*ce));
	enum io_result result = IO_SUCCESS;

	/* FIXME: hard coded magic number */
	struct exception_store *stores[255];
	unsigned count = 255, i;

	if (!ce)
		return IO_ERROR;

	ce->reference_count = 0;
	ce->original_fn = fn;
	ce->original_context = context;

	if (!st_which_stores_use_origin(us->stree, from->dev, stores, &count))
		return IO_ERROR;

	/*
	 * Multiple exception stores may use this as the origin.
	 */
	for (i = 0; i < count; i++) {
		enum io_result r = estore_origin_write(stores[i], from, dec_callback, ce);
		result = combine_result(result, r, ce);
	}
	if (!ce->reference_count)
		free(ce);
	else
		list_add(&us->callbacks, &ce->list);

	return result;
}

int new_snapshot(void *context, dev_t origin, dev_t snap)
{
	/* FIXME: how do we choose which store to put it in ?  First try to
	 * put it in a store that already snaps this origin ?  Then the
	 * store with most space ? */
	return 0;
}

int del_snapshot(void *context, dev_t snap)
{
	struct union_store *us = (struct union_store *) context;
	struct exception_store *es = st_which_store_contains_snap(us->stree, snap);

	if (!es)
		return 0;

	return estore_del_snapshot(es, snap);
}

/*--------------------------------*/

static struct exception_ops ops = {
	.destroy = destroy,
	.get_block_size = get_block_size,
	.get_snapshot_count = get_snapshot_count,
	.get_snapshot_detail = get_snapshot_detail,
	.snapshot_map = snapshot_map,
	.origin_write = origin_write,
	.new_snapshot = new_snapshot,
	.del_snapshot = del_snapshot
};

/*----------------------------------------------------------------*/

/* public interface */

struct exception_store *union_estore_create()
{
	struct exception_store *es;
	struct union_store *us = malloc(sizeof(*us));
	memset(us, 0, sizeof(*us));
	if (us) {
		list_init(&us->stores);
		us->stree = st_create();
		if (!us) {
			free(us);
			return NULL;
		}
	}

	es = malloc(sizeof(*es));
	if (!es) {
		st_destroy(us->stree);
		free(us);
		return NULL;
	}

	es->ops = &ops;
	es->context = us;

	return es;
}

/* FIXME: check that the block sizes are the same */
int union_estore_add_estore(struct exception_store *union_store,
			    struct exception_store *sub_store)
{
	struct union_store *us = (struct union_store *) union_store->context;
	struct store_list *sl = malloc(sizeof(*sl));

	if (!sl)
		return 0;

	sl->es = sub_store;
	list_add(&us->stores, &sl->list);
	us->snapshot_count += estore_get_snapshot_count(sub_store);
	if (!st_add_details(us->stree, sub_store)) {
		/* FIXME: do something atomic */
	}

	return 1;

}

/*----------------------------------------------------------------*/
