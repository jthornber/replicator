#include "persistent_estore.h"
#include "transaction_manager.h"
#include "btree.h"

#include "datastruct/list.h"

/*----------------------------------------------------------------*/

struct top_level {
	block_t origin_maps;	/* a btree of origin maps */
	block_t snap_maps;	/* a btree of snapshot maps */
};

struct snapshot_list {
	struct list list;
	dev_t snap;
	dev_t origin;
	uint64_t creation_time; /* logical */
};

struct pstore {
	int fd;
	struct block_manager *bm;
	struct transaction_manager *tm;

	block_t root;
	uint64_t time;

	block_t new_root;
	struct top_level *tl;

	/* dev database */
	/* FIXME: move this to the disk */
	struct list snaps;
};

/*
 * The origin maps use keys that combine both the block and the logical
 * time step.
 */
static uint64_t pack_block_time(block_t b, unsigned t)
{
	return ((b << 24) | t);
}

static int find_snapshot(struct pstore *ps, dev_t snap, struct snapshot_list **result)
{
	struct snapshot_list *sl;

	list_iterate_items (sl, &ps->snaps)
		if (sl->snap == snap) {
			*result = sl;
			return 1;
		}

	return 0;
}

/*----------------------------------------------------------------*/

void destroy(void *context)
{
	struct pstore *ps = (struct pstore *) context;
	struct snapshot_list *sl, *tmp;

	transaction_manager_destroy(ps->tm);
	block_manager_destroy(ps->bm);
	dev_close(ps->fd);

	list_iterate_items_safe (sl, tmp, &ps->snaps)
		free(sl);

	free(ps);
}

static uint32_t get_block_size(void *context)
{
	/* FIXME: hardcoded */
	return 4 * 1024;
}

static unsigned get_snapshot_count(void *context)
{
	struct pstore *ps = (struct pstore *) context;
	return list_size(ps->snaps);
}

static int
get_snapshot_detail(void *context, unsigned index, struct snapshot_detail *result)
{
	struct pstore *ps = (struct pstore *) context;
	struct list *tmp = &ps->snaps;
	struct snapshot_list *sl = &ps->snaps;

	if (index >= list_size(ps->snaps))
		return 0;

	while (index--)
		tmp = tmp->n;

	sl = list_item(tmp, struct snapshot_list);
	result->snap = sl->snap;
	result->origin = sl->origin;
	return 1;
}

int begin(void *context)
{
	struct pstore *ps = (struct pstore *) context;
	int inc_children;

	if (!tm_begin(ps->tm))
		abort();

	if (!tm_shadow_block(tm, ps->root, &ps->new_root, (void **) &ps->tl, &inc_children))
		abort();

	if (inc_children)
		/* no idea how this can happen */
		abort();

	return 1;
}

int commit(void *context)
{
	struct pstore *ps = (struct pstore *) context;

	if (!ps->new_root)
		abort();

	if (!tm_commit(ps->tm, ps->new_root))
		abort();

	ps->root = ps->new_root;
	ps->new_root = 0;
	ps->tl = NULL;
	return 1;
}

static enum io_result origin_read_since(struct pstore *ps,
					uint64_t since,
					struct location *from,
					struct location *to)
{
	struct pstore *ps = (struct pstore *) context;
	uint64_t keys[2], result_key, result_value;

	keys[0] = from->dev;
	keys[1] = pack_block_time(from->block, since);

	if (!btree_lookup_ge_h(tm, ps->tl->origin_maps, keys, 2,
			       &result_key, &result_value)) {

		/* map to the origin, there is no exception */
		to->dev = from->dev;
		to->block = from->block;
	} else {
		to->dev = ps->dev;
		to->block = result_value;
	}

	return IO_MAPPED;
}

static enum io_result snapshot_exception(struct pstore *ps,
					 uint64_t *keys,
					 struct location *to)
{
	block_t cow_dest, new_snap_maps;

	if (!tm_alloc_block(tm, &cow_dest))
		abort();

	if (!btree_insert_h(tm, ps->tl->snapshot_maps, keys, 2, cow_dest, &new_snap_maps))
		abort();

	ps->tl->snapshot_maps = new_snap_maps;

	to->dev = ps->dev;
	to->block = cow_dest;
	return IO_NEED_COPY;
}

enum io_result snapshot_map(void *context,
			    struct location *from,
			    enum io_direction io_type,
			    struct location *to)
{
	struct pstore *ps = (struct pstore *) context;
	uint64_t keys[2];

	keys[0] = from.dev;
	keys[1] = from.block;

	if (!btree_lookup_exact_h(tm, ps->tl->snapshot_maps, keys, 2,
				  &result_value, leaf_block))
		if (io_type == READ) {
			/* great, just use the origin mapping */
			uint64_t t = snap_time(from->dev);
			from->dev = snap_dev(ps, from->dev);
			return origin_read_since(ps, t, from, to);
		} else
			return snapshot_exception(ps, keys, to);

	to->dev = ps->dev;
	to->block = result_value;
	return IO_MAPPED;
}

static enum io_result origin_exception(struct pstore *ps,
				       uint64_t *keys,
				       struct location *to)
{
	struct pstore *ps = (struct pstore *) context;
	block_t cow_dest, new_origin_maps;

	if (!tm_alloc_block(tm, &cow_dest))
		abort();

	if (!btree_insert_h(tm, tl->origin_maps, keys, 2, cow_dest, &new_origin_maps))
		abort();

	ps->tl->origin_maps = new_origin_maps;

	to.dev = ps->dev;
	to.block = cow_dest;
	return IO_NEED_COPY;
}

enum io_result origin_write(void *context,
			    struct location *from,
			    struct location *to)
{
	struct pstore *ps = (struct pstore *) context;
	struct top_level *tl;
	uint64_t keys[2], result_key, result_value;

	keys[0] = from->dev;
	keys[1] = pack_block_time(from->block, ps->time);

	if (!btree_lookup_exact_h(tm, ps->tl->origin_maps, keys, 2,
				  &result_key, &result_value))
		return origin_exception(ps, keys, to);
	else {
		to->dev = ps->dev;
		to->block = result_value;
		return IO_MAPPED;
	}
}

int new_snapshot(void *context, dev_t origin, dev_t snap)
{
	struct pstore *ps = (struct pstore *) context;
	struct snapshot_list *sd, *sd2;
	block_t new_tree, new_snap_maps;

	sd = malloc(sizeof(*sd));
	if (!sd)
		return 0;

	sd->snap = snap;
	sd->origin = origin;

	if (find_snapshot(ps, origin, &sd2)) {
		sd->origin = sd2->origin;
		sd->creation_time = sd2->creation_time;
		if (!btree_clone(tm, orig_root, &new_tree))
			abort();
	} else {
		sd->creation_time = ps->t++;

		if (!btree_empty(ps->tm, &new_tree))
			abort();
	}

	if (!btree_insert(tm, ps->tl->snapshot_maps, snap, new_tree, &new_snap_maps))
		abort();
	ps->tl->snapshot_maps = new_snap_maps;
	list_add(&ps->snaps, &sd->list);
	return 1;
}

int del_snapshot(void *context, dev_t snap)
{
	struct pstore *ps = (struct pstore *) context;
	return 0;
}

struct exception_ops ops = {
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

static int create_top_level(struct pstore *ps)
{
	block_t new_root;
	struct top_level *tl;

	if (!tm_new_block(ps->tm, &new_root, (void **) &tl))
		abort();

	if (!btree_empty(ps->tm, &tl->origin_maps))
		abort();

	if (!btree_empty(ps->tm, &tl->snapshot_maps))
		abort();

	return 1;
}

struct exception_store *persistent_store_create(dev_t dev)
{
	struct pstore *ps = (struct pstore *) malloc(sizeof(*ps));
	if (ps) {
		ps->fd = dev_open(dev);
		if (ps->fd < 0)
			abort();

		ps->bm = block_manager_create(ps->fd, 4096, dev_size(ps->fd) / 4096);
		if (!ps->bm)
			abort();

		ps->tm = transaction_manager(ps->bm);
		if (!ps->tm)
			abort();

		ps->root = 0;
		ps->time = 0;
		ps->new_root = 0;
		ps->tl = NULL;

		list_init(ps->snaps);

		if (!create_top_level(ps))
			abort();
	}

	return ps;
}

/*----------------------------------------------------------------*/

