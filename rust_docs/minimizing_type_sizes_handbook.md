# Best Practices for Minimising Type Sizes in Rust (Nightly 1.91 – 2025)

## Why Type Sizes Matter

In Rust the *size of a type* directly influences memory usage, cache locality, data‑movement costs and the amount of memory copied during function calls. The Rust compiler (`rustc`) automatically arranges struct and enum fields to minimise size for the default `repr(Rust)`, but **large types can still degrade performance**. Reducing type sizes can speed up hot loops and cut memory traffic on modern CPUs. *The Rust Performance Book* stresses that shrinking often‑instantiated types reduces cache pressure and avoids expensive `memcpy` calls. Minimising type sizes should, however, **never** come at the cost of code correctness or API clarity—**measure** sizes before and after changes and weigh the trade‑offs.

---

## Measuring Type Sizes

Rust provides several mechanisms to inspect the size and layout of types:

| Tool                                       | Purpose                                                   |
| ------------------------------------------ | --------------------------------------------------------- |
| `std::mem::size_of::<T>()` / `size_of_val` | Compile‑time size (bytes) of a type or value              |
| **`-Zprint-type-sizes`** *(nightly flag)*  | `rustc` prints detailed layout (field offsets, padding)   |
| `std::alloc::Layout::new::<T>()`           | Returns size **and** alignment of a type                  |
| **`static_assertions`** crate              | Macros such as `assert_eq_size!` to ensure size stability |

Use these in tests or ad‑hoc `cargo run -Zprint-type-sizes` sessions to explore how design decisions affect layout.

---

## Type Layout and `repr` Attributes

* **`repr(Rust)`** (default): compiler may reorder fields to reduce padding.
* **`#[repr(C)]`**: fixed *C* layout—use **only** for FFI; may introduce extra padding.
* **`#[repr(transparent)]`**: wrapper has the same layout as its single non‑zero‑sized field (enables *null‑pointer optimisation* for `Option<Wrapper>`).
* **`#[repr(u8)]`, `#[repr(i8)]`, …**: set enum discriminant size; payload of the largest variant still dominates.
* **`#[repr(packed(n))]`**: removes padding by lowering alignment to *n*—**dangerous** due to misaligned access; restrict to hardware registers/wire formats.

> **Tip:** Avoid unnecessary `repr(C)` on internal types; keep the compiler free to optimise.

---

## Typical Techniques for Shrinking Types

### 1 Struct Field Ordering and Padding

With `repr(Rust)` the compiler automatically reorders fields, so manual tuning is seldom needed. For `repr(C)` structs (FFI), order fields **largest‑alignment → smallest** to minimise padding.

* Use the default representation when possible.
* In `repr(C)` structs, avoid mixing booleans/`usize` haphazardly; group similar‑sized fields.

### 2 Shrinking Enums

Rust enums store a *discriminant* plus the payload of the **largest** variant. Techniques to shrink them:

| Technique                 | Key idea                                                                                   | When to use                                        |
| ------------------------- | ------------------------------------------------------------------------------------------ | -------------------------------------------------- |
| **Box the large variant** | Store large payloads on the heap (`Box<T>`, `Arc<T>`, `Rc<T>`); enum becomes pointer‑sized | Variant contains a large array or struct           |
| **Small discriminant**    | `#[repr(u8)]`, `#[repr(u16)]` shrink discriminant for many zero‑sized variants             | Payloads are small & FFI layout required           |
| **Niche optimisation**    | Pointers/`NonZero*` types have unused bit patterns; `Option<T>` can equal `T` size         | Designing enums around pointer‑like/non‑zero types |

```rust
enum BigEnum {
    Small(u8),
    Big([u8; 1024]), // 1 KiB variant inflates the whole enum
}

// Improved
enum BetterEnum {
    Small(u8),
    Big(Box<[u8; 1024]>),
}
```

`BetterEnum` is now pointer‑sized instead of \~1 KiB—heap allocation trades a small pointer for better cache locality when most values are `Small`.

### 3 Choosing Smaller Integer Types

`usize` is 8 bytes on 64‑bit platforms. Prefer `u32`/`u16`/`u8` for indices and counts **when the range fits**; convert to `usize` only at the indexing site. Example: a pair of `u16` saves 12 bytes over two `usize`s on 64‑bit.

### 4 Using Memory‑Efficient Containers

#### 4.1 Convert `Vec<T>` → `Box<[T]>` when not growing

`Vec<T>` header = **3 words** (ptr, len, cap). `vec.into_boxed_slice()` drops the capacity → **2 words**; large savings for many instances.

#### 4.2 Use **ThinVec** for optional vectors

`ThinVec` stores len & cap inline with data, so `size_of::<ThinVec<T>>() == size_of::<Option<ThinVec<T>>>`. Ideal for nested/optional collections; nightly‑only (`#![feature(thin_vec)]`).

#### 4.3 Use **SmallVec**, **ArrayVec** (or similar) for small collections

* `SmallVec<[T; N]>` stores up to *N* items inline, then heaps.
* `ArrayVec<T, N>` stores exactly *N* items inline; no capacity field.

These improve locality and remove the capacity word, but benchmark pushes beyond the inline capacity (heap or panic).

### 5 Packing Boolean Flags

Primitive `bool` = 1 byte; multiple bools incur padding. Pack flags via:

* **`bitflags`** crate – type‑safe, stored in `u8`/`u16`/`u32`.
* Procedural macros such as **`bool_to_bitflags`** or **`pack_bools`** generate bitfields.

Choose the smallest integer that fits (e.g., `u8` for ≤ 8 flags).

### 6 Leveraging Null‑Pointer Optimisation (NPO)

`Option<&T>`, `Option<Box<T>>`, `Option<NonNull<T>>`, `Option<NonZero*>` are **the same size** as their plain counterparts because the discriminant is stored in unused bit patterns.

* Use `Option<Box<T>>` instead of raw pointer + bool.
* Wrap IDs in `NonZeroU32`, `NonZeroUsize`, etc., so `Option<NonZeroU32>` stays 4 bytes on 64‑bit.
* `#[repr(transparent)]` wrappers preserve the optimisation.

### 7 Ensuring Size Stability

* **`static_assertions`**: compile‑time checks, e.g.

  ```rust
  use static_assertions::const_assert;
  const_assert!(std::mem::size_of::<MyStruct>() <= 32);
  ```
* Use `assert_eq_size!` between types when refactoring.
* Guard expectations per‑arch with `cfg(target_pointer_width = "64")`.

---

## Atypical and Advanced Cases

### `repr(packed)` and Misaligned Data

`#[repr(packed(n))]` eliminates padding but causes misaligned accesses. When unavoidable (wire formats, hardware registers):

* Use `ptr::read_unaligned` / `ptr::write_unaligned`.
* Or copy to an aligned buffer first.

### SIMD and `repr(simd)`

Nightly‑only; for low‑level numeric kernels. **Not** a general size‑reduction tool.

### Adjusting Alignment

`#[repr(align(N))]` increases alignment (often increases size). Lowering alignment via `repr(packed)` risks UB—avoid for size alone.

### Uncommon Container Types

* **`slab` / `generational-arena`** – dense storage, small integer handles (`u32`).
* **`dashmap` / `hashbrown`** – leaner hash‑maps.
* **`ThinBox` / `ThinArc`** – pointer‑sized smart pointers (experimental).
* **`enumset`, `bitset_core`** – compact bitsets of enum variants.

Benchmark before adopting.

### Optimising for FFI

* Use `repr(C)`/`repr(transparent)` for stable layout; order fields manually.
* Split large payloads into separate pointers/IDs instead of embedding.
* Prefer getters/setters to raw struct exposure—enables future internal reorganisation.

---

## Best‑Practice Checklist (2025 Edition)

* **Measure first**: `size_of`, `-Zprint-type-sizes`—avoid guessing.
* **Prefer `repr(Rust)`**: let the compiler reorder fields.
* **Box large enum variants**: keep enums pointer‑sized.
* **Choose appropriate integer widths**: `u32`/`u16`/`u8` over `usize` when safe.
* **Convert rarely‑mutated `Vec<T>` → `Box<[T]>`**: save one word.
* **Use `ThinVec`** for optional/nested vectors.
* **Store small collections inline**: `SmallVec`, `ArrayVec`, `smallbox`.
* **Pack boolean flags**: `bitflags` or generated bitfields.
* **Exploit NPO**: use `Option<Box<T>>`, `Option<&T>`, `Option<NonZero*>`.
* **Verify with static assertions**: guard against regressions.
* **Avoid `repr(packed)`** unless layout is externally mandated.
* **Benchmark & profile**: size is one aspect—also track CPU time & cache misses.

---

## Concluding Remarks

Shrinking type sizes in Rust can yield significant performance and memory benefits, especially in data‑intensive workloads. The language and ecosystem offer numerous techniques—automatic field reordering, niche optimisations, pointer‑sized containers and bitfield crates—to help you design *compact, efficient* data structures. Follow the practices above, **measure** every change and, when in doubt, favour clarity over micro‑optimisation. Align your code with *Best Practice 2025* ideals for quality, maintainability and performance.
