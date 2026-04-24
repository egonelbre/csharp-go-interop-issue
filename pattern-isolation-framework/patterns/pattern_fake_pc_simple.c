/*
 * Fake PC Manipulation Pattern - Simplified Version
 *
 * Replicates the core concept of Go's fake PC manipulation without complex
 * inline assembly. Creates artificial instruction pointer contexts that
 * will confuse CoreCLR's IsIPInProlog/IsIPInEpilog analysis.
 *
 * Expected Impact: 8-16KB stack consumption in IP boundary analysis
 */

#include "pattern_base.h"

#ifdef ENABLE_FAKE_PC_PATTERN

static __thread int t_is_vulnerable = 0;
static __thread void* t_fake_pc_context = NULL;

void mark_thread_vulnerable(void* ctx) {
    t_is_vulnerable = 1;
    t_fake_pc_context = ctx;
}

void clear_thread_vulnerable(void* ctx) {
    (void)ctx;
    t_is_vulnerable = 0;
    t_fake_pc_context = NULL;
}

// Create artificial stack frames with fake return addresses
static void create_fake_pc_stack_frame(void* context, uintptr_t fake_pc1, uintptr_t fake_pc2) {
    // Simulate Go's gosave_systemstack_switch by storing fake PCs
    // in a context structure that signals might examine
    uintptr_t* pc_array = (uintptr_t*)context;

    // Store multiple fake PCs like Go does
    pc_array[0] = fake_pc1;              // Primary fake PC (g.sched.pc equivalent)
    pc_array[1] = (uintptr_t)context;    // Fake stack pointer
    pc_array[2] = fake_pc2;              // Secondary fake PC
    pc_array[3] = fake_pc1 + 0x10;       // Another offset fake PC
    pc_array[4] = fake_pc2 + 0x20;       // Yet another fake PC
}

// Function that creates complex stack analysis scenarios near fake PC context
static int fake_pc_analysis_stress(void* ctx, int depth, uintptr_t data) {
    volatile char complex_frame[1024];   // Larger frame to stress analysis
    volatile uintptr_t fake_pc_state[16]; // Multiple fake PC contexts

    if (depth <= 0) return (int)(data & 0xFFFF);

    mark_thread_vulnerable(ctx);

    // Extract fake PCs from context and create complex patterns
    uintptr_t* context_pcs = (uintptr_t*)ctx;
    for (int i = 0; i < 16; i++) {
        // Mix real addresses with fake PCs to confuse analysis
        if (i < 8) {
            fake_pc_state[i] = context_pcs[i % 5]; // Use the fake PCs
        } else {
            fake_pc_state[i] = (uintptr_t)&fake_pc_analysis_stress + i * 0x100;
        }
    }

    // Create frame patterns that require deep IP boundary analysis
    for (int i = 0; i < 1024; i++) {
        complex_frame[i] = (char)(fake_pc_state[i % 16] & 0xFF);
    }

    // Intensive computation that keeps us in this function context
    // where signals will hit and try to analyze our fake PC environment
    volatile uintptr_t computation = 0;
    for (int work = 0; work < depth * 20; work++) { // Increased work multiplier
        computation ^= fake_pc_state[work % 16];
        computation = (computation << 1) ^ (uintptr_t)&complex_frame[work % 1024];

        // Create more fake PC contexts during execution
        if (work % 50 == 0) {
            create_fake_pc_stack_frame(ctx,
                (uintptr_t)&fake_pc_analysis_stress + work * 0x10,
                (uintptr_t)&complex_frame + work * 0x8);
        }

        // Recursive call with fake PC context - this is where signals will hit
        if (work % 40 == 0 && depth > 1 && depth <= 12) {
            int sub_result = fake_pc_analysis_stress(ctx, depth - 1, computation ^ data);
            computation += sub_result;
        }
    }

    clear_thread_vulnerable(ctx);
    return (int)(computation % 1000000);
}

// Helper function to create artificial instruction pointer scenarios
static void setup_fake_pc_environment(void* context) {
    // Create fake instruction pointers at various offsets like Go does
    uintptr_t base_addr = (uintptr_t)&setup_fake_pc_environment;

    // Store fake PCs with various offsets (mimicking Go's +8 pattern)
    create_fake_pc_stack_frame(context,
        base_addr + 8,   // Like Go's systemstack_switch+8
        base_addr + 16   // Another offset
    );

    // Add more fake context patterns
    uintptr_t* pc_context = (uintptr_t*)context;
    pc_context[5] = base_addr + 0x100;  // Far offset fake PC
    pc_context[6] = base_addr + 0x200;  // Another far offset
    pc_context[7] = (uintptr_t)&fake_pc_analysis_stress + 0x50; // Cross-function fake PC
}

// Pattern implementations using fake PC manipulation concepts
int pattern_create_complexity(int iterations) {
    uintptr_t fake_pc_context[16] = {0}; // Context array for fake PCs
    int total_result = 0;

    for (int round = 0; round < iterations && round < 50; round++) {
        // Set up fake PC environment like Go's runtime does
        setup_fake_pc_environment((void*)fake_pc_context);

        // Execute in this fake PC context where signals will analyze artificial IPs
        int result = fake_pc_analysis_stress(
            (void*)fake_pc_context,
            round % 10 + 6,  // Deeper recursion for more stress
            (uintptr_t)&total_result + round * 11
        );

        total_result = (total_result + result) % 1000000;
    }

    return total_result;
}

int pattern_stress_scenario(int iterations) {
    uintptr_t fake_pc_context[16] = {0};
    int total = 0;

    for (int i = 0; i < iterations && i < 150; i++) {
        setup_fake_pc_environment((void*)fake_pc_context);

        // Multiple calls with fake PC context
        total += fake_pc_analysis_stress((void*)fake_pc_context, i % 8 + 4, (uintptr_t)&total + i * 7);

        if (i % 15 == 0) {
            // Reset fake PC environment with new patterns
            setup_fake_pc_environment((void*)fake_pc_context);
            total += fake_pc_analysis_stress((void*)fake_pc_context, i % 6 + 5, (uintptr_t)&total + i * 13);
        }
    }

    return total % 1000000;
}

int pattern_extreme_stress(int base_iterations) {
    uintptr_t fake_pc_context[16] = {0};
    int total = 0;

    for (int round = 0; round < base_iterations && round < 30; round++) {
        // Create multiple fake PC environments per round
        setup_fake_pc_environment((void*)fake_pc_context);
        total += fake_pc_analysis_stress((void*)fake_pc_context, round % 9 + 7, (uintptr_t)&total + round * 17);

        if (round % 3 == 0) {
            // More complex fake PC manipulation
            create_fake_pc_stack_frame((void*)fake_pc_context,
                (uintptr_t)&pattern_extreme_stress + round * 0x80,
                (uintptr_t)&fake_pc_analysis_stress + round * 0x40);

            total += fake_pc_analysis_stress((void*)fake_pc_context, round % 7 + 6, (uintptr_t)&total + round * 23);
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

static const pattern_info_t fake_pc_info = {
    .name = "fake-pc-manipulation",
    .description = "Creates artificial instruction pointer contexts like Go's gosave_systemstack_switch",
    .expected_stack_kb = 12, // 8-16KB expected
    .go_equivalent = "gosave_systemstack_switch: stores fake PC addresses that confuse IP boundary analysis"
};

const pattern_info_t* get_pattern_info(void) {
    return &fake_pc_info;
}

#endif // ENABLE_FAKE_PC_PATTERN