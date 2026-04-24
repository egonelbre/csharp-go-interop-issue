/*
 * Fake PC Pattern with Stack Usage Measurement
 *
 * Enhanced version that measures actual stack consumption during
 * signal handling to quantify IP boundary analysis impact.
 */

#include "pattern_base.h"
#include "stack_probe.h"

#ifdef ENABLE_FAKE_PC_MEASURED_PATTERN

// Global measurement context
stack_measurement_t g_stack_measurement = {0};

static __thread int t_is_vulnerable = 0;

void mark_thread_vulnerable(void* ctx) {
    (void)ctx;
    t_is_vulnerable = 1;
    // Probe stack usage when entering vulnerable state
    probe_stack_usage();
}

void clear_thread_vulnerable(void* ctx) {
    (void)ctx;
    t_is_vulnerable = 0;
}

// Enhanced fake PC function with measurement probes
static int measured_fake_pc_stress(void* ctx, int depth, uintptr_t data) {
    volatile char complex_frame[1024];
    volatile uintptr_t fake_pc_state[16];

    if (depth <= 0) return (int)(data & 0xFFFF);

    // Probe at function entry
    probe_stack_usage();

    mark_thread_vulnerable(ctx);

    // Set up fake PC contexts
    uintptr_t* context_pcs = (uintptr_t*)ctx;
    for (int i = 0; i < 16; i++) {
        if (i < 8) {
            fake_pc_state[i] = context_pcs[i % 5];
        } else {
            fake_pc_state[i] = (uintptr_t)&measured_fake_pc_stress + i * 0x100;
        }
    }

    // Probe during setup
    probe_stack_usage();

    for (int i = 0; i < 1024; i++) {
        complex_frame[i] = (char)(fake_pc_state[i % 16] & 0xFF);
    }

    volatile uintptr_t computation = 0;
    for (int work = 0; work < depth * 20; work++) {
        computation ^= fake_pc_state[work % 16];
        computation = (computation << 1) ^ (uintptr_t)&complex_frame[work % 1024];

        // Probe during intensive computation (where signals likely hit)
        if (work % 100 == 0) {
            probe_stack_usage();
        }

        // Create more fake PC contexts
        if (work % 50 == 0) {
            context_pcs[work % 8] = (uintptr_t)&measured_fake_pc_stress + work * 0x10;
            probe_stack_usage();  // Measure after context manipulation
        }

        if (work % 40 == 0 && depth > 1 && depth <= 12) {
            // Probe before recursion
            probe_stack_usage();
            int sub_result = measured_fake_pc_stress(ctx, depth - 1, computation ^ data);
            computation += sub_result;
            // Probe after recursion
            probe_stack_usage();
        }
    }

    clear_thread_vulnerable(ctx);

    // Final probe before return
    probe_stack_usage();

    return (int)(computation % 1000000);
}

// Initialize measurement for this pattern
static void setup_measurement(void) {
    init_stack_measurement();
    install_stack_measurement_handler();
}

// Pattern implementations with measurement
int pattern_create_complexity(int iterations) {
    static int measurement_initialized = 0;
    if (!measurement_initialized) {
        setup_measurement();
        measurement_initialized = 1;
    }

    uintptr_t fake_pc_context[16] = {0};
    int total_result = 0;

    for (int round = 0; round < iterations && round < 50; round++) {
        // Set up fake PC environment
        fake_pc_context[0] = (uintptr_t)&measured_fake_pc_stress + 8;
        fake_pc_context[1] = (uintptr_t)fake_pc_context;
        fake_pc_context[2] = (uintptr_t)&measured_fake_pc_stress + 16;

        // Probe before calling stress function
        probe_stack_usage();

        int result = measured_fake_pc_stress(
            (void*)fake_pc_context,
            round % 10 + 6,
            (uintptr_t)&total_result + round * 11
        );

        total_result = (total_result + result) % 1000000;
    }

    return total_result;
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
    g_stack_measurement.measurement_active = 0;
}

// Additional measurement reporting functions
__attribute__((visibility("default")))
uintptr_t get_pattern_stack_usage(void) {
    return get_max_stack_usage();
}

__attribute__((visibility("default")))
int get_pattern_signal_count(void) {
    return get_signal_count();
}

__attribute__((visibility("default")))
int get_pattern_deep_analysis_count(void) {
    return get_deep_analysis_count();
}

static const pattern_info_t fake_pc_measured_info = {
    .name = "fake-pc-measured",
    .description = "Fake PC manipulation with stack usage measurement to quantify IP analysis impact",
    .expected_stack_kb = 12,
    .go_equivalent = "gosave_systemstack_switch with measurement instrumentation"
};

const pattern_info_t* get_pattern_info(void) {
    return &fake_pc_measured_info;
}

#endif // ENABLE_FAKE_PC_MEASURED_PATTERN