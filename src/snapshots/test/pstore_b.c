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
#define ORIGIN_SIZE (NR_BLOCKS / 10)

struct device_ops {
	void (*destroy)(void *context);
	int (*read_block)(void *context, block_t b, void *data);
	int (*write_block)(void *context, block_t b, const void *data);
};

struct device {
	struct device_ops *ops;
	void *context;
};

void dev_destroy(struct device *d)
{
	return d->ops->destroy(d->context);
}

int dev_read_block(struct device *d, block_t b, void *data)
{
	return d->ops->read_block(d->context, b, data);
}

int dev_write_block(struct device *d, block_t b, const void *data)
{
	return d->ops->write_block(d->context, b, data);
}

/*----------------------------------------------------------------*/

/* origin device */
struct ocontext {
	dev_t dev;
	struct block_manager *bm;
	struct exception_store *es;
};

void odestroy(void *context)
{
	free(context);
}

int oread_block(void *context, block_t b, void *data)
{
	/* we're not really interested in io to the origin */
	memset(data, 0, BLOCK_SIZE);
	return 1;
}

int owrite_block(void *context, block_t b, const void *data)
{
	struct ocontext *oc = (struct ocontext *) context;
	struct location from, to;
	enum io_result r;
	void *cow_data;

	from.dev = oc->dev;
	from.block = b;
	r = estore_origin_write(oc->es, &from, &to);
	switch (r) {
	case IO_BAD_PARAM:
	case IO_ERROR:
		abort();

	case IO_NEED_COPY:
		/* simulate the copy */
		if (!bm_lock(oc->bm, to.block, WRITE, &cow_data))
			abort();

		bm_unlock(oc->bm, to.block, 1);
		break;

	case IO_MAPPED:
		break;
	}

	return 1;
}

static struct device_ops origin_ops = {
	.destroy = odestroy,
	.read_block = oread_block,
	.write_block = owrite_block
};

struct device *origin_device_create(dev_t origin, struct block_manager *bm, struct exception_store *es)
{
	struct device *d;
	struct ocontext *oc = malloc(sizeof(*oc));
	if (oc) {
		oc->dev = origin;
		oc->bm = bm;
		oc->es = es;

		d = malloc(sizeof(*d));
		if (!d) {
			free(oc);
			return 0;
		}

		d->ops = &origin_ops;
		d->context = oc;
	}

	return d;
}

/* snapshot device */
struct scontext {
	dev_t dev;
	struct exception_store *es;
	struct block_manager *bm;
};

void cdestroy(void *context)
{
	free(context);
}

int cread_block(void *context, block_t b, void *data)
{
	struct scontext *sc = (struct scontext *) context;
	struct location from, to;
	enum io_result r;
	unsigned char buffer[BLOCK_SIZE];

	from.dev = sc->dev;
	from.block = b;
	r = estore_snapshot_map(sc->es, &from, READ, &to);
	switch (r) {
	case IO_BAD_PARAM:
	case IO_ERROR:
		return 0;

	case IO_NEED_COPY:
		abort();

	case IO_MAPPED:
		bm_lock(sc->bm, to.block, READ, (void **) &buffer);
		memcpy(data, buffer, BLOCK_SIZE);
		bm_unlock(sc->bm, to.block, 0);
		break;
	}

	return 1;
}

int cwrite_block(void *context, block_t b, const void *data)
{
	struct scontext *sc = (struct scontext *) context;
	struct location from, to;
	enum io_result r;
	void *cow_data;

	from.dev = sc->dev;
	from.block = b;
	r = estore_snapshot_map(sc->es, &from, WRITE, &to);
	switch (r) {
	case IO_BAD_PARAM:
	case IO_ERROR:
		return 0;

	case IO_NEED_COPY:
		abort();

	case IO_MAPPED:
		bm_lock(sc->bm, to.block, WRITE, &cow_data);
		memcpy(cow_data, data, BLOCK_SIZE);
		bm_unlock(sc->bm, to.block, 1);
		break;
	}

	return 1;
}

static struct device_ops snapshot_ops = {
	.destroy = cdestroy,
	.read_block = cread_block,
	.write_block = cwrite_block
};

struct device *snapshot_device_create(dev_t snapshot, struct block_manager *bm, struct exception_store *es)
{
	struct device *d;
	struct scontext *sc = malloc(sizeof(*sc));
	if (sc) {
		sc->dev = snapshot;
		sc->bm = bm;
		sc->es = es;

		d = malloc(sizeof(*d));
		if (!d) {
			free(sc);
			return 0;
		}

		d->ops = &snapshot_ops;
		d->context = sc;
	}

	return d;
}

/*----------------------------------------------------------------*/

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

static unsigned rand_int(unsigned max)
{
	return random() % max;
}

static void linear_origin_write(struct block_manager *bm, struct exception_store *es)
{
	block_t i;
	dev_t origin_dev = 2, snap_dev = 3;
	struct device *origin;

	/* create snapshot */
	estore_begin(es);
	if (!estore_new_snapshot(es, origin_dev, snap_dev))
		abort();

	estore_commit(es);
	bm_io_mark(bm, "commit");

	origin = origin_device_create(origin_dev, bm, es);
	assert(origin);

	estore_begin(es);
	for (i = 0; i < ORIGIN_SIZE; i++) {
		unsigned char buffer[BLOCK_SIZE];

		dev_write_block(origin, i, buffer);

		if (i % 100) {
			estore_commit(es);
			bm_io_mark(bm, "commit");
			estore_begin(es);
		}
	}
	estore_commit(es);
	bm_io_mark(bm, "commit");
}

void random_origin_write(struct block_manager *bm, struct exception_store *es)
{
	block_t i;
	dev_t origin_dev = 2, snap_dev = 3;
	struct device *origin;

	/* create snapshot */
	estore_begin(es);
	if (!estore_new_snapshot(es, origin_dev, snap_dev))
		abort();

	estore_commit(es);
	bm_io_mark(bm, "commit");

	origin = origin_device_create(origin_dev, bm, es);
	assert(origin);

	estore_begin(es);
	for (i = 0; i < ORIGIN_SIZE; i++) {
		unsigned char buffer[BLOCK_SIZE];

		dev_write_block(origin, rand_int(ORIGIN_SIZE), buffer);

		if (i % 100) {
			estore_commit(es);
			bm_io_mark(bm, "commit");
			estore_begin(es);
		}
	}
	estore_commit(es);
	bm_io_mark(bm, "commit");
}

void linear_snapshot_write(struct block_manager *bm, struct exception_store *es)
{
	block_t i;
	dev_t origin_dev = 2, snap_dev = 3;
	struct device *snapshot;

	/* create snapshot */
	estore_begin(es);
	if (!estore_new_snapshot(es, origin_dev, snap_dev))
		abort();

	estore_commit(es);
	bm_io_mark(bm, "commit");

	snapshot = snapshot_device_create(snap_dev, bm, es);
	assert(snapshot);

	estore_begin(es);
	for (i = 0; i < ORIGIN_SIZE; i++) {
		unsigned char buffer[BLOCK_SIZE];

		dev_write_block(snapshot, i, buffer);

		if (i % 100) {
			estore_commit(es);
			bm_io_mark(bm, "commit");
			estore_begin(es);
		}
	}
	estore_commit(es);
	bm_io_mark(bm, "commit");
}

void random_snapshot_write(struct block_manager *bm, struct exception_store *es)
{
	block_t i;
	dev_t origin_dev = 2, snap_dev = 3;
	struct device *snapshot;

	/* create snapshot */
	estore_begin(es);
	if (!estore_new_snapshot(es, origin_dev, snap_dev))
		abort();

	estore_commit(es);
	bm_io_mark(bm, "commit");

	snapshot = snapshot_device_create(snap_dev, bm, es);
	assert(snapshot);

	estore_begin(es);
	for (i = 0; i < ORIGIN_SIZE; i++) {
		unsigned char buffer[BLOCK_SIZE];

		dev_write_block(snapshot, rand_int(ORIGIN_SIZE), buffer);

		if (i % 100) {
			estore_commit(es);
			bm_io_mark(bm, "commit");
			estore_begin(es);
		}
	}
	estore_commit(es);
	bm_io_mark(bm, "commit");
}


int main(int argc, char **argv)
{
	static struct {
		const char *name;
		void (*fn)(struct block_manager *, struct exception_store *);
	} table_[] = {
		{ "linear_origin_write", linear_origin_write },
		{ "random_orgin_write", random_origin_write },
		{ "linear_snapshot_write", linear_snapshot_write },
		{ "random_snapshot_write", random_snapshot_write }
	};

	int i;
	dev_t store_dev = 1;
	char buffer[1024];

	for (i = 0; i < sizeof(table_) / sizeof(*table_); i++) {
		int fd = open_file();
		struct exception_store *ps;
		struct block_manager *bm = block_manager_create(fd, BLOCK_SIZE, NR_BLOCKS, 128);

		if (!bm)
			abort();

		ps = persistent_store_create(bm, store_dev);
		if (!ps)
			abort();

		fprintf(stderr, "%s\n", table_[i].name);
		snprintf(buffer, sizeof(buffer), "%s.trace", table_[i].name);
		bm_start_io_trace(bm, buffer);
		table_[i].fn(bm, ps);

		estore_destroy(ps);
		block_manager_destroy(bm);
		close(fd);
	}

	return 0;
}
