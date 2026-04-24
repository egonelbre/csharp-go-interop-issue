/*
 * Library Interface - Pattern Isolation Framework
 *
 * Common interface that matches the simplified reproducer's API.
 * Routes calls to whichever pattern is compiled in via preprocessor defines.
 */

#include "patterns/pattern_base.h"

// Export the same interface as simplified reproducer for .NET compatibility
__attribute__((visibility("default")))
int create_go_like_complexity() {
    return pattern_create_complexity(8);
}

__attribute__((visibility("default")))
int create_signal_stress_scenario(int iterations) {
    return pattern_stress_scenario(iterations);
}

__attribute__((visibility("default")))
int create_extreme_signal_analysis_stress(int baseIterations) {
    return pattern_extreme_stress(baseIterations);
}

__attribute__((visibility("default")))
int create_atypical_calling_convention_stress(int baseComplexity) {
    return pattern_calling_convention_stress(baseComplexity);
}

__attribute__((visibility("default")))
void cleanup_thread_context() {
    pattern_cleanup_context();
}

// Additional interface for pattern identification
__attribute__((visibility("default")))
const char* get_pattern_name() {
    const pattern_info_t* info = get_pattern_info();
    return info ? info->name : "unknown";
}

__attribute__((visibility("default")))
const char* get_pattern_description() {
    const pattern_info_t* info = get_pattern_info();
    return info ? info->description : "Unknown pattern";
}

__attribute__((visibility("default")))
int get_expected_stack_kb() {
    const pattern_info_t* info = get_pattern_info();
    return info ? info->expected_stack_kb : 0;
}

__attribute__((visibility("default")))
const char* get_go_equivalent() {
    const pattern_info_t* info = get_pattern_info();
    return info ? info->go_equivalent : "N/A";
}