#include "snapshots/persistent_estore.h"
#include "snapshots/device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int new_file(unsigned block_size, unsigned nr_blocks)
{
	unsigned char *data = malloc(block_size);
	int i, fd = mkstemp("EXCEPTIONS_XXXXXX");

	if (!data)
		return -1;

	if (fd < 0)
		return fd;

	memset(data, 0, sizeof(data));
	for (i = 0; i < nr_blocks; i++)
		write(fd, data, block_size);

	return fd;
}

int main(int argc, char **argv)
{
	dev_t store_dev = 1;
	int fd = new_file(4096, 102400);
	struct exception_store *ps;

	if (!dev_register(1, fd))
		abort();

	ps = persistent_store_create(store_dev);
	estore_destroy(ps);

	return 0;
}
