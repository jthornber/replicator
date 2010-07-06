#include "dynamic_buffer.h"

/*----------------------------------------------------------------*/

struct dynamic_buffer {
        void *data;
        size_t len;
};

struct dynamic_buffer *dynamic_buffer_create(size_t hint)
{
        struct dynamic_buffer *db = malloc(sizeof(*db));
        if (!db)
                return NULL;

        db->data = malloc(hint);
        if (!db->data) {
                free(db);
                return NULL;
        }
        db->len = hint;

        return db;
}

void dynamic_buffer_destroy(struct dynamic_buffer *db)
{
        free(db->data);
        free(db);
}

void *dynamic_buffer_get(struct dynamic_buffer *db, size_t required_space)
{
        void *n;

        if (required_space <= db->len)
                return db->data;

        n = realloc(db->data, required_space);
        if (!n)
                return NULL;

        db->data = n;
        return db->data;
}

/*----------------------------------------------------------------*/
