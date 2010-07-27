#ifndef SNAPSHOTS_UNION_ESTORE_H
#define SNAPSHOTS_UNION_ESTORE_H

#include "snapshots/exception.h"

/*----------------------------------------------------------------*/

struct exception_store *union_estore_create();

/* ownership passes */
int union_estore_add_estore(struct exception_store *union_store,
			    struct exception_store *sub_store);

/*----------------------------------------------------------------*/

#endif
