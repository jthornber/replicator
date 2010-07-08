#include "csp/process.h"
#include "csp/control.h"

#include <assert.h>
#include <stdio.h>

void print(void *context)
{
        const char *msg = (const char *) context;

        for (;;) {
                printf("%s\n", msg);
                csp_yield(0);
        }
}

int main(int argc, char **argv)
{
        csp_init();

        csp_spawn(print, "hello from process 1");
        csp_spawn(print, "hello from process 2");
        csp_start();

        csp_exit();

        return 0;
}
