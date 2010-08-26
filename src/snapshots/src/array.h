#ifndef SNAPSHOTS_ARRAY_H
#define SNAPSHOTS_ARRAY_H

#include "snapshots/transaction_manager.h"

/*----------------------------------------------------------------*/

/* element size should be <= block size */
int array_empty(struct transaction_manager *tm,
		block_t *new_root,
		uint32_t element_size);

int array_del(struct transaction_manager *tm,
	      block_t root);

int array_get_element_size(struct transaction_manager *tm,
			   block_t root,
			   uint32_t *result);

int array_get_size(struct transaction_manager *tm,
		   block_t root,
		   uint32_t *len);

int array_set_size(struct transaction_manager *tm,
		   block_t root, uint64_t len, block_t *new_root);

/* data will be copied to result */
int array_get(struct transaction_manager *tm,
	      block_t root, uint32_t index, void *value);

int array_set(struct transaction_manager *tm,
	      block_t root, uint32_t index, void *value,
	      block_t *new_root);

/*----------------------------------------------------------------*/

#endif
