/*
 * Pattern Isolation Framework - Base Header
 *
 * Defines common interfaces and control macros for testing individual
 * Go assembly patterns that stress CoreCLR's IP boundary analysis.
 */

#ifndef PATTERN_BASE_H
#define PATTERN_BASE_H

#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>

// Pattern control macros - define exactly one per build
// #define ENABLE_FAKE_PC_PATTERN
// #define ENABLE_UNDEF_PATTERN
// #define ENABLE_COMPLEX_ABI_PATTERN
// #define ENABLE_STACK_SWITCHING_PATTERN
// #define ENABLE_NOSPLIT_PATTERN
// #define ENABLE_BASELINE_SIMPLE_PATTERN

// Pattern interface - all patterns must implement these functions
typedef struct {
    void* context;
    uintptr_t param1;
    uintptr_t param2;
} pattern_context_t;

// Core pattern functions that each pattern module implements
int pattern_create_complexity(int iterations);
int pattern_stress_scenario(int iterations);
int pattern_extreme_stress(int base_iterations);
int pattern_calling_convention_stress(int base_complexity);
void pattern_cleanup_context(void);

// Common utility functions
void mark_thread_vulnerable(void* ctx);
void clear_thread_vulnerable(void* ctx);

// Pattern metadata for reporting
typedef struct {
    const char* name;
    const char* description;
    int expected_stack_kb;
    const char* go_equivalent;
} pattern_info_t;

extern const pattern_info_t* get_pattern_info(void);

#endif // PATTERN_BASE_H