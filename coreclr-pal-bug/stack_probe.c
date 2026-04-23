/*
 * stack_probe.c — C library for demonstrating CoreCLR sigaltstack overflow
 *
 * This library provides a stack-hungry signal handler that demonstrates
 * CoreCLR's per-thread 16KB sigaltstack is too small for realistic
 * signal handler chains.
 *
 * The handler uses chkstk-style stack probing (sub $0x1000,%rsp; movq $0,(%rsp))
 * which mirrors what libcoreclr.so's own functions do on Linux/x64.
 * A 64KB probe easily overflows CoreCLR's 16KB alt stack.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/ucontext.h>
#include <unistd.h>

static size_t g_probe_bytes = 64 * 1024;  // Default 64KB probe

/*
 * chkstk-style stack probe: walk rsp down 4KB at a time and write
 * through (%rsp). This mirrors what MSVC x64 chkstk does and what
 * libcoreclr.so uses for its own function prologues on Linux/x64.
 */
__attribute__((noinline))
static void probe_stack(size_t n)
{
    if (n == 0) return;
    size_t pages = (n + 4095) / 4096;
    asm volatile(
        "1:                       \n\t"
        "   sub  $0x1000, %%rsp   \n\t"
        "   movq $0, (%%rsp)      \n\t"
        "   sub  $1, %0           \n\t"
        "   jnz  1b               \n\t"
        "   add  %1, %%rsp        \n\t"   // restore rsp
        : "+r"(pages)
        : "r"((unsigned long)((n + 4095) & ~4095UL))
        : "memory", "cc");
}

/*
 * Signal handler that runs on CoreCLR's alt stack and probes
 * g_probe_bytes of stack downward. When this exceeds the alt
 * stack size, it overflows and crashes.
 */
static void probe_handler(int sig, siginfo_t *si, void *ctx)
{
    (void)sig; (void)si; (void)ctx;
    probe_stack(g_probe_bytes);
}

/*
 * Install SA_ONSTACK signal handler for the given signal.
 * probe_bytes: stack depth to probe (0 = use default 64KB)
 * Returns 0 on success, -1 on failure.
 */
int install_probe_handler(int sig, unsigned probe_bytes)
{
    if (probe_bytes) g_probe_bytes = probe_bytes;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = probe_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
    sigemptyset(&sa.sa_mask);

    return sigaction(sig, &sa, NULL);
}

/*
 * Diagnostic: dump current thread's sigaltstack state to stderr.
 * Shows whether CoreCLR installed its 16KB alt stack.
 */
int dump_sigaltstack(const char *tag)
{
    stack_t ss;
    if (sigaltstack(NULL, &ss) != 0) return -1;

    fprintf(stderr, "[probe] %s tid=%ld ss_sp=%p ss_size=%zu ss_flags=%d\n",
        tag ? tag : "",
        (long)syscall(SYS_gettid),
        ss.ss_sp, ss.ss_size, ss.ss_flags);
    fflush(stderr);
    return 0;
}

/*
 * Trivial function for P/Invoke calls from C#. Gives ThreadPool
 * workers something to do while being signal targets.
 */
int noop(void)
{
    return 42;
}

/*
 * Fire the given signal at every thread in the process except
 * the caller. Returns count of successful tgkill calls.
 * Used by signal-sender thread.
 */
int fire_signal_at_all_threads(int sig)
{
    pid_t pid = getpid();
    pid_t self = (pid_t)syscall(SYS_gettid);
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/task", pid);

    DIR *d = opendir(path);
    if (!d) return -1;

    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        pid_t tid = (pid_t)atoi(e->d_name);
        if (tid == self) continue;
        if (syscall(SYS_tgkill, pid, tid, sig) == 0) count++;
    }
    closedir(d);
    return count;
}