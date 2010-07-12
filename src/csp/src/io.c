#include "csp/io.h"

#include <errno.h>

/*----------------------------------------------------------------*/

ssize_t csp_read_exact(int fd, void *buf, size_t count)
{
        ssize_t total = 0;

        do {
                ssize_t n = csp_read(fd, buf, count);
                if (n < 0 && errno != EINTR)
                        return n;

                count -= n;
                total += n;

        } while (count);

        return total;
}

ssize_t csp_write_exact(int fd, const void *buf, size_t count)
{
        ssize_t total = 0;

        do {
                ssize_t n = csp_write(fd, buf, count);
                if (n < 0 && errno != EINTR)
                        return n;

                count -= n;
                total += n;

        } while (count);

        return total;

}

/*----------------------------------------------------------------*/
