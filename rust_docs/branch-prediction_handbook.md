# Branch Prediction Hints in Rust nightly 1.90 – Best Practice 2025

## Background – why branch prediction matters

Modern CPUs fetch and decode instructions before they know which branch of a conditional will be taken. To avoid stalling the pipeline, they use branch predictors to guess the direction of conditional branches and speculatively execute the predicted path. If the prediction is wrong, the pipeline must be flushed, wasting cycles.

Rust targets the same CPUs as C/C++. The compiler (rustc and LLVM) and hardware already use sophisticated heuristics. Therefore, branch prediction hints should be seen as last-resort micro-optimisations only after careful profiling. Used blindly, hints can slow down your code.

### Terminology

| Term            | Meaning                                                                                  |
| --------------- | ---------------------------------------------------------------------------------------- |
| **Hot path**    | Code executed frequently in normal operation.                                            |
| **Cold path**   | Rarely executed code, e.g., error handling.                                              |
| **Static hint** | A hint written by the programmer, telling the compiler that a branch is likely/unlikely. |
| **Branch-less** | Algorithms written without conditional branches, often using arithmetic/bitwise ops.     |

---

## 1. Rust support for branch prediction hints (nightly 1.90)

### 1.1 Nightly `core::intrinsics::likely`/`unlikely`

Rust’s compiler exposes two intrinsics:

* `std::intrinsics::likely(b: bool) -> bool` – hints that `b` is usually true.
* `std::intrinsics::unlikely(b: bool) -> bool` – hints that `b` is usually false.

Both return the input but annotate the branch in the compiler’s IR. They are safe to call (no `unsafe` block needed) and should only be used in `if` conditions.

Status in 2025: these intrinsics remain **nightly‑only experimental APIs** behind the `likely_unlikely` feature gate【504436059622987†L40-L43】.  They currently rely on LLVM’s `llvm.expect` metadata, and if optimisations strip this metadata the hints are ignored.  A stable fix is planned but unavailable as of August 2025.  Use only after benchmarking and confirming codegen improvements, and guard calls behind appropriate `cfg(feature = "nightly")` or custom wrappers so stable builds compile without them.

### 1.2 The `branches` crate

The third-party `branches` crate wraps the nightly intrinsics:

* `branches::likely(b: bool) -> bool` – marks branch as likely.
* `branches::unlikely(b: bool) -> bool` – marks branch as unlikely.
* `branches::assume(b: bool)` – asserts `b` is always true (UB if false).
* `prefetch_read_data` / `prefetch_write_data` for cache prefetch.

On stable Rust, it falls back to no-ops. Benchmarks show up to 10–20 % speed-up in tight loops with highly skewed branch probabilities. Always verify with benchmarks.

### 1.3 The `#[cold]` attribute

The stable `#[cold]` attribute marks functions as unlikely to be called, moving them out of hot paths and reducing inlining. It effectively marks branches cold without unstable intrinsics. Use it for error handling or seldom code paths.

Example replacement for `likely`/`unlikely` on stable Rust:

```rust
#[inline]
#[cold]
fn cold() {}

#[inline]
fn likely(b: bool) -> bool {
    if !b { cold() }
    b
}

#[inline]
fn unlikely(b: bool) -> bool {
    if b { cold() }
    b
}
```

### 1.4 Upcoming `#[likely]`/`#[unlikely]` attributes

A language proposal aims to introduce `#[likely]`/`#[unlikely]` for match arms and statements, similar to C++20. As of August 2025 these are unavailable; continue using intrinsics or `#[cold]`.

---

## 2 General best-practice guidelines (2025)

* **Optimise correctness and clarity first.** Branch hints are micro-optimisations; apply only after profiling.
* **Profile before and after.** Use `criterion`, hardware counters (`perf`, `cargo-llvm-lines`) to measure misprediction rates.
* **Prefer `#[cold]` to intrinsics.** Stable and supported by LLVM; moves cold code out of hot paths.
* **Move cold code out of loops.** Extract error handling or uncommon checks into `#[cold]` functions.
* **Avoid unpredictable branches.** Use branch-less operations or iterator methods for hot loops.
* **Use `assume` only with proven invariants.** Document invariants and use debug assertions in development.
* **Combine with PGO.** Let PGO collect branch probabilities and guide block ordering.
* **Be aware of micro-architectural differences.** Some CPUs ignore static hints; on ARM/Power misuse of hints can degrade performance.
* **When in doubt, let the compiler decide.** Modern compilers and CPUs excel at branch prediction.

---

## 3 Typical use-cases and examples

### 3.1 Error handling and panics

```rust
pub fn compute(x: i32) -> i32 {
    match might_fail(x) {
        Ok(v) => v * 2,         // hot path
        Err(e) => handle_error(e),  // cold path
    }
}

#[cold]
fn handle_error(err: Error) -> ! {
    panic!("unexpected error: {err}");
}
```

### 3.2 Result-like early exits in loops

Unroll loops or batch process; handle remainders with a `#[cold]` function.

### 3.3 Matching enums with biased variants

```rust
match state {
    State::Running(data) => process(data),      // hot
    State::Finished => return,                  // rare
    State::Error(ref e) => return handle_error(e), // cold
}
```

### 3.4 Asserting invariants

Use `unsafe { *slice.get_unchecked(i) }` or `assume(i < slice.len())` in release; add `debug_assert!` in debug.

### 3.5 Data prefetching

Use `branches::prefetch_read_data(addr, locality)` to hide memory latency; profile to confirm benefit.

---

## 4 Atypical & advanced scenarios

### 4.1 Branch-less alternatives

Replace branches with arithmetic/bitwise ops; may be less readable and occasionally slower. Measure performance.

### 4.2 Real-time/low-latency paths

For critical rare paths, inline and mark the common path cold.

### 4.3 Inline assembly hints

On ARM/Power, use `asm!` to insert architecture-specific hint instructions; test on actual hardware.

### 4.4 Unsound hints and UB

Misusing `assume` intrinsics leads to UB; document and guard with assertions.

### 4.5 Profiling tools

Use `perf`, Intel® VTune™, likwid, and `cargo-llvm-lines` combined with `criterion` for micro-optimisation insights.

---

## Conclusion

Branch prediction hints in Rust remain unstable and experimental. Intrinsics are safe but currently unreliable; prefer clear, branch-friendly code and let the compiler/CPU handle prediction. When explicit hints are necessary:

* Move rare branches into `#[cold]` functions.
* Use `#[cold]` or `branches::likely`/`unlikely` only after profiling.
* Avoid unpredictable branches in tight loops.
* Combine hints with PGO when possible.

Following these guidelines ensures you leverage branch prediction hints effectively while maintaining code quality and portability.
