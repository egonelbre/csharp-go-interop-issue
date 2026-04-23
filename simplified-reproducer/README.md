# Simplified CoreCLR Sigaltstack Overflow Reproducer

**Minimal reproduction of CoreCLR sigaltstack overflow with 74% code reduction**

## Overview

This reproducer demonstrates a CoreCLR bug where atypical calling conventions can cause the runtime's IP boundary analysis to consume excessive stack space, overflowing the 16KB sigaltstack and causing crashes.

## Key Components

### Essential Elements (Required for Reproduction)
- **Assembly trampolines** with non-standard prologues/epilogues (`simplified_atypical.c`)
- **Atypical register usage** that confuses IP boundary analysis
- **Controlled complexity** stressing analysis to overflow 16KB limit
- **SA_ONSTACK signal mechanism** (provided by CoreCLR)

### Architecture
- **.NET host program** (`Program.cs`) - Creates CoreCLR runtime environment
- **C library** (`simplified_c_lib.c` + `simplified_atypical.c`) - Triggers overflow conditions
- **P/Invoke integration** - Bridges .NET → C calls during signal analysis

## Crash Signature

```
Fatal error.
Internal CLR error. (0x80131506)
Aborted (core dumped)
```

Exit code 134 (SIGABRT)

## Usage

### Quick Test (Fast Verification)
```bash
make test-minimal    # 4 workers, 500 iterations - crashes in ~10 seconds
```

### Standard Test  
```bash
make test-quick      # 8 workers, 1000 iterations - crashes in ~30 seconds  
```

### Full Test
```bash
make test            # Original parameters - may take longer
```

### Build Only
```bash
make build-c         # Build C library only
make build-dotnet    # Build .NET program only
make all            # Build everything
```

### Debug
```bash
make debug          # Run with GDB
```

## Environment Variables

- `REPRO_WORKERS` - Number of worker threads (default: 64)
- `REPRO_ITERATIONS` - Iterations per worker (default: 5,000,000)  
- `REPRO_INTERVAL_US` - Signal interval in microseconds (default: 1)
- `REPRO_MODE` - "signal" or "gc" (default: signal)

## Code Reduction

**Original complex reproducer:** 1,107 lines (4 source files)  
**This simplified version:** 288 lines (2 source files)  
**Reduction:** 74% while maintaining overflow capability

## Technical Background

This bug occurs when:
1. CoreCLR sets up signal handlers with SA_ONSTACK (16KB limit)
2. SIGRTMIN signals trigger during P/Invoke calls  
3. Signal handler performs IP boundary analysis (IsIPInProlog/IsIPInEpilog)
4. Atypical calling conventions cause analysis to consume excessive stack
5. Stack overflow in signal handler → Internal CLR error

The simplified version proves that basic C complexity patterns are insufficient - specific assembly trampolines with non-standard calling conventions are required to trigger the overflow.