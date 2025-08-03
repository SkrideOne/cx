# Macro Design and Usage in Rust nightly 1.91 – Best‑Practice 2025

## Introduction

Rust macros enable compile‑time metaprogramming.  They come in two flavours:

* **Declarative macros** (also called *`macro_rules!`* macros) match token patterns and emit code.  They are partly hygienic: local variables and labels introduced by the macro resolve at the definition site, while most other names resolve at the call site.  In Rust 1.90 nightly, the macro improvements project extended what declarative macros can do, including experimental built‑in macros like `cfg_select!` (a match‑like form of `#[cfg]`) and `pattern_type!` for pattern types.  The standard macro library also supports constant evaluation blocks (`const { … }`) inside built‑in macros (`assert_eq!`, `vec!`), and improved debug info collapsing.  These capabilities remain available in nightly 1.91.
* **Procedural macros** live in separate `proc‑macro` crates and operate on `TokenStream` inputs.  They encompass derive macros (`#[derive]`), attribute macros (`#[my_attr]`) and function‑like macros (`my_macro!(…)`) and are unhygienic by default: items and identifiers resolve as if they were written inline at the call site.  Rust 1.90 exposed additional APIs like `TokenStream:Default` and new `Span` methods (`resolved_at`, `located_at`, `mixed_site`) for fine‑grained span manipulation.  Function‑like procedural macros can now appear in more positions (expressions, patterns, statements), closing gaps relative to `macro_rules!`.  Nightly 1.91 continues to support these APIs without significant changes.

Macros remove boilerplate, implement domain‑specific languages and enforce invariants at compile time.  However, they also increase compile time and can bloat binary size.  The Best‑Practice 2025 philosophy encourages favouring functions, generics and trait bounds for reusable code, reaching for macros only when they provide clear, lasting benefits.

---

## When to use macros

Macros are powerful but not free.  Use them only when other language features are insufficient.  Appropriate use‑cases include:

* **Reducing boilerplate**: Implement repetitive trait impls or generate similar structures for a list of types.  For example, derive macros generate `Serialize`/`Deserialize` impls automatically.
* **Domain‑specific languages (DSLs)**: Extend Rust’s syntax to embed mini‑languages (e.g. HTML templates in Yew’s `html!` macro, routing definitions in web frameworks).  Macros can perform compile‑time parsing and validation.
* **Compile‑time validation and code generation**: Check SQL queries, regular expressions or configuration formats at compile time and emit constant lookup tables or data structures.
* **Conditional compilation**: Use macros like `cfg!`, `cfg_if!` (external crate) or nightly `cfg_select!` to select code paths based on target features or platform.
* **Variadic arguments and pattern repetition**: Provide user‑friendly APIs for functions that take an arbitrary number of parameters.

Avoid macros for tasks that functions and generics can handle more clearly.  Prefer iterators over variadic macros, type parameters over dynamic `tt` fragments and trait bounds over pattern matching when possible.  Macros should be well‑documented and tested just like normal API surface.

---

## Fundamental concepts

### Pattern matching and fragments

Declarative macros define one or more *arms* that match syntax patterns and generate code.  Metavariables (e.g. `$expr:expr`, `$ident:ident`, `$ty:ty`) capture parts of the invocation.  Repetition is expressed with `$(…)*` or `$(…)+`, optionally separated by a token (e.g. `,`).  Non‑terminal matches must be followed by a *follow set* token to avoid ambiguity.  The `tt` fragment matches any sequence of tokens; use more specific fragments (`expr`, `pat`, `ty`) when possible to improve error messages.

### Hygiene and name resolution

Macros are *partly hygienic*.  Loop labels, block labels and local variables introduced by a declarative macro resolve at the **definition site**.  All other identifiers resolve at the **call site**.  Use the `$crate` metavariable to refer to items in the defining crate when exporting macros:

```rust
#[macro_export]
macro_rules! my_helper {
    () => { $crate::internal_fn() };
}
```

The `#[macro_export(local_inner_macros)]` attribute was historically used to prefix inner macro calls with `$crate::`.  It is now discouraged; prefer explicitly qualifying inner macros with `$crate::`.

Procedural macros are *unhygienic* by default: generated code behaves as if it were written at the call site.  Use fully qualified paths (`::std::option::Option`) or imported symbols to avoid name clashes.  The `proc_macro::Span` API (available on nightly) allows controlling the span of generated tokens to improve diagnostics.

### Debugging and inspecting macro expansions

* Use [`cargo expand`](https://github.com/dtolnay/cargo-expand) to view the expanded code for both declarative and procedural macros without running the compiler.
* Enable `trace_macros!` (nightly) to print every macro invocation and expansion.  Use carefully, as it produces voluminous output.
* For procedural macros, print or log the `TokenStream` during development to inspect what the macro generates.
* Use `rustc -Z unstable-options --pretty=expanded` to view fully expanded code when debugging complex interactions.

---

## General best‑practice guidelines

These guidelines apply to both declarative and procedural macros:

1. **Use macros sparingly**.  Prefer functions, generics and trait bounds when they suffice.  Macros should exist because they provide true compile‑time benefits, not because they save a few keystrokes.
2. **Document syntax and behaviour**.  Clearly explain what the macro expects and produces, including argument patterns, default values, limitations and side effects.  Use doc comments (`///`) and provide examples and doctests.
3. **Design clear patterns**.  Write separate arms for different arities or scenarios.  Support optional trailing commas.  Use named arguments (e.g. `create_user!(name: $name:expr, age: $age:expr)`) for clarity.
4. **Choose specific fragment specifiers**.  Prefer `expr`, `ty`, `ident`, `pat` over `tt` to catch errors early and produce meaningful messages.  Avoid overly permissive patterns.
5. **Prevent name collisions**.  Wrap expansions in blocks (`{ … }`) to limit scope.  Prefix internal variables with underscores or unlikely names (`__my_macro_tmp`).  In declarative macros, refer to items in your crate using `$crate::…`.
6. **Emit explicit errors**.  Use `compile_error!` in declarative macros or emit errors via `syn::Error` and `proc_macro_error` in procedural macros.  Validate inputs and report unsupported cases clearly.
7. **Test macros**.  Use the [`trybuild`](https://docs.rs/trybuild) crate to write compile‑pass and compile‑fail tests for declarative and procedural macros.  Inspect expansions using `cargo expand` and ensure your macros behave as expected.
8. **Debug thoughtfully**.  Use `trace_macros!` and log tokens from procedural macros only during development.  Clean up debug prints before releasing.  Use new `Span` APIs (`resolved_at`, `located_at`, `mixed_site`) for accurate span assignment in nightly.
9. **Monitor performance**.  Macros increase compile times.  Keep expansions small, factor common code into functions and avoid heavy dependencies in procedural macro crates.  Use `#[collapse_debuginfo]` or `-C collapse-macro-debuginfo` to reduce debug‑info size for macro-heavy crates.
10. **Respect stability and feature gates**.  Some macros (`cfg_select!`, `pattern_type!`, `const_format_args!`) and functions (`std::hint::likely`) are nightly‑only.  Gate your code with `#![feature(...)]` and provide stable fallbacks or alternative APIs when required.

---

## Declarative macro specifics

Declarative macros are defined with `macro_rules!` and are most efficient when their patterns are clear and simple.

### Simple and variadic macros

Handle different argument counts by writing multiple arms.  For variadic macros, use repetition with separators and optional trailing commas:

```rust
macro_rules! sum {
    () => { 0 };
    ($x:expr $(, $rest:expr)* $(,)?) => {{
        let mut tmp = $x;
        $( tmp += $rest; )*
        tmp
    }};
}
```

Allow an optional trailing comma to improve ergonomics.  For single‑argument cases, write a separate arm if semantics differ.

### Repetitive trait implementations

Implement the same trait for multiple types without repeating code:

```rust
macro_rules! impl_my_trait {
    ($($t:ty),* $(,)?) => { $(
        impl MyTrait for $t {
            // implementation
        }
    )* };
}

impl_my_trait!(u8, u16, u32, u64);
```

Use descriptive macro names (e.g. `impl_numeric_traits!`) and group unrelated types into separate macros to avoid mixing unrelated logic.

### Domain‑specific languages (DSLs)

DSL macros parse custom syntax at compile time and often validate inputs.  Use `compile_error!` to signal invalid syntax.  Keep the macro expansion simple and delegate complex logic to helper functions.  Document the grammar, provide examples and write tests using `trybuild`.

### Conditional compilation

Use stable macros like `cfg!` and the `cfg_if` crate for simple conditional branches.  On nightly, `cfg_select!` allows multi‑branch `#[cfg]` matching:

```rust
#![feature(cfg_select)]

cfg_select! {
    unix => { /* Unix implementation */ }
    target_pointer_width = "32" => { /* non‑Unix 32‑bit implementation */ }
    _ => { /* fallback */ }
}
```

Always provide a wildcard `_` fallback and gate nightly macros with appropriate `#![feature]` attributes.  Offer stable alternatives (`cfg_if!`) when distributing on stable.

### Built‑in macros

Rust ships many built‑in declarative macros (`vec!`, `format!`, `println!`, `dbg!`, `assert!`, `matches!`, `concat!`, `stringify!`, `include_str!`/`include_bytes!`).  Use them instead of rolling your own.  Use compile‑time format string checking via `format!` and avoid `println!` in hot loops.  Remove `dbg!` calls from release builds via `#[cfg(debug_assertions)]`.

### Testing, logging and debugging macros

* Write compile‑error tests using `trybuild` and compile‑time checks.  Use `#[test]` functions or doctests to assert that your macro produces the expected runtime behaviour.
* For logging macros, embed `#[cfg(debug_assertions)]` to include them only in debug builds.  Document the verbosity level and provide flags to disable logging at compile time.

### Advanced declarative features

* **Recursive macros** can implement compile‑time loops by recursing on remaining tokens.  Ensure each recursive call consumes input to avoid infinite recursion, and provide a base case.
* **Declarative attribute and derive macros** can be built using the external `macro_rules_attribute` crate, but procedural macros are usually better suited for attributes and derives.
* **Generating macros**: macros can emit other macros to build layered abstractions.  Document all generated names to avoid polluting the namespace.
* **Generics and lifetimes**: Capture type parameters (`$t:ty`), lifetime parameters (`$l:lifetime`) and const generics (`$n:expr`).  Ensure captured generics appear consistently in the expansion.  Use `#[inline]` on generated functions to aid inlining.
* **Async and const contexts**: Macros can generate `async fn` and `const fn` definitions.  Be mindful of Rust’s restrictions (e.g. `async` not allowed in `const fn`) and write separate arms or use `#[cfg]` to gate features.

---

## Procedural macro specifics

Procedural macros are compiled into a separate `proc‑macro` crate and run during compilation.  They accept and return `proc_macro::TokenStream` instances and can perform arbitrary transformations.

* **Hygiene**: Procedural macros are unhygienic by default.  Use fully qualified paths and unique identifier prefixes to avoid collisions.  Some crates (e.g. `proc_macro_crate`) help find the correct crate names.
* **Span management**: Use `Span::call_site()`, `Span::mixed_site()`, `Span::resolved_at(other)`, or `Span::located_at` (nightly) to attach accurate spans to generated tokens.  Accurate spans yield better error messages.
* **Parsing and generation**: Avoid string‑based codegen.  Use `syn` to parse the input `TokenStream` into an AST and `quote` (or `proc_macro::quote`) to generate code.  Validate the AST and return informative errors via `syn::Error::to_compile_error()` or the `proc_macro_error` crate.
* **Derive macros**: Use `#[proc_macro_derive(TraitName, attributes(...))]` to implement custom derives.  Validate field attributes and produce helpful compile errors.  Keep the derive crate lightweight; avoid heavy dependencies.
* **Attribute macros**: Accept an item and optional arguments and return a modified item.  Use attribute macros to implement wrappers, embed code before/after functions or generate additional items.  Avoid surprising behaviour—document what is added or removed.
* **Function‑like macros**: Accept arbitrary token streams and produce arbitrary code.  With Rust 1.90 function‑like macros were extended to work in expression, pattern and statement positions, and nightly 1.91 continues to support these call-site improvements.  Provide clear syntax rules and examples in documentation.
* **Error handling**: Convert parser errors into compile errors.  Use the `proc_macro_error` crate to emit multiple errors from a single invocation.  Provide actionable messages.
* **Performance**: Keep procedural macro crates small and avoid transitive dependencies.  Use the `quote::quote_spanned!` macro to generate code with correct spans.  Clone `TokenStream` values judiciously; they are cheap to clone but avoid repeated parsing.  Collapse debug info with `#[collapse_debuginfo]` or `-C collapse-macro-debuginfo` when releasing.

---

## Other nightly features and considerations

* **`cfg_select!` and `pattern_type!`**: These experimental macros expand `#[cfg]` conditions or pattern type definitions.  Use only on nightly with `#![feature(cfg_select)]`/`#![feature(pattern_type)]` and provide stable fallbacks.
* **`const_format_args!`**: Computes format arguments at compile time; still experimental and subject to change.
* **`const { … }` in built‑in macros**: Use `const` blocks inside macros like `assert_eq!` or `vec!` to perform compile‑time computations.  This is stable on nightly and often yields zero‑runtime overhead.
* **Debug info collapsing**: Apply `#[collapse_debuginfo]` or compile with `-C collapse-macro-debuginfo` to collapse debug information from macro expansions, improving compile times and reducing binary size.
* **Macro lints and compatibility**: Keep up to date with compiler release notes.  New lints (e.g. `SEMICOLON_IN_EXPRESSIONS_FROM_MACROS`) may require pattern adjustments.  The macro improvements project is ongoing; test macro code after upgrading the compiler.

---

## Summary

Macros are an essential part of Rust’s metaprogramming toolkit.  In Rust nightly 1.90, declarative macros (`macro_rules!`) gained experimental new capabilities (`cfg_select!`, `pattern_type!`, `const { … }` in built‑ins), and procedural macros gained improved APIs and context positions.  Nightly 1.91 continues to support these enhancements without major changes.  Use macros judiciously: favour functions and generics for everyday code and reserve macros for reducing boilerplate, creating DSLs, performing compile‑time validation or constructing data structures at compile time.  Design macro patterns clearly, document them thoroughly, emit explicit errors, test expansions with `trybuild` and `cargo expand`, and benchmark compile‑time overhead.  Gate nightly‑only macros behind feature flags and provide stable fallbacks.  By following these guidelines, developers can harness the power of Rust’s macro system while maintaining code quality, readability and performance in 2025.