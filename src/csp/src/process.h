#ifndef CSP_PROCESS_H
#define CSP_PROCESS_H

/*----------------------------------------------------------------*/

/* Communicating, Sequential Processes */

typedef uint32_t process_t;

process_t spawn(struct thunk *task);
process_t self();
void kill_process(process_t pid);
void yield(int force);
void sleep(unsigned milli);

/*----------------------------------------------------------------*/

#endif
