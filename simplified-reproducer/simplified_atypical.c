/*
 * Simplified Atypical Calling Convention Module
 *
 * Minimal version that triggers CoreCLR sigaltstack overflow:
 * - Assembly trampolines with non-standard prologues/epilogues
 * - Atypical register usage that confuses IP boundary analysis
 * - Controlled complexity that causes CoreCLR's IP boundary analysis to overflow the 16KB sigaltstack
 */

#define _GNU_SOURCE
#include <stdint.h>

// Forward declarations for simplified assembly functions
extern void simple_atypical_function_1(void* context, uintptr_t param1, uintptr_t param2);
extern void simple_atypical_function_2(void* context, uintptr_t param1, uintptr_t param2);
extern int simple_register_chain(void* ctx, int depth, uintptr_t data);

/*
 * Simplified assembly functions with essential atypical characteristics
 * These create the IP boundary analysis stress without excessive complexity
 */
__asm__(
"   .text\n"
"   .globl simple_atypical_function_1\n"
"simple_atypical_function_1:\n"
"   # Atypical prologue - non-standard register save order\n"
"   pushq %rbp\n"
"   movq  %rsp, %rbp\n"
"   # Non-standard register preservation\n"
"   pushq %rax\n"
"   pushq %rbx\n"
"   pushq %rcx\n"
"   pushq %rdx\n"
"   pushq %r12\n"
"   pushq %r13\n"
"   # Moderate stack frame (not excessive)\n"
"   subq  $1024, %rsp\n"
"   \n"
"   # Store parameters in non-standard locations\n"
"   movq  %rdi, -8(%rbp)     # context\n"
"   movq  %rsi, -16(%rbp)    # param1\n"
"   movq  %rdx, -24(%rbp)    # param2\n"
"   \n"
"   # Atypical register usage\n"
"   movq  %rsi, %r12         # param1 → r12 (non-standard)\n"
"   movq  %rdx, %r13         # param2 → r13 (non-standard)\n"
"   \n"
"   # Call with atypical calling convention\n"
"   movq  %rdi, %rax         # context in RAX (atypical)\n"
"   movq  %r12, %rbx         # param1 in RBX (atypical)\n"
"   movq  %r13, %rcx         # param2 in RCX\n"
"   callq simple_atypical_function_2\n"
"   \n"
"   # Call C function with complex parameters\n"
"   movq  -8(%rbp), %rdi     # context\n"
"   movq  $10, %rsi          # controlled depth\n"
"   movq  %r12, %rdx         # param1 as data\n"
"   callq simple_register_chain\n"
"   \n"
"   # Atypical epilogue - non-standard register restore order\n"
"   addq  $1024, %rsp\n"
"   popq  %r13\n"
"   popq  %r12\n"
"   popq  %rdx\n"
"   popq  %rcx\n"
"   popq  %rbx\n"
"   popq  %rax\n"
"   leave\n"
"   ret\n"
"\n"
"   .globl simple_atypical_function_2\n"
"simple_atypical_function_2:\n"
"   # Different atypical pattern - rbp setup after pushes\n"
"   pushq %rbp\n"
"   pushq %rbx            # Non-standard order\n"
"   pushq %r12\n"
"   movq  %rsp, %rbp      # rbp setup after pushes (atypical)\n"
"   \n"
"   # Moderate stack allocation\n"
"   subq  $2048, %rsp\n"
"   \n"
"   # Parameters in atypical registers from caller\n"
"   movq  %rax, %r12      # context from RAX (atypical)\n"
"   movq  %rbx, -8(%rbp)  # param1 from RBX (atypical)\n"
"   movq  %rcx, -16(%rbp) # param2 from RCX\n"
"   \n"
"   # Create some frame complexity\n"
"   movq  %rsp, -24(%rbp) # Store SP in unusual location\n"
"   \n"
"   # Call C function with modified convention\n"
"   movq  %r12, %rdi      # context\n"
"   movq  $15, %rsi       # controlled depth\n"
"   movq  -8(%rbp), %rdx  # param1 as data\n"
"   callq simple_register_chain\n"
"   \n"
"   # Simple cleanup\n"
"   addq  $2048, %rsp\n"
"   popq  %r12\n"
"   popq  %rbx\n"
"   leave\n"
"   ret\n"
"\n"
"   .globl simple_register_chain\n"
"simple_register_chain:\n"
"   # Assembly bridge - controlled complexity for IP boundary stress\n"
"   pushq %rbp\n"
"   movq  %rsp, %rbp\n"
"   subq  $128, %rsp     # Moderate local area\n"
"   \n"
"   # Store parameters\n"
"   movq  %rdi, -8(%rbp)  # ctx\n"
"   movq  %rsi, -16(%rbp) # depth\n"
"   movq  %rdx, -24(%rbp) # data\n"
"   \n"
"   # Some register manipulation\n"
"   movq  %rsi, %rax\n"
"   imulq $7, %rax\n"
"   addq  %rdx, %rax\n"
"   movq  %rax, -32(%rbp)\n"
"   \n"
"   # Call C function\n"
"   movq  %rdi, %rdi      # ctx unchanged\n"
"   movq  %rsi, %rsi      # depth unchanged\n"
"   movq  -32(%rbp), %rdx # modified data\n"
"   callq simple_register_chain_c\n"
"   \n"
"   addq  $128, %rsp\n"
"   leave\n"
"   ret\n"
);

// External function declarations
extern void mark_thread_vulnerable(void* ctx);
extern void clear_thread_vulnerable(void* ctx);

// Simplified C function with controlled complexity
int simple_register_chain_c(void* ctx, int depth, uintptr_t data) {
    // Controlled local state
    volatile char controlled_frame[512];
    volatile uintptr_t reg_state[8];  // Reduced from 16

    if (depth <= 0) return (int)(data & 0xFFFF);

    mark_thread_vulnerable(ctx);

    // Controlled state setup
    for (int i = 0; i < 8; i++) {
        reg_state[i] = data + (uintptr_t)ctx + i * depth;
    }

    // Controlled complexity loop
    for (int i = 0; i < 512; i++) {
        controlled_frame[i] = (char)(reg_state[i % 8] & 0xFF);
    }

    // Controlled computation
    volatile uintptr_t computation = 0;
    for (int work = 0; work < depth * 5; work++) { // Reduced multiplier
        computation ^= (uintptr_t)&controlled_frame[work % 512];
        computation = (computation << 1) ^ reg_state[work % 8];

        // Controlled recursive call
        if (work % 50 == 0 && depth > 1 && depth <= 8) { // Limited depth
            int sub_result = simple_register_chain_c(ctx, depth - 1, computation ^ data);
            computation += sub_result;
        }
    }

    clear_thread_vulnerable(ctx);
    return (int)(computation % 1000000);
}

// Main simplified function
__attribute__((visibility("default")))
int create_simplified_atypical_stress(int base_complexity) {
    volatile uintptr_t context_base = (uintptr_t)&base_complexity;
    int total_result = 0;

    for (int round = 0; round < base_complexity; round++) {
        // Call the simplified atypical assembly functions
        simple_atypical_function_1(
            (void*)&context_base,
            context_base + round,
            context_base + round * 2
        );

        // Additional controlled complexity
        int chain_result = simple_register_chain_c(
            (void*)&context_base,
            round % 6 + 3,  // Controlled depth: 3-8
            context_base + round * 5
        );

        total_result = (total_result + chain_result) % 1000000;
    }

    return total_result;
}