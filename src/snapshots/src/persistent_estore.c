#include "persistent_estore.h"
#include "transaction_manager.h"
#include "btree.h"

#include "datastruct/list.h"

#include <string.h>

/*----------------------------------------------------------------*/

struct top_level {
	block_t space_map;
	block_t origin_maps;	/* a btree of origin maps */
	block_t snapshot_maps;	/* a btree of snapshot maps */
};

/* FIXME: this needs to be stored on the disk */
struct snapshot_list {
	struct list list;
	dev_t snap;
	dev_t origin;
	uint64_t creation_time; /* logical */
};

struct pstore {
	dev_t dev;
	int fd;
	struct block_manager *bm;
	struct transaction_manager *tm;

	uint64_t time;

	block_t superblock;	/* this is where the top level node always lives */
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

static int snap_time(struct pstore *ps, dev_t snap, uint64_t *time)
{
	struct snapshot_list *sl;

	if (!find_snapshot(ps, snap, &sl))
		return 0;

	*time = sl->creation_time;
	return 1;
}

static int snap_origin(struct pstore *ps, dev_t snap, dev_t *origin)
{
	struct snapshot_list *sl;

	if (!find_snapshot(ps, snap, &sl))
		return 0;

	*origin = sl->origin;
	return 1;
}

/*----------------------------------------------------------------*/

void destroy(void *context)
{
	struct pstore *ps = (struct pstore *) context;
	struct snapshot_list *sl, *tmp;

	tm_destroy(ps->tm);

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
	return list_size(&ps->snaps);
}

static int
get_snapshot_detail(void *context, unsigned index, struct snapshot_detail *result)
{
	struct pstore *ps = (struct pstore *) context;
	struct list *tmp = &ps->snaps;
	struct snapshot_list *sl;

	if (index >= list_size(&ps->snaps))
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

	if (!tm_begin(ps->tm))
		abort();

	if (!bm_lock(ps->bm, ps->superblock, LOCK_WRITE, (void **) &ps->tl))
		abort();

	return 1;
}

int commit(void *context)
{
	struct pstore *ps = (struct pstore *) context;

	if (!tm_pre_commit(ps->tm, &ps->tl->space_map))
		abort();

	if (!tm_commit(ps->tm, ps->superblock))
		abort();

	ps->tl = NULL;
	return 1;
}

static enum io_result origin_read_since(struct pstore *ps,
					uint64_t since,
					struct location *from,
					struct location *to)
{
	uint64_t keys[2], result_key, result_value;

	keys[0] = from->dev;
	keys[1] = pack_block_time(from->block, since);

	switch (btree_lookup_ge_h(ps->tm, ps->tl->origin_maps, keys, 2,
				  &result_key, &result_value)) {
	case LOOKUP_ERROR:
		return IO_ERROR;

	case LOOKUP_NOT_FOUND:
		/* map to the origin, there is no exception */
		to->dev = from->dev;
		to->block = from->block;
		break;

	case LOOKUP_FOUND:
		to->dev = ps->dev;
		to->block = result_value;
		break;
	}

	return IO_MAPPED;
}

static enum io_result snapshot_exception(struct pstore *ps,
					 uint64_t *keys,
					 struct location *to)
{
	block_t cow_dest;

	if (!tm_alloc_block(ps->tm, &cow_dest))
		abort();

	if (!btree_insert_h(ps->tm, ps->tl->snapshot_maps, keys, 2, cow_dest, &ps->tl->snapshot_maps))
		abort();

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
	uint64_t keys[2], result_value;

	keys[0] = from->dev;
	keys[1] = from->block;

	switch (btree_lookup_equal_h(ps->tm, ps->tl->snapshot_maps, keys, 2,
				     &result_value))
	{
	case LOOKUP_ERROR:
		return IO_ERROR;

	case LOOKUP_NOT_FOUND:
		if (io_type == WRITE)
			return snapshot_exception(ps, keys, to);
		else {
			/* great, just use the origin mapping */
			uint64_t t;
			if (!snap_time(ps, from->dev, &t))
				abort();

			if (!snap_origin(ps, from->dev, &from->dev))
				abort();

			return origin_read_since(ps, t, from, to);
		}
		break;

	case LOOKUP_FOUND:
		to->dev = ps->dev;
		to->block = result_value;
		return IO_MAPPED;
	}

	return IO_ERROR;
}

static enum io_result origin_exception(struct pstore *ps,
				       uint64_t *keys,
				       struct location *to)
{
	block_t cow_dest;

	if (!tm_alloc_block(ps->tm, &cow_dest))
		abort();

	if (!btree_insert_h(ps->tm, ps->tl->origin_maps, keys, 2, cow_dest, &ps->tl->origin_maps))
		abort();

	to->dev = ps->dev;
	to->block = cow_dest;
	return IO_NEED_COPY;
}

enum io_result origin_write(void *context,
			    struct location *from,
			    struct location *to)
{
	struct pstore *ps = (struct pstore *) context;
	uint64_t keys[2], result_value;

	keys[0] = from->dev;
	keys[1] = pack_block_time(from->block, ps->time);

	switch (btree_lookup_equal_h(ps->tm, ps->tl->origin_maps,
				     keys, 2, &result_value)) {
	case LOOKUP_ERROR:
		return IO_ERROR;

	case LOOKUP_NOT_FOUND:
		return origin_exception(ps, keys, to);

	case LOOKUP_FOUND:
		to->dev = ps->dev;
		to->block = result_value;
		return IO_MAPPED;
	}

	return IO_ERROR;
}

int new_snapshot(void *context, dev_t origin, dev_t snap)
{
	struct pstore *ps = (struct pstore *) context;
	struct snapshot_list *sd, *sd2;
	block_t new_tree;

	sd = malloc(sizeof(*sd));
	if (!sd)
		return 0;

	sd->snap = snap;
	sd->origin = origin;

	if (find_snapshot(ps, origin, &sd2)) {
		uint64_t orig_root;

		/* snapshot of a snapshot */
		sd->origin = sd2->origin;
		sd->creation_time = sd2->creation_time;

		switch (btree_lookup_equal(ps->tm, ps->tl->snapshot_maps, origin, &orig_root)) {
		case LOOKUP_ERROR:
			abort();

		case LOOKUP_NOT_FOUND:
			abort();

		case LOOKUP_FOUND:
			break;
		}

		if (!btree_clone(ps->tm, orig_root, &new_tree))
			abort();
	} else {
		sd->creation_time = ++ps->time;
		if (!btree_empty(ps->tm, &new_tree))
			abort();
	}

	if (!btree_insert(ps->tm, ps->tl->snapshot_maps, snap, new_tree, &ps->tl->snapshot_maps))
		abort();
	list_add(&ps->snaps, &sd->list);
	return 1;
}

int del_snapshot(void *context, dev_t snap)
{
	return 0;
}

struct exception_ops ops = {
	.destroy = destroy,
	.get_block_size = get_block_size,
	.get_snapshot_count = get_snapshot_count,
	.get_snapshot_detail = get_snapshot_detail,
	.begin = begin,
	.commit = commit,
	.snapshot_map = snapshot_map,
	.origin_write = origin_write,
	.new_snapshot = new_snapshot,
	.del_snapshot = del_snapshot
};

/*----------------------------------------------------------------*/

static int create_top_level(struct pstore *ps)
{
	begin(ps);

	memset(ps->tl, 0, sizeof(*ps->tl));
	if (!btree_empty(ps->tm, &ps->tl->origin_maps))
		abort();

	if (!btree_empty(ps->tm, &ps->tl->snapshot_maps))
		abort();

	return commit(ps);
}

struct exception_store *persistent_store_create(struct block_manager *bm, dev_t dev)
{
	struct pstore *ps = (struct pstore *) malloc(sizeof(*ps));
	struct exception_store *es;

	if (ps) {
		ps->bm = bm;
		ps->dev = dev;

		ps->tm = tm_create(ps->bm);
		if (!ps->tm)
			abort();

		ps->time = 0;
		ps->superblock = 0;
		tm_reserve_block(ps->tm, ps->superblock);
		ps->tl = NULL;

		list_init(&ps->snaps);

		if (!create_top_level(ps))
			abort();

		es = malloc(sizeof(*es));
		if (!es) {
			destroy(ps);
			return NULL;
		}

		es->ops = &ops;
		es->context = ps;
	}

	return es;
}

/*----------------------------------------------------------------*/

