# Alt stack budget: why 16 KiB is not enough for `inject_activation_handler`

Companion analysis to [`DOTNET_ISSUE.md`](./DOTNET_ISSUE.md). Reconstructs the
per-frame stack layout at the observed crash site and shows, from the crash
log's own addresses, that the 16 KiB alt stack is structurally insufficient
for the activation-handler path — no deep chain or unbounded recursion is
needed; the frames visible in the backtrace are enough on their own.

## TL;DR

At the moment `inject_activation_handler` calls the VM's registered
`g_activationFunction` (which is `HandleSuspensionForInterruptedThread`),
**four full Windows `CONTEXT` structures are simultaneously live on the alt
stack**:

1. `CONTEXT winContext` — a local of `inject_activation_handler`, used to
   pre-populate a Windows-style context from the kernel's `ucontext_t`.
2. `CONTEXT ctxOne` — embedded inside the `REGDISPLAY regDisplay` local of
   `HandleSuspensionForInterruptedThread`.
3. `CONTEXT ctxTwo` — the second embedded `T_CONTEXT` in the same
   `REGDISPLAY` (the stackwalker needs two: current and caller).
4. `CONTEXT tempContext` — a local of `IsIPInEpilog`, into which the caller's
   context is copied before feeding `RtlVirtualUnwind`.

On AMD64, `sizeof(CONTEXT) == 1232 B (0x4D0)`. Four copies is **4,928 B**,
just from context structures. Add three `KNONVOLATILE_CONTEXT_POINTERS`
(256 B each = 768 B), the kernel-deposited signal frame (1.3 KiB minimum,
2.5+ KiB with AVX-512, 8+ KiB with AMX), and the scalar / save-area /
spill-slot overhead of the three function frames, and you are already at
or past the 12 KiB usable area of the default 16 KiB alt stack. Thread
state that depends on which XSTATE bits are active at signal delivery
pushes it over the line probabilistically — hence the race.

## Reconstructing the layout from the crash log

From the symbolicated backtrace in `DOTNET_ISSUE.md`:

```
rip = 0x7ffff73285ba  <IsIPInEpilog+58>  call <EECodeInfo::GetFunctionEntry>
rsp = 0x7fbf5648f3b0

pContextToCheck = 0x7fbf56491de0   (&winContext)
pCodeInfo       = 0x7fbf564901e8   (&codeInfo inside HandleSuspensionForInterruptedThread)
context         = 0x7fbf56492ac0   (kernel-delivered ucontext_t*)
```

The alt stack is 16 KiB with a 4 KiB `PROT_NONE` guard page at the low end
(`src/coreclr/pal/src/thread/thread.cpp:2203`). Given the address digits,
the allocation spans:

```
0x7fbf5648f000 .. 0x7fbf56493000    (16 KiB total)
0x7fbf5648f000 .. 0x7fbf56490000    (guard page, PROT_NONE)
0x7fbf56490000 .. 0x7fbf56493000    (12 KiB usable)
```

The faulting instruction is `call`, which decrements `rsp` by 8 and writes
the return address to the new `[rsp]`. So it tries to touch
`0x7fbf5648f3a8` — **inside the guard page**, 1,624 B below the
usable/guard boundary.

Total consumed at fault:
```
0x56493000 - 0x5648f3a8 = 0x3C58 = 15,448 bytes
```

of a 16,384-byte alt stack — within 936 B of complete exhaustion before the
guard was ever touched. The guard page's job here is purely diagnostic: it
converts a silent overwrite of whatever random memory happened to sit below
the alt stack into a deterministic SIGSEGV. The overflow has already
happened by the time the guard hits.

## Per-frame accounting

| Region | Byte range | Size | Contents |
|---|---|---|---|
| Kernel sigframe (ucontext_t + XSAVE + restorer) | `0x56492ac0 .. 0x56493000` | **1,344 B** | Variable with XSTATE width: ~1.3 KiB baseline, ~2.5 KiB with AVX-512 dirty, ~8 KiB with AMX tiles dirty. The kernel places this at the top of the alt stack before calling the handler. |
| `inject_activation_handler` frame above `winContext` | `0x564922b0 .. 0x56492ac0` | **2,064 B** | Return address, saved rbp chain, `siginfo`/`ucontext` argument spills, `contextFlags`, callee-saved register save area, and call-site stack for `RtlCaptureContext`, `CONTEXTFromNativeContext`, `g_safeActivationCheckFunction`, `InvokeActivationHandler`, `CONTEXTToNativeContext`. |
| `CONTEXT winContext` (local) | `0x56491de0 .. 0x564922b0` | **1,232 B** | `sizeof(CONTEXT)` on AMD64. Populated by `RtlCaptureContext` + `CONTEXTFromNativeContext` at `signal.cpp:949,957`. |
| `HandleSuspensionForInterruptedThread` frame above `codeInfo` | `0x564902a0 .. 0x56491de0` (approx) | **~6,900 B** | Dominated by `REGDISPLAY regDisplay`: two embedded `T_CONTEXT` structs (`ctxOne`, `ctxTwo`, 1,232 B each) + two `KNONVOLATILE_CONTEXT_POINTERS` (256 B each) + base pointers ≈ **~3.0 KiB**. Plus `ExecutionState executionState`, `WorkingOnThreadContextHolder`, return address, callee-saved register save, spill/alignment around the `InitRegDisplay` / `IsGcSafe` / `GetRelOffset` / `GetCodeManager` call sequence. |
| `EECodeInfo codeInfo` | `0x564901e8 .. ~0x56490220` | ~56 B | |
| `IsIPInEpilog` frame (with inlined `IsIPInProlog`) | `0x5648f3a8 .. 0x564901e8` | **3,640 B** | `CONTEXT tempContext` (1,232 B) + `KNONVOLATILE_CONTEXT_POINTERS ctxPtrs` (256 B) + scalar locals (~44 B) + callee-saved save area + return address + `RtlVirtualUnwind` call-site argument stack + inlined `IsIPInProlog` locals and spills. |
| Guard page (fault) | `0x5648f000 .. 0x5648f3a8` | SIGSEGV | `call` → push RIP into guard → signal. |
| **Total at fault** | | **15,448 / 16,384 B** | |

## The three big consumers

### 1. `REGDISPLAY regDisplay` (~3.0 KiB on AMD64)

Defined in `src/coreclr/inc/regdisp.h`. `REGDISPLAY_BASE` embeds two full
`T_CONTEXT` fields (`ctxOne`, `ctxTwo`) used as ping-pong buffers by the
stackwalker (current vs caller context), plus two
`T_KNONVOLATILE_CONTEXT_POINTERS`, plus pointer members. On AMD64 that is
`2 × 1232 + 2 × 256 + ~64 = 3,040 B`. On ARM64, 2 × 992 + 2 × 160 +
`Arm64VolatileContextPointer` (144 B) + base ≈ 2,510 B.

This is the single largest local in the entire call chain — more than
`IsIPInEpilog`'s entire frame, more than `inject_activation_handler`'s
entire frame, more than the kernel sigframe.

### 2. Redundant `CONTEXT` copies

There are **four** concurrently-live `CONTEXT`s on the alt stack:

- `winContext` in `inject_activation_handler` (`signal.cpp:946`) —
  populated from the kernel ucontext and passed as
  `pContextToCheck` to `HandleSuspensionForInterruptedThread`.
- `ctxOne` and `ctxTwo` embedded in `REGDISPLAY`
  (`src/coreclr/inc/regdisp.h:34-35`) — `pThread->InitRegDisplay` copies
  the interrupted context into `ctxOne`.
- `tempContext` in `IsIPInEpilog` (`excep.cpp:6298`) —
  `CopyOSContext(&tempContext, pContextToCheck)` copies `winContext`
  (again) before handing it to `RtlVirtualUnwind`, which mutates in
  place and returns the epilog detection signal via `personalityRoutine`.

At least two of those are structural duplicates: `winContext` and
`ctxOne` hold the same data (the interrupted thread's context), and
`tempContext` is a further copy of `winContext`. That is 3,696 B of
alt-stack space spent on three near-identical copies of one 1,232 B
structure.

### 3. Kernel sigframe (variable)

Kernel-delivered on signal entry, before any CoreCLR code runs. On
Linux/AMD64 the sigframe includes `siginfo_t`, `ucontext_t`, an XSAVE
region for FPU/vector state, and a restorer trampoline. The XSAVE region
size is determined by which XSTATE components are currently enabled and
dirty for the thread:

| XSTATE | Approx sigframe size |
|---|---|
| SSE only | ~600 B |
| AVX | ~1.0 KiB |
| AVX-512 | ~2.5 KiB |
| AMX (TILE) | ~8 KiB |

This explains why the crash is probabilistic: a thread executing narrow
scalar code when SIGRTMIN arrives has a smaller sigframe and may fit;
a thread executing in wide-vector-optimized JITted code at the moment of
delivery has a much larger sigframe and overflows. Tiered JIT raising
methods into AVX-512-optimized paths over time shifts the probability
distribution — consistent with the "race window narrowed between 10.0.6
and main" observation.

## Why the shallow chain is enough

The deeper hijack machinery that `HandleSuspensionForInterruptedThread`
would run after `IsIPInEpilog` returns — `StackWalkFramesEx`,
`HijackThread`, or on the safe-point branch `HandleThreadAbort` with its
two `INSTALL_*` holders each carrying a `jmp_buf` — **never runs**. The
crash happens before `IsIPInEpilog` even finishes its first call into
`EECodeInfo::GetFunctionEntry`. Everything needed to overflow 12 KiB of
usable alt stack is already on it by that point: three function frames
totaling ~12 KiB of locals/saves + 1–3 KiB of kernel sigframe.

No pathological call depth is required. The observed path is the
problematic path.

## Flakiness factors

Small perturbations that push a marginal run across the 12 KiB line:

- **XSTATE width at signal delivery.** Biggest single knob, 1.3 KiB to 8+ KiB.
- **Compiler inlining decisions.** Whether `IsIPInProlog` and
  `InvokeActivationHandler` get their own frames or are inlined shifts
  200–400 B each.
- **Register save-area width.** Which callee-saved / XMM registers the
  compiler decides to preserve across the handler's call-site reshuffle.
- **Stack alignment / call-site shadow space** for the five-to-seven
  function calls inside `inject_activation_handler` alone.

None of these individually is the cause; the cause is structural (four
simultaneous `CONTEXT` copies on a 12 KiB usable area). These just
determine which call instruction is the one that finally crosses the
guard.

## Mitigation menu

Ordered from smallest surgery / smallest runtime savings to largest:

### A. Bump `SIGSTKSZ * 4` unconditionally (one-line fix)

The fix proposed in `DOTNET_ISSUE.md`. Raises per-thread alt stack from
16 KiB (12 KiB usable) to ~49 KiB (~44 KiB usable). Cost: ~32 KiB of
additional pinned VA per managed thread. Mirrors what the `HAS_ADDRESS_SANITIZER`
branch already does in the same function, and what the process-wide
stack-overflow handler stack already uses (9 pages, `signal.cpp:227`).

**Pro:** one line, deterministic, no behavior change.
**Con:** extra pinned VA; treats the symptom, not the cause.

### B. Move `REGDISPLAY` off the alt stack

Largest single consumer is `REGDISPLAY regDisplay` (~3 KiB on AMD64).
Allocate it from per-thread scratch (TLS-pointed heap buffer) rather than
on the alt-stack frame of `HandleSuspensionForInterruptedThread`.

**Pro:** saves ~3 KiB without growing alt stack; makes the structural
problem visible in the type system.
**Con:** touches VM code, needs careful review for reentrancy (the
activation handler is reentrant under nested signals in some scenarios).

### C. Eliminate `tempContext` copy in `IsIPInEpilog`

The `CopyOSContext(&tempContext, pContextToCheck)` at `excep.cpp:6311`
defends against `RtlVirtualUnwind` mutating the caller's context. But
`pContextToCheck` is `winContext` in `inject_activation_handler`, which
is already a private copy of the kernel ucontext — letting
`RtlVirtualUnwind` scribble on it is fine. Saves another ~1.5 KiB
(`CONTEXT` + `KNONVOLATILE_CONTEXT_POINTERS`).

**Pro:** saves 1.5 KiB without touching alt-stack sizing; the copy is
arguably already dead weight.
**Con:** requires auditing every caller of `IsIPInEpilog` to confirm
their input context is not shared state (there are only two callers,
both in `threadsuspend.cpp`).

### D. Switch off the alt stack before calling `g_activationFunction`

The long-term structural fix. `inject_activation_handler` runs briefly on
the alt stack, captures the interrupted thread's context, then uses the
same `SwitchStackAndExecuteHandler` / `setcontext` trick that
`sigsegv_handler` already uses to transfer execution to the interrupted
thread's normal stack before running the VM-side activation function.
The normal stack is known-usable by construction:
`g_safeActivationCheckFunction` has already returned true. The alt stack
becomes a trampoline only, as it is for SEGV/FPE/BUS/ILL.

**Pro:** removes the entire class of "unbounded depth on alt stack"
problems. The VM side stops caring about alt-stack budget.
**Con:** non-trivial change to the PAL; requires careful interaction
with libunwind's signal-frame handling and with debuggers. Not a hotfix.

Combinations B+C produce ~4.5 KiB of alt-stack savings without a pinned-VA
increase — roughly the same headroom as A but via shrinking the
consumers instead of widening the container. D subsumes all of them but
is the largest commitment.

## References

- `src/coreclr/pal/src/exception/signal.cpp:931` — `inject_activation_handler`
- `src/coreclr/pal/src/exception/signal.cpp:913` — `InvokeActivationHandler`
  shim
- `src/coreclr/pal/src/thread/thread.cpp:2188` — alt-stack sizing
- `src/coreclr/vm/threadsuspend.cpp:5696` — `HandleSuspensionForInterruptedThread`
- `src/coreclr/vm/excep.cpp:6193` / `6252` — `IsIPInProlog` / `IsIPInEpilog`
- `src/coreclr/inc/regdisp.h:14-50` — `REGDISPLAY_BASE` with embedded
  `ctxOne` / `ctxTwo`
- `src/coreclr/pal/inc/pal.h:1101-1692` — `CONTEXT` definitions per
  architecture
