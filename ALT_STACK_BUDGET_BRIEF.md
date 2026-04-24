# Alt-stack overflow: brief

Companion to [`DOTNET_ISSUE.md`](./DOTNET_ISSUE.md). Assumes familiarity
with CoreCLR internals.

## Claim

`inject_activation_handler` + `HandleSuspensionForInterruptedThread` +
`IsIPInEpilog` simultaneously pin **four full `CONTEXT` structs** and
**three `KNONVOLATILE_CONTEXT_POINTERS`** on the alt stack. On AMD64
that is `4 × 1232 + 3 × 256 = 5,696 B` just from context state, before
any other locals or the kernel sigframe. The 12 KiB usable area of the
default 16 KiB alt stack cannot hold this plus sigframe + REGDISPLAY
scalars + `EECodeInfo` + save areas + call-site shadow; the "race" is
XSTATE width tipping a marginal run over.

## The four CONTEXTs

| # | Source | Purpose |
|---|---|---|
| 1 | `CONTEXT winContext` — `signal.cpp:946` | Windows-style copy of kernel `ucontext_t`; passed as `pContextToCheck`. |
| 2 | `REGDISPLAY::ctxOne` — `regdisp.h:34` | Current-frame context for stackwalk; populated by `InitRegDisplay`. |
| 3 | `REGDISPLAY::ctxTwo` — `regdisp.h:35` | Caller-frame context for stackwalk. |
| 4 | `CONTEXT tempContext` — `excep.cpp:6298` | `CopyOSContext(&tempContext, pContextToCheck)` before `RtlVirtualUnwind`. |

Plus `REGDISPLAY::ctxPtrsOne` / `ctxPtrsTwo` and `IsIPInEpilog::ctxPtrs`
(3 × `KNONVOLATILE_CONTEXT_POINTERS`).

## Arithmetic from the crash log

ss_size=16384, 4 KiB guard at low end → 12 KiB usable = `0x3000`.
Reconstructing from the logged addresses (`ucontext` arg, `&winContext`,
`&codeInfo`, crash `rsp`):

```
ss_top (inferred)       0x7fbf56493000
kernel sigframe         1,344 B      (AMD64 baseline; +2.5 KiB w/ AVX-512; +8 KiB w/ AMX)
inject_activation_handler (incl. winContext 1232 B)    ~3,300 B
HandleSuspensionForInterruptedThread (incl. regDisplay ~3.0 KiB)    ~7,000 B
IsIPInEpilog (+ inlined IsIPInProlog, incl. tempContext 1232 B)    3,640 B
------------------------------------------------------------
consumed at fault       15,448 B of 16,384 B
fault rsp               0x7fbf5648f3a8 — inside guard page
```

The `call EECodeInfo::GetFunctionEntry` at `IsIPInEpilog+58` is the first
instruction whose `push rip` lands in the guard. Nothing pathological —
the observed path is the problematic path.

## Race knob

XSAVE area in the kernel-deposited sigframe scales with dirty XSTATE
components at signal delivery. ~600 B (SSE) to ~8 KiB (AMX). Tiered JIT
raising hot methods into AVX-512-optimized paths over time shifts the
probability distribution of overflow upward. Matches the observation
that the race window narrowed between 10.0.6 and `main` despite no
intentional change to alt-stack sizing.

## Fix options

| | Change | Saves | Cost |
|---|---|---|---|
| A | `SIGSTKSZ*4` bump in `EnsureSignalAlternateStack` (the proposed hotfix) | +32 KiB usable | ~32 KiB pinned VA / thread |
| B | Move `REGDISPLAY` to per-thread TLS scratch | ~3 KiB on stack | VM surgery; reentrancy audit |
| C | Drop `CopyOSContext` in `IsIPInEpilog`; let `RtlVirtualUnwind` mutate the caller's already-private copy | ~1.5 KiB on stack | Audit both call sites in `threadsuspend.cpp` |
| D | `SwitchStackAndExecuteHandler` off the alt stack before `g_activationFunction` (mirror `sigsegv_handler`) | Removes the class | Large PAL change |

B+C ≈ A in headroom, without growing pinned VA. D is the structural fix.

## Entry points

- `src/coreclr/pal/src/exception/signal.cpp:931`
- `src/coreclr/pal/src/thread/thread.cpp:2188`
- `src/coreclr/vm/threadsuspend.cpp:5696`
- `src/coreclr/vm/excep.cpp:6252`
- `src/coreclr/inc/regdisp.h:14`
