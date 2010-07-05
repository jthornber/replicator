#ifndef JOURNAL_JOURNAL_H
#define JOURNAL_JOURNAL_H

/*----------------------------------------------------------------*/

struct journal;
struct journal_device;
struct journal_transaction;

/* 512 byte sectors */
typedef uint64_t journal_sector_t;

struct journal_io {
        struct journal_dev *dev;

        journal_sector_t start_sector;
        journal_sector_t end_sector;
        void *data;
};

struct journal_replayer {
        void *context;

        void (*begin)(void *);
        void (*io)(void *, struct journal_io *);
        void (*commit)(void *);
};

struct journal *journal_create(const char *directory);
void journal_destroy(const char *directory);

struct journal_device *journal_register_device(struct journal *j, const char *name, );

int journal_begin(struct journal *j);
void journal_record_io(struct journal *j, struct journal_io *io);
int journal_commit(struct journal *j, struct thunk *notify_complete);

unsigned journal_transaction_count(struct journal *j);
int journal_transaction_front(struct journal *j, unsigned index);
int journal_transaction_replay_front(struct journal *j, unsigned index, struct journal_replayer *replay);

/*----------------------------------------------------------------*/

#endif
