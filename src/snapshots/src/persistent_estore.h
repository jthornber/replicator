#ifndef SNAPSHOTS_PERSISTENT_ESTORE_H
#define SNAPSHOTS_PERSISTENT_ESTORE_H

#include "snapshots/exception.h"

/*----------------------------------------------------------------*/

struct exception_store *persistent_store_create(dev_t dev);

/*----------------------------------------------------------------*/

#endif
