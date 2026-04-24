#!/bin/bash
# Pattern Isolation Test Runner
# Runs individual pattern tests and detects crashes to measure impact

set -euo pipefail

# Configuration
PATTERN_NAME="$1"
LIBRARY_FILE="$2"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-30}"
WORKERS="${REPRO_WORKERS:-8}"
ITERATIONS="${REPRO_ITERATIONS:-1000}"
INTERVAL="${REPRO_INTERVAL_US:-50}"

# Results tracking
RESULTS_DIR="results"
RESULT_FILE="${RESULTS_DIR}/test_${PATTERN_NAME}_$(date +%s).log"

# Ensure results directory exists
mkdir -p "$RESULTS_DIR"

# Validate inputs
if [[ ! -f "$LIBRARY_FILE" ]]; then
    echo "ERROR: Library file not found: $LIBRARY_FILE"
    exit 1
fi

echo "========================================="
echo "Pattern Isolation Test"
echo "Pattern: $PATTERN_NAME"
echo "Library: $LIBRARY_FILE"
echo "Workers: $WORKERS, Iterations: $ITERATIONS"
echo "Timeout: ${TIMEOUT_SECONDS}s"
echo "========================================="

# Set up environment
export LD_LIBRARY_PATH=".:${LD_LIBRARY_PATH:-}"
export REPRO_WORKERS="$WORKERS"
export REPRO_ITERATIONS="$ITERATIONS"
export REPRO_INTERVAL_US="$INTERVAL"

# Create symlink for the .NET program to find
ln -sf "$LIBRARY_FILE" libpattern.so

# Build .NET program if needed
if [[ ! -f "bin/Release/net10.0/pattern-test" ]]; then
    echo "Building .NET program..."
    dotnet build -c Release --nologo >/dev/null
fi

# Run test with timeout and capture all output
echo "Starting pattern test..."
START_TIME=$(date +%s)
EXIT_CODE=0

timeout "${TIMEOUT_SECONDS}s" ./bin/Release/net10.0/pattern-test 2>&1 | tee "$RESULT_FILE" || EXIT_CODE=$?

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

# Analyze results
echo ""
echo "========================================="
echo "Test Results for Pattern: $PATTERN_NAME"
echo "========================================="

case $EXIT_CODE in
    0)
        echo "✅ COMPLETED: Test completed successfully without crash"
        echo "   Elapsed: ${ELAPSED}s"
        echo "   Analysis: Pattern does NOT cause CoreCLR overflow"
        echo "   Conclusion: Not a critical culprit"
        ;;
    134|139)
        echo "💥 CRASHED: Test crashed with signal (exit code $EXIT_CODE)"
        echo "   Elapsed: ${ELAPSED}s (crashed)"
        if [[ $EXIT_CODE -eq 139 ]]; then
            echo "   Signal: SIGSEGV (segmentation fault) - sigaltstack overflow"
        elif [[ $EXIT_CODE -eq 134 ]]; then
            echo "   Signal: SIGABRT (abort) - CoreCLR internal error"
        fi
        echo "   Analysis: Pattern CAUSES CoreCLR IP boundary analysis overflow"
        echo "   Conclusion: CRITICAL CULPRIT identified"
        ;;
    124)
        echo "⏰ TIMEOUT: Test timed out after ${TIMEOUT_SECONDS}s"
        echo "   Analysis: Pattern may cause excessive analysis but not immediate crash"
        echo "   Conclusion: Possible contributor (non-fatal impact)"
        ;;
    *)
        echo "❌ ERROR: Test failed with exit code $EXIT_CODE"
        echo "   Elapsed: ${ELAPSED}s"
        echo "   Analysis: Unexpected failure mode"
        ;;
esac

# Extract pattern information from output
if grep -q "\[pattern-test\] Expected impact:" "$RESULT_FILE"; then
    EXPECTED_KB=$(grep "Expected impact:" "$RESULT_FILE" | sed 's/.*Expected impact: \([0-9]*\)KB.*/\1/')
    echo "   Expected impact: ${EXPECTED_KB}KB stack consumption"
fi

if grep -q "\[pattern-test\] Go equivalent:" "$RESULT_FILE"; then
    GO_EQUIV=$(grep "Go equivalent:" "$RESULT_FILE" | sed 's/.*Go equivalent: \(.*\)/\1/')
    echo "   Go equivalent: $GO_EQUIV"
fi

# Summary for results analysis
echo ""
echo "Result Summary:"
echo "Pattern: $PATTERN_NAME"
echo "Exit Code: $EXIT_CODE"
echo "Duration: ${ELAPSED}s"
echo "Critical: $([[ $EXIT_CODE -eq 134 || $EXIT_CODE -eq 139 ]] && echo "YES" || echo "NO")"

# Clean up
rm -f libpattern.so

echo "Full log: $RESULT_FILE"
echo "========================================="

exit $EXIT_CODE