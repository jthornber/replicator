#include "snapshots/persistent_estore.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define NR_BLOCKS 102400

static int open_file()
{
	int i;
	unsigned char data[BLOCK_SIZE];
	int fd = open("./block_file", O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0)
		abort();

	memset(data, 0, sizeof(data));
	for (i = 0; i < NR_BLOCKS; i++)
		write(fd, data, sizeof(data));

	return fd;
}

int main(int argc, char **argv)
{
	dev_t store_dev = 1, origin_dev = 2, snap1_dev = 3, snap2_dev = 4, snap3_dev = 5;
	int fd = open_file();
	struct exception_store *ps;
	struct block_manager *bm = block_manager_create(fd, BLOCK_SIZE, NR_BLOCKS, 128);

	enum io_result r;
	struct location from, to1, to2, to3;

	if (!bm)
		abort();

	ps = persistent_store_create(bm, store_dev);
	if (!ps)
		abort();

	/* create snapshot */
	estore_begin(ps);
	if (!estore_new_snapshot(ps, origin_dev, snap1_dev))
		abort();

	estore_commit(ps);

	/* send some write io to the origin to trigger exceptions */
	estore_begin(ps);
	{
		from.dev = origin_dev;
		from.block = 13;
		r = estore_origin_write(ps, &from, &to1);

		assert(r == IO_NEED_COPY);
		assert(to1.dev == store_dev);

		from.dev = origin_dev;
		from.block = 13;
		r = estore_origin_write(ps, &from, &to2);
		assert(r == IO_MAPPED);
		assert(to2.dev == to1.dev);
		assert(to2.block == to1.block);
	}
	estore_commit(ps);

	/* check the mapping stuck */
	estore_begin(ps);
	{
		from.dev = origin_dev;
		from.block = 13;
		r = estore_origin_write(ps, &from, &to2);
		assert(r == IO_MAPPED);
		assert(to2.dev == to1.dev);
		assert(to2.block == to1.block);
	}
	estore_commit(ps);

	/* now we create a second snapshot ... */
	estore_begin(ps);
	{
		if (!estore_new_snapshot(ps, origin_dev, snap2_dev))
			abort();
	}
	estore_commit(ps);

	/* and trigger another exception with a write to the same block */
	estore_begin(ps);
	{
		from.dev = origin_dev;
		from.block = 13;
		r = estore_origin_write(ps, &from, &to2);

		assert(r == IO_NEED_COPY);
		assert(to2.dev == store_dev);
		assert(to2.block != to1.block);
	}
	estore_commit(ps);

	/* check a read from snaps */
	estore_begin(ps);
	{
		from.dev = snap1_dev;
		from.block = 13;
		r = estore_snapshot_map(ps, &from, READ, &to3);

		assert(r == IO_MAPPED);
		assert(to3.dev == to1.dev);
		assert(to3.block == to1.block);

		from.dev = snap2_dev;
		from.block = 13;
		r = estore_snapshot_map(ps, &from, READ, &to3);

		assert(r == IO_MAPPED);
		assert(to3.dev == to2.dev);
		assert(to3.block == to2.block);
	}
	estore_commit(ps);

	/* snap a snap */
	estore_begin(ps);
	{
		if (!estore_new_snapshot(ps, snap1_dev, snap3_dev))
			abort();

		/* check the mapping works straight away */
		from.dev = snap3_dev;
		from.block = 13;
		r = estore_snapshot_map(ps, &from, READ, &to3);
		assert(r == IO_MAPPED);
		assert(to3.dev == to1.dev);
		assert(to3.block == to1.block);
	}
	estore_commit(ps);

	/* write to snap1 */
	estore_begin(ps);
	{
		from.dev = snap1_dev;
		from.block = 13;
		r = estore_snapshot_map(ps, &from, WRITE, &to1);
		assert(r == IO_NEED_COPY);
		assert(to1.dev == store_dev);
		assert(to1.block != to3.block);

		/* check snap1 read mapping has changed */
		from.dev = snap1_dev;
		from.block = 13;
		r = estore_snapshot_map(ps, &from, READ, &to2);
		assert(r == IO_MAPPED);
		assert(to2.dev == to1.dev);
		assert(to2.block == to1.block);

		/* check the snap3 mapping still works */
		from.dev = snap3_dev;
		from.block = 13;
		to1 = to3;
		r = estore_snapshot_map(ps, &from, READ, &to3);
		assert(to3.dev == to1.dev);
		assert(to3.block == to1.block);
	}
	estore_commit(ps);

	estore_destroy(ps);

	return 0;
}
