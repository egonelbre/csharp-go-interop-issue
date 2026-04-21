// Minimal .NET host that mirrors the C reproducer in
// scripts/repro-sigaltstack/ but runs the workers on the CoreCLR
// threadpool. Keeping a CoreCLR runtime loaded alongside the Go
// c-shared library is a closer match to the real crash environment
// (xunit test host with cgo P/Invokes).
//
// Build + run:
//   cd scripts/repro-dotnet
//   CGO_ENABLED=1 go build -buildmode=c-shared -o libgolib.so golib.go
//   dotnet build -c Release
//   LD_LIBRARY_PATH=. ./bin/Release/net10.0/repro-dotnet
//
// Tunables via env vars:
//   REPRO_WORKERS    — concurrent worker tasks (default: 32)
//   REPRO_ITERATIONS — ping calls per worker  (default: 1000000)
//   REPRO_INTERVAL_US — SIGRT_2 send interval (default: 50)

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
    private const int SIGRTMIN_PLUS_2 = 34;

    private static volatile bool s_running = true;

    public static int Main()
    {
        var workers    = GetIntEnv("REPRO_WORKERS",    32);
        var iters      = GetIntEnv("REPRO_ITERATIONS", 1_000_000);
        var intervalUs = GetIntEnv("REPRO_INTERVAL_US", 50);

        Console.Error.WriteLine(
            $"[dotnet-repro] {workers} workers × {iters} calls, "
          + $"SIGRT_2 every {intervalUs} µs, pid={Environment.ProcessId}");

        // Warm cgo: first Ping() initialises the Go runtime and lets it
        // install its own handler for SIGRT_2 (which is what we want to
        // stress — we *don't* install our own handler first, letting Go
        // own it the same way it would in the actual xunit host).
        Native.Ping();

        // Signal-sender thread: fires SIGRT_2 at every other thread in the
        // process. Mirrors the strace evidence from the real crashes, where
        // a sibling thread sends SIGRT_2 via tgkill to a cgo thread just
        // after it installed its sigaltstack.
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
                        Native.Tgkill(pid, t.Id, SIGRTMIN_PLUS_2);
                    }
                }
                catch { /* thread list churns under contention */ }
                Thread.Sleep(TimeSpan.FromMicroseconds(intervalUs));
            }
        }) { IsBackground = true, Name = "sigrt2-sender" };
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
