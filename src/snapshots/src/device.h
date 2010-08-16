#ifndef SNAPSHOTS_DEVICE_H
#define SNAPSHOTS_DEVICE_H

#include <stdint.h>

/*----------------------------------------------------------------*/

typedef uint64_t dev_t;
struct device_register;

struct device_register *device_register_create();
void device_register_destroy(struct device_register *);

/* FIXME: hack, revisit */
int dev_register(struct device_register *dr, dev_t d, int fd, uint64_t bytes);

/* -1 on error */
int dev_lookup(struct device_register *dr, dev_t d);
int dev_size(struct device_register *dr, dev_t d, uint64_t *bytes);

/*----------------------------------------------------------------*/

#endif
