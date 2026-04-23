# Combined .NET + Go Reproducer

This directory contains the combined reproducer that demonstrates the sigaltstack overflow bug when .NET hosts Go code via P/Invoke. This was the original reproducer that led to the discovery of the underlying issues.

## The Bug

When .NET applications P/Invoke into Go c-shared libraries, CoreCLR's
own activation signal (`SIGRTMIN`, fired by GC suspension / tiered JIT)
can land on a ThreadPool worker that is currently parked inside an
unmanaged Go call. The signal runs on the 16 KB sigaltstack that
CoreCLR's PAL installed at thread creation. CoreCLR's
`inject_activation_handler` does *not* switch off the alt stack —
it calls `g_activationFunction(pWinContext)` directly on it — and
that VM-installed activation function can do GC-suspend bookkeeping,
hijack retargeting, and tiered-JIT redispatch, easily exceeding the
~12 KB usable area. The chkstk prologue walks off the bottom of the
alt stack and SIGSEGVs.

Go's role is purely incidental. Go's `needm` takes the record-only
branch on these threads (bounds tracking only; kernel-registered
alt stack is unchanged). No Go signal handler is in the fault chain.
What Go contributes is an unmanaged PC at signal time, so
CoreCLR's `g_safeActivationCheckFunction` returns true and
`inject_activation_handler` takes the full work path. A pure-managed
build can hit the same overflow if its call chain happens to be deep
enough at a safe-point interrupt — this repro just makes it reliable.

## What it demonstrates

- **CoreCLR PAL bug**: The 16 KB per-thread alt stack in
  `EnsureSignalAlternateStack` (`src/coreclr/pal/src/thread/thread.cpp:2184`)
  is too small for `inject_activation_handler`'s chain. Fix belongs in
  CoreCLR (see `INVESTIGATION.md` §2 and `DOTNET_ISSUE.md`).

- **Workaround mechanism**: The `REPRO_FIX=1` shim overwrites
  CoreCLR's 16 KB install with a 1 MiB alt stack per thread before
  the first cgo call. CoreCLR's PAL never re-installs, so the swap
  is durable.

The separate Go-runtime-side bug (32 KB gsignal overflow when Go
*does* take the install branch, i.e. no pre-existing alt stack) is
demonstrated by `go-runtime-bug/`, not this reproducer.

## Reproduction

### Prerequisites
- .NET 10+ runtime  
- Go toolchain
- GCC
- Linux x86_64

### Build and Run
```bash
./run.sh                # Build and run (expect crash)
./run.sh fix             # Run with workaround (should pass)
./run.sh gdb             # Run under debugger for analysis
```

### Modes

**Signal Mode** (default):
```bash
./run.sh
```
- Synthetic signal sender fires SIGRTMIN at ThreadPool workers
- Most reliable reproduction method
- Crashes within seconds

**GC Mode**:
```bash
REPRO_MODE=gc ./run.sh
```
- Relies on CoreCLR's own GC suspension signals
- More realistic but less reliable reproduction

### Expected Output (Crash)
```
[dotnet-repro] mode=signal workers=32 iters=1000000 interval=50µs gc=True fix=False pid=12345
[repro] Main thread before first ping(): ss_size=16384
Segmentation fault (core dumped)
```

### Expected Output (With Fix)
```bash
./run.sh fix
```
```
[dotnet-repro] mode=signal workers=32 iters=1000000 interval=50µs gc=True fix=True pid=12345
[repro] Main thread before first ping(): ss_size=1048576
[dotnet-repro] PASS
```

## How It Works

### Components

1. **`Program.cs`**: .NET host that:
   - Spawns ThreadPool workers calling into Go via P/Invoke
   - Fires SIGRTMIN signals at workers (in signal mode)
   - Forces GC pressure (in gc mode)

2. **`golib.go`**: Minimal Go c-shared library:
   - Exports `ping() int` function that returns 42
   - Triggers Go's needm/dropm on first call from non-Go threads

3. **`sigstack_helper.c`**: Workaround implementation:
   - `ensure_large_sigaltstack()`: Pre-installs 1MB alt stack before first cgo call
   - `dump_sigaltstack()`: Diagnostic function to show current stack state

### Signal Chain

1. SIGRTMIN (glibc `SIGRTMIN` = CoreCLR's `INJECT_ACTIVATION_SIGNAL`) fired at a ThreadPool worker
2. Worker is currently in unmanaged code via P/Invoke to Go
3. Kernel delivers the signal on CoreCLR's 16 KB alt stack (registered at thread creation by `EnsureSignalAlternateStack`)
4. `inject_activation_handler` runs on that alt stack, saves a `CONTEXT` (~2.7 KB), and calls `g_activationFunction(pWinContext)` — the VM-installed activation hook — **without switching off the alt stack**
5. `g_activationFunction` (GC suspend / hijack retargeting / tiered-JIT redispatch) pushes a call chain whose chkstk prologues overflow the ~12 KB usable area → SIGSEGV

No Go signal handler participates; the crash is inside libcoreclr's
own signal-handling chain. Confirmed by gdb: faulting `rip` in
libcoreclr, faulting instruction is the chkstk pattern
`movq $0,(%rsp); sub $0x1000,%rsp`, `rsp` below the kernel-registered
`ss_sp`.

## The Workaround

The `sigstack_helper.c` implements a shim that:
1. Installs 1MB sigaltstack on each thread before first cgo call
2. Go's `minitSignalStack` sees existing stack → takes "record-only" branch  
3. Kernel uses the 1MB stack for signal handlers → no overflow

### Usage in Your Code
```csharp
[DllImport("sigstack_helper")]
static extern void ensure_large_sigaltstack();

// Call once per thread before any P/Invoke to Go
ensure_large_sigaltstack();
YourGoFunction();
```

## Configuration

Environment variables:
- `REPRO_MODE` - `signal` (default) or `gc`
- `REPRO_WORKERS` - ThreadPool workers (default: 32)
- `REPRO_ITERATIONS` - Calls per worker (default: 1M)
- `REPRO_INTERVAL_US` - Signal interval (default: 50µs)
- `REPRO_FIX` - Set to `1` to enable workaround
- `REPRO_PROBE` - Set to `1` for sigaltstack diagnostics

## Files

- `Program.cs` - .NET host application
- `golib.go` - Go c-shared library
- `repro-dotnet.csproj` - .NET project configuration
- `run.sh` - Build and execution script
- `sigstack_helper.c` - Workaround implementation
- `bin/`, `obj/` - Build artifacts
- `crash/` - Core dumps and debugging data

## Real-World Impact

This bug affects any .NET application that:
- Uses P/Invoke to call Go c-shared libraries
- Runs under signal pressure (GC, profiling, debugging)
- Uses ThreadPool workers for Go calls

Examples: microservices, data processing pipelines, performance monitoring tools.

## Fix Status

- **App-side workaround**: Available in this repo (sigstack_helper.c)
- **CoreCLR fix**: Needs upstream fix in dotnet/runtime PAL  
- **Go fix**: Needs upstream fix in Go runtime (for plain cgo case)

See the investigation report in the root `INVESTIGATION.md` for detailed analysis and proposed fixes.

## Related Issues

- golang/go#78883 - Original issue report
- Upstream fix recommendations in `/docs/fix-recommendations.md`