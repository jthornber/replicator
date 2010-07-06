#include "csp/control.h"
#include "csp/io.h"
#include "csp/process.h"
#include "datastruct/list.h"
#include "protocol.h"
#include "utility/dynamic_buffer.h"

#include <stdio.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>

/*
 * Server
 */
struct client {
        struct list list;
        int socket;
        struct dynamic_buffer *db;
};

struct server {
        int stop_requested;
        int listen_socket;
        struct list clients;
};

int read_request(int fd, struct dynamic_buffer *db, struct pool *mem, command **result, uint32_t *req_id)
{
        void *data;
        request_header header_raw, *header;

        /* read the header */
        if (csp_read_exact(fd, &header_raw, sizeof(header)) < 0)
                return 0;

        if (!xdr_unpack_using(request_header, &header_raw, sizeof(header_raw), mem, &header))
                return 0;

        /* read the payload */
        data = dynamic_buffer_get(db, header->msg_size);
        if (csp_read_exact(fd, data, header->msg_size) < 0)
                return 0;

        if (!xdr_unpack_using(command, data, header->msg_size, mem, result))
                return 0;

        *req_id = header->request_id;
        return 1;
}

struct server *prepare_server(const char *host, int port)
{
        struct server *s;
        struct sockaddr_in server_address;

        s = malloc(sizeof(*s));
        if (!s)
                return NULL;

        s->listen_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (s->listen_socket < 0) {
                free(s);
                return NULL;
        }

        bzero(&server_address, sizeof(server_address));
        server_address.sin_family = AF_INET;
        server_address.sin_addr.s_addr = htonl(INADDR_ANY);
        server_address.sin_port = htons(port);

        if (bind(s->listen_socket, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
                close(s->listen_socket);
                free(s);
                return NULL;
        }

        if (listen(s->listen_socket, 20)) {
                close(s->listen_socket);
                free(s);
                return NULL;
        }

        s->stop_requested = 0;
        list_init(&s->clients);
        return s;
}

void client_loop(struct client *c)
{
        struct pool *mem = pool_create("client io", 1024);

        if (!mem)
                return;

        for (;;) {
                command *cmd;
                uint32_t req_id;

                if (!read_request(c->socket, c->db, mem, &cmd, &req_id))
                        break;

                /* FIXME: do something with the command */

                pool_empty(mem);
        }
}

void listen_loop(struct server *s)
{
        while (!s->stop_requested) {
                int client;
                struct sockaddr_in client_address;
                socklen_t len;
                struct client *c;

                client = csp_accept(s->listen_socket, (struct sockaddr *) &client_address, &len);
                if (client < 0)
                        break;

                c = malloc(sizeof(*c));
                if (!c) {
                        close(client);
                        break;
                }

                c->socket = client;
                c->db = dynamic_buffer_create(256 * 1024);
                if (!c->db) {
                        free(c);
                        close(client);
                        break;
                }

                list_add(&s->clients, &c->list);
                csp_spawn((process_fn) client_loop, c);
        }
}

/*
 * Top level
 */
int main(int argc, char **argv)
{
        struct server *s;

        csp_init();

        s = prepare_server("localhost", 6776); /* FIXME: get from the command line */
        if (!s) {
                fprintf(stderr, "couldn't start server\n");
                return 1;
        }

        csp_spawn((process_fn) listen_loop, s);
        csp_start();

        csp_exit();
        return 0;
}
