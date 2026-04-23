// Minimal .NET program to test essential elements for CoreCLR overflow
// Progressive complexity testing without SA_ONSTACK specifics

using System;
using System.Diagnostics;
using System.Linq;
using System.Runtime;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

internal static class Native
{
    // Minimal C functions
    [DllImport("minimal_c_lib", EntryPoint = "minimal_complexity_function")]
    public static extern int MinimalComplexity(int iterations);

    [DllImport("minimal_c_lib", EntryPoint = "medium_complexity_function")]
    public static extern int MediumComplexity(int baseIterations);

    [DllImport("minimal_c_lib", EntryPoint = "high_complexity_function")]
    public static extern int HighComplexity(int baseIterations);

    // Signal sending (same as before)
    [DllImport("libc", EntryPoint = "tgkill")]
    public static extern int Tgkill(int tgid, int tid, int sig);

    [DllImport("libc", EntryPoint = "getpid")]
    public static extern int Getpid();

    [DllImport("libc", EntryPoint = "syscall")]
    public static extern long Syscall(long number);
}

internal static class Program
{
    private const int SYS_GETTID = 186;
    private const int CoreClrActivationSignal = 34; // SIGRTMIN

    private static volatile bool s_running = true;

    public static int Main(string[] args)
    {
        // Parse complexity level from args
        int complexityLevel = 1; // 1=minimal, 2=medium, 3=high
        if (args.Length > 0 && int.TryParse(args[0], out var level))
        {
            complexityLevel = Math.Max(1, Math.Min(3, level));
        }

        var workers    = GetIntEnv("REPRO_WORKERS",    8);
        var iters      = GetIntEnv("REPRO_ITERATIONS", 1000);
        var intervalUs = GetIntEnv("REPRO_INTERVAL_US", 1);

        Console.Error.WriteLine(
            $"[minimal-c] workers={workers} iters={iters} interval={intervalUs}µs " +
            $"complexity={complexityLevel} gc={GCSettings.IsServerGC} pid={Environment.ProcessId}");

        // Start signal sender
        var signalSender = StartSignalSender(intervalUs);

        try
        {
            // Start worker threads
            var tasks = Enumerable.Range(0, workers)
                .Select(i => Task.Run(() => WorkerThread(i, iters, complexityLevel)))
                .ToArray();

            Task.WaitAll(tasks);
            Console.Error.WriteLine("[minimal-c] All workers completed without crash");
            return 0;
        }
        finally
        {
            s_running = false;
            signalSender.Join(1000);
        }
    }

    // Worker thread with progressive complexity
    private static void WorkerThread(int workerId, int iterations, int complexityLevel)
    {
        Console.Error.WriteLine($"[worker-{workerId}] Starting complexity level {complexityLevel}");

        for (int i = 0; i < iterations && s_running; i++)
        {
            try
            {
                int result = 0;

                // Progressive complexity based on level
                switch (complexityLevel)
                {
                    case 1: // Minimal complexity
                        result = Native.MinimalComplexity(i % 100 + 10);
                        break;

                    case 2: // Medium complexity
                        result = Native.MediumComplexity(i % 50 + 5);
                        if (i % 20 == 0)
                        {
                            result += Native.MinimalComplexity(i % 200 + 50);
                        }
                        break;

                    case 3: // High complexity
                        result = Native.HighComplexity(i % 20 + 3);
                        if (i % 10 == 0)
                        {
                            result += Native.MediumComplexity(i % 30 + 8);
                        }
                        if (i % 25 == 0)
                        {
                            result += Native.MinimalComplexity(i % 150 + 30);
                        }
                        break;
                }

                // Verify results
                if (result < 0)
                    throw new Exception($"C function returned negative result: {result}");

                // Occasional yield
                if (i % 1000 == 0)
                {
                    Thread.Yield();
                }
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[worker-{workerId}] Exception at iteration {i}: {ex.Message}");
            }
        }

        Console.Error.WriteLine($"[worker-{workerId}] Completed successfully");
    }

    // Signal sender (same as original)
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
                catch { /* ignore signal errors */ }

                Thread.Sleep(TimeSpan.FromMicroseconds(intervalUs));
            }
        }) { IsBackground = true, Name = "signal-sender" };
        t.Start();
        return t;
    }

    private static int GetIntEnv(string name, int defaultValue)
    {
        var value = Environment.GetEnvironmentVariable(name);
        return int.TryParse(value, out var parsed) ? parsed : defaultValue;
    }
}