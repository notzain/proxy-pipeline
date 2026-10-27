# C++17 Compatibility and Common Utility Library

## Overview
This initiative establishes C++20 as the project default while ensuring all code is written with strict C++17 compatibility in mind. An internal compatibility layer in `common/include/common/compat/` (`Common::Compat`) provides standard polyfills for C++20 language and library features (`span`, `jthread`, `stop_token`, `latch`, `bit_cast`, `atomic_wait`, etc.) with zero external dependencies. Extracted general-purpose utilities (memory primitives, ring buffer queues, RAII file descriptors) are consolidated in the standalone `common` library (`Common` namespace).

## Design Decisions
1. **Zero External Dependencies**:
   - Polyfills are implemented using standard C++17 library mechanisms and platform OS primitives without external dependencies.
   - Primitives: `span`, `jthread`, `stop_token`, `stop_source`, `stop_callback`, `latch`, `bit_cast`, `atomic_wait`/`atomic_notify_*`, and `starts_with`/`ends_with` string helpers.
   - When compiled with C++20 (`__cpp_lib_*`), the types directly alias standard `std::` definitions.
2. **Native OS Address Waiting (`atomic_wait` / `atomic_notify_*`)**:
   - Instead of hash-bucketed condition variable tables (which suffer from lost wakeups when different atomics hash to the same bucket and notify single waiters), the C++17 fallback implements native OS address-wait primitives matching standard C++ library implementations:
     - **Darwin / macOS**: `__ulock_wait` (`UL_COMPARE_AND_WAIT`) and `__ulock_wake` (`ULF_WAKE_ALL`).
       - Reference: LLVM `libc++` (`libcxx/src/atomic.cpp`): [LLVM libc++ atomic.cpp](https://github.com/llvm/llvm-project/blob/main/libcxx/src/atomic.cpp)
     - **Linux**: `syscall(SYS_futex, ..., FUTEX_WAIT_PRIVATE)` and `FUTEX_WAKE_PRIVATE`.
       - Reference: GNU `libstdc++` (`bits/atomic_wait.h`): [GNU libstdc++ atomic_wait.h](https://gcc.gnu.org/git/?p=gcc.git;a=blob;f=libstdc%2B%2B-v3/include/bits/atomic_wait.h) and [Linux futex(2) man page](https://man7.org/linux/man-pages/man2/futex.2.html)
     - **Windows**: `WaitOnAddress`, `WakeByAddressSingle`, and `WakeByAddressAll` (synchapi.h).
       - Reference: [Microsoft Learn: WaitOnAddress](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitonaddress)
     - **Generic OS Fallback**: Striped table with notification broadcasting (`notify_all` under lock) to avoid lost wakeups on hash collisions.
3. **Configurable C++ Standard Options & Presets**:
   - Configurable via `PROXY_CXX_STANDARD` ("20" default, or "17") which synchronizes with `CMAKE_CXX_STANDARD`.
   - CMake presets provided: `debug`, `debug-cpp17`, `release`, and `release-cpp17`.
4. **Authoritative Paths & Strict Namespace Discipline**:
   - Extracted components reside Authoritatively under `Common::` (`Common::Memory`, `Common::Queue`, `Common::Io`, `Common::Compat`).
   - Per project guidelines, no backwards compatibility forwarding headers, transitional shims, or type aliases under `Core::` are retained. All call sites and references across `core`, `backend`, `handler`, `test`, `benchmark`, and `tools` have been updated cleanly.
5. **Build & Package Integration**:
   - `common` is packaged via Conan 2 (`common/0.1.0`) and linked into consumers cleanly via standard CMake target `common::common`.

## Implementation Checklist
- [x] Create `common` library structure and CMake/Conan configuration (`common/CMakeLists.txt`, `common/conanfile.py`).
- [x] Implement C++17/20 compatibility polyfills in `common/include/common/compat/`:
  - [x] `Span.hpp`
  - [x] `StopToken.hpp`
  - [x] `Jthread.hpp`
  - [x] `Latch.hpp`
  - [x] `BitCast.hpp`
  - [x] `AtomicWait.hpp` (Native OS address waiting with Darwin ulock, Linux futex, Windows WaitOnAddress)
  - [x] `StringView.hpp`
  - [x] `TypeTraits.hpp`
  - [x] `Compat.hpp`
- [x] Relocate shared memory, queue, and I/O utilities from `core` to `common`:
  - [x] `common/include/common/memory/`: `BufferPool`, `BufferSlice`, `Borrow`, `NotNull`, `ObserverPtr`, `SmallSpanBuffer`
  - [x] `common/include/common/queue/`: `SpscQueue`, `MpmcQueue`
  - [x] `common/include/common/io/`: `UniqueFd`
  - [x] Remove old headers and implementation files from `core`
- [x] Eliminate all backwards compatibility shims and aliases in `Core::Memory`, `Core::Queue`, and `Core::Io`.
- [x] Update all call sites across handlers, backends, core, tests, benchmarks, and tools to authoritative `Common::` types.
- [x] Add `PROXY_CXX_STANDARD` CMake configuration option ("17" and "20") and CMake presets (`debug-cpp17`, `release-cpp17`).
- [x] Create comprehensive Catch2 unit tests in `test/TestCommonCompat.cpp` covering polyfill behavior.
- [x] Enforce WebKit formatting style with `clang-format`.
- [x] Update project documentation in `AGENTS.md` and `docs/cpp17-common-library.md`.
- [x] Verify complete test suite execution with 100% pass rate in both C++17 and C++20 modes (83 tests passed).
