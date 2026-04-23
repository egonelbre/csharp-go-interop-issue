/*
 * fat_handler.c — stack-hungry signal handler that mimics CoreCLR's
 * chkstk-style probe.
 *
 * The CoreCLR PAL's signal-dispatch chain runs a multi-page stack-probe
 * prologue on entry to its handler before deciding what to do with the
 * signal (forward / translate / panic). On Go's 32 KB sigaltstack that
 * probe can walk off the bottom.
 *
 * We don't have to replicate CoreCLR's full handler — we only need to
 * touch enough stack to overflow whatever sigaltstack the kernel has
 * just switched us onto. REPRO_PROBE_BYTES (default 64 KB) controls
 * the depth.
 *
 * The probe is two passes:
 *   - First, write a sentinel byte every page so the kernel actually
 *     commits the pages (or faults on a guard / unmapped neighbour).
 *   - Second, sum them so the optimiser doesn't elide the writes.
 *
 * If the sigaltstack we landed on is smaller than REPRO_PROBE_BYTES the
 * very first stride should fault. The fault site (rsp, faulting addr)
 * is exactly the diagnostic we want: it tells us whether the alt stack
 * is too small, freed, or wrong-bounds.
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/ucontext.h>
#include <unistd.h>

#ifndef REPRO_PROBE_DEFAULT
#define REPRO_PROBE_DEFAULT (64 * 1024)
#endif

/* Read once at first-handler-entry so we can override via env without
 * paying getenv on every signal. */
static volatile size_t g_probe_bytes = 0;

/* Diagnostic: when REPRO_HANDLER_LOG=1, log (tid, ss_sp, ss_size,
 * ss_flags, rsp_at_entry, probe_bytes) to a file before doing the
 * dangerous probe. Useful for proving where on the alt stack we land
 * and whether it matches Go's gsignal allocation. */
static atomic_int g_log_fd = -1;

static void log_init_once(void)
{
    int expected = -1;
    if (!atomic_compare_exchange_strong(&g_log_fd, &expected, -2)) return;
    const char *path = getenv("REPRO_HANDLER_LOG");
    if (!path || !*path || !strcmp(path, "0")) {
        atomic_store(&g_log_fd, -3); /* disabled */
        return;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    atomic_store(&g_log_fd, fd >= 0 ? fd : -3);
}

static void log_handler_entry(uintptr_t user_rsp)
{
    int fd = atomic_load(&g_log_fd);
    if (fd < 0) return;
    stack_t ss; sigaltstack(NULL, &ss);
    /* Handler's own rsp — close to a local var address. */
    volatile char marker;
    uintptr_t handler_rsp = (uintptr_t)&marker;
    int handler_on_alt =
        ss.ss_sp != NULL &&
        handler_rsp >= (uintptr_t)ss.ss_sp &&
        handler_rsp <  (uintptr_t)ss.ss_sp + ss.ss_size;
    long room_below =
        ss.ss_sp != NULL ? (long)(handler_rsp - (uintptr_t)ss.ss_sp) : -1;
    char buf[320];
    int n = snprintf(buf, sizeof(buf),
        "tid=%ld ss_sp=%p ss_size=%zu ss_flags=%d "
        "user_rsp=0x%lx hdlr_rsp=0x%lx hdlr_on_alt=%d room_below=%ld probe=%zu\n",
        (long)syscall(SYS_gettid),
        ss.ss_sp, ss.ss_size, ss.ss_flags,
        (unsigned long)user_rsp,
        (unsigned long)handler_rsp,
        handler_on_alt,
        room_below,
        g_probe_bytes);
    if (n > 0) {
        ssize_t w = write(fd, buf, (size_t)n); (void)w;
    }
}

__attribute__((noinline))
static void probe_stack_alloca(size_t n)
{
    /* alloca-style: place the buffer on the *signal* stack we landed
     * on. The page-stride writes are what trigger the kernel to commit
     * (or fault) each page of stack. This is a "soft" probe — if the
     * memory below the alt stack is *mapped* (an adjacent goroutine
     * stack from Go's pool), the writes succeed and silently corrupt
     * it; the SEGV materialises later in unrelated Go code. */
    volatile unsigned char *buf = __builtin_alloca(n);
    for (size_t i = 0; i < n; i += 4096) {
        buf[i] = (unsigned char)i;
    }
    asm volatile("" :: "r"(buf) : "memory");
}

__attribute__((noinline))
static void probe_stack_chkstk(size_t n)
{
    /* CoreCLR-style: explicit `sub $0x1000,%rsp; movq $0,(%rsp)` loop.
     * Walks rsp down 4 KB at a time and writes through (%rsp). When
     * it crosses the bottom of the alt stack into an unmapped page
     * (the doublemapper hole), the very first write through (%rsp)
     * faults. Because we're inside a signal handler at that point,
     * the kernel can't deliver a recoverable SEGV → process dies with
     * SI_KERNEL — matching the .NET-case signature. */
    size_t pages = (n + 4095) / 4096;
    asm volatile (
        "1:                       \n\t"
        "   sub  $0x1000, %%rsp   \n\t"
        "   movq $0, (%%rsp)      \n\t"
        "   sub  $1, %0           \n\t"
        "   jnz  1b               \n\t"
        "   add  %1, %%rsp        \n\t"   /* restore rsp */
        : "+r"(pages)
        : "r"((unsigned long)((n + 4095) & ~4095UL))
        : "memory", "cc");
}

static int g_chkstk = 0;

__attribute__((noinline))
static void probe_stack(size_t n)
{
    if (g_chkstk) probe_stack_chkstk(n);
    else          probe_stack_alloca(n);
}

void fat_handler(int sig, siginfo_t *si, void *ctx)
{
    (void)sig; (void)si;
    if (g_probe_bytes == 0) {
        const char *v = getenv("REPRO_PROBE_BYTES");
        size_t n = v && *v ? (size_t)strtoul(v, NULL, 0) : REPRO_PROBE_DEFAULT;
        if (n < 4096) n = 4096;
        g_probe_bytes = n;
        const char *m = getenv("REPRO_PROBE_MODE");
        g_chkstk = (m && (!strcmp(m, "chkstk") || !strcmp(m, "1"))) ? 1 : 0;
        log_init_once();
    }
    if (atomic_load(&g_log_fd) >= 0) {
        ucontext_t *uc = (ucontext_t *)ctx;
        log_handler_entry((uintptr_t)uc->uc_mcontext.gregs[REG_RSP]);
    }
    probe_stack(g_probe_bytes);
}
