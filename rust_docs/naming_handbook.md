# Rust Entity Naming Guidelines — Best Practices for 2025

This guide systematizes the commonly accepted naming conventions for various entities in Rust as of 2025. It is based on official sources (RFC 430 and the Rust API Guidelines), articles on quality API design, community publications on architectural patterns, and best-practice discussions. The goal is to provide strict recommendations and examples so developers can write idiomatic code without unnecessary searching.

> **Notation:** In tables, `PascalCase` means UpperCamelCase, `snake_case` means lowercase words separated by underscores, and `SCREAMING_SNAKE_CASE` means uppercase words separated by underscores.

---

## 1 General Principles

* **Separate by namespace.** Rust has two main namespaces: **type-level** (types, traits, enums) using `PascalCase`, and **value-level** (functions, variables, modules) using `snake_case`.
* **Avoid `-rs`/`-rust` suffixes** on crate names. Every package on crates.io is Rust by default, so adding `-rs` or `-rust` is redundant.
* **Treat acronyms as words.** In `PascalCase` and `snake_case`, acronyms like `UUID` become `Uuid` and `uuid`.
* **Use full words.** Names should reflect intent: prefer `calculate_interest` over `calc`, and `user_set` for a `HashSet` of users instead of `users_list`.
* **Be consistent.** Similar functionality should use the same verbs across your codebase (e.g., `get_all_users` vs. `get_all_products`).
* **Leverage module context.** Place related code in modules so names can be concise—`parser::parse` is clearer than `parse_parser`.

---

## 2 Naming by Category

### 2.1 Crates and Packages

| Entity    | Rule                                                                                                                           | Example                   | Sources                             |
| --------- | ------------------------------------------------------------------------------------------------------------------------------ | ------------------------- | ----------------------------------- |
| Package   | Name is the directory name and defaults to the crate name. Use `snake_case` or hyphens; hyphens map to `_` when importing.     | `serde_json`, `tokio`     | RFC 430; raw\.githubusercontent.com |
| Crate     | `snake_case`; avoid `-rs` and `-rust` suffixes.                                                                                | `hyper`, `clap`           | Rust API Guidelines                 |
| FFI crate | Low-level wrappers over C libraries use the `*-sys` suffix (e.g., `png-sys`). Safe wrappers live in crates without the suffix. | `libz-sys`, `sqlite3-sys` | Convention among `*-sys` crates     |

### 2.2 Modules and Files

| Entity         | Rule                                                                                                                                 | Example                           | Sources                        |
| -------------- | ------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------- | ------------------------------ |
| Module         | `snake_case`; filename must match module name. Submodules go in separate files or directories with `mod.rs`.                         | `http_server`, `config`, `parser` | Cross-platform Rust Components |
| Private module | May use shorter or abbreviated names if not exported.                                                                                | `ffi`, `sys`                      | —                              |
| Test module    | Create a nested `tests` module with `#[cfg(test)]`; test functions use `snake_case` to describe behavior (no `test_` prefix needed). | `tests::parses_valid_input`       | Rust style guide               |

### 2.3 Types, Structs, Enums and Variants

| Entity                         | Rule                                                                              | Example                                 | Sources     |
| ------------------------------ | --------------------------------------------------------------------------------- | --------------------------------------- | ----------- |
| Types (structs, enums, unions) | `PascalCase`; names should be nouns describing the entity or role.                | `HttpRequest`, `ErrorKind`, `Utf8Error` | RFC 430     |
| Enums                          | `PascalCase`; name reflects the set of possible states.                           | `Color`, `Result`, `Either`             | RFC 430     |
| Enum variants                  | `PascalCase`; names are elements of the set.                                      | `Ok`, `Err`, `Some`, `None`             | RFC 430     |
| Type aliases                   | `PascalCase`; for primitives, add context (e.g., `UserId`, `Bytes`).              | `type UserId = u64;`                    | Style guide |
| Private structs                | In private code, shorter or abbreviated names are allowed, but prefer full words. | `RawPtr`                                | —           |

### 2.4 Traits

| Entity           | Rule                                                                                                                                                                         | Example                            | Sources              |
| ---------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------- | -------------------- |
| Traits           | `PascalCase`; use nouns or adjectives describing capability (e.g., `Read`, `Write`, `Clone`). Avoid bare verbs—prefer `Iterator` over `Iterate`.                             | `Iterator`, `IntoIterator`, `Send` | Community guidance   |
| Extension traits | Use `Ext` suffix for traits that add methods to external types (e.g., `IteratorExt`).                                                                                        | `IteratorExt`, `ReadExt`           | Community patterns   |
| Async traits     | Do *not* add an `Async` suffix—the `async` keyword in the signature is sufficient. Differentiate sync/async via separate modules (e.g., `blocking::read` vs. `async::read`). | —                                  | Community discussion |

### 2.5 Functions and Methods

#### 2.5.1 Basic Rules

| Entity                       | Rule                                                                                                               | Example                                               | Sources                  |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------- | ------------------------ |
| Functions and methods        | `snake_case`; use present-tense verbs that reflect the action.                                                     | `add_user`, `calculate_checksum`, `push_str`          | RFC 430                  |
| Constructors                 | Use `new`. For multiple constructors, use `with_…` or describe the constructor (e.g., `with_capacity`).            | `Vec::new`, `Vec::with_capacity`, `String::from_utf8` | API Guidelines           |
| Conversions                  | Prefixes: `as_` for cheap views, `to_` for potentially expensive, `into_` for consuming conversions.               | `str::as_bytes`, `Path::to_str`, `String::into_bytes` | API Guidelines           |
| `from_` conversions          | Use `from_` to construct from another type.                                                                        | `OsString::from_wide`                                 | DEV Community            |
| Bool-returning functions     | Prefix with `is_` (properties) or `has_` (existence checks).                                                       | `Vec::is_empty`, `HashMap::contains_key`              | DEV Community            |
| `Result`-returning functions | Prefix with `try_` for safe variants; `unchecked_` for unsafe ones.                                                | `Vec::try_reserve`, `slice::get_unchecked`            | DEV Community            |
| Checked/unchecked operations | Use pairs `checked_`/`unchecked_` (e.g., `checked_add`).                                                           | `i32::checked_add`, `i32::unchecked_add`              | DEV Community            |
| Iterators                    | Methods: `iter`, `iter_mut`, `into_iter`. Iterator types: `Iter`, `IterMut`, `IntoIter`.                           | `Vec::iter`, `Vec::iter_mut`, `Vec::into_iter`        | API Guidelines           |
| Getters and setters          | Getter: same as field name (e.g., `len()`), no `get_` prefix. Setter: `set_<field>`.                               | `String::len`, `String::set_len`                      | API Guidelines           |
| Async functions              | `async fn` names match sync names; no `async` suffix. Differentiate via modules if needed.                         | `read`, `write`, `connect`                            | Community best practices |
| Test functions               | `#[test] fn` with `snake_case` descriptive names; avoid `test_` prefix.                                            | `#[test] fn parses_unicode() { … }`                   | Style guide              |
| Builder pattern              | Use a `<StructName>Builder` type with `builder()` constructor. Builder methods match field names; final `build()`. | `Foo::builder().name("X").build()`                    | Rust Design Patterns     |

#### 2.5.2 Suffixes and Variants

| Suffix      | Purpose                                                          | Example                  |
| ----------- | ---------------------------------------------------------------- | ------------------------ |
| `mut`       | Method mutates and returns a mutable reference; `mut` goes last. | `entry.get_mut`          |
| `ref`       | Returns a reference without ownership.                           | `as_ref`, `to_ref`       |
| `err`       | Returns an `Err` variant instead of panicking.                   | `try_into`, `expect_err` |
| `unchecked` | Unsafe variant without checks.                                   | `get_unchecked`          |
| `in`        | Consumes the argument by value (rare).                           | `insert_in`              |

### 2.6 Macros

| Macro type                   | Rule                                                                                                                                                                                                                       | Example                                  | Sources     |
| ---------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------- | ----------- |
| Declarative (`macro_rules!`) | Name in `snake_case` and invoked with `!`. Import as usual (e.g., `use crate::my_macro`).                                                                                                                                  | `println!`, `vec!`, `lazy_static!`       | Style guide |
| Procedural                   | Attribute macros (`#[proc_macro]`, `#[proc_macro_attribute]`) in `snake_case`. Derive macros use trait name (`PascalCase`) in `#[derive(Trait)]`. Avoid name conflicts by prefixing with your crate (`#[mycrate::route]`). | `#[derive(Serialize)]`, `#[tokio::main]` | RFC 1561    |
| Tool attributes              | Prefix with tool name (e.g., `#[rustfmt::skip]`, `#[clippy::allow]`). Name in `snake_case`.                                                                                                                                | `#[serde(rename = "id")]`, `#[test]`     | RFC 1561    |
| Visibility                   | `macro_rules!` exports via `#[macro_export]` or `pub macro` in macros 2.0. Default is private.                                                                                                                             | `pub macro my_macro { … }`               | RFC 1561    |

### 2.7 Constants, Statics and Globals

| Entity                         | Rule                                                                                                                    | Example                                    | Sources     |
| ------------------------------ | ----------------------------------------------------------------------------------------------------------------------- | ------------------------------------------ | ----------- |
| Constants (`const`)            | `SCREAMING_SNAKE_CASE`; use full words.                                                                                 | `MAX_BUFFER_SIZE`, `PI`, `DEFAULT_TIMEOUT` | Style guide |
| Immutable statics (`static`)   | Same as constants. For exported statics, refer with module prefix (e.g., `crate::CONFIG`).                              | `static APP_NAME: &str = "…";`             | Style guide |
| Mutable statics (`static mut`) | Avoid if possible; if needed, wrap in `Mutex`/`Atomic*` and document. Use `SCREAMING_SNAKE_CASE` with a purpose prefix. | `static mut COUNTER: u64 = 0;`             | Style guide |

### 2.8 Variables and Parameters

| Entity                  | Rule                                                                                                                    | Example                                         | Sources           |
| ----------------------- | ----------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------- | ----------------- |
| Local variables         | `snake_case`; avoid single letters except indices (`i`, `j`) and iterators; names reflect content.                      | `let max_value = …;`                            | Style guide       |
| Function arguments      | Same as local variables; avoid `a_/an_` prefixes.                                                                       | `fn draw_line(start: Point, end: Point)`        | —                 |
| Generic type parameters | Short uppercase (e.g., `T`, `U`, `E`, `K`, `V`). If many, use descriptive names (`Input`, `Output`). Avoid `_T` suffix. | `fn map<T, F>(iter: T, f: F)`                   | Infinite Circuits |
| Const generics          | Use `const N: usize` rather than a `SIZE` suffix.                                                                       | `fn from_array<T, const N: usize>(arr: [T; N])` | —                 |
| Deep type hierarchies   | Use type aliases for clarity (e.g., `type BytesIter<'a, T> = impl Iterator<Item=&'a [u8]>`).                            | `type FooRef<'a> = &'a Foo;`                    | —                 |

### 2.9 Lifetimes

| Lifetime kind       | Rule                                                                                     | Example                               | Sources      |
| ------------------- | ---------------------------------------------------------------------------------------- | ------------------------------------- | ------------ |
| Regular lifetimes   | Short, lowercase with apostrophe (`'a`, `'b`). Longer names for clarity (`'src`, `'de`). | `fn parse<'a>(s: &'a str) -> &'a str` | PossibleRust |
| Complex contexts    | Longer names permitted in advanced code (e.g., `'<tcx>` for type context).               | `for<'tcx> fn foo(...)`               | PossibleRust |
| Ownership lifetimes | In structs/methods, lifetimes reflect reference ownership (`'a`, `'b`).                  | `struct Foo<'a> { data: &'a str }`    | —            |

---

## 3 Specialized Recommendations

### 3.1 Error Handling

* **Error type naming:** Public API errors use the `Error` suffix (e.g., `IoError`, `ParseError`). Group related errors in an `enum ErrorKind { InvalidInput, NotFound, … }`. Typical return type is `Result<T, ErrorType>`. Inside a module, the detailed error struct may simply be named `Error` with `pub type Result<T> = std::result::Result<T, Error>`.
* **Fallible methods:** Prefix with `try_` and return `Result` rather than panicking (e.g., `try_from`, `try_reserve`).
* **`Option`-returning methods:** Often use `_checked` suffix (e.g., `checked_sub`) or no suffix if idiomatic (e.g., `split_first`).

### 3.2 External Interfaces (FFI)

* **Separate FFI crate.** Low-level unsafe wrappers live in `*-sys` crates, safe wrappers in sibling crates.
* **Naming inside FFI.** Use names close to the original C API in `snake_case`/`PascalCase` (e.g., `ffi::snappy_env`, `snappy_compress`). Public Rust API uses Rust identifiers (`SnappyEnv`, `compress`).
* **`extern "C"` functions.** Declare as `unsafe extern "C" fn`. If C names violate Rust rules, use `#[link_name = "CName"]`.

### 3.3 Design Patterns

* **Builder pattern.** Use a `<StructName>Builder` type and `builder()` constructor. Builder methods match field names; final method is `build()`. If building may fail, `build()` returns `Result<T, Error>`.
* **Extension-trait pattern.** Define a trait with an `Ext` suffix (e.g., `IteratorExt`) and implement `impl<T: Iterator> IteratorExt for T { … }` to add methods to external types.

### 3.4 Async Programming

* **Async fn naming.** Match sync names; no `async` suffix. Differentiate via modules (`blocking`, `async`) or types (`Client`, `ClientAsync`).
* **Return types.** Often `impl Future<Output = T>`; for readability, define type aliases (e.g., `type ReadFuture<'a> = impl Future<Output = usize> + 'a`).

### 3.5 Attributes and Directives

* **Compiler and Clippy warnings.** Adhere to lints; unresolved warnings may break CI.
* **Reserved keywords.** Use raw identifiers (e.g., `r#type`) or append an underscore (e.g., `type_`).

---

## 4 Documentation Recommendations

* **Provide documented examples.** Use `///` comments with code samples to illustrate usage and naming.
* **Explain abbreviations.** Even for acronyms like `Uuid`, clarify their meaning in documentation.
* **Note constraints and side effects.** Document possible errors and safety considerations, especially for FFI functions.

---

## Conclusion

Idiomatic naming is crucial for readable, maintainable Rust code. Following these guidelines ensures consistency, eases code review, and strengthens trust in your API. In 2025, the Rust community adheres to the core principles from RFC 430 and the Rust API Guidelines: `snake_case` for values, `PascalCase` for types, and `SCREAMING_SNAKE_CASE` for constants. Additional prefix, suffix, and builder-pattern rules help make your API clear and self-explanatory.
