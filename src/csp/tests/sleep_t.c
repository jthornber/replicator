#include "csp/process.h"
#include "csp/control.h"

#include <assert.h>
#include <stdio.h>

void thread(void *_)
{
        int i;
        for (i = 0; i < 100; i++) {
                csp_sleep(10);
                printf(".");
        }
}

int main(int argc, char **argv)
{
        csp_init();

        csp_spawn(thread, NULL);
        csp_start();

        csp_exit();

        return 0;
}
