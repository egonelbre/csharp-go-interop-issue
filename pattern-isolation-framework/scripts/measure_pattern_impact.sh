#!/bin/bash
# Stack Usage Impact Measurement Script
# Measures quantitative impact of patterns on IP boundary analysis

set -euo pipefail

PATTERN_NAME="$1"
LIBRARY_FILE="$2"
WORKERS="${REPRO_WORKERS:-4}"
ITERATIONS="${REPRO_ITERATIONS:-500}"
INTERVAL="${REPRO_INTERVAL_US:-100}"
DURATION="${MEASUREMENT_DURATION:-10}"

echo "========================================="
echo "Pattern Impact Measurement"
echo "Pattern: $PATTERN_NAME"
echo "Workers: $WORKERS, Iterations: $ITERATIONS"
echo "Measurement Duration: ${DURATION}s"
echo "========================================="

# Set up environment
export LD_LIBRARY_PATH=".:${LD_LIBRARY_PATH:-}"
export REPRO_WORKERS="$WORKERS"
export REPRO_ITERATIONS="$ITERATIONS"
export REPRO_INTERVAL_US="$INTERVAL"

# Create symlink
ln -sf "$LIBRARY_FILE" libpattern.so

# Build .NET program
if [[ ! -f "bin/Release/net10.0/pattern-test" ]]; then
    dotnet build -c Release --nologo >/dev/null
fi

echo "Starting measurement..."

# Method 1: Monitor via /proc/pid/status for VmRSS changes
echo "=== Method 1: Memory Usage Monitoring ==="

BASELINE_RSS_START=$(ps -o rss= -p $$ | tr -d ' ')
echo "Baseline RSS at start: ${BASELINE_RSS_START}KB"

# Start the test in background
timeout "${DURATION}s" ./bin/Release/net10.0/pattern-test &
TEST_PID=$!

# Monitor memory usage
MAX_RSS=0
RSS_SAMPLES=0
RSS_TOTAL=0

while kill -0 "$TEST_PID" 2>/dev/null; do
    if RSS=$(ps -o rss= -p "$TEST_PID" 2>/dev/null | tr -d ' '); then
        RSS_TOTAL=$((RSS_TOTAL + RSS))
        RSS_SAMPLES=$((RSS_SAMPLES + 1))
        if [[ $RSS -gt $MAX_RSS ]]; then
            MAX_RSS=$RSS
        fi
    fi
    sleep 0.1
done

wait "$TEST_PID" 2>/dev/null || EXIT_CODE=$?

if [[ $RSS_SAMPLES -gt 0 ]]; then
    AVG_RSS=$((RSS_TOTAL / RSS_SAMPLES))
    RSS_DELTA=$((MAX_RSS - BASELINE_RSS_START))
    echo "Peak RSS: ${MAX_RSS}KB (delta: +${RSS_DELTA}KB)"
    echo "Average RSS: ${AVG_RSS}KB"
else
    echo "No RSS samples collected"
fi

# Method 2: System call monitoring via strace (sample)
echo ""
echo "=== Method 2: Signal Delivery Analysis ==="

# Count SIGRTMIN deliveries during a short test
SIGNAL_COUNT=$(timeout 2s strace -e trace=tgkill -c ./bin/Release/net10.0/pattern-test 2>&1 | grep tgkill | awk '{print $1}' || echo "0")
echo "SIGRTMIN signals delivered in 2s sample: $SIGNAL_COUNT"

# Method 3: Stack usage estimation via ulimit
echo ""
echo "=== Method 3: Stack Limit Testing ==="

# Test with progressively smaller stack limits to find crash point
for STACK_LIMIT in 65536 32768 16384 8192 4096; do
    echo -n "Testing with ${STACK_LIMIT}KB stack limit: "

    if timeout 3s bash -c "ulimit -s $STACK_LIMIT && ./bin/Release/net10.0/pattern-test >/dev/null 2>&1"; then
        echo "✅ PASSED"
        MIN_STACK_REQUIRED=$STACK_LIMIT
    else
        echo "❌ FAILED (crashed or timeout)"
        if [[ -z "${MIN_STACK_REQUIRED:-}" ]]; then
            echo "Pattern requires >${STACK_LIMIT}KB stack space"
            break
        fi
    fi
done

# Method 4: CoreCLR-specific analysis
echo ""
echo "=== Method 4: CoreCLR Signal Analysis ==="

# Check for CoreCLR-specific error patterns
echo "Checking for CoreCLR internal errors..."
TEST_OUTPUT=$(timeout 5s ./bin/Release/net10.0/pattern-test 2>&1 || true)

if echo "$TEST_OUTPUT" | grep -q "Internal CLR error"; then
    echo "🚨 CoreCLR internal error detected - likely sigaltstack overflow"
elif echo "$TEST_OUTPUT" | grep -q "Aborted"; then
    echo "⚠️ Program abort detected - possible analysis failure"
elif echo "$TEST_OUTPUT" | grep -q "Segmentation fault"; then
    echo "💥 Segmentation fault - stack or analysis overflow"
elif echo "$TEST_OUTPUT" | grep -q "All workers completed"; then
    echo "✅ No CoreCLR errors detected"
else
    echo "❓ Unclear result - check logs"
fi

# Results summary
echo ""
echo "========================================="
echo "MEASUREMENT SUMMARY"
echo "========================================="
echo "Pattern: $PATTERN_NAME"
echo "Peak Memory Delta: ${RSS_DELTA:-0}KB"
echo "Signal Rate: ~$((SIGNAL_COUNT / 2)) signals/second"

if [[ -n "${MIN_STACK_REQUIRED:-}" ]]; then
    STACK_IMPACT=$((65536 - MIN_STACK_REQUIRED))
    echo "Estimated Stack Impact: ${STACK_IMPACT}KB"

    if [[ $STACK_IMPACT -gt 16 ]]; then
        echo "📊 HIGH IMPACT: Pattern significantly stresses IP analysis (>${STACK_IMPACT}KB)"
    elif [[ $STACK_IMPACT -gt 8 ]]; then
        echo "📊 MODERATE IMPACT: Pattern moderately stresses IP analysis (~${STACK_IMPACT}KB)"
    elif [[ $STACK_IMPACT -gt 0 ]]; then
        echo "📊 LOW IMPACT: Pattern has measurable but minor impact (~${STACK_IMPACT}KB)"
    else
        echo "📊 MINIMAL IMPACT: Pattern has negligible measurable impact"
    fi
else
    echo "📊 IMPACT ASSESSMENT: Unable to determine stack impact quantitatively"
fi

# Cleanup
rm -f libpattern.so

echo "========================================="