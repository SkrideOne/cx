# Loops and iterators in Rust (nightly 1.90): Best-Practice Manual for 2025

## Introduction

Loops and iterators are core abstractions in Rust. A loop executes a block repeatedly (`loop`, `while` or `for`), while an iterator is an object that produces a sequence of values on demand. Rust’s iterators are lazy and zero-cost: the optimizer fuses nested iterator adapters into simple loops, so iterator-heavy code compiles to almost the same assembly as an equivalent hand-written loop.

Because of this, idiomatic code favours iterators for clarity and reliability, but developers must still understand when to use explicit loops for simplicity or to avoid semantic pitfalls. This manual covers current best practices in August 2025 for Rust nightly 1.90.

---

## 1 Typical scenarios and recommended patterns

### 1.1 Iterating over collections

* Use `for` loops with iterators: `for item in iterable` calls `IntoIterator::into_iter()`. Arrays implement `IntoIterator`, so:

    * `for x in [a, b, c]` iterates over `&T` by default.
    * `for x in vec` consumes `vec` when moved.
* Control ownership:

    * `.iter()` for immutable references (`&T`).
    * `.iter_mut()` for mutable references (`&mut T`).
    * `.into_iter()` to take ownership.
* Hash maps: use `keys()`, `values()`, `values_mut()`, `iter()`, `iter_mut()` and `drain()` according to ownership needs.
* Use `.enumerate()` for indices; avoids per-index bounds checks.
* Ranges: `0..n`, `a..=b`, use `.rev()` for reverse, `.step_by(k)` to skip steps. Ranges compile to tight loops without allocation.

### 1.2 Preallocating and building collections

* Preallocate capacity with `Vec::with_capacity(n)` when pushing in loops:

  ```rust
  let mut result = Vec::with_capacity(list.len());
  for entry in list {
      result.push(transform(entry));
  }
  ```
* Transform with iterator chains:

  ```rust
  let result: Vec<_> = list.into_iter()
      .map(transform)
      .collect();
  ```
* Flatten or concatenate nested collections:

    * Prefer `flat_map` or explicit loops with `Vec::extend()` over `fold` with `concat()`.

### 1.3 Memory and allocation behaviour

* Standard iterators over collections do not allocate per element.
* For I/O lines, use `read_line(&mut String)` to reuse buffers and avoid allocations.
* To iterate multiple times over external data, collect first or clone explicitly.

### 1.4 Writing readable iterator chains

* Break complex chains into stages: obtain iterator, apply adapters, then collect/consume.
* Assign intermediate iterators to variables or use helper functions.
* Materialize for side effects or multiple passes with `.collect()`.

### 1.5 Minimizing copying and cloning

* Borrow instead of cloning: yield references and pass slices (`&[T]`).
* Use `.copied()` (for `Copy` types) or `.cloned()` to obtain owned values explicitly.

### 1.6 Controlling loop invariants

* Move invariant computations outside loops.
* Use `while` loops when updating multiple variables; prefer `for`/iterators otherwise.
* Use `if let`/`while let` for `Option` and `Result` patterns.

### 1.7 Miscellaneous patterns

* Check sorted order with `.is_sorted()`, `.is_sorted_by()`, `.is_sorted_by_key()`.
* Use `.map_while()` (nightly) to stop when closure returns `None`.
* Use `.nth_back()` on `DoubleEndedIterator` to get elements from the end.
* Be explicit when using arrays’ `into_iter()` semantics.

---

## 2 Non-typical and advanced scenarios

### 2.1 Implementing custom iterators

* Implement `Iterator` by defining `next(&mut self) -> Option<Item>`.
* Store only necessary state and reference or own the underlying collection.
* Provide separate iterators for `&T`, `&mut T` and owned `T`.
* Implement `IntoIterator` for `&Collection`, `&mut Collection` and `Collection`.
* Consider `DoubleEndedIterator` and `ExactSizeIterator` where applicable.
* Ensure `next()` returns `None` at end; avoid panics.
* Document safety when using `unsafe`.

### 2.2 Lazy evaluation pitfalls

* Iterator adapters fuse into single passes; map+filter interleave operations.
* Fusion can change semantics; materialize with `.collect()` when separate passes are needed.

### 2.3 Asynchronous iteration

* Use `Stream` and `for await` (unstable) or `while let Some(x) = stream.next().await`.
* Adapt channels with `tokio_stream::wrappers::ReceiverStream` or `futures::stream::unfold`.

### 2.4 I/O and external data

* Reuse buffers with `read_line(&mut buffer)`.
* For binary I/O, use `read_exact` or `read_to_end` into preallocated buffers.

### 2.5 Concurrency and parallelism

* Use `rayon::par_iter()` for data-parallel workloads heavy enough to offset thread overhead.
* Avoid sharing single-threaded iterators across parallel tasks; partition data or use thread-safe collections.

### 2.6 Unsafe and low-level loops

* Use `unsafe` pointer loops only for hotspots after profiling; document invariants and ensure safety.

### 2.7 Non-allocating iterators and generators

* Use `core::iter::repeat`, `repeat_with`, `empty`, `once` for non-allocating sequences.
* Experimental: implement `Generator` trait (nightly) or use `async_stream` for coroutine patterns.

---

## Conclusion

Rust nightly 1.90 maintains that iterators are zero-cost abstractions. Prefer iterator methods and `for` loops for clarity and performance. Preallocate when building collections and avoid unnecessary cloning. Use explicit loops to simplify code or avoid semantic pitfalls. In advanced scenarios—custom iterators, concurrency or streaming—understand fusion and laziness to avoid footguns. Always profile real-world code and consult up-to-date Rust release notes for improvements.
