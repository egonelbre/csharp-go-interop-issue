/*
 * Stack Usage Measurement for IP Boundary Analysis Impact
 *
 * Measures actual stack consumption during signal handling to quantify
 * how much each pattern stresses CoreCLR's IP boundary analysis.
 */

#ifndef STACK_PROBE_H
#define STACK_PROBE_H

#include <stdint.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

// Stack measurement context
typedef struct {
    volatile uintptr_t stack_base;
    volatile uintptr_t stack_min_seen;
    volatile uintptr_t stack_max_usage;
    volatile int measurement_active;
    volatile int signal_count;
    volatile int deep_analysis_count;
} stack_measurement_t;

extern stack_measurement_t g_stack_measurement;

// Initialize stack measurement
static inline void init_stack_measurement(void) {
    // Get current stack pointer as baseline
    volatile char stack_marker;
    g_stack_measurement.stack_base = (uintptr_t)&stack_marker;
    g_stack_measurement.stack_min_seen = g_stack_measurement.stack_base;
    g_stack_measurement.stack_max_usage = 0;
    g_stack_measurement.measurement_active = 1;
    g_stack_measurement.signal_count = 0;
    g_stack_measurement.deep_analysis_count = 0;
}

// Probe stack usage (call frequently during execution)
static inline void probe_stack_usage(void) {
    if (!g_stack_measurement.measurement_active) return;

    volatile char current_stack_marker;
    uintptr_t current_sp = (uintptr_t)&current_stack_marker;

    // Track minimum stack pointer (maximum depth)
    if (current_sp < g_stack_measurement.stack_min_seen) {
        g_stack_measurement.stack_min_seen = current_sp;

        // Calculate stack usage from base
        uintptr_t usage = g_stack_measurement.stack_base - current_sp;
        if (usage > g_stack_measurement.stack_max_usage) {
            g_stack_measurement.stack_max_usage = usage;
        }

        // Detect potentially deep analysis (>8KB from base)
        if (usage > 8192) {
            g_stack_measurement.deep_analysis_count++;
        }
    }
}

// Get stack usage results
static inline uintptr_t get_max_stack_usage(void) {
    return g_stack_measurement.stack_max_usage;
}

static inline int get_signal_count(void) {
    return g_stack_measurement.signal_count;
}

static inline int get_deep_analysis_count(void) {
    return g_stack_measurement.deep_analysis_count;
}

// Signal handler to track signal delivery
static void stack_measurement_signal_handler(int sig, siginfo_t* info, void* context) {
    (void)sig; (void)info; (void)context;

    if (g_stack_measurement.measurement_active) {
        g_stack_measurement.signal_count++;

        // Probe stack usage during signal handling
        probe_stack_usage();
    }
}

// Install measurement signal handler
static inline void install_stack_measurement_handler(void) {
    struct sigaction sa;
    sa.sa_sigaction = stack_measurement_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;  // Allow nested signals for measurement

    // Install on a different signal (SIGUSR2) to measure parallel to SIGRTMIN
    sigaction(SIGUSR2, &sa, NULL);
}

#endif // STACK_PROBE_H