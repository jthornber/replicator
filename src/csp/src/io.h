#ifndef CSP_IO_H
#define CSP_IO_H

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

/*----------------------------------------------------------------*/

/*
 * Use the following io functions in a csp environment to ensure that
 * control is yielded if io would block.
 */

/*
 * You must set non blocking on all file descriptors passed to these
 * functions.
 */
void set_non_blocking(int fd);

ssize_t csp_read(int fd, void *buf, size_t count);
ssize_t csp_write(int fd, const void *buf, size_t count);

/*
 * Non-blocking will have already been set on the client socket returned.
 */
int csp_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

ssize_t read_exact(int fd, void *buf, size_t count);
ssize_t write_exact(int fd, const void *buf, size_t count);

/*----------------------------------------------------------------*/

#endif
