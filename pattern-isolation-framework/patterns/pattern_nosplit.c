/*
 * NOSPLIT/NOFRAME Pattern - Tier 3
 *
 * Simulates Go's NOSPLIT|NOFRAME attributes creating unusual frame patterns.
 * Expected Impact: 1-3KB stack consumption
 */

#include "pattern_base.h"

#ifdef ENABLE_NOSPLIT_PATTERN

static __thread int t_is_vulnerable = 0;

void mark_thread_vulnerable(void* ctx) { (void)ctx; t_is_vulnerable = 1; }
void clear_thread_vulnerable(void* ctx) { (void)ctx; t_is_vulnerable = 0; }

static int nosplit_stress(void* ctx, int depth, uintptr_t data) {
    volatile uintptr_t minimal_state[4]; // Very small like NOFRAME

    if (depth <= 0) return (int)(data & 0xFFFF);

    mark_thread_vulnerable(ctx);

    // Minimal frame simulation
    for (int i = 0; i < 4; i++) {
        minimal_state[i] = data + (uintptr_t)ctx + i * depth;
    }

    volatile uintptr_t computation = 0;
    for (int work = 0; work < depth * 8; work++) {
        computation ^= minimal_state[work % 4];

        if (work % 50 == 0 && depth > 1 && depth <= 6) {
            int sub_result = nosplit_stress(ctx, depth - 1, computation ^ data);
            computation += sub_result;
        }
    }

    clear_thread_vulnerable(ctx);
    return (int)(computation % 1000000);
}

int pattern_create_complexity(int iterations) {
    volatile uintptr_t context = (uintptr_t)&iterations;
    int total = 0;
    for (int i = 0; i < iterations && i < 25; i++) {
        total += nosplit_stress((void*)&context, i % 5 + 2, context + i * 3);
    }
    return total % 1000000;
}

int pattern_stress_scenario(int iterations) { return pattern_create_complexity(iterations); }
int pattern_extreme_stress(int base_iterations) { return pattern_create_complexity(base_iterations * 2); }
int pattern_calling_convention_stress(int base_complexity) { return pattern_create_complexity(base_complexity); }
void pattern_cleanup_context(void) { clear_thread_vulnerable(NULL); }

static const pattern_info_t nosplit_info = {
    .name = "nosplit-noframe",
    .description = "Simulates Go's NOSPLIT|NOFRAME attributes with minimal frame complexity",
    .expected_stack_kb = 2,
    .go_equivalent = "NOSPLIT|NOFRAME: zero frame functions with complex register usage"
};

const pattern_info_t* get_pattern_info(void) { return &nosplit_info; }

#endif