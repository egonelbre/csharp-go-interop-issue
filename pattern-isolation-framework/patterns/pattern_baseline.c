/*
 * Baseline Simple Pattern - Control Test
 *
 * Standard C code with conventional calling patterns.
 * Should NOT cause CoreCLR IP boundary analysis stress.
 * Used as control to verify framework is working correctly.
 */

#include "pattern_base.h"

#ifdef ENABLE_BASELINE_SIMPLE_PATTERN

static __thread int t_is_vulnerable = 0;

void mark_thread_vulnerable(void* ctx) {
    (void)ctx;
    t_is_vulnerable = 1;
}

void clear_thread_vulnerable(void* ctx) {
    (void)ctx;
    t_is_vulnerable = 0;
}

// Standard C function with normal calling convention
static int simple_computation(int depth, uintptr_t data) {
    if (depth <= 0) return (int)(data & 0xFFFF);

    volatile char frame[256];  // Small, predictable frame
    volatile uintptr_t local_state[4];  // Minimal state

    for (int i = 0; i < 4; i++) {
        local_state[i] = data + i * depth;
    }

    for (int i = 0; i < 256; i++) {
        frame[i] = (char)(local_state[i % 4] & 0xFF);
    }

    // Simple computation with minimal recursion
    volatile uintptr_t result = 0;
    for (int work = 0; work < depth * 3; work++) {
        result ^= (uintptr_t)&frame[work % 256];
        result = (result << 1) ^ local_state[work % 4];
    }

    // Very limited recursion to keep stack usage minimal
    if (depth > 1 && depth <= 4) {
        result += simple_computation(depth - 1, result ^ data);
    }

    return (int)(result % 100000);
}

// Pattern implementations using standard C only
int pattern_create_complexity(int iterations) {
    int total = 0;
    for (int i = 0; i < iterations && i < 50; i++) {
        total += simple_computation(i % 4 + 1, (uintptr_t)&iterations + i);
    }
    return total % 1000000;
}

int pattern_stress_scenario(int iterations) {
    volatile uintptr_t context = (uintptr_t)&iterations;
    int total = 0;

    for (int i = 0; i < iterations && i < 100; i++) {
        mark_thread_vulnerable((void*)&context);
        total += simple_computation(i % 3 + 2, context + i * 7);
        clear_thread_vulnerable((void*)&context);
    }

    return total % 1000000;
}

int pattern_extreme_stress(int base_iterations) {
    int total = 0;
    volatile uintptr_t context = (uintptr_t)&base_iterations;

    for (int round = 0; round < base_iterations && round < 20; round++) {
        total += simple_computation(round % 4 + 2, context + round * 11);

        if (round % 3 == 0) {
            mark_thread_vulnerable((void*)&context);
            total += simple_computation(round % 3 + 1, context + round * 13);
            clear_thread_vulnerable((void*)&context);
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

static const pattern_info_t baseline_info = {
    .name = "baseline-simple",
    .description = "Standard C code with conventional calling patterns (control test)",
    .expected_stack_kb = 1,
    .go_equivalent = "N/A - standard C baseline"
};

const pattern_info_t* get_pattern_info(void) {
    return &baseline_info;
}

#endif // ENABLE_BASELINE_SIMPLE_PATTERN