# Asynchronous Programming and Coroutines in Rust nightly 1.91 – Best‑Practice 2025

## Introduction

Rust’s `async`/`await` model turns asynchronous functions into *zero‑cost* state machines.  An `async fn` returns a `Future` that implements `poll` and advances through its states when driven by an executor.  Because these coroutines are compiled down to efficient stackless state machines, they have little runtime overhead compared to hand‑written callbacks—hence “zero‑cost coroutines”.  However, asynchronous programming in Rust is primarily about **concurrency** (overlapping I/O or high‑latency operations) rather than raw CPU parallelism.  Using async for compute‑heavy workloads often degrades performance.

This handbook merges and supersedes the previous `async_handbook.md` and `zero_cost_corutines.md`.  It summarises current best practices for writing asynchronous Rust code on nightly 1.91 (as of August 2025).  New language features like **async closures** (stable since 1.85), **`async fn` in traits with Return Type Notation (RTN)** and tooling improvements such as guaranteed `Vec` capacity and safe non‑pointer `std::arch` intrinsics【684066008670463†L134-L140】 are highlighted where relevant.  When using nightly features, gate them appropriately and provide stable fallbacks.

---

## New language features in Rust 1.85–1.91

| Feature | Explanation & best‑practice use |
|--------|---------------------------------|
| **Async closures** | Inline `async` closures capture their environment and return futures.  They implement the `AsyncFn*` traits and can borrow captured state.  Use them for concise callbacks and when passing short asynchronous operations. |
| **`async fn` in traits and Return Type Notation (RTN)** | Nightly Rust allows `async fn` methods in traits.  They desugar to returning an opaque `impl Future`.  Because trait methods cannot add bounds after desugaring, you cannot impose `Send` on the returned future; consider using [trait_variant](https://docs.rs/trait-variant) or macro crates to generate both send and non‑send variants【188239465547906†L19-L48】.  On stable Rust use the `async-trait` crate or return boxed futures (`Box<dyn Future<Output = T> + Send>`). |
| **Cancellation tokens** | Use `tokio_util::sync::CancellationToken` to cooperatively cancel tasks.  Prefer token‑based cancellation over dropping a future; dropping cancels at an `.await` boundary but may leave partial state. |
| **Async iterators & generators** | `Stream` traits from `futures`/`tokio-stream` provide asynchronous sequences.  Use `StreamExt::for_each_concurrent` to process items with bounded concurrency.  The proposed core `AsyncIterator` trait and generator syntax remain unstable.  Until then, rely on `futures::stream` or macros like `async_stream`. |
| **Instrumentations and diagnostics** | Integrate [Tokio Console](https://github.com/tokio-rs/console), `tracing` and `tokio-metrics` to view task lifetimes, detect deadlocks and measure latencies.  Collect metrics in development to catch issues early. |

Nightly features evolve quickly; consult release notes and crate documentation for up‑to‑date syntax and stability.  Avoid relying on nightly features for public APIs without providing stable alternatives.

---

## When to use async

Async Rust shines in these scenarios:

* **Network servers and real‑time applications**: HTTP/HTTPS servers, proxies, message brokers and chat services.  Thousands of connections can be serviced with a handful of threads, reducing memory usage and context‑switch overhead.
* **I/O‑bound applications**: Database drivers, file I/O, cloud services and CLI tools that spend most time waiting on network or disk.
* **Concurrency tasks**: Timed events, scheduled jobs, periodic heartbeats via `tokio::time::interval`, and pipelines processing independent tasks concurrently.
* **Scalable services**: Applications requiring elasticity under load benefit from async architectures with backpressure and graceful shutdown.

Async is **not** ideal for CPU‑heavy computation.  Futures run on a small thread pool and blocking those threads (via CPU‑intensive work or synchronous I/O) starves other tasks.  For compute‑bound work use threads, Rayon or offload to a blocking pool (see §6).

---

## Choosing and configuring a runtime

Rust does not ship a built‑in executor.  Choose one runtime for your application and stick with it to avoid ecosystem friction.

### Tokio

Tokio is the most widely used async runtime.  It provides a multi‑threaded executor by default, but can also operate in single‑threaded mode.  Key points:

* **Thread model**: Multi‑threaded tasks must satisfy `Send + 'static`.  For tasks that only interact within one thread and should not be `Send`, build a current‑thread runtime via `Builder::new_current_thread()`.
* **Utilities**: Tokio exposes powerful primitives—`JoinSet`, `Semaphore`, `Notify`, channels, timers, file and network APIs.  Use `#[tokio::main]` or manually build a `Runtime`.
* **Instrumentation**: Integrate `tokio-console` and `tracing` for monitoring and debugging.  Use `tokio::spawn` for tasks that may run concurrently on worker threads and `spawn_blocking` for blocking or compute‑heavy operations.

### Smol and other runtimes

* **Smol** is a lightweight executor suitable for `no_std` environments and embedded use.  It spawns tasks on a small thread pool and does not require `Send` by default.  Combine with `async-io` and `futures` for I/O.
* **Embassy** targets bare‑metal devices with cooperative multitasking and no heap requirements.
* **Glommio** uses `io_uring` on Linux for low‑latency I/O and pins tasks to CPU cores.  Suitable for high‑throughput single‑threaded services.

**Do not mix runtimes** in the same program.  Choose one and design around its primitives.  For library code, accept generic traits like `AsyncRead`/`AsyncWrite` from `futures` instead of concrete types.

---

## Structured concurrency and task orchestration

Structured concurrency treats tasks as part of a hierarchy: when a parent completes or is cancelled, its children are automatically cancelled.  Use the following patterns to manage tasks:

### 1. Spawning and awaiting tasks

* `tokio::spawn(fut)` creates an independent task that runs concurrently on the executor.  It returns a `JoinHandle<T>` that must be `.await`ed to observe the task’s result.  Dropping the handle **does not** cancel the task; call `.abort()` or use cancellation tokens.
* `tokio::join!(fut1, fut2, …)` concurrently polls multiple futures within the current task.  It awaits all futures and returns a tuple of results.  Use when the number of futures is fixed at compile time.  Error handling (`?`) must occur after the `join!` call.
* `JoinSet` manages a dynamic collection of tasks and yields results as they complete.  Use when the number of child tasks is not known ahead of time.
* `FuturesUnordered` from the `futures` crate supports dynamic sets of homogeneous futures.  Poll it in a loop; it yields completed results in arrival order.

### 2. Races and timeouts

* Use `tokio::select!` (or `futures::select!`) to race multiple futures and act on whichever completes first.  Always pin long‑lived futures outside loops to avoid recreating them every iteration.  `select!` drops all non‑selected futures when a branch completes; ensure your futures are cancellation‑safe (no partial state persists after cancellation).
* Use `tokio::time::timeout(duration, future)` to impose timeouts.  Handle `Elapsed` errors and propagate cancellations appropriately.
* Combine channels with `select!` to handle graceful shutdown signals.

### 3. Controlling concurrency levels

* Use `tokio::sync::Semaphore` to limit the number of simultaneous tasks accessing a resource (e.g. open database connections or file descriptors).  Acquire a permit before starting work and release it on completion.
* For streams, use `StreamExt::buffer_unordered(n)` to process up to `n` items concurrently.  This provides backpressure when producers outrun consumers.
* Use `FuturesUnordered` or `JoinSet` with manual bounds for finer control; drop tasks or return errors when limits are exceeded.

### 4. Periodic and scheduled tasks

* Use `tokio::time::interval` to create a ticker.  Call `.tick().await` in a loop to execute work at regular intervals.  Pin the interval outside the loop.  Avoid `std::thread::sleep` in async code.
* For CPU‑heavy periodic work, wrap the computation in `spawn_blocking` or a Rayon job to avoid blocking the async runtime.

### 5. Cancellation and cleanup

* Every `.await` is a potential cancellation point.  If a future is dropped at that point, it stops executing.  Avoid holding locks, file handles or partially updated state across `.await` points; release resources before awaiting.
* Use `CancellationToken` from `tokio_util` to signal cancellation across multiple tasks.  Each task should check for cancellation and gracefully exit, flushing buffers or releasing locks.
* Provide explicit async `close()`/`shutdown()` methods for types that require asynchronous cleanup since `Drop::drop` is synchronous.

---

## Managing blocking and CPU‑bound work

### 1. `spawn_blocking`

Use `tokio::task::spawn_blocking` to offload blocking operations or CPU‑intensive work to a dedicated thread pool.  This prevents stalling the async runtime.  Limit the size of the blocking pool via `max_blocking_threads` in the runtime configuration.  For long computations, break the work into chunks and periodically yield to check for cancellation.

### 2. Thread pools and Rayon

For data‑parallel tasks, use **Rayon**.  Convert iterators to `par_iter()` and tune pool sizes via `ThreadPoolBuilder`.  Do **not** use Rayon primitives inside async tasks that run on the Tokio worker threads; call Rayon from within `spawn_blocking` to avoid blocking the reactor.

### 3. Single‑thread vs multi‑thread executors

Use a single‑threaded executor (Tokio’s current‑thread runtime or Smol) when tasks share lots of state and concurrency overhead is low.  This avoids `Send`/`Arc`/`Mutex` requirements.  For servers handling many independent connections or CPU‑bound offloading, prefer multi‑threaded executors.

---

## Limiting concurrency, backpressure and fairness

* Use semaphores and bounded channels to control the number of in‑flight tasks.  Exceeding limits should cause senders to wait or return an error; this implements backpressure.
* Use `buffer_unordered` or `for_each_concurrent(n, |item| async move { … })` to process streams with a maximum concurrency of `n`.
* Avoid unbounded task spawning; tasks that outlive their context leak memory and may prevent proper shutdown.  Use `JoinSet` or cancellation tokens to manage their lifetimes.

---

## Synchronisation primitives and shared state

### Choosing the right mutex

* **`tokio::sync::Mutex`/`RwLock`**: Use when holding a lock across `.await` points.  These async‑aware locks yield to the executor when waiting.  Acquire, modify and drop the guard before performing other awaits.
* **`std::sync::Mutex`/`RwLock`**: Use in synchronous code or when locks are never held across `.await` boundaries.  They are faster than their async counterparts but will block the executor if held inside an async function.
* Combine with `Arc<T>` for shared ownership in multi‑threaded code.

### Channels and notifications

| Primitive     | Use case                                                  |
|--------------|------------------------------------------------------------|
| `mpsc`       | Multi‑producer, single‑consumer pipelines with backpressure.  Clone `Sender` for multiple producers.  Use `try_send` to avoid blocking. |
| `broadcast`  | Publish/subscribe: send a message to all subscribers.  Subscribers read independently; drop messages when buffers are full. |
| `watch`      | Always holds the latest value; tasks await updates.  Useful for configuration reloads. |
| `oneshot`    | Single‑use channel for hand‑off.  Errors if receiver is dropped before sending. |
| `Notify`     | Lightweight event signalling; like a condition variable.  Not buffered, so repeated signals coalesce. |

Use typed message enums to capture multiple event types without allocation.  For high‑throughput channels, consider `crossbeam_channel` in synchronous contexts.

---

## Handling errors and panics

* Propagate errors via `Result<T, E>` and map to domain‑specific error types.  Avoid panicking in async tasks; a panic unwinds the future and aborts the task.  Tokio aborts the entire runtime on panic if configured via `abort_on_panic`.
* When using `join!` or `try_join!`, note that if any branch returns an error, the other futures are dropped.  Ensure they are cancellation‑safe.
* Wrap `spawn`ed tasks with `.catch_unwind()` if you need to convert panics into recoverable errors; handle `JoinError`.  Provide top‑level supervisors to respawn or log failed tasks.

---

## Pinning, `Unpin` and storing futures

* **Pinning**: Futures are self‑referential state machines.  They must not move in memory after they are polled because they may hold pointers to their own stack frames.  Use `Box::pin` or the `pin!` macro to pin a future on the heap or stack.  You can only obtain a `Pin<&mut T>` from a pinned future to poll it.
* **`Unpin`**: Types implementing `Unpin` can be moved even after being pinned; most simple futures are `Unpin`.  Types containing `self` references or generators are not.  When storing non‑`Unpin` futures (e.g. in `Vec<Pin<Box<dyn Future<…>>>>`), pin them once at creation.

---

## `async fn` in traits and dynamic dispatch

Using `async fn` in traits is now possible on nightly via Return Type Notation.  However, because the returned future type is opaque and no extra bounds can be added, you cannot require `Send` or `Sync` in the return position【188239465547906†L19-L48】.  For public traits consider:

* Using the [`async-trait`](https://docs.rs/async-trait) crate to desugar `async fn` in traits into boxed futures.  This adds a heap allocation but works on stable Rust.
* Returning `Box<dyn Future<Output = T> + Send + 'static>` explicitly.  This allows dynamic dispatch but monomorphises differently from generics.
* Generics: define a trait method returning `impl Future<Output = T>` with explicit lifetime parameters.  Consumers must be generic over the future type, increasing monomorphisation but avoiding heap allocation.
* Using the [trait-variant](https://docs.rs/trait-variant) crate to generate both `Send` and non‑`Send` variants of a trait【188239465547906†L60-L134】.

Document which executors your traits are designed for (e.g. “must be `Send` because it is spawned onto a multi‑threaded Tokio runtime”) and consider providing both sync and async variants of your API in separate modules.

---

## Non‑typical and advanced scenarios

### Dynamic dispatch of async functions

Use `async_trait` or trait objects to store heterogeneous async functions.  Box the returned futures when necessary.  Be aware of heap allocation and dynamic dispatch overhead.

### Asynchronous streams and generators

Use crates like `futures::stream`, `tokio_stream` or macros like `async_stream` to implement asynchronous streams.  To process streams concurrently, combine with `StreamExt::for_each_concurrent` or `buffer_unordered`.

### Bridging synchronous and asynchronous worlds

* Use `Runtime::block_on` to run an async function in a synchronous context.  Do not call `block_on` inside an async function; instead, spawn tasks or offload blocking code.
* Offload synchronous I/O or CPU‑bound functions to `spawn_blocking` and communicate results via channels.
* When integrating with FFI or other languages, provide synchronous wrappers around async functions and manage pinning and lifetimes carefully.

### `no_std` and embedded environments

Use runtimes like **Embassy** or **Smol** that support `no_std`.  Pre‑allocate buffers and avoid dynamic memory.  Map hardware interrupts and timers to async tasks.  Keep task hierarchies shallow and prefer static dispatch.

### Mixed runtimes and cross‑runtime libraries

Avoid mixing multiple executors (Tokio + Smol) in one process.  When writing libraries, depend on generic traits (`AsyncRead`, `AsyncWrite`, `Stream`, etc.) and accept a generic executor type so consumers can choose their own runtime.  Provide optional feature flags for Tokio or Smol implementations.

---

## Summary and recommendations

Asynchronous programming in Rust combines zero‑cost coroutines with explicit control over task scheduling and concurrency.  Use async for I/O‑bound workloads and orchestrate tasks via structured concurrency constructs (`join!`, `JoinSet`, `FuturesUnordered`, `select!`).  Limit concurrency with semaphores and bounded streams, and offload blocking or CPU‑heavy work using `spawn_blocking` or Rayon.  For shared state, choose appropriate mutexes and channels and drop locks before awaiting.  Handle cancellations explicitly via tokens and provide async cleanup methods.  When designing traits, consider `async fn` support on nightly and stable alternatives like `async-trait`.  Test concurrency code thoroughly, instrument with `tokio-console` and `tracing`, and profile to tune performance.  Following these guidelines will help you build reliable, scalable and maintainable async Rust applications in 2025.