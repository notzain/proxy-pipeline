# Common Feature: Rust-Style Result Type and Match Operations

- **Status**: In Progress
- **Feature Number**: 008
- **Date**: 2026-10-31
- **Domain**: `common/types`

---

## 1. Overview & Problem Statement

The previous `Common::Types::Result<T, E>` suffered from several core design flaws:
1. **Double Indirection**: Nested `Value { T value; }` and `Error { E error; }` wrappers inside `Variant` introduced unnecessary levels of indirection (`storage.get<Value>()->get().value`).
2. **Lack of Mutually Exclusive Safety**: Users could invoke `.value()` or `.error()` regardless of the active variant, resulting in undefined behavior / unchecked null dereferences if accessed incorrectly.
3. **Absence of Pattern Matching**: Despite the presence of `Common::Types::Variant::match`, `Result` did not expose a Rust-like `.match(...)` mechanism to enforce exhaustive, safe branching on success and error states.
4. **Non-Standard Ergonomics**: Monadic operations, safe unwrapping, and Rust-like `Ok` / `Err` constructors were missing.

Per project conventions, no backwards-compatibility aliases or deprecated wrapper types are retained.

---

## 2. Architecture & Design Decisions

### 2.1 First-Class Tags: `Ok<T>` and `Err<E>`
Rather than anonymous nested wrapper structs, `Common::Types` introduces explicit tagged wrappers:
- `Ok<T>`
- `Err<E>`

This allows `Result<T, E>` to cleanly store `Variant<Ok<T>, Err<E>>`, supporting cases where `T` and `E` are identical types (e.g. `Result<int, int>`) without ambiguity or runtime overhead, and enabling direct pattern matching.

### 2.2 Direct Pattern Matching via `.match(...)`
`Result::match` accepts callables handling `T` and `E` directly:
```cpp
auto status = result.match(
    [](const T& val) { /* handle success */ },
    [](const E& err) { /* handle failure */ }
);
```
Unpacking of `Ok<T>` and `Err<E>` occurs transparently inside the member `match()` overloads for lvalue, const-lvalue, and rvalue references.

### 2.3 Safe Accessors & Monadic Operations
- `isOk()` and `isErr()`: Type-safe state queries.
- `unwrap()`: Accesses `T`, throwing `std::logic_error` on error state.
- `unwrapErr()`: Accesses `E`, throwing `std::logic_error` on ok state.
- `unwrapOr(fallback)`: Returns the value or fallback without throwing.
- `map(fn)`: Transforms `Ok<T>` into `Ok<U>`.
- `mapErr(fn)`: Transforms `Err<E>` into `Err<F>`.
- `andThen(fn)`: Chains operations returning `Result<U, E>`.

---

## 3. Tasks / Checklist

- [x] Implement `Ok<T>`, `Err<E>`, and new `Result<T, E>` in `common/include/common/types/Result.hpp`.
- [x] Update all codebase usages from `.hasValue()`, `.value()`, `.error()`, `Result::success()`, and `Result::failure()`.
- [x] Update and expand unit tests in `test/TestChannels.cpp` and add dedicated `Result` unit tests.
- [x] Verify build and tests pass across all presets.

