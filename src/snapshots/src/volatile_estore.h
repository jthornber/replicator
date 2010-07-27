#ifndef SNAPSHOTS_ESTORE_VOLATILE_H
#define SNAPSHOTS_ESTORE_VOLATILE_H

#include "snapshots/exception.h"

/*----------------------------------------------------------------*/

/*
 * This exception store holds all its metadata in memory.  Only really
 * useful as for comparing performance with persistent snapshots.
 */
struct exception_store *volatile_estore_create(dev_t cow_device, unsigned block_count);

/*----------------------------------------------------------------*/

#endif
