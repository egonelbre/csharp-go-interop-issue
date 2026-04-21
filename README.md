# .NET + Go cgo sigaltstack-race reproducer

> **AI-assisted content.** This reproducer, the analysis below, and the
> proposed-fix section were produced in collaboration with an AI coding
> assistant (Claude). The reproducer reliably crashes on the developer
> box it was authored on (3/3 SIGSEGV) and the core-dump evidence is
> reproducible, but the prose interpretation — register-state
> explanations, Go-runtime code references, fix proposals — is
> AI-generated and not independently reviewed by a Go runtime or
> CoreCLR maintainer. Verify claims against the code before acting on
> them.

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

## Prerequisites

| Tool            | Tested version | Required for                    |
| --------------- | -------------- | ------------------------------- |
| Go              | 1.25.3, 1.26.2 | `go build -buildmode=c-shared`  |
| .NET SDK        | 10.0.106       | `dotnet build` / run            |
| gcc / glibc dev | any recent     | cgo (`CGO_ENABLED=1`) linking   |
| gdb             | 15.x           | only for `./run.sh gdb`         |

### Install on Ubuntu 24.04

```bash
# gcc + headers + gdb
sudo apt update
sudo apt install -y build-essential gdb

# Go (snap channel tracks latest; use --channel=1.25/stable if you want
# to match CI exactly, or grab a tarball from https://go.dev/dl/)
sudo snap install go --classic

# .NET SDK 10 — Ubuntu 24.04 has it in the default archive
sudo apt install -y dotnet-sdk-10.0
# If your release doesn't have dotnet-sdk-10.0 yet, use Microsoft's feed:
#   https://learn.microsoft.com/dotnet/core/install/linux-ubuntu

# Sanity check
go version && dotnet --version && gcc --version | head -1 && gdb --version | head -1
```

To match CI exactly (Go 1.25), install alongside the snap:

```bash
mkdir -p ~/sdk
curl -sSL https://go.dev/dl/go1.25.3.linux-amd64.tar.gz \
    | tar -C ~/sdk -xz && mv ~/sdk/go ~/sdk/go1.25
# then invoke as ~/sdk/go1.25/bin/go (or export PATH=$HOME/sdk/go1.25/bin:$PATH)
```

Go 1.25 and 1.26 both reproduce the crash identically; the race is not
fixed in 1.26.

## Files

| File                  | Purpose                                                 |
| --------------------- | ------------------------------------------------------- |
| `golib.go`            | Trivial cgo export: `ping() int { return 42 }`.         |
| `go.mod`              | Go module declaration for `golib.go`.                   |
| `Program.cs`          | .NET host — P/Invokes `ping`, fires signal 34 in loop.  |
| `repro-dotnet.csproj` | .NET 10 console app project.                            |
| `run.sh`              | Build + run helper.                                     |

## What it does

1. Drives the Go `ping()` function (from the co-located `libgolib.so`)
   on 32 concurrent `Task.Run` workers, 1 000 000 calls each. Every call
   causes Go to `needm`/`dropm` an M thread on the .NET TP Worker, which
   re-registers Go's 32 KB sigaltstack on the thread.
2. Starts a dedicated .NET `Thread` that enumerates every TID via
   `Process.GetCurrentProcess().Threads` and sends **kernel signal 34**
   via `tgkill(2)` every 50 µs. Signal 34 is what the real CI crashes
   show a sibling thread sending, and it's what CoreCLR's own GC/JIT
   coordination fires at managed threads under normal operation (see
   "Which signal, and who sends it?" below).

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

Tuning env vars (all optional):

| Var                 | Default   | Effect                            |
| ------------------- | --------- | --------------------------------- |
| `REPRO_WORKERS`     | 32        | Parallel .NET worker tasks        |
| `REPRO_ITERATIONS`  | 1 000 000 | `ping()` calls per worker         |
| `REPRO_INTERVAL_US` | 50        | activation-signal send interval   |

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

## Which signal, and who sends it?

The strace output in the CI crash investigation labelled the
triggering signal `SIGRT_2`. That label is strace's convention —
strace numbers RT signals from the **kernel** `SIGRTMIN` (= 32), and
glibc reserves two of those (`SIGCANCEL` for pthread cancellation on
32, `SIGSETXID` for cross-thread setuid/setgid synchronisation on 33).
So:

| Label      | Kernel # | Who owns it                                  |
| ---------- | -------- | -------------------------------------------- |
| `SIGRT_0`  | 32       | glibc pthread (`SIGCANCEL`)                  |
| `SIGRT_1`  | 33       | glibc (`SIGSETXID`)                          |
| `SIGRT_2`  | **34**   | glibc's public `SIGRTMIN`                    |

Kernel signal 34 is the first RT signal userspace can freely use —
which is why glibc exposes it as its public `SIGRTMIN`.

**CoreCLR's PAL on Linux claims signal 34 for `INJECT_ACTIVATION_SIGNAL`.**
It's used for:

- **GC thread suspension.** Before a GC can scan managed stacks it
  needs every managed thread parked at a safe point. It sends
  `SIGRTMIN` to each target; the handler parks the thread immediately
  (if at a safe point) or flags it to park at the next safe point.
- **Tiered-JIT re-dispatch.** When a hot method is recompiled, other
  threads are activated via the same signal to pick up the new code
  address.
- **Debugger break / `Thread.Interrupt`-style cooperative interrupts.**

See `src/coreclr/pal/src/exception/signal.cpp` → `SEHInitializeSignals`
in the dotnet/runtime source (the activation handler) and
`InjectActivationInternal` (the `pthread_kill(thread, SIGRTMIN)` call
site).

So in the **real CI crash**, the `SIGRT_2` in strace is CoreCLR
sending its own activation signal at a .NET TP Worker that happened
to be inside a cgo call to uplink-c at that moment. The investigation
doc's earlier attribution — "Go uses SIGRT_2 for cooperative
preemption scheduling" — was wrong. Go uses `SIGURG` (signal 23) for
async preemption, not any RT signal.

The reproducer in this gist fires signal 34 explicitly from a
dedicated thread so the race happens under a light synthetic load
rather than needing a full GC-heavy xunit run to reach the same code
path naturally.

## Underlying cause

Two runtimes with incompatible signal-handling assumptions sharing an
OS thread:

- **CoreCLR** installs its signal handlers with `SA_ONSTACK`. Its
  SIGSEGV/activation chain includes stack-probe prologues that walk
  down thousands of bytes before the handler can decide whether to
  ignore / translate / forward the signal.
- **Go** owns `sigaltstack` on any thread that has entered cgo. The
  per-M signal stack is **32 KB on Linux** (`malg(32 * 1024)` in
  `runtime/os_linux.go:mpreinit`) — sized for Go's own handler, not
  CoreCLR's. Go also re-registers / disables that alt stack on every
  `needm` / `dropm`, i.e. on every single cgo call on a non-Go thread.

(Strace captures from CI additionally show a 16 KB sigaltstack at
libc-heap addresses on .NET-owned threads — that's CoreCLR's own alt
stack, separate from Go's. The reproducer's core dump shows an 8-page
unmapped gap, which exactly matches Go's 32 KB gsignal region; the
16 KB one in the CI logs is CoreCLR-side.)

The race:

1. A .NET TP Worker enters Go via P/Invoke; `needm` installs Go's
   32 KB sigaltstack on the thread.
2. A sibling thread fires `SIGRTMIN+2` at it with `tgkill`.
3. Kernel delivers on whatever sigaltstack the thread has registered
   — Go's 32 KB one.
4. The signal isn't Go's to own, so Go's handler chains to CoreCLR's.
5. CoreCLR's handler prologue does a multi-page stack probe that
   needs more than 32 KB.
6. The probe walks off the end of the alt stack. Either it hits the
   guard page (SEGV_ACCERR — the CI strace signature) or an unmapped
   gap whose memory Go has already released in a concurrent
   `dropm` / `needm` cycle (SI_KERNEL, nested fault — the core dump
   signature shown above, with `rsp` 8 pages deep into unmapped VA
   right next to a freshly released `memfd:doublemapper`).
7. The kernel can't deliver a second signal while the first handler
   is still on the broken alt stack → `force_sig(SIGSEGV)` with
   `si_code=SI_KERNEL` → process killed.

Two things have to be wrong simultaneously to crash:

- **Size mismatch.** Go's 32 KB sigaltstack is too small for CoreCLR's
  handler.
- **Lifecycle race.** Go enables / disables / recycles that alt stack
  around every cgo call, so even when the size is borderline OK there
  are windows where the kernel's view of the alt stack and the memory
  it actually points at disagree.

Previous mitigation attempts did not work because they were aimed
at the wrong signal / layer:

- `GODEBUG=asyncpreemptoff=1` — turns off Go's SIGURG-based async
  preemption. But Go isn't sending the triggering signal in the first
  place; CoreCLR is, via its own `INJECT_ACTIVATION_SIGNAL` (signal
  34). Silencing Go-side preemption leaves CoreCLR's activation path
  completely untouched.
- Installing a 1 MB sigaltstack from managed code
  (`SigStackFix.EnsureOnCurrentThread`) — clobbered by Go's next
  `needm` on the same thread, which installs its own 32 KB alt stack
  via `minitSignalStack`.

## Proposed fixes (Go side)

All paths below are in a local checkout of the Go source tree.

### 1. Enlarge the gsignal stack on Linux

`src/runtime/os_linux.go:387-390`:

```go
func mpreinit(mp *m) {
    mp.gsignal = malg(32 * 1024) // Linux wants >= 2K
    mp.gsignal.m = mp
}
```

Bump to `malg(128 * 1024)` or `malg(256 * 1024)`. Addresses the
overflow half of the race and is cheap — one allocation per M, amortised
across the process lifetime. Other Unix platforms (`os_aix.go`,
`os3_solaris.go`, `os_netbsd.go`) already allocate 32 KB; giving Linux
headroom for chained C-runtime handlers is defensible because Go is
far more commonly shipped as `c-shared` on Linux than on those
platforms.

### 2. Block signals around `sigaltstack(SS_DISABLE)` in `unminitSignals`

`src/runtime/signal_unix.go:1370-1383`:

```go
func unminitSignals() {
    if getg().m.newSigstack {
        st := stackt{ss_flags: _SS_DISABLE}
        sigaltstack(&st, nil)
    } else {
        restoreGsignalStack(&getg().m.goSigStack)
    }
}
```

Wrap the `sigaltstack(SS_DISABLE)` call in a full `sigprocmask` block /
unblock so no signal can be delivered between the state flip and the
memory becoming reusable. `dropm` already `sigblock(false)`s before
calling `unminit`, but `false` leaves `_SigUnblock` signals (SIGSEGV /
SIGBUS / SIGFPE, and SIGURG when used for async preemption) free to
arrive anyway. Using `sigblock(true)` here — or an explicit full
sigset — closes that.

This narrows the race but doesn't fully close it: signal delivery is a
multi-step kernel operation and a signal "in flight" before the block
can still land. It's a meaningful improvement, not a fix.

### 3. Keep Go's sigaltstack installed for the whole OS thread lifetime

The root cause of the lifecycle race is that `needm` / `dropm` re-toggle
the sigaltstack state on every cgo round trip. Stop toggling it:

- `minitSignalStack` already has a branch for the common case where
  the thread already had an alt stack (e.g. CoreCLR's) — it just
  records it without reinstalling.
- Add a symmetric branch for the case where Go did install its own:
  install it **once** the first time the OS thread enters Go, and
  leave it registered until the OS thread terminates. Change
  `unminitSignals` to not `SS_DISABLE` when `newSigstack == true`;
  move that disable into `mexit` (or the pthread-key destructor path)
  instead.

Trade-off: ~32–256 KB of permanently pinned per-thread memory, for
ever-cgo'd threads. Eliminates the use-after-free half of the race
entirely, because the memory can't be returned to the allocator
while the kernel still thinks it's an alt stack for some thread.

### 4. Harden `mexit` — only free gsignal after confirming it's disabled

`src/runtime/proc.go:2017-2029` unconditionally `stackfree(mp.gsignal.stack)`
on `mexit`. A robust version:

1. Block all signals on the current thread.
2. `sigaltstack(&disable, &oldss)` — assert `oldss` matches
   `mp.gsignal.stack`.
3. Read back with `sigaltstack(nil, &check)` — assert
   `check.ss_flags & SS_DISABLE != 0`.
4. Only then `stackfree(mp.gsignal.stack)`.

This turns a silent UAF into a clean `throw` if the kernel ever
reports a pending alt-stack reference we didn't expect.

### 5. Place a guard page below every gsignal stack

Today Go's gsignal stack comes from `stackalloc`, which is Go's
internal stack-pool allocator — adjacent pages may be other live
allocations. An overflow corrupts them silently. Allocating gsignal
stacks with an explicit `PROT_NONE` page below via `mmap` converts
overflow into a clean `SEGV_ACCERR` at a known boundary and makes
the class of bug diagnosable from a core dump alone.

### What's the minimum viable upstream change

Fix **#1** (size bump) by itself defangs the CI crash for the .NET +
uplink-c case: CoreCLR's handler has enough room, so the probe never
reaches the region where the lifecycle race could cause a UAF. It's a
one-line patch and doesn't change any semantics.

The lifecycle race (#2, #3, #4) is the real underlying bug but needs
design discussion on golang-dev before a patch is worth writing.
Upstream-issue context that applies:

- [go#43853](https://github.com/golang/go/issues/43853) — handler
  overflows gsignal under cgo.
- [go#60007](https://github.com/golang/go/issues/60007) — signal
  delivered on wrong stack → overflow.
- [go#7227](https://github.com/golang/go/issues/7227),
  [go#14899](https://github.com/golang/go/issues/14899),
  [go#16468](https://github.com/golang/go/issues/16468) — other
  runtimes fighting Go over sigaltstack.

## Alternative fix sites

Fixing this purely in Go isn't the only option:

- **CoreCLR** could avoid chkstk-heavy paths when running on an
  externally-provided sigaltstack (detect via `sigaltstack(nil, &st)`
  at handler entry, switch to a CoreCLR-owned emergency stack before
  diving into managed signal dispatch).
- **Application code** (uplink.NET here) could keep the two runtimes'
  threads strictly disjoint — never P/Invoke from a .NET TP Worker,
  always hop to a dedicated pool of threads whose sigaltstack state
  is under application control. Expensive and fragile.
