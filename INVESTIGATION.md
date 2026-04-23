# Investigation: go#78883 — sigaltstack overflow on cgo + signal-driven runtimes

**Status:** complete
**Date:** 2026-04-23
**Issue thread:** [golang/go#78883](https://github.com/golang/go/issues/78883)

This document is the synthesised conclusion of the experiments under
`experiments/`. The `README.md` documents the reproducer; this
document documents what the bug actually is and how we proved it.

---

## TL;DR

There are **two distinct bugs** sharing the same shape (chkstk-style
overflow off a too-small SA_ONSTACK signal stack) but differing in
**which runtime installed the inadequate stack**:

| Crash class | Who owns the alt stack | Size | Go's `minitSignalStack` branch | Where the fix has to live |
|---|---|---:|---|---|
| **Plain cgo** (any C host that does not pre-install an alt stack on its threads) | **Go** (`malg(32 * 1024)` at `runtime/os_linux.go:388`) | 32 KB | install (`mp.newSigstack = true`) | **Go runtime** — bump the allocation |
| **.NET hosting Go via P/Invoke** | **CoreCLR's PAL** (per-thread `sigaltstack({size=16384})` at thread create) | 16 KB | record-only (`mp.newSigstack = false`) | **CoreCLR PAL**, or app-side shim — Go is not the runtime that installed it |

The same crash signature on the wire (raw SIGSEGV in a chkstk
prologue, no recoverable second-fault) is reachable by both paths.
The README's prior framing — "Go installs its 32 KB sigaltstack on
the TP worker" — is correct for the plain-cgo class but wrong for
the .NET class. **Ian Lance Taylor's hypothesis on the issue thread
is correct**: Go does not modify the kernel's sigaltstack on
threads that already have one — it only records bounds.
**Keith Randall's UAF hypothesis is not load-bearing for this
crash**: bumping the relevant alt stack alone, with no lifecycle
change, deterministically eliminates the failure.

---

## Background — what we already knew, and what was disputed

The original `README.md` proposed (with explicit AI-assistance
disclaimer) that the .NET host crashed because:

1. Go's `needm` installs a 32 KB sigaltstack on every TP worker.
2. CoreCLR's chained signal handler chkstk prologue probes more
   than 32 KB and walks off the bottom.
3. Either the bottom is the gsignal mapping's guard page, or it is
   memory Go has just released back to the doublemapper pool
   (UAF), depending on the exact moment.

On the issue thread, two Go runtime maintainers pushed back:

- **Ian Lance Taylor**: Go does *not* change the kernel's
  sigaltstack to one a non-Go framework installed. It only **records
  bounds** in `mp.goSigStack` for its own bounds-checking. If the
  thread already has a signal stack on entry, Go does not call
  `sigaltstack(2)` at all. Go's gsignal stack memory is freed only
  on `mexit`, which is rare for the extra Ms used by cgo.
- **Keith Randall**: Suspects a different mechanism — that CoreCLR
  installs and later *frees* its own sigaltstack while Go still
  references its bounds.

The README, Ian, and Keith give three mutually inconsistent
stories. Reading `runtime/signal_unix.go:1335`
(`minitSignalStack`), the actual code branches on whether the
thread already has an alt stack:

```go
sigaltstack(nil, &st)
if st.ss_flags&_SS_DISABLE != 0 || !iscgo {
    signalstack(&mp.gsignal.stack)   // Go installs its 32 KB
    mp.newSigstack = true
} else {
    setGsignalStack(&st, &mp.goSigStack)  // record-only
    mp.newSigstack = false
}
```

So the entire dispute reduces to: *which branch is taken on a .NET
TP worker?* If install → READMR is right. If record-only → Ian
is right and the .NET fix has to live somewhere other than Go.

We designed a falsifiable experiment matrix to settle this
empirically rather than by reading one side's analysis.

---

## What we proved

### Class A — plain cgo case: Go's 32 KB stack overflows

**Hypothesis:** when there is no pre-existing alt stack on the
calling thread, Go takes the install branch and registers its own
32 KB gsignal stack. A C runtime that fires SIGRTMIN-class signals
at threads currently in cgo, with an SA_ONSTACK handler that probes
more than ~30 KB, will overflow off the end of that 32 KB stack.

**Evidence:** experiments E6 + E5.

E6 builds a ~150-line C-only host that:

- `dlopen`s `libgolib.so` and calls `ping()` from 32 pthread workers
  in a tight loop;
- spawns a sender thread that walks `/proc/self/task/` and
  `tgkill(pid, tid, SIGRTMIN)` every 50 µs;
- installs a `SA_ONSTACK | SA_SIGINFO` handler for SIGRTMIN that
  probes `REPRO_PROBE_BYTES` of stack downward.

Probe-size sweep (5 runs each, 20 s timeout, default `alloca`-based
probe):

| Probe | Pass | Fail |
|------:|:----:|:----:|
|  4 KB | 5/5  | 0/5  |
|  8 KB | 5/5  | 0/5  |
| 16 KB | 5/5  | 0/5  |
| 24 KB | 5/5  | 0/5  |
| 28 KB | 5/5  | 0/5  |
| **32 KB** | **0/5** | **5/5** |
| 40 KB | 0/5  | 5/5  |
| 64 KB | 0/5  | 5/5  |
|128 KB | 0/5  | 5/5  |

The cliff at exactly 32 KB matches `malg(32 * 1024)` to the byte.
Handler-side instrumentation in `fat_handler.c::log_handler_entry`
confirms this — 99.8 % of signals during cgo land on
`ss_size=32768, ss_flags=SS_ONSTACK`, with handler rsp inside
`[ss_sp, ss_sp + 32 KB)` and ~30 959 bytes of headroom (Go's
sigtramp + signal handler chain consume the first ~1.8 KB).

E5 then patches `runtime/os_linux.go:388`:

```diff
-mp.gsignal = malg(32 * 1024) // Linux wants >= 2K
+mp.gsignal = malg(1024 * 1024) // EXPERIMENT(go#78883): bumped to defang chained-handler overflow
```

Rebuilds Go, rebuilds `libgolib.so`, re-runs the sweep:

| Probe | Stock 32 KB Go | Patched 1 MiB Go |
|------:|:--------------:|:----------------:|
| 16 KB | 5/5 PASS | **3/3 PASS** |
| 32 KB | 0/5 PASS | **3/3 PASS** |
| 64 KB | 0/5 PASS | **3/3 PASS** |
|128 KB | 0/5 PASS | **3/3 PASS** |
|256 KB | —        | **3/3 PASS** |
|512 KB | —        | **3/3 PASS** |
|  1 MB | —        | 2/3 PASS (1 fail at the *new* threshold) |
|  2 MB | —        | 1/3 PASS |

The threshold shifts deterministically from 32 KB to ~1 MB.
**Single variable, exact 32× scaling, no other change.** This
proves H1 (size mismatch) in isolation, and rules out any
load-bearing role for the lifecycle race (H2/H3) — the bug fires
cleanly on a stable, never-freed alt stack.

### Class B — .NET case: CoreCLR's 16 KB stack overflows

**Hypothesis:** when CoreCLR has already installed a sigaltstack on
its TP workers before they call into Go, Go's `minitSignalStack`
takes the record-only branch and never installs its own. The fault
is on **CoreCLR's** 16 KB stack, not Go's.

**Evidence:** experiments E1 + E2 + E5b.

E5b ran the .NET host with the patched 1 MiB-gsignal Go runtime
(same `libgolib.so` that fixed E6): **5/5 SIGSEGV (exit 139), no
improvement**. The size bump that defangs the C-only repro does
*nothing* for .NET. Conclusion: Go's gsignal stack is not the one
being used at crash time on these threads.

E1 builds an LD_PRELOAD shim
(`experiments/ld-trace/trace_sigaltstack.so`) that wraps libc-routed
`sigaltstack(2)` and logs each call to a binary ringbuffer. Note
the design constraint: Go on Linux issues `sigaltstack` as a direct
kernel syscall, bypassing libc, so this shim cannot see Go's
installs at all. That's exactly the property we want — anything we
*do* see is either CoreCLR, glibc, or our own shim, never Go.

Run against the .NET host (no fix), 144 records captured before
crash. The pattern is **perfectly consistent**, one pair per
CoreCLR-created thread:

```
sigaltstack(NULL, &old)         → returns ss_flags=DISABLE  [caller in libcoreclr]
sigaltstack({sp=…, size=16384}) → installs CoreCLR's 16 KB  [caller in libcoreclr, +0x9f]
```

| Records | `ss_size` | source |
|--------:|----------:|--------|
|     144 | **16 384** | libcoreclr (every install — 100 %) |
|       0 |     32 768 | Go would have, but did not — confirming the record-only branch was taken |

E2 corroborates this from the inverse direction — an in-process
probe rather than an external shim. Added `dump_sigaltstack(tag)`
to `sigstack_helper.c` and called it from C# on every TP worker
right before its first `ping()` and around individual `ping()`
calls. Set `REPRO_PROBE=1` to enable.

168 probes captured (no `REPRO_FIX`):

| Count | tag | `ss_size` | `ss_flags` |
|------:|-----|----------:|-----------:|
|     8 | `before-first` | **16 384** | 0 (enabled) |
|    80 | `before` (each ping) | 16 384 | 0 |
|    80 | `after` (each ping) | 16 384 | 0 |

Every TP worker has CoreCLR's 16 KB stack registered before it
ever calls into Go. Go calls `ping()`. Probe afterwards still
shows the same 16 KB stack. **Go literally does not touch the
sigaltstack on .NET threads** — it takes the record-only branch
and leaves the kernel's view alone.

Combined: E1 (external view) + E2 (in-process view) prove the
same fact via independent mechanisms. **The .NET crash is overflow
on CoreCLR's own 16 KB SA_ONSTACK signal stack, not Go's.**

A gdb-captured crash dump with the patched 1 MiB Go agrees:

```
Thread 80 ".NET TP Worker" received signal SIGSEGV
rip = 0x7ffff7528028   inside libcoreclr.so
rsp = 0x7fbecfb60118   in libc-heap region, NOT Go's heap (0x378f9c…)
Disassembly at rip:
    movq   $0x0, (%rsp)        ← faulting instruction
    sub    $0x1000, %rsp
    movq   $0x0, (%rsp)
    sub    $0x1000, %rsp
Stack:
  #0  libcoreclr.so + 0x… (chkstk prologue)
  #1  libcoreclr.so + 0x…
  #2  libcoreclr.so + 0x…
  #3  libcoreclr.so + 0x…
  #4  <signal handler called>     ← chained handler chain on alt stack
  #5  managed code
```

The faulting `rsp` is in libc-heap territory (`0x7fbecfb60118`),
not in Go's heap pool (`0x378f9c…`) — consistent with CoreCLR's
own per-thread sigaltstack allocations being in libc-malloc'd
pages, exactly as the README's strace observation noted. The
faulting instruction is the chkstk write `movq $0x0,(%rsp)` —
exactly what an SA_ONSTACK handler chain runs as it walks the
stack down to commit pages.

### How `REPRO_FIX=1` actually works (E7)

The existing C-side shim (`sigstack_helper.c::ensure_large_sigaltstack`)
allocates 1 MiB of memory with a `PROT_NONE` guard page at the
bottom and registers it via `sigaltstack(2)` from C# code on every
thread before its first cgo call. With `REPRO_FIX=1` the .NET
host PASSes.

E7 ran `LD_PRELOAD=trace_sigaltstack.so REPRO_FIX=1` to see exactly
how. Histogram of 211 records (host PASSes after 20 s):

| Count | `ss_size` | Source library |
|------:|----------:|----------------|
|   105 |    16 384 | libcoreclr (its install at thread create) |
|    33 | **1 048 576** | our shim (overwrites CoreCLR's 16 KB on TP workers that hit `ping()`) |
|    72 |         0 | queries (`sigaltstack(NULL, &old)`) |

The shim's install runs *after* CoreCLR's at thread creation but
*before* the first cgo call. It overwrites CoreCLR's 16 KB
registration with 1 MiB. Go's record-only branch then picks up the
1 MiB (E2 with `REPRO_FIX=1` confirms: `ss_size=1048576` on every
probe). When CoreCLR fires SIGRTMIN, the kernel switches to the
1 MiB stack, and CoreCLR's chkstk has 64× headroom.

The shim is not a Go-side fix. It is a workaround that swaps the
**CoreCLR-installed** 16 KB stack for a 1 MiB one before any signal
arrives. CoreCLR's PAL doesn't try to re-install on its own
threads after thread create, so the swap is durable.

---

## Experiments — design and rationale

The experiments were designed before any results were known so
each would be falsifiable — i.e., the design predicted what we
should see under each competing hypothesis, and the actual result
discriminates between them.

### E6 — C-only reproducer (`experiments/c-repro/`)

**Question:** does this crash require a managed runtime, or is
plain pthread + cgo + SA_ONSTACK enough?

**Design:** strip away CoreCLR. A pure-C host with pthread
workers and a stack-hungry signal handler. If it crashes the same
way as .NET, the bug is single-runtime; if not, something
CoreCLR-specific is required.

**Result:** crashes 5/5. Furthermore, the crash threshold cliff is
at exactly Go's gsignal allocation — strong evidence the C-only
path is hitting Go's stack, not anything else.

**Files:** `experiments/c-repro/host.c`, `fat_handler.c`,
`Makefile`, `FINDINGS.md`. Run via `make && LD_LIBRARY_PATH=.
REPRO_PROBE_BYTES=65536 ./host`.

### E1 — LD_PRELOAD `sigaltstack` tracer (`experiments/ld-trace/`)

**Question:** on a .NET TP worker, does Go's `minitSignalStack`
take the install branch or the record-only branch?

**Design:** wrap libc's `sigaltstack(2)` with LD_PRELOAD and log
every call. Go on Linux uses a direct syscall, so it's invisible
to the shim by design — anything we see is non-Go. If we see no
calls at all on a TP worker → Go must have installed (unobserved)
and the README is right. If we see a 16 KB CoreCLR install → Go is
in the record branch and Ian is right.

**Result:** 144/144 records show CoreCLR installing 16 KB, zero
non-CoreCLR installs. **Go is in the record branch.**

**Files:** `experiments/ld-trace/trace_sigaltstack.c` (LD_PRELOAD
shim), `decode.py` (binary log decoder), `Makefile`. Run via
`make && LD_PRELOAD=$PWD/trace_sigaltstack.so REPRO_TRACE_LOG=/tmp/log.bin
./your-host` then `python3 decode.py /tmp/log.bin`.

### E2 — In-process sigaltstack probe (`sigstack_helper.c::dump_sigaltstack`)

**Question:** independent corroboration of E1 from inside the
process.

**Design:** add a C function that reads the kernel's `sigaltstack`
state for the current thread and appends a single line to a log
file. P/Invoke from C# right before/after the cgo call.

**Result:** every probe on every TP worker shows the same 16 KB
stack before the first `ping()`, before each subsequent `ping()`,
and after each `ping()` — `ss_sp/ss_size/ss_flags` never change
across the cgo round trip. With `REPRO_FIX=1`, the same probes
show 1 MiB consistently — proving the shim's install is what Go
records.

**Activate:** set `REPRO_PROBE=1` and `REPRO_PROBE_LOG=/tmp/probe.log`.

### E5 — Single-variable Go-side size bump

**Question:** is the size of `malg()` the only ingredient for the
plain-cgo crash?

**Design:** patch one line in `runtime/os_linux.go:388`, rebuild
Go, rebuild `libgolib.so` against it, re-run the C-only sweep.
If the threshold shifts deterministically with the new size and
nothing else changes, the bug is purely size; if it persists, a
lifecycle race must be in play.

**Result:** threshold shifts from 32 KB to ~1 MB exactly (32×
scaling). Bug is purely size. Confirms H1 in isolation.

**E5b** — same patch, against the .NET host: **no improvement,
5/5 crash**. Confirms .NET isn't using Go's stack.

### E7 — Shim mechanism cross-check

**Question:** how does `REPRO_FIX=1` actually work, given Go takes
the record branch?

**Design:** run the .NET host with `REPRO_FIX=1` *and* the
LD_PRELOAD tracer, attribute each `sigaltstack` call to its
source library.

**Result:** CoreCLR installs 16 KB at thread create; the shim
installs 1 MiB on the same thread later; Go records 1 MiB. The
shim is a CoreCLR-state mutator, not a Go-state mutator.

### E3 — Patched Go runtime with branch-decision logging (skipped)

**Question:** which branch does `minitSignalStack` take, from
Go's own perspective?

**Why skipped:** E1 and E2 already prove the answer with
*two independent* mechanisms. E3 would be a third confirmation
costing several hours of Go runtime instrumentation work for
zero new information. Its other potential value — checking
whether `mexit` ever fires on extra Ms (relevant to a possible
UAF) — is also moot, because E5 demonstrates the bug fires
cleanly on a stable, never-freed stack: a UAF is not necessary
to explain the crash.

### E4 — Crash-time chained SIGSEGV handler (inconclusive)

**Question:** what does the kernel think the alt stack is at the
exact moment of crash?

**Why inconclusive:** the handler was installed via constructor
priority 101, but both Go's `c-shared` init and CoreCLR's PAL init
register their own SIGSEGV handlers later, overwriting ours. The
data this experiment would have collected is largely redundant
with the gdb crash dump (which already showed `rsp` in libc-heap
territory, consistent with CoreCLR's allocation pattern). Could be
salvaged by re-arming our handler post-init, but the marginal value
is low.

---

## What this changes about the original analysis

Three claims in the original `README.md` need correction:

1. **"`needm` installs Go's 32 KB sigaltstack on the thread"** —
   true for the C-only / plain-cgo case, **false for .NET**.
   On .NET TP workers, CoreCLR has already installed a 16 KB
   stack, and Go takes the record-only branch instead.

2. **"Both ingredients are required — removing either makes the
   crash disappear"** (referring to cgo + signal sender) — true
   in spirit, but the actual chain is more specific: removing
   cgo means CoreCLR's own activation signals don't have a Go
   thread to land on while it's vulnerable; removing the sender
   means no signal is being delivered. Neither change is about
   Go's sigaltstack lifecycle.

3. **"Go's 32 KB sigaltstack is too small for CoreCLR's handler"
   (proposed fix #1)** — correct claim, but the **fix doesn't
   apply to the .NET case** because Go's stack is not what's in
   use there. Fix #1 is still the right Go-side change for the
   plain-cgo class — it just won't fix `repro-dotnet`.

The lifecycle-race fix proposals (#2, #3, #4) are not necessary
to fix either crash class as observed. They may still be defensive
improvements but no measured failure mode requires them.

---

## Recommended fixes

Two independent, both legitimate, neither a substitute for the
other.

### Aside — what the "right" alt-stack size actually is

This is where the analysis is least supported by external sources.
What we have:

| Source | Value | Note |
|---|---:|---|
| Linux `bits/sigstack.h` (legacy) | `MINSIGSTKSZ=2048`, `SIGSTKSZ=8192` | Static defines, predate AVX/AMX — undersized for modern signal frames |
| `getauxval(AT_MINSIGSTKSZ)` (glibc 2.34+) | **1 776** on this box | Kernel-reported minimum for register-state restore; per-CPU-feature |
| Go runtime, every Unix `osXXX.go` | **`malg(32 * 1024)`** | Universal across Linux, AIX, Darwin, Solaris, NetBSD, FreeBSD, Dragonfly, WASM, Plan9. **Comments document only the platform minimum**, never the 32 KB rationale. No git log entry explains the choice. |
| libcoreclr.so 10.0.6 disassembly (this box) | **24 KB** max single-function chkstk prologue | 218 probe prologues total: 96×4 KB, 7×8 KB, 3×12 KB, 16×16 KB, 1×20 KB, 5×24 KB. Multiple of these can stack in a single signal-handling chain. |
| README §"Underlying cause" | 8-page (32 KB) overshoot below alt stack | The post-mortem observation. Implies CoreCLR's *cumulative* chain reaches at least 32 KB past where it landed. |
| **Direct measurement** | — | We have **not** measured CoreCLR's actual signal-handler chain depth. We measured only "16 KB is insufficient" (the .NET crash) and "32 KB is insufficient under our synthetic 64 KB probe" (the C-only repro). |

**No public source we found documents a specific minimum for hosting
CoreCLR's signal-handler chain on a foreign sigaltstack.** The
dotnet/runtime PAL source (`src/coreclr/pal/src/exception/signal.cpp`
upstream, not on this box) is the place that explicit number would
live; reading that source would let us replace the back-of-envelope
estimate below with a number CoreCLR maintainers would recognise.

The same caveat applies in reverse: any size we pick on the Go side
is "more than what we measured was insufficient", not "demonstrably
sufficient for all chained handlers in the wild". A C runtime that
links against a JIT or a managed-exception dispatcher (.NET, Mono,
JVM, Wasmtime, V8) can in principle have a chkstk chain of any
depth; the only fully safe bound is "as much VA as you're willing
to pin".

### 1. Go runtime — for the plain-cgo class

`src/runtime/os_linux.go:388`:

```diff
-mp.gsignal = malg(32 * 1024) // Linux wants >= 2K
+mp.gsignal = malg(64 * 1024) // raised to accommodate chained C-runtime handlers
```

**Honest justification for 64 KB**, in decreasing order of strength:

1. **Lower bound from disassembly**: libcoreclr.so 10.0.6 has
   single functions whose chkstk prologues alone consume 24 KB.
   Plus the kernel's signal trampoline and Go's own sighandler
   chain (~2 KB). 32 KB is therefore insufficient by inspection;
   anything ≥ ~32 KB + 24 KB ≈ 56 KB clears at least the
   single-function ceiling for this build of CoreCLR.

2. **Lower bound from a direct measurement against the C-only
   repro at 32 KB → 1 MiB Go-side bump**: doubled, doubled, and
   doubled — every step short of the new threshold passed cleanly.
   So *any* increase from 32 KB delays the cliff in proportion.

3. **What we did not measure**: CoreCLR's full chained chkstk
   depth across stacked frames during a signal dispatch. The
   24 KB max-single-function number is a lower bound on the
   chain, not the actual chain depth.

64 KB is therefore "comfortably above the worst single-function
prologue we measured but well within reasonable VA cost" — not
"demonstrably sufficient for the entire chain". 128 KB is safer
on the same evidence; 1 MiB has the most margin and was the
size we tested end-to-end. A Go runtime maintainer reviewing
this should pick a value with input from CoreCLR/JVM/etc.
maintainers (or read those PAL sources) rather than rubber-
stamping our number.

This fix does not regress the .NET case (Go's stack is unused
there anyway), and it doesn't change any visible semantics.
Other Unix platforms also allocate 32 KB; a Linux-specific
bump is justified because Linux is by far the most common
target for `c-shared` builds, but the same argument applies on
any OS where Go is hosted by a chkstk-heavy C runtime.

### 2. CoreCLR PAL — for the .NET class

After reading `dotnet/runtime` PAL source
(`src/coreclr/pal/src/thread/thread.cpp` and
`src/coreclr/pal/src/exception/signal.cpp`), we now have a clear
picture of where the install lives, why it's 16 KB, and what the
clean fix looks like.

#### Why it's 16 KB today (`thread.cpp:2164` `EnsureSignalAlternateStack`)

```cpp
// src/coreclr/pal/src/thread/thread.cpp:2184
int altStackSize = SIGSTKSZ
                 + ALIGN_UP(sizeof(SignalHandlerWorkerReturnPoint), 16)
                 + GetVirtualPageSize();
#ifdef HAS_ADDRESS_SANITIZER
// Asan also uses alternate stack so we increase its size on the SIGSTKSZ * 4
// that enough for asan
altStackSize += SIGSTKSZ * 4;
#endif
altStackSize = ALIGN_UP(altStackSize, GetVirtualPageSize());
```

With glibc `SIGSTKSZ = 8192`, `SignalHandlerWorkerReturnPoint`
including a `CONTEXT` (~2.7 KB on AMD64 with AVX-512 register
state), and a 4 KB page guard, the page-aligned total is
**16 384 bytes** — exactly what we observe via E1.

The existing comment at `thread.cpp:2182` is the precedent: *"We
include the size of the SignalHandlerWorkerReturnPoint in the
alternate stack size since the context contained in it is large
and the SIGSTKSZ was not sufficient on ARM64 during testing."*
They already had to expand once.

#### Why the activation handler in particular overflows it

CoreCLR's signal handlers split into two camps:

- **`sigsegv_handler` / `sigfpe_handler` / `sigbus_handler` / `sigill_handler`**
  (`signal.cpp:633` `SwitchStackAndExecuteHandler`): these explicitly
  *switch off* the alt stack via the arch-specific
  `ExecuteHandlerOnCustomStack` — they alloca a
  `SignalHandlerWorkerReturnPoint` on the alt stack, capture context,
  then **rsp-switch back to the interrupted thread's normal stack**
  via `setcontext`, and continue handling there. The alt stack is
  used only as a trampoline. They never need more than the entry
  trampoline.

- **`inject_activation_handler` (SIGRTMIN, the one we crash on)**
  (`signal.cpp:931`): does **not** switch off. Allocates a
  `CONTEXT winContext` on the alt stack (line 946 — ~2.7 KB on
  AMD64-AVX-512), captures registers, then calls
  `InvokeActivationHandler(&winContext)` (line 966) which calls
  `g_activationFunction(pWinContext)` (line 916) — a runtime hook
  that can do GC-suspend bookkeeping, Hijack thread re-targeting,
  tiered-JIT redispatch, etc. **All of that runs on the 12 KB
  usable area of the per-thread alt stack.**

`g_activationFunction` is set by managed-runtime code outside the
PAL; its call depth is essentially unbounded from the PAL's
perspective. Empirically (from our libcoreclr.so 10.0.6
disassembly): individual functions in the post-handler chain have
chkstk prologues up to 24 KB, several stacked frames easily exceed
12 KB cumulative.

Compare with the *separate* stack-overflow handler stack
(`signal.cpp:227`):

```cpp
int stackOverflowStackSize = ALIGN_UP(sizeof(SignalHandlerWorkerReturnPoint), 16)
                           + 9 * 4096;
```

CoreCLR allocates **9 pages (36 KB) + 1 page guard for the SO case
specifically** — a process-wide, single-use stack. So the
maintainers already know 16 KB is not enough for a substantial
signal-handler call chain, and have a 9-page baseline for the case
where they do need depth.

#### Recommended CoreCLR fix — extend the existing pattern

The minimal, smallest-blast-radius patch is to expand the
per-thread alt stack the same way the ASAN branch already does.
Single-line change at `thread.cpp:2184`:

```diff
-int altStackSize = SIGSTKSZ + ALIGN_UP(sizeof(SignalHandlerWorkerReturnPoint), 16) + GetVirtualPageSize();
+// SIGSTKSZ alone is too small for inject_activation_handler's
+// chain (g_activationFunction can call arbitrarily-deep runtime
+// code). Mirror the +SIGSTKSZ*4 expansion the ASAN branch below
+// already uses. Net cost: ~32 KB additional VA per managed thread.
+int altStackSize = SIGSTKSZ + (SIGSTKSZ * 4)
+                 + ALIGN_UP(sizeof(SignalHandlerWorkerReturnPoint), 16)
+                 + GetVirtualPageSize();
```

With glibc on x86-64 this raises the alt stack from 16 384 to
**~49 152 bytes** (12 KB usable → ~44 KB usable), comparable to
the existing 9-page stack-overflow stack. ~32 KB additional pinned
VA per managed thread.

#### Alternative — architectural fix

The cleaner fix mirrors what `sigsegv_handler` already does: have
`inject_activation_handler` also call `SwitchStackAndExecuteHandler`
to drop back to the interrupted thread's normal stack before
invoking `g_activationFunction`. The interrupted thread is, by
construction (`g_safeActivationCheckFunction` returned true), at a
safe point with its normal stack in a usable state. This eliminates
the unbounded-depth-on-alt-stack class of problem entirely, not just
the current symptom.

Sketch (in `signal.cpp:931` `inject_activation_handler`):

```cpp
if (g_safeActivationCheckFunction(CONTEXTGetPC(&winContext)))
{
    // Existing path:
    //   InvokeActivationHandler(&winContext);
    // Proposed: get off the alt stack first, mirroring sigsegv_handler.
    size_t targetSp = CONTEXTGetSP(&winContext) - kActivationStackGuard;
    SwitchStackAndExecuteHandler(/* synthetic code = */ ACTIVATION_HANDLER_FLAG,
                                 siginfo, context, targetSp);
}
```

This is more code, more review, and changes activation-handler
semantics — wouldn't merge in a hotfix release. But it's the right
long-term shape.

#### Why the existing `EnsureSignalAlternateStack` "already-installed" check doesn't help

`thread.cpp:2178`:

```cpp
st = sigaltstack(NULL, &oss);
if ((st == 0) && (oss.ss_flags == SS_DISABLE))
{
    // ... allocate and install ...
}
```

CoreCLR only installs its own alt stack if the thread has none.
This *would* let an external runtime (Go, a C application, the
shim in `sigstack_helper.c`) pre-install a larger stack and
have CoreCLR honour it — **except that on CoreCLR-created
threads, this code runs at thread creation, before any other
runtime can install anything**. So the existing check helps only
for the converse case (CoreCLR called from a non-CoreCLR-owned
thread that already had an alt stack), which isn't the .NET host
shape.

The C# shim
(`sigstack_helper.c::ensure_large_sigaltstack`) works *despite*
this — it overwrites CoreCLR's already-installed 16 KB
unconditionally with `sigaltstack(&new_1MB, NULL)`, ignoring the
`oss` it would have received. CoreCLR's PAL doesn't try to
re-install once the thread is running, so the swap is durable for
the thread's lifetime.

#### What the Go side cannot do

For the .NET case Go is irrelevant. Go takes the record-only
branch (E1+E2 prove this); the alt stack on the kernel's books is
the one CoreCLR installed; Go's `malg(32 * 1024)` size doesn't
participate. The Go-side fix #1 is correct for the plain-cgo class
**only**. There is no Go-side change that fixes the .NET case.

### 3. App-side workaround — already ships in this repo

`sigstack_helper.c::ensure_large_sigaltstack` + the C# wrapper
documented in `README.md` §"How to apply the shim in your own
.NET project". It overwrites CoreCLR's 16 KB with 1 MiB on every
P/Invoke-reaching thread before the first cgo call. Works
because CoreCLR's PAL doesn't reinstall after thread create, so
the swap is durable for the thread lifetime.

For uplink.NET-shaped applications this is the practical
near-term fix while waiting for Go-side and CoreCLR-side
upstream work.

---

## Reproduction

### Plain-cgo class (C-only)

```bash
cd experiments/c-repro && make && cd ../..
LD_LIBRARY_PATH=. REPRO_PROBE_BYTES=65536 ./experiments/c-repro/host
# Expect: SIGABRT (134) within seconds. Threshold cliff at exactly
# 32 KB matches Go's malg(32*1024) at runtime/os_linux.go:388.
```

To get raw SIGSEGV (matching .NET's exit code) instead of Go's
panic:

```bash
LD_LIBRARY_PATH=. REPRO_PROBE_MODE=chkstk REPRO_PROBE_BYTES=65536 \
    ./experiments/c-repro/host
```

To verify the size-bump fix on a patched Go:

```bash
# patch ~/Code/Go/go/src/runtime/os_linux.go:388 → malg(1024 * 1024)
cd ~/Code/Go/go/src && ./make.bash && cd -
GOROOT=~/Code/Go/go CGO_ENABLED=1 ~/Code/Go/go/bin/go build \
    -buildmode=c-shared -o libgolib.so golib.go
# Re-run the sweep: passes through 32 KB, 64 KB, 128 KB, ...
# new threshold cliff is at ~1 MiB.
```

### .NET class

```bash
./run.sh                                  # build + run until crash, no fix
./run.sh fix                              # build + run with REPRO_FIX=1
./run.sh gdb                              # capture core at crash
```

To trace CoreCLR's sigaltstack calls:

```bash
cd experiments/ld-trace && make && cd ../..
LD_LIBRARY_PATH=. \
    LD_PRELOAD=$PWD/experiments/ld-trace/trace_sigaltstack.so \
    REPRO_TRACE_LOG=/tmp/sigaltstack.bin \
    ./bin/Release/net10.0/repro-dotnet
python3 experiments/ld-trace/decode.py /tmp/sigaltstack.bin
```

To probe sigaltstack state from inside C# around each `ping()`:

```bash
LD_LIBRARY_PATH=. REPRO_PROBE=1 REPRO_PROBE_LOG=/tmp/probe.log \
    ./bin/Release/net10.0/repro-dotnet
sort -u /tmp/probe.log | head
```

---

## File guide

| Path | Purpose |
|------|---------|
| `README.md` | The reproducer's user-facing docs and the original analysis (with corrections noted in this document). |
| `INVESTIGATION.md` | This document — synthesised conclusions from the experiments. |
| `experiments/c-repro/` | E6: pure-C host that reproduces the plain-cgo class. `FINDINGS.md` contains the raw per-experiment writeup. |
| `experiments/ld-trace/` | E1: LD_PRELOAD shim that traces every libc-routed `sigaltstack(2)` call, plus a Python decoder. |
| `sigstack_helper.c` | C-side shim. `ensure_large_sigaltstack` is the .NET-class workaround (#3 above). `dump_sigaltstack` is E2's in-process probe. The constructor SIGSEGV handler is E4 (inconclusive). |
| `Program.cs` | .NET host. `REPRO_FIX=1` enables the workaround; `REPRO_PROBE=1` enables E2's probes. |
| `golib.go` | Trivial cgo export for the test surface area. |
