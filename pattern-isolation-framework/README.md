# Pattern Isolation Framework

**Systematic testing framework to identify which specific Go assembly patterns cause CoreCLR sigaltstack overflow**

## Overview

This framework isolates and tests individual Go assembly patterns that were identified in the cross-reference analysis as potential culprits causing CoreCLR's `IsIPInProlog`/`IsIPInEpilog` functions to consume excessive stack space during signal handling.

## Architecture

```
pattern-isolation-framework/
├── patterns/           # Individual pattern implementations
│   ├── pattern_base.h         # Common interfaces
│   ├── pattern_baseline.c     # Control test (standard C)
│   ├── pattern_fake_pc.c      # Tier 1: Fake PC manipulation
│   └── pattern_undef.c        # Tier 1: UNDEF instruction deception
├── tests/             # Test results and analysis
├── scripts/           # Test automation
│   └── run_pattern_test.sh    # Individual pattern test runner
├── results/           # Test output logs
├── lib_interface.c    # Common API matching simplified reproducer
├── Program.cs         # .NET test host program
└── Makefile          # Build system
```

## Available Patterns

### **Tier 1: Critical Patterns (Expected to crash alone)**

1. **baseline-simple**: Standard C code with conventional calling patterns (control test)
   - Expected impact: 1KB stack consumption (should NOT crash)
   - Purpose: Verify framework works correctly

2. **fake-pc-manipulation**: Replicates Go's `gosave_systemstack_switch`
   - Expected impact: 8-16KB stack consumption
   - Go equivalent: `MOVQ $runtime·systemstack_switch+8(SB), R9; MOVQ R9, (g_sched+gobuf_pc)(R14)`
   - Stores artificial instruction pointer addresses that confuse IP boundary analysis

3. **undef-instruction-deception**: Replicates Go's `systemstack_switch` UNDEF traps
   - Expected impact: 4-8KB stack consumption
   - Go equivalent: `PCALIGN $8; UNDEF; CALL runtime·abort(SB)`
   - Creates analysis traps with undefined instructions

### **Tier 2: High Impact (Planned)**
- Complex ABI transitions (28-register preservation)
- Mid-function stack switching
- NOSPLIT/NOFRAME attribute effects

## Usage

### Quick Start
```bash
# Build all patterns and run basic tests
make all
make test-quick

# Test specific patterns
make test-baseline    # Should NOT crash (control)
make test-fake-pc     # Expected crash: fake PC impact
make test-undef       # Expected crash: UNDEF impact
```

### Individual Pattern Testing
```bash
# Build specific pattern
make libpattern_fake_pc.so

# Run isolated test
./scripts/run_pattern_test.sh fake_pc libpattern_fake_pc.so

# Results in results/ directory
```

### Environment Variables
- `REPRO_WORKERS=N` - Number of worker threads (default: 32)
- `REPRO_ITERATIONS=N` - Iterations per worker (default: 1,000,000)
- `REPRO_INTERVAL_US=N` - Signal interval in microseconds (default: 1)
- `TIMEOUT_SECONDS=N` - Test timeout (default: 30)

## Expected Results

### **Success Scenarios (No Crash)**
- **Exit Code 0**: Pattern completed successfully
- **Analysis**: Pattern does not cause IP boundary analysis overflow
- **Conclusion**: Not a critical culprit (may be amplifying factor only)

### **Critical Culprit Scenarios (Crash)**
- **Exit Code 134 (SIGABRT)**: CoreCLR internal error - typical sigaltstack overflow signature
- **Exit Code 139 (SIGSEGV)**: Segmentation fault during IP analysis
- **Analysis**: Pattern CAUSES CoreCLR overflow within 16KB sigaltstack limit
- **Conclusion**: CRITICAL CULPRIT identified

### **Timeout Scenarios**
- **Exit Code 124**: Test timed out (excessive but non-fatal impact)
- **Analysis**: Pattern causes slow analysis but doesn't crash immediately
- **Conclusion**: Contributing factor (non-critical)

## Integration with Task System

This framework implements **Task #17** and enables subsequent tasks:

- **Task #9**: Baseline reproducer test → `make test-baseline`
- **Task #10**: Fake PC isolation → `make test-fake-pc`
- **Task #11**: UNDEF instruction testing → `make test-undef`
- **Task #12-14**: Additional pattern tests (when implemented)
- **Task #15**: Tier 1 combination testing
- **Task #16**: Results analysis and ranking

## Cross-Reference with Analysis

Each pattern maps to specific Go runtime code:

| Pattern | Go Function | Key Assembly | Expected Impact |
|---------|-------------|--------------|----------------|
| fake-pc | `gosave_systemstack_switch` | `MOVQ $runtime·systemstack_switch+8(SB), R9` | 8-16KB |
| undef | `systemstack_switch` | `PCALIGN $8; UNDEF` | 4-8KB |

The framework tests whether these patterns can **independently** cause the same CoreCLR crashes observed in Go interop scenarios.

## Build System

The Makefile provides pattern-specific compilation:

```bash
# Each pattern gets its own preprocessor define
make libpattern_fake_pc.so     # -DENABLE_FAKE_PC_PATTERN
make libpattern_undef.so       # -DENABLE_UNDEF_PATTERN
make libpattern_baseline.so    # -DENABLE_BASELINE_SIMPLE_PATTERN
```

The .NET program dynamically loads different libraries to test each pattern in isolation.

## Next Steps

1. **Complete Task #17**: Framework setup ✓
2. **Run Task #9**: Baseline test to verify framework works
3. **Execute Tasks #10-11**: Test critical Tier 1 patterns
4. **Implement Tier 2 patterns**: Complex ABI, stack switching, NOSPLIT
5. **Run Task #16**: Analyze results and rank actual vs predicted impact

This framework will definitively identify which Go assembly patterns are **essential** for causing CoreCLR sigaltstack overflow vs which are merely **amplifying factors**.