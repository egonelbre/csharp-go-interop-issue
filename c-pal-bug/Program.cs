// .NET host that demonstrates CoreCLR's sigaltstack overflow bug using
// a pure C library instead of Go. This proves that Go is incidental —
// the bug fires when ANY unmanaged P/Invoke is interrupted by CoreCLR's
// activation signal (SIGRTMIN).
//
// Two modes:
//
//   REPRO_MODE=signal  (default)
//     A dedicated thread fires kernel signal 34 (= glibc SIGRTMIN =
//     CoreCLR PAL's INJECT_ACTIVATION_SIGNAL) at every other thread
//     every REPRO_INTERVAL_US microseconds. This synthesizes what
//     CoreCLR's GC / tiered-JIT machinery fires naturally.
//
//   REPRO_MODE=gc
//     No synthetic signal sender. Each worker allocates garbage between
//     ping() calls, and a dedicated thread forces GC.Collect() at a high
//     rate. Let CoreCLR's own GC fire INJECT_ACTIVATION_SIGNAL at the
//     TP Workers while they're inside the C call.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

internal static class Native
{
    [DllImport("clib", EntryPoint = "ping")]
    public static extern int Ping();

    [DllImport("clib", EntryPoint = "set_managed_callback")]
    public static extern void SetManagedCallback(ManagedCallbackDelegate callback);

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

// Callback delegate for complex managed/unmanaged transitions
[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int ManagedCallbackDelegate(int value);

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
        var workers    = GetIntEnv("REPRO_WORKERS",    64);  // Match Go reproducer exactly
        var iters      = GetIntEnv("REPRO_ITERATIONS", 5_000_000);  // Match Go reproducer exactly
        var intervalUs = GetIntEnv("REPRO_INTERVAL_US", 1);   // EXTREMELY aggressive - 1µs intervals
        var allocBytes = GetIntEnv("REPRO_ALLOC_BYTES", 32 * 1024);

        var useFix = Environment.GetEnvironmentVariable("REPRO_FIX") == "1";
        var probeMode = Environment.GetEnvironmentVariable("REPRO_PROBE") == "1";

        Console.Error.WriteLine(
            $"[c-pal-repro] mode={mode} workers={workers} iters={iters} "
          + $"interval={intervalUs}µs gc={GCSettings.IsServerGC} "
          + $"fix={useFix} pid={Environment.ProcessId}");

        if (useFix) Native.EnsureLargeSigaltstack(); // main thread

        // Set up managed callback for complex transition scenarios
        Native.SetManagedCallback(ManagedTransitionCallback);
        Native.Ping(); // warm the C library

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
                // Install the large sigaltstack BEFORE the first ping() on
                // this threadpool thread. This overwrites CoreCLR's 16KB
                // with a 1MB stack if the fix is enabled.
                if (useFix) Native.EnsureLargeSigaltstack();
                // Probe sigaltstack state before any library interaction.
                if (probeMode) Native.DumpSigaltstack("before-first");
                for (int k = 0; k < iters; k++)
                {
                    // Probe both sides of a small subset of calls to monitor
                    // sigaltstack state without excessive logging.
                    if (probeMode && k < 10) Native.DumpSigaltstack("before");

                    // Create scenarios where signals arrive during expensive IP boundary analysis
                    // The key is forcing IsIPInEpilog/IsIPInProlog to take deep analysis paths

                    // Pattern 1: JIT compilation storm during P/Invoke
                    // Force active JIT compilation when signal arrives
                    Parallel.For(0, 8, i => {
                        // Each iteration creates new JIT compilation work
                        CreateJitStorm(k + i);
                        Thread.Yield(); // Create scheduling complexity

                        // P/Invoke right during JIT activity - this creates complex IP analysis
                        if (Native.Ping() != 42)
                            throw new Exception($"ping failed in JIT storm {i}");
                    });

                    // Pattern 2: Deep managed call stacks with P/Invoke at boundaries
                    CreateDeepManagedCallStackWithPInvoke(8);

                    if (probeMode && k < 10) Native.DumpSigaltstack("after");

                    // Always generate garbage to increase GC pressure, which makes
                    // g_activationFunction take deeper paths during suspension
                    GenerateGarbage(allocBytes);
                }
            });
        }

        Task.WaitAll(tasks);
        s_running = false;
        driver?.Join();
        Console.Error.WriteLine("[c-pal-repro] PASS");
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
                // (including ones currently in native code), which is the
                // INJECT_ACTIVATION_SIGNAL code path we want exercised.
                GC.Collect(2, GCCollectionMode.Forced, blocking: true);
                Thread.Sleep(TimeSpan.FromMicroseconds(intervalUs));
            }
        }) { IsBackground = true, Name = "gc-driver" };
        t.Start();
        return t;
    }

    // Burn `bytes` worth of short-lived allocations to keep GC busy
    // between C library calls.
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

    // Managed callback that creates complex transition scenarios
    // Called from unmanaged code, creating unmanaged → managed → unmanaged transitions
    private static int ManagedTransitionCallback(int value)
    {
        // Create complex managed state that might complicate IP analysis
        // when signals arrive during the callback transition

        // Force JIT compilation boundaries
        var dummy = new List<object>();
        for (int i = 0; i < value % 10; i++)
        {
            dummy.Add(new { Value = i, Timestamp = DateTime.Now.Ticks });
        }

        // Force managed exception handling that creates complex call stacks
        try
        {
            if (value % 17 == 0)
                throw new ArgumentException("Synthetic exception for complex IP state");

            // Threading complexity
            Thread.Yield();
            return value * 2 + dummy.Count;
        }
        catch (ArgumentException)
        {
            // Exception handling in callback creates very complex transition state
            GC.KeepAlive(dummy);
            return value + 1;
        }
    }

    // Force complex managed state before P/Invoke
    private static void ForceComplexManagedState()
    {
        // Create complex call stacks and state that make IP analysis expensive
        var tasks = new List<Task<int>>();

        for (int i = 0; i < 3; i++)
        {
            int capturedI = i;
            tasks.Add(Task.Run(() => {
                // Async context creates complex managed state
                Thread.Sleep(1); // Force async scheduler complexity
                return capturedI * capturedI;
            }));
        }

        // Complex LINQ that forces JIT compilation
        var result = tasks.SelectMany(t => new[] { t.Id, t.Status.GetHashCode() })
                          .Where(x => x % 2 == 0)
                          .Sum();

        // Keep everything alive to maintain complex state
        GC.KeepAlive(tasks);
        GC.KeepAlive(result);
    }

    // Force JIT compilation during transition windows
    private static void ForceJitCompilation()
    {
        // Create new generic instantiations and complex method calls
        // that force JIT compilation during signal-sensitive windows

        var dict = new Dictionary<string, object>();
        var random = new Random();

        // Generic method instantiation
        ProcessGenericData(random.Next().ToString(), DateTime.Now);
        ProcessGenericData(random.NextDouble(), TimeSpan.FromTicks(DateTime.Now.Ticks));

        dict[$"key_{Thread.CurrentThread.ManagedThreadId}"] = new {
            ThreadId = Thread.CurrentThread.ManagedThreadId,
            Timestamp = DateTime.Now,
            Random = random.Next()
        };

        GC.KeepAlive(dict);
    }

    // Generic method that creates JIT compilation complexity
    private static void ProcessGenericData<T>(T data, object metadata) where T : notnull
    {
        var processor = new Dictionary<Type, List<object>>
        {
            [typeof(T)] = new List<object> { data, metadata }
        };

        // Force virtual dispatch and interface calls
        ICollection<KeyValuePair<Type, List<object>>> collection = processor;
        foreach (var item in collection)
        {
            Thread.Yield(); // Yield during complex generic processing
        }

        GC.KeepAlive(processor);
    }

    // Create JIT compilation storm - forces active compilation when signals arrive
    // This makes IsIPInEpilog analysis expensive due to complex method boundaries
    private static void CreateJitStorm(int seed)
    {
        // Create unique generic instantiations that force JIT compilation
        var random = new Random(seed);

        // Force JIT of different generic instantiations
        ProcessUniqueGeneric(seed.ToString(), typeof(int));
        ProcessUniqueGeneric(random.NextDouble(), typeof(double));
        ProcessUniqueGeneric(DateTime.Now.AddSeconds(seed), typeof(DateTime));

        // Create complex LINQ expressions that force JIT of complex call chains
        var data = Enumerable.Range(0, 100 + seed % 50)
                           .Select(i => new { Id = i, Hash = i.GetHashCode() })
                           .Where(x => x.Hash % (seed % 7 + 2) == 0)
                           .GroupBy(x => x.Hash % 3)
                           .ToDictionary(g => g.Key, g => g.ToList());

        GC.KeepAlive(data);
    }

    // Forces JIT compilation of unique generic methods per call
    private static void ProcessUniqueGeneric<T>(T value, Type metadata)
    {
        // Create method instantiation that hasn't been JIT'd before
        var processor = new Func<T, string>(v => $"{typeof(T).Name}:{v}:{metadata.Name}");
        var result = processor(value);

        // Force virtual calls and interface dispatch
        IFormattable formattable = $"Result_{result}_{Thread.CurrentThread.ManagedThreadId}";
        var formatted = formattable.ToString("G", null);

        GC.KeepAlive(formatted);
    }

    // Create deep managed call stack with P/Invoke at method boundaries
    // This creates complex IP analysis scenarios when signals arrive
    private static int CreateDeepManagedCallStackWithPInvoke(int depth)
    {
        if (depth <= 0)
        {
            // P/Invoke at the bottom of a deep managed call stack
            // Creates complex IP boundary analysis when signals arrive here
            return Native.Ping();
        }

        // Create complex managed frames that make IP analysis expensive
        var complexData = new Dictionary<string, object>
        {
            [$"depth_{depth}"] = new {
                Depth = depth,
                ThreadId = Thread.CurrentThread.ManagedThreadId,
                Timestamp = DateTime.Now.Ticks
            }
        };

        try
        {
            // Exception handling adds complexity to IP analysis
            if (depth % 7 == 0)
                throw new NotImplementedException($"Synthetic exception at depth {depth}");

            // Recursive call creates deep managed stack
            var result = CreateDeepManagedCallStackWithPInvoke(depth - 1);

            // P/Invoke also on the way back up the stack
            if (depth % 3 == 0)
            {
                var pingResult = Native.Ping();
                return result + pingResult;
            }

            return result + complexData.Count;
        }
        catch (NotImplementedException)
        {
            // Exception handling at various depths creates complex IP scenarios
            GC.KeepAlive(complexData);

            // P/Invoke in exception handler - very complex IP analysis scenario
            return Native.Ping() + depth;
        }
    }
}