<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->

# AGENTS.md – Development Pipeline for Rust 1.90 Nightly (2025)

This document defines the step‑by‑step workflow that an **LLM agent** must follow when writing Rust code.

---

## 0 Canvas protocol

* Each source file owns a dedicated *canvas* named `FILEDOC::<relative/path>`.
* **All edits must be made by updating that canvas**; never paste entire files into chat.
* When you create a new file, create its corresponding `FILEDOC::<new/path>` entry.

---

## 1 Toolchain and build

1. **Detect tool versions**

   ```sh
   rustc --version
   cargo --version
   ```

2. **Makefile**

   A Makefile is not pre‑supplied; it must be created and evolved during the work in accordance with *Best Practice 2025 Makefile*.
   The Makefile **MUST** expose and always run the following targets:

    * `lint`   – Clippy, cargo‑deny, rust‑fmt check, etc.
    * `fmt`    – Automatic formatting (`cargo fmt`).
    * `bench`  – Benchmarks (`cargo bench` or Criterion).
    * `test`   – Unit & integration tests (prefer `cargo nextest run`).

   These actions **MUST** execute on every run (e.g. as dependencies of the default target).
   **All** errors, warnings and issues raised by the Makefile’s targets must be completely fixed before any commit is made.

3. **Install the minimal toolchain components**

   ```sh
   rustup component add rust-analyzer   # language server
   rustup component add clippy          # linter
   rustup component add llvm-tools      # extra LLVM tools (unstable)
   ```

4. **Static analysis & linting**

    * Run `cargo clippy` and fix **all** warnings.
    * Use `rust-analyzer` for code navigation.

5. **Optional coverage**

    * If required, install **`cargo-llvm-cov`** (LLVM `-C instrument-coverage`) or use **`grcov`** to aggregate coverage files.
    * Enable only when coverage metrics are explicitly requested.

---

## 2 Meta rules & documentation

* This file never restates technical rules—**consult the handbooks in `docs/`** for guidance on:
  mutability, I/O isolation, error handling, concurrency, performance, macros, etc.
* **SPDX header:** add the correct licence identifier at the top of every source file.
* **Docs:** use `//!` (module) and `///` (items). Provide examples, pre/post‑conditions, intra‑doc links and `#[must_use]` where appropriate.
* For *all* other coding rules, locate and follow the relevant handbook.
* Editing files inside `docs/` and the `AGENTS.md` itself is forbidden unless the user explicitly instructs otherwise.

---

## 3 Working with conventions

1. **Identify the task’s aspects**
   (naming, memory, concurrency, macros, performance, testing, repo layout, CI, logging, networking, SIMD …).

2. **Locate the handbooks**
   *Open the guides that match each aspect*—e.g.
   `naming_handbook.md`, `memory_and_ownership_handbook.md`, `concurrency_handbook.md`, `macro_handbook.md`, `perfomance_handbook.md`, `testing_handbook.md`, `repos_handbook.md` and any specialised guides (buffers, SIMD, iterators, …).
   Use `ls docs/` to view the full list.

3. **Document your plan**
   Before coding, add an internal note summarising which handbooks (and which sections) you will follow.
   This provides traceability for reviewers.

---

## 4 Static analysis & testing steps

1. `cargo check --all-targets` – ensure everything compiles.

2. `cargo clippy` – fix every lint finding.

3. `cargo test` **or** `cargo nextest run` – run unit & integration tests.

4. *(If requested)* run sanitizers on nightly:

   ```sh
   RUSTFLAGS="-Z sanitizer=address" cargo test --target <triple>
   ```

5. *(Optional)* fuzz with **`cargo-fuzz`** (keep runs short, e.g. 30 s per target).

6. **Complexity analysis (Lizard)** – run `lizard` with the following thresholds: CCN ≤ 3, ≤ 250 tokens and ≤ 30 lines per function.

7. **Error & warning policy** – all errors, warnings or issues reported by any tool (Clippy, Lizard, cargo‑deny, rustc, sanitizer, Makefile targets, etc.) **MUST** be investigated and resolved *before* committing.

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

1. **Read the issue / user story** and note the involved domains.
2. **Open the relevant handbooks** in `docs/` (do **not** copy their content here).
3. **Summarise your plan** in comments or an internal note.

### 7.2 Create a branch & plan changes

| Step               | Action                                                       |
| ------------------ | ------------------------------------------------------------ |
| **Branch**         | `feat/<short-description>` or `fix/<issue-id>` (kebab‑case)  |
| **Initial commit** | concise message referencing the handbooks you will follow    |
| **Canvases**       | create / open `FILEDOC::<path>` for every file to be changed |

### 7.3 Implement according to the conventions

| Area                       | Consult these handbooks                                                                                                                                  |
| -------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Structure / modules        | `repos_handbook.md`, `privacy_handbook.md`                                                                                                               |
| Naming                     | `naming_handbook.md`                                                                                                                                     |
| Memory / ownership         | `memory_and_ownership_handbook.md`, `type_sizes_alignment_handbook.md`, `minimising_type_sizes_handbook.md`, `bit-types_handbook.md`                     |
| Concurrency / async        | `concurrency_handbook.md`, `parallelism_handbook.md`, `async_coroutines_handbook.md`, `channels_handbook.md`, `locking_handbook.md`, `mutex_handbook.md` |
| Macros                     | `macro_handbook.md` (+ domain‑specific guides if affected)                                                                                               |
| Performance                | `perfomance_handbook.md` and sub‑handbooks (branch prediction, SIMD, iterators, buffers, …)                                                              |
| Logging / I/O / networking | `io_handbook.md`, `network_protocol_handbook.md`, `RESTfull-api_handbook.md`, `logging_handbook.md`                                                      |
| External deps / DI         | `external-libraries_handbook.md`, `dependency-injection_handbook.md`                                                                                     |
| Error handling             | `naming_handbook.md`, `memory_and_ownership_handbook.md`, `repos_handbook.md`                                                                            |
| Documentation              | `comments_handbook.md`, `repos_handbook.md`                                                                                                              |
| Testing                    | `testing_handbook.md`, `benchmarking_handbook.md`                                                                                                        |

### 7.4 Verify & refine

* **Compile** – `cargo check --all-targets`
* **Lint** – `cargo clippy` (only allow suppression with justification)
* **Test** – ensure new tests fail *before* the fix and pass *after*
* **Static analysis** – `cargo udeps`; mark dead code
* **Performance** – profile & benchmark only when required
* **Coverage** – `cargo llvm-cov` if requested

### 7.5 Commit, document & open a PR

1. **Commit messages** – clear, present tense, reference handbooks.
2. **Push branch** – ensure CI passes.
3. **PR description** – explain how the change follows the conventions and note any trade‑offs.
4. **Address review** – revisit handbooks, update code, amend commits.

---

By following this **strict pipeline** every change will comply with the project’s conventions, remain well‑tested and integrate cleanly—making even a weak LLM reliable and effective.
