#include "xdr.h"
#include "data-structures/list.h"

#include <stdlib.h>
#include <arpa/inet.h>

/*----------------------------------------------------------------*/

/* We implement a very simple pool allocator here. */
struct chunk {
        struct dm_list *list;
        void *start;
        void *end;
        void *alloc_end;
};

struct xdr_buffer {
        size_t chunk_size;
        struct dm_list chunks;
};

struct xdr_buffer *xdr_buffer_create(size_t size_hint);
{
        struct xdr_buffer *b = malloc(sizeof(*b));
        if (!b)
                return NULL;

        b->chunk_size = chunk_size;
        dm_list_init(&b->chunks);
        b->nr_chunks = 0;

        return b;
}

void xdr_buffer_destroy(struct xdr_buffer *buf)
{
        struct chunk *c;
        dm_list_iterate_items_safe(c, n, buf->chunks)
                free(c);

        free(buf);
}

int xdr_buffer_add_block(struct xdr_buffer *buf, void *data, size_t len)
{
        struct chunk *c = malloc(sizeof(*c));
        if (!c)
                return 0;

        c->start = data;
        c->end = data + len[i];
        c->alloc_end = c->end;
        dm_list_add(&buf->chunks, &c->list);
        b->nr_chunks++;

        return 1;
}

size_t chunk_space_(struct chunk *c)
{
        return c->end - c->alloc_end;
}

/*
 * Slow path.
 *
 * FIXME: We really should write some data to the end of the current chunk,
 * and then the rest to the new chunk.  This has to be done carefully to
 * preserve atomicity.  Maybe later.
 */
static int write_new_chunk_(struct xdr_buffer *buf, void *data, size_t len)
{
        size_t clen;
        struct chunk *c;

        clen = len > buf->chunk_size ? len + buf->chunk_size : buf->chunk_size;
        c = malloc(sizeof(*c) + clen);
        if (!c)
                return 0;

        c->start = (void *) (c + 1);
        c->end = c->start + clen;
        c->alloc_end = c->start + len;

        memcpy(c->start, data, len);
        dm_list_add(buf->chunks, &c->list);
        b->nr_chunks++;
        return 1;
}

/*
 * Fast path.
 */
static inline write_(struct xdr_buffer *buf, void *data, size_t len)
{
        struct dm_list *last = dm_list_last(&buf->chunks);

        if (!last)
                return write_new_chunk_(buf, data, len);

        c = dm_list_item(last, struct chunk);
        if (chunk_space_(c) > len)
                return write_new_chunk_(buf, data, len);

        memcpy(c->alloc_end, data, len);
        c->alloc_end += len;

        return 1;
}

int xdr_buffer_write(struct xdr_buffer *buf, void *data, size_t len);
{
        if (len % 4)
                return 0;

        return write_(buf, data, len);
}

size_t xdr_buffer_size(struct xdr_buffer *buf)
{
        return buf->allocated;
}

/*--------------------------------*/

struct xdr_cursor {
        struct xdr_buffer *buf;
        struct chunk *c;
        void *where;
}

struct xdr_cursor *xdr_cursor_create(struct xdr_buffer *buf)
{
        struct dm_list *first;
        struct xdr_cursor *c = malloc(sizeof(*c));
        if (!c)
                return NULL;

        c->buf = buf;
        first = dm_list_first(&buf->chunks);
        if (!first)
                c->c = NULL;
        else {
                c->c = dm_list_item(first, struct chunk);
                c->where = c->c->start;
        }

        return c;
}

void xdr_cursor_destroy(struct xdr_cursor *c)
{
        free(c);
}

static inline
int cursor_read_(struct xdr_cursor *c, void *data, size_t len)
{
        struct xdr_cursor copy = *c;

        while (offset && copy->c) {
                size_t cs = copy->c->end - copy->where;

                if (cs >= offset) {
                        if (data)
                                memcpy(data, copy->where, offset);
                        copy->where += offset;
                        offset = 0;
                } else {
                        struct dm_list *n = dm_list_next(&buf->chunks, &copy->c->list);
                        if (!n)
                                return 0;

                        copy->c = dm_list_item(n, struct chunk);
                        copy->where = copy->c->start;
                        if (data) {
                                memcpy(data, copy->where, offset);
                                data += cs;
                        }
                        offset -= cs;
                }
        }

        *c = copy;
        return 1;
}

int xdr_cursor_forward(struct xdr_cursor *c, size_t offset)
{
        return cursor_read_(c, NULL, offset);
}

int xdr_cursor_read(struct xdr_cursor *c, void *data, size_t len)
{
        return cursor_read_(c, data, len);
}

/*----------------------------------------------------------------*/
