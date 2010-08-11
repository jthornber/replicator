#ifndef SNAPSHOTS_PERSISTENT_ESTORE_H
#define SNAPSHOTS_PERSISTENT_ESTORE_H

#include "snapshots/exception.h"
#include "snapshots/block_manager.h"

/*----------------------------------------------------------------*/

struct exception_store *persistent_store_create(struct block_manager *bm, dev_t dev);

/* debug */
int ps_dump_space_map(const char *file, struct exception_store *ps);

/*----------------------------------------------------------------*/

#endif
