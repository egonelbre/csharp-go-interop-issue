/*
 * Extreme Complexity Module - Target SA_ONSTACK Signal Handler Analysis
 *
 * Based on SA_ONSTACK insight: CoreCLR's signal handler runs on 16KB sigaltstack
 * and needs to analyze our complex state. Create conditions that make this
 * analysis as expensive as possible.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

// Forward declarations
extern void mark_thread_vulnerable(void* ctx);
extern void clear_thread_vulnerable(void* ctx);

// Create extremely deep call chain with complex register/stack states
__attribute__((noinline))
static int extreme_depth_call_1(uintptr_t base, int depth, volatile void* data);

__attribute__((noinline))
static int extreme_depth_call_2(uintptr_t base, int depth, volatile void* data);

__attribute__((noinline))
static int extreme_depth_call_3(uintptr_t base, int depth, volatile void* data);

__attribute__((noinline))
static int extreme_depth_call_4(uintptr_t base, int depth, volatile void* data);

__attribute__((noinline))
static int extreme_depth_call_5(uintptr_t base, int depth, volatile void* data);

// Functions that create expensive-to-analyze instruction patterns
__attribute__((noinline))
static int extreme_depth_call_1(uintptr_t base, int depth, volatile void* data) {
    // Create complex local state that's expensive to unwind
    volatile char complex_frame[2048];  // Large frame
    volatile uintptr_t ptrs[64];
    jmp_buf context;

    if (depth <= 0) return (int)(base & 0xFFFF);

    // Complex pointer relationships
    for (int i = 0; i < 64; i++) {
        ptrs[i] = base + i * depth;
        if (i < 2040) {
            complex_frame[i] = (char)(ptrs[i % 64] & 0xFF);
        }
    }

    // setjmp creates complex unwinding scenarios
    if (setjmp(context) == 0) {
        // Create patterns that stress IP boundary analysis
        volatile uintptr_t computation = 0;
        for (int work = 0; work < 500; work++) {
            computation ^= (uintptr_t)&complex_frame[work % 2040];
            computation = (computation << 3) ^ ptrs[work % 64];
        }

        // Recursive call through different function (complex call graph)
        return extreme_depth_call_2(computation, depth - 1, &complex_frame[depth % 2040]);
    }

    return (int)(base % 10000);
}

__attribute__((noinline))
static int extreme_depth_call_2(uintptr_t base, int depth, volatile void* data) {
    volatile char frame_data[1024];
    volatile uint64_t registers[32]; // Simulate complex register state

    if (depth <= 0) return (int)(base & 0xFFFF);

    // Complex register-like operations that are hard to analyze
    for (int i = 0; i < 32; i++) {
        registers[i] = base + (uintptr_t)data + i;
        registers[i] = (registers[i] * 17) ^ (depth << i);
    }

    // Complex memory access patterns
    for (int j = 0; j < 1000; j++) {
        frame_data[j % 1024] = (char)(registers[j % 32] & 0xFF);
        if (j % 100 == 0) {
            // Create vulnerable state during complex operations
            mark_thread_vulnerable((void*)&registers[0]);
            // Complex computation while vulnerable
            volatile uint64_t temp = 0;
            for (int k = 0; k < 50; k++) {
                temp ^= registers[k % 32] + (uintptr_t)&frame_data[k % 1024];
            }
            clear_thread_vulnerable((void*)&registers[0]);
            base ^= temp;
        }
    }

    return extreme_depth_call_3(base, depth - 1, &frame_data[depth % 1024]);
}

__attribute__((noinline))
static int extreme_depth_call_3(uintptr_t base, int depth, volatile void* data) {
    volatile char large_frame[4096];  // Very large frame
    volatile uintptr_t complex_state[128];
    jmp_buf another_context;

    if (depth <= 0) return (int)(base & 0xFFFF);

    // Even more complex state setup
    for (int i = 0; i < 128; i++) {
        complex_state[i] = base + (uintptr_t)data + (i * depth);
    }

    // Multiple setjmp contexts (complex unwinding)
    if (setjmp(another_context) == 0) {
        // Fill large frame with complex patterns
        for (int i = 0; i < 4096; i++) {
            large_frame[i] = (char)(complex_state[i % 128] & 0xFF);
        }

        // Create extremely complex call pattern
        mark_thread_vulnerable((void*)complex_state);

        // Deep nested operations while vulnerable
        volatile uintptr_t nested_computation = 0;
        for (int layer = 0; layer < 10; layer++) {
            for (int work = 0; work < 200; work++) {
                nested_computation ^= (uintptr_t)&large_frame[work % 4096];
                nested_computation = (nested_computation >> 2) ^ complex_state[work % 128];
                if (work % 20 == 0) {
                    // Even more nesting to stress analysis
                    volatile char micro_frame[256];
                    for (int micro = 0; micro < 256; micro++) {
                        micro_frame[micro] = (char)(nested_computation & 0xFF);
                    }
                    nested_computation += (uintptr_t)&micro_frame[layer % 256];
                }
            }
        }

        clear_thread_vulnerable((void*)complex_state);

        return extreme_depth_call_4(nested_computation, depth - 1, &large_frame[depth % 4096]);
    }

    return extreme_depth_call_5(base, depth - 1, data);
}

__attribute__((noinline))
static int extreme_depth_call_4(uintptr_t base, int depth, volatile void* data) {
    volatile char mega_frame[8192];  // Huge frame
    volatile uintptr_t mega_state[256];

    if (depth <= 0) return (int)(base & 0xFFFF);

    // Absolutely massive state complexity
    for (int i = 0; i < 256; i++) {
        mega_state[i] = base + (uintptr_t)data + i;
    }

    // Create the most complex unwinding scenario possible
    mark_thread_vulnerable((void*)mega_state);

    // Multiple layers of complex operations
    for (int mega_layer = 0; mega_layer < 20; mega_layer++) {
        for (int large_work = 0; large_work < 1000; large_work++) {
            if (large_work < 8192) {
                mega_frame[large_work] = (char)(mega_state[large_work % 256] & 0xFF);
            }

            // Create sub-contexts within the vulnerable window
            if (large_work % 100 == 0) {
                volatile char sub_frame[512];
                volatile uintptr_t sub_ptrs[64];

                for (int sub = 0; sub < 64; sub++) {
                    sub_ptrs[sub] = (uintptr_t)&mega_frame[sub * 8];
                    if (sub < 512) {
                        sub_frame[sub] = (char)(sub_ptrs[sub] & 0xFF);
                    }
                }

                // Complex computation using all contexts
                volatile uintptr_t final_complexity = 0;
                for (int final = 0; final < 100; final++) {
                    final_complexity ^= sub_ptrs[final % 64];
                    final_complexity += mega_state[final % 256];
                    if (final < 512) {
                        final_complexity ^= (uintptr_t)&sub_frame[final];
                    }
                }

                base ^= final_complexity;
            }
        }
    }

    clear_thread_vulnerable((void*)mega_state);

    return extreme_depth_call_5(base, depth - 1, &mega_frame[depth % 8192]);
}

__attribute__((noinline))
static int extreme_depth_call_5(uintptr_t base, int depth, volatile void* data) {
    volatile char final_frame[1024];
    volatile uintptr_t final_state[128];

    if (depth <= 0) return (int)(base & 0xFFFF);

    // Final layer of complexity
    for (int i = 0; i < 128; i++) {
        final_state[i] = base + (uintptr_t)data + i * depth;
    }

    for (int i = 0; i < 1024; i++) {
        final_frame[i] = (char)(final_state[i % 128] & 0xFF);
    }

    // Recurse through different function to create complex call graph
    return extreme_depth_call_1(base ^ (uintptr_t)&final_frame[0], depth - 1, &final_state[depth % 128]);
}

// Main exported function that creates extreme complexity
__attribute__((visibility("default")))
int create_extreme_signal_analysis_stress(int base_iterations) {
    // Create conditions that would be extremely expensive for a signal handler
    // running on a 16KB sigaltstack to analyze

    volatile uintptr_t base = (uintptr_t)&base_iterations;
    int total_result = 0;

    for (int round = 0; round < base_iterations; round++) {
        // Each round creates a new complex scenario for signal analysis
        int result = extreme_depth_call_1(base + round, 25, &total_result);
        total_result = (total_result + result) % 1000000;

        // Create different starting points for complexity
        result = extreme_depth_call_3(base + round * 2, 20, &base);
        total_result = (total_result ^ result) % 1000000;

        result = extreme_depth_call_4(base + round * 3, 15, &round);
        total_result = (total_result * 3 + result) % 1000000;
    }

    return total_result;
}