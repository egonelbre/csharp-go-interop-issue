/*
 * UNDEF Instruction Pattern - Simplified Version
 *
 * Creates undefined instruction contexts that trap IP boundary analysis
 * without complex inline assembly. Uses the same concept as Go's
 * systemstack_switch but in a way that builds reliably.
 *
 * Expected Impact: 4-8KB stack consumption before analysis failure
 */

#include "pattern_base.h"

#ifdef ENABLE_UNDEF_PATTERN

static __thread int t_is_vulnerable = 0;

void mark_thread_vulnerable(void* ctx) {
    (void)ctx;
    t_is_vulnerable = 1;
}

void clear_thread_vulnerable(void* ctx) {
    (void)ctx;
    t_is_vulnerable = 0;
}

// Create contexts that will be difficult for IP boundary analysis
static void create_analysis_trap_context(void* context, int complexity_level) {
    uintptr_t* trap_context = (uintptr_t*)context;

    // Store addresses that point to problematic locations
    // These simulate what happens when IP analysis hits Go's UNDEF instructions
    uintptr_t base = (uintptr_t)context;

    // Create patterns that mimic undefined instruction boundaries
    for (int i = 0; i < 8; i++) {
        // These addresses will be examined by IP boundary analysis
        // We create offsets that would correspond to undefined regions
        trap_context[i] = base + i * complexity_level * 0x100;

        // Add some addresses that would be problematic for analysis
        if (i % 2 == 0) {
            // Simulate addresses near function boundaries (like UNDEF regions)
            trap_context[i] |= 0xDEADBEEF00000000ULL;  // Invalid high bits
        } else {
            // Create addresses with unusual alignments
            trap_context[i] += 3; // Misaligned (not 4-byte boundary)
        }
    }
}

// Function that executes in contexts that stress IP boundary analysis
static int undef_analysis_stress(void* ctx, int depth, uintptr_t data) {
    // Large frame to force extensive IP boundary analysis
    volatile char analysis_frame[2048];
    volatile uintptr_t trap_addresses[32];

    if (depth <= 0) return (int)(data & 0xFFFF);

    mark_thread_vulnerable(ctx);

    // Create complex address patterns that will stress IP analysis
    uintptr_t* context_traps = (uintptr_t*)ctx;
    for (int i = 0; i < 32; i++) {
        if (i < 8) {
            trap_addresses[i] = context_traps[i]; // Use the trap addresses
        } else {
            // Create more problematic addresses
            trap_addresses[i] = (uintptr_t)&undef_analysis_stress;

            // Add various problematic offsets
            switch (i % 4) {
                case 0: trap_addresses[i] += 0x1; break; // Misaligned
                case 1: trap_addresses[i] += 0x3; break; // Misaligned
                case 2: trap_addresses[i] |= 0x8000000000000000ULL; break; // High bit
                case 3: trap_addresses[i] = ~trap_addresses[i]; break; // Inverted
            }
        }
    }

    // Create frame patterns that require extensive analysis
    for (int i = 0; i < 2048; i++) {
        analysis_frame[i] = (char)(trap_addresses[i % 32] & 0xFF);
    }

    // Intensive computation that maximizes time in this problematic context
    volatile uintptr_t computation = 0;
    for (int work = 0; work < depth * 25; work++) { // High work multiplier
        computation ^= trap_addresses[work % 32];
        computation = (computation << 1) ^ (uintptr_t)&analysis_frame[work % 2048];

        // Update trap context periodically
        if (work % 30 == 0) {
            create_analysis_trap_context(ctx, work % 10 + 1);
        }

        // Deep recursion to create complex call stacks
        if (work % 35 == 0 && depth > 1 && depth <= 15) {
            int sub_result = undef_analysis_stress(ctx, depth - 1, computation ^ data);
            computation += sub_result;
        }

        // Create more trap contexts
        if (work % 20 == 0) {
            context_traps[work % 8] ^= (uintptr_t)&analysis_frame[work % 100];
        }
    }

    clear_thread_vulnerable(ctx);
    return (int)(computation % 1000000);
}

// Pattern implementations
int pattern_create_complexity(int iterations) {
    uintptr_t trap_context[16] = {0};
    int total_result = 0;

    for (int round = 0; round < iterations && round < 40; round++) {
        // Set up trap context that will stress IP analysis
        create_analysis_trap_context((void*)trap_context, round % 10 + 5);

        // Execute in this problematic context
        int result = undef_analysis_stress(
            (void*)trap_context,
            round % 12 + 8,  // Deep recursion
            (uintptr_t)&total_result + round * 13
        );

        total_result = (total_result + result) % 1000000;
    }

    return total_result;
}

int pattern_stress_scenario(int iterations) {
    uintptr_t trap_context[16] = {0};
    int total = 0;

    for (int i = 0; i < iterations && i < 120; i++) {
        create_analysis_trap_context((void*)trap_context, i % 8 + 3);

        total += undef_analysis_stress((void*)trap_context, i % 10 + 6, (uintptr_t)&total + i * 11);

        if (i % 12 == 0) {
            // Create more complex trap scenarios
            create_analysis_trap_context((void*)trap_context, i % 15 + 8);
            total += undef_analysis_stress((void*)trap_context, i % 8 + 7, (uintptr_t)&total + i * 17);
        }
    }

    return total % 1000000;
}

int pattern_extreme_stress(int base_iterations) {
    uintptr_t trap_context[16] = {0};
    int total = 0;

    for (int round = 0; round < base_iterations && round < 25; round++) {
        // Multiple trap contexts per round
        create_analysis_trap_context((void*)trap_context, round % 12 + 10);
        total += undef_analysis_stress((void*)trap_context, round % 13 + 9, (uintptr_t)&total + round * 19);

        if (round % 3 == 0) {
            // Even more complex traps
            create_analysis_trap_context((void*)trap_context, round % 20 + 15);
            total += undef_analysis_stress((void*)trap_context, round % 10 + 8, (uintptr_t)&total + round * 23);
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

static const pattern_info_t undef_info = {
    .name = "undef-instruction-trap",
    .description = "Creates problematic address contexts that trap IP boundary analysis like Go's UNDEF instructions",
    .expected_stack_kb = 6, // 4-8KB expected
    .go_equivalent = "systemstack_switch: PCALIGN $8; UNDEF; - creates analysis traps"
};

const pattern_info_t* get_pattern_info(void) {
    return &undef_info;
}

#endif // ENABLE_UNDEF_PATTERN