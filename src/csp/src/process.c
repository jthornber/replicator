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
#include <sys/types.h>
#include <sys/socket.h>

#if DEBUG
#include <stdio.h>
#endif

/*----------------------------------------------------------------*/

struct process {
        struct list list;

        ucontext_t cpu_state;
        void *stack;

        /* io manager fields */
        struct epoll_event ev;

        /* sleep */
        float delta;
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
        process_t p = csp_self();

        /* move current process to the back of the runnable list */
        list_move(&schedulable_, &p->list);
        swapcontext(&p->cpu_state, &scheduler_);
}

/*----------------------------------------------------------------*/

/* io manager */
static int epoll_fd;
static int io_count = 0;

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
        list_del(&p->list);
        io_count++;
        swapcontext(&p->cpu_state, &scheduler_);
        io_count--;
        return 1;
}

enum {
        MAX_EVENTS = 10
};

static void io_check(unsigned milli)
{
        int i;
        struct epoll_event events[MAX_EVENTS];

        /* try and avoid an unnecessary system call */
        if (!milli && !io_count)
                return;

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

int csp_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
        for (;;) {
                int fd = accept(sockfd, addr, addrlen);
                if (fd < 0 && errno == EAGAIN)
                        io_wait(csp_self(), sockfd, READ);
                else {
                        csp_yield();
                        return fd;
                }
        }
}

/*----------------------------------------------------------------*/

/* sleep manager */
static LIST_INIT(sleepers_);

static float ts_to_float(struct timespec *ts)
{
        return ((float) ts->tv_sec) + ((float) ts->tv_nsec) / 1000000000.0;
}

static float real_elapsed()
{
        static int started = 0;
        static float last;
        static unsigned long init_sec;

        struct timespec t;

        if (clock_gettime(CLOCK_REALTIME, &t) < 0) {
                // FIXME: log
                return 1.0;
        }

        if (!started) {
                init_sec = t.tv_sec;
                t.tv_sec = 0;
                last = ts_to_float(&t);
                started = 1;
                return 0.0;
        } else {
                t.tv_sec -= init_sec;
                float now = ts_to_float(&t);
                float delta = now - last;
                last = now;
                return delta;
        }
}

#if DEBUG
static void print_sleepers()
{
        struct process *cp;
        int i = 0;
        list_iterate_items(cp, &sleepers_)
                fprintf(stderr, "sleeper[%d]: %f\n", i++, cp->delta);
}
#endif

/*
 * Returns the number of seconds that can elapse before you should call
 * this function again.
 */
static float sleep_check()
{
        float elapsed = real_elapsed(), r = 1.0;
        struct list *l = sleepers_.n;

        while (l != &sleepers_) {
                struct process *p = list_item(l, struct process);

                struct list *n = l->n;
                if (elapsed >= p->delta) {
                        elapsed -= p->delta;
                        list_move(&schedulable_, &p->list);
                } else {
                        p->delta -= elapsed;
                        r = p->delta;
                        break;
                }
                l = n;
        }

        return r;
}

static void sleep_add(process_t p, float delay)
{
        /* we call sleep_check first to ensure deltas are up to date */
        sleep_check();

        struct list *l = sleepers_.n;
        while (l != &sleepers_) {
                struct process *cp = list_item(l, struct process);
                if (delay > cp->delta)
                        delay -= cp->delta;
                else {
                        cp->delta -= delay;
                        break;
                }
                l = cp->list.n;
        }

        p->delta = delay;
        list_move(l, &p->list);
}

void csp_sleep(unsigned milli)
{
        process_t p = csp_self();
        sleep_add(p, ((float) milli) / 1000.0);
        swapcontext(&p->cpu_state, &scheduler_);
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
        float sleep_delta = sleep_check();
        io_check(list_empty(&schedulable_) ? sleep_delta * 1000.0 : 0);
        if (!list_empty(&schedulable_))
                swapcontext(&scheduler_, &get_current()->cpu_state);
}

int csp_start()
{
        /* create a context for this scheduler loop */
        /* FIXME: I'm not sure we need to this, since the scheduler always swaps into this context */
        getcontext(&scheduler_);

        for (;;) {
                reap();
                if (list_empty(&schedulable_) &&
                    list_empty(&sleepers_))
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
