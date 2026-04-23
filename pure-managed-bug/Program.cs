// Pure managed reproducer for CoreCLR sigaltstack overflow
// DOTNET_ISSUE.md states: "Pure managed code also triggers it if the interrupted PC
// sits at a point where g_activationFunction takes a deep path (GC suspension under load)."
//
// This reproducer creates complex managed scenarios with no P/Invoke to trigger
// the expensive IP analysis paths in HandleSuspensionForInterruptedThread.

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
        var allocBytes = GetIntEnv("REPRO_ALLOC_BYTES", 64 * 1024);

        Console.Error.WriteLine(
            $"[pure-managed] workers={workers} iters={iters} "
          + $"interval={intervalUs}µs gc={GCSettings.IsServerGC} "
          + $"pid={Environment.ProcessId}");
        Console.Error.WriteLine("[pure-managed] Pure managed code + GC pressure + SIGRTMIN");

        // Start signal sender to fire SIGRTMIN during complex managed work
        var signalSender = StartSignalSender(intervalUs);

        // Start complex managed work that should trigger expensive IP analysis
        var tasks = new Task[workers];
        for (int i = 0; i < workers; i++)
        {
            tasks[i] = Task.Run(() => ComplexManagedWorkLoop(iters, allocBytes));
        }

        Task.WaitAll(tasks);
        s_running = false;
        if (signalSender != null) signalSender.Join();

        Console.Error.WriteLine("[pure-managed] PASS - no crash detected");
        return 0;
    }

    // Complex managed work loop designed to create expensive IP analysis scenarios
    private static void ComplexManagedWorkLoop(int iterations, int allocBytes)
    {
        for (int i = 0; i < iterations; i++)
        {
            // Pattern 1: Deep managed call stacks with complex boundaries
            // Creates scenarios where IP analysis becomes expensive
            var result = CreateDeepManagedCallStack(12, i);

            // Pattern 2: Complex GC pressure scenarios
            // Forces expensive GC suspension analysis
            if (i % 100 == 0)
            {
                CreateGCPressureScenario(allocBytes);
            }

            // Pattern 3: Complex managed state during signal windows
            CreateComplexManagedState(result + i);

            // Frequent yields to create scheduler complexity during signal arrival
            if (i % 10 == 0) Thread.Yield();
        }
    }

    // Create deep managed call stacks that make IP boundary analysis expensive
    private static int CreateDeepManagedCallStack(int depth, int seed)
    {
        if (depth <= 0)
        {
            // At the bottom: complex managed work that creates analysis challenges
            return PerformComplexManagedComputation(seed);
        }

        try
        {
            // Create complex exception boundaries that complicate IP analysis
            if (depth % 7 == 0)
                throw new InvalidOperationException($"Complex exception at depth {depth}");

            // Recursive managed calls with complex state
            var intermediate = CreateDeepManagedCallStack(depth - 1, seed + depth);

            // More complex managed work on the way back up
            var complex = CreateComplexDataStructures(depth, intermediate);
            return ProcessComplexData(complex) + intermediate;
        }
        catch (InvalidOperationException)
        {
            // Exception handling creates very complex IP analysis scenarios
            var recovery = RecoverFromComplexException(depth, seed);
            return recovery + CreateDeepManagedCallStack(depth - 1, seed * 2);
        }
    }

    // Perform complex managed computation that creates analysis challenges
    private static int PerformComplexManagedComputation(int seed)
    {
        // Complex LINQ chains that create expensive IP boundaries
        var data = Enumerable.Range(0, seed % 1000 + 100)
                           .Select(i => new { Id = i, Hash = i.GetHashCode(), Thread = Thread.CurrentThread.ManagedThreadId })
                           .Where(x => x.Hash % 7 != 0)
                           .GroupBy(x => x.Hash % 11)
                           .SelectMany(g => g.Select(item => new { Group = g.Key, Item = item }))
                           .OrderBy(x => x.Item.Id)
                           .ToList();

        // Force complex virtual dispatch and interface calls
        var result = data.Select(x => x.Item.Id).Sum();

        GC.KeepAlive(data);
        return result % 1000;
    }

    // Create complex data structures that make IP analysis expensive
    private static Dictionary<string, List<object>> CreateComplexDataStructures(int depth, int seed)
    {
        var complex = new Dictionary<string, List<object>>();

        for (int i = 0; i < depth + 10; i++)
        {
            var key = $"complex_{depth}_{i}_{Thread.CurrentThread.ManagedThreadId}";
            complex[key] = new List<object>
            {
                new { Depth = depth, Index = i, Seed = seed },
                DateTime.Now.AddTicks(i),
                Enumerable.Range(0, i % 5 + 1).Select(j => j * i).ToArray(),
                new Func<int, string>(x => $"lambda_{x}_{i}")
            };
        }

        return complex;
    }

    // Process complex data with expensive managed operations
    private static int ProcessComplexData(Dictionary<string, List<object>> data)
    {
        var result = 0;

        // Complex iteration patterns that create IP analysis challenges
        foreach (var kvp in data)
        {
            foreach (var item in kvp.Value)
            {
                // Force type checking and virtual dispatch
                result += item.GetHashCode() % 1000;

                // Complex string operations
                var str = item.ToString();
                if (str.Length > 10)
                    result += str.Substring(0, Math.Min(10, str.Length)).GetHashCode() % 100;
            }
        }

        return result;
    }

    // Recovery from exceptions with complex managed state
    private static int RecoverFromComplexException(int depth, int seed)
    {
        var recovery = new List<Task<int>>();

        // Create async complexity during exception recovery
        for (int i = 0; i < 3; i++)
        {
            int captured = i;
            recovery.Add(Task.Run(() => {
                Thread.Sleep(1); // Brief async work
                return depth + seed + captured;
            }));
        }

        return recovery.Select(t => t.Result).Sum() % 1000;
    }

    // Create GC pressure scenarios that force expensive suspension analysis
    private static void CreateGCPressureScenario(int allocBytes)
    {
        var data = new List<object>();

        // Create diverse allocation patterns that stress GC
        for (int i = 0; i < 50; i++)
        {
            data.Add(new byte[allocBytes / 50]);
            data.Add(new object[100]);
            data.Add(new string('x', i + 10));
            data.Add(Enumerable.Range(0, 20).Select(j => new { Id = j, Data = new byte[100] }).ToList());
        }

        // Force GC pressure at specific moments
        if (data.Count > 150)
        {
            GC.Collect(0, GCCollectionMode.Default);
        }

        GC.KeepAlive(data);
    }

    // Create complex managed state during signal windows
    private static void CreateComplexManagedState(int seed)
    {
        // Complex generic instantiations
        ProcessGenericComplexity(seed, typeof(int));
        ProcessGenericComplexity(seed.ToString(), typeof(string));
        ProcessGenericComplexity(DateTime.Now, typeof(DateTime));

        // Complex delegate and event scenarios
        var action = new Action<int>(x => {
            var nested = new Func<int, string>(y => $"nested_{y}_{x}");
            GC.KeepAlive(nested(x));
        });

        action(seed % 1000);
    }

    // Process generic complexity that creates JIT and analysis challenges
    private static void ProcessGenericComplexity<T>(T value, Type metadata)
    {
        var processor = new Dictionary<Type, List<T>>
        {
            [typeof(T)] = new List<T> { value },
            [metadata] = new List<T>()
        };

        // Force complex generic method calls
        var result = ProcessGenericData(processor, value);
        GC.KeepAlive(result);
    }

    // Generic method that forces complex IP boundary analysis
    private static string ProcessGenericData<T>(Dictionary<Type, List<T>> data, T value)
    {
        var result = "";
        foreach (var kvp in data)
        {
            result += $"{typeof(T).Name}:{kvp.Key.Name}:{value}:";
        }
        return result;
    }

    // Signal sender that fires SIGRTMIN at managed threads
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
                catch { /* thread list churns */ }
                Thread.Sleep(TimeSpan.FromMicroseconds(intervalUs));
            }
        }) { IsBackground = true, Name = "signal-sender" };
        t.Start();
        return t;
    }

    private static int GetIntEnv(string name, int def)
    {
        var s = Environment.GetEnvironmentVariable(name);
        return int.TryParse(s, out var v) && v > 0 ? v : def;
    }
}