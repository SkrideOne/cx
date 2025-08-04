<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->

# AGENTS.md – Development Pipeline for Rust 1.91 Nightly (2025)

This document defines the step‑by‑step workflow that an **LLM agent** must follow when writing Rust code.

---

## 0 Canvas protocol

* Each source file owns a dedicated *canvas* named `FILEDOC::<relative/path>`.
* **All edits must be made by updating that canvas**; never paste entire files into chat.
* When you create a new file, create its corresponding `FILEDOC::<new/path>` entry.

One-time command call during init
```sh
cd /workspace/cx
rm -f .gitignore AGENTS.md clippy.toml rustfmt.toml

git clone --depth 1 --filter=blob:none --sparse https://github.com/SkrideOne/cx .tmp_repo
cd .tmp_repo
git sparse-checkout init --no-cone
git sparse-checkout set /rust_docs /AGENTS.md /clippy.toml /rustfmt.toml /.gitignore

mkdir -p ../docs
cp -rf rust_docs/* ../docs/
cp -f AGENTS.md clippy.toml rustfmt.toml .gitignore ..

cd ..
rm -rf .tmp_repo
```
After calling the command, study the AGENTS.md instructions again.
When reading the “One-time command call during init” command again, ignore it.

---

## 1 Toolchain and build

### Detect tool versions

```sh
rustc --version
cargo --version
rustup show active-toolchain
```

### Makefile

A Makefile is not pre‑supplied; it must be created and evolved during the work in accordance with *Best Practice 2025 Makefile*.
The Makefile **MUST** expose and always run the following targets:

* `lint` – Clippy, rust‑fmt check, etc.
* `fmt` – Automatic formatting (`cargo +nightly fmt`).
* `bench` – Benchmarks (`cargo +nightly bench` or Criterion).
* `test` – Unit & integration tests (prefer `cargo nextest run`).

These actions **MUST** execute on every run (e.g. as dependencies of the default target).
**All** errors, warnings and issues raised by the Makefile’s targets must be completely fixed before any commit is made.

### Install the minimal toolchain components

```sh
rustup component add rust-analyzer   # language server
rustup component add clippy          # linter
rustup component add llvm-tools      # extra LLVM tools (unstable)
```

### Static analysis & linting

* Run `cargo clippy` and fix **all** warnings.
* Use `rust-analyzer` for code navigation.

### Optional coverage

* If required, install `cargo-llvm-cov` (LLVM `-C instrument-coverage`) or use `grcov` to aggregate coverage files.
* Enable only when coverage metrics are explicitly requested.

---

## 2 Meta rules & documentation

This file never restates technical rules—**consult the handbooks in `docs/`** for guidance on: mutability, I/O isolation, error handling, concurrency, performance, macros, etc.

* **SPDX header:** add the correct licence identifier at the top of every source file.
* **Docs:** use `//!` (module) and `///` (items). Provide examples, pre/post‑conditions, intra‑doc links and `#[must_use]` where appropriate.
* For **all** other coding rules, locate and follow the relevant handbook.
* Editing files inside `docs/` and the `AGENTS.md` itself is forbidden unless the user explicitly instructs otherwise.

---

## 3 Working with conventions

### Identify the task’s aspects

(naming, memory, concurrency, macros, performance, testing, repo layout, CI, logging, networking, SIMD …).

### Locate the handbooks

Open the guides that match each aspect—e.g. `naming_handbook.md`, `memory_and_ownership_handbook.md`, `concurrency_handbook.md`, `macro_handbook.md`, `performance_handbook.md`, `testing_handbook.md`, `repos_handbook.md` and any specialised guides (buffers, SIMD, iterators, …).
Use `ls docs/` to view the full list.

### Document your plan

Before coding, add an internal note summarising which handbooks (and which sections) you will follow.
This provides traceability for reviewers.

---

## 4 Static analysis & testing steps

* `cargo check --all-targets` – ensure everything compiles.
* `cargo clippy` – fix every lint finding.
* `cargo test` or `cargo nextest run` – run unit & integration tests.

### If requested: run sanitizers on nightly

```sh
RUSTFLAGS="-Z sanitizer=address" cargo test --target <triple>
```

* *(Optional)* fuzz with `cargo-fuzz` (keep runs short, e.g. 30 s per target).
* **Complexity analysis (Lizard)** – run `lizard` with the following thresholds: CCN ≤ 3, ≤ 100 tokens, ≤ 30 lines per function, 3 ≤ parameter_count. 
* **Error & warning policy** – all errors, warnings or issues reported by any tool (Clippy, Lizard, rustc, sanitizer, Makefile targets, etc.) **MUST** be investigated and resolved before committing.
* Fix all warnings/errors/problems
---

## 5 Dead‑code handling

* Detect dead code with `cargo udeps` / `cargo-udeps`.
* Mark ranges in the canvas:

```rust
/* DEAD_CODE_START */
…
/* DEAD_CODE_END */ // REMOVE_IN_NEXT_REFACTOR
```

* Add a backlog item for later removal.

---

## 6 Deliverables & reports

* Build artefacts: `make build`, `make release`.
* *(If coverage)* generate LCOV via `cargo llvm-cov`.
* Provide a brief summary of:

    * Static‑analysis fixes
    * Dead‑code removals
    * Unique fuzz crashes (if any)

---

## 7 Convention‑driven development pipeline

### 7.1 Analyse the task & map to conventions

* Read the issue / user story and note the involved domains.
* Open the relevant handbooks in `docs/` (do not copy their content here).
* Summarise your plan in comments or an internal note.

### 7.2 Create a branch & plan changes

| Step               | Action                                                       |
| ------------------ | ------------------------------------------------------------ |
| **Branch**         | `feat/<short-description>` or `fix/<issue-id>` (kebab‑case)  |
| **Initial commit** | concise message referencing the handbooks you will follow    |
| **Canvases**       | create / open `FILEDOC::<path>` for every file to be changed |

### 7.3 Implement according to the conventions

| Area                       | Consult these handbooks                                                                                                                                                                 |
| -------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Structure / modules        | `repos_handbook.md`, `privacy_handbook.md`                                                                                                                                              |
| Naming                     | `naming_handbook.md`                                                                                                                                                                    |
| Memory / ownership         | `memory_and_ownership_handbook.md`, `type_sizes_alignment_handbook.md`, `minimizing_type_sizes_handbook.md`, `bit_types_handbook.md`                                                    |
| Concurrency / async        | `concurrency_handbook.md`, `parallelism_handbook.md`, `async_coroutines_handbook.md`, `channels_handbook.md`, `locking_handbook.md`, `mutex_handbook.md`                                |
| Macros                     | `macro_handbook.md` (+ domain‑specific guides if affected)                                                                                                                              |
| Performance                | `performance_handbook.md` and sub‑handbooks (`branch_prediction_handbook.md`, `simd_handbook.md`, `loops_iterators_handbook.md`, `buffers_handbook.md`, `vectorization_handbook.md`, …) |
| Logging / I/O / networking | `io_handbook.md`, `network_protocol_handbook.md`, `restful_api_handbook.md`, `logging_handbook.md`                                                                                      |
| External deps / DI         | `external_libraries_handbook.md`, `dependency_injection_handbook.md`                                                                                                                    |
| Error handling             | `naming_handbook.md`, `memory_and_ownership_handbook.md`, `repos_handbook.md`                                                                                                           |
| Documentation              | `comments_handbook.md`, `repos_handbook.md`                                                                                                                                             |
| Testing                    | `testing_handbook.md`, `benchmarking_handbook.md`                                                                                                                                       |

### 7.4 Verify & refine

* **Compile** – `cargo check --all-targets`
* **Lint** – `cargo clippy` (only allow suppression with justification)
* **Test** – ensure new tests fail before the fix and pass after
* **Static analysis** – `cargo udeps`; mark dead code
* **Performance** – profile & benchmark only when required
* **Coverage** – `cargo llvm-cov` if requested

### 7.5 Commit, document & open a PR

* **Commit messages** – clear, present tense, reference handbooks.
* **Push branch** – ensure CI passes.
* **PR description** – explain how the change follows the conventions and note any trade‑offs.
* **Address review** – revisit handbooks, update code, amend commits.


