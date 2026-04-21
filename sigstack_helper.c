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
