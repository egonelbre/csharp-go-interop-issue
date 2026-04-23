# Fix Recommendations

This document provides specific recommendations for fixing the sigaltstack overflow bugs discovered during the golang/go#78883 investigation.

## Overview

Two distinct bugs require fixes in different codebases:

1. **Go Runtime Bug**: Go's 32KB gsignal stack overflows with deep signal handler chains
2. **CoreCLR PAL Bug**: CoreCLR's 16KB per-thread sigaltstack overflows with realistic handlers

## Go Runtime Fix

### Location
`src/runtime/os_linux.go:388` in Go source repository

### Current Code
```go
mp.gsignal = malg(32 * 1024) // Linux wants >= 2K
```

### Recommended Fix
```go
mp.gsignal = malg(128 * 1024) // Raised to accommodate chained C-runtime handlers
```

### Rationale
- **Lower bound**: CoreCLR's libcoreclr.so has single functions with 24KB chkstk prologues
- **Real-world chains**: Signal handler chains can stack multiple 4-24KB prologues  
- **Safety margin**: 128KB provides 4x the measured worst case
- **Memory cost**: ~96KB additional VA per extra M (affordable for typical usage)
- **Platform consistency**: Other Unix platforms also use 32KB and should be updated

### Testing
The fix can be validated with the reproducer in `/go-runtime-bug/`:
```bash
cd go-runtime-bug
# Test current (should fail)
make && LD_LIBRARY_PATH=. REPRO_PROBE_BYTES=65536 ./host

# Test with patched Go runtime (should pass)  
# After applying fix and rebuilding Go:
CGO_ENABLED=1 go build -buildmode=c-shared -o libgolib.so golib.go
LD_LIBRARY_PATH=. REPRO_PROBE_BYTES=65536 ./host
```

### Alternative Sizes
- **64KB**: Minimum to handle known cases, but little safety margin
- **256KB**: Conservative choice with ample margin
- **1MB**: Maximum reasonable size (matches what app-side workarounds use)

## CoreCLR PAL Fix

### Location
`src/coreclr/pal/src/thread/thread.cpp:2184` in dotnet/runtime repository

### Current Code
```cpp
int altStackSize = SIGSTKSZ + ALIGN_UP(sizeof(SignalHandlerWorkerReturnPoint), 16) + GetVirtualPageSize();
```

### Recommended Fix (Minimal)
```cpp
// SIGSTKSZ alone is too small for inject_activation_handler's
// chain (g_activationFunction can call arbitrarily-deep runtime
// code). Mirror the +SIGSTKSZ*4 expansion the ASAN branch below  
// already uses. Net cost: ~32KB additional VA per managed thread.
int altStackSize = SIGSTKSZ + (SIGSTKSZ * 4)
                 + ALIGN_UP(sizeof(SignalHandlerWorkerReturnPoint), 16)
                 + GetVirtualPageSize();
```

### Result
- **Current**: ~16KB alt stack (8KB + 2.7KB + 4KB + padding)
- **Proposed**: ~49KB alt stack (8KB + 32KB + 2.7KB + 4KB + padding)  
- **Cost**: ~32KB additional VA per managed thread

### Rationale
- **Precedent**: CoreCLR already uses this expansion for ASAN builds
- **Stack overflow handler**: CoreCLR uses 9 pages (36KB) for stack overflow, recognizing 16KB insufficiency
- **Real handler depth**: `inject_activation_handler` can call arbitrarily deep via `g_activationFunction`
- **Third-party compatibility**: Allows coexistence with native libraries using SA_ONSTACK handlers

### Testing
The fix can be validated with the reproducer in `/coreclr-pal-bug/`:
```bash
cd coreclr-pal-bug
# Test current (should fail)
./run.sh

# Test with patched CoreCLR runtime (should pass)
# After applying fix and rebuilding .NET runtime:
./run.sh
```

### Alternative Fix (Architectural)
Instead of expanding the alt stack, modify `inject_activation_handler` to use `SwitchStackAndExecuteHandler` like `sigsegv_handler` already does:

```cpp
// In inject_activation_handler, mirror sigsegv_handler pattern
if (g_safeActivationCheckFunction(CONTEXTGetPC(&winContext)))
{
    // OLD: InvokeActivationHandler(&winContext);
    // NEW: Switch back to interrupted thread's normal stack first
    size_t targetSp = CONTEXTGetSP(&winContext) - kActivationStackGuard;
    SwitchStackAndExecuteHandler(ACTIVATION_HANDLER_FLAG, siginfo, context, targetSp);
}
```

This eliminates unbounded depth on the alt stack entirely but requires more extensive changes.

## Implementation Priority

### High Priority
1. **CoreCLR PAL fix**: Affects production .NET applications using native libraries
2. **Application workarounds**: Immediate mitigation available (see `/docs/workaround-guide.md`)

### Medium Priority  
1. **Go runtime fix**: Affects C applications using Go c-shared libraries
2. **Go runtime consistency**: Update other Unix platforms to match

## Validation Strategy

### Per-Fix Testing
Each fix should be validated with its specific reproducer:
- Go fix → `/go-runtime-bug/` reproducer
- CoreCLR fix → `/coreclr-pal-bug/` reproducer

### Integration Testing  
Combined scenarios should also pass after fixes:
- `/dotnet-go-reproducer/` without workaround should pass after both fixes

### Regression Testing
Ensure fixes don't break existing functionality:
- Go: existing c-shared library usage
- CoreCLR: normal .NET application execution

## Timeline Considerations

### Short Term (weeks)
- Application-side workarounds for immediate production issues
- CoreCLR PAL fix (single codebase, clear ownership)

### Medium Term (months)  
- Go runtime fix (requires coordination with Go team)
- Backport considerations for LTS releases

### Long Term (future releases)
- Consider architectural changes for more robust signal handling
- Evaluate whether alt stack sizes should be configurable

## Compatibility Notes

Both fixes are **backward compatible**:
- **Go**: Larger gsignal stack doesn't change any APIs or behavior
- **CoreCLR**: Larger alt stack is invisible to managed code

Memory usage increases are **reasonable**:
- **Go**: ~96KB per extra M (typically few per process)
- **CoreCLR**: ~32KB per managed thread (scales with thread pool)

## Success Criteria

Fixes are successful when:
1. **Reproducers pass**: All three reproducers in this repo pass reliably
2. **No regressions**: Existing applications continue to work
3. **Real-world validation**: Production applications using affected patterns work stably
4. **Performance**: No measurable performance impact from larger stacks

## Contact

For implementation questions:
- **Go runtime**: Submit issue to golang/go with reference to go#78883
- **CoreCLR PAL**: Submit issue to dotnet/runtime with reference to this analysis
- **Questions**: Reference the investigation in `/INVESTIGATION.md`