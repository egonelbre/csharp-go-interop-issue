/*
 * Simplified C Library - Essential elements only
 *
 * Implements the same interface as complex_c_lib.c but with minimal
 * atypical calling convention patterns to test what's truly essential
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// Include the simplified atypical functions
extern int create_simplified_atypical_stress(int base_complexity);
extern void simple_atypical_function_1(void* context, uintptr_t param1, uintptr_t param2);

// Thread vulnerability tracking removed - was not essential

// Simplified implementations of the complex library interface

__attribute__((visibility("default")))
int create_go_like_complexity() {
    // Consolidated basic atypical calling convention complexity
    return create_simplified_atypical_stress(8);
}

__attribute__((visibility("default")))
int create_signal_stress_scenario(int iterations) {
    volatile uintptr_t context = (uintptr_t)&iterations;
    int total = 0;

    for (int i = 0; i < iterations && i < 100; i++) {
        // Use simplified atypical calling with moderate complexity
        simple_atypical_function_1(
            (void*)&context,
            context + i,
            context + i * 3
        );

        // Add some basic computation
        total += create_simplified_atypical_stress(i % 6 + 2);
    }

    return total % 1000000;
}

__attribute__((visibility("default")))
int create_extreme_signal_analysis_stress(int base_iterations) {
    // "Extreme" but still controlled complexity
    int total = 0;
    volatile uintptr_t context = (uintptr_t)&base_iterations;

    for (int round = 0; round < base_iterations && round < 20; round++) {
        // Multiple calls to simplified atypical functions
        total += create_simplified_atypical_stress(round % 8 + 5);

        if (round % 3 == 0) {
            simple_atypical_function_1(
                (void*)&context,
                context + round * 7,
                context + round * 11
            );
            total += create_simplified_atypical_stress(round % 5 + 3);
        }
    }

    return total % 1000000;
}

__attribute__((visibility("default")))
int create_atypical_calling_convention_stress(int base_complexity) {
    // Use the consolidated function - same as create_go_like_complexity but parameterized
    return create_simplified_atypical_stress(base_complexity);
}

// cleanup_thread_context removed - was just a wrapper for clear_thread_vulnerable