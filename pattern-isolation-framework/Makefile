# Pattern Isolation Framework - Build System
#
# Builds different variants of the reproducer with isolated Go assembly patterns
# to identify which specific patterns cause CoreCLR IP boundary analysis overflow

# Pattern definitions
PATTERN_BASELINE = ENABLE_BASELINE_SIMPLE_PATTERN
PATTERN_FAKE_PC = ENABLE_FAKE_PC_PATTERN
PATTERN_UNDEF = ENABLE_UNDEF_PATTERN
PATTERN_COMPLEX_ABI = ENABLE_COMPLEX_ABI_PATTERN
PATTERN_STACK_SWITCH = ENABLE_STACK_SWITCHING_PATTERN
PATTERN_NOSPLIT = ENABLE_NOSPLIT_PATTERN
PATTERN_TIER1_COMBINED = ENABLE_TIER1_COMBINED_PATTERN

# Measured pattern definitions
PATTERN_BASELINE_MEASURED = ENABLE_BASELINE_MEASURED_PATTERN
PATTERN_FAKE_PC_MEASURED = ENABLE_FAKE_PC_MEASURED_PATTERN
PATTERN_UNDEF_MEASURED = ENABLE_UNDEF_MEASURED_PATTERN
PATTERN_TIER1_COMBINED_MEASURED = ENABLE_TIER1_COMBINED_MEASURED_PATTERN
PATTERN_DEBUG_SIMPLE = ENABLE_DEBUG_SIMPLE_PATTERN

# Build configuration
CC = gcc
CFLAGS = -fPIC -shared -O2 -g -Wall -Wextra
LDFLAGS = -lpthread

# Directories
PATTERN_DIR = patterns
TEST_DIR = tests
SCRIPT_DIR = scripts
RESULTS_DIR = results

# Pattern source files
PATTERN_SOURCES = \
	$(PATTERN_DIR)/pattern_baseline.c \
	$(PATTERN_DIR)/pattern_fake_pc.c \
	$(PATTERN_DIR)/pattern_undef.c

# Common library interface (matches simplified reproducer)
INTERFACE_SOURCE = lib_interface.c

# Output libraries
LIB_BASELINE = libpattern_baseline.so
LIB_FAKE_PC = libpattern_fake_pc.so
LIB_UNDEF = libpattern_undef.so
LIB_COMPLEX_ABI = libpattern_complex_abi.so
LIB_STACK_SWITCH = libpattern_stack_switch.so
LIB_NOSPLIT = libpattern_nosplit.so
LIB_TIER1_COMBINED = libpattern_tier1_combined.so

# Measured pattern libraries
LIB_BASELINE_MEASURED = libpattern_baseline_measured.so
LIB_FAKE_PC_MEASURED = libpattern_fake_pc_measured.so
LIB_UNDEF_MEASURED = libpattern_undef_measured.so
LIB_TIER1_COMBINED_MEASURED = libpattern_tier1_combined_measured.so
LIB_DEBUG_SIMPLE = libpattern_debug_simple.so

# .NET program
DOTNET_PROJ = PatternTest.csproj
DOTNET_PROGRAM = Program.cs

# Default target
all: setup patterns dotnet

# Create directory structure
setup:
	@mkdir -p $(TEST_DIR) $(SCRIPT_DIR) $(RESULTS_DIR)

# Build all pattern variants
patterns: $(LIB_BASELINE) $(LIB_FAKE_PC) $(LIB_UNDEF) $(LIB_COMPLEX_ABI) $(LIB_STACK_SWITCH) $(LIB_NOSPLIT)

# Pattern-specific builds
$(LIB_BASELINE): $(PATTERN_SOURCES) $(INTERFACE_SOURCE)
	$(CC) $(CFLAGS) -D$(PATTERN_BASELINE) \
		$(PATTERN_DIR)/pattern_baseline.c $(INTERFACE_SOURCE) \
		-o $@ $(LDFLAGS)

$(LIB_FAKE_PC): $(PATTERN_SOURCES) $(INTERFACE_SOURCE)
	$(CC) $(CFLAGS) -D$(PATTERN_FAKE_PC) \
		$(PATTERN_DIR)/pattern_fake_pc_simple.c $(INTERFACE_SOURCE) \
		-o $@ $(LDFLAGS)

$(LIB_UNDEF): $(PATTERN_SOURCES) $(INTERFACE_SOURCE)
	$(CC) $(CFLAGS) -D$(PATTERN_UNDEF) \
		$(PATTERN_DIR)/pattern_undef_simple.c $(INTERFACE_SOURCE) \
		-o $@ $(LDFLAGS)

$(LIB_COMPLEX_ABI): $(PATTERN_SOURCES) $(INTERFACE_SOURCE)
	$(CC) $(CFLAGS) -D$(PATTERN_COMPLEX_ABI) \
		$(PATTERN_DIR)/pattern_complex_abi.c $(INTERFACE_SOURCE) \
		-o $@ $(LDFLAGS)

$(LIB_STACK_SWITCH): $(PATTERN_SOURCES) $(INTERFACE_SOURCE)
	$(CC) $(CFLAGS) -D$(PATTERN_STACK_SWITCH) \
		$(PATTERN_DIR)/pattern_stack_switch.c $(INTERFACE_SOURCE) \
		-o $@ $(LDFLAGS)

$(LIB_NOSPLIT): $(PATTERN_SOURCES) $(INTERFACE_SOURCE)
	$(CC) $(CFLAGS) -D$(PATTERN_NOSPLIT) \
		$(PATTERN_DIR)/pattern_nosplit.c $(INTERFACE_SOURCE) \
		-o $@ $(LDFLAGS)

$(LIB_TIER1_COMBINED): $(PATTERN_SOURCES) $(INTERFACE_SOURCE)
	$(CC) $(CFLAGS) -D$(PATTERN_TIER1_COMBINED) \
		$(PATTERN_DIR)/pattern_tier1_combined.c $(INTERFACE_SOURCE) \
		-o $@ $(LDFLAGS)

# Measured pattern builds
$(LIB_BASELINE_MEASURED): $(PATTERN_SOURCES) $(INTERFACE_SOURCE)
	$(CC) $(CFLAGS) -D$(PATTERN_BASELINE_MEASURED) \
		$(PATTERN_DIR)/pattern_baseline_measured.c $(INTERFACE_SOURCE) \
		-o $@ $(LDFLAGS)

$(LIB_FAKE_PC_MEASURED): $(PATTERN_SOURCES) $(INTERFACE_SOURCE)
	$(CC) $(CFLAGS) -D$(PATTERN_FAKE_PC_MEASURED) \
		$(PATTERN_DIR)/pattern_fake_pc_measured.c $(INTERFACE_SOURCE) \
		-o $@ $(LDFLAGS)

$(LIB_UNDEF_MEASURED): $(PATTERN_SOURCES) $(INTERFACE_SOURCE)
	$(CC) $(CFLAGS) -D$(PATTERN_UNDEF_MEASURED) \
		$(PATTERN_DIR)/pattern_undef_measured.c $(INTERFACE_SOURCE) \
		-o $@ $(LDFLAGS)

$(LIB_TIER1_COMBINED_MEASURED): $(PATTERN_SOURCES) $(INTERFACE_SOURCE)
	$(CC) $(CFLAGS) -D$(PATTERN_TIER1_COMBINED_MEASURED) \
		$(PATTERN_DIR)/pattern_tier1_combined_measured.c $(INTERFACE_SOURCE) \
		-o $@ $(LDFLAGS)

$(LIB_DEBUG_SIMPLE): $(PATTERN_SOURCES) $(INTERFACE_SOURCE)
	$(CC) $(CFLAGS) -D$(PATTERN_DEBUG_SIMPLE) \
		$(PATTERN_DIR)/pattern_debug_simple.c $(INTERFACE_SOURCE) \
		-o $@ $(LDFLAGS)

# Build .NET test program
dotnet: $(DOTNET_PROJ) $(DOTNET_PROGRAM)
	dotnet build -c Release --nologo

# Test targets
test-baseline: $(LIB_BASELINE) dotnet
	@echo "Testing baseline pattern (should NOT crash)..."
	@$(SCRIPT_DIR)/run_pattern_test.sh baseline $(LIB_BASELINE)

test-fake-pc: $(LIB_FAKE_PC) dotnet
	@echo "Testing fake PC pattern (expected: 8-16KB crash)..."
	@$(SCRIPT_DIR)/run_pattern_test.sh fake_pc $(LIB_FAKE_PC)

test-undef: $(LIB_UNDEF) dotnet
	@echo "Testing UNDEF pattern (expected: 4-8KB crash)..."
	@$(SCRIPT_DIR)/run_pattern_test.sh undef $(LIB_UNDEF)

test-complex-abi: $(LIB_COMPLEX_ABI) dotnet
	@echo "Testing complex ABI pattern (expected: 3-6KB crash)..."
	@$(SCRIPT_DIR)/run_pattern_test.sh complex_abi $(LIB_COMPLEX_ABI)

test-stack-switch: $(LIB_STACK_SWITCH) dotnet
	@echo "Testing stack switching pattern (expected: 2-4KB crash)..."
	@$(SCRIPT_DIR)/run_pattern_test.sh stack_switch $(LIB_STACK_SWITCH)

test-nosplit: $(LIB_NOSPLIT) dotnet
	@echo "Testing NOSPLIT pattern (expected: 1-3KB crash)..."
	@$(SCRIPT_DIR)/run_pattern_test.sh nosplit $(LIB_NOSPLIT)

test-tier1-combined: $(LIB_TIER1_COMBINED) dotnet
	@echo "Testing Tier 1 combined pattern (expected: 12-24KB crash)..."
	@$(SCRIPT_DIR)/run_pattern_test.sh tier1_combined $(LIB_TIER1_COMBINED)

# Measured pattern tests
patterns-measured: $(LIB_BASELINE_MEASURED) $(LIB_FAKE_PC_MEASURED) $(LIB_UNDEF_MEASURED) $(LIB_TIER1_COMBINED_MEASURED)

test-baseline-measured: $(LIB_BASELINE_MEASURED)
	@echo "Testing baseline with measurement..."
	@cp $(LIB_BASELINE_MEASURED) libpattern.so
	@timeout 10s ./bin/Release/net10.0/pattern-test || true
	@rm -f libpattern.so

test-fake-pc-measured: $(LIB_FAKE_PC_MEASURED)
	@echo "Testing fake PC with measurement..."
	@cp $(LIB_FAKE_PC_MEASURED) libpattern.so
	@timeout 15s ./bin/Release/net10.0/pattern-test || true
	@rm -f libpattern.so

test-undef-measured: $(LIB_UNDEF_MEASURED)
	@echo "Testing UNDEF with measurement..."
	@cp $(LIB_UNDEF_MEASURED) libpattern.so
	@timeout 15s ./bin/Release/net10.0/pattern-test || true
	@rm -f libpattern.so

test-tier1-combined-measured: $(LIB_TIER1_COMBINED_MEASURED)
	@echo "Testing Tier 1 combined with measurement..."
	@cp $(LIB_TIER1_COMBINED_MEASURED) libpattern.so
	@timeout 20s ./bin/Release/net10.0/pattern-test || true
	@rm -f libpattern.so

# Run all measured tests
test-all-measured: patterns-measured dotnet test-baseline-measured test-fake-pc-measured test-undef-measured test-tier1-combined-measured
	@echo "All measurement tests completed"

# Run all individual pattern tests
test-all-individual: test-baseline test-fake-pc test-undef
	@echo "Individual pattern testing complete"

# Quick test (minimal iterations for fast results)
test-quick:
	@REPRO_WORKERS=4 REPRO_ITERATIONS=500 make test-all-individual

# Clean build artifacts
clean:
	rm -f *.so
	rm -rf bin obj
	rm -f $(RESULTS_DIR)/*

# Show pattern information
info:
	@echo "Pattern Isolation Framework"
	@echo "Available patterns:"
	@echo "  baseline   - Standard C code (control test)"
	@echo "  fake-pc    - Fake PC manipulation (Tier 1 critical)"
	@echo "  undef      - Undefined instruction deception (Tier 1 critical)"
	@echo "  complex-abi - Complex ABI transitions (Tier 2)"
	@echo "  stack-switch - Mid-function stack switching (Tier 2)"
	@echo "  nosplit    - NOSPLIT/NOFRAME effects (Tier 3)"

# Help target
help:
	@echo "Pattern Isolation Framework - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all              - Build all patterns and .NET program"
	@echo "  patterns         - Build pattern libraries only"
	@echo "  dotnet           - Build .NET test program only"
	@echo "  test-baseline    - Test baseline pattern (should not crash)"
	@echo "  test-fake-pc     - Test fake PC pattern (expected crash)"
	@echo "  test-undef       - Test UNDEF pattern (expected crash)"
	@echo "  test-all-individual - Run all individual pattern tests"
	@echo "  test-quick       - Quick test with reduced iterations"
	@echo "  clean            - Clean build artifacts"
	@echo "  info             - Show pattern information"
	@echo ""
	@echo "Environment Variables:"
	@echo "  REPRO_WORKERS=N      - Number of worker threads (default: 32)"
	@echo "  REPRO_ITERATIONS=N   - Iterations per worker (default: 1000000)"
	@echo "  REPRO_INTERVAL_US=N  - Signal interval microseconds (default: 1)"

.PHONY: all setup patterns dotnet clean test-baseline test-fake-pc test-undef test-all-individual test-quick info help