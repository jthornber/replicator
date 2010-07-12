#include "csp/process.h"
#include "csp/control.h"

#include <assert.h>
#include <string.h>

static int run_ = 0;

void thread(void *context)
{
        const char *msg = (const char *) context;
        assert(!strcmp(msg, "Hello, world!\n"));
        run_ = 1;
}

int main(int argc, char **argv)
{
        csp_init();

        csp_spawn(thread, "Hello, world!\n");
        csp_start();
        assert(run_);

        csp_exit();

        return 0;
}
