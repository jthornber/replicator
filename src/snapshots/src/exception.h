#ifndef SNAPSHOTS_EXCEPTION_H
#define SNAPSHOTS_EXCEPTION_H

#include "snapshots/types.h"

#include <stdint.h>
#include <stdlib.h>

/* FIXME: rename this to estore */

/*----------------------------------------------------------------*/

struct device {
	const char *uuid;
};

struct snapshot_detail {
	dev_t snap;
	dev_t origin;
};

struct location {
	dev_t dev;
	block_t block;
};

enum io_direction {
	READ,
	WRITE
};

enum io_result {
	IO_BAD_PARAM,		/* eg, this snapshot isn't in this store */
	IO_ERROR,		/* something went wrong */
	IO_NEED_COPY,		/* it's a new mapping, do the copy then callback */
	IO_MAPPED		/* simple, pre-existing mapping */
};

typedef void (*io_complete_fn)(void *context, enum io_result outcome, struct location *result);

/* FIXME: What do we do if a copy fails ? */

struct exception_ops {
	void (*destroy)(void *context);

	/*
	 * All snapshots within a particular exception store use the same
	 * block size.  We should probably review this, but I think it's a
	 * reasonable restriction.
	 *
	 * Returns the block size in bytes.
	 */
	uint32_t (*get_block_size)(void *context);

	/*
	 * An exception store can store exceptions for multiple devices,
	 * they may have diferent origins.
	 */
	unsigned (*get_snapshot_count)(void *context);
	int (*get_snapshot_detail)(void *context, unsigned index, struct snapshot_detail *result);

	/*
	 * Good snapshot performance relies on amortising the costs
	 * associated with a copy-on-write exception by running several per
	 * transaction.
	 */
	int (*begin)(void *context);
	int (*commit)(void *context);

	/*
	 * If IO_NEED_COPY is returned then the client should ensure that
	 * this is completed before this transaction is completed.
	 */
	enum io_result (*snapshot_map)(void *context,
				       struct location *from,
				       enum io_direction io_type,
				       struct location *to);

	/*
	 * |to| is only useful when IO_NEED_COPY is returned.
         * Client should ensure that the copy has been completed before
         * commit is called.
	 */
	enum io_result (*origin_write)(void *context,
				       struct location *from,
				       struct location *to);


	/*
	 * Finally we need a way of creating a new snapshot.  The origin
	 * may be a snapshot, either in this exception store, or another.
	 */
	int (*new_snapshot)(void *context, dev_t origin, dev_t snap);
	int (*del_snapshot)(void *context, dev_t snap);
};

/*----------------------------------------------------------------*/

struct exception_store {
	struct exception_ops *ops;
	void *context;
};

static inline uint32_t
estore_get_block_size(struct exception_store *es)
{
	return es->ops->get_block_size(es->context);
}

static inline void
estore_destroy(struct exception_store *es)
{
	es->ops->destroy(es->context);
	free(es);
}

static inline unsigned
estore_get_snapshot_count(struct exception_store *es)
{
	return es->ops->get_snapshot_count(es->context);
}

static inline int
estore_get_snapshot_detail(struct exception_store *es,
			   unsigned index,
			   struct snapshot_detail *result)
{
	return es->ops->get_snapshot_detail(es->context, index, result);
}

static inline int
estore_begin(struct exception_store *es)
{
	return es->ops->begin(es->context);
}

static inline int
estore_commit(struct exception_store *es)
{
	return es->ops->begin(es->context);
}

static inline
enum io_result estore_snapshot_map(struct exception_store *es,
				   struct location *from,
				   enum io_direction io_type,
				   struct location *to)
{
	return es->ops->snapshot_map(es->context, from, io_type, to);
}

static inline
enum io_result estore_origin_write(struct exception_store *es,
				   struct location *from,
				   struct location *to)
{
	return es->ops->origin_write(es->context, from, to);
}

static inline int
estore_new_snapshot(struct exception_store *es,
		    dev_t origin, dev_t snap)
{
	return es->ops->new_snapshot(es->context, origin, snap);
}

static inline int
estore_del_snapshot(struct exception_store *es, dev_t snap)
{
	return es->ops->del_snapshot(es->context, snap);
}

/*----------------------------------------------------------------*/

#endif
