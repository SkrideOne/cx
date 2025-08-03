# Best practices for controlling type sizes and alignment in Rust 1.91‑nightly (2025)

## Background: why size and alignment matter

Modern CPUs access memory in **cache‑line–sized blocks** (typically 64 bytes) rather than single bytes. Reading a field that straddles two cache lines forces the CPU to fetch both lines, which is slower. Hardware therefore requires that a value of size *N* be stored at an address that is a multiple of *N* (or, more generally, its *alignment*). Rust associates **size** and **alignment** with every type:

* **Alignment (n)** – the address of any value of that type must be a multiple of *n*. Alignment is measured in bytes, is always at least 1 and always a power of two (*e.g.* a value with alignment 2 must be stored at an even address).
* **Size** – the offset in bytes between consecutive elements in an array of the type, including any padding. A value’s size is always a multiple of its alignment. Some types are *zero‑sized* (ZSTs); `0` is treated as a multiple of any alignment.

Most programs can let the compiler choose the layout automatically, but when interfacing with C, squeezing memory, writing `unsafe` code or optimising for cache, you must sometimes override the defaults.

---

## Basic rules for layout (Rust 1.91)

### Primitive types

The size and alignment of primitives is platform‑dependent but usually equal to their natural word size. On a 64‑bit system, `u8`/`i8` have size 1 · alignment 1, `u16` has size 2 · alignment 2, `u64` has size 8 · alignment 8, `f32` has size 4 · alignment 4, etc. `usize`/`isize` are large enough to hold any address (*e.g.* 8 bytes on a 64‑bit platform). The compiler may choose a smaller alignment for 128‑bit integers on 32‑bit targets.

### Pointers and references

Pointers and references to *sized* types have the same size and alignment as `usize`. Pointers to dynamically sized types (DSTs) such as slices and trait objects are **fat pointers** that store a data pointer plus metadata; they are currently twice the size of a thin pointer and share the same alignment. Do **not** rely on the exact layout outside FFI with `repr(C)`.

### Arrays, slices and tuples

* An array `[T; N]` has size `size_of::<T>() * N` and the same alignment as `T`. Elements are contiguous (no padding between them).
* A slice `[T]` has the same layout as the portion of the array it slices; pointers to slices (`&[T]`) are fat pointers.
* Tuples use the default **repr(Rust)** representation: fields are ordered to honour alignment, but the compiler may reorder them to reduce padding. The unit tuple `()` has size 0 · alignment 1.

### Structs – default **repr(Rust)**

The default struct representation guarantees:

1. **Proper alignment** – each field’s offset is a multiple of its alignment.
2. **No overlap** – fields never overlap.
3. **Struct alignment** – the struct’s alignment is at least the maximum alignment among its fields.

The compiler may reorder fields to minimise padding. For example:

```rust
struct Foo {
    a: u8,
    b: u16,
    c: u8,
}
```

Naïvely this is `1 + 2 + 1 = 4` bytes, but storing fields in that order would insert 2 bytes of padding after `a`. The compiler reorders the fields to `[b, a, c]`, so the total size is 4 bytes instead of 6.

### Structs – **repr(C)**

`#[repr(C)]` yields a layout compatible with C:

* Fields remain in *declaration order*.
* Padding bytes are inserted before each field to ensure alignment.
* The struct’s alignment is the maximum field alignment, and its size is rounded up to a multiple of that alignment.

Always use `repr(C)` for FFI and binary formats.

### Enums

With default **repr(Rust)** an enum stores a *discriminant* plus enough space for its largest variant. For C‑compatible layout use `repr(C)` or a primitive representation such as `repr(u8)`.

### Unions

A `#[repr(C)]` union’s size equals the maximum size of its fields (rounded up to alignment); its alignment equals the maximum alignment among its fields. Read only the field that you last wrote.

### Trait objects and unsized types

Trait objects (`dyn Trait`) are fat pointers whose vtable stores the destructor, the size and the alignment of the erased type. Obtain these at runtime with `size_of_val` / `align_of_val`.

### `std::alloc::Layout` for manual allocation

Use `Layout::new::<T>()` (or `Layout::from_size_align`) to obtain a *size‑and‑alignment* descriptor when manually allocating memory. The alignment must be a non‑zero power of two; the size is rounded up to a multiple of that alignment.

---

## Controlling alignment and padding

Rust provides several `repr` modifiers. Use them **judiciously** and document why they are necessary.

### `repr(align(N))` – raising alignment

`#[repr(align(N))]` raises a struct’s alignment to *N* bytes (power of two). This may increase the overall size by adding trailing padding but never decreases field alignment. Do **not** mix with `repr(packed)`.

Use it to align data to cache‑line boundaries (*e.g.* 64 or 128 bytes) or to satisfy SIMD requirements, preventing *false sharing* between threads.

### `repr(packed)` – lowering alignment (packing)

`#[repr(packed)]` or `repr(packed(N))` lowers a struct’s alignment to 1 (or *N*). **Dangers:**

* Rust references assume alignment; taking `&`/`&mut` to a mis‑aligned field is *undefined behaviour*. Always copy fields by value (`let x = packed.field;`).
* Never store types that implement `Drop` or contain generics requiring drop glue.

Use packing **only** when required by external formats and document the safety invariants.

### `repr(transparent)` – single‑field wrappers

`#[repr(transparent)]` guarantees that a wrapper struct has **exactly** the same size and alignment as its single non‑ZST field:

```rust
#[repr(transparent)]
struct MyHandle(u32);
```

Ideal for FFI newtypes (handles, pointers) where you need type safety but identical ABI.

### Primitive `repr(u8)` / `repr(i32)` on enums

Applying a primitive representation to a *field‑less* enum forces its size/alignment to match the primitive. For enums *with* fields it becomes a tagged union using the primitive for the discriminant—useful when a C API expects a specific integer size.

---

## Typical use cases and best practices

### 1. FFI interop with C

* Apply `repr(C)` to structs, enums and unions shared with C.
* Use `repr(transparent)` for newtypes that wrap C handles.
* Avoid packing unless the C side also uses packed structs; if packing is required, access fields by value and avoid types needing drop glue.
* For bitfields or compressed data, prefer manual packing (*e.g.* `u8` arrays or the **bitfield** crate).

### 2. Reducing struct size and improving cache locality

* Under **repr(Rust)** let the compiler reorder fields; under **repr(C)** manually order fields from *largest* to *smallest* alignment.
* Group **hot** fields (frequently accessed together) first; place **cold** fields at the end or in a separate struct.
* Where operations need only specific fields over many items, consider a *Structure of Arrays* (SoA) instead of an *Array of Structures* (AoS).
* Prevent false sharing by aligning per‑thread data to cache‑line size with `repr(align(64))`.

### 3. Manual memory management and `unsafe` code

* Compute allocation sizes with `Layout::new::<T>()` or `Layout::for_value` and round up to alignment.
* Use `MaybeUninit<T>` for uninitialised memory.
* Ensure both source and destination types have identical size & alignment when copying via raw pointers or `transmute`.
* Use `pointer::align_offset` when performing pointer arithmetic.

### 4. Working with unsized types and trait objects

* Write generic functions with `T: ?Sized` and use `size_of_val` / `align_of_val`.
* Avoid placing DSTs inside structs except as the *final* field using unsized field syntax.

### 5. Zero‑sized types and tail‑padding reuse

* ZSTs occupy *no space* but may have non‑zero alignment; Rust may place them in tail padding.
* Comparing addresses of ZSTs is meaningless—multiple instances can share an address.

---

## Atypical cases and advanced considerations

### Unions and type punning

Use `repr(C)` unions to reinterpret data safely; read only the field last written and ensure all fields implement `Copy`.

### `repr(simd)`

`repr(simd)` treats a tuple struct as a SIMD vector, often increasing alignment to match vector register size (16 / 32 bytes). It is nightly‑only and unstable—gate behind feature flags.

### Field‑repetition and array‑stride proposals

Rust‑internals discussions propose allowing array stride to differ from element size to reuse tail padding (similar to C++ 20’s `[[no_unique_address]]`). As of 2025 this remains unstable; do not rely on it.

### Unaligned access and portability

Some ISAs (x86) allow unaligned loads, others (certain ARM/MIPS) fault. Use `ptr::read_unaligned` / `write_unaligned` when necessary, but expect slower performance and reduced portability.

### Exotically sized and dynamically sized types

Experimental traits such as **`Aligned`** and **`MetaSized`** (extern types, unsized types) remain unstable in 2025. When alignment must be known at compile time, restrict generics to `T: Sized`.

---

## Summary of best practices (2025)

| Task or goal                | Recommended practice                                                                                                              | Rationale and notes                                                           |
| --------------------------- | --------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| **Interop with C**          | Use `repr(C)` or `repr(transparent)`; avoid `repr(packed)` unless required.                                                       | Preserves field order & padding; safe for FFI.                                |
| **Minimise struct size**    | Let the compiler reorder fields (*repr(Rust)*), or order largest→smallest under `repr(C)`; avoid `repr(packed)` unless essential. | Reduces padding (e.g. 6 → 4 bytes).                                           |
| **Optimise cache usage**    | Group hot fields; separate cold fields; use SoA; align per‑thread data with `repr(align(64))`.                                    | Minimises cache‑line crossings; reduces false sharing.                        |
| **Work with unsized types** | Use `?Sized` bounds; call `size_of_val` / `align_of_val`; store DSTs only as final fields.                                        | DSTs have runtime size & alignment; compile‑time `align_of` requires `Sized`. |
| **Manual allocation**       | Use `Layout::new::<T>()` / `Layout::for_value`; round size up to alignment.                                                       | Ensures returned pointer satisfies alignment.                                 |
| **Packing**                 | Avoid references to packed fields; copy by value; do not store types with destructors.                                            | Mis‑aligned references are UB.                                                |
| **Raising alignment**       | Apply `repr(align(N))` (power of two).                                                                                            | Cache‑line alignment & SIMD; cannot mix with `packed`.                        |
| **Zero‑sized types**        | ZSTs may share addresses; use for type‑level info (*PhantomData*).                                                                | ZSTs occupy no space but alignment still matters.                             |
| **Traits / trait objects**  | Use `dyn Trait` behind pointers; use `align_of_val` on values.                                                                    | Vtable stores size & alignment.                                               |
| **Unions**                  | Mark unions `repr(C)`; read only the active field; ensure fields are `Copy`.                                                      | Predictable size & alignment.                                                 |
| **SIMD types**              | Use `repr(simd)` behind nightly feature gates; align data appropriately.                                                          | Enables SIMD registers; unstable in 2025.                                     |

---

## Conclusion

Understanding size and alignment in Rust lets you write safer FFI bindings, reduce memory usage and improve performance. Core rules—alignment is a power of two, size is a multiple of alignment, and default **repr(Rust)** may reorder fields—remain stable. When deviating with `repr(C)`, `repr(align)` or `repr(packed)`, **document the reasons**, verify correctness on all targets and benchmark carefully. In performance‑critical code, apply cache‑aware layouts and prevent false sharing by aligning structures to cache‑line boundaries. Following these guidelines will help you produce correct, efficient Rust code that stands the test of time.
