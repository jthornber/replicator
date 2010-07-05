#ifndef LVM_XDR_H
#define LVM_XDR_H

#include <arpa/inet.h>
#include <stdlib.h>
#include <stdint.h>

/*----------------------------------------------------------------*/

/*
 * The low level buffer used for packing and unpacking objects.
 */
struct xdr_buffer;

/*
 * |size_hint| is just a hint for your estimate of the final buffer size.
 */
struct xdr_buffer *xdr_buffer_create(size_t size_hint);
void xdr_buffer_destroy(struct xdr_buffer *buf);

/*
 * Adds a preallocated block, this is assumed to be filled with data.  The
 * buffer will hold a reference to the block for its lifetime.  Ownership
 * does not pass however so caller is ultimately responsible for freeing
 * it.
 */
int xdr_buffer_add_block(struct xdr_buffer *buf, void *data, size_t len);

/*
 * Writing data to a buffer.  Make sure your data length is a multiple of
 * 4, this call will fail otherwise.  This has atomic semantics, it either
 * succeeds and all the data is written, or it fails and _no_ data is
 * written.
 */
int xdr_buffer_write(struct xdr_buffer *buf, void *data, size_t len);

size_t xdr_buffer_size(struct xdr_buffer *buf);

struct xdr_cursor;

struct xdr_cursor *xdr_cursor_create(struct xdr_buffer *buf);
void xdr_cursor_destroy(struct xdr_cursor *c);
int xdr_cursor_forward(struct xdr_cursor *c, size_t offset);
int xdr_cursor_read(struct xdr_cursor *c, void *data, size_t len);

/*--------------------------------*/

/*
 * Pack functions for the basic xdr types.  These return 0 on failure.
 * Which can only really happen in oom situations.
 */
static inline int xdr_pack_uint(struct xdr_buffer *buf, uint32_t n)
{
        uint32_t raw = htonl(n);
        return xdr_buffer_write(buf, &raw, sizeof(raw));
}

static inline int xdr_pack_bool(struct xdr_buffer *buf, int b)
{
        return xdr_pack_uint(buf, b ? 1 : 0);
}

static inline int xdr_pack_int(struct xdr_buffer *buf, int32_t i)
{
        union { int32_t i; uint32_t u; } u;
        u.i = i;
        return xdr_pack_uint(buf, u.u);
}

static inline int xdr_pack_uhyper(struct xdr_buffer *buf, uint64_t n)
{
        /* first we pack the low bits */
        if (!xdr_pack_uint(buf, n & 0xffffffff))
                return 0;

        /* then the high */
        n >>= 32;
        return xdr_pack_uint(buf, n);
}

static inline int xdr_pack_hyper(struct xdr_buffer *buf, int64_t i)
{
        union { int64_t i; uint64_t u; } u;
        u.i = i;
        return xdr_pack_uhyper(buf, u.u);
}

static inline int xdr_pack_float(struct xdr_buffer *buf, float f)
{
        /* FIXME: this needs double checking. */
        union { float f; uint32_t u; } u;
        u.f = f;
        return xdr_pack_uint(buf, u.u);
}

static inline int xdr_pack_double(struct xdr_buffer *buf, double d)
{
        /* FIXME: double check */
        union { double d; uint64_t u; } u;
        u.d = d;
        return xdr_pack_uhyper(buf, u.u);
}

/*
 * The corresponding unpack functions extract into already allocated
 * memory.  They can only fail if there's not enough source data in the
 * buffer.
 */

static inline int xdr_unpack_uint(struct xdr_cursor *c, uint32_t *n)
{
        if (!xdr_cursor_read(c, n, sizeof(*n)))
                return 0;

        *n = ntohl(*n);
        return 1;
}

static inline int xdr_unpack_bool(struct xdr_cursor *c, int *b)
{
        union { int32_t i; uint32_t u; } u;
        if (!xdr_unpack_uint(c, &u.u))
                return 0;

        *b = u.i;
        return 1;
}

static inline int xdr_unpack_int(struct xdr_cursor *c, int32_t *i)
{
        union { int32_t i; uint32_t u; } u;
        if (!xdr_unpack_uint(c, &u.u))
                return 0;

        *i = u.i;
        return 1;
}

static inline int xdr_unpack_uhyper(struct xdr_cursor *c, uint64_t *n)
{
        uint32_t l, h;
        if (!xdr_unpack_uint(c, &l))
                return 0;

        if (!xdr_unpack_uint(c, &h))
                return 0;

        *n = h;
        *n <<= 32;
        *n |= l;
        return 1;
}

static inline int xdr_unpack_hyper(struct xdr_cursor *c, int64_t *i)
{
        union { int64_t i; uint64_t u; } u;
        if (!xdr_unpack_uhyper(c, &u.u))
                return 0;

        *i = u.i;
        return 1;
}

static inline int xdr_unpack_float(struct xdr_cursor *c, float *f)
{
        union { float f; uint32_t u; } u;
        if (!xdr_unpack_uint(c, &u.u))
                return 0;

        *f = u.f;
        return 1;
}

static inline int xdr_unpack_double(struct xdr_cursor *c, double *d)
{
        union { double d; uint64_t u; } u;
        if (!xdr_unpack_uhyper(c, &u.u))
                return 0;

        *d = u.d;
        return 1;
}

/*----------------------------------------------------------------*/

#endif
