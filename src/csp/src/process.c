#include "process.h"

#include "datastruct/list.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/times.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

// FIXME: remove
#include <stdio.h>

/*----------------------------------------------------------------*/

struct process {
        struct list list;

        ucontext_t cpu_state;
        void *stack;

        unsigned timeslice_remaining;
};

enum {
        STACK_SIZE = 32 * 1024,
        TIMESLICE = 10          /* milliseconds */
};

static ucontext_t scheduler_;
static void (*launch_fn_)(void *);
static void *launch_arg_ = NULL;

static LIST_INIT(schedulable_);

void init_process()
{
        launch_fn_(launch_arg_);
}

process_t csp_spawn(process_fn fn, void *context)
{
        process_t pid = malloc(sizeof(*pid));
        if (!pid)
                return NULL;

        memset(pid, 0, sizeof(*pid));

        if (!(pid->stack = malloc(STACK_SIZE))) {
                free(pid);
                return NULL;
        }

        list_init(&pid->list);
        if (getcontext(&pid->cpu_state) < 0) {
                free(pid);
                return NULL;
        }

        pid->cpu_state.uc_stack.ss_sp = pid->stack;
        pid->cpu_state.uc_stack.ss_size = STACK_SIZE;
        pid->cpu_state.uc_link = &scheduler_;

        /*
         * makecontext can only be passed integer arguments, sadly we want
         * to pass in a void * to our function.  So we pass info through
         * some static vars.
         */
        launch_fn_ = fn;
        launch_arg_ = context;
        makecontext(&pid->cpu_state, init_process, 0);
        pid->timeslice_remaining = TIMESLICE;
        list_add(&schedulable_, &pid->list);

        return pid;
}

void free_process(process_t p)
{
        free(p->stack);
        free(p);
}

static struct process *get_current()
{
        struct list *l = list_first(&schedulable_);
        assert(l);
        return list_item(l, struct process);

}

process_t csp_self()
{
        return get_current();
}

void csp_kill(process_t pid)
{
        list_del(&pid->list);
        free_process(pid);
}

void csp_yield(int force)
{
        swapcontext(&get_current()->cpu_state, &scheduler_);
}

void csp_sleep(unsigned milli)
{

}

/*----------------------------------------------------------------*/

/*
 * Scheduler.
 */
static void schedule_()
{
        /* look at the time slice for the current process */
        process_t current = get_current();

        /* move current process to the back of the runnable list */
        list_move(&schedulable_, &current->list);

        /*
         * FIXME: need to see if any io or sleeping threads can now
         * proceed.
         */

        get_current()->timeslice_remaining = TIMESLICE;
        swapcontext(&scheduler_, &get_current()->cpu_state);
}

int csp_start()
{
        /* create a context for this scheduler loop */
        /* FIXME: I'm not sure we need to this, since the scheduler always swaps into this context */
        getcontext(&scheduler_);

        for (;;) {
                if (list_empty(&schedulable_)) /* FIXME: conjunction with other lists */
                        break;
                else
                        schedule_();
        }

        return 1;
}

void csp_init()
{
}

void csp_exit()
{
}

/*----------------------------------------------------------------*/
