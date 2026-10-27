# Value Types & RAII Safety: UniqueFd, NotNull, and Borrow

## Overview
This document outlines the design and implementation of core value types and RAII wrappers to replace error-prone primitive types across the codebase:
- **`UniqueFd`**: Move-only RAII file descriptor wrapper ensuring leak-free socket and file handle management across `backend/socket`, `backend/uds`, `backend/sim`, and `core/net`.
- **`Borrow<T>`**: Rebindable, non-null reference wrapper representing borrowed objects, container-friendly and preventing temporary rvalue binding at compile time. Used as the authoritative return type for channel registration.
- **`NotNull<T>`**: Non-null pointer wrapper that guarantees invariants at compile-time and construction-time, pairing cleanly with `ObserverPtr<T>` and raw pointers.

---

## Design Decisions

1. **`Core::Io::UniqueFd`**:
   - Location: `core/include/core/io/UniqueFd.hpp` and `core/src/io/UniqueFd.cpp`.
   - Semantics: Non-copyable, movable RAII handle holding an `int`.
   - Closes automatically via `::close()` upon destruction or `reset()`.
   - Methods:
     - `get() const noexcept -> int`
     - `release() noexcept -> int`
     - `reset(int newFd = -1) noexcept`
     - `isValid() const noexcept -> bool`
     - `explicit operator bool() const noexcept`
     - Comparison operators (`==`, `!=`).

2. **`Core::Memory::Borrow<T>`**:
   - Location: `core/include/core/memory/Borrow.hpp`.
   - Semantics: Non-owning, non-null rebindable reference.
   - Deleted rvalue constructor (`Borrow(T&&) = delete`) prevents dangling temporary binds at compile time.
   - Provides pointer-like member access (`operator->`, `operator*`), implicit conversion to `T&`, and conversion to `ObserverPtr<T>`.
   - Authority: Returned by `IHandler::registerInputChannel` and `IHandler::registerOutputChannel`.

3. **`Core::Memory::NotNull<T>`**:
   - Location: `core/include/core/memory/NotNull.hpp`.
   - Semantics: Zero-cost wrapper around pointer-like types (`ObserverPtr<T>`, raw pointers `T*`, `std::unique_ptr<T>`, `std::shared_ptr<T>`).
   - Disallows construction from `nullptr_t` at compile time.
   - Asserts non-null on construction from dynamic pointer values.
   - Provides `get()`, `operator*()`, `operator->()`, implicit conversion to underlying `T*` / `ObserverPtr<T>`.

---

## Tasks / Checklist

- [x] **1. Implement `Core::Io::UniqueFd`**
  - [x] Create `core/include/core/io/UniqueFd.hpp` with Doxygen documentation.
  - [x] Implement `core/src/io/UniqueFd.cpp`.
  - [x] Export `core` recipe with conan.
  - [x] Write unit tests for `UniqueFd` in `test/TestValueTypes.cpp`.
- [x] **2. Implement `Core::Memory::Borrow<T>`**
  - [x] Create `core/include/core/memory/Borrow.hpp` with Doxygen documentation.
  - [x] Write unit tests for `Borrow` verifying rebindability, `std::vector` compatibility, and conversion to `ObserverPtr`.
  - [x] Update `IHandler::registerInputChannel` and `IHandler::registerOutputChannel` to return `Borrow<T>`.
- [x] **3. Implement `Core::Memory::NotNull<T>`**
  - [x] Create `core/include/core/memory/NotNull.hpp` with Doxygen documentation.
  - [x] Write unit tests for `NotNull` verifying non-null guarantee, compile-time nullptr rejection, and interop with `ObserverPtr<T>`.
- [x] **4. Adopt `UniqueFd` across Sockets & Net**
  - [x] Refactor `core/src/net/UdpInputChannel.cpp` & `UdpOutputChannel.cpp` to use `UniqueFd`.
  - [x] Refactor `core/src/telemetry/StatsDExporter.cpp` to use `UniqueFd`.
- [x] **5. Build & Verification**
  - [x] Verify build with `cmake --build build/gcc16-debug`.
  - [x] Run full test suite with `ctest --test-dir build/gcc16-debug --output-on-failure` (70/70 passing).
  - [x] Format with `clang-format`.
