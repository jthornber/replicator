#include "protocol.h"
#include "csp/control.h"
#include "csp/io.h"
#include "utility/dynamic_buffer.h"

#include <stdio.h>

/*
 * Server
 */
int read_request(int fd, struct dynamic_buffer *db, struct pool *mem, command **result, uint32_t *req_id)
{
        void *data;
        request_header header_raw, *header;

        /* read the header */
        if (read_exact(fd, &header_raw, sizeof(header)) < 0)
                return 0;

        if (!xdr_unpack_using(request_header, &header_raw, sizeof(header_raw), mem, &header))
                return 0;

        /* read the payload */
        data = dynamic_buffer_get(db, header->msg_size);
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
