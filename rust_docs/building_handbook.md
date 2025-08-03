# Best Practice Guide for Building and Optimizing Code with Rust Nightly 1.91 (2025)

Rust nightly 1.91 (branch date: 2 Aug 2025) contains numerous improvements to the compiler and tooling.  In addition to the features introduced in 1.90, the 1.91 release guarantees that `Vec::with_capacity` always allocates at least the requested capacity and makes many `std::arch` intrinsics safe when the appropriate CPU features are enabled【684066008670463†L134-L140】.  These changes can influence build‑time tuning decisions (e.g. you may no longer need to over‑reserve vector capacity or mark some intrinsic calls `unsafe`).  This guide summarises best‑practice (2025) techniques for configuring builds and optimizing compilation with the nightly compiler.  It is aimed at engineers working in performance‑critical or safety‑critical environments and follows the guideline: *measure, reason, and only then optimize*.  All recommendations below are supported by official Rust documentation and recent performance research.

---

## 1 Profiles and Fundamental Build Configuration

Cargo ships with several built-in profiles: `dev`, `release`, `test`, and `bench`. Each profile sets defaults for optimization level, debug info, link-time optimization, and other flags. Understanding these defaults is critical before making changes.

| Profile   | Purpose                                           | Key Descriptors                                                                                                                                                                                           | Effect                                                                        |
| --------- | ------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| `dev`     | Used by `cargo build` / `cargo run` in dev        | `opt-level=0`, `debug=true`, `split-debuginfo` (platform-specific), `strip=none`, `debug-assertions=true`, `overflow-checks=true`, `lto=false`, `panic="unwind"`, `incremental=true`, `codegen-units=256` | Fast compile; unoptimized code; includes debug assertions and overflow checks |
| `release` | Used by `cargo build --release` / `run --release` | `opt-level=3`, `debug=false`, `strip=none`, `debug-assertions=false`, `overflow-checks=false`, `lto=false`, `panic="unwind"`, `incremental=false`, `codegen-units=16`                                     | Highly optimized code; slower compile; no debug or overflow checks            |
| `test`    | Used by `cargo test`                              | Inherits from `dev`                                                                                                                                                                                       | Debug info and assertions retained; compile time like `dev`                   |
| `bench`   | Used by `cargo bench`                             | Inherits from `release`                                                                                                                                                                                   | Release-like optimization for benchmarks                                      |

Custom profiles can be defined when neither `dev` nor `release` fits. A custom profile inherits from a base profile and overrides selected settings. Switch profiles via:

```bash
cargo build --profile=NAME
```

### 1.1 Key Profile Settings

| Setting         | Description                                      | Options                                      | Trade-offs                                                                                            |
| --------------- | ------------------------------------------------ | -------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| `opt-level`     | Controls the optimization level (`-C opt-level`) | `0`–`3` (performance), `s`/`z` (binary size) | Higher levels improve runtime speed but increase compile time; `s`/`z` reduce size but can slow code. |
| `debug`         | Amount of debug info                             | `false`/`0`, `line-tables-only`              | Debug info aids backtraces but increases compile time and binary size.                                |
| `panic`         | Panic strategy (`-C panic`)                      | `unwind` (default), `abort`                  | `abort` reduces binary size and slightly speeds code but disables unwinding across Rust code.         |
| `lto`           | Link-time optimization                           | `false`, `thin`, `fat`                       | `thin`/`fat` improve speed/size at cost of longer link times.                                         |
| `codegen-units` | Number of codegen units compiled in parallel     | Integer ≥ 1 (default: 16 release, 256 dev)   | Fewer units allow cross-module inlining; more units boost parallelism.                                |
| `incremental`   | Enables incremental compilation                  | `true`, `false`                              | Speeds subsequent builds at cost of disk space; disabled for release by default.                      |
| `strip`         | Strips debug info and symbols                    | `debuginfo`, `symbols`, `true`               | Reduces binary size but limits debugging and profiling capabilities.                                  |

---

## 2 Typical Build Scenarios and Recommended Configurations

Below are recommended configurations for common scenarios. Key settings list only notable overrides relative to profile defaults.

### 2.1 Fast Development Builds

Prioritize compile speed and interactive feedback during everyday coding.

| Scenario                     | Base Profile | Key Settings                                | Reason                                                                                              |
| ---------------------------- | ------------ | ------------------------------------------- | --------------------------------------------------------------------------------------------------- |
| Standard dev build           | `dev`        | (defaults)                                  | Quick builds with full debug info and overflow checks.                                              |
| Faster incremental dev build | `dev`        | `debug=false` or `debug="line-tables-only"` | Reduces debug info, saving 20–40% compile time while keeping line tables for backtraces.            |
| Parallel front-end dev build | `dev`        | `RUSTFLAGS="-Z threads=8"`                  | Enables experimental parallel front-end, cutting compile times up to 50% on multi-core machines.    |
| Cranelift back-end           | `dev`        | `RUSTFLAGS="-Z codegen-backend=cranelift"`  | Speeds code generation at the cost of slower runtime; suitable for local development only.          |
| Use `cargo check`            | N/A          | N/A                                         | Performs type/borrow checking without producing a binary; typically 2–3× faster than `cargo build`. |
| Cache builds                 | N/A          | `RUSTC_WRAPPER=sccache`                     | Caches compiled artifacts to drastically reduce build times on repeated builds.                     |

General advice:

* Keep dependencies up to date and remove unused ones (e.g. `cargo machete`).
* Run `cargo build --timings` to identify slow-compiling crates.
* For slow linking, switch to a faster linker (e.g. `-C link-arg=-fuse-ld=lld`).

### 2.2 Optimized Production (Release) Builds

Prioritize runtime performance and, optionally, binary size. Always measure in your application.

| Scenario                    | Base Profile | Key Settings                                                                       | Effect                                                                                         |
| --------------------------- | ------------ | ---------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| Default release             | `release`    | (defaults)                                                                         | Full optimization with moderate parallelism and cross-crate inlining.                          |
| Maximize runtime speed      | `release`    | `codegen-units=1`, `lto="fat"`, `panic="abort"`                                    | Whole-program optimizations; smaller binary; longer compile times.                             |
| CPU-specific instructions   | any          | `-C target-cpu=native`                                                             | Generates code tuned to host CPU (e.g. AVX), yielding 10–20% speedups on homogeneous hardware. |
| Profile-guided optimization | `release`    | Two-phase build: instrument (`-Cprofile-generate`), run workloads,                 | \~10% runtime improvement; use tools like `cargo-pgo` to simplify workflow.                    |
|                             |              | merge with `llvm-profdata`, rebuild with `-Cprofile-use`.                          |                                                                                                |
| Alternative allocator       | `release`    | Use `jemalloc` or `mimalloc` via `#[global_allocator]`                             | Reduces heap overhead and fragmentation; evaluate case by case.                                |
| Strip symbols/debug info    | `release`    | `strip = "debuginfo"` or `strip = "symbols"`                                       | Reduces binary size (often 4× smaller on Linux) at the cost of debuggability.                  |
| Optimize for small size     | `release`    | `opt-level="z"`, `lto="fat"`, `codegen-units=1`, `panic="abort"`,`strip="symbols"` | Smallest binaries; slower runtime and longer compiles.                                         |

Note: On x86\_64/Linux nightly, `lld` is default and reduces link times ≈30%. On other platforms, enable `lld` via `-C link-arg=-fuse-ld=lld` or try `mold`.

### 2.3 Hybrid and Benchmarking Builds

Combine runtime speed with minimal debug capability by defining a custom profile:

```toml
[profile.release-debug]
inherits = "release"
debug = "line-tables-only"
codegen-units = 1
lto = "thin"
strip = "debuginfo"
```

Invoke with:

```bash
cargo build --profile=release-debug
```

---

## 3 Reducing Compilation Time

Strategies to minimize developer build costs:

* **Use `cargo check`** early and often; it skips codegen and is 2–3× faster than `cargo build`.
* **Keep the toolchain updated** (`rustup update`) to benefit from compiler performance improvements.
* **Enable parallel front-end** (`-Z threads=8`) for up to 50% faster compilation on multi-core machines.
* **Switch linkers** to `lld` or `mold` to cut link times dramatically.
* **Reduce debug info** (`debug=false` or `line-tables-only`) and disable incremental for CI to ensure deterministic builds.
* **Cache artifacts** with `sccache` or `cargo-chef` across CI runs and workstations.
* **Simplify code/dependencies**: remove unused crates, minimize procedural macros, and split large crates into smaller ones.
* **Tune codegen units**: fewer units for large crates (better inlining), more units for small crates (parallelism).

---

## 4 Atypical and Advanced Build Scenarios

### 4.1 Cross-compiling to Other Architectures

1. **Install target support**:

   ```bash
   rustup target add <target>
   ```
2. **Configure linker** in `.cargo/config.toml`:

   ```toml
   [target.<triple>]
   linker = "<cross-linker>"
   ar = "<archiver>"
   ```
3. **Build**:

   ```bash
   cargo build --target=<triple>
   ```
4. **Use `-Z build-std`** on nightly to compile core/std with custom flags if needed:

   ```bash
   cargo +nightly build -Z build-std --target=thumbv7em-none-eabihf --release \
     --features="compiler-builtins/mem" \
     --config target.thumbv7em-none-eabihf.runner="arm-none-eabi-gdb -q"
   ```
5. For `#![no_std]` environments, supply custom `panic_handler`, memory layouts (`-C link-arg=-Tlink.x`), and enable `build-std` if the standard library is unavailable.

### 4.2 Interpreters and Static Analysis

* **MIRI**: run `cargo +nightly miri test` to detect undefined behavior, alignment issues, and aliasing. Invaluable for safety-critical code.
* **Clippy & rustfmt**: enforce style and correctness with:

  ```bash
  cargo clippy --all-targets --all-features -- -D warnings
  cargo fmt --all
  ```
* **Rustdoc depinfo**: use `-Z rustdoc-depinfo` to collect dependency info during docs builds.

### 4.3 Custom Back-ends and Experimental Flags

* **Cranelift**: `-Z codegen-backend=cranelift` for faster codegen during development.
* **Parallel threads**: `-Z threads=<N>` for front-end concurrency.
* **Share generics**: `-Z share-generics=y` to reduce binary size at cost of longer incremental builds.
* **Build std**: `-Z build-std` / `-Z build-std-features` for custom std builds.
* **Unstable options**: consult `rustc --help -Z` for nightly-only flags.

---

## 5 Quality, Testing, and Security Best Practices (2025)

* **Static analysis**: run Clippy regularly; treat warnings as errors.
* **Formatting**: use rustfmt for code consistency.
* **Testing**: `cargo test` for unit tests; `cargo bench` for benchmarks; use custom profiles for large suites.
* **CI**: enable `--timings`, cross-platform builds, and strict flags (`-D warnings`).
* **Dependency hygiene**: `cargo update`; `cargo audit`; remove unused crates; enforce `RUSTFLAGS="-D warnings"` in CI.
* **Security**: prefer `panic="unwind"` in critical code to allow cleanup; use MIRI and `cargo-tarpaulin` for coverage.
* **Benchmark & profile**: use `perf`, `cargo flamegraph`, or `samply` to measure changes in isolation.

---

## 6 Summary of Recommendations

| Goal                         | Settings                                                                                                    | Expected Effect                                           | Notes                                                              |
| ---------------------------- | ----------------------------------------------------------------------------------------------------------- | --------------------------------------------------------- | ------------------------------------------------------------------ |
| Maximum runtime performance  | `opt-level=3`, `codegen-units=1`, `lto="fat"`, `panic="abort"`, `strip="symbols"`, `target-cpu=native`      | Fastest code; smallest size; very long compile/link times | Benchmark to verify gains; use on homogeneous hardware only.       |
| Balanced optimization        | `opt-level=2`, `codegen-units=1`, `lto="thin"`, `panic="unwind"`, `strip="debuginfo"`                       | Good speed with moderate compile time                     | Suitable default for many applications.                            |
| Smallest binary size         | `opt-level="z"`, `codegen-units=1`, `lto="fat"`, `panic="abort"`, `strip="symbols"`                         | Minimizes size at expense of runtime speed                | Ideal for embedded or constrained distribution channels.           |
| Fastest development feedback | Dev profile with `debug=false` or `line-tables-only`, `incremental=true`, `-Z threads=8`, Cranelift backend | Shortest compile time; slower binaries                    | Use `cargo check` for most edits; avoid shipping Cranelift builds. |
| Cross-compile                | `--target=<triple>`, configured linker, optional `-Z build-std`                                             | Builds for other architectures                            | Install targets via `rustup target add`; customize for `no_std`.   |

---

*Key takeaway*: Rust’s compile-time and runtime behaviors are highly configurable. Nightly 1.91 builds on the improvements of 1.90—parallel front‑end compilation, custom back‑ends, profile‑guided optimisation and more—and adds stability to previously unstable APIs.  Always start from baseline profiles, adjust a few parameters at a time, benchmark thoroughly, and maintain code quality with lints and testing.  These practices will help you build fast, small and high‑quality Rust binaries in 2025.
