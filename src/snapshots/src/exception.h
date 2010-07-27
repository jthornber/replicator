#ifndef SNAPSHOTS_EXCEPTION_H
#define SNAPSHOTS_EXCEPTION_H

#include <stdint.h>
#include <stdlib.h>

/* FIXME: rename this to estore */

/*----------------------------------------------------------------*/

typedef uint64_t block_t;
typedef uint64_t dev_t;	   /* identifies any device, including snapshots */

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
	IO_SUCCESS,		/* it worked, synchronously */
	IO_DEFER		/* will complete asynchronously */
};

typedef void (*io_complete_fn)(void *context, enum io_result outcome, struct location *result);

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
	 * The client will need to repeatedly call this until a snapshot
	 * location finally gets mapped to a non-snapshot location.  For
	 * instance, if you have a snapshot of a snapshot of a snapshot you
	 * will have to call this function 3 times (possibly on different
	 * exception stores).
	 *
	 * The client can implement efficient caching of these mappings
	 * (eg, via a hash table).  This saves duplication of code in
	 * different exception stores.  Obviously the hash table entry
	 * should be removed if any of the sub mappings are invalidated.
	 *
	 * Writes to a snapshot can trigger a copy-on-write event, so this
	 * function may complete asynchronously.
	 */
	enum io_result (*snapshot_map)(void *context,
				       struct location *from,
				       enum io_direction io_type,
				       struct location *result, /* iff the function completes synchronously */
				       io_complete_fn fn,
				       void *fn_context);	/* iff async */

	/*
	 * Because a write to an origin can trigger a copy-on-write
	 * exception, which takes time, this function completes
	 * asynchronously.  You do not need to call this if the origin is
	 * itself a snapshot (call snapshot_map instead).
	 *
	 * If successful, the location argument in the callback will just
	 * be the same as |from|.
	 */
	enum io_result (*origin_write)(void *context,
				       struct location *from,
				       io_complete_fn fn,
				       void *fn_context);

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

static inline
enum io_result estore_snapshot_map(struct exception_store *es,
				   struct location *from,
				   enum io_direction io_type,
				   struct location *result,
				   io_complete_fn fn,
				   void *fn_context)
{
	return es->ops->snapshot_map(es->context, from, io_type, result, fn, fn_context);
}

static inline
enum io_result estore_origin_write(struct exception_store *es,
				   struct location *from,
				   io_complete_fn fn,
				   void *fn_context)
{
	return es->ops->origin_write(es->context, from, fn, fn_context);
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
