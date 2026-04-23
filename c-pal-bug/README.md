# Pure C CoreCLR PAL Bug Reproducer

This reproducer demonstrates that CoreCLR's sigaltstack overflow bug is **not Go-specific**. It uses a trivial C library instead of Go to trigger the same crash, proving that any unmanaged P/Invoke can expose this vulnerability.

## The Bug

CoreCLR's Platform Abstraction Layer (PAL) installs a 16KB sigaltstack on each managed thread. When CoreCLR's own activation signal (`SIGRTMIN`) fires while a thread is in unmanaged code (via P/Invoke), the signal handler chain overflows this 16KB stack:

1. ThreadPool worker P/Invokes into C library
2. CoreCLR fires `SIGRTMIN` (GC suspension / tiered JIT)
3. `inject_activation_handler` runs on 16KB alt stack **without switching off**
4. `g_activationFunction` calls deep runtime code → stack overflow → SIGSEGV

## Why This Proves Go Is Incidental

This reproducer uses a **4-line C function** that just returns 42:

```c
int ping(void) {
    usleep(1); // 1 microsecond delay to widen race window
    return 42;
}
```

No cgo machinery, no Go runtime, no signal handling on the C side. Yet it crashes identically to the Go reproducer, proving:

- **The bug is in CoreCLR**, not Go
- **Any native library** called via P/Invoke can trigger it
- **The crash depth** comes from CoreCLR's own activation handler, not the callee

## Reproduction

### Prerequisites
- .NET 10+ runtime  
- GCC
- Linux x86_64

### Quick Start
```bash
./run.sh
```

Expected output:
```
[c-pal-repro] mode=signal workers=32 iters=1000000 interval=50µs
Segmentation fault (core dumped)
```

### Modes

**Signal Mode** (default):
```bash
./run.sh
```
- Synthetic `SIGRTMIN` sender fires at ThreadPool workers
- Most reliable reproduction

**GC Mode**:
```bash
./run.sh gc
```
- Relies on CoreCLR's own GC suspension signals
- More realistic but less reliable

**With Workaround**:
```bash
./run.sh fix
```
- Should pass (installs 1MB sigaltstack per thread)

## How It Works

1. **C Library** (`clib.c`):
   - Exports `ping()` that returns 42 with 1µs delay
   - No signal handling or complex logic

2. **C# Program** (`Program.cs`):
   - Spawns ThreadPool workers calling `ping()` via P/Invoke
   - Signal sender fires `SIGRTMIN` at workers every 50µs
   - Workers hit by signal while in C code → CoreCLR treats PC as "safe"
   - `inject_activation_handler` runs full activation path on 16KB stack

3. **Crash**:
   - All in `libcoreclr.so`: no C frames in the crash chain
   - Identical backtrace to Go reproducer
   - Same chkstk-style overflow signature

## Comparison With Other Reproducers

| Reproducer | Callee | Stack Owner | Mechanism | Purpose |
|------------|--------|-------------|-----------|---------|
| `go-runtime-bug/` | Go c-shared | **Go** (32KB) | Go's `malg` overflow | Proves Go runtime bug |
| `coreclr-pal-bug/` | C + synthetic handler | **CoreCLR** (16KB) | Fat `SA_ONSTACK` handler | Demonstrates 16KB insufficiency |
| **`c-pal-bug/` (this)** | **Trivial C** | **CoreCLR** (16KB) | **CoreCLR's own activation handler** | **Proves Go is incidental** |
| `dotnet-go-reproducer/` | Go c-shared | CoreCLR (16KB) | CoreCLR's activation + Go needm | Real-world scenario |

## Key Evidence

**This reproducer validates**:
- ✅ CoreCLR's 16KB alt stack is the bottleneck (not Go's 32KB)
- ✅ Any unmanaged P/Invoke can trigger the overflow
- ✅ Crash mechanism is independent of callee language/runtime
- ✅ Fix location is `src/coreclr/pal/src/thread/thread.cpp:2184`

**Crash location**: Always in `libcoreclr.so` activation handler chain, never in C library code.

## Configuration

Environment variables:
- `REPRO_WORKERS=N` — ThreadPool workers (default: 32)
- `REPRO_ITERATIONS=N` — Calls per worker (default: 1M)  
- `REPRO_INTERVAL_US=N` — Signal interval µs (default: 50)
- `REPRO_MODE=signal|gc` — Signal source (default: signal)
- `REPRO_FIX=1` — Enable workaround
- `REPRO_PROBE=1` — Log sigaltstack state

## For Upstream Teams

**dotnet/runtime team**: This reproducer is simpler than the Go version for debugging CoreCLR-side issues. No Go toolchain required, no cgo complexity.

**Other language teams**: Any language producing shared libraries callable from .NET may hit this bug. The crash is in CoreCLR, not the callee.

## Files

- `clib.c` — Trivial C library (4 lines)
- `Program.cs` — .NET host (mirrors dotnet-go-reproducer)
- `sigstack_helper.c` — Workaround implementation (copied from dotnet-go-reproducer)
- `Makefile` — Build C libraries
- `run.sh` — Build and execution script

## Real-World Impact

This affects .NET applications using:
- Image/video processing libraries (C/C++)
- Cryptography libraries (C/Rust)
- Database drivers (C)
- ML inference engines (C/C++/Rust)
- Game engines (C++)
- Any long-running native P/Invoke under signal pressure

The bug is **callee-agnostic** — it's a CoreCLR PAL sizing issue, not a specific runtime interaction.