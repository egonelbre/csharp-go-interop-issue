# Investigation Tools

This directory contains tools and artifacts used during the investigation of golang/go#78883. These tools helped prove the underlying mechanisms and distinguish between the two bug classes.

## Tools

### ld-preload-tracer/

**Purpose**: Trace all `sigaltstack(2)` syscalls to understand who installs what sigaltstack when.

**Key insight**: Go on Linux uses direct syscalls, so this tracer only sees libc-routed calls (CoreCLR, glibc, app code). Go's installs are invisible by design.

**Files**:
- `trace_sigaltstack.c` - LD_PRELOAD wrapper that intercepts `sigaltstack()`
- `decode.py` - Python script to decode binary trace logs
- `Makefile` - Build tracer shared library
- `runs/` - Captured traces from different test scenarios

**Usage**:
```bash
cd ld-preload-tracer
make
LD_PRELOAD=$PWD/trace_sigaltstack.so REPRO_TRACE_LOG=/tmp/trace.bin your-program
python3 decode.py /tmp/trace.bin
```

**Key findings**:
- 144/144 records show CoreCLR installing 16KB stacks
- 0 records show Go installing 32KB stacks (invisible to tracer)
- Proves Go takes "record-only" branch on .NET ThreadPool workers

### test-artifacts/

**Purpose**: Preserved logs and core dumps from key experiments.

**Files**:
- `probe-fix.log` - Execution with `REPRO_FIX=1` (shim active)
- `probe-nofix.log` - Execution without fix (crashes)
- `crash/` - Core dumps and GDB analysis from crashes

**Key findings**:
- With fix: sigaltstack consistently shows 1,048,576 bytes (1MB shim)
- Without fix: sigaltstack shows 16,384 bytes (CoreCLR) → crash
- Core dumps confirm crash location in chkstk-style stack probes

## Investigation Summary

These tools were used to prove the following critical facts:

### Experiment E1 (ld-preload-tracer)
**Question**: On .NET ThreadPool workers, does Go's `minitSignalStack` install its own 32KB stack or record CoreCLR's existing stack?

**Answer**: Record-only. Tracer shows 144 CoreCLR installs of 16KB, zero Go installs.

**Implication**: Go is not the component that needs fixing for the .NET case.

### Experiment E2 (in-process probes)
**Question**: What does the kernel think the sigaltstack is before/after Go calls?

**Answer**: Consistently 16,384 bytes before and after `ping()`. Go doesn't modify it.

**Implication**: Confirms E1 from inside the process.

### Experiment E5b (patched Go runtime)
**Question**: Does bumping Go's `malg(32*1024)` to `malg(1024*1024)` fix the .NET crash?

**Answer**: No. 5/5 crashes persist with 1MB Go gsignal stack.

**Implication**: .NET crash is not on Go's stack. It's on CoreCLR's.

### Experiment E7 (shim effectiveness)
**Question**: How does the C# workaround (`REPRO_FIX=1`) actually work?

**Answer**: Shim installs 1MB sigaltstack before first cgo call. Go records it. Subsequent signals use the 1MB stack.

**Implication**: Validates workaround mechanism. Also confirms Go's record-only behavior.

## Tool Usage During Investigation

### Systematic Testing Flow
1. **Hypothesis**: Go installs 32KB on .NET workers, CoreCLR's handlers overflow it
2. **Test E1**: LD_PRELOAD tracer to see who installs what → CoreCLR installs 16KB
3. **Test E2**: In-process probes to confirm → 16KB before/after Go calls  
4. **Test E5b**: Patched Go with 1MB stack → still crashes
5. **Conclusion**: CoreCLR's 16KB is the bottleneck, not Go's 32KB

### Proof Strategy
- **External observation** (LD_PRELOAD): What does the system see?
- **Internal observation** (in-process probes): What does the app see?
- **Controlled mutation** (patched Go runtime): Does changing X fix it?
- **Mechanism validation** (shim tracing): How does the workaround work?

## Reproducing the Investigation

To replay the investigation yourself:

### Step 1: Baseline crash
```bash
cd ../dotnet-go-reproducer
./run.sh  # Should crash with SIGSEGV
```

### Step 2: Trace sigaltstack calls
```bash
cd ld-preload-tracer
make
LD_PRELOAD=$PWD/trace_sigaltstack.so REPRO_TRACE_LOG=/tmp/trace.bin ../dotnet-go-reproducer/bin/Release/net10.0/repro-dotnet
python3 decode.py /tmp/trace.bin | head -20
```

### Step 3: In-process observation
```bash
cd ../dotnet-go-reproducer
REPRO_PROBE=1 REPRO_PROBE_LOG=/tmp/probe.log ./run.sh
sort -u /tmp/probe.log
```

### Step 4: Validate workaround
```bash
REPRO_FIX=1 ./run.sh  # Should pass
```

## Files Generated

When using these tools, you'll generate:
- `/tmp/trace.bin` - Binary sigaltstack trace log
- `/tmp/probe.log` - In-process sigaltstack state dumps
- Core dumps in crash directories (if crashes occur)

## Historical Context

This investigation was prompted by the original crash report in golang/go#78883. The initial hypothesis was that Go's 32KB sigaltstack was too small. These tools proved that was only half the story - there are actually two separate bugs in two different runtimes.

The tools in this directory represent the "how we proved it" artifacts that support the conclusions in `/INVESTIGATION.md`.