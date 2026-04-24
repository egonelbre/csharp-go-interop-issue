// Enhanced .NET Pattern Test Host with Stack Usage Measurement
// Measures actual IP boundary analysis impact of each pattern

using System;
using System.Diagnostics;
using System.Linq;
using System.Runtime;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

internal static class NativeMeasured
{
    // Original pattern functions
    [DllImport("libpattern", EntryPoint = "create_go_like_complexity")]
    public static extern int CreateGoLikeComplexity();

    [DllImport("libpattern", EntryPoint = "create_signal_stress_scenario")]
    public static extern int CreateSignalStressScenario(int iterations);

    [DllImport("libpattern", EntryPoint = "create_extreme_signal_analysis_stress")]
    public static extern int CreateExtremeSignalAnalysisStress(int baseIterations);

    [DllImport("libpattern", EntryPoint = "create_atypical_calling_convention_stress")]
    public static extern int CreateAtypicalCallingConventionStress(int baseComplexity);

    [DllImport("libpattern", EntryPoint = "cleanup_thread_context")]
    public static extern void CleanupThreadContext();

    // Pattern identification
    [DllImport("libpattern", EntryPoint = "get_pattern_name")]
    public static extern IntPtr GetPatternNamePtr();

    [DllImport("libpattern", EntryPoint = "get_pattern_description")]
    public static extern IntPtr GetPatternDescriptionPtr();

    [DllImport("libpattern", EntryPoint = "get_expected_stack_kb")]
    public static extern int GetExpectedStackKb();

    [DllImport("libpattern", EntryPoint = "get_go_equivalent")]
    public static extern IntPtr GetGoEquivalentPtr();

    // Measurement functions (available in measured patterns)
    [DllImport("libpattern", EntryPoint = "get_pattern_stack_usage")]
    public static extern ulong GetPatternStackUsage();

    [DllImport("libpattern", EntryPoint = "get_pattern_signal_count")]
    public static extern int GetPatternSignalCount();

    [DllImport("libpattern", EntryPoint = "get_pattern_deep_analysis_count")]
    public static extern int GetPatternDeepAnalysisCount();

    // Signal functions
    [DllImport("libc", EntryPoint = "tgkill")]
    public static extern int Tgkill(int tgid, int tid, int sig);

    [DllImport("libc", EntryPoint = "getpid")]
    public static extern int Getpid();

    [DllImport("libc", EntryPoint = "syscall")]
    public static extern long Syscall(long number);

    // String helpers
    public static string GetPatternName()
    {
        var ptr = GetPatternNamePtr();
        return ptr != IntPtr.Zero ? Marshal.PtrToStringAnsi(ptr) ?? "unknown" : "unknown";
    }

    public static string GetPatternDescription()
    {
        var ptr = GetPatternDescriptionPtr();
        return ptr != IntPtr.Zero ? Marshal.PtrToStringAnsi(ptr) ?? "Unknown" : "Unknown";
    }

    public static string GetGoEquivalent()
    {
        var ptr = GetGoEquivalentPtr();
        return ptr != IntPtr.Zero ? Marshal.PtrToStringAnsi(ptr) ?? "N/A" : "N/A";
    }
}

internal static class ProgramMeasured
{
    private const int SYS_GETTID = 186;
    private const int CoreClrActivationSignal = 34; // SIGRTMIN
    private const int MeasurementSignal = 12;       // SIGUSR2

    private static volatile bool s_running = true;

    public static int Main()
    {
        var workers    = GetIntEnv("REPRO_WORKERS",    8);
        var iters      = GetIntEnv("REPRO_ITERATIONS", 1000);
        var intervalUs = GetIntEnv("REPRO_INTERVAL_US", 50);

        // Get pattern information
        var patternName = NativeMeasured.GetPatternName();
        var patternDesc = NativeMeasured.GetPatternDescription();
        var expectedKb  = NativeMeasured.GetExpectedStackKb();
        var goEquiv     = NativeMeasured.GetGoEquivalent();

        Console.Error.WriteLine($"[measurement] Enhanced Pattern Test with Stack Usage Measurement");
        Console.Error.WriteLine($"[measurement] Pattern: {patternName}");
        Console.Error.WriteLine($"[measurement] Description: {patternDesc}");
        Console.Error.WriteLine($"[measurement] Expected: {expectedKb}KB IP analysis impact");
        Console.Error.WriteLine($"[measurement] Go equivalent: {goEquiv}");
        Console.Error.WriteLine();

        Console.Error.WriteLine(
            $"[measurement] workers={workers} iters={iters} "
          + $"interval={intervalUs}µs gc={GCSettings.IsServerGC} "
          + $"pid={Environment.ProcessId}");

        // Start signal senders
        Thread? driver = StartSignalSender(intervalUs);
        Thread? measurementSender = StartMeasurementSignalSender(intervalUs * 2);

        var startTime = DateTime.UtcNow;

        try
        {
            // Start worker threads
            var tasks = Enumerable.Range(0, workers)
                .Select(i => Task.Run(() => WorkerThread(i, iters)))
                .ToArray();

            Task.WaitAll(tasks);

            var elapsed = DateTime.UtcNow - startTime;

            // Report measurement results
            try
            {
                var stackUsage = NativeMeasured.GetPatternStackUsage();
                var signalCount = NativeMeasured.GetPatternSignalCount();
                var deepAnalysisCount = NativeMeasured.GetPatternDeepAnalysisCount();

                Console.Error.WriteLine();
                Console.Error.WriteLine("=== MEASUREMENT RESULTS ===");
                Console.Error.WriteLine($"Pattern: {patternName}");
                Console.Error.WriteLine($"Duration: {elapsed.TotalSeconds:F1}s");
                Console.Error.WriteLine($"Max Stack Usage: {stackUsage} bytes ({stackUsage / 1024.0:F1} KB)");
                Console.Error.WriteLine($"Signal Count: {signalCount}");
                Console.Error.WriteLine($"Deep Analysis Events (>8KB): {deepAnalysisCount}");
                Console.Error.WriteLine($"Expected Impact: {expectedKb}KB");

                if (stackUsage > 0)
                {
                    var actualKb = stackUsage / 1024.0;
                    var accuracy = Math.Abs(actualKb - expectedKb) / expectedKb * 100;
                    Console.Error.WriteLine($"Prediction Accuracy: {100 - accuracy:F1}% (expected {expectedKb}KB, measured {actualKb:F1}KB)");

                    if (actualKb >= 16.0)
                    {
                        Console.Error.WriteLine("⚠️  CRITICAL: Stack usage >= 16KB sigaltstack limit!");
                    }
                    else if (actualKb >= 12.0)
                    {
                        Console.Error.WriteLine("⚠️  HIGH: Stack usage approaching sigaltstack limit");
                    }
                    else if (actualKb >= 8.0)
                    {
                        Console.Error.WriteLine("🔶 MODERATE: Measurable IP analysis impact");
                    }
                    else
                    {
                        Console.Error.WriteLine("✅ LOW: Minimal IP analysis impact");
                    }
                }
                else
                {
                    Console.Error.WriteLine("📊 No measurement data collected (pattern may not support measurement)");
                }
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"📊 Measurement collection failed: {ex.Message}");
            }

            return 0;
        }
        finally
        {
            s_running = false;
            driver?.Join(1000);
            measurementSender?.Join(1000);
        }
    }

    private static void WorkerThread(int workerId, int iterations)
    {
        Console.Error.WriteLine($"[worker-{workerId}] Starting with measurement...");

        try
        {
            for (int i = 0; i < iterations && s_running; i++)
            {
                // Execute pattern functions (same as original)
                int result1 = NativeMeasured.CreateGoLikeComplexity();

                if (i % 10 == 0)
                {
                    NativeMeasured.CreateSignalStressScenario(50);
                }

                if (i % 50 == 0)
                {
                    NativeMeasured.CreateExtremeSignalAnalysisStress(5);
                }

                if (i % 25 == 0)
                {
                    NativeMeasured.CreateAtypicalCallingConventionStress(3);
                }

                if (i % 10000 == 0)
                {
                    Thread.Yield();
                }
            }
        }
        finally
        {
            NativeMeasured.CleanupThreadContext();
        }

        Console.Error.WriteLine($"[worker-{workerId}] Completed successfully");
    }

    // Original SIGRTMIN signal sender
    private static Thread StartSignalSender(int intervalUs)
    {
        var t = new Thread(() =>
        {
            int myTid = (int)NativeMeasured.Syscall(SYS_GETTID);
            int pid = NativeMeasured.Getpid();

            while (s_running)
            {
                try
                {
                    foreach (var proc in Process.GetCurrentProcess().Threads.Cast<ProcessThread>())
                    {
                        if (proc.Id == myTid) continue;
                        NativeMeasured.Tgkill(pid, proc.Id, CoreClrActivationSignal);
                    }
                }
                catch { /* thread list churns */ }

                Thread.Sleep(TimeSpan.FromMicroseconds(intervalUs));
            }
        }) { IsBackground = true, Name = "sigrtmin-sender" };
        t.Start();
        return t;
    }

    // Additional measurement signal sender
    private static Thread StartMeasurementSignalSender(int intervalUs)
    {
        var t = new Thread(() =>
        {
            int myTid = (int)NativeMeasured.Syscall(SYS_GETTID);
            int pid = NativeMeasured.Getpid();

            while (s_running)
            {
                try
                {
                    foreach (var proc in Process.GetCurrentProcess().Threads.Cast<ProcessThread>())
                    {
                        if (proc.Id == myTid) continue;
                        NativeMeasured.Tgkill(pid, proc.Id, MeasurementSignal);
                    }
                }
                catch { /* thread list churns */ }

                Thread.Sleep(TimeSpan.FromMicroseconds(intervalUs));
            }
        }) { IsBackground = true, Name = "measurement-sender" };
        t.Start();
        return t;
    }

    private static int GetIntEnv(string name, int defaultValue)
    {
        var value = Environment.GetEnvironmentVariable(name);
        return int.TryParse(value, out var parsed) ? parsed : defaultValue;
    }
}