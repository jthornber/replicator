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

/*----------------------------------------------------------------*/

static void *zalloc(size_t len)
{
	void *r = malloc(len);
	if (r)
		memset(r, 0, len);
	return r;
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

static int loc_eq(struct location *l1, struct location *l2)
{
	return l1->dev == l2->dev && l1->block == l2->block;
}

static void assert_mapped(enum io_result r, struct location *l1, struct location *l2)
{
	assert(r == IO_MAPPED);
	assert(loc_eq(l1, l2));
}

/*----------------------------------------------------------------
 * Simple wrappers that call abort
 *--------------------------------------------------------------*/
void _estore_begin(struct exception_store *es)
{
	assert(estore_begin(es));
}

void _estore_commit(struct exception_store *es)
{
	assert(estore_commit(es));
}

void _estore_new_snapshot(struct exception_store *es,
			  dev_t origin, dev_t snap)
{
	assert(estore_new_snapshot(es, origin, snap));
}

/*----------------------------------------------------------------
 * Scenarios
 *--------------------------------------------------------------*/

enum {
	STORE,
	ORIGIN1,
	ORIGIN2,
	ORIGIN3,
	SNAP1,
	SNAP2,
	SNAP3,
	SNAP4
};

/* Scenario 1
 * 1 - origin <- snap
 * 2 - write origin => NEED_COPY
 * 3 - read snap => IO_MAPPED (2)
 * 4 - write origin => IO_MAPPED (origin)
 * 5 - read snap => IO_MAPPED (2)
 */
static void scenario1(struct exception_store *es)
{
	enum io_result r;
	struct location from, result_2, to;

	_estore_new_snapshot(es, ORIGIN1, SNAP1);

	from.dev = ORIGIN1;
	from.block = 13;
	r = estore_origin_write(es, &from, &result_2);

	assert(r == IO_NEED_COPY);
	assert(result_2.dev == STORE);

	from.dev = SNAP1;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_2);

	from.dev = ORIGIN1;
	r = estore_origin_write(es, &from, &to);
	assert_mapped(r, &to, &from);
	assert(from.dev == ORIGIN1);

	from.dev = SNAP1;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_2);
}

/* Scenario 2
 * 1 - origin <- snap1
 * 2 - origin <- snap2
 * 3 - write origin1 => NEED_COPY
 * 4 - read snap1 => IO_MAPPED to same place as (3)
 * 5 - read snap2 => IO_MAPPED to same place as (3)
 */
static void scenario2(struct exception_store *es)
{
	enum io_result r;
	struct location from, to, result_3;

	_estore_new_snapshot(es, ORIGIN1, SNAP1);
	_estore_new_snapshot(es, ORIGIN1, SNAP2);

	from.dev = ORIGIN1;
	from.block = 13;
	r = estore_origin_write(es, &from, &result_3);
	assert(r == IO_NEED_COPY);
	assert(result_3.dev == STORE);

	from.dev = SNAP1;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_3);

	from.dev = SNAP2;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_3);
}

/* Scenario 3
 * 1 - origin1 <- snap1
 * 2 - origin2 <- snap2
 * 3 - write origin1 => NEED_COPY
 * 4 - read snap1 => IO_MAPPED to (3)
 * 5 - read snap2 => IO_MAPPED to origin2
 * 6 - write origin2 => NEED_COPY
 * 7 - read snap1 => IO_MAPPED to (3)
 * 8 - read snap2 => IO _MAPPED to (6)
 */
static void scenario3(struct exception_store *es)
{
	enum io_result r;
	struct location from, to, result_3, result_6;

	_estore_new_snapshot(es, ORIGIN1, SNAP1);
	_estore_new_snapshot(es, ORIGIN2, SNAP2);

	from.dev = ORIGIN1;
	from.block = 13;
	r = estore_origin_write(es, &from, &result_3);
	assert(r == IO_NEED_COPY);

	from.dev = SNAP1;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_3);

	from.dev = SNAP2;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert(r == IO_MAPPED);
	assert(to.dev == ORIGIN2);

	from.dev = ORIGIN2;
	r = estore_origin_write(es, &from, &result_6);
	assert(r == IO_NEED_COPY);

	from.dev = SNAP1;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_3);

	from.dev = SNAP2;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_6);
}

/* Scenario 4
 * 1 - origin <- snap1
 * 2 - write origin => NEED_COPY
 * 3 - origin1 <- snap2
 * 4 - write origin => NEED_COPY
 * 5 - read snap1 => IO_MAPPED (2)
 * 6 - read snap2 => IO_MAPPED (4)
 */
static void scenario4(struct exception_store *es)
{
	enum io_result r;
	struct location from, to, result_2, result_4;

	_estore_new_snapshot(es, ORIGIN1, SNAP1);

	from.dev = ORIGIN1;
	from.block = 13;
	r = estore_origin_write(es, &from, &result_2);
	assert(r == IO_NEED_COPY);

	_estore_new_snapshot(es, ORIGIN1, SNAP2);

	r = estore_origin_write(es, &from, &result_4);
	assert(r == IO_NEED_COPY);

	from.dev = SNAP1;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_2);

	from.dev = SNAP2;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_4);
}

/* Scenario 5
 * 1 - origin <- snap1
 * 2 - write origin => NEED_COPY
 * 3 - snap1 <- snap2
 * 4 - read snap1 => IO_MAPPED (2)
 * 5 - read snap2 => IO_MAPPED (2)
 * 6 - write snap1 => IO_MAPPED (not 2)
 * 7 - read snap1 => IO_MAPPED (6)
 * 8 - read snap2 => IO_MAPPED (2)
 * 9 - write snap2 => IO_MAPPED
 * 10 - read snap2 => IO_MAPPED (9)
 * 11 - read snap1 => IO_MAPPED (6)
 */
static void scenario5(struct exception_store *es)
{
	enum io_result r;
	struct location from, to, result_2, result_6, result_9;

	_estore_new_snapshot(es, ORIGIN1, SNAP1);

	from.dev = ORIGIN1;
	from.block = 13;
	r = estore_origin_write(es, &from, &result_2);
	assert(r == IO_NEED_COPY);

	_estore_new_snapshot(es, SNAP1, SNAP2);

	from.dev = SNAP1;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_2);

	from.dev = SNAP2;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_2);

	from.dev = SNAP1;
	r = estore_snapshot_map(es, &from, WRITE, &result_6);
	assert(r == IO_MAPPED);

	from.dev = SNAP1;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_6);

	from.dev = SNAP2;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_2);

	from.dev = SNAP2;
	r = estore_snapshot_map(es, &from, WRITE, &result_9);
	assert(r == IO_MAPPED);

	from.dev = SNAP2;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_9);

	from.dev = SNAP1;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_6);
}

/* Scenario 6
 * 1 - origin1 <- snap1
 * 2 - write snap1 => IO_MAPPED
 * 3 - snap1 <- snap2
 * 4 - read snap2 => IO_MAPPED (2)
 * 5 - write snap1 => IO_MAPPED (!2)
 * 6 - read snap1 => IO_MAPPED (5)
 * 7 - read snap2 => IO_MAPPED (2)
 */
static void scenario6(struct exception_store *es)
{
	enum io_result r;
	struct location from, to, result_2, result_5;

	_estore_new_snapshot(es, ORIGIN1, SNAP1);

	from.dev = SNAP1;
	from.block = 13;
	r = estore_snapshot_map(es, &from, WRITE, &result_2);
	assert(r == IO_MAPPED);

	_estore_new_snapshot(es, SNAP1, SNAP2);

	from.dev = SNAP2;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_2);

	from.dev = SNAP1;
	r = estore_snapshot_map(es, &from, WRITE, &result_5);
	assert(r == IO_MAPPED);
	assert(!loc_eq(&result_5, &result_2));

	from.dev = SNAP1;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_5);

	from.dev = SNAP2;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_2);
}

/* Scenario 7
 * 1 - origin1 <- snap1
 * 2 - write snap1 => IO_MAPPED
 * 3 - snap1 <- snap2
 * 4 - snap2 <- snap3
 * 5 - write snap2 => IO_MAPPED (!2)
 * 6 - read snap1 => IO_MAPPED (2)
 * 7 - read snap3 => IO_MAPPED (2)
 */
static void scenario7(struct exception_store *es)
{
	enum io_result r;
	struct location from, to, result_2;

	_estore_new_snapshot(es, ORIGIN1, SNAP1);

	from.dev = SNAP1;
	from.block = 13;
	r = estore_snapshot_map(es, &from, WRITE, &result_2);
	assert(r == IO_MAPPED);

	_estore_new_snapshot(es, SNAP1, SNAP2);
	_estore_new_snapshot(es, SNAP2, SNAP3);

	from.dev = SNAP2;
	r = estore_snapshot_map(es, &from, WRITE, &to);
	assert(r == IO_MAPPED);
	assert(!loc_eq(&to, &result_2));

	from.dev = SNAP1;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_2);

	from.dev = SNAP3;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_2);
}

/* Scenario 8
 * 1 - origin1 <- snap1
 * 2 - write snap1 => IO_MAPPED
 * 3 - snap1 <- snap2
 * 4 - snap2 <- snap3
 * 5 - write snap1 => IO_MAPPED (!2)
 * 6 - read snap2 => IO_MAPPED (2)
 * 7 - read snap3 => IO_MAPPED (2)
 */
static void scenario8(struct exception_store *es)
{
	enum io_result r;
	struct location from, to, result_2;

	_estore_new_snapshot(es, ORIGIN1, SNAP1);

	from.dev = SNAP1;
	from.block = 13;
	r = estore_snapshot_map(es, &from, WRITE, &result_2);
	assert(r == IO_MAPPED);

	_estore_new_snapshot(es, SNAP1, SNAP2);
	_estore_new_snapshot(es, SNAP2, SNAP3);

	from.dev = SNAP1;
	r = estore_snapshot_map(es, &from, WRITE, &to);
	assert(r == IO_MAPPED);
	assert(!loc_eq(&to, &result_2));

	from.dev = SNAP2;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_2);

	from.dev = SNAP3;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_2);
}

/* Scenario 9
 * 1 - origin1 <- snap1
 * 2 - write snap1 => IO_MAPPED
 * 3 - snap1 <- snap2
 * 4 - snap2 <- snap3
 * 5 - write snap2 => IO_MAPPED (!2)
 * 6 - read snap1 => IO_MAPPED (2)
 * 7 - read snap3 => IO_MAPPED (2)
 * 8 - write snap1 => IO_MAPPED (!2)
 * 9 - read snap3 => IO_MAPPED (2)
 */
static void scenario9(struct exception_store *es)
{
	enum io_result r;
	struct location from, to, result_2;

	_estore_new_snapshot(es, ORIGIN1, SNAP1);

	from.dev = SNAP1;
	from.block = 13;
	r = estore_snapshot_map(es, &from, WRITE, &result_2);
	assert(r == IO_MAPPED);

	_estore_new_snapshot(es, SNAP1, SNAP2);
	_estore_new_snapshot(es, SNAP2, SNAP3);

	from.dev = SNAP2;
	r = estore_snapshot_map(es, &from, WRITE, &to);
	assert(r == IO_MAPPED);
	assert(!loc_eq(&to, &result_2));

	from.dev = SNAP1;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_2);

	from.dev = SNAP3;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_2);

	from.dev = SNAP1;
	r = estore_snapshot_map(es, &from, WRITE, &to);
	assert(r == IO_MAPPED);
	assert(!loc_eq(&to, &result_2));

	from.dev = SNAP3;
	r = estore_snapshot_map(es, &from, READ, &to);
	assert_mapped(r, &to, &result_2);
}

/* FIXME: add a scenario that sprinkles a few commits in. */
/* FIXME: origin write to an origin that isn't snapshotted in a store should result in an error */
/* FIXME: error if you try and create the same snapshot twice */
/* FIXME: write to an out of bounds block should return an error */

int main(int argc, char **argv)
{
	static struct {
		const char *name;
		void (*fn)(struct exception_store *es);
	} table_[] = {
		{ "scenario 1", scenario1 },
		{ "scenario 2", scenario2 },
		{ "scenario 3", scenario3 },
		{ "scenario 4", scenario4 },
		{ "scenario 5", scenario5 },
		{ "scenario 6", scenario6 },
		{ "scenario 7", scenario7 },
		{ "scenario 8", scenario8 },
		{ "scenario 9", scenario9 }
	};

	unsigned i;
	for (i = 0; i < sizeof(table_) / sizeof(*table_); i++) {
		int fd = open_file();
		struct exception_store *ps;
		struct block_manager *bm = block_manager_create(fd, BLOCK_SIZE, NR_BLOCKS, 128);

		if (!bm)
			abort();

		ps = persistent_store_create(bm, STORE);
		if (!ps)
			abort();

		bm_start_io_trace(bm, "pstore_t.trace");

		_estore_begin(ps);
		fprintf(stderr, "%s\n", table_[i].name);
		table_[i].fn(ps);
		_estore_commit(ps);

		{
			char buffer[1024];
			uint32_t *ref_counts = zalloc(sizeof(*ref_counts) * NR_BLOCKS);
			ps_walk(ps, ref_counts);

			snprintf(buffer, sizeof(buffer), "%s.space_diff", table_[i].name);
			assert(ps_diff_space_map(buffer, ps, ref_counts));
		}

		estore_destroy(ps);
		block_manager_destroy(bm);
		close(fd);
	}

	return 0;
}
