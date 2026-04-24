/*
 * Baseline Pattern with Measurement - Control Test
 */

#include "pattern_base.h"
#include "stack_probe.h"

#ifdef ENABLE_BASELINE_MEASURED_PATTERN

stack_measurement_t g_stack_measurement = {0};
static __thread int t_is_vulnerable = 0;

void mark_thread_vulnerable(void* ctx) { (void)ctx; t_is_vulnerable = 1; probe_stack_usage(); }
void clear_thread_vulnerable(void* ctx) { (void)ctx; t_is_vulnerable = 0; }

static int measured_simple_computation(int depth, uintptr_t data) {
    if (depth <= 0) return (int)(data & 0xFFFF);

    probe_stack_usage();

    volatile char frame[256];
    volatile uintptr_t local_state[4];

    mark_thread_vulnerable(&frame);

    for (int i = 0; i < 4; i++) {
        local_state[i] = data + i * depth;
    }

    probe_stack_usage();

    for (int i = 0; i < 256; i++) {
        frame[i] = (char)(local_state[i % 4] & 0xFF);
    }

    volatile uintptr_t result = 0;
    for (int work = 0; work < depth * 3; work++) {
        result ^= (uintptr_t)&frame[work % 256];
        result = (result << 1) ^ local_state[work % 4];

        if (work % 50 == 0) probe_stack_usage();
    }

    if (depth > 1 && depth <= 4) {
        probe_stack_usage();
        result += measured_simple_computation(depth - 1, result ^ data);
        probe_stack_usage();
    }

    clear_thread_vulnerable(&frame);
    return (int)(result % 100000);
}

static void setup_measurement(void) {
    init_stack_measurement();
    install_stack_measurement_handler();
}

int pattern_create_complexity(int iterations) {
    static int initialized = 0;
    if (!initialized) { setup_measurement(); initialized = 1; }

    int total = 0;
    for (int i = 0; i < iterations && i < 50; i++) {
        probe_stack_usage();
        total += measured_simple_computation(i % 4 + 1, (uintptr_t)&total + i);
    }
    return total % 1000000;
}

int pattern_stress_scenario(int iterations) { return pattern_create_complexity(iterations); }
int pattern_extreme_stress(int base_iterations) { return pattern_create_complexity(base_iterations); }
int pattern_calling_convention_stress(int base_complexity) { return pattern_create_complexity(base_complexity); }
void pattern_cleanup_context(void) { clear_thread_vulnerable(NULL); g_stack_measurement.measurement_active = 0; }

__attribute__((visibility("default"))) uintptr_t get_pattern_stack_usage(void) { return get_max_stack_usage(); }
__attribute__((visibility("default"))) int get_pattern_signal_count(void) { return get_signal_count(); }
__attribute__((visibility("default"))) int get_pattern_deep_analysis_count(void) { return get_deep_analysis_count(); }

static const pattern_info_t baseline_measured_info = {
    .name = "baseline-measured",
    .description = "Standard C code with stack usage measurement (control test)",
    .expected_stack_kb = 1,
    .go_equivalent = "N/A - standard C baseline with measurement"
};

const pattern_info_t* get_pattern_info(void) { return &baseline_measured_info; }

#endif