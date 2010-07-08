#include "process.h"

#include "datastruct/list.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/times.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

/*----------------------------------------------------------------*/

struct process {
        struct list list;

        ucontext_t cpu_state;
        void *stack;

        /* io manager fields */
        struct epoll_event ev;

        unsigned timeslice_remaining;
};

enum {
        STACK_SIZE = 32 * 1024,
        TIMESLICE = 10          /* milliseconds */
};

static ucontext_t scheduler_;
static ucontext_t spawn_;
static void (*launch_fn_)(void *);
static void *launch_arg_ = NULL;
static ucontext_t *launch_cpu_state_;

static LIST_INIT(schedulable_);
static LIST_INIT(dead_);

void init_process()
{
        /*
         * See the comment in csp_spawn for more info on what's happening
         * here.
         */
        void (*fn)(void *) = launch_fn_;
        void *arg = launch_arg_;

        /* now we've got the args we can switch back to the spawn function */
        swapcontext(launch_cpu_state_, &spawn_);

        /* we get here when this process is scheduled properly */
        fn(arg);
        list_move(&dead_, &csp_self()->list);
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
         * some static vars, we then need to run the new context for long
         * enough to pick up these new vars.
         */
        launch_fn_ = fn;
        launch_arg_ = context;
        makecontext(&pid->cpu_state, init_process, 0);
        launch_cpu_state_ = &pid->cpu_state;
        swapcontext(&spawn_, &pid->cpu_state);

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

void csp_yield()
{
        swapcontext(&get_current()->cpu_state, &scheduler_);
}

void csp_sleep(unsigned milli)
{

}

/*----------------------------------------------------------------*/

/* io manager */
static LIST_INIT(ios_);
static int epoll_fd;

enum io_type {
        READ,
        WRITE
};

static void io_init()
{
        epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        assert(epoll_fd >= 0);
}

static void io_exit()
{
        close(epoll_fd);
}

static int io_wait(process_t p, int fd, enum io_type direction)
{
        p->ev.events = (direction == READ ? EPOLLIN : EPOLLOUT) | EPOLLET;
        p->ev.data.ptr = p;
        p->ev.data.fd = fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &p->ev) == -1)
                return 0;
        list_move(&ios_, &p->list);
        csp_yield();
        return 1;
}

enum {
        MAX_EVENTS = 10
};

static void io_check(unsigned milli)
{
        int i;
        struct epoll_event events[MAX_EVENTS];
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, milli);
        if (nfds == -1)
                // FIXME: log
                return;

        for (i = 0; i < nfds; i++) {
                process_t p = (process_t) events[i].data.ptr;

                if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, p->ev.data.fd, &p->ev) == -1) {
                        /* FIXME: log something */
                }

                list_move(&schedulable_, &p->list);
        }
}

ssize_t csp_read(int fd, void *buf, size_t count)
{
        for (;;) {
                int n = read(fd, buf, count);
                if (n < 0 && errno == EAGAIN)
                        io_wait(csp_self(), fd, READ);
                else {
                        csp_yield();
                        return n;
                }
        }
}

ssize_t csp_write(int fd, const void *buf, size_t count)
{
        for (;;) {
                int n = write(fd, buf, count);
                if (n < 0 && errno == EAGAIN)
                        io_wait(csp_self(), fd, WRITE);
                else {
                        csp_yield();
                        return n;
                }
        }
}

void csp_set_non_blocking(int fd)
{
        fcntl(fd, F_SETFL, O_NONBLOCK);
}


/*----------------------------------------------------------------*/

/*
 * Scheduler.
 */
static void reap()
{
        process_t p, tmp;

        list_iterate_items_safe (p, tmp, &dead_) {
                list_del(&p->list);
                free_process(p);
        }
}

static void schedule()
{
        /* look at the time slice for the current process */
        process_t current = get_current();

        /* move current process to the back of the runnable list */
        list_move(&schedulable_, &current->list);
        io_check(0);
        get_current()->timeslice_remaining = TIMESLICE;
        swapcontext(&scheduler_, &get_current()->cpu_state);
}

int csp_start()
{
        /* create a context for this scheduler loop */
        /* FIXME: I'm not sure we need to this, since the scheduler always swaps into this context */
        getcontext(&scheduler_);

        for (;;) {
                reap();
                if (list_empty(&schedulable_)) /* FIXME: conjunction with other lists */
                        break;
                else
                        schedule();
        }

        return 1;
}

void csp_init()
{
        io_init();
}

void csp_exit()
{
        io_exit();
}

/*----------------------------------------------------------------*/
