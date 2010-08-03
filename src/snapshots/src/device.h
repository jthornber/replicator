#ifndef SNAPSHOTS_DEVICE_H
#define SNAPSHOTS_DEVICE_H

#include <stdint.h>

/*----------------------------------------------------------------*/

typedef uint64_t dev_t;

/* FIXME: hack, revisit */
int dev_register(dev_t d, const char *path);

int dev_size(dev_t d);
int dev_open(dev_t d);
int dev_close(dev_t d);

/*----------------------------------------------------------------*/

#endif
