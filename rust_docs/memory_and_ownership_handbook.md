# Memory and Ownership Management in Rust nightly 1.90 – Best‑Practice 2025

## Introduction

Rust’s memory model couples deterministic allocation with compile‑time ownership and borrowing.  Every value has a single owner, and values are automatically freed when they fall out of scope (RAII – Resource Acquisition Is Initialisation).  Borrowing allows multiple immutable references or a single mutable reference, while lifetimes express how long those borrows are valid.  These features eliminate entire classes of memory errors common in C/C++, such as use‑after‑free and double free.

This manual consolidates memory‑management and ownership guidance for Rust nightly 1.90 as of August 2025.  It replaces the previous `memory_handbook.md` and incorporates ownership rules that were split across multiple files.  Each section distinguishes typical patterns, performance‑tuning techniques and advanced use‑cases.  Stable and nightly features are clearly identified; where behaviour has changed recently, notes cite the official release notes (for example, Rust 1.87 guarantees that `Vec::with_capacity` now allocates at least the requested size【968327487850412†L134-L136】).

---

## Fundamental concepts

### Stack vs. heap

* **Prefer stack allocation** for small, fixed‑size data.  Values on the stack are freed automatically when they leave scope and do not require dynamic allocation.  Fixed‑size arrays (`[T; N]`) and small structs live here.
* **Use heap allocations** when data size is unknown at compile time or ownership must be transferred.  Types like `Box<T>`, `Vec<T>`, `String` and `HashMap<K, V>` allocate on the heap and manage memory automatically.
* Implement the **`Drop`** trait for types that manage external resources (files, sockets, FFI handles) to ensure cleanup at scope exit.

### Ownership, moves and borrowing

* **Single ownership**: every value has exactly one owner.  Moving a value transfers ownership; copying types (`Copy` trait) create duplicates.
* **Borrowing**: you may have many immutable borrows (`&T`) or one mutable borrow (`&mut T`) at a time.  Rust’s borrow checker enforces this at compile time to prevent data races.
* **Lifetimes**: explicit annotations (e.g. `<'a>`) indicate how long a reference is valid.  In simple cases lifetimes are elided automatically; in complex structures they document relationships between references.

### Choosing the right pointer or smart‑pointer type

| Pointer type            | Use case                                           | Notes                                                         |
|-------------------------|----------------------------------------------------|---------------------------------------------------------------|
| `Box<T>`                | Exclusive ownership of heap‑allocated data         | Implements `Drop`; value is freed when `Box` is dropped.     |
| `Rc<T>`                 | Shared ownership in single‑threaded code          | Reference‑counted; not thread‑safe.  Use `Rc::clone` to add a reference. |
| `Arc<T>`                | Shared ownership across threads                   | Uses atomic reference counting; more overhead than `Rc`.      |
| `RefCell<T>`            | Interior mutability (single‑threaded)             | Borrow rules enforced at runtime; panics on violation.        |
| `Mutex<T>`/`RwLock<T>` | Interior mutability across threads                | Provide RAII‑based locking; only one thread may mutate at a time. |
| `Weak<T>`               | Non‑owning reference into an `Rc`/`Arc`          | Does not keep the value alive; used to break reference cycles. |

### Collections and capacity

* For dynamic collections like `Vec<T>` and `String`, reserve capacity using `with_capacity` or `reserve` to reduce reallocations.  Since Rust 1.87 the standard library guarantees that `Vec::with_capacity(n)` will allocate **at least** `n` elements up front【968327487850412†L134-L136】; this helps predict memory usage and prevents accidental re‑allocations.
* Use `HashMap::with_capacity` or `HashMap::reserve` to pre‑allocate buckets and avoid rehashing.  Choose between `HashMap` and `BTreeMap` based on key distribution, iteration order and memory overhead.
* Prefer slices (`&[T]`) and fixed‑size arrays (`[T; N]`) when sizes are known at compile time.  Returning slices instead of owned vectors avoids copying data.

### Smart pointers and interior mutability

* Use `Rc<T>`/`Arc<T>` when multiple parts of your program need read‑only access to the same value.  Combine with `RefCell<T>` for single‑threaded interior mutability or `Mutex<T>`/`RwLock<T>` for multi‑threaded cases.
* Always minimise the scope of locks: acquire a lock, perform the minimal mutation, then drop the guard before calling functions that might block or `await`.
* Use `Weak<T>` to hold non‑owning references and break reference cycles in graphs or trees.

### Concurrency and asynchronous memory

* Combine `Arc` with `Mutex`/`RwLock` for shared, mutable state across threads or tasks.
* Understand **pinning**: futures created by `async fn` are self‑referential state machines and must not move after polling.  Use `Pin<Box<T>>`, `pin!` or `Box::pin` when storing futures.  Only types implementing `Unpin` may be moved after being pinned.
* Dropping a future cancels it; design `Drop` implementations to clean up resources or provide explicit async `close()` methods for asynchronous cleanup.

### File and resource management

* Use RAII wrappers (`File`, `TcpStream`, `BufReader/BufWriter`) so resources are closed automatically.
* In asynchronous code, avoid holding file or socket handles across `.await` points—move them into new tasks or drop before awaiting to prevent deadlocks.

---

## Performance tuning and profiling

* **Measure before optimising**.  Use profilers (`perf`, `valgrind`, `cargo flamegraph`, `criterion`) to find hotspots.  Optimisation without data often leads to regressions.
* **Reserve capacity** to avoid repeated reallocations.  The guarantee of `Vec::with_capacity` ensures the requested capacity is honoured【968327487850412†L134-L136】, but calling `reserve` can still grow the allocation when necessary.
* **Reuse allocations**: call `.clear()` on a `Vec` or `String` and append again instead of allocating new ones.  For reading input, reuse a buffer with `read_line(&mut String)` rather than calling `lines()` which allocates a new `String` each time.
* **Minimise allocation size**: choose appropriate numeric types (`u32` vs `u64`) and compact struct layouts to improve cache locality.
* **Borrow instead of clone**: pass references (`&T`) or use `Cow<'a, T>` when both owned and borrowed values are acceptable.  Avoid unnecessary calls to `.clone()`; if cloning is required, prefer `clone_from` for large allocations to reuse existing capacity.

---

## Advanced memory‑management patterns

### Custom allocators

Rust’s [allocator API](https://doc.rust-lang.org/alloc/alloc/trait.Allocator.html) is still nightly‑only (`#![feature(allocator_api)]`).  Implement a custom allocator for niche requirements such as real‑time systems, embedded devices or games.  Use `_in` constructors (`Vec::with_capacity_in`) to allocate via your allocator and `#[global_allocator]` to replace the default global allocator.  Carefully respect alignment and lifetime rules; unsound allocators can corrupt memory.

### Arena allocation

Arena allocators group allocations with the same lifetime and free them all at once.  Use crates like **bumpalo** or **typed‑arena**.  This pattern is common for ASTs or frame allocations.  Be aware that destructors may not run when arenas are cleared; wrap types implementing `Drop` in RAII containers or call destructors manually.

### `MaybeUninit` and zero‑initialisation

The standard functions `mem::uninitialized()` and `mem::zeroed()` are deprecated and unsound for most types.  Use `MaybeUninit<T>` to work with uninitialised memory safely.  Initialise individual fields before calling `.assume_init()`.  Use `MaybeUninit::zeroed()` only for types that are “zeroable” (no invalid bit patterns).

```
use std::mem::MaybeUninit;

let mut uninit: MaybeUninit<MyStruct> = MaybeUninit::uninit();
unsafe { std::ptr::write(uninit.as_mut_ptr(), MyStruct::new()); }
let value = unsafe { uninit.assume_init() };
```

For arrays, prefer `[T; N]::from_fn` (stable since 1.88) to initialise large arrays element‑by‑element without unsafe code.

### Avoiding memory leaks and cycles

* Break reference cycles with `Weak<T>` when using `Rc`/`Arc` in graphs or trees.
* Use `miri`, `valgrind` and sanitizers to detect leaks and undefined behaviour.  Stress test concurrency with `loom` and property‑based testing.

### Embedded and `no_std` environments

* Use `#![no_std]` and link against `libcore`.  Provide an allocator via the `alloc` crate or static buffers.
* Pre‑allocate fixed buffers and avoid dynamic allocation.  Use `heapless` types such as `Vec<T, N>` from the `heapless` crate.
* Implement custom panic handlers and abort on unrecoverable errors.  Use `panic = "abort"` in `Cargo.toml` to minimise binary size.

### FFI and unsafe code

* Clearly define ownership boundaries across FFI: use `Box::into_raw`/`from_raw` when transferring heap ownership and avoid freeing memory on the wrong side.
* Use `#[repr(C)]` and `unsafe extern "C" fn` declarations; avoid passing Rust types with non‑C layout.
* Minimise and encapsulate `unsafe` blocks; uphold invariants (alignment, validity, lifetimes) and document them thoroughly.
* Use `catch_unwind` to prevent Rust panics unwinding across the FFI boundary.

### Self‑referential types and pinning

When implementing self‑referential structs or generators, use `Pin` to prevent the struct from moving.  Create pinned values via `Box::pin` or macros like `pin!`.  Only types implementing `Unpin` may be moved after being pinned.

---

## Non‑typical scenarios

### Embedded / `no_std`

See above; allocate statically and avoid dynamic memory.  Use `core` and `alloc` crates.

### Concurrency across threads and tasks

Be careful when combining threads and async tasks.  Use `Arc<Mutex<T>>` or message passing instead of raw pointers.  Consider using `tokio::sync` primitives for asynchronous code and `std::sync` primitives for synchronous code, and never hold a lock across an `.await` point.

### Interoperability with other languages

Define clear boundaries: avoid mixing Rust deallocation with C allocation.  Provide wrapper functions to allocate and free on the same side.  Use `cbindgen` or `bindgen` to generate bindings and ensure type sizes match across languages.

---

## Summary

Memory and ownership management in Rust 1.90 builds on mature concepts—RAII, borrowing and lifetimes—and adds new guarantees like predictable `Vec::with_capacity` allocation【968327487850412†L134-L136】.  Prefer stack allocation when possible, reserve capacity for heap‑backed collections, and reuse buffers to avoid unnecessary allocations.  Use the appropriate smart pointer type (`Box`, `Rc`, `Arc`, `RefCell`, `Mutex`, `RwLock`) and break cycles with `Weak`.  When necessary, reach for advanced patterns—custom allocators, arenas, `MaybeUninit`, pinning—with caution and thorough documentation.  Measure and profile before optimising, and always test with tools like `miri` and sanitizers to maintain safety and performance.