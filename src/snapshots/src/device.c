#include "device.h"

#include "datastruct/list.h"

#include <stdlib.h>

/*----------------------------------------------------------------*/
#if 0
struct dlist {
	struct list list;
	dev_t dev;
	int fd;
};

struct device_register {
	struct list devs;
};

struct device_register *device_register_create()
{
	struct device_register *dr = malloc(sizeof(*dr));
	if (dr)
		list_init(&dr->devs);
	return dr;
}

void device_register_destroy(struct device_register *dr)
{
	struct dlist *dl, *tmp;

	list_iterate_items_safe (dl, tmp, &dr->devs)
		free(dl);
	free(dr);
}

int dev_register(struct device_register *dr, dev_t d, int fd)
{
	struct dlist *dl = malloc(sizeof(*dl));
	if (!dl)
		return 0;

	dl->dev = d;
	dl->fd = fd;
	list_add(&dr->devs, &dl->list);

	return 1;
}

/* -1 on error */
int dev_lookup(struct device_register *dr, dev_t d)
{
	struct dlist *dl;

	list_iterate_items (dl, &dr->devs)
		if (dl->dev == d)
			return dl->fd;
	return -1;
}
#endif
/*----------------------------------------------------------------*/
