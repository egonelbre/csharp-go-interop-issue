// Minimal .NET host that mirrors the C reproducer in
// scripts/repro-sigaltstack/ but runs the workers on the CoreCLR
// threadpool. Keeping a CoreCLR runtime loaded alongside the Go
// c-shared library is a closer match to the real crash environment
// (xunit test host with cgo P/Invokes).
//
// The signal we fire is kernel signal 34 — which on Linux/glibc is
// the same number as glibc's `SIGRTMIN`, and is also the number
// CoreCLR's PAL uses for `INJECT_ACTIVATION_SIGNAL` (GC thread
// suspension, JIT patching, debugger activation). strace labels it
// "SIGRT_2" because strace numbers RT signals from the kernel's
// SIGRTMIN (= 32); glibc reserves 32/33 for pthread cancel & setxid.
// In the real CI crashes, the SIGRT_2 in the strace is CoreCLR
// sending its own activation signal — NOT Go preempting, as the
// original investigation doc assumed. Go's async preemption uses
// SIGURG (signal 23).
//
// Build + run:
//   cd scripts/repro-dotnet
//   CGO_ENABLED=1 go build -buildmode=c-shared -o libgolib.so golib.go
//   dotnet build -c Release
//   LD_LIBRARY_PATH=. ./bin/Release/net10.0/repro-dotnet
//
// Tunables via env vars:
//   REPRO_WORKERS     — concurrent worker tasks (default: 32)
//   REPRO_ITERATIONS  — ping calls per worker  (default: 1000000)
//   REPRO_INTERVAL_US — activation-signal send interval (default: 50)

using System;
using System.Diagnostics;
using System.Linq;
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
}

internal static class Program
{
    private const int SYS_GETTID = 186;  // x86_64

    // Kernel signal 34 = glibc SIGRTMIN = CoreCLR PAL's
    // INJECT_ACTIVATION_SIGNAL (used for GC thread suspension, JIT
    // patching, debugger activation). strace labels it SIGRT_2.
    // Firing it ourselves reproduces what CoreCLR's own GC would
    // fire during normal operation.
    private const int CoreClrActivationSignal = 34;

    private static volatile bool s_running = true;

    public static int Main()
    {
        var workers    = GetIntEnv("REPRO_WORKERS",    32);
        var iters      = GetIntEnv("REPRO_ITERATIONS", 1_000_000);
        var intervalUs = GetIntEnv("REPRO_INTERVAL_US", 50);

        Console.Error.WriteLine(
            $"[dotnet-repro] {workers} workers × {iters} calls, "
          + $"signal 34 (SIGRTMIN / CoreCLR activation) every "
          + $"{intervalUs} µs, pid={Environment.ProcessId}");

        // Warm cgo: first Ping() initialises the Go runtime, which
        // takes over sigaltstack on the calling thread via needm.
        Native.Ping();

        // Signal-sender thread: fires the CoreCLR activation signal at
        // every other thread in the process. In a real .NET process
        // this would be CoreCLR's own GC / tiered JIT machinery doing
        // the same thing; we fire it explicitly so the race happens
        // under a light synthetic load rather than requiring a full
        // xunit test host.
        var sender = new Thread(() =>
        {
            int myTid = (int)Native.Syscall(SYS_GETTID);
            int pid = Native.Getpid();
            while (s_running)
            {
                try
                {
                    foreach (var t in Process.GetCurrentProcess().Threads.Cast<ProcessThread>())
                    {
                        if (t.Id == myTid) continue;
                        Native.Tgkill(pid, t.Id, CoreClrActivationSignal);
                    }
                }
                catch { /* thread list churns under contention */ }
                Thread.Sleep(TimeSpan.FromMicroseconds(intervalUs));
            }
        }) { IsBackground = true, Name = "activation-sender" };
        sender.Start();

        var tasks = new Task[workers];
        for (int i = 0; i < workers; i++)
        {
            tasks[i] = Task.Run(() =>
            {
                for (int k = 0; k < iters; k++)
                {
                    if (Native.Ping() != 42)
                        throw new Exception("ping returned unexpected value");
                }
            });
        }

        Task.WaitAll(tasks);
        s_running = false;
        sender.Join();
        Console.Error.WriteLine("[dotnet-repro] PASS");
        return 0;
    }

    private static int GetIntEnv(string name, int def)
    {
        var s = Environment.GetEnvironmentVariable(name);
        return int.TryParse(s, out var v) && v > 0 ? v : def;
    }
}
