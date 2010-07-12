#include "xdr.h"
#include "datastruct/list.h"

#include <stdlib.h>
#include <string.h>

/*----------------------------------------------------------------*/

/* We implement a very simple pool allocator here. */
struct chunk {
        struct list list;
        void *start;
        void *end;
        void *alloc_end;
};

struct xdr_buffer {
        size_t chunk_size;
        struct list chunks;
        size_t allocated;
};

struct xdr_buffer *xdr_buffer_create(size_t size_hint)
{
        struct xdr_buffer *b = malloc(sizeof(*b));
        if (!b)
                return NULL;

        b->chunk_size = size_hint;
        list_init(&b->chunks);

        return b;
}

void xdr_buffer_destroy(struct xdr_buffer *buf)
{
        struct chunk *c, *tmp;
        list_iterate_items_safe (c, tmp, &buf->chunks)
                free(c);

        free(buf);
}

int xdr_buffer_add_block(struct xdr_buffer *buf, void *data, size_t len)
{
        struct chunk *c = malloc(sizeof(*c));
        if (!c)
                return 0;

        c->start = data;
        c->end = data + len;
        c->alloc_end = c->end;
        list_add(&buf->chunks, &c->list);

        return 1;
}

size_t chunk_space_(struct chunk *c)
{
        return c->end - c->alloc_end;
}

/*
 * Slow path.
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
        list_add(&buf->chunks, &c->list);

        return 1;
}

static inline uint32_t min(uint32_t lhs, uint32_t rhs)
{
        return lhs <= rhs ? lhs : rhs;
}

static inline int write_(struct xdr_buffer *buf, void *data, size_t len)
{
        struct chunk *c;
        struct list *last = list_last(&buf->chunks);

        if (last) {
                c = list_item(last, struct chunk);

                uint32_t l = min(chunk_space_(c), len);
                memcpy(c->alloc_end, data, l);
                c->alloc_end += l;
                len -= l;
        }

        return len ? write_new_chunk_(buf, data, len) : 1;
}

static inline uint32_t calc_padding(uint32_t len)
{
        uint32_t remains = len % 4;
        return remains ? 4 - remains : 0;
}

int xdr_buffer_write(struct xdr_buffer *buf, void *data, uint32_t len)
{
        int r = write_(buf, data, len);
        uint32_t padding = calc_padding(len);

        if (r && padding) {
                static uint32_t zeroes = 0;
                r  = write_(buf, &zeroes, padding);
        }

        return r;
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
};

struct xdr_cursor *xdr_cursor_create(struct xdr_buffer *buf)
{
        struct list *first;
        struct xdr_cursor *c = malloc(sizeof(*c));
        if (!c)
                return NULL;

        c->buf = buf;
        first = list_first(&buf->chunks);
        if (!first)
                c->c = NULL;
        else {
                c->c = list_item(first, struct chunk);
                c->where = c->c->start;
        }

        return c;
}

void xdr_cursor_destroy(struct xdr_cursor *c)
{
        free(c);
}

static void cursor_next_chunk_(struct xdr_cursor *c)
{
        struct list *n = list_next(&c->buf->chunks, &c->c->list);
        if (n) {
                c->c = list_item(n, struct chunk);
                c->where = c->c->start;
        } else
                c->c = NULL;
}

static
int cursor_read_(struct xdr_cursor *c, void *data, uint32_t len)
{
        /* the cursor has run off the end */
        if (!c->c)
                return 0;

        while (len) {
                uint32_t l = min(len, c->c->end - c->where);
                if (data)
                        memcpy(data, c->where, l);
                data += l;
                len -= l;
                c->where += l;
                if (c->where == c->c->end)
                        cursor_next_chunk_(c);
        }

        return !len;
}

int xdr_cursor_forward(struct xdr_cursor *c, uint32_t offset)
{
        int r;
        uint32_t padding = calc_padding(offset);
        struct xdr_cursor copy = *c;

        r = cursor_read_(&copy, NULL, offset);
        if (r && padding)
                r = cursor_read_(&copy, NULL, padding);

        if (r)
                *c = copy;

        return r;
}

int xdr_cursor_read(struct xdr_cursor *c, void *data, uint32_t len)
{
        int r;
        uint32_t padding = calc_padding(len);
        struct xdr_cursor copy = *c;

        r = cursor_read_(&copy, data, len);
        if (r && padding)
                r = cursor_read_(&copy, NULL, padding);

        if (r)
                *c = copy;
        return r;
}

/*--------------------------------*/

int xdr_unpack_using_(xdr_unpack_fn fn, void *data, size_t len, struct pool *mem, void **result)
{
        struct xdr_buffer *b;
        struct xdr_cursor *c;

        /* decode */
        b = xdr_buffer_create(1024);
        if (!b)
                return 0;

        if (!xdr_buffer_add_block(b, data, len)) {
                xdr_buffer_destroy(b);
                return 0;
        }

        c = xdr_cursor_create(b);
        if (!c) {
                xdr_buffer_destroy(b);
                return 0;
        }

        if (!fn(c, mem, result)) {
                xdr_cursor_destroy(c);
                xdr_buffer_destroy(b);
                return 0;
        }

        xdr_cursor_destroy(c);
        xdr_buffer_destroy(b);

        return 1;
}

/*----------------------------------------------------------------*/
