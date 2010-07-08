#include "csp/process.h"
#include "csp/control.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned counter = 0;

enum {
        TARGET = 1000000
};

void inc(void *_)
{
        while (counter < TARGET) {
                counter++;
                csp_yield();
        }
}

int main(int argc, char **argv)
{
        unsigned nr_processes = 2;

        if (argc == 2)
                nr_processes = atoi(argv[1]);

        csp_init();

        printf("creating %d processes ...", nr_processes);
        while (nr_processes--)
                csp_spawn(inc, NULL);
        printf(" done\n");
        printf("running ...");
        csp_start();
        printf(" done\n");

        printf("%d context switches occurred\n", TARGET);

        csp_exit();

        return 0;
}
