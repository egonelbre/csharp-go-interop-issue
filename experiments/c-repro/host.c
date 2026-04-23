/*
 * c-repro/host.c — pure-C reproducer for go#78883.
 *
 * Mimics what the .NET TP-worker repro does to Go, but with no managed
 * runtime in the picture:
 *
 *   - dlopen ./libgolib.so and call its `ping` cgo export from N
 *     pthread workers in a tight loop.
 *   - One sender thread enumerates /proc/self/task and tgkill's
 *     SIGRTMIN+REPRO_RTSIG (default 2 → kernel signal 34) at every
 *     other thread every REPRO_INTERVAL_US microseconds.
 *   - Handler is fat_handler() (see fat_handler.c) — it touches
 *     REPRO_PROBE_BYTES of stack downward to mimic CoreCLR's chkstk
 *     prologue.
 *
 * If this crashes the same way as the .NET repro (rsp inside the
 * 8-page hole that was Go's gsignal stack), the bug is purely in Go's
 * sigaltstack lifecycle and has nothing CoreCLR-specific about it.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

extern void fat_handler(int sig, siginfo_t *si, void *ctx);

typedef int (*ping_fn)(void);
static ping_fn g_ping;

static int g_workers      = 32;
static long g_iterations  = 1000000;
static int g_interval_us  = 50;
static int g_rtsig_offset = 0;     /* glibc's SIGRTMIN = 34 already */
static volatile int g_done = 0;

static pid_t gettid_(void) { return (pid_t)syscall(SYS_gettid); }

static void *worker_thread(void *arg)
{
    long n = g_iterations;
    volatile int sink = 0;
    for (long i = 0; i < n && !g_done; i++) {
        sink ^= g_ping();
    }
    (void)sink;
    (void)arg;
    return NULL;
}

static void *sender_thread(void *arg)
{
    pid_t pid       = getpid();
    pid_t self_tid  = gettid_();
    pid_t main_tid  = (pid_t)(uintptr_t)arg;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/task", pid);
    int sig = SIGRTMIN + g_rtsig_offset;

    struct timespec ts = {0, (long)g_interval_us * 1000L};
    while (!g_done) {
        DIR *d = opendir(path);
        if (!d) { nanosleep(&ts, NULL); continue; }
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
            pid_t tid = (pid_t)atoi(e->d_name);
            if (tid == self_tid || tid == main_tid) continue;
            (void)syscall(SYS_tgkill, pid, tid, sig);
        }
        closedir(d);
        nanosleep(&ts, NULL);
    }
    return NULL;
}

static void install_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = fat_handler;
    sa.sa_flags     = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    int sig = SIGRTMIN + g_rtsig_offset;
    if (sigaction(sig, &sa, NULL) != 0) {
        perror("sigaction");
        exit(1);
    }
}

static long getenv_long(const char *k, long dflt)
{
    const char *v = getenv(k);
    if (!v || !*v) return dflt;
    return strtol(v, NULL, 0);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    g_workers      = (int)getenv_long("REPRO_WORKERS",     g_workers);
    g_iterations   =      getenv_long("REPRO_ITERATIONS",  g_iterations);
    g_interval_us  = (int)getenv_long("REPRO_INTERVAL_US", g_interval_us);
    g_rtsig_offset = (int)getenv_long("REPRO_RTSIG",       g_rtsig_offset);

    const char *libpath = getenv("REPRO_GOLIB");
    if (!libpath || !*libpath) libpath = "./libgolib.so";

    void *h = dlopen(libpath, RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        fprintf(stderr, "dlopen(%s) failed: %s\n", libpath, dlerror());
        return 1;
    }
    g_ping = (ping_fn)dlsym(h, "ping");
    if (!g_ping) {
        fprintf(stderr, "dlsym(ping) failed: %s\n", dlerror());
        return 1;
    }

    install_handler();

    fprintf(stderr,
        "[c-repro] workers=%d iters=%ld interval=%dus rtsig=SIGRTMIN+%d (=%d) pid=%d\n",
        g_workers, g_iterations, g_interval_us, g_rtsig_offset,
        SIGRTMIN + g_rtsig_offset, getpid());

    pthread_t *tids = calloc((size_t)g_workers, sizeof(pthread_t));
    pthread_t  sender;
    pid_t main_tid = gettid_();

    if (pthread_create(&sender, NULL, sender_thread, (void *)(uintptr_t)main_tid) != 0) {
        perror("pthread_create sender"); return 1;
    }
    for (int i = 0; i < g_workers; i++) {
        if (pthread_create(&tids[i], NULL, worker_thread, (void *)(long)i) != 0) {
            perror("pthread_create worker"); return 1;
        }
    }
    for (int i = 0; i < g_workers; i++) {
        pthread_join(tids[i], NULL);
    }
    g_done = 1;
    pthread_join(sender, NULL);
    fprintf(stderr, "[c-repro] all workers finished without crash\n");
    return 0;
}
