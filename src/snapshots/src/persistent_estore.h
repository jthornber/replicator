#ifndef SNAPSHOTS_PERSISTENT_ESTORE_H
#define SNAPSHOTS_PERSISTENT_ESTORE_H

#include "snapshots/exception.h"
#include "snapshots/block_manager.h"

/*----------------------------------------------------------------*/

struct exception_store *persistent_store_create(struct block_manager *bm, dev_t dev);

/* debug */
void ps_walk(struct exception_store *es, uint32_t *ref_counts);
int ps_dump_space_map(const char *file, struct exception_store *ps);
int ps_diff_space_map(const char *file, struct exception_store *ps,
		      uint32_t *expected_counts);

/*----------------------------------------------------------------*/

#endif
