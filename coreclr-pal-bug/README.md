# CoreCLR Sigaltstack Overflow Reproducer

This reproducer demonstrates that CoreCLR's per-thread 16KB sigaltstack is too small for realistic signal handler chains, leading to stack overflow crashes.

## The Bug

CoreCLR's Platform Abstraction Layer (PAL) installs a 16KB sigaltstack on each managed thread at thread creation time (via `EnsureSignalAlternateStack` in `src/coreclr/pal/src/thread/thread.cpp:2164`).

This 16KB allocation is insufficient for signal handler chains that probe significant stack depth. The overflow manifests as SIGSEGV crashes when:

1. A signal with an `SA_ONSTACK` handler fires on a CoreCLR thread pool worker
2. The handler's stack usage exceeds the 16KB alt stack
3. The stack probe walks off the bottom into unmapped/protected memory

## Evidence

This bug was discovered in a separate investigation of Go + CoreCLR interoperability crashes (golang/go#78883). During that investigation, detailed analysis proved:

1. **CoreCLR installs exactly 16384 bytes** per thread via `EnsureSignalAlternateStack`
2. **CoreCLR's own libcoreclr.so functions have chkstk prologues up to 24KB** (disassembly analysis)
3. **Real-world signal handler chains can exceed 16KB** when stacked frames combine

The formula for CoreCLR's alt stack size (from `thread.cpp:2184`):
```cpp
int altStackSize = SIGSTKSZ + ALIGN_UP(sizeof(SignalHandlerWorkerReturnPoint), 16) + GetVirtualPageSize();
```

With glibc `SIGSTKSZ=8192` + `SignalHandlerWorkerReturnPoint` (~2.7KB) + 4KB page = 16384 bytes total.

## Reproduction Steps

### Prerequisites
- .NET 10+ runtime
- GCC
- Linux x86_64

### Build and Run
```bash
./run.sh
```

Or manually:
```bash
make                           # Build libstack_probe.so
dotnet build -c Release        # Build C# program  
LD_LIBRARY_PATH=. ./bin/Release/net10.0/repro
```

### Expected Output
The program should crash within seconds with SIGSEGV (exit code 139):

```
[repro] CoreCLR sigaltstack overflow reproducer
[repro] pid=12345 workers=32 probe_bytes=65536
[repro] Expected: SIGSEGV crash when 65536-byte probe overflows CoreCLR's 16KB alt stack

[probe] main-thread-init tid=12345 ss_sp=(nil) ss_size=0 ss_flags=2
[repro] Installed 65536-byte probe handler for signal 10
[repro] Started signal sender (interval 50µs)
[probe] tp-worker-0 tid=12389 ss_sp=0x7f1234567000 ss_size=16384 ss_flags=0
[repro] Started 32 ThreadPool workers
[repro] Waiting for crash... (budget: 10000000 iterations per worker)
[sender] iteration 1000, signaled 33 threads
Segmentation fault (core dumped)
```

Key observations:
- **Main thread**: `ss_size=0` (no alt stack)
- **TP worker**: `ss_size=16384` (CoreCLR's 16KB alt stack)
- **Crash**: SIGSEGV when 64KB probe exceeds 16KB

## How It Works

1. **C library** (`stack_probe.c`):
   - Installs an `SA_ONSTACK` signal handler for SIGUSR1
   - Handler performs chkstk-style stack probe: `sub $0x1000,%rsp; movq $0,(%rsp)` loop
   - Default probe depth: 64KB (exceeds CoreCLR's 16KB alt stack)

2. **C# program** (`Program.cs`):
   - Spawns ThreadPool workers (which get CoreCLR's 16KB alt stacks)
   - Signal sender thread fires SIGUSR1 at all threads every 50µs
   - Workers hit by signal run the probe handler on their 16KB alt stack
   - Probe overflows → SIGSEGV

## Root Cause Analysis

### Why 16KB Is Too Small

1. **CoreCLR's own functions**: Disassembly of libcoreclr.so shows individual functions with chkstk prologues consuming up to 24KB
2. **Signal handler chains**: `inject_activation_handler` (SIGRTMIN) can call arbitrarily deep runtime code via `g_activationFunction`
3. **Third-party libraries**: Any native library using `SA_ONSTACK` handlers will overflow

### Comparison With Other Stacks

CoreCLR already recognizes 16KB is insufficient in some contexts:
- **Stack overflow handler**: 9 pages (36KB) + guard in `signal.cpp:227`
- **ASAN builds**: `SIGSTKSZ * 4` expansion (32KB+)

## Proposed Fix

### Location
`src/coreclr/pal/src/thread/thread.cpp:2184` in `EnsureSignalAlternateStack`

### Minimal Fix
Apply the same expansion the ASAN branch already uses:

```cpp
-int altStackSize = SIGSTKSZ + ALIGN_UP(sizeof(SignalHandlerWorkerReturnPoint), 16) + GetVirtualPageSize();
+// SIGSTKSZ alone is too small for inject_activation_handler's
+// chain (g_activationFunction can call arbitrarily-deep runtime
+// code). Mirror the +SIGSTKSZ*4 expansion the ASAN branch below
+// already uses. Net cost: ~32 KB additional VA per managed thread.
+int altStackSize = SIGSTKSZ + (SIGSTKSZ * 4)
+                 + ALIGN_UP(sizeof(SignalHandlerWorkerReturnPoint), 16)
+                 + GetVirtualPageSize();
```

This raises the alt stack from 16KB to ~49KB (usable: 12KB → ~44KB).

### Cost
~32KB additional virtual address space per managed thread.

### Validation
End-to-end tested on `dotnet/runtime` main at `b6421ec9f4f`
(Release build, `clr+libs` subset): unpatched **19/20 SIGSEGV** under
aggressive stress, patched **0/20** same stress. See
`../INVESTIGATION.md` §2 "Recommended CoreCLR fix" for build procedure
and measurements.

### Alternative: Architectural Fix
Have `inject_activation_handler` use `SwitchStackAndExecuteHandler` like `sigsegv_handler` already does, dropping back to the interrupted thread's normal stack before calling `g_activationFunction`.

## Environment Details

Tested on:
- .NET 10.0 runtime
- Linux 6.x x86_64  
- glibc 2.x

## Tunables

Environment variables:
- `REPRO_WORKERS=N` — number of ThreadPool workers (default: 32)
- `REPRO_ITERATIONS=N` — busy loop iterations per worker (default: 10M)
- `REPRO_INTERVAL_US=N` — signal interval in microseconds (default: 50)

## References

- Original investigation: golang/go#78883
- CoreCLR PAL source: `src/coreclr/pal/src/thread/thread.cpp`
- Signal handling: `src/coreclr/pal/src/exception/signal.cpp`