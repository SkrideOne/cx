# Avoiding Unnecessary Allocations in Rust 1.91 (Nightly) – Best‑Practice Manual 2025

**Date:** 02 Aug 2025 (Europe/Berlin)

Rust 1.91 (nightly) is slated to become stable in autumn 2025; this manual targets nightly 1.91 and emphasises practices consistent with *Best Practice 2025* for quality and performance.  The 1.91 nightly retains the unstable `vec_push_within_capacity` feature and continues to refine allocation guarantees—for example, `Vec::with_capacity` now guarantees it allocates at least the requested capacity【684066008670463†L134-L140】.

---

## Why reducing allocations matters

Heap allocations involve global locks, metadata updates and sometimes system calls; reducing the number of allocations often yields measurable speed‑ups. Even small reductions—e.g. ten fewer allocations per million instructions—can improve run‑time by ≈ 1 %. Therefore, **profile first** with tools such as **DHAT** or **dhat‑rs**, and optimise allocation patterns only when profiling shows they are a bottleneck.

---

## Typical cases and best‑practice guidelines

### 1  Vectors (`Vec<T>`) and `String`

| Situation                                              | Best‑practice guideline                                                                                                                                                                                                                                                                     |
| ------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Growing a vector whose size is known approximately** | Pre‑allocate capacity with `Vec::with_capacity(n)` or `reserve` / `reserve_exact` before pushing elements. Without preallocation, pushing *n* items into an empty vector triggers several reallocations (e.g. adding 20 items performs four allocations: capacity doubles 4 → 8 → 16 → 32). |
| **Reading lines or building strings repeatedly**       | Avoid `BufRead::lines()` (creates a new `String` for each line). Use a reusable `String` with `BufRead::read_line()` and call `.clear()` after processing. Likewise, reuse a workhorse `Vec` inside loops and call `.clear()`.                                                              |
| **Building a `String` of known size**                  | Use `String::with_capacity()` to pre‑allocate (backed by `Vec<u8>`).                                                                                                                                                                                                                        |
| **Avoiding `format!` in hot paths**                    | `format!` allocates a new `String`. Prefer string literals, `write!` into a reusable buffer or `std::format_args!` / `lazy_format` for zero‑allocation formatting.                                                                                                                          |
| **Cloning collections**                                | `.clone()` on `Vec` or `String` allocates. If overwriting an existing vector, use `clone_from(&source)` to reuse the allocation.                                                                                                                                                            |
| **Converting borrowed to owned data**                  | Avoid unnecessary `to_owned`, `to_string` or `clone`; store references with lifetimes or use `std::borrow::Cow`. `Cow<'_, str>` allocates only when ownership is required.                                                                                                                  |
| **Extending a vector**                                 | Use `extend_from_slice`, `append` or `extend` rather than repeated `push`.                                                                                                                                                                                                                  |
| **Using `push` when capacity is critical**             | On nightly 1.91, activate `#![feature(vec_push_within_capacity)]` and call `push_within_capacity(item)`—it appends only if spare capacity exists, returning `Err(item)` rather than reallocating. Combine with `reserve` to control growth precisely.  Note that `Vec::with_capacity` now guarantees at least the requested capacity【684066008670463†L134-L140】, so you can better predict when `push_within_capacity` will succeed.                                       |
| **Shrinking capacity after oversizing**                | `Vec::shrink_to_fit()` trims capacity to length. Use after large build phases to free memory, but remember it reallocates.                                                                                                                                                                  |
| **Hash maps / sets**                                   | Use `HashMap::with_capacity(cap)` and `HashSet::with_capacity(cap)` to pre‑allocate buckets and avoid rehashing.                                                                                                                                                                            |

---

### 2  Borrowing vs. owning and zero‑copy

* **Borrow whenever possible** rather than cloning owned data. Cloning `String`, `Vec`, `Arc`/`Rc` allocates.
* **`Cow<'a, T>` (clone‑on‑write)** can hold borrowed or owned data; allocation occurs only on mutation.
* **Small‑string types**

    * **`smol_str::SmolStr`** – immutable, stores ≤ 22 bytes inline.
    * **`smartstring::SmartString<LazyCompact>`** – mutable, stores ≤ 23 bytes inline.
    * These drastically cut allocation counts for workloads heavy in short strings but add branch overhead for stack/heap switching—use only when profiling supports it.

---

### 3  Stack‑allocated vectors and arrays

* **`smallvec::SmallVec<[T; N]>`** – inline up to *N* elements, heap after; incurs a branch on `push`.
* **`arrayvec::ArrayVec<T, N>`** – fixed maximum length, **never** allocates; panics (or errs) on overflow.
* **`tinyvec::TinyVec<[T; N]>`** – similar to `SmallVec` but avoids `unsafe`; requires `T: Default`.

These types may be slower than `Vec` for large sizes but excel where most collections are small.

---

### 4  Reusing collections

* Allocate a vector/string **once outside a loop** and call `.clear()` each iteration.
* Use `Vec::retain` or `Vec::drain` to mutate in place rather than creating new vectors.

---

### 5  Reading and writing efficiently

* Pass **mutable buffer references** to serializers/deserializers and reuse them.
* Write directly to an `io::Write` implementer (`Vec<u8>`, `File`, `BufWriter`) instead of building large temporary strings.

---

### 6  Alternative allocators for the whole program

| Allocator                            | Use case & configuration                                                                                                                                                                                                                      | Notes                                                                                            |
| ------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| **jemalloc** via `tikv-jemallocator` | Add `tikv-jemallocator = "0.5"`; declare:<br>`#[global_allocator] static GLOBAL: tikv_jemallocator::Jemalloc = tikv_jemallocator::Jemalloc;`<br>On Linux set `MALLOC_CONF="thp:always,metadata_thp:always"` to enable transparent huge pages. | Reduces fragmentation & improves multithreaded allocation; increases binary size & compile time. |
| **mimalloc**                         | Add `mimalloc = "0.1"`; declare:<br>`#[global_allocator] static GLOBAL: mimalloc::MiMalloc = mimalloc::MiMalloc;`                                                                                                                             | Good multi‑threaded performance; larger binaries.                                                |
| **wee\_alloc**                       | Tiny allocator for WebAssembly; shrinks code size.                                                                                                                                                                                            | Limited desktop gains.                                                                           |

---

### 7  Custom allocators and bump allocation (nightly features)

* Nightly 1.91 exposes `#![feature(allocator_api)]` so collections can take a custom allocator parameter.
* **Local bump allocator** – implement a simple bump arena and use `Vec::<T, &BumpAllocator>::with_capacity_in`.
* **Global bump allocator** – declare with `#[global_allocator]`; risky: never frees memory.
* **`bumpalo` crate** – safe scoped bump arena (`Bump::new()`); memory freed on drop.
* Custom allocators offer isolation & predictable frees but complicate code and skip destructors—use sparingly.

---

### 8  Compile‑time and static preallocation (advanced)

Allocate large buffers in `static` arrays and wrap them safely; eliminates runtime allocations but requires meticulous `unsafe` to avoid races/unsoundness.

---

### 9  Monitoring and testing allocation regressions

* Use **`dhat‑rs`** in tests to assert heap‑allocation counts.
* Combine with **Criterion** or **cargo bench**; consider **bencher** or **iai** for assembly‑level metrics.

---

## Atypical cases and strategies

### A. Deserialization & parsing large data sets

* **Stream & reuse buffers** – `serde_json::Deserializer::from_reader`, `csv::Reader`.
* Avoid `Vec::collect` on huge iterators; push into pre‑reserved `Vec`.
* Use `serde` with `Cow<'a, str>` to avoid string clones.

### B. Real‑time & embedded systems

* Prefer **stack allocation** and fixed‑size buffers (`[T; N]`, `ArrayVec`, `heapless::Vec`).
* Set `panic = "abort"`; disable unwinding.
* Use `no_std` crates (`heapless`, `embedded‑alloc`).
* Custom global allocators tuned for small/static memory pools.

### C. High‑throughput servers & multi‑threaded workloads

* Switch to **jemalloc** or **mimalloc**.
* Use object pools / thread‑local caches (`pool`, `deadpool`, `object_pool`, `typed_arena`).
* **Batch allocation** – allocate large buffers and slice.

### D. Data‑oriented design & cache locality

* **Structure of arrays (SoA)** – one vector per field.
* Use `SmallVec` inside structs for small sequences; keep inline capacity minimal.
* Consider `VecDeque` for queue workloads; reserve capacity to avoid reallocations.

### E. Self‑referential or pin‑in‑place structures

* Allocate contiguous blocks, reference with `Pin<Box<_>>`; encapsulate `unsafe`.

### F. Working with FFI or low‑level buffers

* Use `Vec::into_raw_parts` / `Vec::from_raw_parts` to transfer ownership without copy.
* Wrap in `ManuallyDrop` to avoid double free.
* Ensure alignment & lifetime expectations are honoured.

---

## Conclusion

Avoiding unnecessary allocations in Rust 1.91 combines foresight (pre‑allocating when sizes are predictable), **buffer reuse**, and judicious data‑structure choices.  Simple steps like `Vec::with_capacity`—now guaranteed to allocate the requested capacity【684066008670463†L134-L140】—and buffer reuse eliminate most allocations; advanced techniques (small‑vector types, `Cow`, bump allocators) provide further gains when profiling justifies them.  Alternative allocators improve throughput for multithreaded or real‑time workloads, while custom allocators and static buffers should be wielded sparingly due to complexity.  **Always profile before and after optimisation—what aids one workload may hinder another.**
