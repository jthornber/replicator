#include "snapshots/volatile_estore.h"

/*----------------------------------------------------------------*/

struct node {
	struct list list;
	dev_t snap;
	block_t source;
	block_t dest;
};

struct volatile_store {
	
	block_t first_free_block;
};

void destroy(void *context)
{
}

static uint32_t get_block_size(void *context)
{
	/* FIXME: hardcoded */
	return 4 * 1024;
}

static unsigned get_snapshot_count(void *context)
{
	struct volatile_store *us = (struct union_store *) context;

}

static int
get_snapshot_detail(void *context, unsigned index, struct snapshot_detail *result)
{
	struct volatile_store *us = (struct union_store *) context;

}

static enum io_result snapshot_map(void *context,
				   struct location *from,
				   enum io_direction io_type,
				   struct location *result,
				   io_complete_fn fn,
				   void *fn_context)
{
	struct volatile_store *us = (struct union_store *) context;

}

static enum io_result origin_write(void *context,
				   struct location *from,
				   io_complete_fn fn,
				   void *fn_context)
{
	struct volatile_store *us = (struct union_store *) context;

}

int new_snapshot(void *context, dev_t origin, dev_t snap)
{
	struct volatile_store *us = (struct union_store *) context;

}

int del_snapshot(void *context, dev_t snap)
{
	struct volatile_store *us = (struct union_store *) context;

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

/* public functions */
struct exception_store *volatile_estore_create(dev_t cow_device, unsigned block_count)
{
	
}

/*----------------------------------------------------------------*/
