/*
 * trace_sigaltstack.c — LD_PRELOAD shim that logs every sigaltstack(2)
 * the process makes, plus enough context to attribute the call to its
 * library (Go runtime vs CoreCLR vs glibc vs us).
 *
 * Output: a binary ring of fixed-size records to a file path given by
 *   REPRO_TRACE_LOG=/tmp/sigaltstack.bin
 * (default ./sigaltstack.bin in cwd). Use the bundled `decode.py` /
 * shell snippets to dump it.
 *
 * Why binary not text: signal handlers may run while a sigaltstack
 * call is in flight; using snprintf+write per call is slow enough that
 * it can mask the race. A 64-byte fixed record + atomic seek-and-write
 * is microsecond-scale.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

struct rec {
    uint64_t ts_ns;
    int32_t  tid;
    int32_t  rc;
    int32_t  errno_;
    int32_t  pad;
    /* new */
    void    *new_sp;
    uint64_t new_size;
    int32_t  new_flags;
    int32_t  new_present;
    /* old (returned) */
    void    *old_sp;
    uint64_t old_size;
    int32_t  old_flags;
    int32_t  old_present;
    /* caller — first non-libc, non-self frame */
    void    *caller_pc;
};

_Static_assert(sizeof(struct rec) == 80, "rec size");

static atomic_int   g_fd = -1;
static atomic_uchar g_init_done = 0;

static int (*real_sigaltstack)(const stack_t *, stack_t *);

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void init_once(void)
{
    if (atomic_load(&g_init_done)) return;
    /* Lazy init under a coarse lock — sigaltstack is rare enough that
     * a single lock acquisition per process is fine. */
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mu);
    if (!atomic_load(&g_init_done)) {
        real_sigaltstack = dlsym(RTLD_NEXT, "sigaltstack");
        const char *path = getenv("REPRO_TRACE_LOG");
        if (!path || !*path) path = "./sigaltstack.bin";
        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        atomic_store(&g_fd, fd);
        atomic_store(&g_init_done, 1);
    }
    pthread_mutex_unlock(&mu);
}

int sigaltstack(const stack_t *ss, stack_t *oss)
{
    if (!real_sigaltstack) init_once();

    /* Capture the caller before we invoke real_sigaltstack so we pay
     * the unwind cost off the critical path. */
    void *caller = __builtin_return_address(0);

    /* Always read the previous state, even if caller passed oss=NULL,
     * so we have a complete before/after picture. */
    stack_t local_oss;
    stack_t *p_oss = oss ? oss : &local_oss;

    int rc  = real_sigaltstack(ss, p_oss);
    int err = errno;

    int fd = atomic_load(&g_fd);
    if (fd >= 0) {
        struct rec r = {0};
        r.ts_ns       = now_ns();
        r.tid         = (int32_t)syscall(SYS_gettid);
        r.rc          = rc;
        r.errno_      = err;
        if (ss) {
            r.new_present = 1;
            r.new_sp      = ss->ss_sp;
            r.new_size    = (uint64_t)ss->ss_size;
            r.new_flags   = ss->ss_flags;
        }
        r.old_present = 1;
        r.old_sp      = p_oss->ss_sp;
        r.old_size    = (uint64_t)p_oss->ss_size;
        r.old_flags   = p_oss->ss_flags;
        r.caller_pc   = caller;
        /* Single write — kernel guarantees atomicity for blocks <
         * PIPE_BUF on regular files isn't a thing, but for short
         * O_APPEND writes the kernel still serialises. 80 bytes is
         * well under any plausible page-tear. */
        ssize_t w = write(fd, &r, sizeof(r));
        (void)w;
    }
    errno = err;
    return rc;
}
