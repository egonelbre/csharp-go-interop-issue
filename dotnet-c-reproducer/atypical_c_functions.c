/*
 * C Functions for Atypical Calling Convention Support
 *
 * These functions are called from the atypical assembly code and create
 * the complex conditions that stress IP boundary analysis.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

// External function declarations
extern void mark_thread_vulnerable(void* ctx);
extern void clear_thread_vulnerable(void* ctx);

// Assembly function declarations
extern void atypical_prologue_function_1(void* context, uintptr_t param1, uintptr_t param2, uintptr_t param3);

// C function called from assembly with complex register manipulation
int complex_register_manipulation_chain_c(void* ctx, int depth, uintptr_t data) {
    // Create complex local state that stresses unwinding analysis
    volatile char complex_local_state[1024];
    volatile uintptr_t register_simulation[16];
    jmp_buf context;

    if (depth <= 0) return (int)(data & 0xFFFF);

    // Mark as vulnerable during complex operations
    mark_thread_vulnerable(ctx);

    // Simulate complex register state
    for (int i = 0; i < 16; i++) {
        register_simulation[i] = data + (uintptr_t)ctx + i * depth;
    }

    // Create complex local state patterns
    for (int i = 0; i < 1024; i++) {
        complex_local_state[i] = (char)(register_simulation[i % 16] & 0xFF);
    }

    // setjmp creates complex unwinding scenarios
    if (setjmp(context) == 0) {
        // Complex nested operations
        volatile uintptr_t nested_computation = 0;

        // Controlled complexity to avoid infinite loops
        for (int layer = 0; layer < (depth < 15 ? depth : 15); layer++) {
            for (int work = 0; work < 50; work++) {  // Reduced from 100 to 50
                nested_computation ^= (uintptr_t)&complex_local_state[work % 1024];
                nested_computation = (nested_computation << 2) ^ register_simulation[work % 16];

                // Create additional call stack complexity (LIMITED DEPTH)
                if (work % 20 == 0 && layer > 0 && depth <= 10) {
                    // Recursive call with modified state (depth-limited)
                    int sub_result = complex_register_manipulation_chain_c(
                        ctx,
                        layer - 1,
                        nested_computation ^ data
                    );
                    nested_computation += sub_result;
                }
            }

            // Update register simulation with complex patterns
            for (int reg = 0; reg < 16; reg++) {
                register_simulation[reg] = (register_simulation[reg] * 7) ^ nested_computation;
            }
        }

        clear_thread_vulnerable(ctx);
        return (int)(nested_computation % 1000000);
    }

    clear_thread_vulnerable(ctx);
    return (int)((data * depth) % 1000000);
}

// C function for stack frame confusion
void stack_frame_confusion_pattern_c(void* ctx, uintptr_t base) {
    // Create extremely complex stack frame patterns
    volatile char large_frame[2048];
    volatile uintptr_t frame_ptrs[32];
    jmp_buf contexts[4];

    mark_thread_vulnerable(ctx);

    // Create complex frame pointer relationships
    for (int i = 0; i < 32; i++) {
        frame_ptrs[i] = base + (uintptr_t)&large_frame[i * 60];
    }

    // Fill frame with complex patterns
    for (int i = 0; i < 2048; i++) {
        large_frame[i] = (char)(frame_ptrs[i % 32] & 0xFF);
    }

    // Create multiple setjmp contexts (complex unwinding)
    for (int ctx_idx = 0; ctx_idx < 4; ctx_idx++) {
        if (setjmp(contexts[ctx_idx]) == 0) {
            // Complex work in each context
            volatile uintptr_t context_work = 0;

            for (int work = 0; work < 200; work++) {
                context_work ^= frame_ptrs[work % 32];
                context_work += (uintptr_t)&large_frame[work % 2048];

                // Create sub-frame complexity
                if (work % 50 == 0) {
                    volatile char sub_frame[512];
                    for (int sub = 0; sub < 512; sub++) {
                        sub_frame[sub] = (char)(context_work & 0xFF);
                        context_work = (context_work >> 1) ^ (uintptr_t)&sub_frame[sub];
                    }
                }
            }

            // Update frame pointers with results
            frame_ptrs[ctx_idx % 32] = context_work;
        }
    }

    clear_thread_vulnerable(ctx);
}

// Additional complex pattern that creates expensive analysis scenarios
void create_complex_unwind_metadata_scenario(void* ctx, int complexity_level) {
    // Create the types of complex scenarios mentioned in CoreCLR analysis

    // 1. Exception handling regions with complex unwind metadata
    volatile int exception_trigger = complexity_level;

    for (int level = 0; level < complexity_level; level++) {
        volatile char level_frame[1024];
        volatile uintptr_t level_state[64];

        mark_thread_vulnerable(ctx);

        // Create nested exception contexts
        for (int i = 0; i < 64; i++) {
            level_state[i] = (uintptr_t)ctx + level + i;
        }

        for (int i = 0; i < 1024; i++) {
            level_frame[i] = (char)(level_state[i % 64] & 0xFF);
        }

        // Simulate exception handling complexity
        if (exception_trigger > 0) {
            // Create complex exception context
            volatile uintptr_t exception_context = 0;

            for (int ex_work = 0; ex_work < 300; ex_work++) {
                exception_context ^= level_state[ex_work % 64];
                exception_context += (uintptr_t)&level_frame[ex_work % 1024];

                // Nested exception scenarios
                if (ex_work % 100 == 0) {
                    volatile char exception_frame[256];
                    for (int ex_sub = 0; ex_sub < 256; ex_sub++) {
                        exception_frame[ex_sub] = (char)(exception_context & 0xFF);
                    }
                    exception_context ^= (uintptr_t)&exception_frame[0];
                }
            }

            level_state[level % 64] = exception_context;
            exception_trigger--;
        }

        clear_thread_vulnerable(ctx);

        // Chain to next level with complex state (LIMITED DEPTH)
        if (level < complexity_level - 1 && level < 8) {
            create_complex_unwind_metadata_scenario(ctx, level + 1);
        }
    }
}

// Main exported function that orchestrates atypical calling patterns
__attribute__((visibility("default")))
int create_atypical_calling_convention_stress(int base_complexity) {
    volatile uintptr_t context_base = (uintptr_t)&base_complexity;
    int total_result = 0;

    for (int round = 0; round < base_complexity; round++) {
        // Call the atypical assembly functions
        atypical_prologue_function_1(
            (void*)&context_base,
            context_base + round,
            context_base + round * 2,
            context_base + round * 3
        );

        // Add additional complexity scenarios (BOUNDED)
        create_complex_unwind_metadata_scenario((void*)&context_base, round % 5 + 3);

        // Create more complex register/stack scenarios (BOUNDED)
        int chain_result = complex_register_manipulation_chain_c(
            (void*)&context_base,
            round % 8 + 5,  // Reduced from 20+10 to 8+5
            context_base + round * 7
        );

        total_result = (total_result + chain_result) % 1000000;

        // Additional stack frame confusion
        stack_frame_confusion_pattern_c((void*)&context_base, context_base + round);
    }

    return total_result;
}