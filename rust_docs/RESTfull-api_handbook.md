# Building High-Performance RESTful APIs in Rust (Nightly 1.90)

Rust’s focus on memory safety, zero-cost abstractions and fearless concurrency make it an excellent choice for building modern web services. In 2025 the ecosystem around Rust’s nightly compiler (1.90) continues to mature; frameworks like Actix Web and Axum offer ergonomic APIs, while low-level building blocks such as Hyper provide fine-grained control. This guide summarises best practices for designing and implementing RESTful APIs in Rust, with a focus on high quality, performance and maintainability.

---

## 1 Choose the Right Framework

| Framework       | Highlights                                                                                                                                     | When to Use                                                                                                      |
| --------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| **Actix Web**   | Actor-based, extremely fast and scalable; asynchronous architecture handles high concurrency with very low latency. Rich middleware ecosystem. | Maximum throughput or large numbers of simultaneous connections; accept steeper learning curve (actor concepts). |
| **Axum**        | Router-centric design built on Tower; type-safe request extraction; strong async performance. Middleware layers are easy to compose.           | Most new projects; balances performance and developer ergonomics; functional routing style.                      |
| **Rocket**      | High-level, declarative macros with type-safe routing and simple API.                                                                          | Prototypes or ease-of-use priority; slightly lower performance under heavy load.                                 |
| **Warp / Poem** | Lightweight, filter-based (Warp) or minimalist (Poem); good performance.                                                                       | Micro-services or preference for composable filters.                                                             |

> **Tip:** For extremely specialized workloads, build directly on **Hyper**, a “fast and correct” HTTP implementation underpinning many frameworks.

---

## 2 Designing a RESTful API

Follow good API design principles to ensure your service is intuitive, consistent and evolvable.

### 2.1 Resource-Oriented URIs

* Use nouns (not verbs) for resource names and pluralise collection names, e.g. `/books` rather than `/getBooks`.
* Organise related resources hierarchically but avoid deep nesting; keep URIs simple.
* Use path parameters for specific resources (e.g. `/orders/123`) and query parameters for filtering, pagination or sorting.

### 2.2 HTTP Methods & Status Codes

| Verb       | Purpose                                              | Example                        |
| ---------- | ---------------------------------------------------- | ------------------------------ |
| **GET**    | Retrieve a resource; idempotent and side-effect free | `GET /users/5` returns user 5  |
| **POST**   | Create a new resource                                | `POST /users` with a JSON body |
| **PUT**    | Replace an existing resource                         | `PUT /users/5`                 |
| **PATCH**  | Partially update a resource                          | `PATCH /users/5`               |
| **DELETE** | Remove a resource                                    | `DELETE /users/5`              |

Return appropriate status codes:

* `200 OK` for success
* `201 Created` when creating a resource
* `204 No Content` when no body is returned (e.g. deletion)
* `400 Bad Request` for client-side errors
* `401 Unauthorized` / `403 Forbidden` for auth errors
* `500 Internal Server Error` for server issues
* `202 Accepted` for asynchronous operations, with `Location` header pointing to a status endpoint

### 2.3 Versioning

Version from the start. Use URL-based versioning (`/api/v1/users`) or header-based versioning. Nest versions in separate routers or handle via custom headers. Deprecate old versions gradually and document changes clearly.

### 2.4 Hypermedia (HATEOAS)

Include links in responses to describe available operations. Define a `Link` structure and embed it, e.g.:

```json
{
  "data": { /* resource fields */ },
  "links": {
    "self": "/users/5",
    "delete": "/users/5",
    "collection": "/users"
  }
}
```

### 2.5 Pagination, Filtering & Partial Responses

* **Pagination:** use `limit` and `offset` query parameters; enforce sensible defaults and upper limits.
* **Filtering & Sorting:** e.g. `/orders?minCost=100&status=shipped`; be aware of cache implications.
* **Partial Responses:** allow clients to request specific fields: `/orders?fields=id,name`.
* **Range Requests:** support `Accept-Ranges` and honor the `Range` header to return `206 Partial Content` for large resources.

### 2.6 Asynchronous Operations

For long-running tasks, return `202 Accepted` with a `Location` header for polling. Once complete, return `303 See Other` with the new resource URI.

---

## 3 High-Performance Rust Implementation

### 3.1 Async Runtime & Concurrency

* Use `async`/`await` with the **Tokio** runtime.
* Avoid blocking inside async handlers; use `tokio::task::spawn_blocking` when necessary.
* In Actix Web, configure worker threads based on CPU cores.
* For shared mutable state, prefer `Arc<RwLock<T>>` or **DashMap** to minimise contention.
* In Axum, inject state via `State<T>` and share with `Arc`.

### 3.2 Serialization & Data Access

* Use **Serde** for JSON (derive `Serialize`, `Deserialize`; use `#[serde(rename_all = "camelCase")]`).
* For databases, prefer **SQLx** (compile-time query checks) with connection pooling (e.g. `sqlx::Pool`).
* For ORMs, **Diesel** is mature but wrap blocking calls with `spawn_blocking` or use `tokio_diesel`.

### 3.3 Middlewares & Cross-Cutting Concerns

* **Logging & Tracing:** use the `tracing` crate with `tower_http::trace::TraceLayer`.
* **Timeouts & Limits:** apply `TimeoutLayer` and `RequestBodyLimitLayer`.
* **Compression:** use `CompressionLayer` (gzip, Brotli).
* **CORS:** configure with `CorsLayer` or `actix_cors`.
* **Rate Limiting:** implement middleware to track IP/request counts.
* **Auth:** use `Authorization: Bearer <token>` or `x-api-key`; implement RBAC.
* **TLS:** secure with `rustls` or `openssl`; load certificates and wrap the listener.

### 3.4 Error Handling

* Define custom error types (`thiserror`, `anyhow`).
* Implement `ResponseError` (Actix) or `IntoResponse` (Axum).
* Return structured error bodies; avoid leaking internal details.
* Use **RFC 7807** Problem Details format where appropriate.

### 3.5 Profiling & Optimization

* Profile regularly (e.g. **Criterion**) to find hotspots.
* Keep compiler/deps up to date.
* Use release builds (`--release`) and flags like `-C target-cpu=native`.
* Employ connection pooling, caching and batching to reduce overhead.

---

## 4 Quality & Maintainability

### 4.1 Project Structure

Organise code into modules: `handlers`, `models`, `state`, `services`, `utils`. Use environment variables for config (`dotenv`, `config` crate); avoid hard-coded secrets.

### 4.2 Logging & Monitoring

* Structured logging with **tracing** and correlation IDs.
* Integrate metrics (Prometheus, OpenTelemetry).
* Use distributed tracing to propagate trace contexts.

### 4.3 Documentation & Testing

* Generate OpenAPI/Swagger docs (e.g. **utoipa**, **paperclip**) and serve Swagger UI.
* Write unit tests for handlers and integration tests (use `reqwest` or `hyper`).
* Enforce linting (`clippy`) and formatting (`rustfmt`).

### 4.4 Security Best Practices

* Validate and sanitize all inputs; escape outputs to prevent XSS.
* Use strong auth (OAuth2, JWT, API keys).
* Enforce TLS everywhere; never log secrets.
* Implement rate limiting and throttle abnormal traffic.
* Follow least-privilege principle for DB and network access.

---

## 5 Caching & Performance Enhancements

* **Server-Side Caching:** use in-memory caches (e.g. **DashMap**, `cached` crate); design invalidation strategies.
* **Client Caching:** set `Cache-Control` and `ETag` headers.
* **Compression:** gzip or Brotli for JSON.
* **Batching:** support bulk operations to reduce round-trips.
* **Connection Reuse:** configure `keep_alive` or rely on Hyper defaults.
* **Database Tuning:** index filtered columns; use prepared statements and pooling.

---

## 6 Handling Non-Typical Cases

### 6.1 Streaming & Real-Time Communication

* **Server-Sent Events (SSE):** use `tokio::sync::broadcast` or `futures::stream`.
* **WebSockets:** Actix Web’s `ws` or Axum’s `WebSocketUpgrade`, handle ping/pong.
* **HTTP/2 Streaming:** use Hyper HTTP/2 with streaming bodies.

### 6.2 File Uploads & Downloads

* Use streaming multipart handlers (`actix_multipart`, `axum::extract::Multipart`).
* Validate size/type; stream files in chunks; set `Content-Disposition`.

### 6.3 Long-Running / Background Tasks

* Offload to background workers (`tokio::task::spawn`) or external queues (Redis, RabbitMQ).
* Return `202 Accepted` with status endpoint.
* Use schedulers (e.g. `tokio-cron-scheduler`).

### 6.4 GraphQL or gRPC Endpoints

* For GraphQL, use **async-graphql** or **Juniper**.
* For gRPC, use **Tonic** with Protocol Buffers.

### 6.5 Multi-Tenancy & SaaS

* Use tenant identifiers in path or subdomains.
* Isolate data per tenant (schemas or separate DBs).
* Implement per-tenant rate limits and quotas.

---

## 7 Deployment & Operations

* **Containerization:** build minimal Docker images (scratch/distroless); use `musl` for static linking.

```dockerfile
FROM rust:1.90-slim AS builder
WORKDIR /app
COPY . .
RUN cargo build --release

FROM gcr.io/distroless/cc
COPY --from=builder /app/target/release/my-api /my-api
ENTRYPOINT ["/my-api"]
```

* **Configuration:** manage via environment variables or config files.
* **Observability:** export metrics to Prometheus; enable OpenTelemetry tracing; send logs to centralized systems.
* **CI/CD:** run `cargo test`, `cargo clippy`, `cargo fmt`; deploy via Kubernetes or serverless; use readiness/liveness probes.

---

## Summary

Building RESTful APIs in Rust (nightly 1.90) requires balancing correct API design with Rust’s capabilities. Carefully designed URIs, proper HTTP semantics, versioning, hypermedia and pagination provide a solid foundation. Leveraging asynchronous runtimes, efficient serialization, robust middleware and profiling yields high performance. Adhering to security best practices, structured logging, testing and documentation ensures maintainable and secure services. Finally, understanding non-typical scenarios and preparing for deployment completes a mature, production-ready API.
