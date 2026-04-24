/*
 * Complex ABI Transition Pattern - Tier 2
 *
 * Replicates Go's PUSH_REGS_HOST_TO_ABI0 pattern with extensive
 * register preservation that stresses IP boundary analysis.
 *
 * Expected Impact: 3-6KB stack consumption
 */

#include "pattern_base.h"

#ifdef ENABLE_COMPLEX_ABI_PATTERN

static __thread int t_is_vulnerable = 0;

void mark_thread_vulnerable(void* ctx) { (void)ctx; t_is_vulnerable = 1; }
void clear_thread_vulnerable(void* ctx) { (void)ctx; t_is_vulnerable = 0; }

// Simulate complex register preservation
static int complex_abi_stress(void* ctx, int depth, uintptr_t data) {
    // Simulate 28 registers being saved (like Go's PUSH_REGS_HOST_TO_ABI0)
    volatile uintptr_t reg_save[28];
    volatile char frame[1536]; // Larger frame for ABI complexity

    if (depth <= 0) return (int)(data & 0xFFFF);

    mark_thread_vulnerable(ctx);

    // Simulate complex register preservation patterns
    for (int i = 0; i < 28; i++) {
        reg_save[i] = data + (uintptr_t)ctx + i * depth * 0x100;
    }

    for (int i = 0; i < 1536; i++) {
        frame[i] = (char)(reg_save[i % 28] & 0xFF);
    }

    volatile uintptr_t computation = 0;
    for (int work = 0; work < depth * 15; work++) {
        computation ^= reg_save[work % 28];
        computation = (computation << 1) ^ (uintptr_t)&frame[work % 1536];

        if (work % 45 == 0 && depth > 1 && depth <= 10) {
            int sub_result = complex_abi_stress(ctx, depth - 1, computation ^ data);
            computation += sub_result;
        }
    }

    clear_thread_vulnerable(ctx);
    return (int)(computation % 1000000);
}

int pattern_create_complexity(int iterations) {
    volatile uintptr_t context = (uintptr_t)&iterations;
    int total = 0;
    for (int i = 0; i < iterations && i < 35; i++) {
        total += complex_abi_stress((void*)&context, i % 8 + 5, context + i * 7);
    }
    return total % 1000000;
}

int pattern_stress_scenario(int iterations) {
    return pattern_create_complexity(iterations);
}

int pattern_extreme_stress(int base_iterations) {
    return pattern_create_complexity(base_iterations * 2);
}

int pattern_calling_convention_stress(int base_complexity) {
    return pattern_create_complexity(base_complexity);
}

void pattern_cleanup_context(void) {
    clear_thread_vulnerable(NULL);
}

static const pattern_info_t complex_abi_info = {
    .name = "complex-abi-transitions",
    .description = "Simulates Go's 28-register preservation patterns from PUSH_REGS_HOST_TO_ABI0",
    .expected_stack_kb = 4,
    .go_equivalent = "PUSH_REGS_HOST_TO_ABI0: saves 28 registers in non-standard sequence"
};

const pattern_info_t* get_pattern_info(void) {
    return &complex_abi_info;
}

#endif // ENABLE_COMPLEX_ABI_PATTERN