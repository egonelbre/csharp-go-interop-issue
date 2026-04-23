// Minimal .NET host that mirrors the C reproducer in
// scripts/repro-sigaltstack/ but runs the workers on the CoreCLR
// threadpool. Keeping a CoreCLR runtime loaded alongside the Go
// c-shared library is a closer match to the real crash environment
// (xunit test host with cgo P/Invokes).
//
// Two modes:
//
//   REPRO_MODE=signal  (default)
//     A dedicated thread fires kernel signal 34 (= glibc SIGRTMIN =
//     CoreCLR PAL's INJECT_ACTIVATION_SIGNAL) at every other thread
//     every REPRO_INTERVAL_US microseconds. strace labels this signal
//     "SIGRT_2" (kernel-SIGRTMIN-relative: glibc reserves 32/33 for
//     pthread cancel & setxid, so 34 is glibc's public SIGRTMIN).
//     This synthesises what CoreCLR's GC / tiered-JIT machinery fires
//     naturally. Most reliable way to reproduce.
//
//   REPRO_MODE=gc
//     No synthetic signal sender. Each worker allocates a burst of
//     garbage between Ping() calls, and a dedicated thread forces
//     GC.Collect() at a high rate. The idea: let CoreCLR's own GC
//     fire INJECT_ACTIVATION_SIGNAL at the TP Workers while they're
//     inside cgo, no libc signalling from us. Answers the question
//     "does it reproduce under realistic GC pressure alone?".
//     Pair with Server GC in runtimeconfig for max pressure.
//
// The original investigation doc attributed the signal to Go's
// cooperative preemption — that was wrong. Go uses SIGURG (signal 23)
// for async preemption, not any RT signal.
//
// Build + run:
//   cd scripts/repro-dotnet
//   CGO_ENABLED=1 go build -buildmode=c-shared -o libgolib.so golib.go
//   dotnet build -c Release
//   LD_LIBRARY_PATH=. ./bin/Release/net10.0/repro-dotnet
//
// Tunables via env vars:
//   REPRO_MODE         — "signal" (default) or "gc"
//   REPRO_WORKERS      — concurrent worker tasks (default: 32)
//   REPRO_ITERATIONS   — ping calls per worker  (default: 1000000)
//   REPRO_INTERVAL_US  — signal / GC.Collect interval (default: 50)
//   REPRO_ALLOC_BYTES  — garbage allocated per ping in gc mode
//                         (default: 16384)

using System;
using System.Diagnostics;
using System.Linq;
using System.Runtime;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

internal static class Native
{
    [DllImport("golib", EntryPoint = "ping")]
    public static extern int Ping();

    [DllImport("libc", EntryPoint = "tgkill")]
    public static extern int Tgkill(int tgid, int tid, int sig);

    [DllImport("libc", EntryPoint = "getpid")]
    public static extern int Getpid();

    [DllImport("libc", EntryPoint = "syscall")]
    public static extern long Syscall(long number);

    [DllImport("sigstack_helper", EntryPoint = "ensure_large_sigaltstack")]
    public static extern void EnsureLargeSigaltstack();

    [DllImport("sigstack_helper", EntryPoint = "dump_sigaltstack", CharSet = CharSet.Ansi)]
    public static extern void DumpSigaltstack(string tag);
}

internal static class Program
{
    private const int SYS_GETTID = 186;  // x86_64

    // Kernel signal 34 = glibc SIGRTMIN = CoreCLR PAL's
    // INJECT_ACTIVATION_SIGNAL (GC thread suspension, JIT patching,
    // debugger activation). strace labels it SIGRT_2.
    private const int CoreClrActivationSignal = 34;

    private static volatile bool s_running = true;

    public static int Main()
    {
        var mode       = (Environment.GetEnvironmentVariable("REPRO_MODE") ?? "signal").ToLowerInvariant();
        var workers    = GetIntEnv("REPRO_WORKERS",    32);
        var iters      = GetIntEnv("REPRO_ITERATIONS", 1_000_000);
        var intervalUs = GetIntEnv("REPRO_INTERVAL_US", 50);
        var allocBytes = GetIntEnv("REPRO_ALLOC_BYTES", 16 * 1024);

        var useFix = Environment.GetEnvironmentVariable("REPRO_FIX") == "1";
        var probeMode = Environment.GetEnvironmentVariable("REPRO_PROBE") == "1";

        Console.Error.WriteLine(
            $"[dotnet-repro] mode={mode} workers={workers} iters={iters} "
          + $"interval={intervalUs}µs gc={GCSettings.IsServerGC} "
          + $"fix={useFix} pid={Environment.ProcessId}");

        if (useFix) Native.EnsureLargeSigaltstack(); // main thread
        Native.Ping(); // warm cgo

        Thread? driver = mode switch
        {
            "signal" => StartSignalSender(intervalUs),
            "gc"     => StartGcDriver(intervalUs),
            _ => throw new ArgumentException($"unknown REPRO_MODE={mode}"),
        };

        var tasks = new Task[workers];
        for (int i = 0; i < workers; i++)
        {
            tasks[i] = Task.Run(() =>
            {
                // Install the large sigaltstack BEFORE the first Ping() on
                // this threadpool thread. Go's minitSignalStack will see it
                // on needm and not install its own 32 KB stack.
                if (useFix) Native.EnsureLargeSigaltstack();
                // E2: probe before any Go has touched this thread.
                if (probeMode) Native.DumpSigaltstack("before-first");
                for (int k = 0; k < iters; k++)
                {
                    // Probe both sides of a small subset of pings to keep the
                    // log size manageable while still capturing churn.
                    if (probeMode && k < 10) Native.DumpSigaltstack("before");
                    if (Native.Ping() != 42)
                        throw new Exception("ping returned unexpected value");
                    if (probeMode && k < 10) Native.DumpSigaltstack("after");
                    if (mode == "gc")
                        GenerateGarbage(allocBytes);
                }
            });
        }

        Task.WaitAll(tasks);
        s_running = false;
        driver?.Join();
        Console.Error.WriteLine("[dotnet-repro] PASS");
        return 0;
    }

    // Signal-sender thread: fires the CoreCLR activation signal at
    // every other thread in the process. In a real .NET process this
    // would be CoreCLR's own GC / tiered JIT machinery; we fire it
    // explicitly so the race happens under a light synthetic load.
    private static Thread StartSignalSender(int intervalUs)
    {
        var t = new Thread(() =>
        {
            int myTid = (int)Native.Syscall(SYS_GETTID);
            int pid = Native.Getpid();
            while (s_running)
            {
                try
                {
                    foreach (var proc in Process.GetCurrentProcess().Threads.Cast<ProcessThread>())
                    {
                        if (proc.Id == myTid) continue;
                        Native.Tgkill(pid, proc.Id, CoreClrActivationSignal);
                    }
                }
                catch { /* thread list churns under contention */ }
                Thread.Sleep(TimeSpan.FromMicroseconds(intervalUs));
            }
        }) { IsBackground = true, Name = "activation-sender" };
        t.Start();
        return t;
    }

    // GC driver: forces CoreCLR to do full-blocking GCs at a high rate
    // so its INJECT_ACTIVATION_SIGNAL path fires "naturally" at the TP
    // Workers. No libc-level tgkill from us.
    private static Thread StartGcDriver(int intervalUs)
    {
        var t = new Thread(() =>
        {
            while (s_running)
            {
                // Mode=Forced guarantees a blocking, thread-suspending
                // collection rather than a background/concurrent one —
                // this is the path that needs to park every thread
                // (including ones currently in cgo), which is the
                // INJECT_ACTIVATION_SIGNAL code path we want exercised.
                GC.Collect(2, GCCollectionMode.Forced, blocking: true);
                Thread.Sleep(TimeSpan.FromMicroseconds(intervalUs));
            }
        }) { IsBackground = true, Name = "gc-driver" };
        t.Start();
        return t;
    }

    // Burn `bytes` worth of short-lived allocations to keep GC busy
    // between cgo calls.
    private static void GenerateGarbage(int bytes)
    {
        // A mix of arrays of different element types so the allocator
        // touches multiple heap regions and promotion patterns.
        var a = new byte[bytes];
        var b = new int[bytes / 4];
        var c = new object[bytes / 64];
        for (int i = 0; i < c.Length; i++) c[i] = new string('x', 8);
        GC.KeepAlive(a);
        GC.KeepAlive(b);
        GC.KeepAlive(c);
    }

    private static int GetIntEnv(string name, int def)
    {
        var s = Environment.GetEnvironmentVariable(name);
        return int.TryParse(s, out var v) && v > 0 ? v : def;
    }
}
