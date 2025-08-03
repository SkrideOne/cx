# Best Practices for Organising Rust Code and Repositories (Nightly 1.91 – Best Practice 2025)

Modern Rust development emphasises maintainability, clarity and efficiency. This manual summarises best‑practice guidance (circa 2025) for organising code and repositories when using the Rust nightly 1.91 toolchain.  Nightly 1.91 builds on the improvements introduced in 1.90—such as `Vec::with_capacity` now guaranteeing it allocates at least the requested capacity and many `std::arch` intrinsics being callable from safe code when the appropriate CPU features are enabled【684066008670463†L134-L140】—and retains the same project‑structuring principles.  Recommendations follow official documentation, the Rust API Guidelines and the Cargo book.  Typical cases (library/binary projects, workspaces, FFI and concurrency‑heavy crates) are covered first, followed by non‑typical situations.

---

## 1 Project layout

### 1.1 Standard Cargo package structure (typical case)

Rust packages follow a conventional layout recognised by Cargo:

```
Cargo.toml        # Manifest: metadata, dependencies, build scripts
Cargo.lock        # Exact versions (commit for applications, omit for libraries)
src/
  ├── lib.rs      # Default library target (public API)
  ├── main.rs     # Default binary target
  └── bin/        # Additional binaries (kebab-case filenames or subdirectories)
tests/            # Integration tests (each file is its own crate)
examples/         # Example programs and demos
environments/    # Optional: example configs
target/           # Build output (ignore in VCS)
benches/          # Benchmarks (nightly)
```

Module and file names use **snake\_case**; binary and test names use **kebab-case**. Integration tests import the library with `use your_crate::*`.

### 1.2 Customising the manifest

* **Package metadata**: description, license (SPDX identifier), authors, edition, `rust-version`. Workspaces can define defaults in `[workspace.package]` to inherit.
* **Dependencies**: group logically; document purpose; enable optional features via `features = ["dep/feature"]`.
* **Lints and profiles**: set `lints.workspace = true` in root manifest for shared Clippy settings. Define `[profile.dev]` and `[profile.release]` to tune optimisations and debug info. For performance-critical crates, set `panic = "abort"`.

### 1.3 Modules and visibility

* **Group by functionality** (e.g., `network`, `storage`) rather than by type.
* Declare modules with `mod foo;` corresponding to `foo.rs` or `foo/mod.rs`; avoid deep hierarchies (2–3 levels max).
* Items are private by default; use `pub` to expose. Flatten public APIs with `pub use`.
* Organise `use` statements: group external crates first, then `use` sorted lexically; list `self` and `super` imports before others; use `as` to avoid conflicts.
* Keep definitions and implementations together; avoid separate `types.rs` and `impl.rs` files.

---

## 2 Workspaces and monorepos

### 2.1 When to use a workspace

Use a workspace when:

* The repo contains multiple crates that evolve together (library, CLI, plugin).
* You want shared dependencies, lints and profiles across crates.
* You need coordinated versioning via `workspace.package.version`.

In the root `Cargo.toml`, add:

```toml
[workspace]
members = ["crate1", "crates/cli"]
# optional: exclude or default-members
```

A virtual manifest has no `[package]` section.

### 2.2 Workspace best practices

* **Root configuration**: define `[workspace.package]` fields (edition, description, license, repository) for inheritance.
* **Per-crate overrides**: override inherited fields in member manifests as needed.
* **Shared dependencies**: list common dependencies under `[workspace.dependencies]`; members use `foo = { workspace = true }`.
* **Shared profiles and lints**: define `[workspace.profile.*]` and `[workspace.lints]` for consistent optimisation and lint levels.

---

## 3 Coding style and naming conventions

### 3.1 General style

* **Ordering**: extern crates (rare), then `use` statements, then modules; sort each group lexically (case-insensitive), with `self`/`super` first.
* **Functions**: consistent indentation, aligned return types and `where` clauses; keep functions small and cohesive.
* **Naming**: `snake_case` for functions/variables/modules/files; `UpperCamelCase` for types/traits/enums; `SCREAMING_SNAKE_CASE` for constants.
* **Conversion methods**: `as_` for cheap reference conversions, `to_` for non-consuming conversions, `into_` for consuming conversions.

### 3.2 Documenting code

* **Crate-level docs**: start with a module-level `//!` comment explaining purpose and entry points.
* **Examples and doctests**: include runnable examples using `?`; place larger demos in `examples/` and cross-link.
* **Error/panic/safety sections**: document error types, panic conditions and `unsafe` invariants.
* **Intra-doc links**: use `[TypeName]` or `[module::item]` for navigation.
* **Nightly features**: hide unstable APIs behind feature flags; link to tracking issues.

### 3.3 Error handling

* Return `Result` for recoverable errors with context (custom enums or `thiserror`).
* Use `panic!` only for unrecoverable cases; set `panic = "abort"` for performance.
* Avoid `unwrap`/`expect`; prefer `?` and pattern matching.
* Use `anyhow` for applications and `thiserror` for libraries.
* Integrate structured logging (e.g., `tracing`) with sufficient context.

---

## 4 Testing and continuous integration


### 4.1 Unit, integration and documentation tests

* **Unit tests**: `#[cfg(test)] mod tests` in the same file; test private and edge cases.
* **Integration tests**: files in `tests/` as separate crates.
* **Doc tests**: code examples in doc comments compile and run with `cargo test`.
* **Examples**: programs in `examples/` compiled with tests.
* **Benchmarks** (nightly): since Rust 1.91 the long‑deprecated `#[bench]` attribute is now fully de‑stabilised and triggers a hard error when used without enabling custom test frameworks【47017583311317†L20-L23】.  For benchmarking, use an external harness such as **Criterion** or **Iai**, or define a custom test framework via `#![feature(custom_test_frameworks)]` and the `test` crate.  Place benchmarks in `benches/` and run them with `cargo bench` or your harness of choice.

### 4.2 Continuous integration (CI)

* Use GitHub Actions or similar to build/test on stable, beta and nightly.
* Test on Linux, macOS and Windows; run `cargo fmt --check` and `cargo clippy`.
* Cache dependencies; test `--all-features` and `--no-default-features`.
* Optionally verify `cargo update --verbose` builds.

---

## 5 Performance and quality

### 5.1 Optimising memory allocations

* **Pre-allocate**: `Vec::with_capacity`, `String::with_capacity`.
* Use `smallvec::SmallVec` or `arrayvec::ArrayVec` for small collections.
* Return iterators instead of collecting intermediate data.
* Use `Vec::extend` and `Vec::retain` for operations.
* Choose hashers: default SipHash vs faster `FxHashMap` or `AHashMap` where safe.

### 5.2 I/O and buffering

* Wrap streams in `BufReader`/`BufWriter`; call `flush()` explicitly.
* Lock `stdout` once for batched writes rather than repeated `println!`.

### 5.3 Iterators and algorithmic efficiency

* Expose iterators; implement `size_hint()` and `ExactSizeIterator` where appropriate.
* Use `filter_map` and `chunks_exact` for single-pass efficiency.

### 5.4 Parallelism and concurrency

* Understand `Send`/`Sync`; use `Arc` for shared ownership and `Mutex`/`RwLock` for mutable state.
* Prefer channels (`std::sync::mpsc`, `crossbeam`) or atomic types.
* Use `Rayon` for data-parallel iterator chains.

### 5.5 General performance advice

* Profile hot code before optimising; choose algorithms and data structures wisely.
* Benchmark under release builds; use `criterion` or `cargo bench`.
* Use `cargo clippy` to catch performance anti-patterns.

---

## 6 Build configuration and profiles

Rust build profiles allow tuning for speed or size:

```toml
[profile.dev]
opt-level = 0

[profile.release]
opt-level = 3        # or "s"/"z" for size
codegen-units = 1   # better optimisation
lto = "thin"       # link-time optimisation
strip = "debuginfo" # remove debug symbols
```

---

## 7 Concurrency, unsafe code and FFI

### 7.1 Concurrency and safety

* Use `Arc` with `Mutex`/`RwLock`; consider `parking_lot` for faster locks.
* Avoid busy-waiting; use channels or condition variables.
* For async, use `tokio` or `async-std` and document thread-safety invariants.

### 7.2 Unsafe code guidelines

* Encapsulate `unsafe` behind safe abstractions; document invariants.
* Wrap `extern` functions in safe wrappers; validate inputs.
* Allocate buffers in Rust before passing pointers to C and use `.set_len()` carefully.
* Mark exported functions with `#[no_mangle] pub extern "C" fn ...`.
* Do not implement `Send`/`Sync` manually without full understanding.

### 7.3 FFI and non-Rust dependencies

* Keep FFI boundaries in dedicated modules (e.g., `ffi.rs`).
* Use `bindgen` or handwritten declarations for correct signatures.
* Provide safe Rust wrappers and handle errors gracefully.

---

## 8 Handling nightly features (non-typical cases)

* Gate unstable APIs behind Cargo features; disable by default.
* Link to tracking issues; avoid exposing unstable APIs publicly.
* Pin nightly version in `rust-toolchain.toml` or via `rustup override set`.

---

## 9 Non-typical project patterns

### 9.1 Cross-language monorepos

* Isolate Rust in `rust/` with its own workspace.
* Use top-level scripts or meta-build tools (e.g., `just`, `Makefile`).
* Provide FFI crates (`ffi-cpp`, `ffi-python`) with safe wrappers.
* Document build and test steps for all languages.

### 9.2 `no_std` and embedded development

* Add `#![no_std]` and `extern crate alloc` if needed.
* Use HAL crates (`embedded-hal`), logging (`defmt`), and provide `memory.x` linker script.
* Set `panic = "abort"` or use a custom panic handler.
* Document supported boards and config.

### 9.3 Plugin architecture or dynamic loading

* Define a versioned trait; build plugins as `cdylib`.
* Use `libloading` or `xtask` patterns for scaffolding.
* Document the plugin ABI and provide examples.

---

## 10 Summary tables

### 10.1 Typical scenarios

| Scenario                | Key best practices                                                                                                                                     |
| ----------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Library crate           | Standard layout; `src/lib.rs`; modules by feature; flat public API; crate-level docs; return iterators; pre-allocate collections; tune release profile |
| Binary crate            | `src/main.rs`; additional binaries in `src/bin/`; CLI parsing; buffered I/O; integration tests                                                         |
| Workspace (multi-crate) | Root `Cargo.toml` with `[workspace]`; shared deps, lints, profiles; separate crates; cross-crate Clippy                                                |
| Concurrency-heavy crate | Use `Arc` + locks; prefer `parking_lot`; thread-safe APIs; `Rayon`; safe `Send`/`Sync`; avoid logic races                                              |
| FFI crate               | Dedicated FFI modules; safe wrappers around `unsafe extern`; Rust-side buffers; `#[no_mangle]` exports; documented invariants                          |

### 10.2 Non-typical scenarios

| Scenario                 | Key considerations                                                              |
| ------------------------ | ------------------------------------------------------------------------------- |
| Cross-language monorepo  | Isolate Rust workspace; clear FFI layers; build scripts; safe wrappers          |
| `no_std` / embedded      | `#![no_std]`; `alloc`; linker script; `panic = "abort"`; HAL crates; board docs |
| Plugin / dynamic loading | Versioned trait; `cdylib` plugins; `libloading`; ABI docs; `xtask` scaffolding  |
| Nightly feature usage    | Feature gates; pin toolchain; tracking issues; avoid public unstable APIs       |

---

## 11 Conclusion

Best practices in 2025 emphasise clarity (well-defined modules, documented APIs), quality (comprehensive tests, CI, consistent style) and performance (release builds, efficient data structures, careful memory and I/O management). Organise projects around cohesive features and workspaces; document errors and safety invariants; profile before optimising. Tailor these guidelines to your use case to produce maintainable, human-friendly, high-performance Rust code.
