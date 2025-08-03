# Best‑Practice Methodology for Hashing and Data‑Structure Selection in Rust nightly 1.91 (2025)

## 1 Background

The Rust nightly 1.91 release (branched from master on **2 Aug 2025**) uses the same core data structures as the stable standard library.  However, the nightly release includes additional experimental APIs (e.g. `Vec::pop_if`, `try_with_capacity`, `VecDeque::pop_front_if`) and exposes the underlying **hashbrown‑based SwissTable** implementation for `HashMap` and `HashSet`.  Nightly 1.91 also inherits the guarantee that `Vec::with_capacity` allocates at least the requested capacity and allows many `std::arch` intrinsics to be called from safe code【684066008670463†L134-L140】.  The guidance below targets **Best Practice 2025**: *avoid premature optimisation but know when to choose a faster hasher or alternative collection to meet performance, quality and security requirements*.

### 1.1 Hashing basics in Rust

Rust’s `HashMap` and `HashSet` rely on a randomised hashing algorithm, **SipHash 1–3**, which protects against Hash‑DoS attacks but is slower than many non‑cryptographic hashers (doc.rust-lang.org). Keys must implement `Eq` *and* `Hash`, and equal keys must produce equal hashes; **modifying a key so that its equality or hash changes while it is in the map is a logic error** (doc.rust-lang.org). A map’s seed is randomly generated at creation to prevent predictable collisions; this makes the map unsuitable for `const`/`static` initialisers unless you wrap it in a `LazyLock` or use a fixed hasher (`BuildHasherDefault`), but using a fixed seed opens the door to DoS attacks (doc.rust-lang.org). The nightly `HashMap` is implemented using **SwissTable**, which uses quadratic probing and SIMD lookups to achieve better cache locality and performance compared with the older `hashbrown` design (doc.rust-lang.org).

### 1.2 Alternative hashers

For performance‑critical workloads where Hash‑DoS is not a concern (e.g. internal tools, numerical data with trusted inputs), you may substitute a faster hasher:

| Hasher (crate)                                 | Characteristics (2025)                                                                                                                                                    | Use cases & notes                                                                                                                                                     |
| ---------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **`rustc‑hash`** (`FxHashMap`, `FxHashSet`)    | Very fast non‑cryptographic hash used by the Rust compiler; suitable for small keys. *Reduces security because it is not randomised* (nnethercote.github.io).             | Use when speed is critical and input is trusted, such as in compilers, build systems and offline data processing.                                                     |
| **`ahash`** (`AHashMap`)                       | AVX‑accelerated, fast for both small and large keys; uses randomised seeds but is more efficient than SipHash (nnethercote.github.io).                                    | Use for general performance‑critical code when a balance between speed and some randomness is desired.                                                                |
| **`fnv`** (`FnvHashMap`)                       | Simple Fowler–Noll–Vo hash; slower than ahash and less robust; rarely recommended by 2025.                                                                                | Legacy code or when other crates require it.                                                                                                                          |
| **`nohash_hasher`** (with `BuildNoHashHasher`) | Eliminates hashing for keys whose domain is already well‑distributed (e.g. integer newtypes); uses identity function and trusts key distribution (nnethercote.github.io). | Great for `HashMap<u64, V>` when keys are sequential or random; reduces CPU overhead but should *not* be used when keys are adversarial or not uniformly distributed. |

**Guideline:** Start with the default `HashMap` (SipHash) for safety. If profiling shows that hashing is a bottleneck *and* data is trusted, switch to `FxHashMap` or `AHashMap`. For integer keys where hashing is unnecessary, use `nohash_hasher` with `HashMap` to avoid hashing costs.

### 1.3 Creating `HashMap` in `const`/`static` contexts

Because `HashMap` uses random seeds, it cannot be constructed at compile‑time. In nightly 1.91 you must either:

1. Wrap the map inside `std::lazy::LazyLock` (or `once_cell::Lazy`) and build it at first use; **or**
2. Use `BuildHasherDefault` with a deterministic hasher (e.g. `FxHasher`).

Be aware that using a deterministic seed exposes your program to **Hash‑DoS attacks** (doc.rust-lang.org). If you need constant‑time initialisation *but not run‑time lookup*, consider using an array or the **`phf`** (perfect hash) crate.

---

## 2 Selecting the right data structure (typical cases)

Choosing the right container affects both performance and code clarity. The following table summarises typical scenarios and the recommended collection:

| Scenario                                                          | Recommended structure                                                     | Rationale & notes                                                                                                                                                                                                                                                                                                                    |
| ----------------------------------------------------------------- | ------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Key–value store with high performance and no ordering requirement | `HashMap<K, V>` (default hasher). Use `with_capacity(n)` to pre‑allocate. | Provides amortised *O(1)* insert/lookup and uses SwissTable for cache efficiency (doc.rust-lang.org). Pre‑allocating reduces re‑hashing overhead.                                                                                                                                                                                    |
| Key–value store with **stable ordering** (sorted by key)          | `BTreeMap<K, V>`                                                          | B‑trees store multiple keys per node, reducing heap allocations and improving cache locality but require linear search within nodes (≈ *B* × log n comparisons) (doc.rust-lang.org). Iterators yield items in *sorted* order with amortised *O(1)* per step. Use when you need range queries or ordered iteration.                   |
| Key–value store with **insertion order**                          | `indexmap::IndexMap<K, V>`                                                | Maintains the order of insertion regardless of hash values and uses a compact index without holes (docs.rs). Use for deterministic ordering or when iteration order matters.                                                                                                                                                         |
| Set of unique items **without** ordering                          | `HashSet<T>` (or `FxHashSet`)                                             | Implemented using `HashMap<T, ()>` (doc.rust-lang.org); same guidance as `HashMap`. Use `with_capacity` when you know the size.                                                                                                                                                                                                      |
| Set of unique items **with sorted order**                         | `BTreeSet<T>`                                                             | Balanced B‑tree; iterators yield items in order with worst‑case logarithmic and amortised constant time per item (doc.rust-lang.org). Good for sets requiring range queries.                                                                                                                                                         |
| **Dynamic array** (random access, contiguous)                     | `Vec<T>`                                                                  | Provides fast indexing and good cache locality (nnethercote.github.io). Use `reserve`/`with_capacity` to allocate upfront. To remove items efficiently without preserving order, use `swap_remove` (*O(1)*) instead of `remove` (*O(n)*); use `retain` when removing many items. Nightly 1.90 introduced `pop_if` and `try_with_capacity`, and these APIs remain available in 1.91. |
| Queue or double‑ended queue                                       | `VecDeque<T>`                                                             | Ring buffer; supports amortised *O(1)* pushes/pops at both ends and *O(1)* indexing (doc.rust-lang.org). Use `make_contiguous` before sorting or slicing. Nightly 1.90 introduced `pop_front_if`/`pop_back_if` and `try_with_capacity`; these remain available in 1.91.                                                                                                         |
| **Avoid unless required**                                         | `LinkedList<T>`                                                           | Constant‑time insertion/removal at ends but slow due to extra allocations and poor cache locality (doc.rust-lang.org). Use only when you need to splice/split lists without copying.                                                                                                                                                 |
| Priority queue / heap                                             | `BinaryHeap<T>`                                                           | Implements a max‑heap; `push`/`pop` are *O(log n)*, `peek` is *O(1)*. Building from a vector is *O(n)* and enables in‑place heap‑sort (doc.rust-lang.org).                                                                                                                                                                           |
| Sparse map with rarely removed items                              | `phf::Map<K, V>` (perfect hash)                                           | Static, read‑only maps known at compile time; no hashing overhead at runtime.                                                                                                                                                                                                                                                        |

### 2.1 Pre‑allocation and capacity management

Whenever you know (or can estimate) the number of elements, pre‑allocate using `with_capacity` or `reserve`. This avoids repeated reallocations and re‑hashings. **For `HashMap`/`HashSet`, iteration cost is proportional to the *capacity* (empty buckets are visited)** (doc.rust-lang.org); avoid maps with large capacity but few items if iterating frequently.

### 2.2 Memory vs speed trade‑offs

* **Vector vs LinkedList** – `Vec<T>` offers contiguous storage and far better cache behaviour. `LinkedList` should be avoided except when you need to move nodes between lists without copying (doc.rust-lang.org). `VecDeque` usually outperforms `LinkedList` for queue behaviour.
* **HashMap vs BTreeMap** – `HashMap` is generally faster for random lookups/inserts. `BTreeMap` offers predictable ordering and range queries but incurs extra key comparisons. Choose `BTreeMap` when ordering or range queries dominate.
* **Memory overhead** – Hash maps allocate buckets and may use more memory than `BTreeMap` for small sizes. B‑trees store several keys per node, improving locality but increasing comparison count.

#### 2.2.1 Static lookups: using a sorted `Vec` instead of a tree

When your key–value set is **static or infrequently modified**, an ordered vector (or slice) can outperform both `HashMap` and `BTreeMap` in terms of **memory footprint and cache locality**.  A contiguous array stores keys and values in one allocation; the CPU can prefetch consecutive elements, avoiding the pointer chasing inherent in tree structures.  In a discussion on r/rust, experienced Rustaceans noted that if a collection “will never be modified, it is always better to use a sorted slice (e.g. `Vec`, array or boxed slice) with binary search compared to BTree or any other kind of sorted tree”【884805162721633†L100-L107】 because all values share a single allocation and the contiguous layout improves cache locality and iteration speed【884805162721633†L100-L107】.

To implement this pattern:

* **Build a `Vec<(K, V)>`** with the key as the first element of each tuple.  Sort the vector by key (using `sort_by` or `sort_unstable_by`).  Alternatively, store keys and values in separate `Vec`s (structure-of-arrays) for even better locality when keys and values are accessed independently.
* **Perform lookups with binary search.**  Use `slice::binary_search_by` or the nightly `partition_point` method to locate a key.  If found, index into the vector to retrieve the associated value.  Because the vector is contiguous, binary search performs O(log n) comparisons, similar to a B‑tree node search but with far fewer pointer indirections.
* **Handle updates carefully.**  Insertion or removal requires shifting elements (O(n)).  For datasets that rarely change, this overhead is acceptable.  If modifications are frequent, prefer `BTreeMap` or `HashMap`.

Example:

```rust
// Build and sort a vector of (key, value) pairs
let mut entries: Vec<(u32, String)> = vec![
    (3, "three".into()),
    (1, "one".into()),
    (2, "two".into()),
];
entries.sort_unstable_by(|a, b| a.0.cmp(&b.0));

// Lookup using binary search
fn lookup(entries: &[(u32, String)], key: u32) -> Option<&String> {
    entries
        .binary_search_by(|entry| entry.0.cmp(&key))
        .ok()
        .map(|index| &entries[index].1)
}

assert_eq!(lookup(&entries, 2), Some(&"two".to_string()));
```

This technique is especially useful for **read‑heavy tables**, such as keyword lists, protocol registries or static mappings compiled into the binary.  It echoes the “memory hack” described by Figma engineers—replacing map structures with sorted arrays and performing binary search improved both memory usage and speed—while relying only on publicly documented behaviour of `Vec` and binary search.

### 2.3 Concurrent and thread‑safe access

The standard library collections are *not* thread‑safe. For concurrency, use:

* `parking_lot::Mutex` / `RwLock` – coarse‑grained locking around a map.
* **`dashmap`** – lock‑free concurrent hash map (per‑shard locks for fast reads/writes).
* **`flurry`** – Java‑style concurrent hash map.

---

## 3 Methodology for typical cases

1. **Identify access pattern & ordering requirement.** Random lookups? Sorted iteration? Deque operations? Use the table in §2.
2. **Estimate element count** and pre‑allocate (`with_capacity`, `reserve`). When unsure, *slightly* over‑allocate.
3. **Balance performance & security.** For untrusted input, keep SipHash. For trusted data, benchmark `FxHashMap` or `AHashMap`.
4. **Check determinism needs.** Use `BTreeMap` (sorted) or `IndexMap` (insertion) if iteration order matters.
5. **Leverage specialised methods.** Examples:

    * `Vec::swap_remove` – *O(1)* deletion without preserving order.
    * `Vec::retain` – efficient mass removal.
    * `VecDeque::make_contiguous` – obtain contiguous slice.
    * Nightly `pop_if` / `pop_front_if` / `pop_back_if` – conditional pops.
    * `HashMap::get_disjoint_mut` – obtain multiple mutable references; panics on duplicates.

---

## 4 Handling atypical cases

### 4.1 Static data compiled into the binary

Use **`phf`** or *static‑map* crates to generate a perfect hash for *read‑only* maps (O(1) lookup, zero runtime hashing). Ideal for keyword tables, protocol enums, etc.

### 4.2 Keys frequently replaced or swapped

Consider **`slotmap`** or **`generational_arena`**. They give stable indices even after insert/remove, perfect for ECS or LRU caches.

### 4.3 Custom types as keys

* Implement `Hash` + `Eq`; maintain invariant `k1 == k2 ⇒ hash(k1) == hash(k2)`.
* **Do not mutate key fields affecting equality** while the key is inside a map.
* For `BTreeMap`, implement `Ord` and avoid mutating fields that affect ordering (undefined behaviour).

### 4.4 Absence of `HashMap` in `const` contexts

Use `LazyLock`/`once_cell::Lazy` or adopt `phf`. If you *must* use a fixed hasher (`BuildHasherDefault<FxHasher>`) in a constant, accept the security trade‑off.

### 4.5 Huge datasets or memory‑mapped structures

* **sled** or **rocksdb** – persistent B‑tree/LSM‑tree databases.
* **memmap2** – mmap‑backed arrays for fast read‑only access.
* Use `hashbrown` directly for advanced configuration.

### 4.6 Deterministic performance (real‑time systems)

Avoid hash tables due to collision risk. Use `Vec`, `VecDeque`, `BTreeMap`, arrays, or `indexmap` with a fixed hasher. Pre‑allocate to fixed size.

---

## 5 Quality and performance considerations (2025)

* **Minimise allocations** – pre‑allocate, avoid intermediate `collect`s; return iterators where possible.
* **Prefer `&str` over `String`** when no ownership change is needed.
* **Avoid unnecessary clones/moves** – use references or slices.
* **Leverage iterators & closures** for clear, allocation‑free pipelines.
* **Profile before optimising** – use `cargo bench` or **Criterion** with realistic workloads. Micro‑benchmarks rarely predict production behaviour.

---

## 6 Summary

Rust’s standard library and ecosystem provide a rich set of collections:

* `HashMap`/`HashSet` – general‑purpose, secure (SipHash).
* `BTreeMap`/`BTreeSet` – sorted order & range queries.
* `Vec`/`VecDeque` – dynamic arrays & queues.
* `LinkedList` – niche splicing use‑cases only.
* `BinaryHeap` – priority queues.
* External crates – `indexmap`, `phf`, `slotmap`, `dashmap`, `ahash`, etc.

The methodology above helps decide *when* to use each, *how* to manage hashing to balance security and performance, and *how* to handle atypical cases such as compile‑time maps or massive datasets. Following these practices ensures **high‑quality, performant Rust code** aligned with **Best Practice 2025**.
