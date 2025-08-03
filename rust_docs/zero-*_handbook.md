# Zero-* Patterns in Rust Nightly 1.91 (Best Practice 2025)

## Introduction

Rust prides itself on zero‑cost abstractions—high‑level code that compiles to the same machine code you would have written by hand.  The zero‑* family of patterns extends this idea: zero‑copy access avoids unnecessary memory moves, zero‑sized types encode information at compile time without runtime storage, zeroization securely wipes secrets, and zero‑cost abstractions (iterators, closures and async/await) give expressiveness without adding runtime overhead.  Rust 1.91 (nightly) continues to emphasise safety and performance and brings incremental improvements such as the guaranteed capacity of `Vec::with_capacity`【684066008670463†L134-L140】 and safe non‑pointer intrinsics.  The practices below are aligned with Best Practice 2025 and assume the user’s code is compiled with recent nightly features (e.g., `let_chains` stabilised in 1.88).

This guide focuses on practical patterns for quality and performance. For typical cases the recommended approach is explained with examples. Non-typical cases—those requiring advanced patterns or `unsafe`—are discussed separately.

---

## 1. Zero-Cost Abstractions

### 1.1 Understanding the principle

Zero-cost abstraction means you pay no runtime penalty for using higher-level constructs; what you don’t use, you don’t pay for. Rust’s trait system provides compile-time static dispatch and generates code that is as efficient as hand-written loops. Examples include:

| Abstraction    | Benefit                                                                  |
| -------------- | ------------------------------------------------------------------------ |
| Iterators      | `.iter().map().sum()` compiles to loops with no iterator objects left    |
| Closures       | Captures are monomorphized, avoiding heap unless boxed                   |
| Option/Result  | Pattern matches compiled away without overhead                           |
| Trait generics | Static dispatch via `fn foo<T: Trait>(t: T)` yields inlined methods      |
| async/await    | Compiles to stack-allocated state machine unless boxed or trait-objected |

### 1.2 Best practice

* Prefer static dispatch: Use generics over trait objects for performance.
* Avoid boxing: Return concrete futures or `impl Future<Output = _>` instead of boxing.
* Use iterators over indexing: Improves inlining and avoids intermediates.
* Leverage const generics for fixed-size array operations.
* Enable lints like `dangerous_implicit_autorefs`, `invalid_null_arguments`.
* Measure performance: benchmark critical paths.

---

## 2. Zero-Copy Patterns

### 2.1 Basic zero-copy with references

* Use references (`&[u8]`, `&T`) or `Cow<'a, T>`.
* Prefer slices over `Vec` for read-only data.
* Use `Cow` for APIs that can take borrowed or owned.
* Explicitly annotate lifetimes to maintain borrow checker guarantees.

### 2.2 Using the `zerocopy` crate

* Derive `FromBytes` on `#[repr(C)]` structs.
* Ensure only Plain Old Data fields.
* Add padding manually for alignment.
* Use `read_from` to cast byte slices into typed structs.

### 2.3 Zero-copy deserialization with `rkyv`

* Derive `Archive`, `Serialize`, `Deserialize`.
* Use `#[repr(C)]` with aligned fields.
* Use `bytecheck` and `CheckBytes` for validation.
* Efficient for loading large static or read-only datasets.

### 2.4 Alignment and casting with `bytemuck`

* Use `#[repr(transparent)]` for wrappers.
* Implement `TransparentWrapper`.
* Ensure layout equivalence before casting.

### 2.5 Self-referential zero-copy with `yoke` / `ouroboros`

* Use `yoke` to borrow from owned data.
* Use `ouroboros` for more complex self-referential abstractions.

### 2.6 Zero-copy I/O and streams

* Use `bytes::Bytes` or `quiche` for network buffers.
* Use memory-mapped files or `io_uring` for file I/O.

---

## 3. Zero-Sized Types (ZSTs) and Zero-Sized References (ZSR)

### 3.1 Marker types and `PhantomData`

* `PhantomData<T>` declares type ownership, variance, or lifetimes.
* Example: `PhantomData<&'a T>` marks a borrow without storage.
* Always derive `Copy/Clone` if sound.

### 3.2 Transparent wrappers

* Use `#[repr(transparent)]` with one non-ZST field.
* Add ZST fields (like `PhantomData`) for invariants.

```rust
#[repr(transparent)]
pub struct MyWrapper<T> {
    field: T,
    _marker: PhantomData<()>,
}
```

### 3.3 Zero-Sized Reference (ZSR) pattern

* Use PhantomData markers to avoid storing `'static` references.
* Common in embedded (e.g., `heapless::pool` macro).
* Ensures compile-time resource correctness.

### 3.4 Generativity and type-level proofs

* Encode constraints like `IsCopy<T>` or `SameLayout<A,B>`.
* Constructible only when type requirements are satisfied.

### 3.5 Non-typical ZST uses

* Self-referential tracking via ZST lifetimes.
* Phantom tags with `Tagged<T, Tag>`.
* Singleton patterns in embedded: statically enforce exclusivity.

---

## 4. Zeroization and Secure Memory Handling

* Use the `zeroize` crate for sensitive data.
* Call `.zeroize()` manually or wrap in `Zeroizing<T>`.
* Derive `Zeroize`/`ZeroizeOnDrop` for custom types.
* Avoid `mem::zeroed()` for secret types; unsafe for many kinds.
* Use `Pin` to avoid reallocation and ensure memory locality.

---

## 5. Zero-Initialisation and Unsafe Constructors

### 5.1 Avoid `mem::uninitialized` and `mem::zeroed`

* Both are deprecated and unsafe for types with invalid bit patterns.

### 5.2 Using `MaybeUninit`

```rust
use std::mem::MaybeUninit;

let mut uninit: MaybeUninit<MyStruct> = MaybeUninit::uninit();
unsafe { ptr::write(uninit.as_mut_ptr(), MyStruct::new()); }
let value = unsafe { uninit.assume_init() };
```

* Use `MaybeUninit::zeroed()` when `T: Zeroable` (e.g., via `bytemuck`).

### 5.3 Initializing large arrays

* Use `[T; N]::from_fn` (stable since 1.88) to initialise arrays efficiently.

---

## 6. Zero-Allocation Techniques

* Reserve capacity in `Vec`/`String` with `with_capacity`.
* Use stack-based containers: `ArrayVec`, `SmallVec`, `heapless`.
* Reuse buffers by calling `.clear()`.
* Use `read_exact` instead of `read_to_end`.
* Use `write!` over `format!` in hot paths.

---

## 7. Zero-Cost Async and Concurrency

* Return concrete futures: avoid `Box<dyn Future>` unless needed.
* Use `async fn` and expose `poll` for fine-grained control.
* Futures are stack-allocated; avoid moves post-`.await`.
* Never call blocking functions in async contexts; spawn tasks.
* Measure `.await` boundaries to avoid excessive yield points.

---

## 8. Non-Typical Scenarios

### 8.1 Self-referential and generative types

* Use `ouroboros`, `yoke`, or `PhantomData<fn(&'a())>` to encode generativity.

### 8.2 Unsafe FFI and transmutation

* Use `[u8; 0]` for flexible trailing arrays.
* Avoid raw pointer `null` references.
* Use proof types to validate `transmute` safety.

### 8.3 Zero-cost macros and metaprogramming

* Use declarative macros or `const fn` + const generics for compile-time code.

### 8.4 Advanced zeroization and hardware attacks

* Zeroize cannot prevent register/cache leaks.
* Use OS support (`mlock`, `mprotect`) or enclaves if needed.
* The `secrecy` crate provides safer APIs for sensitive data.

---

## Conclusion

The zero-* patterns in Rust revolve around eliminating unnecessary work—runtime overhead, memory copies, runtime storage or secret leaks. Rust 1.91 nightly continues this trajectory and builds upon 1.90: features like `let_chains` (stabilised in 1.88) remain available, while the allocator API improvements guarantee that `Vec::with_capacity` always allocates at least the requested capacity and many `std::arch` intrinsics can now be used in safe code【684066008670463†L134-L140】.  These improvements make zero-* techniques more predictable and safer.

* Prefer high-level abstractions when truly zero-cost—but measure.
* Use `PhantomData` and lifetimes to enforce compile-time invariants.
* Use specialized crates (`zerocopy`, `rkyv`, `bytemuck`, `zeroize`, `heapless`, `ouroboros`, `yoke`) responsibly.
* Avoid `mem::uninitialized`/`zeroed` for non-POD types—prefer `MaybeUninit + Zeroable`.

Zero-* techniques enhance performance and safety, but clarity and correctness come first. Apply these guidelines to build robust, modern, high-performance Rust systems.
