#include "mm/pool.h"
#include "xdr/xdr.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/*
 * This test program only exercises the xdr_buffer_* and xdr_cursor_*
 * functions.  The pack/unpack functions will be tested as part of the
 * xdrgen test suite.
 */
void test_create_destroy()
{
        int i;
        for (i = 0; i < 1000; i++) {
                struct xdr_buffer *b = xdr_buffer_create(i);
                assert(b);
                xdr_buffer_destroy(b);
        }
}

void test_add_block()
{
        int i;
        struct pool *mem = pool_create("test_add_block", 10240);
        struct xdr_buffer *b = xdr_buffer_create(1024);

        assert(mem);
        assert(b);

        /*
         * Interleave external blocks with internally allocated ones.
         * We're mainly looking for memory leaks here.
         */
        for (i = 10; i < 10000; i += 13) {
                const char *msg = "hello, world!";
                void *data = pool_alloc(mem, i);
                assert(xdr_buffer_add_block(b, data, i));
                assert(xdr_buffer_write(b, (void *) msg, strlen(msg)));
        }

        xdr_buffer_destroy(b);
        pool_destroy(mem);
}

void test_write()
{
        enum {
                MAX = 1024
        };

        int i, j;
        unsigned char data[MAX];
        struct xdr_buffer *b = xdr_buffer_create(1024);
        struct xdr_cursor *c;

        for (i = 0; i < MAX; i++) {
                for (j = 0; j < i; j++)
                        data[j] = (unsigned char) i & 0xff;

                assert(xdr_buffer_write(b, (void *) data, i));
        }

        c = xdr_cursor_create(b);
        for (i = 0; i < MAX; i++) {
                assert(xdr_cursor_read(c, data, i));
                for (j = 0; j < i; j++)
                        assert(data[j] == ((unsigned char) i & 0xff));
        }

        xdr_cursor_destroy(c);
        xdr_buffer_destroy(b);
}

int main(int argc, char **argv)
{
        test_create_destroy();
        test_add_block();
        test_write();
        return 0;
}
