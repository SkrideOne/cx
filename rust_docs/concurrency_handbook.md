# Best Practices for Concurrency in Rust (Nightly 1.90) – 2025 Methodology

Rust’s fearless concurrency ethos builds on strict ownership and borrowing rules to prevent data races at compile time.  Nightly 1.90 (stable planned for September 18 2025) doesn’t radically change the concurrency story but continues the evolution of stable APIs.  This guide distills current best practices and quality/performance considerations for writing concurrent Rust software in 2025.  The focus is on typical patterns and non‑typical (edge‑case) scenarios for both multi‑threading and asynchronous programming.

---

## 1 Principles of safe concurrency

### 1.1 Rust’s guarantees

Rust prevents data races by enforcing single ownership and restricting mutation through borrowing.  For shared, mutable data across threads, wrap values in `Arc<Mutex<T>>` (shared ownership via `Arc`, mutual exclusion via `Mutex`).  A thread must lock the mutex to modify data and release it after use—guaranteeing only one thread mutates the value at a time without a garbage collector.  The type system distinguishes mutable and immutable references: many `&T` or one `&mut T`, but not both simultaneously.

### 1.2 Minimise unsafe code

`unsafe` blocks bypass safety checks and should be confined to interfacing with low‑level C APIs or certain atomic operations.  Most concurrency tasks fit into safe Rust using the standard library or ecosystem crates—avoid `unsafe` unless profiling shows safe abstractions are insufficient.

### 1.3 Match the approach to the workload

* **I/O‑bound concurrency**: Use `async fn` with an async runtime (e.g. Tokio).  Async functions return `Future`s scheduled cooperatively without blocking threads.  Avoid synchronous blocking (e.g. `std::thread::sleep`) in async contexts; use non‑blocking equivalents like `tokio::time::sleep`.  Offload CPU‑heavy work in async code via `tokio::task::spawn_blocking` or `async_std::task::spawn_blocking`.
* **CPU‑bound concurrency**: Use threads (`std::thread`) or data‑parallelism crates (Rayon’s `par_iter()`).  Benchmark before parallelising—small workloads may regress due to thread overhead.  For high‑throughput, consider lock‑free structures (Crossbeam) or faster channels.

---

## 2 Typical concurrency patterns (2025)

### 2.1 Threads and shared state

* Spawn threads with `std::thread::spawn(...)`; always call `.join()` on handles to prevent premature exit of the main thread.
* Share state via `Arc<Mutex<T>>` or `Arc<RwLock<T>>`; hold locks only for minimal durations.
* Use message‑passing (`std::sync::mpsc` or `crossbeam_channel`) to avoid shared mutability.
* Employ scoped threads (`std::thread::scope`) to borrow stack data without `'static` lifetimes.

### 2.2 Asynchronous concurrency in Tokio

Rust’s async ecosystem emphasises structured concurrency: tasks have clear lifetimes, and parents await children.

#### 2.2.1 `tokio::join!`

Use for a fixed number of heterogeneous futures:

```rust
let (a, b) = tokio::join!(task1(), task2());
```

* Explicit futures only; perform error handling (`?`) after `join!`.
* Avoid blocking inside `join!` groups.

#### 2.2.2 `FuturesUnordered`

For dynamic, homogeneous tasks:

```rust
let mut stream = FuturesUnordered::new();
for id in ids {
    stream.push(fetch(id));
}
while let Some(res) = stream.next().await {
    // handle result
}
```

* Poll continuously to prevent starvation.
* All futures run on the same task thread—use `spawn` for CPU parallelism.

#### 2.2.3 `tokio::spawn` and `JoinSet`

* `tokio::spawn` offloads tasks (requires `'static` data) across runtime worker threads.
* Combine with `FuturesUnordered` for parallelism on multiple cores.
* `JoinSet` manages dynamic task collections:

```rust
let mut set = JoinSet::new();
for url in urls {
    set.spawn(fetch(url));
}
while let Some(res) = set.join_next().await {
    // handle result
}
```

#### 2.2.4 `tokio::select!`

Race futures or implement graceful shutdown:

```rust
tokio::select! {
    _ = ctrl_c() => break,
    _ = work() => {}
}
```

* Pin long‑lived futures outside loops.
* Ensure cancel‑safety (no inconsistent state on drop).
* Combine with `tokio::time::timeout` for timeouts.

#### 2.2.5 Controlling concurrency levels

* Use `buffer_unordered(n)` to limit concurrency in streams.
* Use `tokio::Semaphore` for fine‑grained resource control across tasks.

---

## 3 Common pitfalls and mitigations

| Pitfall                                   | Mitigation                                                      |
|-------------------------------------------|-----------------------------------------------------------------|
| Synchronous blocking in `async fn`        | Use non‑blocking APIs or `spawn_blocking`.                      |
| Forgetting `.await`                       | Always `.await` futures or store handles for later awaiting.    |
| Overusing `spawn`                         | Reserve for long‑running or CPU‑bound tasks; await small tasks. |
| Starvation in `FuturesUnordered`          | Offload heavy work; ensure continuous polling.                  |
| Deadlocks from held locks across `.await` | Limit lock scope; avoid `await` while holding mutexes.          |
| Unbounded concurrency                     | Use `buffer_unordered` or `Semaphore` to cap parallelism.       |

---

## 4 Non‑typical and advanced cases

### 4.1 Cancellation and clean shutdown

* Race tasks with a shutdown signal using `select!`.
* Use `CancellationToken` to propagate cancellations; ensure tasks flush data and release locks on cancellation.

### 4.2 Mixed async and sync code

* Isolate blocking code via `spawn_blocking`.
* Ensure FFI calls are thread‑safe; confine `unsafe` accordingly.

### 4.3 Data parallelism with Rayon

* Use `par_iter()` for CPU‑bound loops; benchmark for overhead.
* Avoid Rayon's blocking inside async contexts; call from blocking tasks.

### 4.4 Lock‑free and message‑passing structures

* Use Crossbeam’s channels and atomics for high‑throughput scenarios; verify `Send` and `Sync` traits.

### 4.5 Atomic operations & memory ordering

* Use `std::sync::atomic::{AtomicUsize, …}` for simple lock‑free state.
* Explicitly set ordering (`Relaxed`, `Acquire`, `Release`, `SeqCst`); `SeqCst` is safest but costliest.

### 4.6 Scoped concurrency in libraries

Some crates (e.g. `moro`) allow borrowing futures without `'static` lifetimes; ensure back‑pressure management.

### 4.7 Concurrency across processes & FFI

* Use OS primitives (shared memory, pipes, memory‑mapped files) with explicit synchronisation (atomics, semaphores).
* Employ cross‑platform crates (`tokio::process`) for async subprocess management.

---

## 5 Quality and performance recommendations

* **Benchmark & profile**: Use Criterion, Tokio Console and custom benchmarks to measure before/after changes.
* **Limit concurrency**: Deliberate caps via `buffer_unordered`, `Semaphore` or explicit permit counts.
* **Prefer immutable data & message passing**: It simplifies reasoning and avoids locks.
* **Handle errors & panics**: Always `.await` `JoinHandle` and handle `JoinError`.
* **Structured concurrency**: Encapsulate tasks with `JoinSet`, `join!`, `select!` for predictable lifetimes and cancellation.
* **Stay updated**: Review release notes and use `cargo‑audit` to track dependency security and deprecations.

---

## 6 Summary

Rust’s ownership model removes data races, but writing high‑quality concurrent code demands thoughtful design.  Match patterns to workloads, avoid blocking the async runtime, control concurrency levels, benchmark rigorously and employ structured concurrency.  By following these best practices for nightly 1.90, developers can build fast, reliable and scalable concurrent systems in 2025.