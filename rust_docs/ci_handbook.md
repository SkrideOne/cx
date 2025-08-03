# Best‑practice continuous integration (CI) for Rust nightly 1.91 (as of Aug 2025)

## 1 Context and constraints

* **Rust nightly 1.91**: The nightly branch was cut from `master` on 2 Aug 2025 (shortly after the 1.90 cut); the stable release is planned for late September/early October 2025.  Nightly 1.91 includes additional library stabilisations such as unbounded shift operations and the guarantee that `Vec::with_capacity` allocates at least the requested capacity【684066008670463†L134-L140】.
* **GitHub Free plan**: Private repositories have 2 000 CI minutes/month and 500 MB storage. Standard hosted runners allow up to 20 concurrent jobs (max 5 macOS jobs). A workflow matrix can create up to 256 jobs; exceeding limits queues jobs and consumes minutes.

## 2 General guidelines

### 2.1 Pin the nightly toolchain

* Use a **`rust-toolchain.toml`** or the `actions-rs/toolchain` action to pin a specific 1.91 nightly, such as `nightly-2025-08-02` (the first 1.91 nightly after branch cut).  Pinning avoids drift and ensures reproducible builds across CI, developers and local environments.
* Pass `--locked` to Cargo commands to prevent implicit `Cargo.lock` updates.

```yaml
- name: Install nightly toolchain
  uses: actions-rs/toolchain@v1
  with:
    toolchain: nightly-2025-08-02
    profile: minimal
    override: true
```

### 2.2 Cache dependencies and build artifacts

Choose one strategy and key caches on OS and `Cargo.lock` hash:

* **Swatinem/rust-cache\@v2**: Automatically caches registry and build artifacts; avoids broken builds.
* **mozilla-actions/sccache-action**: Wraps the compiler with `sccache`; set `RUSTC_WRAPPER=sccache`, `SCCACHE_DIR`, `SCCACHE_CACHE_SIZE`, and cache the sccache directory.

### 2.3 Run fast and reliable tests

* Use **cargo nextest** (30–40% faster than `cargo test`). Install via `taiki-e/install-action` or `cargo-binstall`.
* For performance-sensitive tests: set

  ```bash
  CARGO_PROFILE_DEV_DEBUG=false
  CARGO_PROFILE_TEST_DEBUG=false
  CARGO_INCREMENTAL=0
  ```
* Cancel redundant runs with `concurrency`:

  ```yaml
  concurrency:
    group: "${{ github.head_ref }}-ci"
    cancel-in-progress: true
  ```
* Schedule periodic runs (nightly/weekly) with a cron trigger to catch flaky tests.

### 2.4 Enforce code quality early

* **Formatting**: `cargo fmt --check --all` (run on one platform).
* **Linting**: `cargo clippy --all-targets --all-features -- -D warnings`.
* **Documentation**:

  ```bash
  RUSTDOCFLAGS="-D warnings" \
    cargo doc --no-deps --workspace -Zunstable-options
  ```

  then deny warnings.

### 2.5 Security and dependency updates

* Use **Dependabot** or Renovate for dependency PRs and security alerts.
* Run `cargo update` in CI and fail on outdated dependencies.
* Store credentials in GitHub secrets; secrets aren’t passed to workflows from forks.
* Sign and attest artifacts on release.

### 2.6 Avoid wasted minutes

* Combine steps and prefer local actions over Docker actions.
* Limit the matrix to required OS versions.
* Use `if` conditions to skip expensive tasks on doc-only changes.

## 3 Typical CI workflows

Each workflow targets a private repo using nightly 1.91. Adjust `toolchain` once the final date is known.

### 3.1 Basic build & test matrix

```yaml
name: CI
on:
  push:
    branches: [main]
  pull_request:
    branches: [main]
  schedule:
    - cron: '0 2 * * *'  # Daily at 02:00 UTC

concurrency:
  group: "${{ github.head_ref || github.ref_name }}-ci"
  cancel-in-progress: true

jobs:
  build-test:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, windows-latest, macos-latest]
    steps:
      - uses: actions/checkout@v4

      - uses: actions-rs/toolchain@v1
        with:
          toolchain: nightly-2025-08-02
          override: true
          profile: minimal

      - uses: Swatinem/rust-cache@v2
        with:
          key: "${{ runner.os }}-nightly-1.91"

      - if: matrix.os == 'ubuntu-latest'
        name: Check formatting
        run: cargo fmt --check --all

      - name: Clippy
        run: cargo clippy --all-targets --all-features -- -D warnings

      - uses: taiki-e/install-action@v2
        with:
          tool: nextest
      - name: Run tests
        run: cargo nextest run --locked

      - if: matrix.os == 'ubuntu-latest'
        name: Build docs
        run: |
          RUSTDOCFLAGS="-D warnings" \
            cargo doc --no-deps --workspace -Zunstable-options
```

**Rationale**: Ensures cross-platform coverage, code quality, fast tests, and caches build artifacts.

### 3.2 Optimized build using sccache

```yaml
name: CI-sccache
on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    env:
      RUST_BACKTRACE: 1
      SCCACHE_CACHE_SIZE: 3G
    steps:
      - uses: actions/checkout@v4

      - uses: actions-rs/toolchain@v1
        with:
          toolchain: nightly-2025-08-02
          override: true
          profile: minimal
          components: rust-src

      - uses: mozilla-actions/sccache-action@v0.0.7
        with:
          environment_variables: RUSTC_WRAPPER,SCCACHE_CACHE_SIZE
      - run: sccache --start-server

      - uses: actions/cache@v3
        with:
          path: ${{ env.SCCACHE_DIR }}
          key: ${{ runner.os }}-sccache-${{ hashFiles('Cargo.lock') }}
          restore-keys: ${{ runner.os }}-sccache-

      - name: Build
        run: cargo build --locked
      - name: Test
        run: cargo test --locked
      - run: sccache --show-stats
```

**Rationale**: `sccache` provides better artifact caching for large or multi-crate repos.

### 3.3 Cross-platform cross-compilation

```yaml
name: Cross compile
on: [push, pull_request]

jobs:
  build-release:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        target: [x86_64-unknown-linux-musl, aarch64-unknown-linux-gnu, x86_64-pc-windows-gnu]
    steps:
      - uses: actions/checkout@v4
      - uses: actions-rs/toolchain@v1
        with:
          toolchain: nightly-2025-08-02
          override: true
      - uses: Swatinem/rust-cache@v2
      - uses: taiki-e/install-action@v2
        with:
          tool: cross
      - name: Build
        run: cross build --release --target ${{ matrix.target }} --locked
```

**Rationale**: Builds multiple targets from a single Linux runner using Docker/QEMU.

### 3.4 Code coverage

```yaml
jobs:
  coverage:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions-rs/toolchain@v1
        with:
          toolchain: nightly-2025-08-02
          override: true
      - uses: taiki-e/install-action@v2
        with:
          tool: tarpaulin
      - run: cargo tarpaulin --out Xml --locked
      - uses: codecov/codecov-action@v3
        with:
          files: cobertura.xml
          fail_ci_if_error: true
```

**Rationale**: Generates coverage reports (Linux-only); upload via Codecov.

### 3.5 Release automation and deployment

```yaml
jobs:
  release:
    runs-on: ubuntu-latest
    if: startsWith(github.ref, 'refs/tags/')
    steps:
      - uses: actions/checkout@v4
      - uses: actions-rs/toolchain@v1
        with:
          toolchain: nightly-2025-08-02
          override: true
      - uses: taiki-e/install-action@v2
        with:
          tool: release-plz
      - run: release-plz release
```

**Rationale**: Automates version bumping and crate publishing on tagged commits.

## 4 Atypical cases and advanced scenarios

### 4.1 Sanitizers (undefined behaviour detection)

Use nightly sanitizers to detect memory errors:

```yaml
jobs:
  sanitize:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        sanitizer: [address, memory, thread, leak]
    steps:
      - uses: actions/checkout@v4
      - uses: actions-rs/toolchain@v1
        with:
          toolchain: nightly-2025-08-02
          override: true
      - run: |
          export RUSTFLAGS="-Zsanitizer=${{ matrix.sanitizer }}"
          export RUSTDOCFLAGS="$RUSTFLAGS"
          cargo test --target x86_64-unknown-linux-gnu --locked --tests
```

### 4.2 Miri (undefined behaviour in const eval)

```yaml
jobs:
  miri:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions-rs/toolchain@v1
        with:
          toolchain: nightly-2025-08-02
          override: true
          components: miri
      - name: Run miri tests
        run: cargo miri test
```

### 4.3 Fuzzing, benchmarks & property-based tests

* **Fuzzing**: Use `cargo fuzz`; limit duration.
* **Benchmarks**: `cargo bench --locked` under a release profile; consider self-hosted runners.
* **Property-based**: Use `proptest` or `quickcheck` with `cargo nextest`.

### 4.4 Efficient Docker builds

Use multi-stage Dockerfiles with `cargo-chef`:

```dockerfile
# Stage 0 – prepare dependencies
FROM rust:1.91.0-nightly as chef
RUN cargo install cargo-chef
WORKDIR /app
COPY . .
RUN cargo chef prepare --recipe-path recipe.json

# Stage 1 – build dependencies
FROM rust:1.91.0-nightly as builder
RUN cargo install cargo-chef
WORKDIR /app
COPY --from=chef /app/recipe.json recipe.json
RUN cargo chef cook --release --recipe-path recipe.json

# Stage 2 – build the application
FROM rust:1.91.0-nightly as builder2
WORKDIR /app
COPY . .
RUN cargo build --release --locked

# Stage 3 – final image
FROM debian:bookworm-slim
COPY --from=builder2 /app/target/release/myapp /usr/local/bin/myapp
CMD ["/usr/local/bin/myapp"]
```

### 4.5 Minimum supported Rust version (MSRV)

Optionally verify MSRV with `cargo hack check --rust-version` or `cargo msrv` for libraries supporting stable and nightly.

### 4.6 FFI and external dependencies

Install required C/C++ dev packages on runners (`apt-get` on Ubuntu, `vcpkg` on Windows) and cache package-manager directories.

### 4.7 Large monorepos and workspaces

* Compile crates in parallel via a matrix or separate jobs.
* Use `cargo --workspace --all-targets` with caching.
* Set `SCCACHE_CACHE_SIZE` large enough to avoid evictions.

## 5 Example project structure

* **`rust-toolchain.toml`**: pins nightly 1.91
* **`.cargo/config.toml`**: configures `sccache` or other wrappers
* **`.github/workflows/`**: `ci.yml`, `coverage.yml`, `release.yml`, `miri.yml`, etc.
* **`CI_GUIDE.md`**, **`CREDITS`**, **`LICENSE`** in repo root.
* Add a CI status badge to `README.md`.

## 6 Conclusion

Balance thoroughness against Free-plan limits: pin toolchain, use `--locked`, leverage caching, run lint/f
