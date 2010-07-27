#include "snapshots/exception.h"

/*----------------------------------------------------------------*/

sector_t estore_get_block_size(void *context);
void estore_destroy(struct exception_store *es);
unsigned estore_get_dev_count(struct exception_store *es);
int estore_get_snapshot_detail(struct exception_store *es,
			       unsigned index,
			       struct snapshot_detail *result);
int estore_set_invalidate_fn(struct exception_store *es, invalidate_fn fn);
int estore_map(struct exception_store *es,
	       struct location *from,
	       enum io_direction io_type,
	       struct location *result);
int estore_new_snapshot(struct exception_store *es,
			dev_t snap, dev_t origin);


/*----------------------------------------------------------------*/

#endif
