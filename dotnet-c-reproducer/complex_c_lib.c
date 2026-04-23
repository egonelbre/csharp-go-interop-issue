/*
 * Complex C Library - Replicates Go's enter/exitsyscall conditions
 *
 * This library implements the specific conditions that Go's cgo mechanism
 * creates, but in pure C. When called from .NET via P/Invoke, it should
 * stress CoreCLR's IP boundary analysis enough to trigger sigaltstack overflow.
 *
 * Key Go characteristics replicated:
 * 1. Complex runtime state transitions (like enter/exitsyscall)
 * 2. Multi-stack-like call complexity
 * 3. Unusual instruction boundaries via assembly
 * 4. Vulnerable signal timing windows
 */

#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <setjmp.h>

// Simulate Go's runtime states
typedef enum {
    STATE_NORMAL = 0,
    STATE_ENTERING_SYSCALL,     // Like Go's entersyscall()
    STATE_IN_SYSCALL,           // Like _Gsyscall
    STATE_EXITING_SYSCALL,      // Like Go's exitsyscall()
    STATE_VULNERABLE            // Critical window
} runtime_state_t;

// Per-thread context (like Go's M structure)
typedef struct {
    runtime_state_t state;
    jmp_buf saved_context;
    void* stack_contexts[8];    // Multiple stack contexts
    int context_depth;
    volatile int in_critical_section;
    uint64_t transition_counter;
} thread_context_t;

// Thread-local storage for context
static __thread thread_context_t* g_thread_ctx = NULL;

// Assembly functions that create unusual instruction boundaries
extern void complex_transition_asm(thread_context_t* ctx);
extern void vulnerable_window_asm(thread_context_t* ctx);
extern int deep_call_chain_asm(thread_context_t* ctx, int depth, int value);

/*
 * Assembly implementation with Go-like complexity
 * Creates unusual calling conventions and instruction boundaries
 * that stress CoreCLR's IP boundary analysis
 */
__asm__(
"   .text\n"
"   .globl complex_transition_asm\n"
"complex_transition_asm:\n"
"   # Create complex prologue similar to Go's runtime code\n"
"   pushq %rbp\n"
"   movq  %rsp, %rbp\n"
"   pushq %rbx\n"
"   pushq %r12\n"
"   pushq %r13\n"
"   pushq %r14\n"
"   pushq %r15\n"
"   subq  $128, %rsp        # Large stack frame\n"
"   \n"
"   # Store context in unusual way (like Go's context switching)\n"
"   movq  %rdi, %r12        # Save context pointer\n"
"   movq  %rsp, -8(%rbp)    # Save stack pointer in unusual location\n"
"   \n"
"   # Create complex call pattern with multiple intermediate frames\n"
"   # This mimics Go's complex runtime call chains\n"
"   movq  %r12, %rdi\n"
"   callq vulnerable_window_asm\n"
"   \n"
"   # Create another layer of complexity\n"
"   movq  %r12, %rdi\n"
"   movq  $10, %rsi\n"
"   movq  $42, %rdx\n"
"   callq deep_call_chain_asm\n"
"   \n"
"   # Complex epilogue with atypical cleanup\n"
"   movq  -8(%rbp), %rsp    # Restore stack in unusual way\n"
"   addq  $128, %rsp\n"
"   popq  %r15\n"
"   popq  %r14\n"
"   popq  %r13\n"
"   popq  %r12\n"
"   popq  %rbx\n"
"   leave\n"
"   ret\n"
"\n"
"   .globl vulnerable_window_asm\n"
"vulnerable_window_asm:\n"
"   # This creates the critical window where signals cause expensive analysis\n"
"   pushq %rbp\n"
"   movq  %rsp, %rbp\n"
"   pushq %rbx\n"
"   subq  $64, %rsp\n"
"   \n"
"   # Mark thread as vulnerable (like Go's setg(nil))\n"
"   movq  %rdi, %rbx\n"
"   callq mark_thread_vulnerable\n"
"   \n"
"   # Do complex work while in vulnerable state\n"
"   # This is where signals should arrive to stress IP analysis\n"
"   movq  %rbx, %rdi\n"
"   callq simulate_complex_runtime_work\n"
"   \n"
"   # Multiple nested calls to create complex unwinding scenarios\n"
"   movq  %rbx, %rdi\n"
"   callq create_deep_call_context\n"
"   \n"
"   # Clear vulnerable state\n"
"   movq  %rbx, %rdi\n"
"   callq clear_thread_vulnerable\n"
"   \n"
"   addq  $64, %rsp\n"
"   popq  %rbx\n"
"   leave\n"
"   ret\n"
"\n"
"   .globl deep_call_chain_asm\n"
"deep_call_chain_asm:\n"
"   # Create deep recursive call chain with complex state\n"
"   pushq %rbp\n"
"   movq  %rsp, %rbp\n"
"   pushq %rbx\n"
"   pushq %r12\n"
"   pushq %r13\n"
"   \n"
"   # Check recursion depth\n"
"   cmpq  $0, %rsi\n"
"   jle   .return_value\n"
"   \n"
"   # Save parameters in unusual registers\n"
"   movq  %rdi, %rbx        # context\n"
"   movq  %rsi, %r12        # depth\n"
"   movq  %rdx, %r13        # value\n"
"   \n"
"   # Do complex work at this level\n"
"   movq  %rbx, %rdi\n"
"   callq simulate_complex_runtime_work\n"
"   \n"
"   # Recursive call with modified parameters\n"
"   movq  %rbx, %rdi\n"
"   movq  %r12, %rsi\n"
"   subq  $1, %rsi          # depth - 1\n"
"   movq  %r13, %rdx\n"
"   imulq $3, %rdx          # value * 3\n"
"   addq  $7, %rdx          # value * 3 + 7\n"
"   callq deep_call_chain_asm\n"
"   \n"
"   # Combine result in complex way\n"
"   addq  %r13, %rax        # Add current value\n"
"   jmp   .cleanup\n"
"   \n"
".return_value:\n"
"   movq  %rdx, %rax        # Return value\n"
"   \n"
".cleanup:\n"
"   popq  %r13\n"
"   popq  %r12\n"
"   popq  %rbx\n"
"   leave\n"
"   ret\n"
);

// Initialize thread context (like Go's needm)
static thread_context_t* ensure_thread_context(void) {
    if (g_thread_ctx == NULL) {
        g_thread_ctx = malloc(sizeof(thread_context_t));
        if (g_thread_ctx == NULL) return NULL;

        memset(g_thread_ctx, 0, sizeof(thread_context_t));
        g_thread_ctx->state = STATE_NORMAL;
        g_thread_ctx->context_depth = 0;
        g_thread_ctx->in_critical_section = 0;
        g_thread_ctx->transition_counter = 0;
    }
    return g_thread_ctx;
}

// Simulate Go's entersyscall transition
static void enter_syscall_simulation(thread_context_t* ctx) {
    ctx->state = STATE_ENTERING_SYSCALL;
    __sync_synchronize(); // Memory barrier

    // Create complex state like Go's runtime transitions
    ctx->in_critical_section = 1;
    ctx->transition_counter++;

    // Save context in multiple ways (like Go's complex context handling)
    if (setjmp(ctx->saved_context) == 0) {
        ctx->state = STATE_IN_SYSCALL;
    }

    // Add to stack context tracking
    if (ctx->context_depth < 7) {
        ctx->stack_contexts[ctx->context_depth++] = (void*)&ctx;
    }

    ctx->state = STATE_VULNERABLE; // Critical window
}

// Simulate Go's exitsyscall transition
static void exit_syscall_simulation(thread_context_t* ctx) {
    ctx->state = STATE_EXITING_SYSCALL;
    __sync_synchronize();

    // Another vulnerable window
    ctx->state = STATE_VULNERABLE;

    // Complex cleanup like Go's dropm
    if (ctx->context_depth > 0) {
        ctx->context_depth--;
    }

    // Clear critical section last (like Go's timing)
    ctx->in_critical_section = 0;
    ctx->state = STATE_NORMAL;
}

// C functions called from assembly
void mark_thread_vulnerable(thread_context_t* ctx) {
    ctx->state = STATE_VULNERABLE;
    ctx->in_critical_section = 1;
    __sync_synchronize();
}

void clear_thread_vulnerable(thread_context_t* ctx) {
    ctx->state = STATE_NORMAL;
    ctx->in_critical_section = 0;
    __sync_synchronize();
}

void simulate_complex_runtime_work(thread_context_t* ctx) {
    // Create the expensive work that makes IP analysis costly

    // Much more aggressive complexity to match Go's stress levels
    for (int i = 0; i < 2000; i++) {
        volatile char local_data[1024];
        uintptr_t base = (uintptr_t)local_data;

        // Complex addressing patterns that stress unwinding
        for (int j = 0; j < 16; j++) {
            if (j * sizeof(uint64_t) < sizeof(local_data) - sizeof(uint64_t)) {
                volatile uint64_t* ptr = (volatile uint64_t*)(base + j * sizeof(uint64_t));
                *ptr = (uintptr_t)ctx + i + j;

                // Force some actual computation
                *ptr = (*ptr * 7) ^ (ctx->transition_counter << (j % 16));
            }
        }

        // Create much more frequent state transitions (like Go's rapid enter/exit)
        if (i % 10 == 0) {
            enter_syscall_simulation(ctx);
            exit_syscall_simulation(ctx);

            // Add nested transitions to increase complexity
            ctx->state = STATE_VULNERABLE;
            enter_syscall_simulation(ctx);
            exit_syscall_simulation(ctx);
        }
    }
}

void create_deep_call_context(thread_context_t* ctx) {
    // Create multiple layers of function calls with complex state

    // Much deeper recursive-like structure with more state changes
    for (int depth = 0; depth < 100; depth++) {
        volatile char frame_data[256];

        // Complex frame relationships
        uintptr_t frame_ptr = (uintptr_t)frame_data;
        if (depth < 8) {
            ctx->stack_contexts[depth] = (void*)frame_ptr;
        }

        // Nested state transitions
        runtime_state_t saved_state = ctx->state;
        ctx->state = STATE_VULNERABLE;

        // Do work that requires complex analysis
        for (int work = 0; work < 100; work++) {
            volatile uint64_t computation = 0;
            computation = (frame_ptr + work) * ctx->transition_counter;
            computation ^= (uintptr_t)ctx;

            // Touch memory in patterns that complicate analysis
            if (work % 10 == 0 && (work % 200) < sizeof(frame_data)) {
                volatile char* mem = (volatile char*)(frame_ptr + (work % 200));
                *mem = (char)(computation & 0xFF);
            }
        }

        ctx->state = saved_state;
    }
}

// Main exported function - equivalent to Go's ping()
__attribute__((visibility("default")))
int create_go_like_complexity(void) {
    thread_context_t* ctx = ensure_thread_context();
    if (ctx == NULL) return -1;

    // Simulate the complex work that Go does during cgo calls
    enter_syscall_simulation(ctx);

    // Temporarily disable assembly functions to test basic approach
    // complex_transition_asm(ctx);

    // Create deep call chains that stress unwinding (using C version)
    // int result = deep_call_chain_asm(ctx, 15, 42);
    int result = 42; // Placeholder

    // Use C functions that create complexity
    simulate_complex_runtime_work(ctx);
    create_deep_call_context(ctx);

    // Add even more aggressive patterns
    for (int round = 0; round < 10; round++) {
        enter_syscall_simulation(ctx);
        simulate_complex_runtime_work(ctx);
        exit_syscall_simulation(ctx);
        create_deep_call_context(ctx);
    }

    exit_syscall_simulation(ctx);

    // Some final complex work
    simulate_complex_runtime_work(ctx);

    return result % 1000000; // Bounded result
}

// Include extreme complexity targeting SA_ONSTACK signal handler analysis
extern int create_extreme_signal_analysis_stress(int base_iterations);

// Include atypical calling conventions based on CoreCLR analysis
extern int create_atypical_calling_convention_stress(int base_complexity);

// Alternative entry point with different complexity patterns
__attribute__((visibility("default")))
int create_signal_stress_scenario(int iterations) {
    thread_context_t* ctx = ensure_thread_context();
    if (ctx == NULL) return -1;

    int total = 0;
    for (int i = 0; i < iterations; i++) {
        enter_syscall_simulation(ctx);

        // Create different patterns each iteration - including atypical calling conventions
        switch (i % 4) {
            case 0:
                simulate_complex_runtime_work(ctx);
                break;
            case 1:
                create_deep_call_context(ctx);
                total += i * 42; // Simple computation
                break;
            case 2:
                // Extreme complexity targeting signal handler analysis
                total += create_extreme_signal_analysis_stress(3);
                break;
            case 3:
                // Atypical calling conventions (Go's key characteristic)
                total += create_atypical_calling_convention_stress(5);
                break;
        }

        exit_syscall_simulation(ctx);

        // Occasional yield to allow signals
        if (i % 100 == 0) {
            usleep(1);
        }
    }

    return total % 1000000;
}

// Cleanup function
__attribute__((visibility("default")))
void cleanup_thread_context(void) {
    if (g_thread_ctx != NULL) {
        free(g_thread_ctx);
        g_thread_ctx = NULL;
    }
}