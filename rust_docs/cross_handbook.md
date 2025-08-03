# Best-Practice 2025 methodology for building fully cross-platform Rust applications (nightly 1.90)

## 1 Overview

Rust’s toolchain and ecosystem have matured quickly, making it possible to deliver the same codebase across desktops, mobile platforms and even embedded devices. Rust nightly 1.90 sits near the 2024–2025 boundary, so this guide focuses on techniques that remain valid going into 2025. Best-practice recommendations below prioritize performance, quality, safety and maintainability. Typical cases (Linux, Windows, macOS, mobile, embedded and WebAssembly) are treated separately. Non-typical cases (unusual architectures, mixed-language FFI and vendor-specific toolchains) are covered in a dedicated section.

---

## 2 Toolchain foundation

### 2.1 Use `rustup` to manage toolchains and targets

Install Rust nightly 1.90 via **rustup**, then use `rustup toolchain install` to add nightly components. Rustup automatically manages compiler, Cargo and standard-library versions.

Add targets through `rustup target add <triple>`. (The Rustup book notes that only the host standard library is installed by default, so additional targets must be added manually before cross-compiling.)

Cross-compile by passing `--target=<triple>` to Cargo. Examples include `x86_64-unknown-linux-gnu`, `x86_64-apple-darwin`, `aarch64-linux-android`, `aarch64-apple-ios`, `thumbv7m-none-eabi`, `wasm32-unknown-unknown`, etc. For targets that require external linkers (Android, embedded, etc.), install the appropriate toolchains (Android NDK, arm-gcc).

### 2.2 Pin toolchain for reproducible builds

Create a **`rust-toolchain.toml`** file specifying the channel (`nightly`), version (`1.90.0`) and components (e.g. `clippy`, `rustfmt`). Include all target triples used in the project. A per-project `rust-toolchain.toml` ensures that every developer and CI runner uses the same toolchain and prevents breaking changes.

### 2.3 Cargo configuration for cross-compiling

Create **`.cargo/config.toml`** in the repository root. Define per-target settings such as linkers, runners and rustflags using `[target.<triple>]` or `[target.<cfg>]` tables, e.g.:

```toml
[target.x86_64-unknown-linux-gnu]
linker = "clang"

[target.aarch64-linux-android]
linker  = "aarch64-linux-android21-clang"
runner  = "qemu-aarch64"
rustflags = ["-C", "link-arg=-Wl,--build-id"]
```

Keep environment-specific settings (e.g. an `sccache` wrapper) **within the repository**, not in the global `~/.cargo/config.toml`, because global settings like `rustc-wrapper="sccache"` may break cross-compilation when building inside *cross* containers.

### 2.4 Cargo profiles and performance tuning

Define separate profiles for *dev*, *release* and any custom configurations in **`Cargo.toml`** using `[profile.<name>]` sections.

| Setting                     | Purpose and best practice (2025)                                                                                                        | Recommendation                                                                                                                                   |
| --------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| `opt-level`                 | Controls compiler optimisation. Levels 1–3 trade compile time for runtime performance. Options `s`/`z` optimise for binary size.        | For production builds use `opt-level = 3` or `s` if size matters; for dev builds use lower levels to improve compile speed.                      |
| `strip`                     | Strips symbols (`symbols`) or debug info (`debuginfo`) from binaries.                                                                   | Use `debuginfo` to remove debug sections in release packages but keep minimal symbols for backtraces.                                            |
| `lto`                       | Enables link-time optimisation (`true`, `thin`, `fat`). LTO can significantly improve runtime performance at the cost of longer builds. | Enable **Thin LTO** for release on mainstream targets. For resource-constrained embedded devices, evaluate **Fat LTO** for maximum optimisation. |
| `debug` / `overflow-checks` | Control debug info and integer-overflow checking.                                                                                       | Keep overflow checks **disabled** in release builds for performance; enable them in dev/test.                                                    |

#### Profile-guided optimisation (PGO)

1. Compile **instrumented** binaries using `rustc -Cprofile-generate=/path/to/data` or `RUSTFLAGS="-Cprofile-generate=/path/to/data"` with `cargo build --release --target=<triple>`.
2. Run the instrumented executable with representative workloads to generate `.profraw` files, then merge them into a `.profdata` using `llvm-profdata merge`.
3. Re-compile with `-Cprofile-use=/path/to/merged.profdata` and `--release` to improve inlining and register allocation.
4. Clean up leftover `.profraw` data between builds.

PGO yields notable gains for compute-heavy workloads; always measure throughput or energy usage before and after applying PGO.

---

## 3 Typical cross-platform cases

### 3.1 Linux, Windows and macOS (desktop/server)

#### Steps

1. **Install target triples** (`x86_64-unknown-linux-gnu`, `x86_64-apple-darwin`, `x86_64-pc-windows-msvc`, etc.) via `rustup target add`.
2. **Build** with `cargo build --release --target=<triple>`. When cross-compiling:

    * From Linux to Windows, use clang/mingw-w64 cross-linkers.
    * From Linux to macOS, use **osxcross** with the Xcode SDK and configure `CC`/`AR` in `.cargo/config.toml`.
3. **Conditional compilation**: use `#[cfg(target_os = "windows")]`, `#[cfg(target_arch = "aarch64")]`, etc., plus `cfg_attr` to apply attributes conditionally.
4. **Continuous integration**: use the `cross` tool to build & test across architectures (`cross test --target=aarch64-unknown-linux-gnu`). Because QEMU emulation is slow, limit integration tests on emulated targets.
5. **Package & distribute**:

    * **`cargo-bundle`** – generates `.app`, `.deb`, `.msi` installers; configure via `[package.metadata.bundle]`.
    * **Tauri** – thin native wrappers using system WebViews; targets Windows/macOS/Linux/iOS/Android.
    * **Other GUI toolkits** – e.g. Slint (declarative, < 300 KiB runtime), Iced (Elm-inspired), Dioxus (web/desktop/mobile).

#### Performance & quality tips

* Build with the *release* profile plus LTO and, where beneficial, PGO.
* Use asynchronous runtimes (e.g. Tokio) for I/O-bound tasks; use `std::thread` or thread-pools for CPU-bound work.
* Keep core logic platform-agnostic; place OS abstractions behind traits.
* Isolate FFI (e.g. calls to C system APIs on Windows) in dedicated modules, ensuring correct memory management and error handling.

---

### 3.2 Mobile platforms (Android & iOS)

#### Steps

1. **Install mobile targets** (`aarch64-linux-android`, `armv7-linux-androideabi`, `x86_64-linux-android`, `aarch64-apple-ios`, `x86_64-apple-ios`).
2. **Configure Cargo & CMake**:

    * **Android** – set `CC` to the NDK clang (`aarch64-linux-android21-clang`); specify `ar` and linker flags in `.cargo/config.toml`.
    * **iOS** – on non-Mac hosts, use **osxcross**; on macOS, build with Xcode toolchain and `-target aarch64-apple-ios` flags. Provide a CMake toolchain for any C/C++ deps.
3. **Bindings** – expose Rust via JNI (Android) or Swift/Obj-C (iOS) using crates such as `jni`, `swift-bridge`, or `cxx`.
4. **Testing** – run `cross test --target=aarch64-linux-android` or unit tests in emulators; on iOS, run tests on simulators via Xcode.
5. **Packaging** –

    * Hybrid apps: **Tauri 2.0** provides mobile wrappers and store distribution.
    * Native UIs: link Rust as a static or dynamic library in Android Studio/Xcode projects.

#### Performance & quality tips

* Use `strip = "symbols"` in `profile.release` to shrink APK/IPA size.
* On Android, minimise heap allocations across JNI boundaries; pass Rust-owned pointers as `jlong` handles.
* On iOS, compile with `-Cembed-bitcode` if required by current App Store policy.
* Ensure callbacks touching the UI run on the main thread.

---

### 3.3 Embedded and bare-metal (microcontrollers)

#### Steps

1. **Choose the correct target triple** (e.g. `thumbv7m-none-eabi`, `thumbv7em-none-eabihf`, `riscv32imac-unknown-none-elf`) and add via Rustup.
2. Install the appropriate cross-compiler (e.g. `arm-none-eabi-gcc`) and set `runner = "arm-none-eabi-gdb"` or a flashing tool in `.cargo/config.toml`.
3. Use an embedded **HAL** (`cortex-m`, `embedded-hal`, etc.) and add `panic-halt` or `panic-probe` for panic handling.
4. Compile with `#![no_std]`; provide an `#[entry]` via `cortex-m-rt`.
5. For concurrency, use RTIC or Embassy (async executor for embedded).

#### Performance & quality tips

* Use `opt-level = "z"` or `"s"` plus `lto = true` to minimise code size.
* Set `panic = "abort"` to remove formatting code.
* Use **defmt** for efficient logging; debug with semihosting or SWD/JTAG.
* Prototype with QEMU before flashing hardware; profile with `cargo-binutils` or PGO only when necessary.

---

### 3.4 WebAssembly (`wasm32`)

#### Steps

1. Add the `wasm32-unknown-unknown` target via Rustup and build with:

   ```bash
   cargo build --target wasm32-unknown-unknown --release
   ```
2. Use **wasm-bindgen** to generate JavaScript bindings; integrate via Webpack, Vite, etc.
3. For serverless/WASI, target **`wasm32-wasi`** and interact via POSIX-like syscalls.
4. Build full web UIs with frameworks such as **Yew**, **Leptos** or **Dioxus**.
5. Post-process with **wasm-opt** (Binaryen) and enable `opt-level = "z"`, `lto = true` to shrink binaries.

#### Performance & quality tips

* Minimise JS↔Wasm boundary crossings; batch operations.
* Export only necessary functions with `#[wasm_bindgen]`.
* Use `wasm_bindgen_futures` to bridge Rust `async` with JavaScript `Promise`s.

---

## 4 Non-typical scenarios and advanced topics

### 4.1 Unusual architectures and OSes

* Check Rust **platform-support tiers** for targets such as PowerPC, MIPS, SPARC, FreeBSD, QNX, etc.
* Install the target with Rustup and verify `rust-std` availability.
* Install the matching cross-compiler (e.g. `powerpc64le-linux-gnu-gcc`) and configure linker/runner in `.cargo/config.toml`.
* For musl/uclibc, add `-Ctarget-feature=+crt-static` to `rustflags`.
* For libc-less OSes, compile with `-Z build-std` (nightly), and supply custom `panic_handler` & `lang_items`.

### 4.2 Mixed-language and FFI integration

* Mark FFI structs with `#[repr(C)]` and encapsulate `unsafe` code.
* Use brid
