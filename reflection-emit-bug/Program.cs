// Reflection.Emit reproducer for CoreCLR sigaltstack overflow
// Theory: Dynamic code generation creates complex IP analysis scenarios.
// Dynamic methods have unusual metadata boundaries that could stress
// the IsIPInEpilog/IsIPInProlog analysis enough to overflow 16KB sigaltstack.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
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
    private static readonly List<Delegate> s_dynamicMethods = new();

    public static int Main()
    {
        var workers    = GetIntEnv("REPRO_WORKERS",    64);
        var iters      = GetIntEnv("REPRO_ITERATIONS", 5_000_000);
        var intervalUs = GetIntEnv("REPRO_INTERVAL_US", 1);  // Very aggressive
        var methodCount = GetIntEnv("REPRO_METHODS", 1000);  // Dynamic methods to generate

        Console.Error.WriteLine(
            $"[reflection-emit] workers={workers} iters={iters} "
          + $"interval={intervalUs}µs methods={methodCount} gc={GCSettings.IsServerGC} "
          + $"pid={Environment.ProcessId}");
        Console.Error.WriteLine("[reflection-emit] Dynamic code generation + SIGRTMIN");

        // Pre-generate complex dynamic methods to stress IP boundary analysis
        GenerateDynamicMethods(methodCount);

        // Start signal sender to fire SIGRTMIN during dynamic method execution
        var signalSender = StartSignalSender(intervalUs);

        try
        {
            // Start worker threads that execute dynamic methods
            var tasks = Enumerable.Range(0, workers)
                .Select(i => Task.Run(() => WorkerThread(i, iters)))
                .ToArray();

            Task.WaitAll(tasks);
            Console.Error.WriteLine("[reflection-emit] All workers completed without crash");
            return 0;
        }
        finally
        {
            s_running = false;
            signalSender.Wait(1000);
        }
    }

    private static void GenerateDynamicMethods(int count)
    {
        Console.Error.WriteLine($"[reflection-emit] Generating {count} dynamic methods...");

        for (int i = 0; i < count; i++)
        {
            // Create increasingly complex dynamic methods
            var complexity = Math.Min(i / 100, 10); // Complexity level 0-10

            switch (i % 5)
            {
                case 0: s_dynamicMethods.Add(CreateComplexArithmeticMethod(i, complexity)); break;
                case 1: s_dynamicMethods.Add(CreateExceptionHandlingMethod(i, complexity)); break;
                case 2: s_dynamicMethods.Add(CreateLoopingMethod(i, complexity)); break;
                case 3: s_dynamicMethods.Add(CreateBranchingMethod(i, complexity)); break;
                case 4: s_dynamicMethods.Add(CreateCallChainMethod(i, complexity)); break;
            }
        }

        Console.Error.WriteLine($"[reflection-emit] Generated {s_dynamicMethods.Count} dynamic methods");
    }

    private static Func<int, int> CreateComplexArithmeticMethod(int id, int complexity)
    {
        var method = new DynamicMethod($"ComplexArithmetic_{id}", typeof(int), new[] { typeof(int) });
        var il = method.GetILGenerator();

        // Complex arithmetic with many locals and operations
        var locals = new LocalBuilder[complexity + 5];
        for (int i = 0; i < locals.Length; i++)
        {
            locals[i] = il.DeclareLocal(typeof(int));
        }

        // Load input parameter
        il.Emit(OpCodes.Ldarg_0);
        il.Emit(OpCodes.Stloc_0);

        // Create complex arithmetic chain
        for (int i = 0; i < complexity * 10; i++)
        {
            var localIdx = i % locals.Length;
            il.Emit(OpCodes.Ldloc, localIdx);
            il.Emit(OpCodes.Ldc_I4, i + 1);
            il.Emit(OpCodes.Add);
            il.Emit(OpCodes.Ldc_I4, 7);
            il.Emit(OpCodes.Mul);
            il.Emit(OpCodes.Ldc_I4, 3);
            il.Emit(OpCodes.Rem);
            il.Emit(OpCodes.Stloc, (localIdx + 1) % locals.Length);
        }

        // Return result
        il.Emit(OpCodes.Ldloc_0);
        il.Emit(OpCodes.Ret);

        return (Func<int, int>)method.CreateDelegate(typeof(Func<int, int>));
    }

    private static Func<int, int> CreateExceptionHandlingMethod(int id, int complexity)
    {
        var method = new DynamicMethod($"ExceptionHandling_{id}", typeof(int), new[] { typeof(int) });
        var il = method.GetILGenerator();

        var endLabel = il.DefineLabel();
        var result = il.DeclareLocal(typeof(int));

        // Create nested try-catch blocks
        for (int level = 0; level < complexity + 1; level++)
        {
            var tryStart = il.DefineLabel();
            var catchStart = il.DefineLabel();
            var tryEnd = il.DefineLabel();

            il.MarkLabel(tryStart);
            il.BeginExceptionBlock();

            // Complex work in try block
            il.Emit(OpCodes.Ldarg_0);
            il.Emit(OpCodes.Ldc_I4, level + 1);
            il.Emit(OpCodes.Mul);
            il.Emit(OpCodes.Ldc_I4, 100);
            il.Emit(OpCodes.Rem);
            il.Emit(OpCodes.Stloc, result);

            if (level < complexity)
            {
                // Might throw
                il.Emit(OpCodes.Ldloc, result);
                il.Emit(OpCodes.Ldc_I4_0);
                il.Emit(OpCodes.Ceq);
                var noThrow = il.DefineLabel();
                il.Emit(OpCodes.Brfalse_S, noThrow);
                il.Emit(OpCodes.Newobj, typeof(InvalidOperationException).GetConstructor(Type.EmptyTypes)!);
                il.Emit(OpCodes.Throw);
                il.MarkLabel(noThrow);
            }

            il.BeginCatchBlock(typeof(Exception));
            il.Emit(OpCodes.Pop); // Remove exception from stack
            il.Emit(OpCodes.Ldc_I4, -1);
            il.Emit(OpCodes.Stloc, result);
            il.EndExceptionBlock();
        }

        il.Emit(OpCodes.Ldloc, result);
        il.Emit(OpCodes.Ret);

        return (Func<int, int>)method.CreateDelegate(typeof(Func<int, int>));
    }

    private static Func<int, int> CreateLoopingMethod(int id, int complexity)
    {
        var method = new DynamicMethod($"Looping_{id}", typeof(int), new[] { typeof(int) });
        var il = method.GetILGenerator();

        var accumulator = il.DeclareLocal(typeof(int));
        var counter = il.DeclareLocal(typeof(int));

        // Initialize
        il.Emit(OpCodes.Ldarg_0);
        il.Emit(OpCodes.Stloc, accumulator);
        il.Emit(OpCodes.Ldc_I4_0);
        il.Emit(OpCodes.Stloc, counter);

        // Nested loops with complex control flow
        for (int depth = 0; depth < complexity + 1; depth++)
        {
            var loopStart = il.DefineLabel();
            var loopEnd = il.DefineLabel();
            var continueLabel = il.DefineLabel();

            il.MarkLabel(loopStart);

            // Loop condition
            il.Emit(OpCodes.Ldloc, counter);
            il.Emit(OpCodes.Ldc_I4, 100 + depth * 10);
            il.Emit(OpCodes.Clt);
            il.Emit(OpCodes.Brfalse, loopEnd);

            // Complex loop body with branching
            il.Emit(OpCodes.Ldloc, counter);
            il.Emit(OpCodes.Ldc_I4_2);
            il.Emit(OpCodes.Rem);
            il.Emit(OpCodes.Brtrue, continueLabel);

            // Even iteration: complex arithmetic
            il.Emit(OpCodes.Ldloc, accumulator);
            il.Emit(OpCodes.Ldloc, counter);
            il.Emit(OpCodes.Ldc_I4_3);
            il.Emit(OpCodes.Mul);
            il.Emit(OpCodes.Add);
            il.Emit(OpCodes.Stloc, accumulator);

            il.MarkLabel(continueLabel);

            // Increment counter
            il.Emit(OpCodes.Ldloc, counter);
            il.Emit(OpCodes.Ldc_I4_1);
            il.Emit(OpCodes.Add);
            il.Emit(OpCodes.Stloc, counter);

            il.Emit(OpCodes.Br, loopStart);
            il.MarkLabel(loopEnd);
        }

        il.Emit(OpCodes.Ldloc, accumulator);
        il.Emit(OpCodes.Ret);

        return (Func<int, int>)method.CreateDelegate(typeof(Func<int, int>));
    }

    private static Func<int, int> CreateBranchingMethod(int id, int complexity)
    {
        var method = new DynamicMethod($"Branching_{id}", typeof(int), new[] { typeof(int) });
        var il = method.GetILGenerator();

        var result = il.DeclareLocal(typeof(int));
        var temp = il.DeclareLocal(typeof(int));

        il.Emit(OpCodes.Ldarg_0);
        il.Emit(OpCodes.Stloc, result);

        // Create complex branching tree
        CreateBranchingTree(il, result, temp, complexity + 1, 0);

        il.Emit(OpCodes.Ldloc, result);
        il.Emit(OpCodes.Ret);

        return (Func<int, int>)method.CreateDelegate(typeof(Func<int, int>));
    }

    private static void CreateBranchingTree(ILGenerator il, LocalBuilder result, LocalBuilder temp, int depth, int value)
    {
        if (depth <= 0) return;

        var leftBranch = il.DefineLabel();
        var rightBranch = il.DefineLabel();
        var endBranch = il.DefineLabel();

        // Conditional based on result value
        il.Emit(OpCodes.Ldloc, result);
        il.Emit(OpCodes.Ldc_I4, value + depth);
        il.Emit(OpCodes.And);
        il.Emit(OpCodes.Brfalse, leftBranch);

        // Right branch - more complex
        il.Emit(OpCodes.Ldloc, result);
        il.Emit(OpCodes.Ldc_I4, depth * 7);
        il.Emit(OpCodes.Mul);
        il.Emit(OpCodes.Ldc_I4, 13);
        il.Emit(OpCodes.Add);
        il.Emit(OpCodes.Stloc, result);

        CreateBranchingTree(il, result, temp, depth - 1, value + 1);
        il.Emit(OpCodes.Br, endBranch);

        // Left branch - simpler
        il.MarkLabel(leftBranch);
        il.Emit(OpCodes.Ldloc, result);
        il.Emit(OpCodes.Ldc_I4, depth);
        il.Emit(OpCodes.Add);
        il.Emit(OpCodes.Stloc, result);

        CreateBranchingTree(il, result, temp, depth - 1, value + 2);

        il.MarkLabel(endBranch);
    }

    private static Func<int, int> CreateCallChainMethod(int id, int complexity)
    {
        var method = new DynamicMethod($"CallChain_{id}", typeof(int), new[] { typeof(int) });
        var il = method.GetILGenerator();

        // Create a chain of method calls with complex parameter passing
        var result = il.DeclareLocal(typeof(int));

        il.Emit(OpCodes.Ldarg_0);
        il.Emit(OpCodes.Stloc, result);

        for (int i = 0; i < complexity + 5; i++)
        {
            // Call Math.Abs with complex expressions
            il.Emit(OpCodes.Ldloc, result);
            il.Emit(OpCodes.Ldc_I4, i * 17 - 100);
            il.Emit(OpCodes.Mul);
            il.Emit(OpCodes.Call, typeof(Math).GetMethod(nameof(Math.Abs), new[] { typeof(int) })!);
            il.Emit(OpCodes.Stloc, result);

            // Call custom static method
            il.Emit(OpCodes.Ldloc, result);
            il.Emit(OpCodes.Ldc_I4, i);
            il.Emit(OpCodes.Call, typeof(Program).GetMethod(nameof(StaticComplexWork), BindingFlags.NonPublic | BindingFlags.Static)!);
            il.Emit(OpCodes.Stloc, result);
        }

        il.Emit(OpCodes.Ldloc, result);
        il.Emit(OpCodes.Ret);

        return (Func<int, int>)method.CreateDelegate(typeof(Func<int, int>));
    }

    private static int StaticComplexWork(int input, int modifier)
    {
        // Complex work to stress call chain analysis
        var result = input;
        for (int i = 0; i < 10; i++)
        {
            result = (result * 3 + modifier) % 1000000;
            if (result < 0) result = -result;
        }
        return result;
    }

    private static void WorkerThread(int workerId, int iterations)
    {
        var random = new Random(workerId);
        var methodCount = s_dynamicMethods.Count;

        for (int i = 0; i < iterations && s_running; i++)
        {
            try
            {
                // Randomly select and execute dynamic methods
                var methodIdx = random.Next(methodCount);
                var dynamicMethod = s_dynamicMethods[methodIdx];

                // Execute with varying inputs to create different code paths
                var input = (i * workerId) % 10000;
                var result = ((Func<int, int>)dynamicMethod)(input);

                // Create additional complexity by calling multiple methods
                if (i % 10 == 0)
                {
                    ExecuteMethodChain(random, input, 3);
                }

                // Some GC pressure to complicate runtime state
                if (i % 1000 == 0)
                {
                    var temp = new byte[random.Next(1024, 8192)];
                    temp[0] = (byte)(result & 0xFF);
                }
            }
            catch (Exception ex)
            {
                // Expected from some dynamic methods - continue working
                Console.Error.WriteLine($"[worker-{workerId}] Exception: {ex.Message}");
            }
        }
    }

    private static void ExecuteMethodChain(Random random, int input, int chainLength)
    {
        var result = input;
        var methodCount = s_dynamicMethods.Count;

        for (int i = 0; i < chainLength; i++)
        {
            var methodIdx = random.Next(methodCount);
            var dynamicMethod = (Func<int, int>)s_dynamicMethods[methodIdx];
            result = dynamicMethod(result);
        }
    }

    private static Task StartSignalSender(int intervalMicroseconds)
    {
        return Task.Run(() =>
        {
            var pid = Native.Getpid();
            Console.Error.WriteLine($"[signal-sender] Sending SIGRTMIN every {intervalMicroseconds}µs to pid {pid}");

            while (s_running)
            {
                try
                {
                    Native.Tgkill(pid, (int)Native.Syscall(SYS_GETTID), CoreClrActivationSignal);
                    Thread.SpinWait(intervalMicroseconds * 100); // Rough microsecond delay
                }
                catch
                {
                    // Continue on signal errors
                }
            }
        });
    }

    private static int GetIntEnv(string name, int defaultValue)
    {
        var value = Environment.GetEnvironmentVariable(name);
        return int.TryParse(value, out var parsed) ? parsed : defaultValue;
    }
}