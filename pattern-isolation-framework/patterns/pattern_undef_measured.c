/*
 * UNDEF Pattern with Measurement
 */

#include "pattern_base.h"
#include "stack_probe.h"

#ifdef ENABLE_UNDEF_MEASURED_PATTERN

stack_measurement_t g_stack_measurement = {0};
static __thread int t_is_vulnerable = 0;

void mark_thread_vulnerable(void* ctx) { (void)ctx; t_is_vulnerable = 1; probe_stack_usage(); }
void clear_thread_vulnerable(void* ctx) { (void)ctx; t_is_vulnerable = 0; }

static void create_analysis_trap_context_measured(void* context, int complexity_level) {
    uintptr_t* trap_context = (uintptr_t*)context;
    uintptr_t base = (uintptr_t)context;

    probe_stack_usage();

    for (int i = 0; i < 8; i++) {
        trap_context[i] = base + i * complexity_level * 0x100;

        if (i % 2 == 0) {
            trap_context[i] |= 0xDEADBEEF00000000ULL;
        } else {
            trap_context[i] += 3;
        }
    }

    probe_stack_usage();
}

static int measured_undef_analysis_stress(void* ctx, int depth, uintptr_t data) {
    volatile char analysis_frame[2048];
    volatile uintptr_t trap_addresses[32];

    if (depth <= 0) return (int)(data & 0xFFFF);

    probe_stack_usage();
    mark_thread_vulnerable(ctx);

    uintptr_t* context_traps = (uintptr_t*)ctx;
    for (int i = 0; i < 32; i++) {
        if (i < 8) {
            trap_addresses[i] = context_traps[i];
        } else {
            trap_addresses[i] = (uintptr_t)&measured_undef_analysis_stress;
            switch (i % 4) {
                case 0: trap_addresses[i] += 0x1; break;
                case 1: trap_addresses[i] += 0x3; break;
                case 2: trap_addresses[i] |= 0x8000000000000000ULL; break;
                case 3: trap_addresses[i] = ~trap_addresses[i]; break;
            }
        }
    }

    probe_stack_usage();

    for (int i = 0; i < 2048; i++) {
        analysis_frame[i] = (char)(trap_addresses[i % 32] & 0xFF);
    }

    volatile uintptr_t computation = 0;
    for (int work = 0; work < depth * 25; work++) {
        computation ^= trap_addresses[work % 32];
        computation = (computation << 1) ^ (uintptr_t)&analysis_frame[work % 2048];

        if (work % 30 == 0) {
            probe_stack_usage();
            create_analysis_trap_context_measured(ctx, work % 10 + 1);
        }

        if (work % 35 == 0 && depth > 1 && depth <= 15) {
            probe_stack_usage();
            int sub_result = measured_undef_analysis_stress(ctx, depth - 1, computation ^ data);
            computation += sub_result;
            probe_stack_usage();
        }

        if (work % 20 == 0) {
            context_traps[work % 8] ^= (uintptr_t)&analysis_frame[work % 100];
        }
    }

    clear_thread_vulnerable(ctx);
    probe_stack_usage();
    return (int)(computation % 1000000);
}

static void setup_measurement(void) {
    init_stack_measurement();
    install_stack_measurement_handler();
}

int pattern_create_complexity(int iterations) {
    static int initialized = 0;
    if (!initialized) { setup_measurement(); initialized = 1; }

    uintptr_t trap_context[16] = {0};
    int total_result = 0;

    for (int round = 0; round < iterations && round < 40; round++) {
        probe_stack_usage();
        create_analysis_trap_context_measured((void*)trap_context, round % 10 + 5);

        int result = measured_undef_analysis_stress(
            (void*)trap_context,
            round % 12 + 8,
            (uintptr_t)&total_result + round * 13
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

static const pattern_info_t undef_measured_info = {
    .name = "undef-measured",
    .description = "UNDEF instruction traps with stack usage measurement",
    .expected_stack_kb = 6,
    .go_equivalent = "systemstack_switch: PCALIGN $8; UNDEF; with measurement"
};

const pattern_info_t* get_pattern_info(void) { return &undef_measured_info; }

#endif