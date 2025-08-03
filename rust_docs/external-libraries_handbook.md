# Best-Practice 2025 methodology for organising and using external libraries in Rust (nightly 1.91)

## Introduction

Rust’s ecosystem thrives on a large collection of third‑party crates, yet pulling in external code has costs: build times, binary size, maintenance burden and security risk.  Nightly 1.91 gives access to experimental features such as portable SIMD, the new feature resolver and safe non‑pointer `std::arch` intrinsics【684066008670463†L134-L140】, but it also demands care because unstable features may change.  This guide summarises Best Practice 2025 for managing external libraries on Rust nightly 1.91.  The focus is on quality, performance and clarity, and all recommendations use declarative approaches (controlling behaviour through `Cargo.toml` and compile‑time flags rather than runtime scripts).

---

## 1 Selecting dependencies

### 1.1 Prefer high-quality crates

* Choose crates with a strong reputation (stars, downloads, active maintainers).
* Minimize unsafe usage and isolate unsafe code behind safe abstractions.
* Evaluate documentation and tests; doc-tested examples indicate good API design.
* For crates wrapping C/C++ libraries, prefer simple build scripts and avoid reliance on system tools like pkg-config.

### 1.2 Security and maintenance

* Audit dependencies regularly using `cargo audit` or equivalent vulnerability scanners in CI.
* Monitor update history and yanked versions; avoid crates without recent releases or with multiple yanks.
* Keep dependencies up-to-date with `cargo update` to reduce exposure to known issues.

---

## 2 Declaring dependencies in Cargo.toml

### 2.1 Specifying versions

Rust uses semantic versioning. Version requirements define ranges rather than exact versions:

> `time = "0.1.12"` means `>=0.1.12, <0.2.0`.

* Use the default caret requirement (e.g. `serde = "1.2.3"` is equivalent to `^1.2.3`).
* Avoid overly broad requirements (e.g. `serde = "1"`); at minimum specify major and minor, often including patch.
* Do not set an upper bound below the next major release; only pin full versions under exceptional circumstances.
* When re-exporting dependencies in a public API, enable the `public-dependency` feature to trigger the `exported_private_dependencies` lint.

### 2.2 Minimal vs. pinned versions

Two valid strategies:

| Strategy                 | Description                                                                                      |
| ------------------------ | ------------------------------------------------------------------------------------------------ |
| Minimum required version | Specify the lowest version you rely on; use `cargo update -Z minimal-versions` in CI to test.    |
| Latest known version     | Specify the latest tested minor/patch release; bump at the start of development, rely on semver. |

For libraries, a hybrid approach is common: pin to the latest patch of a minor release, update regularly and run minimal-version tests in CI.

### 2.3 Git and path dependencies

* Use git dependencies when unreleased fixes are needed; specify URL plus `branch`, `tag` or `rev`.
* Use `[patch]` in the workspace root to override crates (forks or local paths) across all members.
* Avoid mixing `git` and `path` keys or specifying commits in URLs to prevent warnings.

### 2.4 Alternative registries and local repositories

* For private registries, set the `registry` key in dependency declarations; note that crates.io packages cannot depend on external registries.
* For local development, use `path` dependencies; replace with registry releases or `[patch]` overrides for production builds.

---

## 3 Managing features and optional dependencies

### 3.1 Define fine-grained features

* Mark optional dependencies with `optional = true` and include them under `[features]`.
* Group related functionality into features; keep the default feature set minimal.
* Disable default features of dependencies by setting `default-features = false` and enabling only needed features.

### 3.2 Gating optional back-ends

Declare separate features to map to dependency features:

```toml
[dependencies]
ureq = { version = "3.0", default-features = false }

[features]
default = ["rustls"]
rustls  = ["ureq/rustls"]
native-tls = ["ureq/native-tls"]
```

Users select the desired back-end by enabling the corresponding feature, avoiding unnecessary dependencies.

### 3.3 New feature resolver

Set the resolver to version 2:

```toml
[package]
resolver = "2"
```

This prevents build-, dev- and target-specific dependency features from unifying with normal dependencies, reducing unintended activations.

### 3.4 Public and private dependencies

Enable the nightly `public-dependency` feature and mark dependencies exposed in the public API. This activates the `exported_private_dependencies` lint to maintain API stability.

### 3.5 Disabling unused features

* Inspect dependency features on docs.rs; many crates enable heavy defaults by default.
* Disable unused features to reduce compile time and binary size.
* Verify functionality by running the test suite after pruning.

---

## 4 Organizing code and workspaces

### 4.1 Modules, crates and packages

* Use modules and crates to split functionality for encapsulation and parallel compilation.
* Keep each crate focused; avoid overly large “kitchen sink” crates.
* Use workspaces to share `Cargo.lock` and target directories, reducing rebuilds and ensuring consistent versions.

### 4.2 Overriding dependencies and local fixes

* Use `[patch.crates-io]` in the workspace root for temporary overrides (forks or local paths).
* Document and remove overrides once upstream fixes are released.

### 4.3 Build scripts and foreign libraries

* Keep `build.rs` simple and deterministic; prefer the `cc` crate for compiling C/C++ and `bindgen` for Rust bindings.
* Offer a `vendored` feature (default) for bundled C/C++ code; allow disabling for system-installed libraries.
* Commit generated bindings and provide a `bindgen`-only feature for developers to regenerate when needed.

### 4.4 Cross-compilation and `no_std`

* For embedded or WebAssembly targets, disable `std` in dependencies and enable `alloc` or `core` features.
* Use `[target.'cfg(...)'.dependencies]` for platform-specific crates; the new resolver prevents feature leakage.

---

## 5 Performance-oriented practices

### 5.1 Parallelism and SIMD

* Use `rayon` for data parallelism (e.g., `.par_bridge().try_for_each()` maintains error handling with parallelism).
* Encapsulate portable SIMD code behind features (e.g., `simd`) and provide scalar fallbacks.

### 5.2 Reducing build times and binary size

* Split large crates into smaller ones within a workspace to minimize recompilation.
* Remove unused dependencies using tools like `cargo machete`.
* Disable default features in dependencies to reduce compile time and attack surface.
* Gate expensive code paths behind features so most users compile only the core logic.

---

## 6 Testing and continuous integration

* Enable `#![warn(missing_docs)]` to enforce documentation on public items; use doc tests for examples.
* Test all feature combinations: `cargo test --all-features` and `cargo test --no-default-features`.
* Run minimal-version tests with `cargo update -Z direct-minimal-versions` (nightly).
* Audit dependencies in CI with `cargo audit` and detect outdated crates with `cargo outdated`.
* Use Clippy to enforce code quality, enabling lints like `deny(unsafe_code)` where possible.

---

## 7 Methodology for atypical cases

* **Private registries / internal crates**: configure `.cargo/config.toml` with `[source]` entries and use `[patch]` for overrides.
* **Air-gapped / offline**: use `cargo vendor` to prepare dependencies; set `source.crates-io.replace-with = "vendored-sources"` for offline builds.
* **Bridging to other languages**: isolate FFI in `build.rs`, gate Python or C APIs behind features; use `pyo3` or `cbindgen`.
* **Proc macros**: isolate procedural macro crates; set `proc-macro = true` and avoid enabling them by default.
* **No-std / embedded**: depend on `alloc`/`core`, disable default features, use `#[cfg(no_std)]`.
* **Multi-TLS support**: provide mutually exclusive features; emit compile-time errors on conflicts.
* **Internal caching**: enable tools like `sccache` for CI, but design deterministic builds without caching.
* **Experimental nightly features**: hide unstable APIs behind a `nightly` feature; provide stable fallbacks and guard with `#[cfg(nightly)]`.

---

## Conclusion

Organizing external dependencies effectively is essential for sustainable Rust projects. Key themes in Best Practice 2025 include:

* **Explicitness**: specify versions precisely.
* **Modularity**: use features and workspaces to control compilation.
* **Security & maintenance**: audit and update dependencies regularly.
* **Performance awareness**: disable unused features and leverage parallelism/SIMD.

Integrate these declarative strategies into your CI workflow to build reliable, maintainable and high‑performance Rust applications on nightly 1.91.  Nightly 1.91 inherits all the improvements of 1.90 (including the guaranteed capacity of `Vec::with_capacity` and safe `std::arch` intrinsics【684066008670463†L134-L140】) and adds incremental performance fixes, so keeping your toolchain up to date is recommended.
