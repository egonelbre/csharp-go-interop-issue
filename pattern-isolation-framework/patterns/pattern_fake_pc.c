/*
 * Fake PC Manipulation Pattern - Tier 1 Critical
 *
 * Replicates Go's gosave_systemstack_switch fake PC manipulation.
 * Stores artificial instruction pointer addresses that don't correspond
 * to actual call sites, causing CoreCLR's IsIPInProlog to analyze
 * invalid/non-existent instruction sequences.
 *
 * Expected Impact: 8-16KB stack consumption in IP boundary analysis
 */

#include "pattern_base.h"

#ifdef ENABLE_FAKE_PC_PATTERN

static __thread int t_is_vulnerable = 0;
static __thread void* t_fake_pc_context = NULL;

void mark_thread_vulnerable(void* ctx) {
    t_is_vulnerable = 1;
    t_fake_pc_context = ctx;
}

void clear_thread_vulnerable(void* ctx) {
    (void)ctx;
    t_is_vulnerable = 0;
    t_fake_pc_context = NULL;
}

// Forward declarations for assembly functions
extern void fake_pc_trampoline_1(void* context, uintptr_t param1, uintptr_t param2);
extern void fake_pc_trampoline_2(void* context, uintptr_t param1, uintptr_t param2);
extern int fake_pc_save_context(void* ctx, int depth, uintptr_t data);

/*
 * Assembly functions that replicate Go's fake PC manipulation patterns
 * Key feature: Store fake/artificial program counter addresses in contexts
 */
__asm__(
"   .text\n"
"   .globl fake_pc_trampoline_1\n"
"fake_pc_trampoline_1:\n"
"   # Standard prologue\n"
"   pushq %rbp\n"
"   movq  %rsp, %rbp\n"
"   subq  $64, %rsp\n"
"   \n"
"   # Store parameters\n"
"   movq  %rdi, -8(%rbp)     # context\n"
"   movq  %rsi, -16(%rbp)    # param1\n"
"   movq  %rdx, -24(%rbp)    # param2\n"
"   \n"
"   # CRITICAL: Store fake PC address (like Go's gosave_systemstack_switch)\n"
"   # This creates artificial instruction pointer that CoreCLR will try to analyze\n"
"   leaq  fake_pc_trampoline_2(%rip), %rax      # Fake PC: start of function\n"
"   addq  $8, %rax                              # Offset like Go's +8\n"
"   movq  %rax, -32(%rbp)                       # Store fake PC in local context\n"
"   \n"
"   # Also store fake PC in 'goroutine-like' context structure\n"
"   movq  -8(%rbp), %rbx     # Load context pointer\n"
"   movq  %rax, 0(%rbx)      # Store fake PC at context[0] (simulate g.sched.pc)\n"
"   \n"
"   # Create fake stack pointer context too\n"
"   leaq  -40(%rbp), %rcx    # Fake stack pointer\n"
"   movq  %rcx, 8(%rbx)      # Store fake SP at context[1] (simulate g.sched.sp)\n"
"   \n"
"   # Call second trampoline with fake context\n"
"   movq  -8(%rbp), %rdi     # context\n"
"   movq  -16(%rbp), %rsi    # param1\n"
"   movq  -24(%rbp), %rdx    # param2\n"
"   callq fake_pc_trampoline_2@PLT\n"
"   \n"
"   # Call C function with fake PC context\n"
"   movq  -8(%rbp), %rdi     # context with fake PC\n"
"   movq  $12, %rsi          # depth\n"
"   movq  -16(%rbp), %rdx    # param1 as data\n"
"   callq fake_pc_save_context@PLT\n"
"   \n"
"   addq  $64, %rsp\n"
"   leave\n"
"   ret\n"
"\n"
"   .globl fake_pc_trampoline_2\n"
"fake_pc_trampoline_2:\n"
"   # Another fake PC manipulation pattern\n"
"   pushq %rbp\n"
"   movq  %rsp, %rbp\n"
"   subq  $48, %rsp\n"
"   \n"
"   # Store fake return address (like Go's systemstack_switch)\n"
"   leaq  fake_pc_save_context(%rip), %rax     # Fake PC: function start\n"
"   addq  $16, %rax                             # Offset mid-function\n"
"   movq  %rax, -8(%rbp)                       # Store fake return PC\n"
"   \n"
"   # Manipulate context to contain fake instruction pointers\n"
"   movq  %rdi, %rbx         # context\n"
"   movq  -8(%rbp), %rcx     # fake PC\n"
"   movq  %rcx, 16(%rbx)     # Store another fake PC at context[2]\n"
"   \n"
"   # Store fake base pointer too\n"
"   leaq  -24(%rbp), %rdx    # Fake frame pointer\n"
"   movq  %rdx, 24(%rbx)     # Store fake BP at context[3]\n"
"   \n"
"   # Call C function that will see these fake PCs\n"
"   movq  %rdi, %rdi         # context (unchanged)\n"
"   movq  $8, %rsi           # depth\n"
"   movq  %rsi, %rdx         # param1 as data\n"
"   callq fake_pc_save_context@PLT\n"
"   \n"
"   addq  $48, %rsp\n"
"   leave\n"
"   ret\n"
"\n"
"   .globl fake_pc_save_context\n"
"fake_pc_save_context:\n"
"   # Assembly portion that manipulates fake context\n"
"   pushq %rbp\n"
"   movq  %rsp, %rbp\n"
"   subq  $32, %rsp\n"
"   \n"
"   # Store fake PC in yet another location\n"
"   leaq  1f(%rip), %rax     # Point to arbitrary label\n"
"   movq  %rax, -8(%rbp)     # Store fake PC\n"
"   \n"
"   # Store this fake PC in the context too\n"
"   movq  %rdi, %rbx         # context\n"
"   movq  -8(%rbp), %rcx     # fake PC\n"
"   movq  %rcx, 32(%rbx)     # Store at context[4]\n"
"   \n"
"   # Call C function\n"
"   callq fake_pc_save_context_c@PLT\n"
"   \n"
"1: # Arbitrary label that fake PC points to\n"
"   addq  $32, %rsp\n"
"   leave\n"
"   ret\n"
);

// C function that processes the fake PC context
int fake_pc_save_context_c(void* ctx, int depth, uintptr_t data) {
    // This context now contains multiple fake PCs that will confuse
    // CoreCLR's IP boundary analysis when signals arrive
    volatile char frame[512];
    volatile uintptr_t fake_pc_array[8];

    if (depth <= 0) return (int)(data & 0xFFFF);

    mark_thread_vulnerable(ctx);

    // Extract the fake PCs from context (they're stored at context[0], context[2], context[4])
    uintptr_t* context_array = (uintptr_t*)ctx;
    for (int i = 0; i < 8; i++) {
        // Mix real data with fake PC addresses
        fake_pc_array[i] = (i % 2 == 0) ? context_array[i % 5] : data + i * depth;
    }

    // Process with fake PC context that will stress IP analysis
    for (int i = 0; i < 512; i++) {
        frame[i] = (char)(fake_pc_array[i % 8] & 0xFF);
    }

    volatile uintptr_t computation = 0;
    for (int work = 0; work < depth * 8; work++) { // More work due to fake PC complexity
        computation ^= fake_pc_array[work % 8];
        computation = (computation << 1) ^ (uintptr_t)&frame[work % 512];

        // Recursive call with fake PC context
        if (work % 30 == 0 && depth > 1 && depth <= 10) {
            int sub_result = fake_pc_save_context_c(ctx, depth - 1, computation ^ data);
            computation += sub_result;
        }
    }

    clear_thread_vulnerable(ctx);
    return (int)(computation % 1000000);
}

// Pattern implementations using fake PC manipulation
int pattern_create_complexity(int iterations) {
    uintptr_t fake_context[8] = {0}; // Will be filled with fake PCs
    int total_result = 0;

    for (int round = 0; round < iterations && round < 30; round++) {
        // Call fake PC trampoline which stores artificial instruction pointers
        fake_pc_trampoline_1(
            (void*)fake_context,
            (uintptr_t)&total_result + round,
            (uintptr_t)&iterations + round * 2
        );

        // Additional complexity with fake PC context
        int chain_result = fake_pc_save_context_c(
            (void*)fake_context,
            round % 8 + 4,
            (uintptr_t)&total_result + round * 7
        );

        total_result = (total_result + chain_result) % 1000000;
    }

    return total_result;
}

int pattern_stress_scenario(int iterations) {
    uintptr_t fake_context[8] = {0};
    int total = 0;

    for (int i = 0; i < iterations && i < 100; i++) {
        fake_pc_trampoline_1((void*)fake_context, (uintptr_t)&total + i, (uintptr_t)&iterations + i * 3);

        if (i % 10 == 0) {
            total += fake_pc_save_context_c((void*)fake_context, i % 6 + 3, (uintptr_t)&total + i * 5);
        }
    }

    return total % 1000000;
}

int pattern_extreme_stress(int base_iterations) {
    uintptr_t fake_context[8] = {0};
    int total = 0;

    for (int round = 0; round < base_iterations && round < 20; round++) {
        // Multiple fake PC manipulations per round
        fake_pc_trampoline_1((void*)fake_context, (uintptr_t)&total + round * 11, (uintptr_t)&base_iterations + round * 13);
        total += fake_pc_save_context_c((void*)fake_context, round % 7 + 5, (uintptr_t)&total + round * 17);

        if (round % 3 == 0) {
            fake_pc_trampoline_2((void*)fake_context, (uintptr_t)&total + round * 19, (uintptr_t)&base_iterations + round * 23);
            total += fake_pc_save_context_c((void*)fake_context, round % 5 + 4, (uintptr_t)&total + round * 29);
        }
    }

    return total % 1000000;
}

int pattern_calling_convention_stress(int base_complexity) {
    return pattern_create_complexity(base_complexity);
}

void pattern_cleanup_context(void) {
    clear_thread_vulnerable(NULL);
}

static const pattern_info_t fake_pc_info = {
    .name = "fake-pc-manipulation",
    .description = "Stores artificial instruction pointer addresses like Go's gosave_systemstack_switch",
    .expected_stack_kb = 12, // 8-16KB expected
    .go_equivalent = "gosave_systemstack_switch: MOVQ $runtime·systemstack_switch+8(SB), R9; MOVQ R9, (g_sched+gobuf_pc)(R14)"
};

const pattern_info_t* get_pattern_info(void) {
    return &fake_pc_info;
}

#endif // ENABLE_FAKE_PC_PATTERN