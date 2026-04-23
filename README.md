# Sigaltstack Overflow Reproducers

This repository contains reproducers for sigaltstack overflow bugs discovered during investigation of golang/go#78883. The investigation revealed **two distinct bugs** in different runtime components that both cause `SIGSEGV` crashes in signal handlers.

## Bug Summary

| Bug Class | Runtime Component | Stack Size | Fix Location | Reproducer |
|-----------|------------------|------------|--------------|------------|
| **Go Runtime Bug** | Go's gsignal stack | 32KB | `runtime/os_linux.go:388` | [`go-runtime-bug/`](go-runtime-bug/) |
| **CoreCLR PAL Bug** | CoreCLR's per-thread alt stack | 16KB | `src/coreclr/pal/src/thread/thread.cpp:2184` | [`coreclr-pal-bug/`](coreclr-pal-bug/) |

Both bugs manifest when signal handlers with deep stack usage (chkstk-style prologues) overflow their respective sigaltstacks.

## Quick Reproduction

Choose the reproducer that matches your scenario:

```bash
# Pure CoreCLR bug (most common production case)
cd coreclr-pal-bug && ./run.sh

# Pure Go runtime bug (C apps using Go libraries) 
cd go-runtime-bug && make && LD_LIBRARY_PATH=. REPRO_PROBE_BYTES=65536 ./host

# Combined case (original reproducer: .NET + Go)
cd dotnet-go-reproducer && ./run.sh
```

All should crash within seconds with `SIGSEGV (exit 139)`.

## Directory Structure

### Reproducers (Self-Contained)

- **[`go-runtime-bug/`](go-runtime-bug/)** — Pure C + Go reproducer demonstrating Go's 32KB gsignal stack overflow
- **[`coreclr-pal-bug/`](coreclr-pal-bug/)** — Pure C# + C reproducer demonstrating CoreCLR's 16KB PAL stack overflow  
- **[`dotnet-go-reproducer/`](dotnet-go-reproducer/)** — Combined .NET + Go reproducer (original) with workaround

### Analysis & Tools

- **[`investigation-tools/`](investigation-tools/)** — Tools used during investigation (syscall tracers, test artifacts)
- **[`docs/`](docs/)** — Comprehensive guides for reproduction, fixes, and workarounds
- **[`INVESTIGATION.md`](INVESTIGATION.md)** — Complete technical analysis (31KB) of both bugs

## Key Findings

### Root Cause Analysis

The investigation proved these are **two separate bugs**:

1. **CoreCLR PAL Bug**: CoreCLR installs 16KB sigaltstack per thread. Signal handlers with realistic stack usage (24KB+ chkstk prologues) overflow this.

2. **Go Runtime Bug**: Go installs 32KB gsignal stack per cgo thread. Deep signal handler chains exceed this limit.

### Evidence

| Finding | Evidence Location |
|---------|------------------|
| CoreCLR installs exactly 16,384 bytes | `investigation-tools/` syscall traces |
| Go takes "record-only" path on .NET threads | `investigation-tools/` in-process probes |
| 32KB threshold for Go runtime bug | `go-runtime-bug/FINDINGS.md` systematic testing |
| Workaround mechanism validation | `dotnet-go-reproducer/` with/without `REPRO_FIX=1` |
| **CoreCLR-side fix works end-to-end** | **Built `dotnet/runtime` main with `SIGSTKSZ*4` bump at `thread.cpp:2184`: unpatched 19/20 SIGSEGV, patched 0/20 under aggressive stress. See `INVESTIGATION.md` §2.** |

## For Immediate Production Use

If you're experiencing crashes in production:

1. **Apply workaround**: See [`docs/workaround-guide.md`](docs/workaround-guide.md)
2. **Quick test**: Use [`coreclr-pal-bug/`](coreclr-pal-bug/) to verify the issue
3. **Report upstream**: Reference this analysis in bug reports

## For Upstream Maintainers

### .NET Team (dotnet/runtime)
- **Reproducer**: [`coreclr-pal-bug/`](coreclr-pal-bug/) and [`dotnet-go-reproducer/`](dotnet-go-reproducer/)
- **Fix location**: `src/coreclr/pal/src/thread/thread.cpp:2184`
- **Proposed fix**: [`docs/fix-recommendations.md`](docs/fix-recommendations.md#coreclr-pal-fix) — single-line `+ (SIGSTKSZ * 4)`, mirrors the ASAN branch
- **Issue draft**: [`DOTNET_ISSUE.md`](DOTNET_ISSUE.md) — one-page writeup ready to post
- **Validation**: fix built and tested end-to-end against main (`b6421ec9f4f`). 19/20→0/20 under aggressive stress.

### Go Team (golang/go)  
- **Reproducer**: [`go-runtime-bug/`](go-runtime-bug/)
- **Fix location**: `src/runtime/os_linux.go:388`
- **Proposed fix**: [`docs/fix-recommendations.md`](docs/fix-recommendations.md#go-runtime-fix)

## Documentation Guide

| Document | Purpose | Audience |
|----------|---------|----------|
| [`docs/reproduction-guide.md`](docs/reproduction-guide.md) | Step-by-step reproduction | Developers investigating crashes |
| [`docs/workaround-guide.md`](docs/workaround-guide.md) | Apply immediate fix | Production applications |  
| [`docs/fix-recommendations.md`](docs/fix-recommendations.md) | Upstream fix details | Runtime maintainers |
| [`INVESTIGATION.md`](INVESTIGATION.md) | Complete technical analysis | Security researchers, runtime developers |

## Environment Requirements

| Reproducer | .NET | Go | GCC | Platform |
|-----------|------|----|----|----------|
| `coreclr-pal-bug/` | 10+ | — | ✓ | Linux x86_64 |
| `go-runtime-bug/` | — | 1.20+ | ✓ | Linux x86_64 |
| `dotnet-go-reproducer/` | 10+ | 1.20+ | ✓ | Linux x86_64 |

## Issue Context

This work originated from investigating crashes in production .NET applications that P/Invoke into Go c-shared libraries. The original issue report is [golang/go#78883](https://github.com/golang/go/issues/78883).

The investigation methodology was systematic:
1. **Reproduce crash** in controlled environment
2. **Isolate variables** with pure C/Go and pure C#/C reproducers
3. **Trace system behavior** with syscall monitoring and in-process probes
4. **Validate fixes** with patches to both runtimes
5. **Implement workaround** for immediate production relief

## Success Stories

This analysis has enabled:
- ✅ **Production mitigation**: Apps can deploy workaround immediately
- ✅ **Clear upstream path**: Each runtime team has specific reproducer and fix location  
- ✅ **Investigation methodology**: Reproducible evidence for complex runtime interactions
- ✅ **Educational value**: Documents systematic debugging of cross-runtime issues

## Contributing

Found related crashes or have additional test cases? 
- Add issues with stack traces and environment details
- Test reproducers on different platforms/versions
- Validate proposed fixes against your specific crash scenarios

## Acknowledgments

> **AI-assisted content.** This investigation, analysis, and reproducers were produced in collaboration with an AI coding assistant (Claude). The reproducers reliably demonstrate the bugs, but the technical interpretation should be verified against runtime source code before implementing fixes.

The systematic investigation approach was inspired by distributed systems debugging methodologies: isolate variables, trace interactions, validate hypotheses with controlled mutations.