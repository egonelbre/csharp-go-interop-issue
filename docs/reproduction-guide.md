# Step-by-Step Reproduction Guide

This guide walks you through reproducing the sigaltstack overflow bugs discovered in golang/go#78883. Choose the reproducer that matches your scenario.

## Quick Start

If you just want to see the crashes quickly:

```bash
# CoreCLR 16KB overflow (most common)
cd coreclr-pal-bug && ./run.sh

# Go 32KB overflow (C applications)  
cd go-runtime-bug && make && LD_LIBRARY_PATH=. REPRO_PROBE_BYTES=65536 ./host

# Combined case (.NET + Go)
cd dotnet-go-reproducer && ./run.sh
```

All should crash within seconds with `SIGSEGV (exit 139)`.

## Detailed Reproduction

### Scenario 1: CoreCLR PAL Bug (Pure .NET)

**When to use**: You want to demonstrate that CoreCLR's own sigaltstack is inadequate, without involving Go.

**Audience**: dotnet/runtime team, .NET developers

#### Prerequisites
- .NET 10+ runtime
- GCC
- Linux x86_64

#### Steps
```bash
cd coreclr-pal-bug

# Build C library and .NET project  
make clean && make
dotnet build -c Release

# Run reproducer
LD_LIBRARY_PATH=. ./bin/Release/net10.0/repro

# Expected output:
# [repro] CoreCLR sigaltstack overflow reproducer
# [repro] pid=12345 workers=32 probe_bytes=65536
# [probe] tp-worker-0 tid=12389 ss_sp=0x7f1234567000 ss_size=16384 ss_flags=0
# Segmentation fault (core dumped)
```

#### What's Happening
1. .NET spawns ThreadPool workers 
2. CoreCLR installs 16KB sigaltstack per worker
3. Signal sender fires SIGUSR1 at workers
4. Handler probes 64KB of stack on 16KB alt stack → overflow

#### Key Evidence
- `ss_size=16384` proves CoreCLR installed 16KB alt stack
- Crash occurs when 64KB probe exceeds 16KB limit
- No Go involved - pure CoreCLR issue

### Scenario 2: Go Runtime Bug (Pure C)

**When to use**: You want to demonstrate Go's gsignal stack overflow without .NET involvement.

**Audience**: Go team, C developers using Go libraries

#### Prerequisites  
- Go toolchain
- GCC
- Linux x86_64

#### Steps
```bash
cd go-runtime-bug

# Build Go library and C host
make clean && make

# Run with different probe sizes to find threshold
LD_LIBRARY_PATH=. REPRO_PROBE_BYTES=28672 ./host  # Should pass
LD_LIBRARY_PATH=. REPRO_PROBE_BYTES=32768 ./host  # Should fail
LD_LIBRARY_PATH=. REPRO_PROBE_BYTES=65536 ./host  # Should fail

# Expected output (crash):
# [c-repro] workers=32 iters=1000000 interval=50us
# fatal error: unexpected signal during runtime execution
# SIGABRT: abort
```

#### What's Happening
1. C host dlopen's Go c-shared library
2. pthread workers call Go `ping()` function
3. Go's `needm` installs 32KB gsignal stack per worker thread
4. Signal sender fires SIGRTMIN at workers
5. Handler probes stack on Go's 32KB alt stack → overflow at exactly 32KB

#### Key Evidence
- Clean threshold at exactly 32KB (28KB passes, 32KB fails)
- Matches Go's `malg(32 * 1024)` allocation
- Threshold shifts proportionally when Go's allocation is changed

### Scenario 3: Combined Case (Real-World)

**When to use**: You want to demonstrate the actual crash scenario that affects production applications.

**Audience**: Application developers, incident investigation

#### Prerequisites
- .NET 10+ runtime
- Go toolchain  
- GCC
- Linux x86_64

#### Steps
```bash
cd dotnet-go-reproducer

# Build everything
./run.sh build

# Run without fix (should crash)
./run.sh

# Expected output:
# [dotnet-repro] mode=signal workers=32 pid=12345
# Segmentation fault (core dumped)

# Run with workaround (should pass)  
./run.sh fix

# Expected output:
# [dotnet-repro] mode=signal workers=32 pid=12345 fix=True
# [dotnet-repro] PASS
```

#### Alternative: GC Mode
```bash
# Use CoreCLR's own GC signals instead of synthetic ones
REPRO_MODE=gc ./run.sh

# More realistic but less reliable reproduction
```

#### What's Happening
1. .NET host P/Invokes into Go c-shared library
2. Go's `needm` sees CoreCLR's existing 16KB alt stack → records it (bounds only; kernel registration unchanged)
3. `SIGRTMIN` fires (synthetic sender, or CoreCLR's own GC/tiered-JIT)
4. Signal delivered while worker is in unmanaged code → CoreCLR treats the PC as a "safe point"
5. `inject_activation_handler` runs on CoreCLR's 16KB alt stack and calls `g_activationFunction` (GC suspend / hijack / tiered JIT) **without switching off the alt stack** — chain overflows ~12KB usable → SIGSEGV

No Go signal handler is in the fault chain; the crash is inside libcoreclr. Go's role is to supply an unmanaged PC at signal time.

## Advanced Investigation

### Tracing Signal Stack Operations

See exactly who installs what:

```bash
cd investigation-tools/ld-preload-tracer
make

# Trace a crashing run
LD_PRELOAD=$PWD/trace_sigaltstack.so REPRO_TRACE_LOG=/tmp/trace.bin \
    ../../dotnet-go-reproducer/bin/Release/net10.0/repro-dotnet

# Decode the trace  
python3 decode.py /tmp/trace.bin

# Expected: All installs show 16384 bytes (CoreCLR), none show 32768 (Go)
```

### In-Process Stack State Monitoring

```bash
cd dotnet-go-reproducer

# Monitor sigaltstack state around Go calls
REPRO_PROBE=1 REPRO_PROBE_LOG=/tmp/probe.log ./run.sh

# View the logged stack states
sort -u /tmp/probe.log

# Expected: Consistent ss_size=16384 before/after Go calls
```

### Core Dump Analysis

```bash
cd dotnet-go-reproducer

# Capture core dump at crash
ulimit -c unlimited
./run.sh gdb

# When it crashes in GDB:
(gdb) bt
(gdb) info registers
(gdb) x/16i $rip-32

# Look for crash in chkstk-style prologue:
# sub $0x1000,%rsp
# movq $0,(%rsp)     <- faulting instruction
```

## Systematic Testing

### Test Matrix

| Reproducer | Go Stack | CoreCLR Stack | Expected Result | Validates |
|-----------|----------|---------------|-----------------|-----------|
| go-runtime-bug | 32KB | N/A | FAIL @ 32KB | Go runtime bug |
| coreclr-pal-bug | N/A | 16KB | FAIL @ 16KB | CoreCLR PAL bug |  
| dotnet-go-reproducer | Record-only | 16KB | FAIL @ 16KB | Combined case |
| dotnet-go-reproducer + fix | Record-only | 1MB (shim) | PASS | Workaround |

### Probe Size Sweeps

Find the exact overflow threshold:

```bash
# Go runtime bug threshold
for size in 16384 24576 28672 32768 40960; do
    echo "Testing $size bytes..."
    LD_LIBRARY_PATH=. REPRO_PROBE_BYTES=$size timeout 10s ./go-runtime-bug/host
    echo "Result: $?"
done

# CoreCLR PAL bug threshold  
for size in 8192 12288 16384 20480 24576; do
    echo "Testing $size bytes..."
    timeout 10s ./coreclr-pal-bug/bin/Release/net10.0/repro
    echo "Result: $?"
done
```

## Validation After Fixes

### Testing Go Runtime Fix

After applying the Go runtime fix (`malg(128 * 1024)`):

```bash
cd go-runtime-bug

# Rebuild with patched Go
GOROOT=/path/to/patched/go CGO_ENABLED=1 go build -buildmode=c-shared -o libgolib.so golib.go

# Test new thresholds
LD_LIBRARY_PATH=. REPRO_PROBE_BYTES=65536 ./host   # Should pass now
LD_LIBRARY_PATH=. REPRO_PROBE_BYTES=131072 ./host  # Should still fail at new limit
```

### Testing CoreCLR PAL Fix

After applying the CoreCLR PAL fix (~49KB alt stack):

```bash
cd coreclr-pal-bug

# Should pass with fixed runtime
./run.sh

# Can also test higher probe sizes
# (requires modifying C code to increase probe beyond 64KB)
```

## Environment Variants

### Different .NET Versions
```bash
# Test with different .NET versions  
docker run --rm -v $PWD:/work mcr.microsoft.com/dotnet/sdk:8.0 bash -c "cd /work/coreclr-pal-bug && ./run.sh"
docker run --rm -v $PWD:/work mcr.microsoft.com/dotnet/sdk:9.0 bash -c "cd /work/coreclr-pal-bug && ./run.sh"
```

### Different Go Versions
```bash
# Test with different Go versions
export GOROOT=/usr/local/go1.20 && cd go-runtime-bug && make clean && make && LD_LIBRARY_PATH=. ./host
export GOROOT=/usr/local/go1.21 && cd go-runtime-bug && make clean && make && LD_LIBRARY_PATH=. ./host  
```

### Different Linux Distributions
The bug reproduces consistently across:
- Ubuntu 20.04+ 
- RHEL 8+
- Alpine (with glibc)
- Debian 11+

## Troubleshooting

### "No such file" errors
Ensure `LD_LIBRARY_PATH` includes current directory:
```bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:.
```

### Crashes don't reproduce
1. Check if running under debugger (may mask signals)
2. Verify signal delivery: `strace -e trace=tgkill ./reproducer` 
3. Check system limits: `ulimit -a`
4. Try higher signal frequency: `REPRO_INTERVAL_US=10`

### Builds fail
1. Ensure development packages: `apt install build-essential golang-go dotnet-sdk-8.0`
2. Check Go CGO: `CGO_ENABLED=1 go env`
3. Verify .NET version: `dotnet --version`

### Different crash signatures
The bug can manifest as:
- `SIGSEGV` in handler prologue (most common)
- `SIGBUS` on some systems
- Heap corruption → delayed crash elsewhere
- Application hang (if handler is blocked)

All indicate the same root cause: stack overflow in signal handlers.

## Success Criteria

Reproduction is successful when:
1. **Crash occurs**: Exit code 139 (SIGSEGV) or 135 (SIGBUS)  
2. **Crash location**: In signal handler, specifically chkstk-style prologue
3. **Stack evidence**: Core dump shows RSP outside alt stack bounds
4. **Deterministic**: Crash occurs reliably within 30 seconds

Fix validation is successful when:
1. **Reproducer passes**: Previously crashing reproducer now completes
2. **Threshold shifts**: Stack probe limit increases proportionally to fix size
3. **No regression**: Normal applications continue to work