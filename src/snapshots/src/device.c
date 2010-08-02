#include "device.h"

/*----------------------------------------------------------------*/

int dev_register(dev_t d, const char *path);
int dev_open(dev_t d);
int dev_close(dev_t d);
int dev_size(int fd);


/*----------------------------------------------------------------*/
