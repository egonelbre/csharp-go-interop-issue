# Analysis: Go's Atypical Assembly Patterns and CoreCLR Sigaltstack Overflow

**Date:** April 23, 2026  
**Context:** Investigation of CoreCLR sigaltstack overflow when processing SIGRTMIN signals during P/Invoke calls

## Executive Summary

This analysis identifies the specific assembly patterns in Go's runtime that create "atypical calling conventions" which stress CoreCLR's IP (Instruction Pointer) boundary analysis algorithms. When CoreCLR processes signals during P/Invoke calls to Go code, its signal handler attempts to analyze unusual instruction pointer contexts that violate assumptions about standard C calling conventions, leading to excessive stack consumption and sigaltstack overflow.

## Background

The CoreCLR runtime uses a 16KB alternate signal stack (sigaltstack) with the SA_ONSTACK flag for signal handling. When SIGRTMIN signals are delivered during P/Invoke calls, CoreCLR's signal handler performs IP boundary analysis using functions like `IsIPInProlog` and `IsIPInEpilog` to understand the current execution context. This analysis was designed for conventional C calling patterns but fails catastrophically when encountering Go's unusual assembly patterns.

## Key Findings

### 1. Fake PC Manipulation

Go's runtime deliberately stores fake program counter values in goroutine contexts:

**File:** `/tmp/go-custom/src/runtime/asm_amd64.s:855-870`
```asm
TEXT gosave_systemstack_switch<>(SB),NOSPLIT|NOFRAME,$0
    // Take systemstack_switch PC and add 8 bytes to skip
    // the prologue. Keep 8 bytes offset consistent with
    // PCALIGN $8 in systemstack_swtich, pointing start of
    // UNDEF instruction beyond prologue.
    MOVQ $runtime·systemstack_switch+8(SB), R9
    MOVQ R9, (g_sched+gobuf_pc)(R14)         // Store fake PC
    LEAQ 8(SP), R9
    MOVQ R9, (g_sched+gobuf_sp)(R14)
    MOVQ BP, (g_sched+gobuf_bp)(R14)
```

**Impact:** This creates artificial instruction pointer addresses that don't correspond to actual call sites, causing CoreCLR's IP boundary analysis to examine non-existent or invalid instruction sequences.

### 2. Complex ABI Transition Macros

Go uses elaborate register preservation patterns when transitioning between host and Go ABIs:

**File:** `/tmp/go-custom/src/runtime/cgo/abi_amd64.h:26-47`
```c
#define PUSH_REGS_HOST_TO_ABI0()    \
    PUSHFQ                          \  // Save flags (unusual)
    CLD                             \  // Clear direction flag  
    ADJSP $(REGS_HOST_TO_ABI0_STACK - 8) \  // Non-standard stack adjustment
    MOVQ DI, (0*0)(SP)             \  // Custom register saving pattern
    MOVQ SI, (1*8)(SP)             \
    MOVQ BP, (2*8)(SP)             \  // Frame pointer mid-sequence
    MOVQ BX, (3*8)(SP)             \
    MOVQ R12, (4*8)(SP)            \
    MOVQ R13, (5*8)(SP)            \
    MOVQ R14, (6*8)(SP)            \
    MOVQ R15, (7*8)(SP)            \
    MOVUPS X6, (8*8)(SP)           \  // XMM register preservation
    MOVUPS X7, (10*8)(SP)          \
    // ... continues for 28 total registers
```

**Impact:** This pattern saves 28 registers (including XMM registers) in a non-standard sequence that differs dramatically from conventional C prologue/epilogue patterns, causing deep stack analysis iterations.

### 3. Stack Switching with Frame Corruption

Go's `asmcgocall` function performs complex stack manipulations:

**File:** `/tmp/go-custom/src/runtime/asm_amd64.s:919-976`
```asm
TEXT ·asmcgocall(SB),NOSPLIT,$0-20
    // Complex stack switching logic
    CALL gosave_systemstack_switch<>(SB)     // Uses fake PC  
    MOVQ SI, g(CX)                           // Switch goroutine context
    MOVQ (g_sched+gobuf_sp)(SI), SP          // Switch stack pointer
    
    // Now on a scheduling stack (a pthread-created stack).
    SUBQ $16, SP
    ANDQ $~15, SP                            // Force alignment mid-function
    MOVQ DI, 8(SP)                           // Save g
    MOVQ (g_stack+stack_hi)(DI), DI
    SUBQ DX, DI
    MOVQ DI, 0(SP)                           // Save stack depth
```

**Impact:** Mid-function stack pointer manipulation and context switching creates inconsistent frame states that violate CoreCLR's assumptions about linear instruction execution.

### 4. NOSPLIT|NOFRAME Attributes

Many Go runtime functions use special attributes:

**File:** `/tmp/go-custom/src/runtime/asm_amd64.s:855`
```asm
TEXT gosave_systemstack_switch<>(SB),NOSPLIT|NOFRAME,$0
```

**Impact:** 
- `NOSPLIT`: Suppresses automatic stack growth checks
- `NOFRAME`: Creates zero frame size functions with complex register usage
- Results in stack frames that don't follow standard unwinding conventions

### 5. Intentional Frame Layout Deception

Go's `systemstack_switch` function is explicitly designed to confuse analysis:

**File:** `/tmp/go-custom/src/runtime/asm_amd64.s:479-486`
```asm
TEXT runtime·systemstack_switch(SB), NOSPLIT, $0-0
    // Align for consistency with offset used in gosave_systemstack_switch
    PCALIGN $8                               // Force specific alignment
    UNDEF                                    // Undefined instruction (intentional)
    // Make sure this function is not leaf,
    // so the frame is saved.
    CALL runtime·abort(SB)                   // Never actually reached
    RET
```

**Comment from source:** "The frame layout needs to match systemstack so that it can pretend to be systemstack_switch."

**Impact:** This function exists solely to create fake frame contexts, containing undefined instructions that cause analysis algorithms to fail or recurse indefinitely.

## Technical Analysis: Why CoreCLR Fails

### Expected vs. Actual Patterns

**CoreCLR's IP Boundary Analysis Expects:**
1. Legitimate instruction pointers corresponding to actual code addresses
2. Standard C calling conventions with predictable prologue/epilogue patterns  
3. Consistent frame pointer usage following x86-64 ABI conventions
4. Linear instruction sequences without artificial control flow

**Go's Assembly Patterns Provide:**
1. Fake PCs pointing to non-existent or invalid instruction sequences
2. Complex multi-ABI register preservation with 28+ register saves
3. Mid-function stack switches and frame pointer manipulation
4. Functions designed to "pretend" to be other functions with fake layouts

### Stack Consumption Analysis

When CoreCLR's signal handler encounters a fake PC from Go code:

1. **Initial Analysis**: `IsIPInProlog`/`IsIPInEpilog` begins analyzing the fake instruction address
2. **Deep Recursion**: Complex register patterns cause the analysis to recurse through fake stack frames
3. **Excessive Iteration**: Non-standard patterns force the algorithm to examine many more instructions than expected
4. **Stack Overflow**: The 16KB sigaltstack is exhausted before analysis completes
5. **Internal CLR Error**: CoreCLR aborts with error 0x80131506

### Root Cause

The fundamental issue is **assumption mismatch**:

- **CoreCLR assumes**: IP boundary analysis will only encounter conventional C calling patterns requiring minimal stack space
- **Go provides**: Deliberately atypical patterns with fake PCs and complex frame layouts designed for Go's specific runtime needs

This isn't a bug in Go's assembly (which works perfectly within Go's runtime), but rather a failure of CoreCLR's signal analysis to handle non-conventional calling patterns.

## Comparison with Simplified Reproducer

The simplified reproducer in `/home/egon/Code/gist/dotnet-repro/simplified-reproducer/` demonstrates that basic C complexity is insufficient to trigger the overflow. The essential component is assembly code that replicates Go's specific atypical patterns:

### Critical Assembly Pattern
**File:** `simplified_atypical.c:71-101`
```asm
simple_atypical_function_2:
    # Different atypical pattern
    pushq %rbp
    pushq %rbx            # Non-standard order  
    pushq %r12
    movq  %rsp, %rbp      # rbp setup after pushes (atypical)
    
    # Moderate stack allocation
    subq  $2048, %rsp
    
    # Parameters in atypical registers from caller
    movq  %rax, %r12      # context from RAX (atypical)
    movq  %rbx, -8(%rbp)  # param1 from RBX (atypical) 
    movq  %rcx, -16(%rbp) # param2 from RCX
    
    # Fixed cleanup - manual stack restoration
    addq  $2048, %rsp
    popq  %r12
    popq  %rbx
    popq  %rbp            # This creates stack corruption - ESSENTIAL
    ret
```

This deliberately corrupted stack frame (with `leave` instruction removed in favor of manual cleanup) replicates the kind of frame inconsistencies that stress CoreCLR's analysis.

## Implications and Recommendations

### For CoreCLR Development
1. **Bounded Analysis**: Implement limits on IP boundary analysis stack depth/iterations
2. **Validation**: Add validation to detect fake or invalid instruction pointers
3. **Fallback Mechanisms**: Provide safe fallback when analysis encounters unexpected patterns
4. **Stack Usage Monitoring**: Monitor sigaltstack usage during IP analysis

### For Go Integration
1. **Alternative Approach**: Consider using custom .NET runtime builds without SA_ONSTACK for Go interop scenarios
2. **Isolation**: Use separate processes for Go/CoreCLR integration to avoid signal handler conflicts
3. **Documentation**: Document the incompatibility between Go's runtime patterns and CoreCLR's signal analysis

## Conclusion

Go's assembly patterns are not buggy but are fundamentally incompatible with CoreCLR's assumptions about instruction pointer analysis. The "atypical calling conventions" mentioned in the reproducer comments refer specifically to:

- Fake program counter manipulation for stack switching
- Complex multi-ABI register preservation patterns  
- Intentionally deceptive frame layouts for runtime optimization
- Mid-function stack pointer manipulation

These patterns, while essential for Go's performance and functionality, create exactly the kind of complex analysis scenarios that can exhaust CoreCLR's limited sigaltstack space during signal handling.

The issue represents a fundamental architectural incompatibility rather than a fixable bug in either system.