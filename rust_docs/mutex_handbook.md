# Best‑Practice methodology for using mutexes in Rust 1.91 (nightly) – 2025 edition

Rust’s `Mutex<T>` is a mutual‑exclusion primitive that contains the data it guards.  It enforces exclusive access by returning a guard object (`MutexGuard`), which unlocks the mutex when dropped.  This design prevents data races and ensures RAII‑based unlocking; manually unlocking is unsafe.  On nightly 1.91 (stable Oct 2025), `std::sync::Mutex` remains the general‑purpose lock; alternative implementations (`parking_lot::Mutex`, `spin::Mutex`) trade off fairness, speed and behaviour.  Nightly 1.91 continues to use the futex‑based implementations introduced in 1.90 for fairer and faster mutexes, and no major API changes were made.

---

## 1 Selecting the right synchronization primitive

| Use case                                          | Recommended primitive                              | Notes                                                                                                             |
| ------------------------------------------------- | -------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| Shared mutable state accessed by multiple threads | `Arc<Mutex<T>>`                                    | Wrap data in `Mutex<T>` and share via `Arc`; each thread clones the `Arc` and calls `.lock()` for RAII unlocking. |
| Read-heavy shared state                           | `RwLock<T>`                                        | Allows multiple readers or one writer. Use `parking_lot::RwLock` for fair locking and lock elision.               |
| Simple counters or boolean flags                  | Atomic types (`AtomicUsize`, `AtomicBool`)         | Atomics avoid mutex overhead; use `fetch_add`, `compare_exchange`, etc.                                           |
| Synchronous code within async context             | `std::sync::Mutex` (if not held across `.await`)   | Use `tokio::sync::Mutex` only when holding guard across `.await`.                                                 |
| Uncontended, ultra-short critical sections        | Spin lock (`spin::Mutex`)                          | Avoid kernel scheduling; only slightly faster uncontended, slower under contention.                               |
| Performance-critical code                         | `parking_lot::Mutex`                               | Fast inline paths for uncontended locks, adaptive spinning and eventual fairness.                                 |
| High-priority or real-time threads                | `std::sync::Mutex` on OS with priority inheritance | Std mutex on macOS/BSD uses OS primitives with priority inheritance; others do not.                               |
| Reentrant locking                                 | `parking_lot::ReentrantMutex`                      | Supports recursive locking; use sparingly, prefer redesign.                                                       |
| Lock-free data structures                         | `crossbeam`, `dashmap`, `arc-swap`                 | Specialized lock-free or sharded structures eliminate bottlenecks.                                                |

---

## 2 Typical patterns and best practices

### 2.1 Shared counter or simple state

```rust
use std::sync::{Arc, Mutex};
use std::thread;

fn main() {
    let counter = Arc::new(Mutex::new(0));
    let mut handles = Vec::new();

    for _ in 0..10 {
        let ctr = Arc::clone(&counter);
        handles.push(thread::spawn(move || {
            let mut num = ctr.lock().unwrap();
            *num += 1;
        }));
    }

    for handle in handles {
        handle.join().unwrap();
    }
    println!("Counter: {}", *counter.lock().unwrap());
}
```

**Guidelines:**

* Clone `Arc` only when needed; avoid unnecessary atomic refcounting.
* Keep critical sections small: acquire lock, clone or copy data, then drop guard before heavy work.
* Never hold locks during I/O or long operations; release guard first.
* Prefer atomics for simple counters or flags instead of mutexes.
* Handle poisoning: use `match` on `lock()`, recover via `PoisonError::into_inner()`, avoid `unwrap()`.

### 2.2 Shared caches or collections

Protect maps or caches with `Arc<Mutex<Cache>>`:

* Use `HashMap` or `VecDeque` inside the mutex.
* Writers lock, modify, and immediately unlock.
* Readers lock, clone or copy data, and release guard.
* For read-dominated workloads, consider `RwLock<HashMap<K, V>>`; note overhead under heavy writes.

### 2.3 Worker pools and producer–consumer queues

Use a `VecDeque` protected by a mutex:

1. Lock and `pop_front()` a task while holding guard.
2. Release guard before processing to avoid blocking others.
3. If empty, release lock and wait or exit.
4. In async contexts, prefer message passing (`std::sync::mpsc`, `tokio::sync::mpsc`) over shared locks.

### 2.4 Mutexes in asynchronous code

* Do not hold a `MutexGuard` across `.await`; restructure code to release locks before awaiting.
* For locks spanning `.await`, use `tokio::sync::Mutex`, which suspends tasks rather than blocking threads.
* Encapsulate lock logic in synchronous methods that tasks call.

### 2.5 Avoiding deadlocks and managing lock scope

* Group related data under one lock instead of nested locks.
* Keep lock scopes small and obvious; avoid passing guards across functions.
* Never invoke unknown callbacks while holding locks.
* Avoid returning `MutexGuard` from functions.
* Document strict lock ordering if multiple locks are unavoidable.
* Integrate deadlock detection (`parking_lot::deadlock`, `no_deadlocks`, ThreadSanitizer) in CI.

### 2.6 Choosing between `std::sync::Mutex` and `parking_lot::Mutex`

| Aspect               | `std::sync::Mutex`          | `parking_lot::Mutex`                        |
| -------------------- | --------------------------- | ------------------------------------------- |
| Memory footprint     | Larger                      | 1 byte                                      |
| Uncontended path     | One atomic + kernel call    | Single atomic                               |
| Contention           | Kernel-based                | Adaptive spinning; faster under contention  |
| Fairness             | OS-dependent                | Eventual fairness; explicit `unlock_fair()` |
| Poisoning            | Yes (returns `PoisonError`) | No                                          |
| Priority inheritance | On some OSes                | No                                          |
| Static construction  | No                          | Yes                                         |

### 2.7 Lock-free and high-performance alternatives

* **Atomics / `AtomicCell`**: for simple flags and counters.
* **Shard-based maps**: `dashmap`, `evmap`, or `RwLock<HashMap>`.
* **Arc swap**: lock-free swapping of `Arc<T>`.
* **Channels**: prefer `std::sync::mpsc`, `crossbeam::channel`, or `tokio::sync::mpsc` over mutexes.

---

## 3 Non-typical or special cases

### 3.1 Real-time or latency-sensitive environments

* Minimize locks; use per-thread state.
* Spin locks for extremely short sections; always benchmark on target hardware.
* Use single-producer/single-consumer lock-free queues (`crossbeam::deque`).
* Choose OS primitives with priority inheritance or real-time mutexes via FFI when needed.

### 3.2 Embedded and `no_std` environments

* Use `critical-section` or `cortex_m::interrupt::free` to protect shared state via interrupt disabling.
* Prefer `bare_metal::Mutex` or `cortex_m::interrupt::Mutex` over `std::sync::Mutex`.
* Use `heapless::mpmc::Q` for message queues.

### 3.3 Recursive locking

* Use `parking_lot::ReentrantMutex` only when unavoidable; prefer code redesign.

### 3.4 Interacting with FFI / C code

* Do not hand `MutexGuard` to C; copy data, drop guard, then pass pointer.
* For C-side locking, use `parking_lot` raw APIs (`raw_lock`, `raw_unlock`) wrapped in `unsafe`.

### 3.5 Deadlock detection and debugging

* **`parking_lot::deadlock`**: hook to detect and log deadlocked threads.
* **`no_deadlocks` crate**: runtime lock-order checks.
* **ThreadSanitizer**: compile with `-Z sanitizer=thread` (nightly) or use external TSan.

---

## 4 Quality and performance checklist for 2025

* 🎯 **Question necessity**: prefer immutable data, atomics or channels over mutexes.
* 🔧 **Implementation choice**: use `parking_lot::Mutex` for performance; `std::sync::Mutex` for unwind-safety or priority inheritance.
* 🔒 **Encapsulation**: wrap shared data in `Mutex<T>` + `Arc<T>`; avoid global mutable statics.
* ⏱ **Scope**: keep critical sections short; no I/O or heavy work inside locks.
* 🚫 **Non-blocking**: use `try_lock()` to skip work if locked.
* 💡 **Async**: never hold locks across `.await`.
* 🛠 **Poisoning**: handle `PoisonError` gracefully.
* ❌ **No guard leaks**: do not return `MutexGuard`.
* 🌐 **Scalability**: use sharded or lock-free structures for high contention.
* 📊 **Profiling**: use `criterion`, `perf`, flamegraphs to find contention.
* 🔍 **Detection**: integrate `parking_lot::deadlock` or other deadlock tools in CI.
* 📚 **Documentation**: record which locks protect which data, lock ordering and invariants.

---

## 5 Conclusion

`Mutex<T>` in Rust 1.91 provides RAII‑based safety but does not prevent logical deadlocks.  Best Practice 2025 focuses on choosing the right primitive, minimising lock scope, handling poisoning, profiling contention and documenting concurrency.  For maximum performance, prefer `parking_lot` features, but choose `std::sync::Mutex` for priority inheritance or unwind safety.  By following these guidelines—using atomics and channels when possible, avoiding locks across `.await`, and employing detection tools—you can write safe, maintainable and high‑performance concurrent Rust code.
