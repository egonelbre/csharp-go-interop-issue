/*
 * Stack Switching Pattern - Tier 2
 *
 * Simulates Go's mid-function stack switching like asmcgocall.
 * Expected Impact: 2-4KB stack consumption
 */

#include "pattern_base.h"

#ifdef ENABLE_STACK_SWITCHING_PATTERN

static __thread int t_is_vulnerable = 0;

void mark_thread_vulnerable(void* ctx) { (void)ctx; t_is_vulnerable = 1; }
void clear_thread_vulnerable(void* ctx) { (void)ctx; t_is_vulnerable = 0; }

static int stack_switch_stress(void* ctx, int depth, uintptr_t data) {
    volatile uintptr_t stack_contexts[8];
    volatile char frame[1024];

    if (depth <= 0) return (int)(data & 0xFFFF);

    mark_thread_vulnerable(ctx);

    // Simulate stack context switching
    for (int i = 0; i < 8; i++) {
        stack_contexts[i] = data + (uintptr_t)ctx + i * depth;
    }

    for (int i = 0; i < 1024; i++) {
        frame[i] = (char)(stack_contexts[i % 8] & 0xFF);
    }

    volatile uintptr_t computation = 0;
    for (int work = 0; work < depth * 10; work++) {
        computation ^= stack_contexts[work % 8];

        if (work % 40 == 0 && depth > 1 && depth <= 8) {
            int sub_result = stack_switch_stress(ctx, depth - 1, computation ^ data);
            computation += sub_result;
        }
    }

    clear_thread_vulnerable(ctx);
    return (int)(computation % 1000000);
}

int pattern_create_complexity(int iterations) {
    volatile uintptr_t context = (uintptr_t)&iterations;
    int total = 0;
    for (int i = 0; i < iterations && i < 30; i++) {
        total += stack_switch_stress((void*)&context, i % 6 + 3, context + i * 5);
    }
    return total % 1000000;
}

int pattern_stress_scenario(int iterations) { return pattern_create_complexity(iterations); }
int pattern_extreme_stress(int base_iterations) { return pattern_create_complexity(base_iterations * 2); }
int pattern_calling_convention_stress(int base_complexity) { return pattern_create_complexity(base_complexity); }
void pattern_cleanup_context(void) { clear_thread_vulnerable(NULL); }

static const pattern_info_t stack_switch_info = {
    .name = "stack-switching",
    .description = "Simulates Go's mid-function stack switching like asmcgocall",
    .expected_stack_kb = 3,
    .go_equivalent = "asmcgocall: mid-function stack pointer manipulation"
};

const pattern_info_t* get_pattern_info(void) { return &stack_switch_info; }

#endif