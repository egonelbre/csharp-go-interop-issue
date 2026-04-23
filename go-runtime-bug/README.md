# Go Runtime Bug: 32KB Sigaltstack Overflow

This directory contains a reproducer for the Go runtime bug where Go's 32KB `gsignal` stack is too small for chained signal handlers with stack-hungry prologues.

## The Bug

When Go's `needm` creates a new M (OS thread) for a non-Go thread entering cgo, it allocates a 32KB sigaltstack via `malg(32 * 1024)` in `runtime/os_linux.go:388`. This stack is used for signal handlers on that thread.

However, some signal handler chains probe more than 32KB of stack, causing overflow. Specifically:
- CoreCLR's chkstk prologues can consume up to 24KB in a single function 
- Chained handlers (e.g., multiple runtime signal handlers) can exceed the 32KB limit
- The overflow walks off the bottom of the sigaltstack into unmapped memory → SIGSEGV

## Evidence

This reproducer demonstrates the **exact 32KB threshold** through systematic testing:

| Probe Size | Result |
|------------|--------|
| 28 KB | 5/5 PASS |
| **32 KB** | **5/5 FAIL** |
| 64 KB | 5/5 FAIL |

The cliff at exactly 32KB matches Go's `malg(32 * 1024)` allocation.

## Reproduction

### Prerequisites
- Go toolchain
- GCC
- Linux x86_64

### Build and Run
```bash
make                    # Build reproducer and libgolib.so
LD_LIBRARY_PATH=. REPRO_PROBE_BYTES=65536 ./host
```

### Expected Output
```
[c-repro] workers=32 iters=1000000 interval=50us rtsig=SIGRTMIN+0 (=34) pid=12345
fatal error: unexpected signal during runtime execution
...
SIGABRT: abort
PC=0x... sp=0x... 
```

The crash occurs within seconds as the 64KB probe overflows Go's 32KB sigaltstack.

## How It Works

1. **`host.c`**: Pure C program that dlopen's libgolib.so and calls `ping()` from pthread workers
2. **`fat_handler.c`**: SA_ONSTACK signal handler that probes configurable stack depth using chkstk-style `sub $0x1000,%rsp; movq $0,(%rsp)` loop
3. **Signal sender**: Fires SIGRTMIN at all worker threads every 50µs
4. **Overflow**: Handler runs on Go's 32KB sigaltstack, probe exceeds limit, crashes

## Configuration

Environment variables:
- `REPRO_PROBE_BYTES` - Stack depth to probe (default: 64KB)
- `REPRO_WORKERS` - Number of worker threads (default: 32)  
- `REPRO_INTERVAL_US` - Signal interval (default: 50µs)
- `REPRO_PROBE_MODE` - `alloca` (default) or `chkstk` probe style

## Fix Location

**File**: `src/runtime/os_linux.go:388` in Go source
**Current**: `mp.gsignal = malg(32 * 1024)`
**Proposed**: `mp.gsignal = malg(128 * 1024)` or larger

### Rationale for 128KB
- CoreCLR's own functions have 24KB chkstk prologues
- Chained handlers can stack multiple prologues
- 128KB provides comfortable margin while remaining reasonable memory cost
- Other platforms (Unix variants) also use 32KB - should be updated consistently

## Files

- `host.c` - C host program that loads Go library
- `fat_handler.c` - Stack-hungry signal handler implementation  
- `golib.go` - Minimal Go library with cgo export
- `Makefile` - Build system
- `FINDINGS.md` - Detailed experimental results and analysis
- `crash/` - Core dumps and debugging artifacts

## Related Issues

- golang/go#78883 - Original issue report
- This bug affects ANY C program that loads Go c-shared libraries and uses SA_ONSTACK signal handlers