# Best-Practice Methodology for Vectorisation in Rust (nightly 1.90, 2025)

## 1 Principles of Auto‑vectorisation

Modern compilers convert sequential loops into SIMD instructions when the code meets certain criteria. Trusting the compiler is usually a good first step: write clear, idiomatic Rust and verify the assembly rather than immediately reaching for intrinsics. The following guidelines improve the compiler’s ability to vectorise your code:

### 1.1 Use slices (&\[T]) and avoid repeated bounds checks

Work on slices rather than `Vec<T>` and avoid manual indexing when possible. Auto‑vectorisation only applies to loops whose iteration bounds are easily understood by the compiler. Reslicing a slice to an in‑bounds range allows the compiler to hoist bounds checks out of the loop (users.rust-lang.org).

When working with chunks of a slice, call `split_at` or `split_at_unchecked` to create an unbounded sub‑slice and then iterate over it. This eliminates repeated `len` checks and enables vectorisation (users.rust-lang.org).

### 1.2 Keep loops simple and avoid loop‑carried dependencies

Write straight‑line loops without branching inside the core iteration. The compiler struggles to vectorise when there are data‑dependent branches, early returns or irregular control flow (luiscardoso.dev).

Avoid loop‑carried dependencies. Each iteration should operate independently; otherwise the compiler cannot reorder operations for SIMD. For example, `running_sum += f(x[i])` is not vectorisable because each iteration depends on the previous sum—perform reductions after the vectorised loop (§1.4). Similarly, pointer‑chasing loops are not vectorisable.

### 1.3 Use iterators and zipped slices

Replace manual index‑based loops with iterators such as `iter()`, `iter_mut()`, `zip()`, `map()`, and `for_each()`. The compiler can recognise these as internal iteration and generate vectorised code. For example:

```rust
for (x, y) in xs.iter().zip(ys) {
    *x += *y;
}
```

compiles to a vectorised inner loop (users.rust-lang.org).

When processing multiple arrays, use `zip()` on slices rather than indexing separately. This reduces the number of bounds checks and encourages the compiler to vectorise across all arrays (users.rust-lang.org).

### 1.4 Separate horizontal reductions from vectorised computation

Floating‑point reductions (e.g., summing an array of `f32`) are not auto‑vectorised because floating‑point addition is not associative; the compiler cannot change the order of operations (users.rust-lang.org). To vectorise such code, perform the element‑wise operations using SIMD and then perform the horizontal reduction separately (e.g., using `reduce_sum` on a `Simd` register). Alternatively, use explicit SIMD (see §2). For integer reductions the compiler will often vectorise automatically.

### 1.5 Enable compiler optimisations and inlining

Compile with:

```bash
cargo build --release
```

to enable optimisations (`-C opt-level=3`). Auto‑vectorisation is disabled at `-O0`.

Consider specifying a target CPU or target features. `-C target-cpu=native` enables all features supported by the host CPU (use only when not cross‑compiling). Alternatively, enable specific features such as `SSE4.2` or `AVX2` via `-C target-feature=+avx2,+fma` (rust-lang.github.io).

Small helper functions should be marked `#[inline(always)]` so that the body is exposed to the vectoriser. Auto‑vectorisation rarely spans function boundaries (nrempel.com).

### 1.6 Check your assembly

Use `cargo asm`, [Godbolt](https://godbolt.org) or `llvm-objdump` to verify that your loop has been vectorised. Inspect the compiled assembly for vector instructions (`vpadd*`, `vpmul*`, etc.) (tweedegolf.nl).

---

## 2 Portable SIMD with `core::simd`

Rust’s portable SIMD API in `core::simd` (available on nightly) provides explicit vector types and operations that work across architectures. It offers type‑safe operations without needing unsafe intrinsics.

### 2.1 Creating and using `Simd`

Choose an appropriate lane count (e.g., `Simd<u32, 8>` for eight 32‑bit integers). Use `Simd::splat(x)` to replicate a scalar across lanes and perform element‑wise operations as usual.

Avoid manual alignment by using `slice::as_simd` or `as_simd_mut`. This method splits a slice into a prefix, middle (aligned SIMD slice) and suffix. The middle portion is properly aligned for your platform and safe to operate on (doc.rust-lang.org). The prefix and suffix may require scalar processing. Example:

```rust
use core::simd::Simd;

fn sum_simd(xs: &[i32]) -> i32 {
    let (prefix, middle, suffix) = xs.as_simd::<8>(); // 8‑lane SIMD
    let mut acc = Simd::splat(0);
    for chunk in middle { acc += *chunk; }
    let mut sum = acc.reduce_sum();
    // handle head and tail
    for &x in prefix.iter().chain(suffix) { sum += x; }
    sum
}
```

Keep in mind that alignment requirements of `Simd` are typically greater than the underlying scalar type (users.rust-lang.org). When manually allocating buffers, use `#[repr(align(N))]` or `std::alloc::alloc` to ensure proper alignment.

### 2.2 Unaligned loads and stores

`Simd::from_slice_aligned` and `Simd::write_to_slice_aligned` check both the length and alignment of the slice and panic if these are not correct (rust-lang.github.io). Use them when you can guarantee alignment.

When alignment is unknown, use `Simd::from_slice_unaligned` or `Simd::write_to_slice_unaligned`. These functions perform unaligned loads/stores and may be slower on some architectures. Avoid unchecked versions unless you have verified alignment in debug builds (rust-lang.github.io).

### 2.3 Lane manipulation and horizontal operations

Use methods such as `reduce_sum`, `reduce_max`, `reduce_min`, `swizzle`, `gather_or_default` and `scatter` to perform lane‑wise reductions or gather/scatter operations. These operations are portable and will compile down to appropriate machine instructions.

Note that some horizontal operations may compile to multiple instructions and may not be as efficient as architecture‑specific intrinsics. Benchmark when performance is critical.

---

## 3 Architecture‑specific intrinsics (`std::arch`)

For maximum performance or when the compiler cannot vectorise code automatically, use architecture‑specific intrinsics. The `std::arch` module exposes low‑level instructions with type‑safe wrappers.

### 3.1 Safety and enabling CPU features

Intrinsics are unsafe: you must ensure that the required CPU feature is available on the executing machine (doc.rust-lang.org). For example, calling `_mm256_add_epi32` requires AVX.

Enable features either statically or dynamically:

**Static:** compile with `-C target-feature` or set `RUSTFLAGS="-C target-feature=+avx2,+fma"`. Alternatively, annotate functions with `#[target_feature(enable = "avx2")]` (doc.rust-lang.org). When features are enabled statically, certain intrinsics become safe in Rust 1.87+ (doc.rust-lang.org).

**Dynamic:** detect features at runtime using `is_x86_feature_detected!` and call intrinsics only when supported. Encapsulate the unsafe code in a cfg/if block to handle unsupported CPUs (doc.rust-lang.org). Example:

```rust
if std::is_x86_feature_detected!("avx2") {
    unsafe { avx2_function(); }
} else {
    scalar_fallback();
}
```

Avoid `-C target-cpu=native` in cross‑compilation or when distributing binaries to unknown hardware; use specific features instead (rust-lang.github.io).

### 3.2 Example: AVX2 vector addition

```rust
use std::arch::x86_64::{__m256i, _mm256_loadu_si256, _mm256_storeu_si256,
                        _mm256_add_epi32};

#[target_feature(enable = "avx2")]
unsafe fn avx2_add(a: &mut [i32], b: &[i32]) {
    assert_eq!(a.len(), b.len());
    let chunks = a.len() / 8;
    for i in 0..chunks {
        let pa = a.as_mut_ptr().add(i * 8) as *const __m256i;
        let pb = b.as_ptr().add(i * 8) as *const __m256i;
        let va = _mm256_loadu_si256(pa);
        let vb = _mm256_loadu_si256(pb);
        let vc = _mm256_add_epi32(va, vb);
        _mm256_storeu_si256(a.as_mut_ptr().add(i * 8) as *mut __m256i, vc);
    }
    // handle remainder scalarly
    for i in (chunks * 8)..a.len() { a[i] += b[i]; }
}
```

This function adds two slices using AVX2. It is marked `#[target_feature]` and may be called safely only when AVX2 is enabled. Note how the remainder (tail) is handled scalarly.

### 3.3 Interaction with inlining and performance

Functions annotated with `#[target_feature]` cannot be inlined into non‑target‑feature functions. Consider marking them `#[inline(always)]` or splitting them into separate modules that are only called when the feature is available (doc.rust-lang.org).
Benchmark both the explicit intrinsic version and auto‑vectorised version. Intrinsics provide fine‑grained control but may not improve performance if the compiler already emits good SIMD instructions (nrempel.com).

---

## 4 Memory layout and alignment

### 4.1 Alignment requirements

SIMD vectors often require alignments larger than their scalar element. For example, a `Simd<u32, 8>` may require 32‑byte alignment. When casting a `u8` buffer to a `u32` buffer (e.g., reading network packets), misalignment can occur; use `align_to::<u32>()` to properly split the buffer and handle the unaligned prefix/suffix (users.rust-lang.org).

If you define your own structs that will be vectorised, use `#[repr(align(N))]` to increase the alignment to a multiple of the vector width (users.rust-lang.org).

### 4.2 Chunking and tail handling

Use `chunks_exact(N)` or `chunks_exact_mut(N)` to process the slice in fixed‑size blocks of N elements, where N matches your vector lane count. Handle the remainder via `remainder()` or an explicit scalar loop. Tweede Golf’s zlib port demonstrates using `chunks_exact_mut(32)` to process 32 16‑bit integers at a time and emit vectorised instructions (tweedegolf.nl).

Alternatively, when using `std::simd`, use `slice::as_simd` to obtain an aligned middle portion and process the prefix and suffix scalarly (§2.1).

---

## 5 Handling Non‑typical Cases

Certain workloads cannot be vectorised easily because of irregular memory access or control flow. Best Practice 2025 recommends the following strategies:

### 5.1 Irregular access patterns and pointer chasing

Gather/scatter operations allow loading/storing values at arbitrary indices. In `core::simd`, use `Simd::gather_or_default` and `Simd::scatter` to load from or store to non‑contiguous addresses. On x86 these compile to gather/scatter instructions when supported; otherwise they may be emulated. Use them carefully, as they can be slower than contiguous loads.

When loops depend on data computed in previous iterations (e.g., linked‑list traversal), consider reorganising the data into a structure of arrays (SoA) or flattening the data into contiguous arrays. This removes pointer chasing and allows vectorisation.

### 5.2 Control flow within loops

Complex if/else branches inside a loop hinder auto‑vectorisation. Move conditionals out of the hot loop if possible, or precompute masks and use SIMD select/blend instructions. For example, compute a boolean mask with `lane_is_nonzero` and then use `Simd::select(mask, a, b)` to choose between two values for all lanes.

### 5.3 Small loops and short arrays

Vectorisation has overhead (loading registers, handling tails). For very small arrays (fewer than a few vector widths), scalar code may be faster. Write generic functions that select vectorised or scalar implementations based on length and benchmark both paths.

### 5.4 Variable‑length operations and early exits

When the loop length depends on runtime data or may exit early (e.g., scanning until a delimiter), the compiler cannot fully vectorise it. One technique is to process blocks of fixed size with SIMD and perform the termination check in scalar code after each block. This transforms the irregular loop into a regular vectorised loop with a check between blocks.

### 5.5 Combining multi‑dimensional computations

In multi‑dimensional arrays (matrices, images), memory is often stored in row‑major order. Iterate over contiguous memory to enable vectorisation. When processing columns, consider transposing the matrix or using blocking techniques to operate on contiguous sub‑blocks.

---

## 6 Performance and Quality Considerations (Best Practice 2025)

* **Portability vs. specificity** – Start with high-level, portable SIMD (`core::simd`) and auto‑vectorisation. These abstractions generate efficient code across architectures and avoid vendor lock‑in. Introduce `std::arch` intrinsics only after profiling and only behind feature detection (nrempel.com).
* **Alignment and memory safety** – Always consider alignment. Use `as_simd` or `align_to` to guarantee safe access; misalignment can cause undefined behaviour or performance penalties (users.rust-lang.org, rust-lang.github.io).
* **Maintainability and readability** – Write clear, idiomatic Rust first. Intrinsics and unsafe code introduce complexity and potential bugs. Document assumptions (alignment, CPU features) and provide scalar fallbacks. The long‑term cost of unreadable code often outweighs small performance gains.
* **Testing and benchmarking** – Use Criterion or similar frameworks to benchmark different implementations on your target hardware. Because vector instructions vary across CPUs, measure on representative machines and consider enabling features conditionally. Use Godbolt or `cargo asm` to inspect the compiled assembly.
* **Stability** – Nightly features such as `core::simd` may change. Consider feature flags or compile‑time checks to prepare for API changes. If you rely on `#[target_feature]` or unstable intrinsics, plan for potential deprecations.

---

## 7 Summary

Vectorisation in Rust 1.90 relies on a balance between trusting the compiler and using explicit SIMD when necessary:

* Structure loops to make data access predictable and independent, avoid loop‑carried dependencies, and use iterators and zipped slices for clarity and performance (users.rust-lang.org).
* Use `core::simd` to write portable, safe SIMD code. Employ `slice::as_simd` for aligned processing and handle tails with scalar code (doc.rust-lang.org).
* For maximum control, use `std::arch` intrinsics with proper static or dynamic feature detection (doc.rust-lang.org), and enable CPU features via RUSTFLAGS or `#[target_feature]` (rust-lang.github.io).
* Always consider alignment and memory layout, and restructure data when necessary (users.rust-lang.org).
* Benchmark and verify the generated assembly to ensure your code is vectorising as expected (tweedegolf.nl).

By following these guidelines, you can harness the full power of SIMD on Rust nightly 1.90 while maintaining code quality and portability.
