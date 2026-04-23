/*
 * Minimal C Reproducer - Testing which elements are essential
 *
 * Starting with basic approach to see if we can trigger overflow
 * without complex SA_ONSTACK-specific patterns
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>

// Very simple function that just creates some stack usage
__attribute__((visibility("default")))
int minimal_complexity_function(int iterations) {
    volatile char stack_data[1024];  // 1KB stack frame
    volatile int result = 0;

    for (int i = 0; i < iterations; i++) {
        // Simple computation
        result += i * 7;

        // Touch stack memory
        if (i < 1024) {
            stack_data[i] = (char)(result & 0xFF);
        }

        // Simple recursive call to add some depth
        if (i % 100 == 0 && iterations > 1000) {
            result += minimal_complexity_function(iterations / 2);
        }
    }

    return result % 1000000;
}

// Medium complexity - add some function calls
static int medium_depth_function(int depth, uintptr_t data) {
    volatile char frame[512];
    volatile int local_result = 0;

    if (depth <= 0) return (int)(data & 0xFFFF);

    // Fill frame with some pattern
    for (int i = 0; i < 512; i++) {
        frame[i] = (char)((data + i + depth) & 0xFF);
    }

    // Some computation
    for (int work = 0; work < depth * 10; work++) {
        local_result ^= (int)(data >> (work % 8));
        local_result += frame[work % 512];
    }

    // Recursive call with reduced depth
    if (depth > 1) {
        local_result += medium_depth_function(depth - 1, data ^ local_result);
    }

    return local_result;
}

__attribute__((visibility("default")))
int medium_complexity_function(int base_iterations) {
    volatile uintptr_t context = (uintptr_t)&base_iterations;
    int total = 0;

    for (int i = 0; i < base_iterations; i++) {
        // Call medium depth function
        total += medium_depth_function(i % 10 + 5, context + i);

        // Some additional work
        if (i % 50 == 0) {
            total += minimal_complexity_function(i * 2);
        }
    }

    return total % 1000000;
}

// Higher complexity - more aggressive patterns
static int deep_recursive_function(int depth, volatile void* ctx, uintptr_t data) {
    volatile char large_frame[2048];  // 2KB frame
    volatile uintptr_t ptrs[16];

    if (depth <= 0) return (int)(data & 0xFFFF);

    // Setup frame data
    for (int i = 0; i < 16; i++) {
        ptrs[i] = data + (uintptr_t)ctx + i * depth;
    }

    // Fill large frame
    for (int i = 0; i < 2048; i++) {
        large_frame[i] = (char)(ptrs[i % 16] & 0xFF);
    }

    // Complex computation
    volatile uintptr_t computation = 0;
    for (int work = 0; work < depth * 20; work++) {
        computation ^= (uintptr_t)&large_frame[work % 2048];
        computation += ptrs[work % 16];

        if (work % 100 == 0) {
            // Create sub-frame complexity
            volatile char sub_frame[256];
            for (int sub = 0; sub < 256; sub++) {
                sub_frame[sub] = (char)(computation & 0xFF);
            }
            computation ^= (uintptr_t)&sub_frame[0];
        }
    }

    // Recursive call with controlled depth
    if (depth > 1 && depth <= 15) {
        computation += deep_recursive_function(depth - 1, &large_frame[0], computation);
    }

    return (int)(computation % 1000000);
}

__attribute__((visibility("default")))
int high_complexity_function(int base_iterations) {
    volatile uintptr_t context = (uintptr_t)&base_iterations;
    int total = 0;

    for (int round = 0; round < base_iterations; round++) {
        // Call deep recursive function
        total += deep_recursive_function(round % 12 + 8, &context, context + round);

        // Additional complexity patterns
        total += medium_complexity_function(round % 20 + 10);

        if (round % 25 == 0) {
            total += minimal_complexity_function(round * 3);
        }
    }

    return total % 1000000;
}