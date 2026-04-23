# Workaround Guide

This guide explains how to apply the sigaltstack workaround to protect your .NET applications from the CoreCLR sigaltstack overflow bug while waiting for upstream fixes.

## When You Need This Workaround

Apply this workaround if your .NET application:
- ✅ P/Invokes into Go c-shared libraries (`-buildmode=c-shared`)
- ✅ Runs under signal pressure (GC, profilers, debuggers)
- ✅ Uses ThreadPool workers for calls to Go code
- ✅ Experiences crashes with `SIGSEGV` in signal handlers

## The Workaround

The workaround installs a larger (1MB) sigaltstack on each thread before it makes its first call to Go code. This prevents overflow when signal handlers exceed CoreCLR's default 16KB limit.

## Implementation

### Step 1: Add the C Helper

Copy `sigstack_helper.c` from this repository to your project:

```c
// sigstack_helper.c
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef LARGE_SIGSTACK_SIZE
#define LARGE_SIGSTACK_SIZE (1 * 1024 * 1024) /* 1 MiB */
#endif

static __thread int large_sigstack_installed;
static __thread void* large_sigstack_base;

void ensure_large_sigaltstack(void) {
    if (large_sigstack_installed) return;

    stack_t cur;
    if (sigaltstack(NULL, &cur) != 0) {
        fprintf(stderr, "ensure_large_sigaltstack: sigaltstack(query) failed: %s\n",
                strerror(errno));
        return;
    }

    if ((cur.ss_flags & SS_DISABLE) == 0 && cur.ss_size >= LARGE_SIGSTACK_SIZE) {
        large_sigstack_installed = 1;
        return;
    }

    long pagesize = sysconf(_SC_PAGESIZE);
    size_t total = (size_t)pagesize + LARGE_SIGSTACK_SIZE;
    void* base = mmap(NULL, total, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (base == MAP_FAILED) {
        fprintf(stderr, "ensure_large_sigaltstack: mmap failed: %s\n",
                strerror(errno));
        return;
    }
    
    if (mprotect(base, (size_t)pagesize, PROT_NONE) != 0) {
        fprintf(stderr, "ensure_large_sigaltstack: mprotect(guard) failed: %s\n",
                strerror(errno));
    }

    stack_t ss = {
        .ss_sp    = (char*)base + pagesize,
        .ss_flags = 0,
        .ss_size  = LARGE_SIGSTACK_SIZE,
    };
    if (sigaltstack(&ss, NULL) != 0) {
        fprintf(stderr, "ensure_large_sigaltstack: sigaltstack(install) failed: %s\n",
                strerror(errno));
        munmap(base, total);
        return;
    }

    large_sigstack_base = base;
    large_sigstack_installed = 1;
}
```

### Step 2: Build the Helper

```bash
gcc -O2 -fPIC -shared -o libsigstack_helper.so sigstack_helper.c -lpthread
```

Or add to your existing native library build.

### Step 3: Add P/Invoke Declaration

```csharp
[DllImport("sigstack_helper", EntryPoint = "ensure_large_sigaltstack")]
public static extern void EnsureLargeSigaltstack();
```

### Step 4: Call Before First Go Interaction

```csharp
// Call once per thread, before first P/Invoke to Go
EnsureLargeSigaltstack();

// Now safe to call Go functions
[DllImport("your_go_library")]
static extern int YourGoFunction();

var result = YourGoFunction();
```

## Integration Patterns

### Pattern 1: ThreadPool Worker Initialization

```csharp
public static void ProcessItem(WorkItem item)
{
    // Ensure large sigaltstack on first call per thread
    EnsureLargeSigaltstack();
    
    // Now safe to call Go code
    var result = GoProcessItem(item.Data);
    // ... handle result
}
```

### Pattern 2: Lazy Initialization

```csharp
private static readonly ThreadLocal<bool> _initialized = new(() => false);

private static void EnsureInitialized()
{
    if (!_initialized.Value)
    {
        EnsureLargeSigaltstack();
        _initialized.Value = true;
    }
}

[DllImport("go_library")]
private static extern int GoFunction(IntPtr data);

public static int SafeGoCall(IntPtr data)
{
    EnsureInitialized();
    return GoFunction(data);
}
```

### Pattern 3: Wrapper Class

```csharp
public static class GoInterop
{
    private static readonly ThreadLocal<bool> _stackInitialized = new(() => false);
    
    [DllImport("sigstack_helper")]
    private static extern void EnsureLargeSigaltstack();
    
    [DllImport("go_library")]
    private static extern int go_ping();
    
    [DllImport("go_library")]  
    private static extern int go_process(IntPtr data, int len);
    
    private static void EnsureStack()
    {
        if (!_stackInitialized.Value)
        {
            EnsureLargeSigaltstack();
            _stackInitialized.Value = true;
        }
    }
    
    public static int Ping()
    {
        EnsureStack();
        return go_ping();
    }
    
    public static int Process(ReadOnlySpan<byte> data)
    {
        EnsureStack();
        fixed (byte* ptr = data)
        {
            return go_process((IntPtr)ptr, data.Length);
        }
    }
}
```

## Deployment

### Local Development
```bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/path/to/your/native/libs
dotnet run
```

### Production (systemd)
```ini
[Service]
Environment="LD_LIBRARY_PATH=/opt/myapp/lib"
ExecStart=/opt/myapp/MyApp
```

### Production (Docker)
```dockerfile
COPY libsigstack_helper.so /usr/local/lib/
RUN ldconfig
```

### Production (NuGet Package)
Include the native library in your NuGet package's `runtimes/linux-x64/native/` directory.

## Verification

### Test the Workaround Works
```csharp
[DllImport("sigstack_helper")]
static extern int dump_sigaltstack(string tag);

// Before workaround
dump_sigaltstack("before");  // Should show ss_size=16384

EnsureLargeSigaltstack();

// After workaround  
dump_sigaltstack("after");   // Should show ss_size=1048576
```

### Monitor for Crashes
The workaround is successful when:
- ✅ No more `SIGSEGV` crashes in signal handlers
- ✅ Application continues running under signal pressure
- ✅ No performance regression

## Performance Impact

### Memory Usage
- **Per thread**: +1MB virtual address space 
- **Physical memory**: Only touched pages (typically <100KB)
- **Total cost**: Scales with active thread count

### Runtime Overhead
- **First call per thread**: ~1µs (mmap + sigaltstack syscalls)
- **Subsequent calls**: <1ns (thread-local check)
- **Signal handling**: No change (same alt stack, just larger)

### Production Monitoring
Monitor these metrics after deployment:
- Process virtual memory size (should increase by ~threads × 1MB)
- Signal-related crashes (should decrease to zero)
- Application throughput (should be unchanged)

## Limitations

This workaround:
- ✅ **Fixes**: CoreCLR 16KB sigaltstack overflow
- ✅ **Compatible**: Works with any .NET version, any Go version
- ✅ **Safe**: No API changes, backward compatible
- ❌ **Doesn't fix**: Go's own 32KB gsignal stack overflow (separate issue)
- ❌ **Memory cost**: +1MB VA per thread (acceptable for most apps)

## Troubleshooting

### "Function not found" errors
Ensure `libsigstack_helper.so` is in `LD_LIBRARY_PATH` or `/usr/local/lib`.

### Still crashing after workaround
1. Verify the workaround is called before first Go interaction per thread
2. Check that crashes are actually in signal handlers (`gdb`, core dumps)
3. Consider if you're hitting the separate Go runtime bug (32KB limit)

### Memory usage concerns
1. Monitor actual RSS, not virtual memory (VmSize vs VmRSS in `/proc/pid/status`)
2. Consider reducing stack size from 1MB to 256KB if memory is constrained:
   ```c
   #define LARGE_SIGSTACK_SIZE (256 * 1024)
   ```

## Migration Path

### Short Term
Apply this workaround to production applications immediately.

### Long Term
- **CoreCLR fix**: Remove workaround after dotnet/runtime fixes PAL
- **Go fix**: May still need workaround for Go c-shared overflow until Go runtime fix

The workaround can be safely removed once upstream fixes are deployed.

## Example Application

See `/dotnet-go-reproducer/` for a complete example that demonstrates both the problem and the workaround solution.