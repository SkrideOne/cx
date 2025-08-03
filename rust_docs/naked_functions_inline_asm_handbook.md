# Best Practices for Naked Functions and Inline Assembly in Rust Nightly 1.91 (2025)

## Introduction

Rust 1.88 stabilised naked functions and the `naked_asm!` macro, building on the already‑stable `asm!` macro for inline assembly.  Rust nightly 1.91 continues to support these features and adds important usability improvements: many `std::arch` intrinsics that do not take pointer arguments can now be called from **safe** Rust if the correct CPU features are enabled at compile time【684066008670463†L134-L140】.  This reduces the need for hand‑written assembly in some low‑level tasks.  Both naked functions and inline assembly provide direct control over generated machine code, but they bypass many of Rust’s safety guarantees and complicate compiler optimisations.  The guidelines below distil Best Practice 2025 recommendations to ensure code quality, portability and performance when working on nightly 1.91.

## Background

### Inline assembly

Inline assembly in Rust is available via the `asm!` macro. It allows embedding hand-written assembly instructions into Rust code and is supported on x86/x86-64, ARM/AArch64, RISC-V, LoongArch and s390x targets. `asm!` must always appear inside an `unsafe` block, because arbitrary instructions can violate Rust’s safety invariants.

The general syntax is:

```rust
unsafe {
    asm!("instruction templates", operands..., options(...));
}
```

Operands describe how Rust values are mapped to registers or memory. Main operand types include `in`, `out`, `inout`, `lateout` and `inlateout`. The `const` and `sym` operands insert compile-time constants and symbol addresses. You may specify explicit registers (e.g., "rax") or register classes (`reg`, `reg_abcd`, etc.).

Options inform the compiler about side effects and constraints. Key options:

* `pure`: no side effects, depends only on inputs
* `nomem` / `readonly`: does not read/write memory
* `preserves_flags`: does not modify CPU flags
* `noreturn`, `nostack`, `att_syntax`, `raw`
* `clobber_abi(ABI)`: marks all caller-saved registers of the given ABI as clobbered

### Naked functions and `naked_asm!`

A naked function is declared with `#[unsafe(naked)]` and contains exactly one call to `naked_asm!`. The compiler emits no prologue/epilogue; the assembly block must implement the calling convention, preserve callee-saved registers and return correctly. Typical uses include operating systems, bootloaders, interrupt handlers and embedded firmware.

RFC 2972 (“constrained naked functions”) imposes these constraints:

* Must specify a non-`extern "Rust"` calling convention; arguments and return type must be FFI-safe.
* Body contains a single `asm!` or `naked_asm!` with only `const` or `sym` operands and the `noreturn` option.
* Cannot be `#[inline]` (implicitly `#[inline(never)]`).
* Compiler guarantees registers match calling convention on entry, but provides no stack frame.
* Must preserve callee-saved registers and must not fall off the end of the assembly block.
* Always `unsafe`; incorrect ABI implementation is unsound.

Rust 1.88 replaced `#[naked]` with `#[unsafe(naked)]` and introduced `naked_asm!` to enforce these constraints and improve diagnostics.

## Typical Use Cases and Best Practices (2025)

### 1. Simple arithmetic or bit‑twiddling instructions

Use inline assembly only when a suitable intrinsic is unavailable.  Many operations (e.g., bit rotations, population count) are exposed in `std::arch` or `core::arch` and compile to efficient instructions.  With nightly 1.91, most intrinsics that do not take pointer arguments can be called from safe Rust when the appropriate target features are enabled【684066008670463†L134-L140】, so prefer these safe intrinsics over inline assembly wherever possible.

* **Use `inout` / `lateout`** to minimize register pressure.
* **Annotate side effects**: use `options(pure, nomem, nostack)` when appropriate.

```rust
#[cfg(target_arch = "x86_64")]
fn add_three(x: u64) -> u64 {
    let mut x = x;
    unsafe {
        asm!("add {0}, 3", inout(reg) x, options(pure, nomem, nostack));
    }
    x
}
```

* **Avoid over‑use**: unnecessary inline assembly can inhibit optimizations and reduce portability.

### 2. Accessing special registers or CPU features

Reading `rdtsc`, `cpuid` or model-specific registers often requires assembly.

* **Wrap in a safe abstraction** and document safety requirements.
* **Use `preserves_flags`** when appropriate.
* **Use `clobber_abi("C")`** to mark caller-saved registers when calling via assembly.

```rust
#[cfg(target_arch = "x86_64")]
#[inline]
pub fn rdtsc() -> u64 {
    let low: u32;
    let high: u32;
    unsafe {
        asm!("rdtsc", out("eax") low, out("edx") high, options(nomem, nostack));
    }
    ((high as u64) << 32) | (low as u64)
}
```

### 3. I/O port access and low‑level hardware control

Hardware drivers often need `in`/`out` instructions or control-register manipulation.

* **Ensure correct privileges**: I/O port instructions require privileged CPU mode.
* **Mark memory effects**: omit `nomem` when accessing device memory.
* **Treat side-effects as volatile** (default for assembly).

### 4. Calling other functions from assembly (FFI wrappers)

Inline assembly can implement custom calling conventions or system calls.

* **Prefer Rust FFI** (`extern "C"`) when possible.
* **Use `clobber_abi("C")`** when calling function pointers.
* **Follow the ABI strictly** and document register and stack usage.

### 5. Interrupt handlers and context switching (naked functions)

Naked functions avoid prologue/epilogue for minimal-overhead ISR and context-switch routines.

* **Use the correct extern ABI** (e.g., `extern "sysv64"`).
* **Preserve callee-saved registers**.
* **Return correctly**: include `ret`, `iret` or appropriate instruction; use `noreturn` option.
* **Avoid stack usage**; access arguments via registers.
* **Only `const` or `sym` operands** in `naked_asm!`.
* **Document safety** and preconditions.

```rust
#[cfg(target_arch = "x86_64")]
#[unsafe(naked)]
pub unsafe extern "sysv64" fn interrupt_handler() {
    core::arch::naked_asm!(
        // Save callee-saved registers
        "push rbp", "push rbx", "push r12", "push r13", "push r14", "push r15",
        // Call Rust handler
        "call {handler}",
        // Restore registers
        "pop r15", "pop r14", "pop r13", "pop r12", "pop rbx", "pop rbp",
        "iretq",
        handler = sym rust_interrupt_handler,
    );
}

extern "C" fn rust_interrupt_handler() {
    // Safe Rust code
}
```

### 6. Cryptographic or vectorized algorithms

Cryptographic primitives may use AES-NI, SHA extensions or SIMD.

* **Use portable intrinsics** from `std::arch` first.
* **Isolate assembly paths** behind feature flags with `#[cfg(target_feature = "…")]`.
* **Annotate memory effects**: use `options(pure, readonly)` or omit `nomem` if reading/writing memory.
* **Consider `nostack`** when no stack access is needed.

### 7. Platform‑specific inline assembly (RISC‑V, ARM, etc.)

* **Conditionally compile** with `#[cfg(target_arch)]`.
* **Respect alignment and memory model**, using barriers (`dmb`, `isb`) when needed.
* **Test on hardware or QEMU**; Rust treats `asm!` as a black box.

## Methodology for Atypical/Non‑Typical Cases

Rare or risky uses (self-modifying code, hook patches, manual stack-pointer manipulation) should be avoided.

1. **Evaluate necessity**: prefer safe Rust, intrinsics or external `.s`/`.asm` files.
2. **Prohibit self‑modifying code**; if unavoidable, use `global_asm!` in separate files.
3. **Avoid stack-pointer tweaks** except in bootloaders/kernels.
4. **Keep naked functions simple**; avoid loops or Rust calls inside `naked_asm!`.
5. **Use `raw`/`att_syntax`** only when necessary.
6. **Audit safety** with peer review and static analysis.
7. **Document custom ABIs** fully, including register and stack layouts.

## Summary of Key Best-Practice Guidelines

| Context                     | Best-Practice Highlights                                                                                       |
| --------------------------- | -------------------------------------------------------------------------------------------------------------- |
| Use cases                   | Inline assembly only when necessary; prefer intrinsics or external assembly files.                             |
| Safety and documentation    | Wrap `asm!` in `unsafe` blocks; document safety. Mark naked functions `unsafe` and document the ABI.           |
| Operands and registers      | Use correct operand types (`in`, `out`, `inout`, etc.); use `const`/`sym` for constants and symbols.           |
| Options                     | Specify side-effect options (`pure`, `nomem`, `readonly`, `preserves_flags`, `noreturn`, `nostack`, etc.).     |
| Clobbered registers         | Use `clobber_abi("C")` or explicit clobber lists to mark modified registers.                                   |
| Naked functions             | Declare `#[unsafe(naked)] extern "ABI" fn`; body is one `naked_asm!` with only `const`/`sym` operands.         |
| Cross-platform code         | Use `#[cfg(target_arch)]` and `#[cfg(target_feature)]` with safe fallbacks.                                    |
| Testing and debugging       | Test on real hardware or accurate simulators; inspect with `objdump` or `cargo asm`.                           |
| Quality and maintainability | Restrict assembly to short, documented fragments; avoid complex flow in `asm!`. Use descriptive operand names. |

## Conclusion

Inline assembly and naked functions empower low‑level Rust programming while demanding rigorous attention to safety, ABI compliance and maintainability.  Nightly 1.91 further reduces the need for inline assembly by allowing many CPU‑specific intrinsics to be called from safe code【684066008670463†L134-L140】.  By following Best Practice 2025 guidelines—sparing use of assembly, strict option annotations, register preservation, and thorough documentation—developers can achieve high performance without compromising code quality or safety.
