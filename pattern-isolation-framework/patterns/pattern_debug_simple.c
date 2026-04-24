/*
 * Debug Pattern - Minimal Implementation to Test for Bugs
 */

#include "pattern_base.h"
#include "stack_probe.h"

#ifdef ENABLE_DEBUG_SIMPLE_PATTERN

stack_measurement_t g_stack_measurement = {0};
static __thread int t_is_vulnerable = 0;

void mark_thread_vulnerable(void* ctx) { (void)ctx; t_is_vulnerable = 1; }
void clear_thread_vulnerable(void* ctx) { (void)ctx; t_is_vulnerable = 0; }

static void setup_measurement(void) {
    init_stack_measurement();
    // Skip signal handler installation to avoid interference
    // install_stack_measurement_handler();
}

int pattern_create_complexity(int iterations) {
    static int initialized = 0;
    if (!initialized) { setup_measurement(); initialized = 1; }

    printf("[debug] Starting pattern_create_complexity with %d iterations\n", iterations);

    int total = 0;
    for (int i = 0; i < iterations && i < 10; i++) { // Very limited iterations
        printf("[debug] Iteration %d\n", i);

        // Minimal computation - no recursion, no complex loops
        volatile int simple_work = i * 7;
        total += simple_work % 1000;

        // Single probe call
        probe_stack_usage();

        printf("[debug] Completed iteration %d, total=%d\n", i, total);
    }

    printf("[debug] Finished pattern_create_complexity, total=%d\n", total);
    return total;
}

int pattern_stress_scenario(int iterations) {
    printf("[debug] pattern_stress_scenario called with %d\n", iterations);
    return pattern_create_complexity(iterations);
}

int pattern_extreme_stress(int base_iterations) {
    printf("[debug] pattern_extreme_stress called with %d\n", base_iterations);
    return pattern_create_complexity(base_iterations);
}

int pattern_calling_convention_stress(int base_complexity) {
    printf("[debug] pattern_calling_convention_stress called with %d\n", base_complexity);
    return pattern_create_complexity(base_complexity);
}

void pattern_cleanup_context(void) {
    printf("[debug] pattern_cleanup_context called\n");
    clear_thread_vulnerable(NULL);
    g_stack_measurement.measurement_active = 0;
}

__attribute__((visibility("default"))) uintptr_t get_pattern_stack_usage(void) { return get_max_stack_usage(); }
__attribute__((visibility("default"))) int get_pattern_signal_count(void) { return get_signal_count(); }
__attribute__((visibility("default"))) int get_pattern_deep_analysis_count(void) { return get_deep_analysis_count(); }

static const pattern_info_t debug_simple_info = {
    .name = "debug-simple",
    .description = "Minimal debug pattern to isolate timeout issues",
    .expected_stack_kb = 1,
    .go_equivalent = "Debug pattern for bug isolation"
};

const pattern_info_t* get_pattern_info(void) { return &debug_simple_info; }

#endif