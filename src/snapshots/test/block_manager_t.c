#include "snapshots/block_manager.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BLOCK_SIZE 1024
#define NR_BLOCKS 10240

void barf(const char *msg)
{
	fprintf(stderr, "%s", msg);
	exit(1);
}

int create_file()
{
	unsigned char data[BLOCK_SIZE];
	int i;
	int fd = open("./block_file", O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0)
		barf("couldn't open block file");

	for (i = 0; i < NR_BLOCKS; i++) {
		memset(data, i, sizeof(data));
		write(fd, data, sizeof(data));
	}

	lseek(fd, 0, SEEK_SET);
	return fd;
}

void check_read(struct block_manager *bm)
{
	int i;
	unsigned char data[BLOCK_SIZE];
	void *block = NULL;

	for (i = 0; i < NR_BLOCKS; i++) {
		block_lock(bm, i, READ, &block);
		memset(data, i, sizeof(data));
		assert(!memcmp(data, block, sizeof(data)));
		block_unlock(bm, i, 0);
	}
}

void check_write(struct block_manager *bm)
{
	unsigned char *data;

	if (!block_lock(bm, 123, WRITE, (void **) &data))
		barf("coudln't lock block");

	data[0] = 'e';
	data[457] = 'j';
	data[1023] = 't';

	block_unlock(bm, 123, 1);

	if (!block_lock(bm, 123, READ, (void **) &data))
		barf("couldn't lock block");

	assert(data[0] == 'e');
	assert(data[457] == 'j');
	assert(data[1023] = 't');
}

void check_bad_unlock(struct block_manager *bm)
{
	void *data;

	assert(!block_unlock(bm, 124, 0));
	assert(!block_unlock(bm, 124, 1));

	assert(block_lock(bm, 124, READ, &data));
	assert(!block_unlock(bm, 124, 1));
	assert(block_unlock(bm, 124, 0));
}

int main(int argc, char **argv)
{
	int fd = create_file();
	struct block_manager *bm = block_manager_create(fd, BLOCK_SIZE, NR_BLOCKS);
	check_read(bm);
	check_write(bm);
	check_bad_unlock(bm);
	block_manager_destroy(bm);

	return 0;
}