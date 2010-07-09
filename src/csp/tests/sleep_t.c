#include "csp/process.h"
#include "csp/control.h"

#include <assert.h>
#include <stdio.h>

void thread(void *context)
{
        uintptr_t count = (uintptr_t) context;

        int i;
        for (i = 0; i < 10; i++) {
                csp_sleep(count);
                fprintf(stderr, "slept for %ld\n", count);
        }
}

int main(int argc, char **argv)
{
        csp_init();

        csp_spawn(thread, (void *) 73);
        csp_spawn(thread, (void *) 113);
        csp_start();

        csp_exit();

        return 0;
}
