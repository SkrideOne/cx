# SIMD best practices in Rust nightly 1.90 (Best Practice 2025)

## Overview

Single Instruction Multiple Data (SIMD) enables parallel execution of the same operation on multiple data elements. Modern CPUs provide SSE/AVX on x86/x86‑64 and NEON on ARM, but support varies across CPU generations. Rust’s `std::simd` module (requires the nightly compiler and `#![feature(portable_simd)]`) offers a **portable abstraction** that compiles to the best available instructions and falls back to scalar code when the target CPU lacks a SIMD unit (see *doc.rust-lang.org*). Each SIMD vector type has lanes (parallel elements) and provides element‑wise arithmetic and bitwise operations, comparisons and reduction functions; a separate **Mask** type encodes per‑lane booleans.

SIMD programming is powerful but error‑prone. Performance depends on data alignment, lane count, CPU features and algorithm choice. The following methodological guide for Rust 1.90 nightly compiles best‑practice ideas from 2024–2025 literature (GitHub, *linebender.org*). It covers **typical SIMD use‑cases, atypical patterns, performance and quality considerations, and CPU‑feature management**. The guidelines use `std::simd` when available and mention safe abstractions such as **pulp** for runtime dispatch.

---

## Basic terminology and constructs

| Concept          | Notes                                                                                                                                                                                                                                                                                 |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **SIMD vector**  | `Simd<T, N>` is a fixed‑length array of `T` (lanes) with element‑wise operators (`+`, `-`, `*`, `/`, `%`, bitwise ops) and comparisons. `Mask<T, N>` is a per‑lane boolean array for branchless operations.                                                                           |
| **Lane count**   | `N` must be a supported lane count (1–64 for many types). The `LANES` constant or `simd.lanes()` returns the number of lanes.                                                                                                                                                         |
| **Construction** | `Simd::from_array([…])` copies an array into a SIMD vector; `Simd::splat(x)` replicates `x` across lanes; `slice::as_simd` splits a slice into **prefix** (unaligned), **middle** (aligned SIMD chunks) and **suffix**; `Simd::as_array()` converts back to an array without copying. |
| **Reductions**   | Methods like `reduce_sum`, `reduce_max`, `reduce_min`, etc., perform horizontal reductions across lanes.                                                                                                                                                                              |
| **Masks**        | Comparisons (`simd_lt`, `simd_le`, `simd_gt`, etc.) produce **Mask** values; `Mask::select` chooses elements from two vectors based on a mask. `Mask::any`/`all` test if any or all lanes satisfy a condition.                                                                        |

---

## Enabling SIMD

* **Use nightly `std::simd` or safe wrappers** – `std::simd` is portable and automatically falls back to scalar operations; it requires `#![feature(portable_simd)]`. On stable Rust, consider the **wide** crate or platform‑specific intrinsics in `std::arch`. The **pulp** crate provides a safe abstraction that dispatches to vectorised implementations at runtime.
* **Compile with optimisations** – auto‑vectorisation and inlining are only enabled in *release* mode (e.g. `cargo build --release`).
* **Select CPU features carefully** – CPUs differ in supported instruction sets. `cargo simd-detect` (from the *cargo‑simd‑detect* tool) lists available and enabled SIMD extensions. To enable features, set `RUSTFLAGS="-C target-feature=+avx2"` or use `-C target-cpu=native` for personal builds; **do not distribute binaries compiled with unsupported features**.
* **Runtime detection & multiversioning** – When distributing software, compile multiple versions and dispatch at runtime. Use `is_x86_feature_detected!` macros or safe crates such as **multiversion** and **rust‑target‑feature‑dispatch**. The **pulp** crate uses zero‑sized types to encode SIMD capabilities and provides `Arch::new().dispatch` to select the best version.

---

## Typical SIMD patterns and best practice

### 1. Element‑wise arithmetic (vector addition, subtraction, multiplication)

* **Construct SIMD vectors** via `Simd::from_array` or `Simd::splat`. For long slices, use `slice::as_simd` (or `as_simd_mut`) to split into `(prefix, aligned_chunks, suffix)`.
* Perform element‑wise ops using operators (`+`, `-`, `*`, `/`). These map to the best SIMD instructions and fall back to scalar when needed.
* Write **simple loops** that operate on contiguous data without complex branching; autovectorisation works best when the loop body is straightforward.
* Prefer **wide lane counts**; benchmarking often shows that 32‑ or 64‑lane vectors offer the best throughput. Use generics (`const LANES: usize`) to experiment.
* **Benchmark** with *criterion* to choose lane width and confirm benefits.

```rust
#![feature(portable_simd)]
use core::simd::Simd;

fn scale(data: &mut [f32], factor: f32) {
    let (prefix, simd_chunks, suffix) = data.as_simd_mut::<f32, 32>();
    let factor_vec = Simd::<f32, 32>::splat(factor);
    for v in simd_chunks {
        *v *= factor_vec;
    }
    for x in prefix.iter_mut().chain(suffix.iter_mut()) {
        *x *= factor;
    }
}
```

### 2. Dot products and reductions

* **Multiply and accumulate**: multiply two `Simd` vectors and accumulate into a third `Simd` or a scalar.
* **Reduce in chunks**: after processing, accumulate with `reduce_sum`.
* **Unroll loops** if necessary and check alignment.

```rust
#![feature(portable_simd)]
use core::simd::{Simd, SimdFloat};

fn dot(a: &[f32], b: &[f32]) -> f32 {
    assert_eq!(a.len(), b.len());
    let (pa, ca, sa) = a.as_simd::<f32, 16>();
    let (pb, cb, sb) = b.as_simd::<f32, 16>();
    let mut sum = Simd::<f32, 16>::splat(0.0);
    for (va, vb) in ca.iter().zip(cb.iter()) {
        sum += *va * *vb;
    }
    let mut scalar = sum.reduce_sum();
    for (x, y) in pa.iter().chain(sa.iter()).zip(pb.iter().chain(sb.iter())) {
        scalar += *x * *y;
    }
    scalar
}
```

### 3. Conditional selection without branches

Use masks to control divergent behaviour per element (e.g., simplified Mandelbrot iterations):

```rust
#![feature(portable_simd)]
use core::simd::{Simd, Mask};
const LANES: usize = 8;

fn mandelbrot_iter(real: Simd<f64, LANES>, imag: Simd<f64, LANES>, threshold: f64, limit: u32) -> Simd<u32, LANES> {
    let mut zr = real;
    let mut zi = imag;
    let thr = Simd::splat(threshold);
    let mut count = Simd::<u32, LANES>::splat(0);
    let mut mask = Mask::<i64, LANES>::splat(true);
    for _ in 0..limit {
        let rr = zr * zr;
        let ii = zi * zi;
        let diverged = (rr + ii).simd_gt(thr);
        mask &= !diverged;
        if !mask.any() { break; }
        count = mask.select(count + Simd::splat(1), count);
        let ri = zr * zi;
        zr = mask.select(real + (rr - ii), zr);
        zi = mask.select(imag + (ri + ri), zi);
    }
    count
}
```

### 4. Gather/scatter and non‑contiguous data

* Use `Simd::gather_or` / `Simd::scatter` when necessary, but **prefer contiguous layouts** to avoid performance penalties.
* Always benchmark; gather instructions are slower on many CPUs.

### 5. Horizontal reductions (min/max, sum, any/all)

* Use built‑in `reduce_*` methods or `Mask::{any, all}`.
* Be aware of floating‑point subtleties (subnormals, NaNs).

### 6. Swizzling and permutation

* Use `simd_swizzle!` for compile‑time lane rearrangements, `rotate_elements_left/right` for cyclic shifts.
* Use `deinterleave` / `interleave` for RGBA‑style data.

### 7. Arithmetic with overflow or saturation

* Use `SimdUint::saturating_add` / `saturating_sub` for integer saturation.
* Clamp floats with `Simd::clamp` or `Mask::select`.

### 8. Transcendental functions (sqrt, exp, log, sin, cos)

* `SimdFloat` supplies `simd_sqrt`, `simd_abs`, etc. For functions not yet available (e.g. `exp`), use per‑lane scalars or external crates.

### 9. Matrix operations and linear algebra

* Prefer existing libraries like **faer** (leverages pulp) for SIMD‑optimised BLAS routines.
* Align & tile matrices; choose loop order for cache‐friendly access.

### 10. Checksum/hash computations

* Process multiple bytes/words at once with `Simd`; combine partial sums/XORs with `reduce_*`.
* Keep buffers aligned; unaligned loads hurt performance.

---

## Atypical SIMD scenarios and patterns

### A1. Non‑power‑of‑two data lengths & misaligned slices

* Use `slice::as_simd` / `as_simd_mut` to split data safely; process prefixes/suffixes scalarly.

### A2. Dynamic vector widths & runtime dispatch

* Write generic functions `fn f<const LANES: usize>(…)` and dispatch using feature detection or **pulp**.

### A3. Prefix sums & cumulative operations

* Implement log‑time scans via swizzles or wait for future `simd_scan_exclusive` support.

### A4. Searching for a value / first‑match detection

* Compare to produce a Mask → bitmask → `trailing_zeros` to get index.

### A5. Irregular control flow (per‑lane exit)

* Track active lanes with a mask; use `mask.any()` to terminate.

### A6. Transmuting between scalar structs & SIMD

* Use `bytemuck::cast_slice` or safe conversions; ensure alignment.

### A7. Unsupported element types (e.g., `i128`/`u128`)

* Split into high/low `u64` halves or use big‑int libraries.

### A8. Target‑specific intrinsics vs. portable SIMD

* For maximum speed, use `std::arch` intrinsics guarded by `#[cfg(target_feature)]`; consider **safe\_arch** or **pulp** for safer wrappers.

---

## Performance and quality considerations (Best Practice 2025)

* **Maintainability** – use type aliases, descriptive lane counts, conceal `unsafe` in small wrappers, document assumptions.
* **Benchmark extensively** – with *criterion*; validate lane sizes, algorithms, CPU features.
* **Correctness** – use property‑based tests; watch FP edge cases.
* **Prefer auto‑vectorisation first**; move to explicit SIMD only after measuring gains.
* **Manage code size** – limit monomorphisations; gate via feature flags.
* **Distribute baseline binaries** – e.g., SSE2 baseline + optional AVX2 features.
* **Prefer safe abstractions** – leverage **pulp**, **faer**, etc.

---

## Conclusion

SIMD programming in Rust nightly 1.90 offers significant performance potential when following structured best practices: choose appropriate lane counts, align data, use mask‑driven patterns for divergent control flow, and **benchmark thoroughly**. Typical operations (vector arithmetic, dot products, reductions, conditional selection) map cleanly to `std::simd` and safe abstractions, while non‑typical scenarios (prefix sums, first‑match searches, misaligned data, runtime lane selection) require careful handling with masks, swizzles and scalar fallbacks. By adhering to these guidelines—and leveraging tools such as **pulp** for runtime dispatch—developers can achieve high performance while retaining portability and code quality in Rust 2025.

