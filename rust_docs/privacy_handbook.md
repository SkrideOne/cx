# Privacy Best Practices in Rust Nightly 1.91 (Best Practice 2025)

Rust’s module system and its visibility modifiers (`pub`, `pub(crate)`, `pub(super)`, `pub(in <path>)`, `pub(self)`) give fine‑grained control over item accessibility.  Proper use improves encapsulation, prevents accidental API exposure and helps optimisation.  The guidance below summarises best practices for Rust Nightly 1.91 (Rust 2024 edition) and incorporates minor improvements introduced since 1.90.  It emphasises quality, maintainability and performance.

---

## 1 Key Concepts and Terminology

### 1.1 Module privacy model

Rust organizes code into a module tree. Modules create namespaces; items are private by default—only the module and its children can access them. To expose a module externally, use `pub mod` instead of `mod`, and prefix items within it with `pub` to make them visible.

Use the `use` keyword to shorten paths without changing visibility.

| Modifier         | Scope                                                               | Notes                                                                      |
| ---------------- | ------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| `pub`            | Public if all parent modules are public                             | For API surface; allows re‑exporting and cross‑crate inlining              |
| `pub(crate)`     | Visible anywhere in the current crate                               | For intra-crate sharing; hides items externally                            |
| `pub(super)`     | Visible to the parent module (equivalent to `pub(in super)`)        | For splitting large modules                                                |
| `pub(self)`      | Visible within the current module (same as private)                 | Occasionally used in macros for clarity                                    |
| `pub(in <path>)` | Visible within the given ancestor module (`crate`, `self`, `super`) | Fine‑grained restrictions; path must start with `crate`, `self` or `super` |

All ancestor modules must be visible for an item to be accessed. E.g., a `pub(crate) fn f()` inside a private module remains inaccessible externally.

### 1.2 Re-exports

Use `pub use` to re‑export items under a different path. Common pattern:

```rust
pub use self::implementation::api;
```

Group internal modules under an `internal` namespace and build a flat public facade at the crate root to decouple internal structure from external API.

### 1.3 Linting and edition restrictions

* **`unreachable_pub` lint** warns when a `pub` item cannot be reached externally. Fix by marking it `pub(crate)`. Enable `#![deny(unreachable_pub)]` to enforce.
* **`private_in_public` lint** warns when public interfaces reference private types. Either make types public or hide the function. Enable `#![warn(private_in_public)]`.
* **Rust 2024 `pub(in path)` restriction**: paths must start with `crate`, `self` or `super`.

---

## 2 Typical Use Cases and Best Practices

### 2.1 Structs, Enums and Data Types

* Keep fields **private** by default. Expose invariants via accessor methods.

* Use **tuple structs** or **newtype wrappers** with private fields for opaque types:

  ```rust
  pub struct FileHandle(pub(crate) u32);
  ```

  For FFI, use `#[repr(transparent)]`.

* When fields must be shared intra-crate but hidden externally, mark them `pub(crate)` or `pub(in path)`:

  ```rust
  pub struct Config {
      pub(crate) timeout: Duration,
      pub(crate) retries: u8,
      pub(crate) internal_flag: bool,
  }
  ```

* Use `#[non_exhaustive]` on enums and structs to allow future extension without breaking semver.

### 2.2 Functions, Methods and Traits

* Keep small helpers **private** to the module. Use `pub(crate)` for cross-module helpers.
* Avoid exposing private types in public signatures; relocate functions or make types public.
* For traits:

    * Public trait definitions become part of the API; keep them minimal and stable.
    * Use `pub(crate)` default methods for internal helpers.
* For generic functions, limit `pub` to performance-critical APIs. Use `#[inline]` judiciously.

### 2.3 Modules and Crate Organization

* Organize code into **internal** and **public** modules. Re‑export public API from the crate root.
* Use `pub(super)` to split large modules while hiding submodule internals.
* Avoid deep public paths; re‑export items from flat modules for a clean API.
* Maintain a consistent visibility style (global vs. local).

### 2.4 Macros and Code Generation

* Export macros intentionally with `#[macro_export]` at the crate root. For crate-restricted macros, avoid `#[macro_export]` and use `pub(crate)`.

* Use `$vis:vis` in declarative macros to propagate caller visibility:

  ```rust
  macro_rules! make_wrapper {
      ($vis:vis $name:ident) => {
          $vis struct $name(pub u32);
      };
  }
  make_wrapper!(pub(crate) Wrapper);
  ```

* Prefer functions over macros. Use macros for DSLs, compile-time checks or repetitive patterns. Test macros with crates like `trybuild` and provide clear errors.

### 2.5 Public APIs and Documentation

* Document every public item with `///`, including purpose, invariants and examples.
* Use `pub(crate)` or `pub(super)` for items documented only for internal developers.
* Provide a **prelude** module (`crate::prelude::*`) for frequently used traits and types.

---

## 3 Performance Considerations

* **Reduced metadata**: `pub(crate)` and `pub(super)` generate less external metadata, speeding compilation and reducing code size.
* **Cross-crate inlining**: use `#[inline]` on small public APIs for hot paths; limit binary bloat.
* **Generics**: limit public generic APIs; consider trait objects or internal helpers to reduce monomorphization.
* Use `#[inline(never)]` on large internal functions unlikely to benefit from inlining to speed compilation.

---

## 4 Handling Non-Typical Cases

### 4.1 Foreign-Function Interface (FFI)

* Export symbols with `#[no_mangle] pub extern "C" fn` for predictable names; mark `unsafe` if needed.
* For FFI structs, use `#[repr(C)]` or `#[repr(transparent)]` and keep fields public only if required.
* Avoid exposing Rust types (e.g. `Vec<T>`) in FFI; use opaque handles and wrappers.

### 4.2 Private Types in Public Traits

* Ensure associated types and parameters are public or crate-public, or move traits to private modules to avoid `private_in_public` warnings.

### 4.3 Macros with Hidden Dependencies

* Keep helper functions private; expose only the procedural macro (`#[proc_macro]` or `#[proc_macro_derive]`).
* Test macros thoroughly and provide clear compile-time errors.

### 4.4 Conditional Compilation and Feature Flags

* Items under `#[cfg]` conditions must respect privacy rules. Use `pub(crate)` or `pub(super)` for feature-specific helpers.
* Document which features expose which APIs.

### 4.5 Unsized or Opaque Types

* Hide unsized types (`dyn Trait`, slices) behind `Pin<Box<dyn Trait>>` or `impl Trait` return types.

---

## 5 Checklist for Designing a Public API

* Define clear boundary between public API and internal implementation.
* Default to the most restrictive visibility; relax only when necessary.
* Document all public items with examples.
* Re-export from flat modules to simplify imports.
* Enable `#![deny(unreachable_pub)]` and `#![warn(private_in_public)]` in `lib.rs`.
* Review generic and trait signatures to avoid leaking private types.
* Use feature flags and `cfg` to gate unstable APIs.
* Mark extensible enums/structs with `#[non_exhaustive]` for semver stability.
* Profile performance when adding `#[inline]` and test compile times when changing visibility.
* Maintain consistent style across the codebase.

---

Following these practices ensures a maintainable, secure and performant Rust codebase by leveraging Rust’s visibility system for strong encapsulation and clear APIs.
