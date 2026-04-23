/*
 * Per-thread "large sigaltstack" shim for the .NET + Go cgo sigaltstack
 * crash.
 *
 * The race we're avoiding:
 *   - Go's needm installs its own 32 KB sigaltstack on every non-Go
 *     thread that enters cgo.
 *   - CoreCLR's signal handler (for SIGRTMIN / INJECT_ACTIVATION_SIGNAL)
 *     needs more than 32 KB and/or Go's sigaltstack lifecycle
 *     (dropm -> SS_DISABLE -> memory recycled) races with signal
 *     delivery, producing SIGSEGV.
 *
 * The shim: install a large (default 1 MiB) sigaltstack on every thread
 * BEFORE it first calls into Go. When Go's minitSignalStack later reads
 * the current sigaltstack state, it sees an existing stack and takes
 * the "use existing" branch — it never installs its own 32 KB stack,
 * and never SS_DISABLEs on dropm. This closes both halves of the race:
 *
 *   1. Size mismatch — our stack is 1 MiB, way more than CoreCLR needs.
 *   2. Lifecycle race — we never free the memory (held until thread
 *      exit), so the kernel's sigaltstack pointer is always valid.
 *
 * Usage:
 *   cc -O2 -fPIC -shared -o libsigstack_helper.so sigstack_helper.c -lpthread
 *
 *   // In C# — call ONCE per thread, before any cgo P/Invoke on that
 *   // thread. Safe to call from any thread, cheap after the first call
 *   // (one TLS read).
 *   [DllImport("sigstack_helper")]
 *   static extern void ensure_large_sigaltstack();
 */

#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef LARGE_SIGSTACK_SIZE
#define LARGE_SIGSTACK_SIZE (1 * 1024 * 1024) /* 1 MiB */
#endif

/* Per-thread flag — set once the current thread has a shim stack
 * installed. Using __thread avoids pthread_key ceremony; it's also
 * zero-initialised so the first access on a new thread is naturally
 * "not installed". */
static __thread int large_sigstack_installed;
static __thread void* large_sigstack_base;

/*
 * Install a large sigaltstack on the current thread if it doesn't
 * already have one big enough. Idempotent per thread.
 *
 * We intentionally do NOT free the backing memory when the thread
 * exits — the stack is held for the OS thread's lifetime. Under
 * threadpool reuse this means the same memory is used across many
 * logical work items, which is fine and exactly what we want.
 */
void ensure_large_sigaltstack(void) {
    if (large_sigstack_installed) return;

    stack_t cur;
    if (sigaltstack(NULL, &cur) != 0) {
        /* Very unlikely on Linux; leave the thread as-is. */
        fprintf(stderr, "ensure_large_sigaltstack: sigaltstack(query) failed: %s\n",
                strerror(errno));
        return;
    }

    /* If the thread already has a sufficiently large alt stack (e.g.
     * someone else installed one), leave it. This lets CoreCLR's own
     * alt stack (if it already put one on the thread) stay in place
     * too. */
    if ((cur.ss_flags & SS_DISABLE) == 0 && cur.ss_size >= LARGE_SIGSTACK_SIZE) {
        large_sigstack_installed = 1;
        return;
    }

    /* Allocate: 1 page guard + LARGE_SIGSTACK_SIZE usable.
     * mmap with PROT_NONE below the stack turns overflow into a
     * clean SEGV_ACCERR at a known boundary. */
    long pagesize = sysconf(_SC_PAGESIZE);
    size_t total = (size_t)pagesize + LARGE_SIGSTACK_SIZE;
    void* base = mmap(NULL, total, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (base == MAP_FAILED) {
        fprintf(stderr, "ensure_large_sigaltstack: mmap failed: %s\n",
                strerror(errno));
        return;
    }
    /* Lower 1 page = guard. */
    if (mprotect(base, (size_t)pagesize, PROT_NONE) != 0) {
        fprintf(stderr, "ensure_large_sigaltstack: mprotect(guard) failed: %s\n",
                strerror(errno));
        /* Not fatal — keep going with the non-guarded stack. */
    }

    stack_t ss = {
        .ss_sp    = (char*)base + pagesize,
        .ss_flags = 0,
        .ss_size  = LARGE_SIGSTACK_SIZE,
    };
    if (sigaltstack(&ss, NULL) != 0) {
        fprintf(stderr, "ensure_large_sigaltstack: sigaltstack(install) failed: %s\n",
                strerror(errno));
        munmap(base, total);
        return;
    }

    large_sigstack_base = base;
    large_sigstack_installed = 1;
}

/*
 * E2 in-process probe — read the current thread's sigaltstack state
 * and append a single line to the file given by REPRO_PROBE_LOG.
 *
 * Call from C# right before and right after every ping():
 *
 *   [DllImport("sigstack_helper")] static extern void dump_sigaltstack(string tag);
 *   SigStackProbe.Dump("before");
 *   ping();
 *   SigStackProbe.Dump("after");
 *
 * Lets us see exactly what alt stack a TP worker is on before/after
 * Go runs. If 'before' shows ss_size=16384 on a fresh worker, we know
 * CoreCLR pre-installed it (no Go involvement). If 'after' shows the
 * same, Go didn't change anything (record-only branch).
 */
#include <fcntl.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/types.h>

static int probe_fd = -1;
static __thread int probe_thread_init;

void dump_sigaltstack(const char *tag) {
    if (probe_fd == -2) return; /* disabled */
    if (probe_fd == -1) {
        const char *p = getenv("REPRO_PROBE_LOG");
        if (!p || !*p) { probe_fd = -2; return; }
        int fd = open(p, O_WRONLY | O_CREAT | O_APPEND, 0644);
        probe_fd = fd >= 0 ? fd : -2;
        if (probe_fd == -2) return;
    }
    stack_t ss;
    if (sigaltstack(NULL, &ss) != 0) return;
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "tid=%ld tag=%s ss_sp=%p ss_size=%zu ss_flags=%d shim=%d\n",
        (long)syscall(SYS_gettid),
        tag ? tag : "?",
        ss.ss_sp, ss.ss_size, ss.ss_flags,
        large_sigstack_installed);
    if (n > 0) { ssize_t w = write(probe_fd, buf, (size_t)n); (void)w; }
    (void)probe_thread_init;
}

/*
 * E4 crash-snapshot SIGSEGV chain — the FIRST handler to run when
 * SIGSEGV fires. Captures (tid, ucontext rip/rsp, kernel-sigaltstack
 * view) to a pre-opened fd, then chains to the previous handler if
 * any (otherwise re-raises with default action so we still get a
 * core).
 *
 * Activated by setting REPRO_CRASH_LOG=/path/to/file BEFORE the
 * process starts. We arm it from a constructor priority 101 so it
 * runs before normal constructors and certainly before Go's
 * c-shared module init.
 */
#include <ucontext.h>

static int crash_fd = -1;
static struct sigaction prev_segv;

static void crash_handler(int sig, siginfo_t *si, void *ctx)
{
    if (crash_fd >= 0) {
        ucontext_t *uc = (ucontext_t *)ctx;
        stack_t ss; sigaltstack(NULL, &ss);
        char buf[512];
        uintptr_t rip = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
        uintptr_t rsp = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
        int n = snprintf(buf, sizeof(buf),
            "CRASH tid=%ld sig=%d code=%d addr=%p rip=0x%lx rsp=0x%lx "
            "ss_sp=%p ss_size=%zu ss_flags=%d "
            "rsp_in_alt=%d rsp_offset_from_base=%ld\n",
            (long)syscall(SYS_gettid), sig, si->si_code, si->si_addr,
            (unsigned long)rip, (unsigned long)rsp,
            ss.ss_sp, ss.ss_size, ss.ss_flags,
            (ss.ss_sp != NULL && rsp >= (uintptr_t)ss.ss_sp && rsp < (uintptr_t)ss.ss_sp + ss.ss_size) ? 1 : 0,
            ss.ss_sp != NULL ? (long)(rsp - (uintptr_t)ss.ss_sp) : -1);
        if (n > 0) { ssize_t w = write(crash_fd, buf, (size_t)n); (void)w; }
        fsync(crash_fd);
    }
    /* Chain or default. */
    if (prev_segv.sa_flags & SA_SIGINFO && prev_segv.sa_sigaction) {
        prev_segv.sa_sigaction(sig, si, ctx);
        return;
    }
    if (prev_segv.sa_handler && prev_segv.sa_handler != SIG_DFL && prev_segv.sa_handler != SIG_IGN) {
        prev_segv.sa_handler(sig);
        return;
    }
    /* Default: restore SIG_DFL and re-raise so we get a core/exit. */
    struct sigaction dfl = {0};
    dfl.sa_handler = SIG_DFL;
    sigaction(sig, &dfl, NULL);
    raise(sig);
}

__attribute__((constructor(101)))
static void install_crash_handler(void)
{
    const char *p = getenv("REPRO_CRASH_LOG");
    if (!p || !*p) return;
    int fd = open(p, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    crash_fd = fd;
    struct sigaction sa = {0};
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &prev_segv);
}
