# go#78883 — cross-experiment findings

**Date:** 2026-04-23
**Target:** golang/go#78883

## Summary across all experiments

Two distinct crash classes share the same root cause (chkstk-style
overflow off a too-small SA_ONSTACK signal stack) but differ in
*who installed the inadequate stack* and therefore differ in where
the fix has to live:

| Repro | Who owns the alt stack | Stack size | Go's `minitSignalStack` branch | E5 (bump Go gsignal) fixes? |
|-------|-----------------------|-----------:|--------------------------------|:----:|
| **C-only** (`experiments/c-repro/host`) | **Go** | 32 KB | install (`newSigstack=true`) | **YES** — clean 32× threshold shift |
| **.NET** (`bin/.../repro-dotnet`) | **CoreCLR PAL** | **16 KB** | record-only (`newSigstack=false`) | **NO** — Go's stack is never used |

The maintainer disagreement on the issue thread is now resolvable
from direct evidence:

- **Ian Lance Taylor was right**: Go does not change the kernel's
  sigaltstack on threads that already have one. E1 + E2 confirm Go
  takes the record-only branch on every TP worker.
- **Keith Randall's UAF hypothesis is not load-bearing for this
  crash.** Bumping Go's stack alone (E5) fixes the C-only repro
  cleanly and deterministically — no UAF needed to explain it. The
  .NET repro isn't a UAF either; it's overflow on CoreCLR's 16 KB.
- **The README's "Go installs its 32 KB on TP workers" framing was
  wrong** for the .NET case (correct for plain cgo callers).

### Experiment matrix

| ID | What | Repro | Result | Hypothesis impact |
|----|------|-------|--------|-------------------|
| E6 | C-only repro (no managed runtime) | C-only | 5/5 SIGABRT (alloca-mode) / SIGSEGV (chkstk-mode); threshold cliff at exactly 32 KB | H1 confirmed for C-only; H4/H5 disproved for that case |
| E5 | `malg(32*1024)` → `malg(1024*1024)` in Go runtime | C-only | Threshold shifts to ~1 MiB exactly | H1 in isolation: size-bump alone closes the C-only crash |
| E5b | Same patched Go, against .NET | .NET | **5/5 SIGSEGV — no improvement** | H1 ruled out for .NET (Go's stack isn't being used) |
| E1 | LD_PRELOAD `sigaltstack` tracer | .NET | 144 records: every CoreCLR thread does `sigaltstack(NULL,&old)` then `sigaltstack({size=16384})`; both callers in libcoreclr; **zero non-CoreCLR installs** | H4 confirmed: CoreCLR pre-installs 16 KB on every thread |
| E2 | In-process probe before/after `ping()` from C# | .NET | 100% of probes show `ss_size=16384, ss_flags=0`; identical before and after — Go literally doesn't touch it | Direct corroboration of E1 via different mechanism |
| E2b | Same probe with `REPRO_FIX=1` | .NET | 100% show `ss_size=1048576, ss_flags=0`; shim install precedes Go and survives unchanged | Confirms shim mechanism: it runs first, overwrites CoreCLR's 16 KB, Go records 1 MiB |
| E7 | LD_PRELOAD trace + `REPRO_FIX=1` | .NET | 105 CoreCLR installs (16 KB), 33 shim installs (1 MiB), 0 Go installs; host PASS | Shim's install runs *after* CoreCLR's at thread create, overwrites it; PAL doesn't re-install |
| E3 | Patch Go runtime to log branch / mexit | — | **Skipped.** Question already answered: E1 + E2 prove Go takes record branch on .NET, E5 proves Go takes install branch on C-only. mexit irrelevant — no UAF in evidence. | — |
| E4 | Crash-time chained SIGSEGV handler | — | **Inconclusive.** Both Go's and CoreCLR's c-shared init overwrite our SIGSEGV handler at module load (priority 101 isn't early enough). Could be re-armed post-load but the answer is already given by E1 + E2 + the gdb crash dump. | — |

### What to write back to the issue thread

Two distinct fixes, both legitimate:

1. **Plain cgo callers (the C-only repro):** the README's fix #1 is
   correct — bump `runtime/os_linux.go:388` from `malg(32 * 1024)` to
   something that comfortably accommodates a chained C-runtime
   chkstk handler. 64 KB is the conservative minimum (handlers
   typically need 16–48 KB); 128 KB is a safe round number; 1 MiB
   was demonstrated and has 30× margin. E5 confirms this is
   sufficient when Go installs the stack.

2. **CoreCLR-hosted callers:** Go cannot fix this because Go is not
   installing the stack. The fix has to be **CoreCLR-side** (PAL
   bumps its 16 KB sigaltstack allocation — the symbolic location
   inside libcoreclr is at PAL caller PC `+0x9f`-ish into a routine
   that pairs `sigaltstack(NULL,&old)` and `sigaltstack({size=16384})`)
   **or app-side** (the existing C# shim, but with the broader
   coverage README §"How to apply the shim" describes — module
   initializer + every public P/Invoke entry point + every
   `Task.Run` lambda + every `IDisposable.Dispose`).

The Go-side change (#1) does not regress the .NET case — it just
doesn't help it. So both fixes are independently shippable.

---

# E6 — C-only reproducer findings (raw)

**Goal:** decide whether the .NET-host crash needs CoreCLR or whether a
plain pthread+cgo+SA_ONSTACK setup is enough.

## TL;DR

A pure-C host with **no managed runtime** crashes Go's c-shared
`libgolib.so` reliably (5/5) when its SA_ONSTACK SIGRTMIN handler probes
≥ 32 KB of stack. The threshold is **exactly the 32 KB hardcoded for
the gsignal stack at `runtime/os_linux.go:388`**. CoreCLR is therefore
**not required** — it was just the practical signal source. This
strongly supports H1 (size mismatch on Go's gsignal alt stack) and
makes the upstream argument single-runtime.

## Setup

- `experiments/c-repro/host.c` — `dlopen("./libgolib.so")`, calls `ping()`
  from 32 pthread workers in a 200 000-iter loop each.
- `experiments/c-repro/fat_handler.c` — `SA_ONSTACK | SA_SIGINFO` handler
  for `SIGRTMIN` (= signal 34 with glibc) that probes
  `REPRO_PROBE_BYTES` of stack downward via `__builtin_alloca` plus
  page-stride writes.
- A sender thread enumerates `/proc/self/task/` and `tgkill`s every
  other thread every 50 µs.
- Go: `go1.26.2 linux/amd64` (`/snap/go/11127`).

## Result — probe-size sweep, 5 runs each, 20 s timeout

| `REPRO_PROBE_BYTES` | Pass | Fail |
|--------------------:|-----:|-----:|
|              4 096  |  5/5 |  0/5 |
|              8 192  |  5/5 |  0/5 |
|             16 384  |  5/5 |  0/5 |
|             24 576  |  5/5 |  0/5 |
|             28 672  |  5/5 |  0/5 |
| **       32 768**   |**0/5**|**5/5**|
|             40 960  |  0/5 |  5/5 |
|             49 152  |  0/5 |  5/5 |
|             65 536  |  0/5 |  5/5 |
|            131 072  |  0/5 |  5/5 |

Threshold is 32 KB to within sweep granularity. The 28 KB pass shows
that Go's own signal-handler entry chain (sigtramp → sighandler →
adjustSignalStack → user handler) consumes ~4 KB of the 32 KB before
our probe runs.

## Does the C-only failure match the .NET failure exactly?

**Setup and root cause: yes. Proximate fault propagation: no.**

### What matches exactly

Instrumented `fat_handler` log (16 KB probe, 105 669 handler entries
captured in 8 s before clean shutdown):

| Entries | `ss_size` | `ss_flags`     | `hdlr_rsp` location | Notes |
|--------:|----------:|----------------|---------------------|-------|
| 105 455 | **32 768**| **1 (SS_ONSTACK)** | inside `[ss_sp, ss_sp+32 KB)` of Go's pool address (e.g. `0x342b79fd2000`) | Signal hit thread mid-cgo. Go's `minitSignalStack` had taken the **install** branch and registered its 32 KB gsignal stack. Kernel switched to it on signal entry. Handler had ~30 959 bytes of headroom (Go's sigtramp + sighandler chain ate the first ~1 800 bytes). |
| 214     | 0         | 2 (SS_DISABLE) | thread's normal pthread stack | Signal hit thread outside cgo (no alt stack). Handler runs on the 8 MB pthread stack — no overflow risk. |

So on the .NET-equivalent code path (signal during cgo), the kernel
delivers to **exactly** the same 32 KB Go-owned alt stack the .NET
case described, with the same ~30 KB of usable space for the chained
handler. The setup is identical.

### What differs — proximate fault

| Aspect | .NET (per README) | C-only (this repro) |
|--------|-------------------|---------------------|
| Exit signal | **SIGSEGV (139)** | **SIGABRT (134)** |
| Faulting PC | inside `libcoreclr.so` chkstk prologue (`movq $0,(%rsp)/sub $0x1000,%rsp`) on top of JIT'd managed code | inside `runtime.unwindm` at `cgocall.go:490` (Go runtime, not in our handler) |
| `<signal handler called>` frame | yes — fault is **inside** the chained handler chain on the alt stack | no — handler returned cleanly, then Go faulted later |
| `rsp` at crash | inside an unmapped 8-page gap adjacent to a freshly-released `memfd:doublemapper` | not yet captured (Apport intercepts cores; gdb serialises the race away) — likely back on a normal Go stack by the time `unwindm` runs |
| Why the SEGV escapes | kernel can't deliver a second SIGSEGV while we're mid-handler on a broken alt stack → `force_sig(SIGSEGV) SI_KERNEL` → process killed | Go's SIGSEGV handler catches the (later) SEGV normally and translates it to a `runtime.panicmem` |

### Why the fault sites differ even though the setup is the same

Both handlers walk past the bottom of the same 32 KB alt stack. But:

- **CoreCLR's chkstk prologue is a `sub $0x1000,%rsp; movq $0,(%rsp)`
  loop.** It walks rsp downward by exactly 4 KB and *commits* each
  page by writing through `(%rsp)`. When it crosses out of the alt
  stack mapping into the next-lower page, that next-lower page is
  often **unmapped** (the doublemapper hole left by a freshly
  released stack), so the very first probe through `(%rsp)` faults.
  And because the SEGV happens *inside* the chained signal handler,
  the kernel can't deliver another SEGV to a useful handler →
  process dies with `SI_KERNEL`.

- **My handler does `__builtin_alloca(64 KB)` plus a page-stride
  write loop.** `alloca` just adjusts `rsp`; the write loop touches
  pages from the (alloca-adjusted) rsp upward. If the 32 KB below
  the alt stack happens to be a *mapped* adjacent goroutine stack
  (which it usually will be — the doublemapper region around it),
  the writes **succeed** and corrupt that goroutine's data instead
  of faulting. The handler returns. Some time later, Go's cgo
  unwind path runs `unwindm`, dereferences a corrupted scheduler-sp
  pointer, and *now* SEGVs — but on a normal stack with Go's SEGV
  handler intact, so it surfaces as `runtime.panicmem` and abort,
  not raw SIGSEGV.

In other words: the C-only repro has the **same race window** and
the **same alt-stack-overflow root cause**, but it produces a
*forgiving* version of the fault because alloca-corrupts-then-fails-later
is a softer landing than chkstk-faults-immediately-on-unmapped.

### Closing the gap — chkstk-style probe matches .NET exit code

Added `REPRO_PROBE_MODE=chkstk` (see `fat_handler.c::probe_stack_chkstk`).
Replaces the soft alloca probe with an explicit
`sub $0x1000,%rsp; movq $0,(%rsp)` loop — the same shape as
CoreCLR's PAL signal-handler chkstk prologue. Walks rsp down 4 KB at
a time and *writes through (%rsp)*, so it must fault when it crosses
out of the alt stack into an unmapped neighbour.

5 runs at 64 KB probe, 32 workers, 50 µs cadence:

| Mode (probe size 64 KB) | Exit codes |
|------------------------|------------|
| `REPRO_PROBE_MODE=chkstk` | 124, **139**, 124, 124, **139** (2 raw SIGSEGV, 3 hangs) |
| default (alloca)          | **134**, **134**, 124 (3 SIGABRT-via-Go-panic) |

**Exit 139 = raw SIGSEGV — identical to the .NET case** (`run.sh
loop`'s `if [[ $rc -eq 139 ]]; then echo SIGSEGV — reproduced`). The
chkstk path lands the fault inside the signal handler, on the alt
stack, with the kernel unable to deliver a recoverable second SIGSEGV
— exactly the propagation the README's
`info proc mappings` snapshot describes.

The intermittent exit=124 (hang) outcomes are a side effect of the
probe sometimes corrupting Go runtime state in a way that leaves a
thread spinning — it confirms the probe *did* damage the alt stack's
neighbour but didn't reach an unmapped page on this particular hit.

### Bottom line for the issue thread

- **Setup is identical** between .NET and C-only: Go installs its
  32 KB gsignal alt stack on `needm` and the kernel delivers
  signals on it via SS_ONSTACK. Confirmed by the handler log
  showing 99.8 % of signals during cgo land on a 32 KB stack at
  Go-pool addresses with `~30 KB` of headroom.
- **Root cause is identical**: any SA_ONSTACK signal handler that
  probes more than ~30 KB of stack will overflow Go's gsignal
  region. CoreCLR's chkstk prologue does so; our chkstk-style probe
  does so. CoreCLR is incidental — the bug is single-runtime.
- **Exit code 139 matches** when the C-only handler uses an
  explicit `(%rsp)` chkstk pattern instead of `alloca`. With
  `alloca` the SEGV is caught later by Go and translated to
  SIGABRT (134) — same root cause, softer landing.

So: yes, the same bug; same alt-stack identity, size, and overflow
mechanism; reproducible on Linux with no managed runtime; and with
chkstk-mode the externally visible signature (raw SIGSEGV, exit 139)
also matches.

## Cross-experiment: E5 — bump Go's gsignal stack from 32 KB → 1 MiB

Patched `~/Code/Go/go/src/runtime/os_linux.go:388`:

```diff
-mp.gsignal = malg(32 * 1024) // Linux wants >= 2K
+mp.gsignal = malg(1024 * 1024) // EXPERIMENT(go#78883): bumped to defang chained-handler overflow
```

Rebuilt the toolchain (`./make.bash`), rebuilt `libgolib.so` against
it, re-ran the alloca-mode probe sweep (3 runs each):

| `REPRO_PROBE_BYTES` | Stock 32 KB Go | Patched 1 MiB Go |
|--------------------:|:--------------:|:----------------:|
| 16 384              | 5/5 PASS       | **3/3 PASS** |
| 32 768              | 0/5 PASS       | **3/3 PASS** |
| 65 536              | 0/5 PASS       | **3/3 PASS** |
| 131 072             | 0/5 PASS       | **3/3 PASS** |
| 262 144             | —              | **3/3 PASS** |
| 524 288             | —              | **3/3 PASS** |
| 1 048 576           | —              | 2/3 PASS (1 fail at the new threshold) |
| 2 097 152           | —              | 1/3 PASS |

The crash threshold **shifted from ~32 KB to ~1 MB — exact 32× match
with the gsignal allocation**. Anything below the new ceiling is
safe; anything above it crashes the same way. There is no other
ingredient: the size of `malg()` controls the failure cliff
deterministically.

This proves H1 in isolation **for the C-only case**: the Go-side
fix #1 from the README ("bump `malg(32 * 1024)`") is **sufficient**
to eliminate the C-only crash class on its own — when Go takes the
*install* branch in `minitSignalStack`.

## Cross-experiment: E5 against the .NET case — does NOT fix it

Ran the .NET host with the same patched 1 MiB-gsignal Go runtime,
5 attempts, 30 s timeout each: **5/5 SIGSEGV (exit 139)**. The
size bump that defangs the C-only repro does *nothing* for the
.NET repro.

A gdb-captured crash with the patched runtime shows:

```
Thread 80 ".NET TP Worker" received signal SIGSEGV
rip = 0x7ffff7528028  (libcoreclr.so)
rsp = 0x7fbecfb60118  (NOT in Go's heap region 0x378f9c…)
Disassembly at rip:
    movq   $0x0, (%rsp)        ← faulting instruction
    sub    $0x1000, %rsp
    movq   $0x0, (%rsp)
    sub    $0x1000, %rsp
Stack:
  #0  libcoreclr.so + 0x… (chkstk)
  #1  libcoreclr.so + 0x…
  #2  libcoreclr.so + 0x…
  #3  libcoreclr.so + 0x…
  #4  <signal handler called>     ← chained handler chain on alt stack
  #5  managed code (junk PCs)
```

So: same chkstk-overflow signature as before, but the alt stack the
fault is on is *not* in Go's heap pool. It's elsewhere — pointing to
a different alt stack altogether.

## Cross-experiment: E1 — LD_PRELOAD `sigaltstack` tracer on the .NET host

`experiments/ld-trace/trace_sigaltstack.so` wraps libc-routed
`sigaltstack(2)` calls. Run on the .NET host with patched 1 MiB Go,
captured 144 records before crash.

The pattern is **perfectly consistent**, one pair per CoreCLR thread:

```
sigaltstack(NULL, &old)         → returns ss_flags=DISABLE   [caller in libcoreclr]
sigaltstack({sp=…, size=16384}) → installs CoreCLR's 16 KB    [caller in libcoreclr, +0x9f]
```

| Records | `new->ss_size` | source |
|--------:|:--------------:|:-------|
| 144     | **16 384**     | libcoreclr (every install — 100 % of records) |
|   0     | 32 768 / 1 048 576 | Go would install — never happens because `ss_flags=0` (already-installed) on Go's `minitSignalStack` query |

(Caller PCs `0x70f5bee58ede` for the query and `0x70f5bee58f7d` for
the install — same library, +0x9f apart, in CoreCLR's PAL.
Note: Go's `sigaltstack` is a direct kernel syscall; LD_PRELOAD
cannot see Go's installs by design. Combined with the absence of
any 32 KB / 1 MiB record, this confirms Go never installs because
CoreCLR's 16 KB already there forces the record-only branch.)

### What this proves

1. **CoreCLR's PAL installs a 16 KB sigaltstack on every thread it
   creates** — TP workers, GC workers, all of them. This is *not* an
   accident of timing or a property of one thread class.
2. **On every TP worker entering Go via P/Invoke, Go's
   `minitSignalStack` takes the record-only branch**. The
   `sigaltstack(NULL, &st)` query at the top of that function
   returns ss_flags=0 (alt stack present), so the code reaches
   `setGsignalStack(&st, &mp.goSigStack)` — bounds-tracking only.
   Go does *not* call `signalstack(&mp.gsignal.stack)` on these
   threads.
3. **Ian Lance Taylor's hypothesis is correct**: Go is not changing
   the kernel's sigaltstack — only recording bounds. The README's
   "needm installs Go's 32 KB sigaltstack on the thread" is wrong
   for the .NET case (it's right for the C-only case).
4. **The .NET crash is an overflow on CoreCLR's 16 KB sigaltstack**,
   not Go's. CoreCLR's chained signal handler chkstk prologue
   (`movq $0,(%rsp); sub $0x1000,%rsp` loop) walks past the bottom
   of CoreCLR's own 16 KB region.
5. **The Go-side fix #1 in the README is irrelevant to the .NET
   case.** It cannot help — Go is not the runtime that installed
   the inadequate alt stack.

### What does fix the .NET case

| Mitigation | Effect |
|-----------|--------|
| Go-side: `malg(32*1024)` → `malg(1024*1024)` (E5) | **0/5 fixed** — Go's stack is never used on .NET threads |
| App-side: pre-install 1 MiB alt stack from C# (the existing `REPRO_FIX=1` shim) | 16/20 in the README; CoreCLR's PAL skips the install when `sigaltstack(NULL,&old)` already shows ss_size=1MiB; Go records the 1 MiB; the chkstk now has 30× headroom |
| CoreCLR-side: bump PAL's signal-stack allocation from 16 KB → 256 KB+ | Would fix it directly. Microsoft change. |

This shifts the upstream story for the *.NET case* from "Go-side
fix" to "CoreCLR-side fix or app-side workaround". The Go-side fix
is still the right thing for **other** runtimes that take the
install branch (any C runtime that doesn't pre-install its own alt
stack — including, importantly, "plain" cgo callers).

## Hypothesis verdict

| H | Status | Evidence |
|---|--------|----------|
| **H1** size overflow on Go's 32 KB gsignal | **CONFIRMED** | Threshold matches `malg(32 * 1024)` exactly; 28 K passes, 32 K fails |
| H2 C-installed-then-freed | **Not required** | No C-side `sigaltstack` install in this host; bug reproduces anyway |
| H3 SS_DISABLE / signal-in-flight race | Possible but not necessary | Crash happens with no SS_DISABLE racing — pure overflow on a stable alt stack is sufficient |
| H4 overflow on CoreCLR's 16 KB | **DISPROVED** as the dominant bug | No CoreCLR present; crash still 5/5 |
| H5 unrelated CoreCLR-internal | **DISPROVED** for this crash class | C-only repro crashes the same way |

The .NET reproducer's "16 KB CoreCLR-owned alt stack" observed in
strace is real but is a separate alt stack belonging to a
*different* CoreCLR thread — it doesn't matter for the TP worker
threads that enter Go via P/Invoke, because those workers don't
have it.

## What this changes about the upstream issue

1. **The reproducer can be reduced to ~150 lines of C.** No .NET
   needed to file or to investigate. We should attach `host.c +
   fat_handler.c + Makefile` to the issue thread.
2. **The maintainer disagreement is resolvable from this experiment
   alone.** Ian wrote "Go does not change the signal stack to one
   another framework installed". That's correct; the C-only repro
   shows Go takes the **install** branch (no pre-existing alt
   stack), and the crash is on the 32 KB Go-owned stack — not on a
   C-owned one. So Keith's UAF hypothesis isn't wrong as a class of
   bug, but it isn't *this* bug.
3. **The minimal Go-side fix is the size bump (#1 in README).**
   Bumping `malg(32*1024)` → `malg(128*1024)` or larger at
   `runtime/os_linux.go:388` should fix this exact crash class. E5
   is the next experiment to confirm that on the C-only host before
   filing an upstream patch.

## Repro

```bash
cd experiments/c-repro && make
cd ../..
LD_LIBRARY_PATH=. REPRO_PROBE_BYTES=65536 ./experiments/c-repro/host
# Expect: SIGABRT (exit 134) within seconds.
```

Knobs:

| Env                    | Default   | Effect |
|------------------------|-----------|--------|
| `REPRO_WORKERS`        | 32        | pthread workers calling `ping()` |
| `REPRO_ITERATIONS`     | 1 000 000 | Calls per worker |
| `REPRO_INTERVAL_US`    | 50        | µs between sender passes |
| `REPRO_RTSIG`          | 0         | Offset added to glibc `SIGRTMIN` |
| `REPRO_PROBE_BYTES`    | 65 536    | Bytes probed by SA_ONSTACK handler |
| `REPRO_GOLIB`          | `./libgolib.so` | Path to Go c-shared lib |
