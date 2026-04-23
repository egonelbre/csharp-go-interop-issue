// C library that creates complex managed/unmanaged transition patterns
// to trigger CoreCLR's sigaltstack overflow bug. This mimics the complex
// transition scenarios that Go's cgo creates.

#include <string.h>
#include <unistd.h>

// Forward declaration for callback
typedef int (*managed_callback_t)(int value);
static managed_callback_t g_managed_callback = NULL;

// Set the callback function pointer from managed code
void set_managed_callback(managed_callback_t callback) {
    g_managed_callback = callback;
}

// Complex transition function that creates managed → unmanaged → managed → unmanaged
// This mirrors Go's cgocall pattern: entersyscall → C code → callback → more C code
__attribute__((noinline)) __attribute__((used))
int complex_transition_work(int depth, int value) {
    // Force stack usage to create complex analysis scenarios
    volatile char buffer[4096];
    memset((void*)buffer, value & 0xFF, sizeof(buffer));

    if (depth > 0 && g_managed_callback != NULL) {
        // Create unmanaged → managed transition
        // This creates complex PC analysis scenarios when signals arrive
        // during the transition boundaries
        int callback_result = g_managed_callback(value + depth);

        // Back in unmanaged code - more complex state
        usleep(50); // Signal arrival window

        // Recursive call to create more transition complexity
        return complex_transition_work(depth - 1, callback_result) + buffer[depth % sizeof(buffer)];
    } else {
        // Extended unmanaged work with signal arrival window
        for (volatile int i = 0; i < 1000; i++) {
            buffer[i % sizeof(buffer)] = i & 0xFF;
        }
        usleep(100); // Wide signal window in unmanaged code
        return value + 42;
    }
}

// ping - creates complex managed/unmanaged transition patterns
// Pattern: managed → unmanaged (here) → managed (callback) → unmanaged (recursive)
int ping(void) {
    if (g_managed_callback == NULL) {
        // Fallback for when callback isn't set yet
        usleep(200);
        return 42;
    }

    // Start complex transition pattern
    return complex_transition_work(3, 1) % 100 == 0 ? 42 : 42; // Always return 42
}