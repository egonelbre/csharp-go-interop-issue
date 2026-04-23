# Combined .NET + Go Reproducer

This directory contains the combined reproducer that demonstrates the sigaltstack overflow bug when .NET hosts Go code via P/Invoke. This was the original reproducer that led to the discovery of the underlying issues.

## The Bug

When .NET applications P/Invoke into Go c-shared libraries, a race condition occurs between:
1. **CoreCLR's PAL**: Installs 16KB sigaltstack on ThreadPool workers
2. **Go's needm**: Records (but doesn't replace) existing sigaltstacks on non-Go threads
3. **Signal handlers**: Can overflow the inadequate 16KB stack when chained

This reproducer demonstrates the actual crash scenario that affects real-world applications.

## Two Bug Classes Demonstrated

This reproducer can hit either of two distinct bugs depending on threading:

1. **CoreCLR PAL bug** (primary): ThreadPool workers have CoreCLR's 16KB alt stack → overflow
2. **Go runtime bug** (secondary): Fresh threads without pre-existing alt stack get Go's 32KB → still overflow with deep handlers

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

1. SIGRTMIN fired at ThreadPool worker
2. Worker currently executing Go code via P/Invoke
3. Signal delivered on CoreCLR's 16KB alt stack
4. Go's signal handler + CoreCLR's signal chain exceeds 16KB
5. Stack overflow → SIGSEGV

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