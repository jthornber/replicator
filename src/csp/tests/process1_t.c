#include "csp/process.h"
#include "csp/control.h"

#include <assert.h>
#include <stdio.h>

void thread(void *context)
{
        const char *msg = (const char *) context;
        printf("%s", msg);
}

int main(int argc, char **argv)
{
        csp_init();

        csp_spawn(thread, "Hello, world!\n");
        csp_start();

        csp_exit();

        return 0;
}
