#include "xdrgen.h"
#include "pool.h"

#include <time.h>
#include <stdio.h>
#include <string.h>

/*----------------------------------------------------------------*/

static struct pool *global_pool_;

void init()
{
        global_pool_ = pool_create("global pool", 100 * 1024);
        if (!global_pool_) {
                fprintf(stderr, "couldn't allocate global memory pool\n");
                exit(1);
        }

        srandom(time(0));
}

void fin()
{
        pool_destroy(global_pool_);
}

void *zalloc(size_t s)
{
        void *r = pool_alloc(global_pool_, s);
        if (!r) {
                fprintf(stderr, "out of memory\n");
                exit(1);
        }

        return r;
}

char *dup_string(const char *str)
{
        char *r = pool_alloc(global_pool_, strlen(str) + 1);
        if (!r) {
                fprintf(stderr, "out of memory\n");
                exit(1);
        }

        strcpy(r, str);
        return r;
}

/*--------------------------------*/

static unsigned line_ = 1;

void inc_line()
{
        line_++;
}

unsigned get_line()
{
        return line_;
}

/*--------------------------------*/

static struct specification *spec_ = NULL;

void set_result(struct specification *spec)
{
        spec_ = spec;
}

struct specification *get_result()
{
        return spec_;
}

/*----------------------------------------------------------------*/

