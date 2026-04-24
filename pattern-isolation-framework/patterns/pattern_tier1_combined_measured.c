/*
 * Tier 1 Combined Pattern with Measurement
 */

#include "pattern_base.h"
#include "stack_probe.h"

#ifdef ENABLE_TIER1_COMBINED_MEASURED_PATTERN

stack_measurement_t g_stack_measurement = {0};
static __thread int t_is_vulnerable = 0;

void mark_thread_vulnerable(void* ctx) { (void)ctx; t_is_vulnerable = 1; probe_stack_usage(); }
void clear_thread_vulnerable(void* ctx) { (void)ctx; t_is_vulnerable = 0; }

static int measured_tier1_combined_stress(void* ctx, int depth, uintptr_t data) {
    volatile char combined_frame[3072];
    volatile uintptr_t fake_pc_state[16];
    volatile uintptr_t trap_addresses[32];

    if (depth <= 0) return (int)(data & 0xFFFF);

    probe_stack_usage();
    mark_thread_vulnerable(ctx);

    // Fake PC setup with measurement
    uintptr_t* context_pcs = (uintptr_t*)ctx;
    for (int i = 0; i < 16; i++) {
        if (i < 8) {
            fake_pc_state[i] = context_pcs[i % 5];
        } else {
            fake_pc_state[i] = (uintptr_t)&measured_tier1_combined_stress + i * 0x100;
        }
    }

    probe_stack_usage();

    // UNDEF trap setup with measurement
    for (int i = 0; i < 32; i++) {
        if (i < 8) {
            trap_addresses[i] = context_pcs[i];
        } else {
            trap_addresses[i] = (uintptr_t)&measured_tier1_combined_stress;
            switch (i % 4) {
                case 0: trap_addresses[i] += 0x1; break;
                case 1: trap_addresses[i] += 0x3; break;
                case 2: trap_addresses[i] |= 0x8000000000000000ULL; break;
                case 3: trap_addresses[i] = ~trap_addresses[i]; break;
            }
        }
    }

    probe_stack_usage();

    // Combined frame patterns
    for (int i = 0; i < 3072; i++) {
        if (i % 2 == 0) {
            combined_frame[i] = (char)(fake_pc_state[i % 16] & 0xFF);
        } else {
            combined_frame[i] = (char)(trap_addresses[i % 32] & 0xFF);
        }
    }

    probe_stack_usage();

    volatile uintptr_t computation = 0;
    for (int work = 0; work < depth * 30; work++) {
        if (work % 2 == 0) {
            computation ^= fake_pc_state[work % 16];
        } else {
            computation ^= trap_addresses[work % 32];
        }

        computation = (computation << 1) ^ (uintptr_t)&combined_frame[work % 3072];

        if (work % 50 == 0) {
            probe_stack_usage();
            context_pcs[work % 8] = (uintptr_t)&measured_tier1_combined_stress + work * 0x10;
        }

        if (work % 30 == 0) {
            probe_stack_usage();
            context_pcs[(work + 4) % 8] ^= (uintptr_t)&combined_frame[work % 100];
        }

        if (work % 25 == 0 && depth > 1 && depth <= 18) {
            probe_stack_usage();
            int sub_result = measured_tier1_combined_stress(ctx, depth - 1, computation ^ data);
            computation += sub_result;
            probe_stack_usage();
        }
    }

    clear_thread_vulnerable(ctx);
    probe_stack_usage();
    return (int)(computation % 1000000);
}

static void setup_tier1_combined_environment_measured(void* context) {
    uintptr_t* combined_context = (uintptr_t*)context;

    probe_stack_usage();

    uintptr_t base_addr = (uintptr_t)&setup_tier1_combined_environment_measured;
    combined_context[0] = base_addr + 8;
    combined_context[1] = (uintptr_t)context;
    combined_context[2] = base_addr + 16;
    combined_context[3] = base_addr + 0x100;

    for (int i = 4; i < 8; i++) {
        combined_context[i] = base_addr + i * 0x100;
        if (i % 2 == 0) {
            combined_context[i] |= 0xDEADBEEF00000000ULL;
        } else {
            combined_context[i] += 3;
        }
    }

    probe_stack_usage();
}

static void setup_measurement(void) {
    init_stack_measurement();
    install_stack_measurement_handler();
}

int pattern_create_complexity(int iterations) {
    static int initialized = 0;
    if (!initialized) { setup_measurement(); initialized = 1; }

    uintptr_t combined_context[16] = {0};
    int total_result = 0;

    for (int round = 0; round < iterations && round < 60; round++) {
        probe_stack_usage();
        setup_tier1_combined_environment_measured((void*)combined_context);

        int result = measured_tier1_combined_stress(
            (void*)combined_context,
            round % 15 + 10,
            (uintptr_t)&total_result + round * 17
        );

        total_result = (total_result + result) % 1000000;
    }

    return total_result;
}

int pattern_stress_scenario(int iterations) { return pattern_create_complexity(iterations); }
int pattern_extreme_stress(int base_iterations) { return pattern_create_complexity(base_iterations * 2); }
int pattern_calling_convention_stress(int base_complexity) { return pattern_create_complexity(base_complexity); }
void pattern_cleanup_context(void) { clear_thread_vulnerable(NULL); g_stack_measurement.measurement_active = 0; }

__attribute__((visibility("default"))) uintptr_t get_pattern_stack_usage(void) { return get_max_stack_usage(); }
__attribute__((visibility("default"))) int get_pattern_signal_count(void) { return get_signal_count(); }
__attribute__((visibility("default"))) int get_pattern_deep_analysis_count(void) { return get_deep_analysis_count(); }

static const pattern_info_t tier1_combined_measured_info = {
    .name = "tier1-combined-measured",
    .description = "Combined fake PC + UNDEF patterns with stack usage measurement",
    .expected_stack_kb = 18,
    .go_equivalent = "gosave_systemstack_switch + systemstack_switch with measurement"
};

const pattern_info_t* get_pattern_info(void) { return &tier1_combined_measured_info; }

#endif