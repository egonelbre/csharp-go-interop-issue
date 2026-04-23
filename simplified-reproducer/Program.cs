// .NET host for Simplified C Library reproducer
// Triggers CoreCLR sigaltstack overflow by calling C functions with atypical
// calling conventions via P/Invoke during SIGRTMIN signal bombardment.
//
// CoreCLR provides SA_ONSTACK signal handlers with 16KB sigaltstack limit.
// The C library creates atypical assembly patterns that cause CoreCLR's IP
// boundary analysis (IsIPInProlog/IsIPInEpilog) to consume excessive stack.

using System;
using System.Diagnostics;
using System.Linq;
using System.Runtime;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

internal static class Native
{
    // Our C library functions - replicate Go's complexity
    [DllImport("complex_c_lib", EntryPoint = "create_go_like_complexity")]
    public static extern int CreateGoLikeComplexity();

    [DllImport("complex_c_lib", EntryPoint = "create_signal_stress_scenario")]
    public static extern int CreateSignalStressScenario(int iterations);

    [DllImport("complex_c_lib", EntryPoint = "create_extreme_signal_analysis_stress")]
    public static extern int CreateExtremeSignalAnalysisStress(int baseIterations);

    [DllImport("complex_c_lib", EntryPoint = "create_atypical_calling_convention_stress")]
    public static extern int CreateAtypicalCallingConventionStress(int baseComplexity);

    [DllImport("complex_c_lib", EntryPoint = "cleanup_thread_context")]
    public static extern void CleanupThreadContext();

    // Signal sending functions (same as other reproducers)
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
    private const int CoreClrActivationSignal = 34; // SIGRTMIN

    private static volatile bool s_running = true;

    public static int Main()
    {
        var workers    = GetIntEnv("REPRO_WORKERS",    64);
        var iters      = GetIntEnv("REPRO_ITERATIONS", 5_000_000);
        var intervalUs = GetIntEnv("REPRO_INTERVAL_US", 1);  // Very aggressive
        var mode       = Environment.GetEnvironmentVariable("REPRO_MODE") ?? "signal";

        Console.Error.WriteLine(
            $"[dotnet-c] workers={workers} iters={iters} "
          + $"interval={intervalUs}µs mode={mode} gc={GCSettings.IsServerGC} "
          + $"pid={Environment.ProcessId}");
        Console.Error.WriteLine("[dotnet-c] C Library complexity + SIGRTMIN");

        // Start signal sender or GC driver
        Thread? driver = null;
        if (mode == "signal") {
            driver = StartSignalSender(intervalUs);
        } else if (mode == "gc") {
            driver = StartGcDriver(intervalUs);
        } else {
            Console.Error.WriteLine($"[dotnet-c] Unknown mode: {mode}");
            return 1;
        }

        try
        {
            // Start worker threads calling C library
            var tasks = Enumerable.Range(0, workers)
                .Select(i => Task.Run(() => WorkerThread(i, iters)))
                .ToArray();

            Task.WaitAll(tasks);
            Console.Error.WriteLine("[dotnet-c] All workers completed without crash");
            return 0;
        }
        finally
        {
            s_running = false;
            driver?.Join(1000);
        }
    }

    // Worker thread - calls C library functions that create Go-like complexity
    private static void WorkerThread(int workerId, int iterations)
    {
        Console.Error.WriteLine($"[worker-{workerId}] Starting with {iterations} iterations");

        try
        {
            for (int i = 0; i < iterations && s_running; i++)
            {
                // Call our C functions that replicate Go's complexity
                int result1 = Native.CreateGoLikeComplexity();

                // Alternate between different complexity patterns to stress CoreCLR's signal analysis
                if (i % 10 == 0)
                {
                    int result2 = Native.CreateSignalStressScenario(50);
                    if (result1 < 0 || result2 < 0)
                        throw new Exception("C library function failed");
                }

                // Add extreme complexity targeting signal handler analysis
                if (i % 50 == 0)
                {
                    int result3 = Native.CreateExtremeSignalAnalysisStress(5);
                    if (result3 < 0)
                        throw new Exception("Extreme complexity function failed");
                }

                // Add atypical calling conventions (the key Go characteristic!)
                if (i % 25 == 0)
                {
                    int result4 = Native.CreateAtypicalCallingConventionStress(3);
                    if (result4 < 0)
                        throw new Exception("Atypical calling convention function failed");
                }

                // Verify we got reasonable results
                if (result1 < 0)
                    throw new Exception("CreateGoLikeComplexity failed");

                // Occasional yield to increase signal/work overlap
                if (i % 10000 == 0)
                {
                    Thread.Yield();
                }
            }
        }
        finally
        {
            // Cleanup C library resources
            Native.CleanupThreadContext();
        }

        Console.Error.WriteLine($"[worker-{workerId}] Completed successfully");
    }

    // Signal sender - fires SIGRTMIN at worker threads
    private static Thread StartSignalSender(int intervalUs)
    {
        var t = new Thread(() =>
        {
            int myTid = (int)Native.Syscall(SYS_GETTID);
            int pid = Native.Getpid();
            Console.Error.WriteLine($"[signal-sender] Firing SIGRTMIN every {intervalUs}µs to pid {pid}");

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

    // GC driver - forces CoreCLR GCs at high rate
    private static Thread StartGcDriver(int intervalUs)
    {
        var t = new Thread(() =>
        {
            Console.Error.WriteLine($"[gc-driver] Forcing GC every {intervalUs}µs");

            while (s_running)
            {
                try
                {
                    GC.Collect();
                    GC.WaitForPendingFinalizers();
                    GC.Collect();
                }
                catch { /* ignore GC errors */ }

                Thread.Sleep(TimeSpan.FromMicroseconds(intervalUs));
            }
        }) { IsBackground = true, Name = "gc-driver" };
        t.Start();
        return t;
    }

    private static int GetIntEnv(string name, int defaultValue)
    {
        var value = Environment.GetEnvironmentVariable(name);
        return int.TryParse(value, out var parsed) ? parsed : defaultValue;
    }
}