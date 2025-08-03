# Best Practices for I/O in Rust 1.90 Nightly (2025)

## 1 Overview

### 1.1 Philosophy of I/O in Rust

Rust’s I/O model is based on traits such as [`Read`](https://doc.rust-lang.org/std/io/trait.Read.html) and [`Write`](https://doc.rust-lang.org/std/io/trait.Write.html), which abstract over byte sources and sinks. These traits support composition (e.g., wrapping a `TcpStream` in a `BufReader`) and provide convenience methods like `read_to_end()` and `write_all()`. Because each call to `read()` or `write()` may trigger a system call, the standard library offers buffered adapters (`BufReader`, `BufWriter`) that minimize syscall overhead.

Rust’s ownership model prevents use-after-free and ensures explicit resource release. The `Result` type forces callers to handle I/O errors rather than ignore them.

I/O can be synchronous (blocking) or asynchronous (event-driven). The standard library provides only blocking I/O; asynchronous I/O comes via ecosystems such as [Tokio](https://tokio.rs) or [async-std](https://docs.rs/async-std), which use thread pools or, on Linux, **io\_uring** for high-throughput workloads.

### 1.2 API Stability

Rust 1.87 stabilised the anonymous pipe API (`io::pipe`), exposing `PipeReader`/`PipeWriter` for cross-process communication. Rust 1.88 (June 2025) improved ergonomics without breaking changes. The guidelines below apply to nightly 1.90 and will remain valid for the 2025 stable release.

---

## 2 Typical I/O scenarios and best practices

### 2.1 Reading and writing whole files

* **Convenience functions**: Use `fs::read()` and `fs::read_to_string()` to load entire files into memory. They handle `EINTR` internally and return `Result<Vec<u8>>` or `Result<String>`. For writing, `fs::write()` creates or replaces a file’s contents.
* **Error propagation**: Always use `?` rather than `unwrap()`.
* **UTF-8 validation**: Reserve `read_to_string()` for UTF-8 text. Use `fs::read()` for arbitrary binary data.
* **Path checks**: These functions error if the path is missing. Ensure parent directories exist with `fs::create_dir_all()` before writing.

### 2.2 Incremental reading and writing of large files

* **Buffering**: Wrap readers/writers in `BufReader`/`BufWriter` to batch syscalls. Tune buffer size with `with_capacity()`—defaults to 8 KiB, but NVMe devices often benefit from 1 MiB.
* **Explicit flush**: Call `flush()` on buffered writers before drop. Drops ignore flush errors.
* **LineWriter**: For text logs that flush on newline, use `LineWriter`.
* **Random access**: Use the `Seek` trait and `SeekFrom` enum for arbitrary file positioning. Always check returned offsets.

### 2.3 Copying data between files and streams

* **`io::copy`**: Streams from any `Read` to `Write` until EOF; returns bytes copied. Internally retries on interruption and uses optimized syscalls (`copy_file_range`, `sendfile`) on Linux.
* **`fs::copy`**: Copies by path and preserves permissions. Beware truncation when source and destination coincide.
* **Pipes**: Avoid relying on splicing optimizations (removed in 1.89); for large transfers, implement custom loops or async methods.

### 2.4 Standard input/output

* Use `io::stdin()`, `stdout()`, and `stderr()`. Wrap `stdin()` in a `BufReader` and call `read_line()` to avoid per-byte syscalls.
* Detect terminals with `io::IsTerminal` (stabilised in 1.90).
* Always `flush()` when prompting in interactive contexts.

### 2.5 Network I/O (TCP/UDP)

**TCP**

* Connect with `TcpStream::connect()`, which tries each resolved address.
* Servers: bind `TcpListener` and iterate on `incoming()`.
* Spawn threads or tasks per connection to avoid serial processing.
* Configure socket options:

    * `set_read_timeout`/`set_write_timeout` to avoid indefinite blocking.
    * `set_nonblocking(true)` for custom event loops or async integration.
    * `set_nodelay(true)` to disable Nagle’s algorithm for small messages.

**UDP**

* Create sockets with `UdpSocket::bind()`, send with `send_to()`, receive with `recv_from()`.
* Optionally `connect()` to fix peer address and use `send`/`recv` APIs.
* Use `set_nonblocking()` and timeouts in event loops.

> **Partial reads/writes**: Always loop until all data is processed; check return sizes.

### 2.6 Error handling and robustness

* **Propagate errors**: Use `?` or `match` on `Result<T, io::Error>`. Handle specific `ErrorKind` cases like `WouldBlock`.
* **Interrupted syscalls**: Standard functions retry on `Interrupted`; custom loops must catch and retry.
* **Library errors**: Wrap I/O errors in custom enums or use crates like `thiserror` or `anyhow` for context.
* **Concurrency**: Share file/socket handles via `Arc<Mutex<T>>` or `try_clone()`. For multi-process, use advisory locks (see §3.6).

---

## 3 Non-typical and advanced I/O scenarios

### 3.1 Asynchronous file I/O with Tokio

Tokio’s `tokio::fs` offloads blocking operations to a thread pool. Use `AsyncReadExt`/`AsyncWriteExt` for methods like `read_to_end()` and `write_all()`. Each call blocks a thread, so async file I/O is mainly to avoid blocking the Tokio scheduler rather than to improve raw throughput.

### 3.2 Memory-mapped file access (mmap)

```rust
use memmap2::MmapOptions;
use std::{fs::File, error::Error};

fn process(path: &str) -> Result<(), Box<dyn Error>> {
    let file = File::open(path)?;
    let mmap = unsafe { MmapOptions::new().map(&file)? };
    // Validate offsets before slicing
    if &mmap[0..8] != b"MAGIC" { return Err("invalid signature".into()); }
    for chunk in mmap[8..].chunks(RECORD_SIZE) {
        // process chunk
    }
    Ok(())
}
```

Always validate offsets and sizes. Use page-aligned offsets and call `mmap.flush()` after mutations.

### 3.3 High-performance async I/O with io\_uring (Linux only)

Use the `io-uring` crate for true asynchronous I/O with minimal context switches and batched submissions. For network I/O, consider `tokio-uring` to integrate with Tokio.

### 3.4 Direct (unbuffered) I/O

```rust
use std::{fs::OpenOptions, os::unix::fs::OpenOptionsExt, io::Write, alloc::Layout};

fn write_direct(path: &str, data: &[u8]) -> std::io::Result<()> {
    let layout = Layout::from_size_align(data.len(), 512).unwrap();
    let mut buf = unsafe { std::alloc::alloc_zeroed(layout) };
    unsafe { std::ptr::copy_nonoverlapping(data.as_ptr(), buf, data.len()); }

    let file = OpenOptions::new()
        .write(true)
        .create(true)
        .custom_flags(libc::O_DIRECT)
        .open(path)?;
    file.write_all(unsafe { std::slice::from_raw_parts(buf, data.len()) })?;
    unsafe { std::alloc::dealloc(buf, layout); }
    Ok(())
}
```

Align buffers to the device’s sector size (use `ioctl(fd, BLKSSZGET)`) to avoid `EINVAL`.

### 3.5 Zero-copy file parsing

```rust
#[repr(C)]
struct Record { timestamp: i64, price: f64, quantity: u32 }

fn parse(data: &[u8]) -> Option<&Record> {
    if data.len() >= std::mem::size_of::<Record>() {
        Some(unsafe { &*(data.as_ptr() as *const Record) })
    } else { None }
}
```

Use `#[repr(C)]` for predictable layout and explicitly validate endianness.

### 3.6 Advisory file locks

```rust
use fs4::FileExt;
use std::fs::OpenOptions;

fn increment(path: &str) -> std::io::Result<()> {
    let mut file = OpenOptions::new().read(true).write(true).create(true).open(path)?;
    file.lock_exclusive()?;
    // read, update, write...
    file.unlock()?;
    Ok(())
}
```

Advisory locks only coordinate cooperating processes. Use `try_lock()` for non-blocking attempts.

### 3.7 Efficient directory traversal

Use the `walkdir` crate with depth limits and disable symlink resolution to minimize syscalls. Combine with `rayon`'s `.par_bridge()` for parallel metadata processing.

### 3.8 Batched writes

```rust
const BUFFER_SIZE: usize = 64 * 1024;
struct BatchWriter { file: File, buffer: Vec<u8>, position: usize }

impl BatchWriter {
    fn write(&mut self, data: &[u8]) -> std::io::Result<()> {
        if self.position + data.len() > self.buffer.len() {
            self.flush()?;
        }
        self.buffer[self.position..self.position + data.len()].copy_from_slice(data);
        self.position += data.len();
        Ok(())
    }
    fn flush(&mut self) -> std::io::Result<()> {
        if self.position > 0 {
            self.file.write_all(&self.buffer[..self.position])?;
        }
        self.position = 0;
        Ok(())
    }
}

impl Drop for BatchWriter {
    fn drop(&mut self) { let _ = self.flush(); }
}
```

Group writes into medium-sized chunks (e.g., 64 KiB) for optimal throughput.

### 3.9 File change monitoring

Use the `notify` crate for cross-platform file watching. Configure polling intervals and enable `with_compare_contents(true)`. Handle events on a separate thread to avoid blocking.

### 3.10 Anonymous pipes and inter-process communication

Rust 1.87 stabilised `io::pipe()`, returning `(PipeReader, PipeWriter)`. Reads block until data is available; writes block when full. EOF is seen when all writers are dropped. Avoid pipes for large transfers—`io::copy()` no longer splices files into pipes.

---

## 4 Performance and quality guidelines (2025 best practice)

* Profile before optimising (e.g., `perf`, `strace`, `bcc`).
* Choose buffer sizes wisely: start with 8 KiB, adjust to 1 MiB for SSDs, or lower for real-time.
* Batch operations and reuse buffers to minimise allocations.
* Avoid unnecessary conversions—use `read_to_string()` for text, operate on `Vec<u8>` for binary.
* Use vectored I/O (`read_vectored()`, `write_vectored()`) to gather/scatter across buffers.
* Introduce `unsafe` only when measurements justify zero-copy gains.
* Prefer async network I/O for high concurrency; file I/O async still blocks threads.
* Handle partial reads/writes by looping; treat `WouldBlock` with readiness checks.
* Explicitly `flush()` and `shutdown()` when needed; rely on drop semantics otherwise.
* Implement robust error handling: distinguish transient vs fatal errors, provide context, and retry when appropriate.

## 5 Conclusion

Rust 1.90 nightly delivers robust, efficient I/O primitives. By applying buffering judiciously, choosing appropriate abstractions (sync vs async), validating boundaries, batching operations, and handling errors explicitly, developers can meet 2025’s quality and performance expectations. Advanced techniques—zero-copy parsing, direct I/O, multi-process coordination—can yield further gains when guided by profiling and careful unsafe use.
