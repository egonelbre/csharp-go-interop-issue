/*
 * Intentional Frame Deception with UNDEF Instructions - Tier 1 Critical
 *
 * Replicates Go's systemstack_switch pattern with undefined instructions.
 * Creates analysis traps that cause CoreCLR's IP boundary analysis to
 * hit undefined instruction sequences, potentially causing infinite loops
 * or deep recursion as algorithms try to understand impossible patterns.
 *
 * Expected Impact: 4-8KB stack consumption before analysis failure
 */

#include "pattern_base.h"

#ifdef ENABLE_UNDEF_PATTERN

static __thread int t_is_vulnerable = 0;

void mark_thread_vulnerable(void* ctx) {
    (void)ctx;
    t_is_vulnerable = 1;
}

void clear_thread_vulnerable(void* ctx) {
    (void)ctx;
    t_is_vulnerable = 0;
}

// Forward declarations
extern void undef_deception_function_1(void* context, uintptr_t param1, uintptr_t param2);
extern void undef_deception_function_2(void* context, uintptr_t param1, uintptr_t param2);
extern int undef_analysis_trap(void* ctx, int depth, uintptr_t data);

/*
 * Assembly functions with intentional undefined instructions
 * Designed to trap analysis algorithms like Go's systemstack_switch
 */
__asm__(
"   .text\n"
"   .globl undef_deception_function_1\n"
"undef_deception_function_1:\n"
"   # Standard entry that leads to deception\n"
"   pushq %rbp\n"
"   movq  %rsp, %rbp\n"
"   subq  $64, %rsp\n"
"   \n"
"   # Store parameters normally\n"
"   movq  %rdi, -8(%rbp)     # context\n"
"   movq  %rsi, -16(%rbp)    # param1\n"
"   movq  %rdx, -24(%rbp)    # param2\n"
"   \n"
"   # Jump over the undefined section\n"
"   jmp   1f\n"
"   \n"
"   # CRITICAL: Undefined instruction sequence (like Go's systemstack_switch)\n"
"   # IP boundary analysis will try to decode these and fail\n"
"   .byte 0x0f, 0x0b         # UD2 instruction (undefined)\n"
"   .byte 0xff, 0xff         # Invalid instruction bytes\n"
"   .byte 0x00, 0x00         # More invalid bytes\n"
"   .byte 0x0f, 0x0b         # Another UD2\n"
"   \n"
"1: # Resume normal execution\n"
"   # Call function that references the undefined region\n"
"   movq  -8(%rbp), %rdi     # context\n"
"   movq  -16(%rbp), %rsi    # param1\n"
"   movq  -24(%rbp), %rdx    # param2\n"
"   callq undef_deception_function_2\n"
"   \n"
"   # Call C function\n"
"   movq  -8(%rbp), %rdi     # context\n"
"   movq  $10, %rsi          # depth\n"
"   movq  -16(%rbp), %rdx    # param1 as data\n"
"   callq undef_analysis_trap\n"
"   \n"
"   addq  $64, %rsp\n"
"   leave\n"
"   ret\n"
"\n"
"   .globl undef_deception_function_2\n"
"undef_deception_function_2:\n"
"   # Function that contains embedded undefined instructions\n"
"   pushq %rbp\n"
"   movq  %rsp, %rbp\n"
"   subq  $48, %rsp\n"
"   \n"
"   # Store parameters\n"
"   movq  %rdi, -8(%rbp)     # context\n"
"   movq  %rsi, -16(%rbp)    # param1\n"
"   movq  %rdx, -24(%rbp)    # param2\n"
"   \n"
"   # Normal execution path\n"
"   jmp   2f\n"
"   \n"
"   # CRITICAL: More undefined instruction patterns\n"
"   # These create 'function' boundaries that confuse analysis\n"
"   .align 8                 # Alignment like Go's PCALIGN\n"
"   .byte 0x0f, 0x0b         # UD2 - undefined instruction\n"
"   .byte 0xc3               # RET - but unreachable\n"
"   .byte 0x0f, 0x0b         # Another UD2\n"
"   .byte 0xff, 0xe0         # JMP %rax - but rax is undefined\n"
"   \n"
"2: # Continuation\n"
"   # Call C function that will be analyzed during signals\n"
"   movq  -8(%rbp), %rdi     # context\n"
"   movq  $8, %rsi           # depth\n"
"   movq  -16(%rbp), %rdx    # param1 as data\n"
"   callq undef_analysis_trap\n"
"   \n"
"   addq  $48, %rsp\n"
"   leave\n"
"   ret\n"
"\n"
"   .globl undef_analysis_trap\n"
"undef_analysis_trap:\n"
"   # Assembly function that creates analysis complexity near undefined regions\n"
"   pushq %rbp\n"
"   movq  %rsp, %rbp\n"
"   subq  $64, %rsp\n"
"   \n"
"   # Store parameters\n"
"   movq  %rdi, -8(%rbp)     # ctx\n"
"   movq  %rsi, -16(%rbp)    # depth\n"
"   movq  %rdx, -24(%rbp)    # data\n"
"   \n"
"   # Create complex frame near undefined region\n"
"   jmp   3f\n"
"   \n"
"   # CRITICAL: Undefined region that looks like valid function boundary\n"
"   .align 16                # Force alignment\n"
"   .byte 0x55               # PUSH %rbp (looks like prologue start)\n"
"   .byte 0x48, 0x89, 0xe5   # MOV %rsp, %rbp (looks like prologue)\n"
"   .byte 0x0f, 0x0b         # UD2 - undefined (analysis trap)\n"
"   .byte 0x48, 0x89, 0xec   # MOV %rbp, %rsp (looks like epilogue)\n"
"   .byte 0x5d               # POP %rbp (looks like epilogue)\n"
"   .byte 0x0f, 0x0b         # UD2 - undefined (analysis trap)\n"
"   \n"
"3: # Normal execution continues\n"
"   # Perform some register manipulation\n"
"   movq  -16(%rbp), %rax    # depth\n"
"   imulq $11, %rax          # multiply\n"
"   addq  -24(%rbp), %rax    # add data\n"
"   movq  %rax, -32(%rbp)    # store result\n"
"   \n"
"   # Call C function\n"
"   movq  -8(%rbp), %rdi     # ctx\n"
"   movq  -16(%rbp), %rsi    # depth\n"
"   movq  -32(%rbp), %rdx    # modified data\n"
"   callq undef_analysis_trap_c\n"
"   \n"
"   addq  $64, %rsp\n"
"   leave\n"
"   ret\n"
);

// C function that will be analyzed when signals hit near undefined regions
int undef_analysis_trap_c(void* ctx, int depth, uintptr_t data) {
    // This function executes normally but its instruction pointer context
    // is near undefined instruction sequences that will trap IP analysis
    volatile char frame[512];
    volatile uintptr_t trap_state[6];

    if (depth <= 0) return (int)(data & 0xFFFF);

    mark_thread_vulnerable(ctx);

    // Set up state that will be analyzed during signal handling
    for (int i = 0; i < 6; i++) {
        trap_state[i] = data + (uintptr_t)ctx + i * depth;
    }

    // Create frame complexity that requires analysis
    for (int i = 0; i < 512; i++) {
        frame[i] = (char)(trap_state[i % 6] & 0xFF);
    }

    // Computation that keeps us in this function longer (more signal opportunities)
    volatile uintptr_t computation = 0;
    for (int work = 0; work < depth * 7; work++) {
        computation ^= (uintptr_t)&frame[work % 512];
        computation = (computation << 1) ^ trap_state[work % 6];

        // Recursive call near undefined instruction context
        if (work % 25 == 0 && depth > 1 && depth <= 8) {
            int sub_result = undef_analysis_trap_c(ctx, depth - 1, computation ^ data);
            computation += sub_result;
        }
    }

    clear_thread_vulnerable(ctx);
    return (int)(computation % 1000000);
}

// Pattern implementations using undefined instruction deception
int pattern_create_complexity(int iterations) {
    volatile uintptr_t context_base = (uintptr_t)&iterations;
    int total_result = 0;

    for (int round = 0; round < iterations && round < 25; round++) {
        // Call functions with embedded undefined instructions
        undef_deception_function_1(
            (void*)&context_base,
            context_base + round,
            context_base + round * 2
        );

        // Additional complexity near undefined regions
        int trap_result = undef_analysis_trap_c(
            (void*)&context_base,
            round % 6 + 3,
            context_base + round * 5
        );

        total_result = (total_result + trap_result) % 1000000;
    }

    return total_result;
}

int pattern_stress_scenario(int iterations) {
    volatile uintptr_t context = (uintptr_t)&iterations;
    int total = 0;

    for (int i = 0; i < iterations && i < 80; i++) {
        // Multiple calls to undefined deception functions
        undef_deception_function_1((void*)&context, context + i, context + i * 3);

        if (i % 8 == 0) {
            total += undef_analysis_trap_c((void*)&context, i % 5 + 2, context + i * 7);
        }
    }

    return total % 1000000;
}

int pattern_extreme_stress(int base_iterations) {
    volatile uintptr_t context = (uintptr_t)&base_iterations;
    int total = 0;

    for (int round = 0; round < base_iterations && round < 15; round++) {
        // Multiple undefined deception calls per round
        undef_deception_function_1((void*)&context, context + round * 11, context + round * 13);
        undef_deception_function_2((void*)&context, context + round * 17, context + round * 19);

        total += undef_analysis_trap_c((void*)&context, round % 7 + 4, context + round * 23);

        if (round % 3 == 0) {
            total += undef_analysis_trap_c((void*)&context, round % 4 + 3, context + round * 29);
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

static const pattern_info_t undef_info = {
    .name = "undef-instruction-deception",
    .description = "Embedded undefined instructions that trap IP boundary analysis like Go's systemstack_switch",
    .expected_stack_kb = 6, // 4-8KB expected
    .go_equivalent = "systemstack_switch: PCALIGN $8; UNDEF; CALL runtime·abort(SB)"
};

const pattern_info_t* get_pattern_info(void) {
    return &undef_info;
}

#endif // ENABLE_UNDEF_PATTERN