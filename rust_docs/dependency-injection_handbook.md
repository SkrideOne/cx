# Dependency Injection (DI) in Rust Nightly 1.90 – Best Practice 2025

Rust does not have a built‑in DI framework. Instead, DI is achieved by designing code around traits, generics and trait objects. In 2025 there are emerging frameworks (e.g. **shaku**, **teloc**, **pavex**) but most patterns rely on language primitives and careful API design. The following guide summarises best‑practice for high‑performance, quality DI in Rust nightly 1.90, based on current 2025 guidance.

---

## 1 General principles

| Principle                                  | Explanation                                                                                                                                                                                                                                                                                                                                                                                      |
| ------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Dependency inversion**                   | Depend on abstractions instead of concrete types. Use traits to define interfaces and inject implementations at construction time. This decouples modules and makes testing easier. ([codesignal.com](https://codesignal.com))                                                                                                                                                                   |
| **Choose static dispatch first**           | Generics (static dispatch) provide zero‑cost monomorphised code. They avoid dynamic dispatch overhead and allow the compiler to optimise away abstraction layers. Use them where performance matters and the number of implementations is limited. ([dev.to](https://dev.to))                                                                                                                    |
| **Use dynamic dispatch judiciously**       | Trait objects (`Box<dyn Trait>`, `Arc<dyn Trait>`) enable runtime selection of implementations and heterogeneous collections but introduce a vtable indirection and heap allocation. They reduce code bloat and compile times but add slight runtime overhead. ([dev.to](https://dev.to))                                                                                                        |
| **Limit trait visibility**                 | When a trait is public, all types referenced in its methods must also be public. This can break encapsulation and hamper dead‑code detection. Use the newtype pattern to wrap trait objects in a public struct and keep the trait private. ([jmmv.dev](https://jmmv.dev))                                                                                                                        |
| **Design APIs for testability**            | Traits and generics allow injecting mock implementations for testing. Always write unit tests for trait implementations. ([dev.to](https://dev.to))                                                                                                                                                                                                                                              |
| **Manage lifetimes explicitly**            | Decide whether dependencies should be transient, scoped or singleton. Use `OnceCell`/`async_once_cell` and `Arc`/`Rc` to control instantiation and sharing. Lazy instantiation avoids expensive initialisation unless needed. ([chesedo.me](https://chesedo.me))                                                                                                                                 |
| **Prefer compile‑time DI for performance** | Frameworks such as **shaku** and **pavex** perform DI at compile time. They avoid runtime reflection and dynamic dispatch and generate zero‑cost code. ([lpalmieri.com](https://lpalmieri.com))                                                                                                                                                                                                  |
| **Avoid over‑abstracting**                 | Only create traits for components that interact with the outside world (databases, file systems, networks) or complex functionality. Avoid hiding simple data types behind traits. ([jmmv.dev](https://jmmv.dev))                                                                                                                                                                                |
| **Use `async fn` in traits (nightly)**     | Nightly Rust 1.90 supports the `async_fn_in_trait` feature. You can write `async fn` directly in traits for static dispatch; it desugars to a generic associated type. However, traits with `async fn` are not yet dyn-safe; use the [`async-trait`](https://docs.rs/async-trait) crate when dynamic dispatch of async methods is required. ([rust-lang.github.io](https://rust-lang.github.io)) |

---

## 2 Typical cases and patterns

### 2.1 Injecting a concrete type (simple dependencies)

Use a factory method that returns the concrete type, then expose a public method that calls the factory. This hides instantiation details and avoids propagating generics.

```rust
struct ConfigManager { /* fields omitted */ }
impl ConfigManager {
    fn new() -> Self { /* ... */ }
}

struct Container;
impl Container {
    // private factory
    fn create_config(&self) -> ConfigManager {
        ConfigManager::new()
    }
    // public method returns the concrete type
    pub fn config(&self) -> ConfigManager {
        self.create_config()
    }
}
```

**Guidelines:**

* Use this pattern for dependencies with no alternate implementation (e.g., configuration objects, ID generators).
* Avoid generics; return the concrete type to keep the container easy to pass around.

### 2.2 Trait‑based dependencies (static dispatch)

Define a trait that specifies required behaviour, implement it for concrete types, and expose factory methods returning `impl Trait`. The compiler monomorphises calls, giving zero‑cost abstractions.

```rust
trait Logger {
    fn log(&self, message: &str);
}
struct ConsoleLogger;
impl Logger for ConsoleLogger {
    fn log(&self, message: &str) {
        println!("{}", message);
    }
}

struct Container;
impl Container {
    fn create_logger(&self) -> ConsoleLogger {
        ConsoleLogger
    }
    pub fn logger(&self) -> impl Logger {
        self.create_logger()
    }
}

fn run_app<L: Logger>(logger: L) {
    logger.log("app started");
}
```

**Guidelines:**

* Use when there is one implementation or when all call sites can remain generic.
* The public method returns `impl Trait` instead of exposing generics on the container, avoiding “generic invasion”.
* Because `impl Trait` is syntactic sugar for a hidden generic type, it cannot be used when multiple concrete implementations may be returned conditionally.

### 2.3 Dynamic trait dependencies (runtime selection)

When the concrete type depends on runtime configuration (e.g., choose between SQL or API), return a boxed trait object (`Box<dyn Trait>`). Provide a blanket implementation of the trait for `Box<T>` so that the boxed type implements the trait.

```rust
trait DataCollector {
    fn collect(&self) -> Vec<String>;
}
struct ApiCollector { /* fields omitted */ }
struct SqlCollector { /* fields omitted */ }
impl DataCollector for ApiCollector { /* ... */ }
impl DataCollector for SqlCollector { /* ... */ }

impl<T: DataCollector + ?Sized> DataCollector for Box<T> {
    fn collect(&self) -> Vec<String> { (**self).collect() }
}

struct Container {
    config: Config,
}
impl Container {
    fn create_collector(&self) -> Box<dyn DataCollector> {
        if self.config.use_api {
            Box::new(ApiCollector::new(self.config.api_key.clone()))
        } else {
            Box::new(SqlCollector::new(self.config.conn.clone()))
        }
    }
    pub fn collector(&self) -> Box<dyn DataCollector> {
        self.create_collector()
    }
}
```

**Guidelines:**

* Use when the implementation depends on runtime values.
* Box the trait object to give it a known size and allocate it on the heap.
* Implement the trait for `Box<T>` so that callers can use the boxed trait object transparently.
* Dynamic dispatch incurs a vtable lookup; in performance‑critical code prefer static dispatch unless heterogeneity is required.

### 2.4 Chained dependencies

A dependency may require other dependencies (e.g., a service depends on a database and a logger). Use a private `create_…` method that takes the required dependencies as arguments and a public method that resolves the dependencies and calls the factory.

```rust
trait Service { /* … */ }
struct ConcreteService<D: DataCollector, L: Logger> { /* … */ }
impl<D: DataCollector, L: Logger> Service for ConcreteService<D, L> { /* … */ }

impl Container {
    fn create_service<D: DataCollector, L: Logger>(
        &self,
        collector: D,
        logger: L,
    ) -> ConcreteService<D, L> {
        ConcreteService::new(collector, logger)
    }

    pub fn service(&self) -> impl Service {
        let collector = self.collector();
        let logger = self.logger();
        self.create_service(collector, logger)
    }
}
```

**Guidelines:**

* Keep private factory methods parameterised over dependencies; public methods gather dependencies via other public methods and call the factory.
* This pattern avoids generics in the public API and centralises dependency wiring.
* Use closures (`Fn` / `AsyncFn`) for lazy or optional dependencies so expensive dependencies are only constructed when used.

### 2.5 Lazy, scoped and singleton dependencies

Use `OnceCell` (or `async_once_cell` for async) with `Arc`/`Rc` to control instantiation and sharing. Provide methods `new_scope` or `new` to clone or reset cells for different lifetimes:

| Lifetime  | Pattern                                                                                                                                     | Example                                                                           |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| Transient | Create a new instance on every call; the default pattern for `impl` and boxed returns.                                                      | `pub fn collector(&self) -> impl DataCollector { self.create_collector() }`       |
| Scoped    | Use `OnceCell` to initialise a dependency once per scope; provide `new_scope` that resets the cell and stores scope‑specific configuration. | For example, a request-scoped database connection reused during a single request. |
| Singleton | Use `Rc<OnceCell<T>>` (or `Arc`) to share an initialised instance across scopes; clone the `Rc` when creating new scopes.                   | Used for configuration or database pools.                                         |

### 2.6 Asynchronous dependencies

Nightly Rust 1.90 allows writing `async fn` in traits for static dispatch. For dynamic dispatch of async methods, use the `async-trait` crate. When a dependency’s initialisation is async, propagate async through the call chain and use `async_once_cell` for lazy singletons.

```rust
use async_once_cell::OnceCell as AsyncOnceCell;

struct Container {
    logger: AsyncOnceCell<LoggerImpl>,
}
impl Container {
    async fn create_logger(&self) -> LoggerImpl {
        LoggerImpl::new().await
    }
    pub async fn logger(&self) -> impl Logger {
        // only initialises once
        self.logger.get_or_init(self.create_logger()).await
    }
}
```

**Guidelines:**

* For static dispatch, prefer `async fn` in trait when available; it desugars to a generic associated type and eliminates the need for macros.
* For dynamic dispatch of async methods, use `async-trait` but note it boxes the future and adds overhead.
* Replace `OnceCell` with `async_once_cell` when initialisation is asynchronous. Propagate async across all public methods in the call chain.

### 2.7 Newtype pattern to hide traits and maintain encapsulation

Traits used for DI must be public if exposed in public function signatures. To hide implementation details, wrap the trait object in a public struct and keep the trait private. This pattern prevents internal types from leaking and improves dead‑code detection.

```rust
// db.rs (private trait)
trait Db {
    fn put_log_entries(&self, entries: Vec<LogEntry>);
}
// public wrapper type
#[derive(Clone)]
pub struct Connection(Arc<dyn Db + Send + Sync>);
impl Connection {
    pub fn from_pg(opts: PgOpts) -> Self {
        Self(Arc::new(PostgresDb::connect(opts)))
    }
    pub fn from_sqlite(opts: SqliteOpts) -> Self {
        Self(Arc::new(SqliteDb::connect(opts)))
    }
}

// business logic only exposes Connection, not trait Db
pub fn init(conn: Connection) {
    // uses conn internally
}
```

**Guidelines:**

* Keep the trait `Db` and associated types private. Only the wrapper type `Connection` is public.
* Provide constructors on the wrapper to create concrete implementations.
* Use `Arc` for thread safety when shared across threads and `Rc` for single-threaded cases.

---

## 2.8 Using compile‑time DI frameworks

### 2.8.1 shaku

**shaku** is a compile‑time DI library that uses macros. It defines interface traits that must inherit `Interface` (ensuring `'static` and optionally `Send` + `Sync`) and provides macros to generate components and modules. Components are injected via `Arc<dyn Trait>` fields, and the `module!` macro wires them together. You can override components for testing at build time.

**Guidelines:**

* Structure your application into traits and concrete component structs. Define traits that extend `Interface` and implement `Component` for each concrete type.
* Express dependencies as `Arc<dyn Trait>` fields annotated with `#[shaku(inject)]`.
* Define a module using the `module!` macro listing components and providers.
* Build the module at application start and resolve components using `resolve_ref`.
* Use `with_component_override` to override components in tests or alternative environments.

### 2.8.2 pavex

**pavex** is an experimental compile‑time web framework. It takes a blueprint describing routes, constructors and lifecycles (singleton, request‑scoped, transient), then generates a full web server. It performs compile‑time DI by analysing the signature of handlers and constructors and building a dependency graph. The generated code has no dynamic dispatch or reflection; it simply calls functions with the correct arguments.

**Guidelines:**

* Provide constructor functions for each dependency and annotate their lifecycle (singleton, request‑scoped or transient).
* Use handler functions that declare their dependencies as parameters; pavex infers how to construct them.
* Compile the blueprint with `pavex_cli`, which generates a runtime crate. The generated code is a zero‑cost abstraction.
* Evaluate its ergonomics and maturity before adopting.

### 2.9 Dependency injection in web frameworks (Axum)

In **axum**, you can inject dependencies into handlers using the `State` extractor. You can choose between static dispatch (via generics) or dynamic dispatch (via trait objects) depending on flexibility and performance needs.

**Guidelines:**

* For simple apps, store a concrete type in `AppState` and inject it into handlers; this is straightforward but lacks flexibility.
* To allow swapping implementations, define a trait (e.g., `DB`) and parameterise `AppState` and handlers over the trait. Note that generics propagate through every handler and router builder, leading to verbose type signatures.
* Alternatively, use trait objects (`Arc<dyn DB + Send + Sync>`) in `AppState`. This removes generics from handler signatures and allows runtime selection. The performance overhead is usually negligible compared with I/O operations.

### 2.10 Testing and mocking dependencies

Use traits to inject mock implementations. Crates like **mockall** generate mocks for trait methods. When using shaku, override components in the module builder for tests. For manual DI, provide functions that accept generic trait implementations; in tests, pass in mock structs.

**Example using mockall:**

```rust
use mockall::mock;

mock! {
    pub Db {}
    impl Db for Db {
        fn put_log_entries(&self, entries: Vec<LogEntry>);
    }
}

fn process_logs(db: impl Db) {
    db.put_log_entries(vec![/*…*/]);
}

#[test]
fn test_process_logs() {
    let mut mock_db = MockDb::new();
    mock_db
        .expect_put_log_entries()
        .times(1)
        .withf(|entries| !entries.is_empty())
        .returning(|_| ());
    process_logs(mock_db);
}
```

---

## 3 Non‑typical cases and special patterns

### 3.1 Injecting closures or functions

Sometimes dependencies are functions or closures rather than traits. Use generic function types or `Fn`/`FnMut`/`FnOnce` trait bounds. For asynchronous closures, use the `AsyncFn` traits introduced in Rust 2024 and now available in nightly.

```rust
use std::future::Future;

fn create_repository<L, F>(logging_service_fn: F) -> impl Repository
where
    F: Fn() -> L,
    L: LoggingService,
{
    if should_log() {
        let logging_service = logging_service_fn();
        SqliteRepo::new_with_logging(logging_service)
    } else {
        SqliteRepo::new()
    }
}

async fn create_repo<'a, L, F>(logging_service_fn: F) -> impl Repository
where
    F: AsyncFn() -> L,
    L: LoggingService + 'a,
{
    if let Some(key) = maybe_key().await {
        let logging_service = logging_service_fn().await;
        ApiRepo::new(key, logging_service)
    } else {
        SqlRepo::new().await
    }
}
```

**Guidelines:**

* Use closures for optional dependencies or expensive instantiations.
* Use `AsyncFn` traits for async closures starting with Rust 2024.

### 3.2 Injecting generic types with lifetimes

When a dependency needs to borrow data with a lifetime (e.g., an iterator over a data source), use higher‑rank trait bounds or generic associated types (GATs).

```rust
trait Reader {
    type Iter<'a>: Iterator<Item = &'a str>
    where
        Self: 'a;
    fn iter<'a>(&'a self) -> Self::Iter<'a>;
}
fn process<R: Reader>(reader: &R) {
    for line in reader.iter() {
        /* ... */
    }
}
```

This allows returning an iterator that borrows from `self` without boxing. Avoid using trait objects for GATs as they are not object safe.

### 3.3 Mixing dynamic and static dispatch

Sometimes you need to store heterogeneous trait objects while still using generics for performance‑critical paths. One pattern is to wrap the trait object in an enum or trait alias and implement the trait for both the concrete type and the boxed version. Another pattern is to provide both generic and trait‑object based interfaces:

```rust
trait Storage { fn get(&self, key: &str) -> Option<String>; }
impl<T: Storage + ?Sized> Storage for &T { fn get(&self, key: &str) -> Option<String> { (**self).get(key) } }

struct FastStore<S: Storage> { store: S }
struct AnyStore { store: Box<dyn Storage> }

// Clients choose FastStore when the type is known at compile time or AnyStore when runtime flexibility is required.
```

### 3.4 Compile‑time macro‑generated containers

Large projects often suffer from repetitive boilerplate for the factory functions and public methods. Procedural macros can generate the container code. **teloc** is an example of a compile‑time DI library that uses macros to generate container structs and provider functions.

**Guidelines:**

* Keep the macro API simple; require the user to specify dependencies explicitly.
* Ensure macro‑generated code fails at compile time when dependencies are missing; this preserves Rust’s safety guarantee.
* Avoid hiding complex logic in macros; produce straightforward, debuggable code.

### 3.5 Sharing state across async tasks and threads

Use `Arc<dyn Trait + Send + Sync>` for thread‑safe sharing of trait objects within asynchronous runtimes. For synchronous single‑threaded contexts, `Rc<dyn Trait>` suffices. When injecting state into asynchronous tasks (e.g., `tokio::spawn`), ensure dependencies implement `Send` (or wrap them in `Arc` or `Mutex`).

### 3.6 Interop with external frameworks

When integrating with frameworks (e.g., **actix-web**, **axum**, **bevy**), follow the framework’s recommended patterns for DI:

* In **axum**, use the `State` extractor.
* In **actix-web**, store shared state in `web::Data<T>`; for dynamic dispatch, store `Arc<dyn Trait>`.
* In **bevy**, prefer using the ECS resources system; define resources and systems that depend on them; avoid DI frameworks.

### 3.7 Safety and performance pitfalls

* Don’t leak internal types. Wrap traits in newtypes to maintain encapsulation.
* Avoid overusing `async-trait`. Prefer `async_fn_in_trait` (nightly) or GAT patterns for static dispatch.
* Beware of generics explosion; consider trait objects or newtype‑trait patterns to contain generics.
* Ensure `OnceCell` initialisation is safe; avoid deadlocks or reentrancy issues. Use `async_once_cell` for async contexts.
* When sharing dependencies across threads, ensure they implement `Send` and `Sync` or are protected by synchronization primitives.

---

## 4 Summary of best‑practice guidelines

* Use traits for abstractions and inject implementations via generics or trait objects. Choose static dispatch for performance, dynamic dispatch for flexibility.
* Minimise trait visibility. Use the newtype pattern to hide trait objects and their associated types.
* Design container functions with private factories and simple public methods returning either concrete types, `impl Trait` or boxed trait objects. This centralises wiring and prevents generics invasion.
* Control lifetimes using transient, scoped and singleton patterns with `OnceCell`/`async_once_cell` and `Arc`/`Rc`.
* Use `async fn` in traits for static dispatch of asynchronous methods (nightly). For dynamic dispatch, use `async-trait` but be aware of the cost.
* Prefer compile‑time DI frameworks like **shaku** or **pavex** for large systems; they generate zero‑cost code and catch missing dependencies at compile time.
* For web applications, use the framework’s state‑injection mechanisms with trait objects for flexibility and generics for performance where needed.
* Test with mocks. Provide mock implementations of traits or override components in DI frameworks. Use crates like **mockall** for automatic mock generation.
* Avoid over‑abstracting or hiding trivial data. Only abstract external side effects or complex logic.
* Measure and profile. When in doubt, measure the performance impact of dynamic dispatch or compile‑time DI; choose the simplest solution that meets your requirements.

---

## 5 Looking forward

Rust nightly 1.90 introduces incremental improvements such as `async fn` in traits and generic associated types, making DI easier and more expressive. Future versions may stabilise dynamic async traits and improved trait upcasting. Keep an eye on the Rust release notes and RFCs for changes. For now, the above patterns provide a robust foundation for high‑quality, performant DI in modern Rust.
# Dependency Injection (DI) in Rust Nightly 1.90 – Best Practice 2025

Rust does not have a built‑in DI framework. Instead, DI is achieved by designing code around traits, generics and trait objects. In 2025 there are emerging frameworks (e.g. **shaku**, **teloc**, **pavex**) but most patterns rely on language primitives and careful API design. The following guide summarises best‑practice for high‑performance, quality DI in Rust nightly 1.90, based on current 2025 guidance.

---

## 1 General principles

| Principle                                  | Explanation                                                                                                                                                                                                                                                                                                                                                                                      |
| ------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Dependency inversion**                   | Depend on abstractions instead of concrete types. Use traits to define interfaces and inject implementations at construction time. This decouples modules and makes testing easier. ([codesignal.com](https://codesignal.com))                                                                                                                                                                   |
| **Choose static dispatch first**           | Generics (static dispatch) provide zero‑cost monomorphised code. They avoid dynamic dispatch overhead and allow the compiler to optimise away abstraction layers. Use them where performance matters and the number of implementations is limited. ([dev.to](https://dev.to))                                                                                                                    |
| **Use dynamic dispatch judiciously**       | Trait objects (`Box<dyn Trait>`, `Arc<dyn Trait>`) enable runtime selection of implementations and heterogeneous collections but introduce a vtable indirection and heap allocation. They reduce code bloat and compile times but add slight runtime overhead. ([dev.to](https://dev.to))                                                                                                        |
| **Limit trait visibility**                 | When a trait is public, all types referenced in its methods must also be public. This can break encapsulation and hamper dead‑code detection. Use the newtype pattern to wrap trait objects in a public struct and keep the trait private. ([jmmv.dev](https://jmmv.dev))                                                                                                                        |
| **Design APIs for testability**            | Traits and generics allow injecting mock implementations for testing. Always write unit tests for trait implementations. ([dev.to](https://dev.to))                                                                                                                                                                                                                                              |
| **Manage lifetimes explicitly**            | Decide whether dependencies should be transient, scoped or singleton. Use `OnceCell`/`async_once_cell` and `Arc`/`Rc` to control instantiation and sharing. Lazy instantiation avoids expensive initialisation unless needed. ([chesedo.me](https://chesedo.me))                                                                                                                                 |
| **Prefer compile‑time DI for performance** | Frameworks such as **shaku** and **pavex** perform DI at compile time. They avoid runtime reflection and dynamic dispatch and generate zero‑cost code. ([lpalmieri.com](https://lpalmieri.com))                                                                                                                                                                                                  |
| **Avoid over‑abstracting**                 | Only create traits for components that interact with the outside world (databases, file systems, networks) or complex functionality. Avoid hiding simple data types behind traits. ([jmmv.dev](https://jmmv.dev))                                                                                                                                                                                |
| **Use `async fn` in traits (nightly)**     | Nightly Rust 1.90 supports the `async_fn_in_trait` feature. You can write `async fn` directly in traits for static dispatch; it desugars to a generic associated type. However, traits with `async fn` are not yet dyn-safe; use the [`async-trait`](https://docs.rs/async-trait) crate when dynamic dispatch of async methods is required. ([rust-lang.github.io](https://rust-lang.github.io)) |

---

## 2 Typical cases and patterns

### 2.1 Injecting a concrete type (simple dependencies)

Use a factory method that returns the concrete type, then expose a public method that calls the factory. This hides instantiation details and avoids propagating generics.

```rust
struct ConfigManager { /* fields omitted */ }
impl ConfigManager {
    fn new() -> Self { /* ... */ }
}

struct Container;
impl Container {
    // private factory
    fn create_config(&self) -> ConfigManager {
        ConfigManager::new()
    }
    // public method returns the concrete type
    pub fn config(&self) -> ConfigManager {
        self.create_config()
    }
}
```

**Guidelines:**

* Use this pattern for dependencies with no alternate implementation (e.g., configuration objects, ID generators).
* Avoid generics; return the concrete type to keep the container easy to pass around.

### 2.2 Trait‑based dependencies (static dispatch)

Define a trait that specifies required behaviour, implement it for concrete types, and expose factory methods returning `impl Trait`. The compiler monomorphises calls, giving zero‑cost abstractions.

```rust
trait Logger {
    fn log(&self, message: &str);
}
struct ConsoleLogger;
impl Logger for ConsoleLogger {
    fn log(&self, message: &str) {
        println!("{}", message);
    }
}

struct Container;
impl Container {
    fn create_logger(&self) -> ConsoleLogger {
        ConsoleLogger
    }
    pub fn logger(&self) -> impl Logger {
        self.create_logger()
    }
}

fn run_app<L: Logger>(logger: L) {
    logger.log("app started");
}
```

**Guidelines:**

* Use when there is one implementation or when all call sites can remain generic.
* The public method returns `impl Trait` instead of exposing generics on the container, avoiding “generic invasion”.
* Because `impl Trait` is syntactic sugar for a hidden generic type, it cannot be used when multiple concrete implementations may be returned conditionally.

### 2.3 Dynamic trait dependencies (runtime selection)

When the concrete type depends on runtime configuration (e.g., choose between SQL or API), return a boxed trait object (`Box<dyn Trait>`). Provide a blanket implementation of the trait for `Box<T>` so that the boxed type implements the trait.

```rust
trait DataCollector {
    fn collect(&self) -> Vec<String>;
}
struct ApiCollector { /* fields omitted */ }
struct SqlCollector { /* fields omitted */ }
impl DataCollector for ApiCollector { /* ... */ }
impl DataCollector for SqlCollector { /* ... */ }

impl<T: DataCollector + ?Sized> DataCollector for Box<T> {
    fn collect(&self) -> Vec<String> { (**self).collect() }
}

struct Container {
    config: Config,
}
impl Container {
    fn create_collector(&self) -> Box<dyn DataCollector> {
        if self.config.use_api {
            Box::new(ApiCollector::new(self.config.api_key.clone()))
        } else {
            Box::new(SqlCollector::new(self.config.conn.clone()))
        }
    }
    pub fn collector(&self) -> Box<dyn DataCollector> {
        self.create_collector()
    }
}
```

**Guidelines:**

* Use when the implementation depends on runtime values.
* Box the trait object to give it a known size and allocate it on the heap.
* Implement the trait for `Box<T>` so that callers can use the boxed trait object transparently.
* Dynamic dispatch incurs a vtable lookup; in performance‑critical code prefer static dispatch unless heterogeneity is required.

### 2.4 Chained dependencies

A dependency may require other dependencies (e.g., a service depends on a database and a logger). Use a private `create_…` method that takes the required dependencies as arguments and a public method that resolves the dependencies and calls the factory.

```rust
trait Service { /* … */ }
struct ConcreteService<D: DataCollector, L: Logger> { /* … */ }
impl<D: DataCollector, L: Logger> Service for ConcreteService<D, L> { /* … */ }

impl Container {
    fn create_service<D: DataCollector, L: Logger>(
        &self,
        collector: D,
        logger: L,
    ) -> ConcreteService<D, L> {
        ConcreteService::new(collector, logger)
    }

    pub fn service(&self) -> impl Service {
        let collector = self.collector();
        let logger = self.logger();
        self.create_service(collector, logger)
    }
}
```

**Guidelines:**

* Keep private factory methods parameterised over dependencies; public methods gather dependencies via other public methods and call the factory.
* This pattern avoids generics in the public API and centralises dependency wiring.
* Use closures (`Fn` / `AsyncFn`) for lazy or optional dependencies so expensive dependencies are only constructed when used.

### 2.5 Lazy, scoped and singleton dependencies

Use `OnceCell` (or `async_once_cell` for async) with `Arc`/`Rc` to control instantiation and sharing. Provide methods `new_scope` or `new` to clone or reset cells for different lifetimes:

| Lifetime  | Pattern                                                                                                                                     | Example                                                                           |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| Transient | Create a new instance on every call; the default pattern for `impl` and boxed returns.                                                      | `pub fn collector(&self) -> impl DataCollector { self.create_collector() }`       |
| Scoped    | Use `OnceCell` to initialise a dependency once per scope; provide `new_scope` that resets the cell and stores scope‑specific configuration. | For example, a request-scoped database connection reused during a single request. |
| Singleton | Use `Rc<OnceCell<T>>` (or `Arc`) to share an initialised instance across scopes; clone the `Rc` when creating new scopes.                   | Used for configuration or database pools.                                         |

### 2.6 Asynchronous dependencies

Nightly Rust 1.90 allows writing `async fn` in traits for static dispatch. For dynamic dispatch of async methods, use the `async-trait` crate. When a dependency’s initialisation is async, propagate async through the call chain and use `async_once_cell` for lazy singletons.

```rust
use async_once_cell::OnceCell as AsyncOnceCell;

struct Container {
    logger: AsyncOnceCell<LoggerImpl>,
}
impl Container {
    async fn create_logger(&self) -> LoggerImpl {
        LoggerImpl::new().await
    }
    pub async fn logger(&self) -> impl Logger {
        // only initialises once
        self.logger.get_or_init(self.create_logger()).await
    }
}
```

**Guidelines:**

* For static dispatch, prefer `async fn` in trait when available; it desugars to a generic associated type and eliminates the need for macros.
* For dynamic dispatch of async methods, use `async-trait` but note it boxes the future and adds overhead.
* Replace `OnceCell` with `async_once_cell` when initialisation is asynchronous. Propagate async across all public methods in the call chain.

### 2.7 Newtype pattern to hide traits and maintain encapsulation

Traits used for DI must be public if exposed in public function signatures. To hide implementation details, wrap the trait object in a public struct and keep the trait private. This pattern prevents internal types from leaking and improves dead‑code detection.

```rust
// db.rs (private trait)
trait Db {
    fn put_log_entries(&self, entries: Vec<LogEntry>);
}
// public wrapper type
#[derive(Clone)]
pub struct Connection(Arc<dyn Db + Send + Sync>);
impl Connection {
    pub fn from_pg(opts: PgOpts) -> Self {
        Self(Arc::new(PostgresDb::connect(opts)))
    }
    pub fn from_sqlite(opts: SqliteOpts) -> Self {
        Self(Arc::new(SqliteDb::connect(opts)))
    }
}

// business logic only exposes Connection, not trait Db
pub fn init(conn: Connection) {
    // uses conn internally
}
```

**Guidelines:**

* Keep the trait `Db` and associated types private. Only the wrapper type `Connection` is public.
* Provide constructors on the wrapper to create concrete implementations.
* Use `Arc` for thread safety when shared across threads and `Rc` for single-threaded cases.

---

## 2.8 Using compile‑time DI frameworks

### 2.8.1 shaku

**shaku** is a compile‑time DI library that uses macros. It defines interface traits that must inherit `Interface` (ensuring `'static` and optionally `Send` + `Sync`) and provides macros to generate components and modules. Components are injected via `Arc<dyn Trait>` fields, and the `module!` macro wires them together. You can override components for testing at build time.

**Guidelines:**

* Structure your application into traits and concrete component structs. Define traits that extend `Interface` and implement `Component` for each concrete type.
* Express dependencies as `Arc<dyn Trait>` fields annotated with `#[shaku(inject)]`.
* Define a module using the `module!` macro listing components and providers.
* Build the module at application start and resolve components using `resolve_ref`.
* Use `with_component_override` to override components in tests or alternative environments.

### 2.8.2 pavex

**pavex** is an experimental compile‑time web framework. It takes a blueprint describing routes, constructors and lifecycles (singleton, request‑scoped, transient), then generates a full web server. It performs compile‑time DI by analysing the signature of handlers and constructors and building a dependency graph. The generated code has no dynamic dispatch or reflection; it simply calls functions with the correct arguments.

**Guidelines:**

* Provide constructor functions for each dependency and annotate their lifecycle (singleton, request‑scoped or transient).
* Use handler functions that declare their dependencies as parameters; pavex infers how to construct them.
* Compile the blueprint with `pavex_cli`, which generates a runtime crate. The generated code is a zero‑cost abstraction.
* Evaluate its ergonomics and maturity before adopting.

### 2.9 Dependency injection in web frameworks (Axum)

In **axum**, you can inject dependencies into handlers using the `State` extractor. You can choose between static dispatch (via generics) or dynamic dispatch (via trait objects) depending on flexibility and performance needs.

**Guidelines:**

* For simple apps, store a concrete type in `AppState` and inject it into handlers; this is straightforward but lacks flexibility.
* To allow swapping implementations, define a trait (e.g., `DB`) and parameterise `AppState` and handlers over the trait. Note that generics propagate through every handler and router builder, leading to verbose type signatures.
* Alternatively, use trait objects (`Arc<dyn DB + Send + Sync>`) in `AppState`. This removes generics from handler signatures and allows runtime selection. The performance overhead is usually negligible compared with I/O operations.

### 2.10 Testing and mocking dependencies

Use traits to inject mock implementations. Crates like **mockall** generate mocks for trait methods. When using shaku, override components in the module builder for tests. For manual DI, provide functions that accept generic trait implementations; in tests, pass in mock structs.

**Example using mockall:**

```rust
use mockall::mock;

mock! {
    pub Db {}
    impl Db for Db {
        fn put_log_entries(&self, entries: Vec<LogEntry>);
    }
}

fn process_logs(db: impl Db) {
    db.put_log_entries(vec![/*…*/]);
}

#[test]
fn test_process_logs() {
    let mut mock_db = MockDb::new();
    mock_db
        .expect_put_log_entries()
        .times(1)
        .withf(|entries| !entries.is_empty())
        .returning(|_| ());
    process_logs(mock_db);
}
```

---

## 3 Non‑typical cases and special patterns

### 3.1 Injecting closures or functions

Sometimes dependencies are functions or closures rather than traits. Use generic function types or `Fn`/`FnMut`/`FnOnce` trait bounds. For asynchronous closures, use the `AsyncFn` traits introduced in Rust 2024 and now available in nightly.

```rust
use std::future::Future;

fn create_repository<L, F>(logging_service_fn: F) -> impl Repository
where
    F: Fn() -> L,
    L: LoggingService,
{
    if should_log() {
        let logging_service = logging_service_fn();
        SqliteRepo::new_with_logging(logging_service)
    } else {
        SqliteRepo::new()
    }
}

async fn create_repo<'a, L, F>(logging_service_fn: F) -> impl Repository
where
    F: AsyncFn() -> L,
    L: LoggingService + 'a,
{
    if let Some(key) = maybe_key().await {
        let logging_service = logging_service_fn().await;
        ApiRepo::new(key, logging_service)
    } else {
        SqlRepo::new().await
    }
}
```

**Guidelines:**

* Use closures for optional dependencies or expensive instantiations.
* Use `AsyncFn` traits for async closures starting with Rust 2024.

### 3.2 Injecting generic types with lifetimes

When a dependency needs to borrow data with a lifetime (e.g., an iterator over a data source), use higher‑rank trait bounds or generic associated types (GATs).

```rust
trait Reader {
    type Iter<'a>: Iterator<Item = &'a str>
    where
        Self: 'a;
    fn iter<'a>(&'a self) -> Self::Iter<'a>;
}
fn process<R: Reader>(reader: &R) {
    for line in reader.iter() {
        /* ... */
    }
}
```

This allows returning an iterator that borrows from `self` without boxing. Avoid using trait objects for GATs as they are not object safe.

### 3.3 Mixing dynamic and static dispatch

Sometimes you need to store heterogeneous trait objects while still using generics for performance‑critical paths. One pattern is to wrap the trait object in an enum or trait alias and implement the trait for both the concrete type and the boxed version. Another pattern is to provide both generic and trait‑object based interfaces:

```rust
trait Storage { fn get(&self, key: &str) -> Option<String>; }
impl<T: Storage + ?Sized> Storage for &T { fn get(&self, key: &str) -> Option<String> { (**self).get(key) } }

struct FastStore<S: Storage> { store: S }
struct AnyStore { store: Box<dyn Storage> }

// Clients choose FastStore when the type is known at compile time or AnyStore when runtime flexibility is required.
```

### 3.4 Compile‑time macro‑generated containers

Large projects often suffer from repetitive boilerplate for the factory functions and public methods. Procedural macros can generate the container code. **teloc** is an example of a compile‑time DI library that uses macros to generate container structs and provider functions.

**Guidelines:**

* Keep the macro API simple; require the user to specify dependencies explicitly.
* Ensure macro‑generated code fails at compile time when dependencies are missing; this preserves Rust’s safety guarantee.
* Avoid hiding complex logic in macros; produce straightforward, debuggable code.

### 3.5 Sharing state across async tasks and threads

Use `Arc<dyn Trait + Send + Sync>` for thread‑safe sharing of trait objects within asynchronous runtimes. For synchronous single‑threaded contexts, `Rc<dyn Trait>` suffices. When injecting state into asynchronous tasks (e.g., `tokio::spawn`), ensure dependencies implement `Send` (or wrap them in `Arc` or `Mutex`).

### 3.6 Interop with external frameworks

When integrating with frameworks (e.g., **actix-web**, **axum**, **bevy**), follow the framework’s recommended patterns for DI:

* In **axum**, use the `State` extractor.
* In **actix-web**, store shared state in `web::Data<T>`; for dynamic dispatch, store `Arc<dyn Trait>`.
* In **bevy**, prefer using the ECS resources system; define resources and systems that depend on them; avoid DI frameworks.

### 3.7 Safety and performance pitfalls

* Don’t leak internal types. Wrap traits in newtypes to maintain encapsulation.
* Avoid overusing `async-trait`. Prefer `async_fn_in_trait` (nightly) or GAT patterns for static dispatch.
* Beware of generics explosion; consider trait objects or newtype‑trait patterns to contain generics.
* Ensure `OnceCell` initialisation is safe; avoid deadlocks or reentrancy issues. Use `async_once_cell` for async contexts.
* When sharing dependencies across threads, ensure they implement `Send` and `Sync` or are protected by synchronization primitives.

---

## 4 Summary of best‑practice guidelines

* Use traits for abstractions and inject implementations via generics or trait objects. Choose static dispatch for performance, dynamic dispatch for flexibility.
* Minimise trait visibility. Use the newtype pattern to hide trait objects and their associated types.
* Design container functions with private factories and simple public methods returning either concrete types, `impl Trait` or boxed trait objects. This centralises wiring and prevents generics invasion.
* Control lifetimes using transient, scoped and singleton patterns with `OnceCell`/`async_once_cell` and `Arc`/`Rc`.
* Use `async fn` in traits for static dispatch of asynchronous methods (nightly). For dynamic dispatch, use `async-trait` but be aware of the cost.
* Prefer compile‑time DI frameworks like **shaku** or **pavex** for large systems; they generate zero‑cost code and catch missing dependencies at compile time.
* For web applications, use the framework’s state‑injection mechanisms with trait objects for flexibility and generics for performance where needed.
* Test with mocks. Provide mock implementations of traits or override components in DI frameworks. Use crates like **mockall** for automatic mock generation.
* Avoid over‑abstracting or hiding trivial data. Only abstract external side effects or complex logic.
* Measure and profile. When in doubt, measure the performance impact of dynamic dispatch or compile‑time DI; choose the simplest solution that meets your requirements.

---

## 5 Looking forward

Rust nightly 1.90 introduces incremental improvements such as `async fn` in traits and generic associated types, making DI easier and more expressive. Future versions may stabilise dynamic async traits and improved trait upcasting. Keep an eye on the Rust release notes and RFCs for changes. For now, the above patterns provide a robust foundation for high‑quality, performant DI in modern Rust.
