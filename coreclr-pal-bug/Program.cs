using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

internal static class Native
{
    [DllImport("stack_probe")]
    public static extern int install_probe_handler(int sig, uint probe_bytes);

    [DllImport("stack_probe")]
    public static extern int dump_sigaltstack([MarshalAs(UnmanagedType.LPStr)] string tag);

    [DllImport("stack_probe")]
    public static extern int noop();

    [DllImport("stack_probe")]
    public static extern int fire_signal_at_all_threads(int sig);
}

internal static class Program
{
    // SIGUSR1 = signal 10. Safe choice that doesn't conflict with
    // CoreCLR's own signal handlers (SIGRTMIN, SIGSEGV, etc.)
    private const int ReproSignal = 10; // SIGUSR1

    // 64KB probe - this will overflow CoreCLR's 16KB sigaltstack.
    // Size is chosen to be larger than CoreCLR's alt stack but
    // representative of real signal handler chains (CoreCLR's own
    // functions have chkstk prologues up to 24KB).
    private const int ProbeBytes = 64 * 1024;

    private static volatile bool s_running = true;

    public static int Main()
    {
        var workers    = GetIntEnv("REPRO_WORKERS",    32);
        var iters      = GetIntEnv("REPRO_ITERATIONS", 10_000_000);
        var intervalUs = GetIntEnv("REPRO_INTERVAL_US", 50);

        Console.Error.WriteLine($"[repro] CoreCLR sigaltstack overflow reproducer");
        Console.Error.WriteLine($"[repro] pid={Environment.ProcessId} workers={workers} probe_bytes={ProbeBytes}");
        Console.Error.WriteLine($"[repro] Expected: SIGSEGV crash when {ProbeBytes}-byte probe overflows CoreCLR's 16KB alt stack");
        Console.Error.WriteLine();

        // Show main thread's initial sigaltstack state
        Native.dump_sigaltstack("main-thread-init");

        // Install our stack-hungry SA_ONSTACK handler for SIGUSR1
        if (Native.install_probe_handler(ReproSignal, (uint)ProbeBytes) != 0)
        {
            Console.Error.WriteLine("ERROR: install_probe_handler failed");
            return 1;
        }
        Console.Error.WriteLine($"[repro] Installed {ProbeBytes}-byte probe handler for signal {ReproSignal}");

        // Signal sender thread: fires SIGUSR1 at all threads repeatedly
        var sender = new Thread(() =>
        {
            int iteration = 0;
            while (s_running)
            {
                int count = Native.fire_signal_at_all_threads(ReproSignal);
                if (++iteration % 1000 == 0)
                {
                    Console.Error.WriteLine($"[sender] iteration {iteration}, signaled {count} threads");
                }
                Thread.Sleep(TimeSpan.FromMicroseconds(intervalUs));
            }
        }) { IsBackground = true, Name = "probe-sender" };

        sender.Start();
        Console.Error.WriteLine($"[repro] Started signal sender (interval {intervalUs}µs)");

        // ThreadPool workers: busy loop calling noop() so they're live targets.
        // These workers run on CoreCLR-created threads with 16KB sigaltstacks.
        var tasks = new Task[workers];
        for (int i = 0; i < workers; i++)
        {
            int wid = i;
            tasks[i] = Task.Run(() =>
            {
                // Dump one worker's sigaltstack to prove CoreCLR installed 16KB
                if (wid == 0)
                {
                    Native.dump_sigaltstack($"tp-worker-{wid}");
                }

                // Busy loop - each worker becomes a signal target
                for (int k = 0; k < iters && s_running; k++)
                {
                    Native.noop();
                }
            });
        }

        Console.Error.WriteLine($"[repro] Started {workers} ThreadPool workers");
        Console.Error.WriteLine($"[repro] Waiting for crash... (budget: {iters} iterations per worker)");

        Task.WaitAll(tasks);
        s_running = false;
        sender.Join();

        // If we reach here, no crash occurred within the iteration budget
        Console.Error.WriteLine("[repro] UNEXPECTED: No crash within iteration budget");
        Console.Error.WriteLine("[repro] This suggests the probe didn't overflow or memory below alt stack was mapped");
        return 0;
    }

    private static int GetIntEnv(string name, int def)
    {
        var s = Environment.GetEnvironmentVariable(name);
        return int.TryParse(s, out var v) && v > 0 ? v : def;
    }
}