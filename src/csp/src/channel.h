#ifndef CSP_CHANNEL_H
#define CSP_CHANNEL_H

/*----------------------------------------------------------------*/

struct channel;

struct channel *chan_create();
void chan_inc(struct channel *chan);
void chan_dec(struct channel *chan);
void chan_push(struct channel *c, void *data, size_t len);
void chan_pop(struct channel *c, void **data, size_t *len);

/* FIXME: add support for poison */

/*----------------------------------------------------------------*/

#endif
