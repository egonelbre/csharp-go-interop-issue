# .NET + Go cgo sigaltstack-race reproducer

Self-contained reproducer for a SIGSEGV in CoreCLR's signal-dispatch
chain when a .NET threadpool thread calls into Go via cgo while an
RT signal is delivered to it.

## Quick start

```bash
./run.sh            # build + run up to 10 times, stops on first crash
./run.sh build      # build only
./run.sh run        # run once
./run.sh loop 50    # run up to 50 times in a row
./run.sh gdb        # run under gdb and dump a core at crash → ./crash/core
```

Requires Go (tested 1.25.3 and 1.26.2), .NET SDK (10.0+), gcc, gdb.

## Files

| File                  | Purpose                                                 |
| --------------------- | ------------------------------------------------------- |
| `golib.go`            | Trivial cgo export: `ping() int { return 42 }`.         |
| `go.mod`              | Go module declaration for `golib.go`.                   |
| `Program.cs`          | .NET host — P/Invokes `ping`, fires SIGRT_2 in a loop.  |
| `repro-dotnet.csproj` | .NET 10 console app project.                            |
| `run.sh`              | Build + run helper.                                     |

## What it does

1. Drives the Go `ping()` function (from the co-located `libgolib.so`)
   on 32 concurrent `Task.Run` workers, 1 000 000 calls each. Every call
   causes Go to `needm`/`dropm` an M thread on the .NET TP Worker, which
   re-registers Go's 16 KB sigaltstack on the thread.
2. Starts a dedicated .NET `Thread` that enumerates every TID via
   `Process.GetCurrentProcess().Threads` and sends `SIGRTMIN+2` via
   `tgkill(2)` every 50 µs. Mirrors the strace evidence from the real
   crashes where a sibling thread fires an RT signal at a thread that
   had just installed its sigaltstack.

Both ingredients are required — removing either makes the crash
disappear on this hardware:

- Remove the cgo workers  → no Go-owned sigaltstack on the thread, no
  crash.
- Remove the signal sender → nothing lands on the alt stack, no crash.

## Manual build (if you don't want `run.sh`)

```bash
# 1. Build the tiny Go c-shared helper.
CGO_ENABLED=1 go build -buildmode=c-shared -o libgolib.so golib.go

# 2. Build the .NET host.
dotnet build -c Release

# 3. Run.
LD_LIBRARY_PATH=. ./bin/Release/net10.0/repro-dotnet
```

Both Go 1.25.3 and Go 1.26.2 crash identically — the race is not fixed
in Go 1.26.

Tuning env vars (all optional):

| Var                 | Default   | Effect                            |
| ------------------- | --------- | --------------------------------- |
| `REPRO_WORKERS`     | 32        | Parallel .NET worker tasks        |
| `REPRO_ITERATIONS`  | 1 000 000 | `ping()` calls per worker         |
| `REPRO_INTERVAL_US` | 50        | SIGRT_2 send interval             |

## Observed behaviour on the dev box

| Mode                                        | Outcome                |
| ------------------------------------------- | ---------------------- |
| Plain run                                   | SIGSEGV, 3/3 attempts  |
| `strace -f -e trace=signal`                 | PASS, 5/5 attempts     |

The crash is timing-sensitive enough that `strace`'s per-syscall
stop-and-log serialises the race away. Use `gdb --args` (or attach with
`gdb -p`) to catch the signal live instead.

## Analysing a crash

Apport silently drops cores for unpackaged binaries, and
`/proc/sys/kernel/core_pattern` needs root to override. Easiest is to
run under gdb:

```bash
gdb -batch -nx \
    -ex 'handle all nostop noprint pass' \
    -ex 'handle SIGSEGV stop print' \
    -ex 'run' \
    -ex 'gcore /tmp/repro.core' \
    -ex 'bt' \
    -ex 'info proc mappings' \
    -ex 'thread apply all bt 6' \
    -ex 'quit' \
    --args env LD_LIBRARY_PATH=. \
           ./bin/Release/net10.0/repro-dotnet
```

## What the crash looks like (core examined 2026-04-21)

- Faulting thread: `.NET TP Worker`.
- PC: CoreCLR stack-probe prologue
  (`movq $0, (%rsp)` / `sub $0x1000, %rsp`) inside libcoreclr.so — part
  of the CoreCLR signal-handling dispatch chain, called from frame
  `<signal handler called>` on top of JIT'd managed code.
- `rsp`, `rbp`, `r15` all point into an 8-page gap
  (`0x7ffff7731000 – 0x7ffff7739000`) that is **unmapped** per
  `info proc mappings`. Adjacent low side is a recently-released
  `/memfd:doublemapper (deleted)` region.

## Underlying cause

Two runtimes with incompatible signal-handling assumptions sharing an
OS thread:

- **CoreCLR** installs its signal handlers with `SA_ONSTACK`. Its
  SIGSEGV/activation chain includes stack-probe prologues that walk
  down thousands of bytes before the handler can decide whether to
  ignore / translate / forward the signal.
- **Go** owns `sigaltstack` on any thread that has entered cgo. The
  per-M signal stack is 16 KB — sized for Go's own handler, not
  CoreCLR's. Go also re-registers/disables that alt stack on every
  `needm`/`dropm`, i.e. on every single cgo call on a non-Go thread.

The race:

1. A .NET TP Worker enters Go via P/Invoke; `needm` installs Go's
   16 KB sigaltstack on the thread.
2. A sibling thread fires `SIGRTMIN+2` at it with `tgkill`.
3. Kernel delivers on whatever sigaltstack the thread has registered
   — Go's 16 KB one.
4. The signal isn't Go's to own, so Go's handler chains to CoreCLR's.
5. CoreCLR's handler prologue does a multi-page stack probe that
   needs more than 16 KB.
6. The probe walks off the end of the alt stack. Either it hits the
   guard page (SEGV_ACCERR — the CI strace signature) or an unmapped
   gap whose memory Go has already released in a concurrent
   `dropm`/`needm` cycle (SI_KERNEL, nested fault — the core dump
   signature shown above, with `rsp` 8 pages deep into unmapped VA
   right next to a freshly released `memfd:doublemapper`).
7. The kernel can't deliver a second signal while the first handler
   is still on the broken alt stack → `force_sig(SIGSEGV)` with
   `si_code=SI_KERNEL` → process killed.

Two things have to be wrong simultaneously to crash:

- **Size mismatch.** Go's 16 KB sigaltstack is too small for CoreCLR's
  handler.
- **Lifecycle race.** Go enables / disables / recycles that alt stack
  around every cgo call, so even when the size is borderline OK there
  are windows where the kernel's view of the alt stack and the memory
  it actually points at disagree.

Previous mitigation attempts did not work because each one addressed
only one half:

- `GODEBUG=asyncpreemptoff=1` — silences SIGURG preemption but leaves
  every other signal free to land on Go's alt stack.
- Installing a 1 MB sigaltstack from managed code
  (`SigStackFix.EnsureOnCurrentThread`) — clobbered by Go's next
  `needm` on the same thread.

A proper fix has to come from the Go side (bigger gsignal stack, or
don't reinstall it on every `needm`/`dropm`), from CoreCLR (don't run
chkstk-heavy paths on whatever alt stack happens to be installed), or
from keeping the two runtimes' threads strictly disjoint.
