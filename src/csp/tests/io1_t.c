#include "csp/process.h"
#include "csp/control.h"
#include "csp/io.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

static int fds[2];

enum {
        TARGET = 1000000
};

void reader(void *_)
{
        char c;
        while (csp_read(fds[0], &c, 1) > 0) {
                printf("r");
                assert(c == 'j');
        }
}

void writer(void *_)
{
        int i;
        char c = 'j';
        for (i = 0; i < TARGET; i++) {
                printf("w");
                csp_write(fds[1], &c, 1);
        }
        close(fds[1]);
}

int main(int argc, char **argv)
{
        csp_init();

        if (pipe(fds) == -1) {
                perror("pipe2 call failed");
                exit(1);
        }
        csp_set_non_blocking(fds[0]);
        csp_set_non_blocking(fds[1]);

        csp_spawn(reader, NULL);
        csp_spawn(writer, NULL);
        csp_start();
        printf("%d write/read cycles\n", TARGET);
        csp_exit();

        return 0;
}
