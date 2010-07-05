#ifndef MERGE_MERGE_H
#define MERGE_MERGE_H

/*----------------------------------------------------------------*/

struct merge;

struct merge *merge_create(struct journal *j);
void merge_destroy(struct merge *m);

int merge_bind_device(struct merge *m, struct journal_device *dev, const char *dev_path);
struct journal_replayer* merge_get_replayer(struct merge *m);

/*----------------------------------------------------------------*/

#endif
