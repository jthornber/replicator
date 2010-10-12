#include "persistent_estore.h"
#include "transaction_manager.h"
#include "btree.h"
#include "space_map.h"

#include "datastruct/list.h"

#include <stdio.h>
#include <string.h>

/*----------------------------------------------------------------*/

struct top_level {
	block_t space_bitmap;
	block_t space_ref_count;
	block_t snapshot_maps;	/* a btree of snapshot maps */
};

/* FIXME: this needs to be stored on the disk */
struct snapshot_list {
	struct list list;
	dev_t snap;
	dev_t requested_origin;	/* eg, this may be another snapshot */
	dev_t actual_origin;	/* but all snapshots eventually map onto an origin device (FIXME: thin provisioning) */
	uint64_t creation_time; /* logical */
	uint64_t last_snap_time; /* when someone last took a snapshot of this snapshot */
};

struct pstore {
	dev_t dev;
	int fd;
	struct block_manager *bm;
	struct transaction_manager *tm;

	struct btree_info snapshot_tl_map_info; /* the top level, so we can
						 * insert clones of other
						 * snaps */
	struct btree_info snapshot_map_info;

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
static uint64_t pack_block_time(block_t b, uint64_t t)
{
	return ((b << 24) | t);
}

static void unpack_block_time(uint64_t v, block_t *b, uint64_t *t)
{
	*b = v >> 24;
	*t = v & ((1 << 24) - 1);
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
#if 0
static int snap_time(struct pstore *ps, dev_t snap, uint64_t *time)
{
	struct snapshot_list *sl;

	if (!find_snapshot(ps, snap, &sl))
		return 0;

	*time = sl->creation_time;
	return 1;
}
#endif
static int snap_origin(struct pstore *ps, dev_t snap, dev_t *origin)
{
	struct snapshot_list *sl;

	if (!find_snapshot(ps, snap, &sl))
		return 0;

	*origin = sl->actual_origin;
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
	result->origin = sl->requested_origin;
	return 1;
}

int begin(void *context)
{
	struct pstore *ps = (struct pstore *) context;

	if (!tm_begin(ps->tm))
		abort();

	if (!bm_lock(ps->bm, ps->superblock, BM_LOCK_WRITE, (void **) &ps->tl))
		abort();

	return 1;
}

int commit(void *context)
{
	struct pstore *ps = (struct pstore *) context;

	if (!tm_pre_commit(ps->tm, &ps->tl->space_bitmap, &ps->tl->space_ref_count))
		abort();

	if (!tm_commit(ps->tm, ps->superblock))
		abort();

	ps->tl = NULL;
	return 1;
}

static enum io_result snapshot_exception(struct pstore *ps,
					 uint64_t *keys,
					 struct location *to)
{
	block_t cow_dest;
	uint64_t value;

	if (!tm_alloc_block(ps->tm, &cow_dest))
		abort();

	value = pack_block_time(cow_dest, ps->time);
	if (!btree_insert(&ps->snapshot_map_info, ps->tl->snapshot_maps, keys, &value, &ps->tl->snapshot_maps))
		abort();

	to->dev = ps->dev;
	to->block = cow_dest;
	return IO_MAPPED;
}

enum io_result snapshot_map(void *context,
			    struct location *from,
			    enum io_direction io_type,
			    struct location *to)
{
	struct pstore *ps = (struct pstore *) context;
	struct snapshot_list *sl;
	enum io_result r;
	uint64_t keys[2], result_value, exception_block, exception_time;

	keys[0] = from->dev;
	keys[1] = from->block;

	switch (btree_lookup_equal(&ps->snapshot_map_info, ps->tl->snapshot_maps, keys,
				     &result_value))
	{
	case LOOKUP_ERROR:
		return IO_ERROR;

	case LOOKUP_NOT_FOUND:
		if (io_type == WRITE)
			return snapshot_exception(ps, keys, to);
		else {
			/* just use the origin mapping */
			if (!snap_origin(ps, from->dev, &to->dev))
				abort();

			to->block = from->block;
			return IO_MAPPED;
		}
		break;

	case LOOKUP_FOUND:
		unpack_block_time(result_value, &exception_block, &exception_time);
		if (io_type == READ) {
			to->dev = ps->dev;
			to->block = exception_block;
			return IO_MAPPED;
		}

		if (!find_snapshot(ps, from->dev, &sl))
			abort();

		if (exception_time >= sl->last_snap_time) {
			to->dev = ps->dev;
			to->block = exception_block;
			return IO_MAPPED;
		}

		/*
		 * someone has taken a snapshot of _this_ snapshot since
		 * this exception was made, we therefore need to make a new
		 * exception.
		 */
		r = snapshot_exception(ps, keys, to);
#if 0
		if (r == IO_MAPPED)
			/* FIXME: think of a way to test this */
			tm_dec(ps->tm, exception_block);
#endif
		return r;
	}

	return IO_ERROR;
}

enum io_result origin_write(void *context,
			    struct location *from,
			    struct location *to)
{
	return snapshot_map(context, from, WRITE, to);
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
	sd->requested_origin = origin;
	sd->creation_time = ++ps->time;
	sd->last_snap_time = sd->creation_time;

	if (find_snapshot(ps, origin, &sd2)) {
		uint64_t orig_root;

		/* snapshot of a snapshot */
		sd->creation_time = sd2->creation_time;
		sd->actual_origin = sd2->actual_origin;

		switch (btree_lookup_equal(&ps->snapshot_tl_map_info, ps->tl->snapshot_maps, &origin, &orig_root)) {
		case LOOKUP_ERROR:
			abort();

		case LOOKUP_NOT_FOUND:
			abort();

		case LOOKUP_FOUND:
			break;
		}

		if (!btree_clone(&ps->snapshot_map_info, orig_root, &new_tree))
			abort();

		sd2->last_snap_time = ps->time;

		/* FIXME: I expected to need to run down the whole chain of
		 * snapshots adjusting the last_snap_time.  But the tests
		 * pass as it is.  Revisit.
		 *
		 * A bit more thinking and I'm half convinced the current
		 * code is correct.  This does highlight the need for a
		 * more rigorous way of generating test scenarios.  We need
		 * to _prove_ this is correct before we ask people to trust
		 * their data to it.
		 */
	} else {
		sd->actual_origin = origin;
		if (!btree_empty(&ps->snapshot_map_info, &new_tree))
			abort();
	}

	if (!btree_insert(&ps->snapshot_tl_map_info, ps->tl->snapshot_maps, &snap, &new_tree, &ps->tl->snapshot_maps))
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
	.map = snapshot_map,
	.new_snapshot = new_snapshot,
	.del_snapshot = del_snapshot
};

/*----------------------------------------------------------------*/

static int create_top_level(struct pstore *ps)
{
	begin(ps);

	memset(ps->tl, 0, sizeof(*ps->tl));
	if (!btree_empty(&ps->snapshot_map_info, &ps->tl->snapshot_maps))
		abort();

	return commit(ps);
}

static void value_is_block_time(struct transaction_manager *tm, void *value, int32_t delta)
{
	block_t b;
	uint64_t t;
	unpack_block_time(*((uint64_t *) value), &b, &t);
	value_is_block(tm, &b, delta);
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

		ps->snapshot_tl_map_info.tm = ps->tm;
		ps->snapshot_tl_map_info.levels = 1;
		ps->snapshot_tl_map_info.value_size = sizeof(uint64_t);
		ps->snapshot_tl_map_info.adjust = value_is_block;
		ps->snapshot_tl_map_info.eq = NULL;

		ps->snapshot_map_info.tm = ps->tm;
		ps->snapshot_map_info.levels = 2;
		ps->snapshot_map_info.value_size = sizeof(uint64_t);
		ps->snapshot_map_info.adjust = value_is_block_time;
		ps->snapshot_map_info.eq = NULL;

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

void ignore_values(void *leaf_value, uint32_t *ref_counts)
{
}

void block_values(void *leaf_value, uint32_t *ref_counts)
{
	ref_counts[*((uint64_t *) leaf_value)]++;
}

void block_time_values(void *leaf_value, uint32_t *ref_counts)
{
	ref_counts[*((uint64_t *) leaf_value) >> 24]++;
}

void ps_walk(struct exception_store *es, uint32_t *ref_counts)
{
	struct top_level *tl;
	struct pstore *ps = (struct pstore *) es->context;

	if (!tm_read_lock(ps->tm, ps->superblock, (void **) &tl))
		abort();

	ref_counts[ps->superblock]++;
	sm_walk(tm_get_sm(ps->tm), ref_counts);
	btree_walk_h(&ps->snapshot_map_info, block_time_values, &tl->snapshot_maps, 1, 2, ref_counts);

	tm_read_unlock(ps->tm, ps->superblock);
}

int ps_dump_space_map(const char *file, struct exception_store *ps_)
{
	struct pstore *ps = (struct pstore *) ps_->context;

	FILE *fp = fopen(file, "w");
	if (!fp)
		return 0;

	{
		block_t nr_blocks = bm_nr_blocks(ps->bm);
		block_t i;
		struct space_map *sm = tm_get_sm(ps->tm);

		uint32_t count;
		for (i = 0; i < nr_blocks; i++) {
			if (!sm_get_count(sm, i, &count))
				abort();
			fprintf(fp, "%u\n", count);
		}
	}

	fclose(fp);
	return 1;
}

int ps_diff_space_map(const char *file, struct exception_store *ps_,
		      uint32_t *expected_counts)
{
	int r = 1;
	struct pstore *ps = (struct pstore *) ps_->context;

	FILE *fp = fopen(file, "w");
	if (!fp)
		return 0;

	{
		block_t nr_blocks = bm_nr_blocks(ps->bm);
		block_t i;
		struct space_map *sm = tm_get_sm(ps->tm);

		for (i = 0; i < nr_blocks; i++) {
			uint32_t actual_count;
			if (!sm_get_count(sm, i, &actual_count))
				abort();

			if (actual_count != expected_counts[i]) {
				fprintf(fp, "%u %d\n", (unsigned) i,
					(int32_t) actual_count - (int32_t) expected_counts[i]);
				r = 0;
			}
		}
	}

	fclose(fp);
	return r;
}

/*----------------------------------------------------------------*/
