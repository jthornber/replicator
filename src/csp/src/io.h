#ifndef CSP_IO_H
#define CSP_IO_H

/*----------------------------------------------------------------*/

/* sets non-blocking */
void set_non_blocking(int fd);

ssize_t csp_read(int fd, void *buf, size_t count);
ssize_t csp_write(int fd, const void *buf, size_t count);

/*----------------------------------------------------------------*/

#endif
