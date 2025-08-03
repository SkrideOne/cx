# Best Practices for Logging in Rust nightly 1.91 (Best Practice 2025)

Logging is a critical part of an observability stack: it lets developers correlate events, troubleshoot failures and analyse performance.  Rust nightly 1.91 inherits mature logging crates such as `log`, `tracing` and the surrounding ecosystem (e.g., `tracing‑subscriber`, `tracing‑appender`, `log4rs`).  Nightly 1.91 brings only minor API changes relative to 1.90 but benefits from improved lints and the deprecation of the `--nocapture` test flag, which now appears as `--no‑capture`【684066008670463†L134-L140】.  This guide summarises Best Practice 2025 for implementing high‑quality, performant logging with an emphasis on structured logs, safe error handling and support for both synchronous and asynchronous code.  Typical cases (small CLI programs, web services, libraries) and atypical cases (resource‑constrained or high‑throughput environments, sensitive data) are covered.

---

## 1 Choosing a Logging Framework

### 1.1 `log` crate as a façade

The `log` crate is a lightweight facade for logging. It defines macros (`error!`, `warn!`, `info!`, `debug!`, `trace!`) that check the current log level and avoid evaluating expensive arguments when the level is disabled. When no implementation is configured, `log` falls back to a no-op implementation that incurs only an integer comparison cost.

**Best practices:**

* **Initialize early and once**
  A logging implementation (e.g., `env_logger`, `log4rs`) must be initialized before logging macros are called; initialization is allowed only once per process.
* **Avoid side-effect expressions**
  Since arguments are not evaluated when the log level is disabled, avoid side-effects inside log macro arguments.
* **Use structured fields when possible**
  Enable the `kv` feature and supply key–value fields so that structured logs can be captured by subscribers.

### 1.2 `tracing`: the modern standard

For modern applications, `tracing` (part of the Tokio ecosystem) is the gold standard. Unlike traditional unstructured logs, `tracing` records events and spans with structured key–value data, enabling rich correlation across asynchronous tasks and threads.

**Key benefits:**

* **Structured context**: attach fields (e.g., `user_id`, `request_type`, `order_id`) to events and spans.
* **Spans for causality**: nest logical operations; context propagates across async boundaries.
* **Asynchronous-friendly**: non-blocking subscribers offload I/O to worker threads.
* **Extensible**: compose layers (`fmt`, `filter`, `metrics`, `JSON`) and export to OpenTelemetry or third-party backends.

Use `tracing` for new services, and adopt `tracing-log` adapters to unify logs from libraries that emit via `log`.

---

## 2 Setting Up Logging

### 2.1 Basic tracing setup

Add dependencies (latest pre-1.0 versions) to `Cargo.toml`:

```toml
[dependencies]
tracing             = "0.1"
tracing-subscriber  = { version = "0.3", features = ["env-filter", "fmt"] }
tracing-appender    = "0.2"   # non-blocking file/rolling writers
# Optionally: tracing-opentelemetry = "0.21" for OTLP exports
anyhow              = "1.0"   # for error context (§ 5)
```

Initialize the subscriber early (e.g., in `main`):

```rust
use tracing_subscriber::{fmt, EnvFilter};

fn init_tracing() {
    let fmt_layer = fmt::layer()
        .with_target(false)
        .with_thread_ids(false)
        .with_span_events(fmt::format::FmtSpan::CLOSE);

    let filter_layer = EnvFilter::try_from_default_env()
        .unwrap_or_else(|_| EnvFilter::new("info"));

    tracing_subscriber::registry()
        .with(filter_layer)
        .with(fmt_layer)
        .init();
}
```

The `EnvFilter` reads the `RUST_LOG` environment variable, enabling dynamic filtering without code changes.

Use macros like `tracing::info!`, `tracing::warn!`, each supporting key–value pairs:

```rust
use tracing::info;

info!(user_id = %user_id, request_type = %method, "received HTTP request");
```

### 2.2 Using spans

Spans provide context propagation and performance measurement:

```rust
use tracing::{info, info_span};

async fn handle_request(user_id: u64) {
    let span = info_span!("request", user_id);
    let _guard = span.enter();

    info!("processing request");
    // ... work ...
    info!("request finished");
}
```

* Start a span with `info_span!` or `span!` and structured fields.
* Enter via `.enter()` or use `.in_scope()` for synchronous code.
* Avoid holding guards across `await`; prefer `#[tracing::instrument]` on async functions.

---

## 3 Choosing Log Levels

Logging levels balance signal and noise. Over-logging wastes I/O and CPU; under-logging obscures context.

* **ERROR**: unrecoverable failures or external errors. Include consistent, contextualized messages.
* **WARN**: handled yet unexpected situations (retries, fallbacks).
* **INFO**: high-level lifecycle events (startup, shutdown, connections). Avoid tight-loop spamming.
* **DEBUG**: development diagnostics (variables, branch decisions). Disable in production.
* **TRACE**: extremely verbose (entry/exit, internal state). Use sparingly in early development.

Adjust levels via `RUST_LOG` or runtime via `EnvFilter`.

---

## 4 Structured Logging and Context

High-quality logs are machine-parsable and consistent. Best Practice 2025 recommends:

* **Consistent fields**: use the same key names (e.g., `user_id`, `request_id`).
* **Essential context**: always include timestamp (UTC/RFC 3339), severity, message, correlation IDs, service name, instance ID.
* **Avoid sensitive data**: never log passwords, API keys or PII; mark sensitive parameters with `skip` in `#[instrument]`.
* **Snake\_case naming**: adopt hierarchical module targets (e.g., `mycrate::handler::payment`).

Example with `#[instrument]`:

```rust
#[tracing::instrument(name = "handle_login", skip(password))]
async fn handle_login(username: &str, password: &str) -> Result<(), Error> {
    info!("User login attempt");
    // ... authenticate ...
    Ok(())
}
```

---

## 5 Error Handling and Logging

Use `anyhow` or `eyre` for rich, contextual errors. Wrap low-level errors with high-level context:

```rust
use anyhow::{Context, Result};

fn read_config(path: &Path) -> Result<Config> {
    let content = fs::read_to_string(path)
        .with_context(|| format!("failed to read config from {}", path.display()))?;

    let config = toml::from_str(&content)
        .context("invalid TOML in config file")?;

    Ok(config)
}
```

In async code, log errors with structured fields and use `tracing_error` or `ErrorLayer` to format stacks.

---

## 6 Writing Logs to Files and Avoiding Blocking

Synchronous writes can block async runtimes. For high-performance services:

* Use `tracing-appender`’s non-blocking writer: it spawns a worker thread and returns `(NonBlocking, WorkerGuard)`.
* Pass `non_blocking` to `fmt::layer().with_writer(non_blocking)` and keep the guard to flush on shutdown.
* Configure adequate buffer capacity to avoid drops or backpressure.
* For rolling files, choose size- or time-based rotation.
* Alternatively, use `log4rs` with rolling appenders and JSON/custom encoders.

---

## 7 Filtering and Dynamic Configuration

`tracing-subscriber` supports:

* **Static filters**: compile-time `max_level_*` features to remove code for lower levels entirely.
* **Environment filters**: `EnvFilter` from `RUST_LOG`, plus runtime reload handles.
* **Per-layer filters**: e.g., console at `debug`, file at `info`.

---

## 8 Integration with OpenTelemetry and Distributed Tracing

1. Add `tracing-opentelemetry` and `opentelemetry-sdk`.
2. Configure a tracer provider with OTLP exporter and resource attributes.
3. Layer `tracing-opentelemetry` into your subscriber; spans carry trace/span IDs.
4. Call `opentelemetry::global::shutdown_tracer_provider()` on shutdown to flush data.

---

## 9 Typical Use Cases

### 9.1 Small CLI Tools or Libraries

* Use `log` crate with simple backends (`env_logger`, `simple_logger`).
* Initialize in `main()` and respect `RUST_LOG`.
* Document log levels in `--help`.

### 9.2 Web Services (REST/GraphQL/Microservices)

* Use `tracing`, `tracing-subscriber`, and `tracing-futures`.
* Create a span per request (e.g., `tower-http`’s `TraceLayer` or `tracing_actix_web`).
* Use `#[instrument]` on handlers; skip sensitive data.
* Export to OpenTelemetry for cross-service correlation.
* Sample or drop low-value events at high throughput.

### 9.3 Libraries/Frameworks

* Emit via `log` facade; never initialize a logger.
* Define crate-specific targets for filtering.
* Use compile-time `max_level_*` features to shrink release binaries.

### 9.4 Background Jobs and Data Processing

* Use `tracing` with spans per job or partition.
* Write with non-blocking appenders.
* Include job IDs and dataset names in spans.

---

## 10 Atypical Scenarios and Advanced Considerations

### 10.1 Performance-Critical or High-Throughput Systems

* Raise global log level in production; use per-module filters.
* Always use non-blocking writers; tune buffer sizes and monitor drops.
* Implement sampling or rate-limiting (e.g., log every Nth event).
* Batch remote exports to reduce overhead.

### 10.2 Embedded or Resource-Constrained Environments

* Avoid dynamic allocation; compile with `max_level_info` or `max_level_warn` to strip lower levels.
* Use embedded logger crates like `defmt` for microcontrollers.
* If using `tracing`, choose minimal subscribers writing to ring buffers.

### 10.3 Handling Sensitive Data and Security

* Never log secrets; use `skip` in `#[instrument]` for sensitive args.
* Anonymize or hash PII before logging.
* Restrict log file permissions; transmit logs securely.

### 10.4 Bridging `log` and `tracing`

* Install `tracing_log::LogTracer::init()` before other subscribers to unify backends.

### 10.5 JSON and Third-Party Integrations

* Use `tracing_bunyan_formatter` or `tracing-serde` for JSON output.
* Combine with `tracing-appender` or `log4rs` for rolling JSON logs.

---

## 11 Quality and Maintenance

* **Analyze logs regularly**: review during development and post-incident.
* **Consistent naming**: adopt clear targets and field conventions.
* **Test logging**: use `tracing-test` or `logtest` to assert critical events.
* **Document strategy**: include examples in README and developer guides.

---

**Conclusion**

Rust nightly 1.91 provides a mature logging ecosystem.  While `log` remains suitable for simple binaries and libraries, `tracing` is the recommended choice for new projects due to structured data, span‑based context propagation and async compatibility.  Nightly 1.91 introduces no breaking changes to logging itself but improves tooling (new lints and the `--no‑capture` flag in libtest【684066008670463†L134-L140】).  By following these Best Practice 2025 guidelines—selecting appropriate levels, structuring logs, using non‑blocking writers and integrating with observability platforms—developers can build systems that are observable, performant and secure.
