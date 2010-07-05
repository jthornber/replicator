#ifndef UTILITY_THUNK_H
#define UTILITY_THUNK_H

/*----------------------------------------------------------------*/

/* Represents a deferred computation */
struct thunk {
        void (*fn)(void *);
        void *context;
};

static inline void execute(struct thunk *t)
{
        t->fn(t->context);
}

/*----------------------------------------------------------------*/

#endif
