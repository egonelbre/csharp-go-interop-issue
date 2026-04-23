# SIGSEGV when `inject_activation_handler` overflows the per-thread alt stack

## Summary

The per-thread signal alternate stack installed by `EnsureSignalAlternateStack` in
`src/coreclr/pal/src/thread/thread.cpp` sizes to 16 KB
(`SIGSTKSZ + sizeof(SignalHandlerWorkerReturnPoint) + page_guard`).

For `inject_activation_handler` — the SIGRTMIN path used by GC suspension,
tiered JIT, and debugger activation — this is not enough. Unlike
`sigsegv_handler` / `sigfpe_handler` / `sigbus_handler` / `sigill_handler`,
the activation handler does **not** switch off the alt stack before invoking
`g_activationFunction`. The VM-installed activation function has unbounded
depth from the PAL's perspective and routinely exceeds the ~12 KB usable
area of the alt stack. The result is a chkstk SIGSEGV deep inside
libcoreclr, below the kernel-registered `ss_sp`.

## Reproduction

A .NET host that calls into a Go c-shared library via P/Invoke from many
ThreadPool workers, with a synthetic sender firing `SIGRTMIN` at every
worker, reliably crashes:

- System-installed .NET 10.0.6 (Linux x64): **5/5 SIGSEGV** within seconds
  at default stress (32 workers, 50 µs signal interval, 1 M iters).
- Release build of `main` (b6421ec9f4f): **19/20 SIGSEGV** under aggressive
  stress (64 workers, 10 µs interval, 5 M iters). Default stress passes —
  the race window narrowed between 10.0.6 and main (likely via
  JIT/suspension changes in `src/coreclr/vm/`), but the underlying
  alt-stack size bug is unchanged.

Full reproducer, tracer, and investigation notes:
<https://github.com/egonelbre/csharp-go-interop-issue>

Minimal shape:

```
N TP workers × tight loop of P/Invoke into Go
1 dedicated thread: tgkill(tid, SIGRTMIN) at every other thread every T µs
```

Any C runtime whose thread happens to be in an unmanaged P/Invoke at the
moment SIGRTMIN is delivered is enough — Go is incidental. Pure managed
code also triggers it if the interrupted PC sits at a point where
`g_activationFunction` takes a deep path (GC suspension under load).

## Mechanism

CoreCLR's signal handlers split into two camps:

- **`sigsegv_handler`, `sigfpe_handler`, `sigbus_handler`, `sigill_handler`**
  (`signal.cpp`): capture context on the alt stack, then
  `SwitchStackAndExecuteHandler` / `setcontext` back to the interrupted
  thread's normal stack and continue there. The alt stack is a trampoline
  only. 12 KB usable is plenty.

- **`inject_activation_handler`** (`signal.cpp:931`): stores a
  `CONTEXT winContext` on the alt stack (~2.7 KB on AMD64 with AVX-512
  register state), then calls `InvokeActivationHandler` →
  `g_activationFunction(pWinContext)` (`signal.cpp:916`). The activation
  function is installed from the VM and can do GC-suspend bookkeeping,
  hijack retargeting, tiered-JIT redispatch — its depth is unbounded from
  the PAL's view. **All of that runs on the 12 KB usable area.**

Single-function chkstk prologues in the release `libcoreclr.so` measure up
to 24 KB on this machine — one frame alone can exceed the entire usable
alt stack, let alone a chain.

### Example output

#### Process exit (unpatched `main`, aggressive stress)

```
$ DOTNET_ROOT=<testhost> DOTNET_ROLL_FORWARD=Major LD_LIBRARY_PATH=. \
    REPRO_WORKERS=64 REPRO_ITERATIONS=5000000 REPRO_INTERVAL_US=10 \
    ./bin/Release/net10.0/repro-dotnet
[dotnet-repro] mode=signal workers=64 iters=5000000 interval=10µs gc=True fix=False pid=104637
Segmentation fault (core dumped)
$ echo $?
139
```

#### Symbolicated backtrace (Release build of `main` with debug info)

```
Thread 40 ".NET Finalizer" received signal SIGSEGV, Segmentation fault.
0x00007ffff73285ba in IsIPInEpilog (…) at src/coreclr/vm/excep.cpp:6210

===== CRASHED =====
rip  0x7ffff73285ba  <IsIPInEpilog(_CONTEXT*, EECodeInfo*, int*)+58>
rsp  0x7fbf5648f3b0
=> 0x7ffff73285ba <IsIPInEpilog+58>:   call   0x7ffff736b060 <EECodeInfo::GetFunctionEntry>

#0  IsIPInProlog (pCodeInfo=0x7fbf564901e8)
      at src/coreclr/vm/excep.cpp:6210
#1  IsIPInEpilog (pContextToCheck=0x7fbf56491de0, pCodeInfo=0x7fbf564901e8, …)
      at src/coreclr/vm/excep.cpp:6277
#2  HandleSuspensionForInterruptedThread (interruptedContext=0x7fbf56491de0)
      at src/coreclr/vm/threadsuspend.cpp:5768
#3  inject_activation_handler (code=<optimized out>, siginfo=<optimized out>, context=0x7fbf56492ac0)
      at src/coreclr/pal/src/exception/signal.cpp:966
#4  <signal handler called>
#5  0x00007fff791b16c4 in ?? ()
#6  0x00007fff782459ea in ?? ()
```

The chain is exactly what the mechanism section describes:

- `<signal handler called>` — kernel delivered `SIGRTMIN` on the alt stack.
- `inject_activation_handler` (frame #3, `signal.cpp:966`) — the `g_activationFunction(pWinContext)` call site.
- `HandleSuspensionForInterruptedThread` (frame #2, `threadsuspend.cpp:5768`) — the VM's registered activation function.
- `IsIPInEpilog` → `IsIPInProlog` (frames #1, #0, `excep.cpp:6277`, `6210`) — deep enough that the `call` instruction's push overflows the alt stack.

No Go frames. The entire chain runs inside `libcoreclr.so` on the 16 KB alt stack.

Same signature on a stripped 10.0.6 build (full log in
`investigation-tools/test-artifacts/crash/gdb.log` of the repro repo)
shows the classic chkstk overshoot pattern at the faulting `rip`:

```
=> rip:  movq   $0x0, (%rsp)
         sub    $0x1000, %rsp
         movq   $0x0, (%rsp)
         sub    $0x1000, %rsp
#0  libcoreclr.so + 0x360028   ← chkstk prologue
#1  libcoreclr.so + 0x361e2e
#2  libcoreclr.so + 0x364331
#3  libcoreclr.so + 0x46804b
#4  <signal handler called>
```

On a different call path, different depth, same overflow.

#### `sigaltstack` tracer histogram (unpatched `main`)

The `LD_PRELOAD` shim that wraps `sigaltstack(2)` and logs every call
sees only the PAL-routed installs (Go on Linux uses a direct syscall
and is invisible to libc-level interception):

```
  72 ss_size=16384     ← EnsureSignalAlternateStack install on every CoreCLR-created thread
```

After the patch:

```
  73 ss_size=49152     ← same code path, new size
```

Single variable changed; size goes from 16384 to 49152 exactly as
predicted by the formula.

## Proposed fix

The ASAN branch at `thread.cpp:2188` already knows 16 KB is too small and
adds `SIGSTKSZ * 4`. Apply the same bump unconditionally:

```diff
// src/coreclr/pal/src/thread/thread.cpp:2184
-int altStackSize = SIGSTKSZ + ALIGN_UP(sizeof(SignalHandlerWorkerReturnPoint), 16) + GetVirtualPageSize();
+int altStackSize = SIGSTKSZ + (SIGSTKSZ * 4) + ALIGN_UP(sizeof(SignalHandlerWorkerReturnPoint), 16) + GetVirtualPageSize();
```

Raises the per-thread alt stack from 16 KB → ~49 KB (12 KB usable →
~44 KB usable), comparable to the existing process-wide 9-page
stack-overflow handler stack (`signal.cpp:227`). Cost: ~32 KB additional
pinned VA per managed thread.

### Tested

Built `main` at b6421ec9f4f with `-s clr+libs -c Release`, applied the
one-line patch, swapped `libcoreclr.so` into the built testhost layout:

| Variant | Default stress (5/5) | Aggressive stress (20 runs) |
|---|---|---|
| Main, unpatched (16 KB alt stack) | 5 pass | **19 SIGSEGV** |
| Main, patched (49 KB alt stack)   | 5 pass | **0 SIGSEGV**  |

`LD_PRELOAD` tracer confirms installed `sigaltstack` size went from
16384 to 49152 bytes; the source of the fix is the single line above.

## Long-term alternative

Mirror what `sigsegv_handler` already does: have `inject_activation_handler`
also call `SwitchStackAndExecuteHandler` to move off the alt stack before
invoking `g_activationFunction`. The interrupted thread's normal stack is
usable by construction (`g_safeActivationCheckFunction` returned true).
Eliminates the "unbounded depth on alt stack" class of problem entirely
rather than just extending the budget. Larger change; probably not a
hotfix.
