#include "snapshots/space_map.h"

#include <stdio.h>
#include <stdlib.h>

#define NR_BLOCKS 1024

void barf(const char *msg)
{
	fprintf(stderr, "%s", msg);
	abort();
}

void check_alloc(struct space_map *sm)
{
	int i;
	block_t b;

	for (i = 1; i < NR_BLOCKS; i++) {
		if (!sm_new_block(sm, &b))
			barf("couldn't allocate block");
	}

	if (sm_new_block(sm, &b))
		barf("allocated more blocks than possible");
}

int main(int argc, char **argv)
{
	struct space_map *sm = space_map_create(NR_BLOCKS);

	check_alloc(sm);
	space_map_destroy(sm);

	return 0;
}
