# Best Practices for Locking in Rust Nightly 1.91 (Best Practice 2025)

## Introduction

Rust’s ownership and borrowing model provide strong compile‑time guarantees against data races, but most real‑world programs still need run‑time synchronisation when multiple threads or tasks access shared state.  Locking primitives in Rust’s standard library (`std::sync`) and ecosystem have evolved significantly: the nightly 1.90 series introduced futex‑based implementations for fairer and faster mutexes and read–write locks and stabilised functions such as `clear_poison` to recover from lock poisoning.  These improvements carry forward into nightly 1.91, which also benefits from other runtime refinements.  Third‑party crates like `parking_lot` provide efficient mutexes and read–write locks without poisoning, and the Tokio runtime offers asynchronous mutexes (`tokio::sync::Mutex`) and RwLocks tailored to async tasks.  Best Practice 2025 emphasises writing concurrency code that is safe, high‑quality and performant.

---

## 1 Overview of Synchronization Primitives

### 1.1 `Mutex<T>`

* **Mutual exclusion** for data of type `T`. Acquire via `lock()`, yielding a `MutexGuard<T>` that releases on `Drop`.
* **Poisoning**: panics set a poisoned state. Recover with `clear_poison()` or `into_inner()` on the error.
* **Usage patterns**:

    * Scope-based unlocking: avoid holding guards across blocking operations.
    * Share across threads with `Arc<Mutex<T>>`.
* **Futex-based fairness**: Linux/Windows implementations use futexes for fairness and performance.

### 1.2 `RwLock<T>`

* **Multiple readers or one writer**; prefer when reads ≫ writes.
* Guards release on `Drop`, support poisoning and `clear_poison()`.
* **Std vs `parking_lot` vs Tokio**:

    * Std: fair but readers may starve writers.
    * `parking_lot`: task-fair, blocks new readers when writers wait; warns against recursive reads.
    * Tokio: write-preferring, queue head for writers; blocks new readers once a writer is waiting.

### 1.3 Condition Variables and Barriers

* **`Condvar`**: sleep until notified. Always use with the same `Mutex`; check predicates in a loop to handle spurious wake-ups.
* **`Barrier`**: block threads until all have arrived; useful for phase synchronization.

### 1.4 One-Time Initialization: `OnceLock` and `LazyLock`

* **`OnceLock<T>`**: safe one-time initialization; use `get_or_init`.
* **`LazyLock<T, F>`**: initializes on first access; blocks other threads during init; may appear leaked in profiling.

### 1.5 Asynchronous Primitives (`tokio::sync`)

* Async `Mutex`, `RwLock`: allow locks across `.await` but are slower and risk deadlocks if misused.
* `Notify` and watch channels replace `Condvar` in async contexts.
* `Semaphore` and async `Barrier` for concurrency limits and task synchronization.

---

## 2 Third-Party Crates

| Crate               | Features                                                                 |
| ------------------- | ------------------------------------------------------------------------ |
| `parking_lot`       | Fast `Mutex`, `RwLock`, `Condvar`; no poisoning; task-fair locking.      |
| `crossbeam`         | Channels, lock-free queues, `AtomicCell`, epoch-based memory management. |
| `dashmap`/`ArcSwap` | Concurrent hash maps and atomic swaps for read-mostly data.              |
| `spin`              | Spinlocks for very short critical sections; busy-wait only.              |

---

## 3 Typical Cases and Recommended Practices

### 3.1 Protecting Shared Mutable State (`Arc<Mutex<T>>`)

* Wrap shared data in `Arc<Mutex<T>>`.
* Handle `lock()` errors: avoid `unwrap()`; recover via `clear_poison()` or `into_inner()`.
* Keep critical sections small: lock, clone needed data, drop guard, then perform work.
* For simple counters/flags, prefer `AtomicUsize`/`AtomicBool`.

**Best Practice 2025:**

* Minimize lock duration; never perform blocking/I/O inside a critical section.
* In async code, release guards before `.await`.
* Profile contention with `perf`, flamegraphs or `parking_lot::deadlock::check_deadlock()`.
* Prefer `parking_lot` for high throughput and no poisoning.

### 3.2 Read-Mostly Data (`RwLock<T>`)

* Use when reads greatly outnumber writes.
* Acquire with `read()`/`write()`. Avoid recursive reads on `parking_lot::RwLock`.
* Upgrade by dropping read guard then acquiring write guard (no safe std upgrade).

**Best Practice 2025:**

* Ensure appropriate read/write ratio; otherwise use `Mutex`.
* Choose fair implementation (`parking_lot` or Tokio) to avoid writer starvation.
* Release guards before async awaits.

### 3.3 One-Time Initialization

* Use `OnceLock` or `LazyLock` instead of mutex for global/static data.

### 3.4 Condition Synchronization and Barriers

* Use `Condvar` with a loop-check on the predicate; call `notify_one()` or `notify_all()`.
* For async, prefer `tokio::sync::Notify` or watch channels.
* Use `Barrier` for thread rendezvous without busy-waiting.

### 3.5 Fairness and Deadlock Avoidance

* Consistent lock ordering: acquire multiple locks in a fixed order.
* Early unlocking: limit critical section size; drop guards promptly.
* Use `try_lock()` or timeouts to avoid indefinite waits.
* Use fair handover (`unlock_fair`) in `parking_lot` locks.
* Detect deadlocks with `parking_lot::deadlock::check_deadlock()`.

### 3.6 Synchronous vs Asynchronous Locks

* In async functions, drop sync guards before `.await`; compile errors prevent guards across awaits.
* Avoid third-party sync locks that implement `Send` for guards to prevent deadlocks.
* Use async locks only when necessary; they are slower.

**Best Practice 2025:**

* Prefer message passing (channels) to avoid locks entirely in async systems.
* Never hold locks across `.await`.
* Use `Notify` or watch for condition signaling.

### 3.7 Fine-Grained Locking

* Separate mutable and immutable data into distinct structures.
* Guard only small, mutable portions with locks; store read-only data in lock-free maps.

**Best Practice 2025:**

* Minimize lock scope and use fine-grained locks for scalability.

---

## 4 Atypical and Advanced Scenarios

### 4.1 Lock-Free and Message-Passing Architectures

* Use crossbeam channels and lock-free structures (e.g., `AtomicCell`, `ArcSwap`).
* Tasks own state; communicate via messages to eliminate contention.

### 4.2 Mixing Sync and Async Contexts

* Segregate data by access context; avoid holding sync locks in async code.
* Clone or channel data between contexts to prevent blocking the executor.

### 4.3 Spinlocks and Low-Latency Locks

* Use `spin` crate for extremely short, CPU-bound sections; never across blocking operations or in async code.

### 4.4 Semaphores and Rate Limiting

* Use `tokio::sync::Semaphore` or `async_semaphore` to bound concurrency via permits.

### 4.5 FFI and OS-Level Integration

* Isolate unsafe FFI code in small, documented modules.
* Integrate Rust locks with external primitives only when necessary.

### 4.6 Handling Long-Running Work in Locked Sections

* Copy or clone needed data, release the lock, then perform long-running work or I/O.

### 4.7 Recovering from Poisoned Locks

* Use `Mutex::clear_poison()` and `RwLock::clear_poison()` to reset poisoned state; validate data consistency.

---

## 5 Tools, Profiling and Performance Tuning

* **Profiling contention**: `perf`, Valgrind, Criterion, flamegraphs.
* **Deadlock detection**: `parking_lot::deadlock::check_deadlock()`.
* **Futex optimizations**: rely on nightly 1.91’s futex‑based mutexes and condvars.  The futex enhancements introduced in 1.90 remain available and continue to provide fair and fast locking behaviour.
* **Fairness tuning**: use `unlock_fair()` for lock handover.
* **Documentation**: clearly outline data sharing, protection and lock ordering.
* **Thread management**: prefer thread pools or async runtimes; avoid excessive OS threads.

---

## Conclusion

Rust’s concurrency primitives offer strong safety guarantees, but correctness and performance depend on careful design. Best Practice 2025 emphasizes minimal, fine‑grained locking, poison recovery, fairness and lock‑free or message‑passing architectures. Separate mutable and immutable data, never hold locks across `.await`, and profile to detect contention. These guidelines enable developers to leverage Rust nightly 1.91’s improved primitives—building on the futex optimizations and poisoning APIs from 1.90—for high‑quality, performant concurrent systems.
