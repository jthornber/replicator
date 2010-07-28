#include "snapshots/btree.h"

/* FIXME: for now I've completely ignored endian issues in the disk format */
/* FIXME: enable close packing for on disk structures */

struct btree {
	struct block_manager *bm;
	block_t free_space_root;
};

/*----------------------------------------------------------------*/

struct space_map {
        
};

/*
 * An in core representation of the freespace.
 * FIXME: move to another file.
 */
int allocate_block(struct space_map *sm, block_t *b);
int inc_block(struct space_map *sm, block_t b);
int dec_block(struct space_map *sm, block_t b);

/*----------------------------------------------------------------*/

/* the transaction brackets all on-disk operations */
struct transaction {
        struct pool *mem;
        struct btree *bt;
        struct list shadowed_blocks;
        struct list free_blocks;
};

struct shadowed_block {
        block_t b;
        unsigned ref_count;
};

struct transaction *trans_begin(struct btree *bt);

/*
 * Commits the new root, then deletes the old one.  Can deletion be
 * deferred until the subsequent transaction ?  If so we'll have to hand up
 * the open delete transaction to the btree.  Transaction scope needs to be
 * defined by the client.
 */
int trans_commit(struct transaction *t, block_t new_root);
int trans_rollback(struct transaction *t);

/* only need to shadow once per block, so has a small cache of shadowed */
int trans_shadow(struct transaction *t, block_t b, block_t *new);

/*----------------------------------------------------------------*/

/*
 * Free space tree.  We need to keep a reference count for allocated
 * blocks.  Free blocks have a reference count of 0.  Leaves consist of
 * {Block, RefCount, RunLength}
*/
#define BLOCK_SIZE 4096
#define NR_INTERNAL_ENTRIES_PER_NODE ((BLOCK_SIZE / (sizeof(block_t) * 2)) - 1)
#define NR_LEAF_ENTRIES_PER_NODE (BLOCK_SIZE / (sizeof(block_t) + 2 + 2) - 1)

enum free_node_flags {
        INTERNAL_NODE = 1,
        LEAF_NODE = 1 << 1
};

struct free_node {
        uint32_t flags;
        uint32_t nr_entries;

        union {
                struct {
                        block_t keys[NR_INTERNAL_ENTRIES_PER_NODE];
                        block_t children[NR_INTERNAL_ENTRIES_PER_NODE];
                } internal;

                struct {
                        block_t keys[NR_LEAF_ENTRIES_PER_NODE];
                        struct {
                                uint16_t ref_count;
                                uint16_t run_length; /* FIXME: this is spurious, since it's implied by the gaps between the keys */
                        } values[NR_LEAF_ENTRIES_PER_NODE];
                } leaf;
        } u;
};

void free_node_to_disk(struct free_node *in, struct free_node *out);
void free_node_to_core(struct free_node *in, struct free_node *out);

int get_ref_count(struct btree *bt, block_t b)
{
        int r;
        block_t last_block;
        struct free_node *fn;

	if (!block_lock(bt->bm, bt->free_space_root, READ, &((void *) fn)))
		return -1;

        /* FIXME: use a binary search */
        last_block = bt->free_space_root;
        while (fn->flags & INTERNAL_NODE) {
                for (i = 0; i < fn->nr_entries; i++)
                        if (fn->u.internal.keys[i] > b)
                                break;

                assert(i > 0);
                new_block = fn->u.internal.children[--i];
                if (!block_lock(bt->bm, new_block, READ, &((void *) fn))) {
                        block_unlock(last_block, 0);
                        return -1;
                }

                block_unlock(last_block);
                last_block = new_block;
        }

        /* FIXME: use a binary search */
        for (i = 0; i < fn->nr_entries; i++)
                if (fn->u.leaf.keys[i] > b)
                        break;

        assert(i);
        r = fn->u.leaf.values[--i].ref_count;
        block_unlock(last_block, 0);

        return r;
}

int inc_ref_count(struct btree *bt, block_t b)
{
        int r, parent_dirty;
        unsigned depth;
        block_t parent_block, current_block = bt->free_space_root;
        struct free_node *fn;

        if (!block_lock(bt->bm, bt->free_space_root, WRITE, &((void *) fn)))
                return -1;

        parent_dirty = 0;
        for (depth = 0; depth < ???; depth++) {
                if (depth > 0)
                        block_unlock(bt->bm, parent, parent_dirty);

                parent = current_block;

                if (fn->nr_entries == NR_INTERNAL_ENTRIES) {
                        /* split */

                        /* patch parent */
                        
                }
        }
}
