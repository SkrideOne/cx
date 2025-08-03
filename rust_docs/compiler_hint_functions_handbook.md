# Compiler-Hint Functions in Rust Nightly 1.90 – Best-Practice 2025 Manual

Rust’s `std::hint` module and certain function attributes let developers provide hints to the compiler and LLVM back-end about expected code behavior. When used correctly, these hints can improve performance or generate better code, but they are only suggestions—the compiler may ignore them or even degrade performance. The Best-Practice 2025 philosophy favors clear, maintainable code and algorithmic improvements over micro-optimizations; hints should be used only after profiling and accompanied by comments explaining the reasoning.

---

## 1 Typical hints and how to use them

### 1.1 Branch-prediction hints: `likely`, `unlikely` and `cold_path`

| Hint                      | Use case                                             | Guidelines                                                                                                                                                                                           |
| ------------------------- | ---------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `hint::likely(b: bool)`   | Tell the compiler the branch is expected to be true. | Use only when a branch is strongly biased and profiling shows prediction matters (e.g., input validation in hot loops). Avoid outside branch conditions; let common case be first if skew is slight. |
| `hint::unlikely(b: bool)` | Opposite of `likely`: branch rarely true.            | Apply to rare error branches or assertions. Best in condition; overuse can hurt performance as predictors adapt quickly.                                                                             |
| `hint::cold_path()`       | Marks the current path as cold.                      | Call inside rare-branch body (e.g., match arm) to separate hot and cold code and improve cache layout. Avoid wrapping condition.                                                                     |

**Stability and feature gates:**  At the time of writing (Rust 1.90 nightly, August 2025) the `likely`, `unlikely` and `cold_path` functions are part of `std::hint` but they remain **nightly‑only experimental APIs** and require enabling the corresponding feature gates (`#![feature(likely_unlikely)]` and `#![feature(cold_path)]`).  The standard library documentation explicitly marks `likely`【504436059622987†L40-L43】 and `cold_path`【844368048706843†L40-L43】 as unstable and subject to change.  You must guard calls behind `#[cfg(feature = "nightly")]` or `#[cfg(feature = "enable_branch_hints")]` and provide fallbacks for stable builds (e.g., custom `likely`/`unlikely` wrappers that call `#[cold]` functions on stable).  Treat these hints as optional metadata, not correctness requirements.

**Best practice:** These hints inform **static** branch prediction only; modern CPUs predominantly use **dynamic** predictors.  The benefit of static hints is marginal and can sometimes hurt performance when branch behaviour changes at runtime.  Therefore:

* Measure before using: profile your hot code paths and confirm that branch misprediction is a bottleneck.
* Prefer algorithmic improvements or data‑layout changes to micro‑hints.
* Use `#[cold]` functions on stable Rust to isolate rare code paths instead of relying on nightly intrinsics.
* Document why a hint is used and remove it if subsequent profiling shows no benefit.

### 1.2 Avoiding branches: `select_unpredictable`

`hint::select_unpredictable(condition, true_val, false_val)` returns one of two values, hinting that the condition is hard to predict. On CPUs with conditional moves, the optimizer may generate a branch-free conditional move.

* **Pros:** Useful in loops with random direction (e.g., binary search).
* **Cons:** Not guaranteed on all platforms; may slow code if condition is predictable; unsuitable for constant-time cryptography.

**Best practice:** Benchmark both branch and branchless versions. Use only when profiling shows branch misprediction is the main bottleneck.

### 1.3 Benchmarking helper: `black_box`

`hint::black_box` is an identity function that tells the compiler its argument could be used in any way. It prevents optimizations that eliminate or reorder code when measuring performance.

```rust
pub fn benchmark() {
    let haystack = vec!["abc", "def", "ghi", "jkl", "mno"];
    let needle = "ghi";
    for _ in 0..10 {
        black_box(contains(
            black_box(&haystack),
            black_box(needle),
        ));
    }
}
```

**Guidelines:**

* Use in benchmarks to prevent folding loops or eliminating pure functions.
* Wrap inputs and outputs separately to avoid compile-time evaluation.
* Not for security-critical constant-time code; has no effect at compile-time evaluation.

### 1.4 Busy-wait hint: `spin_loop`

`hint::spin_loop()` emits an instruction signalling a spin-loop. The CPU may reduce power or switch hyper-threads.

* Use inside short spin loops; terminate after finite iterations and fall back to a blocking syscall to avoid priority inversion.
* On unsupported platforms, it does nothing.
* To yield to the OS, use `std::thread::yield_now()` instead.

### 1.5 Unsafe optimizer hints: `assert_unchecked` and `unreachable_unchecked`

| Hint                                     | Use case                                          | Guidelines                                                                                                                                               |
| ---------------------------------------- | ------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `unsafe fn assert_unchecked(cond: bool)` | Promise the optimizer that `cond` is always true. | Prove `cond` externally; benchmark before/after; treat as if `if !cond { unreachable_unchecked() }`; false condition ⇒ undefined behavior.               |
| `unsafe fn unreachable_unchecked() -> !` | Marks code path as unreachable.                   | Use only when logically impossible to reach; undefined behavior if reached; prefer `unreachable!` macro for runtime panic; benchmark to confirm benefit. |

### 1.6 Miscellaneous hint: `must_use`

`hint::must_use(value)` returns its argument but triggers a `unused_must_use` warning if dropped. Mainly for macros where attaching `#[must_use]` is inconvenient. Use sparingly.

---

## 2 Code-generation attributes

### 2.1 Inline attributes

Rust supports three hints: `#[inline]`, `#[inline(always)]`, `#[inline(never)]`. None are guarantees.

* **No annotation:** Compiler heuristics decide based on optimization level, function size and generics.
* `#[inline]` suggests inlining; `#[inline(always)]` forces it; `#[inline(never)]` discourages it.
* Inlining is non-transitive: to inline `f()` and its callee `g()`, mark both.
* Best for tiny functions or hot loops; avoid `always` on large functions to prevent code-size bloat.
* Measure performance before and after; heuristics and cross-crate effects can be unpredictable.

### 2.2 Cold attribute: `#[cold]`

`#[cold]` suggests a function is unlikely to be called, lowering its weight in IR. Use on error handlers, panic functions or other rare paths. Combine with `#[inline(never)]` for small functions.

### 2.3 Architecture-specific: `#[target_feature]`

`#[target_feature(enable = "feat")]` enables CPU features (e.g., AVX2, SSE2) for a function. Use runtime detection:

```rust
#[cfg(target_feature = "avx2")]
#[target_feature(enable = "avx2")]
pub unsafe fn fast_avx2(x: &[i16]) -> i16 { /* ... */ }

pub fn dot_product(x: &[i16]) -> i16 {
    if std::is_x86_feature_detected!("avx2") {
        unsafe { fast_avx2(x) }
    } else {
        fallback_dot_product(x)
    }
}
```

* Only call when feature is supported; calling otherwise is undefined behavior.
* `#[target_feature]` functions are not inlined into callers lacking the feature.

---

## 3 Atypical and advanced cases

### 3.1 Constant-time cryptography

Branch hints and `select_unpredictable` do **not** guarantee constant-time. Use specialized crates (`subtle`, `crypto_box`) or bitwise ops and `ct_eq`. Test with tools like **dudect**.

### 3.2 Hot/cold splitting and layout

For performance-critical code (e.g., decoders), split into hot (#\[inline(always)]) and cold (#\[inline(never)]) functions to optimise cache layout and inlining.

### 3.3 Intrinsics and assembly

Nightly exposes `std::intrinsics`. Use only after high-level optimizations; misuse is undefined behavior. Document and benchmark carefully.

### 3.4 Compiler assumptions: `llvm.assume`

Wrapped by `assert_unchecked`, this can remove runtime checks but may slow code or increase compile times. Use only for externally enforced invariants.

---

## 4 Best-practice checklist (2025 edition)

1. **Profile first.** Identify hot code and mispredicted branches.
2. **Prefer algorithmic improvements.** Biggest speedups come from better algorithms or data structures.
3. **Use hints sparingly.** Document rationale; remove obsolete hints as compilers evolve.
4. **Benchmark changes.** Perform A/B tests with realistic workloads.
5. **Avoid hints for correctness.** They are not guarantees; do not enforce constant-time or program correctness.
6. **Never misuse unsafe hints.** `assert_unchecked` and `unreachable_unchecked` with invalid conditions ⇒ undefined behavior.
7. **Consider portability.** Some hints noop on certain platforms; guard nightly features with feature flags and provide fallbacks.
8. **Prefer high-level constructs.** Use `?` and idiomatic error handling over manual branch hints.
9. **Focus on readability.** Comments explaining every hint are critical; maintainability over micro-optimizations.

---

## 5 Summary

Rust nightly 1.90 provides hint functions (`likely`, `unlikely`, `cold_path`, `select_unpredictable`, `black_box`, `spin_loop`, `assert_unchecked`, `unreachable_unchecked`, `must_use`) and attributes (`#[inline]`, `#[inline(always)]`, `#[inline(never)]`, `#[cold]`, `#[target_feature]`). These allow developers to guide branch prediction, inlining and instruction selection. However, they are not silver bullets: apply judiciously, back by profiling and algorithmic insight. Unsafe hints carry undefined-behavior risks and require proven invariants. Profiling, benchmarking and clear documentation are the cornerstones of effective performance engineering in Rust.
