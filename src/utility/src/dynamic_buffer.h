#ifndef UTILITY_DYNAMIC_BUFFER_H
#define UTILITY_DYNAMIC_BUFFER_H

#include <stdlib.h>

/*----------------------------------------------------------------*/

struct dynamic_buffer;
struct dynamic_buffer *dynamic_buffer_create(size_t hint);
void dynamic_buffer_destroy(struct dynamic_buffer *db);
void *dynamic_buffer_get(struct dynamic_buffer *db, size_t required_space);

/*----------------------------------------------------------------*/

#endif

