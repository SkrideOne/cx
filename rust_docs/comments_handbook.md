# Commenting Best Practices for Rust Nightly 1.91 (2025)

## 1 Introduction

Comments in Rust fall into two broad categories:

* **Implementation comments** help developers understand how and why code works.
* **Documentation comments** are transformed into user-facing API docs by `rustdoc`.

Following the Rust Style Guide and the Rustdoc Book ensures clear, maintainable comments that improve code readability and development efficiency. Well-structured documentation comments enable `cargo doc` to generate accurate HTML docs and allow examples to be tested automatically via `cargo test`. Poorly written or outdated comments can mislead users and waste time.

## 2 Key Principles

* **Explain intent, not implementation**: Use comments to clarify *why* code works or highlight invariants, rather than restating *what* the code does.
* **Choose the right form**: Rust offers

    * Single-line (`// …`) and block (`/* … */`) comments for implementation notes.
    * Outer doc comments (`///`, `/** … */`) and inner doc comments (`//!`, `/*! … */`) for API documentation.
* **Follow style rules**: Place a single space after `//` or `/*`, write complete sentences starting with a capital letter and ending with a period, and wrap lines at 80 characters.
* **Document public APIs**: Every public module, trait, struct, enum, function, and method should have a documentation comment, including practical examples.
* **Use doc tests**: Embed example code in documentation comments. These examples are compiled and executed by `cargo test`, ensuring docs stay in sync with code.

## 3 Typical Commenting Scenarios

| Comment Type                  | Usage                                          | Guidelines                                                                                                                                                                    |
| ----------------------------- | ---------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Line comment** (`// …`)     | Implementation notes or temporary explanations | Use `// ` with one space; prefer line comments over block; write full sentences; place above or after code.                                                                   |
| **Block comment** (`/* … */`) | Multi-line implementation notes (rare)         | Prefer line comments; for single-line, add spaces after `/*` and before `*/`; for multi-line, delimit on separate lines.                                                      |
| **Trailing comment**          | Explanations on the same line                  | Precede with one space before `//`; keep concise; avoid on long expressions; prefer above-line comments.                                                                      |
| **Outer doc comment** (`///`) | Documents the following item                   | Use `///`; place immediately above the item; write a summary, detailed description, and at least one example; include sections like *Examples*, *Panics*, *Errors*, *Safety*. |
| **Inner doc comment** (`//!`) | Documents the enclosing module/crate           | Use `//!` at the top of a module or crate; provide an overview and architectural notes.                                                                                       |
| **Doc tests**                 | Code examples in docs                          | Surround with \`\`\`rust; use `?` instead of `unwrap`; mark long-running examples with `no_run` or `ignore`.                                                                  |
| **Safety & invariants**       | Inside `unsafe` blocks or functions            | Explain why unsafe is sound; describe caller invariants in a **Safety** section of the doc comment.                                                                           |
| **TODO/FIXME/HACK**           | Technical debt or future work                  | Prefix with `// TODO:` / `// FIXME:` / `// HACK:`; provide context or issue links; triage regularly.                                                                          |

## 4 Implementation Comments

* **Single-line comments (`//`)** are preferred for annotations. Always put one space after `//` and write full sentences. Place them on their own line above the code rather than trailing it, unless the comment is very short.
* **Block comments (`/* … */`)** may temporarily disable code or hold long notes. Avoid them when possible. For single-line blocks, add a space after `/*` and before `*/`. For multi-line, start and end on separate lines. Rust supports nested block comments.
* **Trailing comments** appear after code on the same line. Insert one space before `//`, keep the total line length under 100 characters (80 characters if the line is solely a comment), and avoid them on complex expressions.

## 5 Documentation Comments (`///`, `//!`)

* **Outer docs (`///` or `/** … */`)**

    * Document functions, structs, enums, traits, macros, and modules.
    * Start with a concise one-line summary, followed by a blank line and detailed description.
    * Provide at least one example demonstrating real-world use.
    * Include sections like **Panics**, **Errors**, and **Safety** when relevant.

* **Inner docs (`//!` or `/*! … */`)**

    * Document the enclosing module or crate.
    * Place at the top of `src/lib.rs` or `src/main.rs` to describe purpose and architecture.
    * Use Markdown headings (`# Examples`, `# Panics`), lists, and code fences.
    * Keep the first summary line concise for search results and overviews.

## 6 Documentation Tests

Doc tests are examples inside documentation comments that `cargo test` compiles and runs:

````rust
/// Adds two numbers.
///
/// # Examples
///
/// ```rust
/// let sum = add(2, 3);
/// assert_eq!(sum, 5);
/// ```
pub fn add(a: i32, b: i32) -> i32 {
    a + b
}
````

* Use \`\`\`rust fences with optional `no_run` or `ignore` flags for long-running or non-executable examples.
* Prefer the `?` operator over `unwrap` to illustrate error handling.

## 7 Safety, Panics and Errors

* **Panics**: Describe conditions that cause a panic.
* **Errors**: For `Result`-returning functions, explain error types and scenarios.
* **Safety**: In `unsafe` functions/blocks, document caller invariants in a **Safety** section and reinforce with inline comments near unsafe operations.

## 8 Comments in Macros and Generic Code

* In `macro_rules!` macros, place comments between rules or after delimiters; avoid inside patterns.
* In procedural macros, propagate doc comments to generated code using `quote! { #[doc = $docs] }`.
* For generic code, document type parameters only when non-obvious; focus on relationships and assumptions rather than restating syntax.

## 9 Non-typical and Advanced Scenarios

### 9.1 Commenting Out Code

Avoid long-term commented-out blocks; remove unused code or use feature flags (`#[cfg(feature = "…")]`) to conditionally compile code.

### 9.2 Conditional Documentation

Use `#[cfg_attr(feature = "nightly", doc = "…")]` to include docs only when features are enabled. This avoids stale comments and keeps docs consistent.

### 9.3 Hidden or Internal APIs

Use `#[doc(hidden)]` to hide items from generated docs. Do not rely on comments like `// internal`; those are ignored by `rustdoc`.

### 9.4 Generated Code and Attributes

Ensure procedural macros preserve doc comments on generated items. Keep implementation comments out of generated code; document the generator itself instead.

### 9.5 Comments and Performance

Comments do not affect runtime, but heavy doc tests can slow CI. Mark expensive examples with `no_run` to skip execution.

### 9.6 Concurrency, Memory Safety and Invariants

For atomic operations and memory ordering, explain required orderings and pointer-aliasing assumptions. Summarize invariants in a **Safety** doc section.

### 9.7 Using Comments for Metadata

Use attributes (`#[deprecated]`, `#[allow(...)]`) rather than comments. Comments can explain *why* an item is deprecated and suggest alternatives.

## 10 Additional Tools and Practices

* **rustfmt**: Auto-format code and comments. Use the default config (4-space indent, max line width 100, trailing commas).
* **Clippy**: Enable `#![warn(missing_docs)]` to require docs on public items and `#![deny(clippy::all)]` for stricter linting.
* **Issue tracking for TODOs**: Link `// TODO(#1234): …` to issue URLs so tasks can be triaged.
* **Internationalization**: Write docs in English for broadest reach; avoid non-ASCII characters except when necessary.
* **Security considerations**: When documenting functions handling untrusted input, highlight validation requirements and show safe usage examples.

## 11 Conclusion

Proper commenting in Rust Nightly 1.91 balances style-guide adherence with thoughtful explanation of intent, safety and usage patterns.  Nightly 1.91 inherits the same commenting model as earlier releases but benefits from improved tooling (e.g., `rustdoc` tests respect the `--no‑capture` flag and Clippy includes new lints for null pointer arguments【684066008670463†L134-L140】).  Use implementation comments to clarify *why* decisions were made and documentation comments to describe API behaviour, errors, panics and safety invariants.  Reinforce docs with examples validated via doc tests.  By following these best practices, you will create maintainable, high‑quality Rust code that is both easy to understand and evolve.
