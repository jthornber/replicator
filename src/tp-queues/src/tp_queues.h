#ifndef TP_QUEUES_H
#define TP_QUEUES_H

/*----------------------------------------------------------------*/

/* Transactional, Persistent Queues */

struct tpq_manager;
struct tpq_queue;

struct tpq_message {
        void *data;
        size_t len;
};

struct tpq_manager_create(const char *directory);
void tpq_manager_destroy(struct tpq_manager *m);

int tpq_begin(struct tpq_manager *m);
int tpq_commit(struct tpq_manager *m, struct thunk *notify_complete);
int tpq_rollback(struct tpq_manager *m);

struct tpq_queue *tpq_open_queue(struct tpq_manager *m, const char *name);
void tpq_close_queue(struct tpq_manager *m, struct tpq_queue *q);

/*----------------------------------------------------------------*/

#endif
