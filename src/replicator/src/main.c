#include "protocol.h"
#include "csp/control.h"
#include "csp/io.h"

#include <stdio.h>

/*
 * Server
 */
int read_request(int fd, struct pool *mem, command **result, uint32_t *req_id)
{
        request_header header_raw, *header;
        char data[32 * 1024];   /* FIXME: use a dynamic buffer */

        /* read the header */
        if (read_exact(fd, &header_raw, sizeof(header)) < 0)
                return 0;

        if (!xdr_unpack_using(request_header, &header_raw, sizeof(header_raw), mem, &header))
                return 0;

        /* read the payload */
        if (read_exact(fd, data, header->msg_size) < 0)
                return 0;

        if (!xdr_unpack_using(command, data, header->msg_size, mem, result))
                return 0;

        *req_id = header->request_id;
        return 1;
}

/*
 * Top level
 */
int main(int argc, char **argv)
{
        /* start the server */

        /* set up the top level csp process */

        /* start the csp scheduler */

        printf("hello, world!\n");
        return 0;
}
