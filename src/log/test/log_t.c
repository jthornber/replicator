#include "log/log.h"

int main(int argc, char **argv)
{
        log_init(".", DEBUG, EVENT);

        debug("hello, world!");
        info("sldfkjsl");
        warn("lskdjfls");
        event("SERVER_STARTED", "lskdfjs");
        error("lsdkfjls");

        log_exit();

        return 0;
}
