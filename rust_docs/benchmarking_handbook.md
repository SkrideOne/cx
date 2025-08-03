# Rust Benchmarking Manual (Best Practice 2025)

## Introduction

Benchmarking is the disciplined practice of measuring the performance of code to detect bottlenecks, compare implementations and avoid regressions. In modern Rust (nightly version 1.90 as of 2 Aug 2025), the traditional `#[bench]` attribute has been fully de‑stabilised (github.com). As a result, external benchmarking frameworks and custom harnesses are now the recommended way to write and run benchmarks on both the stable and nightly compilers. This manual sets out the 2025 best practices for creating reliable, reproducible and high‑quality benchmarks in Rust. The focus is on code quality and performance, not just raw speed, so each section discusses proper setup, fairness, analysis and reporting.

---

## 1 Types of Benchmarks

| Type                        | Description & Typical Tools                                                                     | When to Use                                                                                       |
| --------------------------- | ----------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| **Micro‑benchmark**         | Measures performance of a small function or algorithm. Tools: Criterion.rs, Divan, Iai.         | Tuning inner loops, comparing algorithms, checking small performance changes.                     |
| **Macro‑benchmark**         | Measures execution time of an entire program or CLI workload. Tools: Hyperfine, custom harness. | Assessing end‑to‑end latency, I/O, network or multi‑process benchmarks.                           |
| **Continuous benchmarking** | Tracks performance over time in CI. Tools: Bencher.dev, others.                                 | Maintaining performance characteristics across releases.                                          |
| **Custom measurement**      | Measures metrics other than wall‑clock time (e.g. CPU instructions, allocations).               | When time is noisy or alternative metrics (memory pressure, hardware counters) are more relevant. |

---

## 2 Environment & Build Configuration

* **Release builds only**: Always run benchmarks with optimisation enabled (`cargo bench --release`) because debug builds can be 10–100× slower (nnethercote.github.io).
* **Pin the CPU and isolate the system**: Use `taskset` or `numactl` to pin the process, disable Turbo Boost, and shut down other heavy processes. In cloud, use dedicated runners or average repeated runs.
* **Warm‑up**: Include an initial warm‑up phase to populate caches and JIT layers. Criterion and Divan handle this automatically; for custom harnesses, run a warm‑up iteration manually.
* **Consistent inputs**: Provide deterministic inputs and avoid global mutable state. Ensure idempotent functions so repeated calls do not change behavior (doc.rust-lang.org).
* **Use `std::hint::black_box`**: Prevent compiler optimisations from eliminating the code under test. Wrap inputs and outputs with `black_box` in Criterion and Divan (doc.rust-lang.org).

---

## 3 Benchmarking Frameworks

### 3.1 Criterion.rs – the gold standard

Criterion.rs is a statistical micro‑benchmarking framework that produces detailed measurements and plots (docs.rs). It works on stable and nightly Rust and provides:

* **Benchmark functions**: Use `criterion_group!` and `criterion_main!` to register. Each function takes `&mut Criterion` and calls `c.bench_function(name, |b| b.iter(|| /* code */ ));`.

```rust
use criterion::{criterion_group, criterion_main, Criterion};
use std::hint::black_box;

fn bench_multiply(c: &mut Criterion) {
    c.bench_function("multiply", |b| {
        b.iter(|| {
            let mut x = 1u64;
            for _ in 0..1000 { x = black_box(x * 2); }
            black_box(x)
        });
    });
}

criterion_group!(benches, bench_multiply);
criterion_main!(benches);
```

* **Parameterised benchmarks**: `bench_with_input`, `BenchmarkGroup`, `Throughput::Bytes(size)`.
* **Comparing implementations**: Register multiple variants under the same group.
* **Timing loops**: `b.iter`, `b.iter_with_large_drop`, `b.iter_batched(setup, test, BatchSize)`.
* **Custom measurements**: Implement `Measurement` to capture CPU, memory or hardware counters.
* **Async benchmarks**: Use `b.to_async(Executor)` with the production executor.
* **Profiling integration**: `--profile-time` option or implement `Profiler`.
* **Reports**: HTML in `target/criterion`, CSV for external tools (nickb.dev).
* **Libtest compatibility**: Criterion\_bencher\_compat for `#[bench]`-style code.

**Best practices**:

* Move setup outside the timing loop (doc.rust-lang.org).
* Always use `black_box` and return values to avoid dead-code elimination.
* Set throughput and control sample size & measurement time.
* Interpret statistics (mean, median, SD, outliers) thoughtfully.

---

### 3.2 Divan – simple and flexible

Divan is a lightweight framework with a friendly API and built-in profilers (nikolaivazquez.com).

* **Setup**: Add to `[dev-dependencies]`, configure `[[bench]] harness = false`, and call `divan::main()` in `main.rs`.
* **Declaring benchmarks**: Annotate functions with `#[divan::bench]`. Divan infers names and uses `black_box` internally.

```rust
fn main() { divan::main(); }

#[divan::bench]
fn increment() -> u64 {
    let mut x = 0;
    for _ in 0..1000 { x += 1; }
    x
}
```

* **Generic benchmarks**: Use `#[divan::bench(types = [Vec<i32>, LinkedList<i32>, HashSet<i32>, BTreeSet<i32>])]`.
* **Allocation profiling**: Enable `AllocProfiler` as `#[global_allocator]`.
* **Thread contention**: Use `threads` parameter to measure scalability.
* **Throughput & CPU timestamp**: Express bytes/sec or cycle counts.

**Best practices**:

* Benchmarks should return a value for accurate timing.
* Use generic parameters and consistent inputs.
* Leverage `AllocProfiler` and thread contention profiling.
* Compile with `--release --benches` in release mode.
* Monitor stability of Divan in 2025; prefer Criterion for rigorous needs.

---

### 3.3 Iai – instruction‑count micro‑benchmarking

Iai uses Cachegrind to count CPU instructions, cache and RAM accesses (bheisler.github.io).

* **Setup**: `[[bench]] harness = false`, `iai = "X.Y"` in `[dev-dependencies]`, and list functions in `iai::main!`.

```rust
use iai::{black_box, main};

fn fib_recursive(n: u32) -> u32 { /* … */ }
fn fib_iterative(n: u32) -> u32 { /* … */ }

fn bench_recursive() { black_box(fib_recursive(20)); }
fn bench_iterative() { black_box(fib_iterative(20)); }

main!(bench_recursive, bench_iterative);
```

* **Output**: Instruction counts and estimated cycles per function.
* **Limitations**: No separation of setup, no async/multi-thread support, high Valgrind overhead.

Use Iai alongside Criterion to validate instruction-level improvements.

---

## 4 Writing Benchmarks on Nightly 1.90

The `#[bench]` attribute is fully de‑stabilised as of Rust 1.88 (github.com). To use it on nightly:

1. Enable features at crate root: `#![feature(test)]`, `#![feature(custom_test_frameworks)]`.
2. `extern crate test;` and use `test::black_box`.
3. Define `#[bench] fn my_bench(b: &mut test::Bencher)` with setup outside the closure.
4. Run `cargo bench` to execute benchmarks. **Note**: this harness is deprecated; prefer Criterion or Divan.

---

## 5 Atypical Benchmarking Scenarios

### 5.1 Macro‑benchmarks and CLI programs

* Use **Hyperfine** for shell commands: warm‑up runs, sample counts, JSON export.
* Or write a custom Rust harness with `std::time::Instant` and `std::process::Command`.
* Avoid micro‑benchmarks for I/O; use macro‑benchmarks with large datasets and discard the first run.

### 5.2 Asynchronous and concurrent code

* Benchmark async functions with Criterion’s async support (`b.to_async(Executor)`).
* Consider benchmarking the underlying synchronous logic separately.
* For multi-threaded code, use Divan’s `threads` parameter and barriers, or macro‑benchmarks for throughput.

### 5.3 Memory and allocation analysis

* Use Divan’s `AllocProfiler` for allocation counts and bytes.
* Combine Criterion’s `--profile-time` with Valgrind’s massif/cachegrind.
* Use Iai for instruction and cache-access counts.
* Manual instrumentation: GlobalAlloc hooks, metrics counters.

### 5.4 FFI and cross‑language benchmarks

* Isolate FFI calls and benchmark Rust wrappers separately.
* Prevent inlining with `#[inline(never)]` and use `black_box`.
* Use large input sizes to amortise FFI overhead.
* Ensure correct `unsafe` usage to avoid UB.

### 5.5 Network and I/O benchmarks

* Use realistic workloads and warm or clear OS caches.
* Use macro‑benchmarks (Hyperfine for CLI, wrk/ab/hey for servers) under varied concurrency.
* Compare async vs blocking I/O with appropriate runtimes and measure both latency and throughput.

### 5.6 Continuous benchmarking & CI

* Use Bencher.dev or similar to store histories and alert on regressions.
* Commit benchmark code and baseline results (CSV/JSON) to VCS.
* Prefer dedicated hardware runners or relative benchmarking on shared CI.
* Visualise results with custom plots: clear titles, labeled axes, variant distinctions.

---

## 6 General Performance Tips (2025)

* **Optimise hot code first**: Profile (`perf`, `flamegraph`, Valgrind) before benchmarking (nnethercote.github.io).
* **Cache awareness**: Place data contiguously, reduce branching, use profilers to find contention (nikolaivazquez.com).
* **`#[inline]` judiciously**: Inline small functions; mark FFI boundaries with `#[inline(never)]`.
* **Choose the right metric**: Use instruction counts or cycle counters for high precision in noisy environments.
* **Iterate and document**: Record assumptions, environment and inputs; refine benchmarks over time (nnethercote.github.io).

---

## Conclusion

Rust’s de‑stabilisation of `#[bench]` necessitates modern frameworks. **Criterion.rs** remains the most feature‑rich for statistical micro‑benchmarks; **Divan** offers simplicity with built‑in profiling; **Iai** provides instruction counts. For macro‑benchmarks, use **Hyperfine** or custom harnesses. Always follow best practices: release builds, isolation, `black_box`, warm‑ups and thoughtful analysis. Continuous benchmarking (e.g. Bencher.dev) catches regressions early. Adhering to these guidelines produces reliable, actionable benchmarks that guide algorithmic and systemic optimisations in Rust 2025 and beyond.
