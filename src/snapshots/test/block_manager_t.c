#include "snapshots/block_manager.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#undef BLOCK_SIZE
#define BLOCK_SIZE 4096
#define NR_BLOCKS 10240

void barf(const char *msg)
{
	fprintf(stderr, "%s", msg);
	abort();
}

int create_file()
{
	ssize_t r;
	unsigned char data[BLOCK_SIZE];
	int i;
	int fd = open("./block_file", O_CREAT | O_TRUNC | O_RDWR,
		      S_IRUSR | S_IWUSR);
	if (fd < 0)
		barf("couldn't open block file");

	for (i = 0; i < NR_BLOCKS; i++) {
		memset(data, i, sizeof(data));
		r = write(fd, data, sizeof(data));
		if (r < 0)
			barf("write failed");
	}

#if 0
	close(fd);

	fd = open("./block_file", O_CREAT | O_TRUNC | O_RDWR | O_DIRECT | O_SYNC,
		  S_IRUSR | S_IWUSR);
	if (fd < 0)
		barf("couldn't reopen block file");
#endif

	return fd;
}

void check_read(struct block_manager *bm)
{
	int i;
	unsigned char data[BLOCK_SIZE];
	void *block = NULL;

	for (i = 0; i < NR_BLOCKS; i++) {
		if (!bm_lock(bm, i, BM_LOCK_READ, &block))
			barf("bm_lock failed");
		memset(data, i, sizeof(data));
		assert(!memcmp(data, block, sizeof(data)));
		if (!bm_unlock(bm, i, 0))
			barf("bm_unlock failed");
	}

	assert(bm_read_locks_held(bm) == 0);
}

/*
 * scrolls a window of write locks across the device.
 */
#define WINDOW_SIZE 64
void check_write(struct block_manager *bm)
{
	block_t b;
	unsigned char *data[WINDOW_SIZE];
	unsigned char expected[BLOCK_SIZE];

	for (b = 0; b < WINDOW_SIZE; b++) {
		if (!bm_lock(bm, b, BM_LOCK_WRITE, (void **) &data[b]))
			barf("couldn't lock block");

		memset(data[b], 1, BLOCK_SIZE);
	}

	for (b = WINDOW_SIZE; b < NR_BLOCKS; b++) {
		if (!bm_lock(bm, b, BM_LOCK_WRITE, (void **) &data[b % WINDOW_SIZE]))
			barf("couldn't lock block");

		memset(data[b % WINDOW_SIZE], 1, BLOCK_SIZE);

		if (!bm_unlock(bm, b - WINDOW_SIZE, 1))
			barf("bm_unlock");
	}

	for (b = NR_BLOCKS - WINDOW_SIZE; b < NR_BLOCKS; b++)
		if (!bm_unlock(bm, b, 1))
			barf("bm_unlock");

	/* data[0] = 'e'; */
	/* data[457] = 'j'; */
	/* data[1023] = 't'; */

	memset(expected, 1, BLOCK_SIZE);
	for (b = 0; b < NR_BLOCKS; b++) {
		if (!bm_lock(bm, b, BM_LOCK_READ, (void **) &data[0]))
			barf("bm_lock");

		assert(memcmp(data[0], expected, BLOCK_SIZE) == 0);

		if (!bm_unlock(bm, b, 0))
			barf("bm_unlock");
	}

	bm_flush(bm, 1);

	for (b = 0; b < NR_BLOCKS; b++) {
		if (!bm_lock(bm, b, BM_LOCK_READ, (void **) &data[0]))
			barf("bm_lock");

		assert(memcmp(data[0], expected, BLOCK_SIZE) == 0);

		if (!bm_unlock(bm, b, 0))
			barf("bm_unlock");
	}

	assert(bm_read_locks_held(bm) == 0);
	assert(bm_write_locks_held(bm) == 0);
}

void check_bad_unlock(struct block_manager *bm)
{
	void *data;

	assert(!bm_unlock(bm, 124, 0));
	assert(!bm_unlock(bm, 124, 1));
	assert(bm_read_locks_held(bm) == 0);
	assert(bm_write_locks_held(bm) == 0);

	assert(bm_lock(bm, 124, BM_LOCK_READ, &data));
	assert(!bm_unlock(bm, 124, 1));
	assert(bm_unlock(bm, 124, 0));
	assert(bm_read_locks_held(bm) == 0);
	assert(bm_write_locks_held(bm) == 0);
}

int main(int argc, char **argv)
{
	int fd = create_file();
	struct block_manager *bm = block_manager_create(fd, BLOCK_SIZE, NR_BLOCKS, 4);
	check_read(bm);
	check_write(bm);
	check_bad_unlock(bm);
	block_manager_destroy(bm);

	return 0;
}
