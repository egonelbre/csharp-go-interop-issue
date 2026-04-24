/*
 * Tier 1 Combined Pattern - Fake PC + UNDEF
 *
 * Combines both critical Tier 1 patterns:
 * - Fake PC manipulation (8-16KB expected impact)
 * - UNDEF instruction deception (4-8KB expected impact)
 *
 * Expected Combined Impact: 12-24KB stack consumption
 */

#include "pattern_base.h"

#ifdef ENABLE_TIER1_COMBINED_PATTERN

static __thread int t_is_vulnerable = 0;

void mark_thread_vulnerable(void* ctx) { (void)ctx; t_is_vulnerable = 1; }
void clear_thread_vulnerable(void* ctx) { (void)ctx; t_is_vulnerable = 0; }

// Combined fake PC + UNDEF stress function
static int tier1_combined_stress(void* ctx, int depth, uintptr_t data) {
    // Large frame combining both patterns' complexity
    volatile char combined_frame[3072];        // Fake PC (2048) + UNDEF (2048) - overlap
    volatile uintptr_t fake_pc_state[16];      // Fake PC contexts
    volatile uintptr_t trap_addresses[32];     // UNDEF trap contexts

    if (depth <= 0) return (int)(data & 0xFFFF);

    mark_thread_vulnerable(ctx);

    // Set up fake PC contexts (from fake PC pattern)
    uintptr_t* context_pcs = (uintptr_t*)ctx;
    for (int i = 0; i < 16; i++) {
        if (i < 8) {
            fake_pc_state[i] = context_pcs[i % 5]; // Use the fake PCs
        } else {
            fake_pc_state[i] = (uintptr_t)&tier1_combined_stress + i * 0x100;
        }
    }

    // Set up UNDEF trap contexts (from UNDEF pattern)
    for (int i = 0; i < 32; i++) {
        if (i < 8) {
            trap_addresses[i] = context_pcs[i]; // Use trap addresses
        } else {
            trap_addresses[i] = (uintptr_t)&tier1_combined_stress;

            // Add problematic offsets (UNDEF pattern)
            switch (i % 4) {
                case 0: trap_addresses[i] += 0x1; break; // Misaligned
                case 1: trap_addresses[i] += 0x3; break; // Misaligned
                case 2: trap_addresses[i] |= 0x8000000000000000ULL; break; // High bit
                case 3: trap_addresses[i] = ~trap_addresses[i]; break; // Inverted
            }
        }
    }

    // Create combined frame patterns
    for (int i = 0; i < 3072; i++) {
        // Mix both fake PC and trap address patterns
        if (i % 2 == 0) {
            combined_frame[i] = (char)(fake_pc_state[i % 16] & 0xFF);
        } else {
            combined_frame[i] = (char)(trap_addresses[i % 32] & 0xFF);
        }
    }

    // Intensive computation combining both patterns
    volatile uintptr_t computation = 0;
    for (int work = 0; work < depth * 30; work++) { // Higher multiplier for combined stress
        // Alternate between fake PC and UNDEF processing
        if (work % 2 == 0) {
            computation ^= fake_pc_state[work % 16];
        } else {
            computation ^= trap_addresses[work % 32];
        }

        computation = (computation << 1) ^ (uintptr_t)&combined_frame[work % 3072];

        // Update fake PC contexts periodically (fake PC pattern behavior)
        if (work % 50 == 0) {
            context_pcs[work % 8] = (uintptr_t)&tier1_combined_stress + work * 0x10;
        }

        // Update trap contexts periodically (UNDEF pattern behavior)
        if (work % 30 == 0) {
            context_pcs[(work + 4) % 8] ^= (uintptr_t)&combined_frame[work % 100];
        }

        // Deep recursion with combined context
        if (work % 25 == 0 && depth > 1 && depth <= 18) { // Deeper recursion for combined stress
            int sub_result = tier1_combined_stress(ctx, depth - 1, computation ^ data);
            computation += sub_result;
        }
    }

    clear_thread_vulnerable(ctx);
    return (int)(computation % 1000000);
}

// Set up combined fake PC + UNDEF environment
static void setup_tier1_combined_environment(void* context) {
    uintptr_t* combined_context = (uintptr_t*)context;

    // Fake PC setup (from fake PC pattern)
    uintptr_t base_addr = (uintptr_t)&setup_tier1_combined_environment;
    combined_context[0] = base_addr + 8;   // Fake PC like Go's systemstack_switch+8
    combined_context[1] = (uintptr_t)context;    // Fake stack pointer
    combined_context[2] = base_addr + 16;  // Another fake PC
    combined_context[3] = base_addr + 0x100;  // Far offset fake PC

    // UNDEF trap setup (from UNDEF pattern)
    for (int i = 4; i < 8; i++) {
        combined_context[i] = base_addr + i * 0x100;

        // Add problematic patterns
        if (i % 2 == 0) {
            combined_context[i] |= 0xDEADBEEF00000000ULL;  // Invalid high bits
        } else {
            combined_context[i] += 3; // Misaligned
        }
    }
}

// Pattern implementations using combined Tier 1 stress
int pattern_create_complexity(int iterations) {
    uintptr_t combined_context[16] = {0};
    int total_result = 0;

    for (int round = 0; round < iterations && round < 60; round++) { // Higher iteration limit
        // Set up combined Tier 1 environment
        setup_tier1_combined_environment((void*)combined_context);

        // Execute with combined fake PC + UNDEF stress
        int result = tier1_combined_stress(
            (void*)combined_context,
            round % 15 + 10,  // Deeper recursion for combined patterns
            (uintptr_t)&total_result + round * 17
        );

        total_result = (total_result + result) % 1000000;
    }

    return total_result;
}

int pattern_stress_scenario(int iterations) {
    uintptr_t combined_context[16] = {0};
    int total = 0;

    for (int i = 0; i < iterations && i < 200; i++) { // Higher limit for combined stress
        setup_tier1_combined_environment((void*)combined_context);

        total += tier1_combined_stress((void*)combined_context, i % 12 + 8, (uintptr_t)&total + i * 13);

        if (i % 20 == 0) {
            // Reset combined environment with new patterns
            setup_tier1_combined_environment((void*)combined_context);
            total += tier1_combined_stress((void*)combined_context, i % 10 + 9, (uintptr_t)&total + i * 19);
        }
    }

    return total % 1000000;
}

int pattern_extreme_stress(int base_iterations) {
    uintptr_t combined_context[16] = {0};
    int total = 0;

    for (int round = 0; round < base_iterations && round < 40; round++) {
        // Multiple combined setups per round
        setup_tier1_combined_environment((void*)combined_context);
        total += tier1_combined_stress((void*)combined_context, round % 16 + 12, (uintptr_t)&total + round * 23);

        if (round % 3 == 0) {
            // Even more complex combined patterns
            setup_tier1_combined_environment((void*)combined_context);
            // Add extra fake PCs and trap addresses
            uintptr_t* ctx = (uintptr_t*)combined_context;
            ctx[8] = (uintptr_t)&pattern_extreme_stress + round * 0x200;
            ctx[9] = ~((uintptr_t)&tier1_combined_stress + round * 0x80);

            total += tier1_combined_stress((void*)combined_context, round % 12 + 10, (uintptr_t)&total + round * 29);
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

static const pattern_info_t tier1_combined_info = {
    .name = "tier1-combined",
    .description = "Combines fake PC manipulation + UNDEF instruction deception (both Tier 1 critical patterns)",
    .expected_stack_kb = 18, // 12-24KB expected combined
    .go_equivalent = "gosave_systemstack_switch + systemstack_switch: fake PCs + UNDEF instructions combined"
};

const pattern_info_t* get_pattern_info(void) {
    return &tier1_combined_info;
}

#endif // ENABLE_TIER1_COMBINED_PATTERN