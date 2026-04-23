/*
 * Multi-Stack C Reproducer for CoreCLR Sigaltstack Overflow
 *
 * Artificially replicates Go's enter/exitsyscall conditions that trigger
 * the CoreCLR sigaltstack overflow:
 *
 * 1. Multi-stack architecture (worker_stack ↔ system_stack)
 * 2. Signal-during-transition timing (vulnerable windows)
 * 3. Unusual calling conventions (assembly trampolines)
 * 4. Runtime state complexity (state machine simulation)
 *
 * Theory: By replicating Go's specific runtime characteristics, we can
 * stress CoreCLR's IP boundary analysis enough to overflow 16KB sigaltstack.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <ucontext.h>

// Stack sizes - mimic Go's architecture
#define WORKER_STACK_SIZE   (64 * 1024)   // Like goroutine stack
#define SYSTEM_STACK_SIZE   (32 * 1024)   // Like Go's g0 stack
#define MAX_WORKERS         64
#define DEFAULT_ITERATIONS  5000000

// Runtime state simulation - mimic Go's M/G/P states
typedef enum {
    STATE_NORMAL = 0,
    STATE_ENTERING_SYSCALL,     // Like Go's entersyscall()
    STATE_IN_SYSCALL,           // Like _Gsyscall state
    STATE_EXITING_SYSCALL,      // Like Go's exitsyscall()
    STATE_STACK_SWITCHING,      // Like Go's g0 transitions
    STATE_VULNERABLE            // Critical window for signal arrival
} thread_state_t;

// Per-thread context - mimic Go's M structure
typedef struct {
    int worker_id;
    thread_state_t state;
    void* worker_stack_base;
    void* system_stack_base;
    void* current_stack_ptr;
    ucontext_t worker_context;
    ucontext_t system_context;
    volatile int in_critical_section;
    pthread_t thread;
} worker_context_t;

// Global state
static volatile int g_running = 1;
static worker_context_t g_workers[MAX_WORKERS];
static int g_num_workers = MAX_WORKERS;
static int g_iterations = DEFAULT_ITERATIONS;
static int g_signal_interval_us = 1;

// Assembly trampoline declarations - create unusual calling conventions
extern void stack_switch_trampoline(void* worker_ctx, void (*func)(void*));
extern void complex_transition_bridge(void* ctx);
extern void vulnerable_signal_window(void* ctx);

// Forward declarations
static void* worker_stack_alloc(size_t size);
static void* worker_thread_main(void* arg);
static void simulate_complex_work(worker_context_t* ctx);
static void enter_syscall_simulation(worker_context_t* ctx);
static void exit_syscall_simulation(worker_context_t* ctx);
static void create_vulnerable_window(worker_context_t* ctx);

/*
 * Assembly trampolines with unusual calling conventions
 * These create the "unusual instruction boundaries" that confuse
 * CoreCLR's IP boundary analysis, similar to Go's asmcgocall
 */
__asm__(
"   .text\n"
"   .globl stack_switch_trampoline\n"
"stack_switch_trampoline:\n"
"   # Save caller state\n"
"   pushq %rbp\n"
"   movq  %rsp, %rbp\n"
"   pushq %rbx\n"
"   pushq %r12\n"
"   pushq %r13\n"
"   pushq %r14\n"
"   pushq %r15\n"
"   \n"
"   # Complex register manipulation (atypical)\n"
"   movq  %rdi, %r12        # worker_ctx\n"
"   movq  %rsi, %r13        # func pointer\n"
"   \n"
"   # Simulate Go's stack switching complexity\n"
"   movq  %rsp, %r14        # Save current stack\n"
"   \n"
"   # Call through multiple intermediate functions (unusual)\n"
"   movq  %r12, %rdi\n"
"   callq complex_transition_bridge\n"
"   \n"
"   # Create vulnerable window similar to Go's dropm()\n"
"   movq  %r12, %rdi\n"
"   callq vulnerable_signal_window\n"
"   \n"
"   # Restore state with atypical epilogue\n"
"   movq  %r14, %rsp\n"
"   popq  %r15\n"
"   popq  %r14\n"
"   popq  %r13\n"
"   popq  %r12\n"
"   popq  %rbx\n"
"   leave\n"
"   ret\n"
"\n"
"   .globl complex_transition_bridge\n"
"complex_transition_bridge:\n"
"   # Atypical prologue - different from normal C\n"
"   pushq %rbp\n"
"   pushq %rax\n"
"   pushq %rcx\n"
"   movq  %rsp, %rbp\n"
"   \n"
"   # Complex frame manipulation\n"
"   subq  $64, %rsp         # Unusual stack allocation\n"
"   movq  %rdi, -8(%rbp)    # Store context in unusual location\n"
"   \n"
"   # Multiple nested calls to create complex unwinding\n"
"   movq  -8(%rbp), %rdi\n"
"   callq vulnerable_signal_window\n"
"   \n"
"   # Atypical cleanup\n"
"   addq  $64, %rsp\n"
"   popq  %rcx\n"
"   popq  %rax\n"
"   leave\n"
"   ret\n"
"\n"
"   .globl vulnerable_signal_window\n"
"vulnerable_signal_window:\n"
"   # This creates the critical window where signals cause complex unwinding\n"
"   pushq %rbp\n"
"   movq  %rsp, %rbp\n"
"   \n"
"   # Mark as vulnerable (like Go's setg(nil))\n"
"   pushq %rdi\n"
"   callq mark_vulnerable_state\n"
"   popq  %rdi\n"
"   \n"
"   # Do complex work while vulnerable\n"
"   pushq %rdi\n"
"   callq simulate_complex_state_transition\n"
"   popq  %rdi\n"
"   \n"
"   # Exit vulnerable state\n"
"   callq clear_vulnerable_state\n"
"   \n"
"   leave\n"
"   ret\n"
);

// Stack allocation with guard pages (like Go's stack allocator)
static void* worker_stack_alloc(size_t size) {
    long pagesize = sysconf(_SC_PAGESIZE);
    size_t total = pagesize + size + pagesize; // guard + stack + guard

    void* base = mmap(NULL, total, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (base == MAP_FAILED) {
        perror("mmap failed");
        return NULL;
    }

    // Guard pages
    if (mprotect(base, pagesize, PROT_NONE) != 0 ||
        mprotect((char*)base + pagesize + size, pagesize, PROT_NONE) != 0) {
        perror("mprotect failed");
        munmap(base, total);
        return NULL;
    }

    return (char*)base + pagesize; // Return usable stack area
}

// Simulate Go's enter_syscall state transition
static void enter_syscall_simulation(worker_context_t* ctx) {
    ctx->state = STATE_ENTERING_SYSCALL;
    __sync_synchronize(); // Memory barrier like Go's runtime

    // Create complex state transition that stresses IP analysis
    ctx->in_critical_section = 1;
    ctx->state = STATE_VULNERABLE;

    // Simulate Go's complex stack manipulation during entersyscall
    if (getcontext(&ctx->worker_context) == 0) {
        ctx->state = STATE_IN_SYSCALL;
    }
}

// Simulate Go's exit_syscall state transition
static void exit_syscall_simulation(worker_context_t* ctx) {
    ctx->state = STATE_EXITING_SYSCALL;
    __sync_synchronize();

    // Another vulnerable window (like Go's exitsyscall complexity)
    ctx->state = STATE_VULNERABLE;
    create_vulnerable_window(ctx);

    ctx->in_critical_section = 0;
    ctx->state = STATE_NORMAL;
}

// Create the critical vulnerable window for signal arrival
static void create_vulnerable_window(worker_context_t* ctx) {
    // This simulates the critical window in Go's dropm() where signals
    // can arrive after setg(nil) but before cleanup completes

    volatile int dummy = 0;
    for (int i = 0; i < 1000; i++) {
        // Complex operations that require expensive IP boundary analysis
        dummy += i * ctx->worker_id;
        if (i % 100 == 0) {
            // Force some stack operations
            volatile char local_array[256];
            memset((void*)local_array, dummy & 0xFF, sizeof(local_array));
        }
    }
}

// C functions called from assembly
void mark_vulnerable_state(worker_context_t* ctx) {
    ctx->state = STATE_VULNERABLE;
    ctx->in_critical_section = 1;
}

void clear_vulnerable_state(worker_context_t* ctx) {
    ctx->state = STATE_NORMAL;
    ctx->in_critical_section = 0;
}

void simulate_complex_state_transition(worker_context_t* ctx) {
    // Simulate the complex runtime state that makes IP analysis expensive

    // Create multiple stack frames with complex relationships
    for (int depth = 0; depth < 50; depth++) {
        volatile char frame_data[512];

        // Complex pointer arithmetic (like Go's stack management)
        uintptr_t stack_ptr = (uintptr_t)frame_data;
        volatile uintptr_t* ptrs[16];

        for (int i = 0; i < 16; i++) {
            ptrs[i] = (volatile uintptr_t*)(stack_ptr + i * 32);
            *ptrs[i] = (uintptr_t)ctx + i;
        }

        // Force some actual work to prevent optimization
        if (depth % 10 == 0) {
            enter_syscall_simulation(ctx);
            exit_syscall_simulation(ctx);
        }
    }
}

// Main work simulation that creates Go-like conditions
static void simulate_complex_work(worker_context_t* ctx) {
    // Simulate Go's ping() call complexity with state transitions

    enter_syscall_simulation(ctx);

    // Use assembly trampolines to create unusual instruction boundaries
    stack_switch_trampoline(ctx, (void(*)(void*))simulate_complex_state_transition);

    exit_syscall_simulation(ctx);

    // Some actual work to prevent optimization
    volatile int result = 42;
    for (int i = 0; i < 100; i++) {
        result = (result * 7 + ctx->worker_id) % 1000000;
    }
}

// Worker thread - mimics .NET ThreadPool worker calling Go cgo
static void* worker_thread_main(void* arg) {
    worker_context_t* ctx = (worker_context_t*)arg;

    printf("[worker-%d] Starting with %d iterations\n", ctx->worker_id, g_iterations);

    // Allocate dual stacks like Go's architecture
    ctx->worker_stack_base = worker_stack_alloc(WORKER_STACK_SIZE);
    ctx->system_stack_base = worker_stack_alloc(SYSTEM_STACK_SIZE);

    if (!ctx->worker_stack_base || !ctx->system_stack_base) {
        fprintf(stderr, "[worker-%d] Stack allocation failed\n", ctx->worker_id);
        return NULL;
    }

    for (int i = 0; i < g_iterations && g_running; i++) {
        simulate_complex_work(ctx);

        // Occasional yield to increase signal/work overlap
        if (i % 10000 == 0) {
            usleep(1);
        }
    }

    printf("[worker-%d] Completed successfully\n", ctx->worker_id);
    return NULL;
}

// Signal sender - fires SIGRTMIN like CoreCLR's GC
static void* signal_sender_thread(void* arg) {
    (void)arg; // Suppress unused parameter warning
    pid_t pid = getpid();
    printf("[signal-sender] Firing SIGRTMIN every %dµs to pid %d\n",
           g_signal_interval_us, pid);

    while (g_running) {
        // Send signals to all worker threads
        for (int i = 0; i < g_num_workers; i++) {
            worker_context_t* ctx = &g_workers[i];
            if (ctx->thread != 0) {
                // Send to specific worker thread
                pthread_kill(ctx->thread, 34);
            }
        }

        // Microsecond delay
        usleep(g_signal_interval_us);
    }

    return NULL;
}

// Environment variable parsing
static int get_int_env(const char* name, int default_val) {
    const char* val = getenv(name);
    return val ? atoi(val) : default_val;
}

int main() {
    // Parse environment
    g_num_workers = get_int_env("REPRO_WORKERS", MAX_WORKERS);
    g_iterations = get_int_env("REPRO_ITERATIONS", DEFAULT_ITERATIONS);
    g_signal_interval_us = get_int_env("REPRO_INTERVAL_US", 1);

    if (g_num_workers > MAX_WORKERS) g_num_workers = MAX_WORKERS;

    printf("[multistack-c] workers=%d iters=%d interval=%dµs pid=%d\n",
           g_num_workers, g_iterations, g_signal_interval_us, getpid());
    printf("[multistack-c] Multi-stack C reproducer + SIGRTMIN\n");

    // Start signal sender
    pthread_t signal_thread;
    if (pthread_create(&signal_thread, NULL, signal_sender_thread, NULL) != 0) {
        perror("Failed to create signal thread");
        return 1;
    }

    // Start worker threads
    for (int i = 0; i < g_num_workers; i++) {
        worker_context_t* ctx = &g_workers[i];
        ctx->worker_id = i;
        ctx->state = STATE_NORMAL;

        if (pthread_create(&ctx->thread, NULL, worker_thread_main, ctx) != 0) {
            perror("Failed to create worker thread");
            g_running = 0;
            break;
        }
    }

    // Wait for workers
    for (int i = 0; i < g_num_workers; i++) {
        if (g_workers[i].thread != 0) {
            pthread_join(g_workers[i].thread, NULL);
        }
    }

    // Cleanup
    g_running = 0;
    pthread_join(signal_thread, NULL);

    printf("[multistack-c] All workers completed without crash\n");
    return 0;
}