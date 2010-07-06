#ifndef CSP_PROCESS_H
#define CSP_PROCESS_H

#include <stdint.h>

/*----------------------------------------------------------------*/

/* Communicating, Sequential Processes */

typedef uint32_t process_t;
typedef void (*process_fn)(void *);
process_t csp_spawn(process_fn fn, void *context);
process_t csp_self();
void csp_kill(process_t pid);
void csp_yield(int force);
void csp_sleep(unsigned milli);

/*----------------------------------------------------------------*/

#endif
