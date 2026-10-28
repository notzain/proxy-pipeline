# Feature: ByteView Convenience Value Type (`Common::Memory::ByteView`)

## Overview & Problem Statement
In high-throughput messaging pipelines, methods frequently pass and inspect raw immutable byte buffers (`Common::Compat::span<const std::byte>`).
Writing `Common::Compat::span<const std::byte>` repeatedly is verbose, and converting standard types (such as `std::string`, `std::string_view`, `std::vector<T>`, `std::array<T, N>`, or raw pointer/length pairs) required repetitive, error-prone `reinterpret_cast<const std::byte*>` invocations at call sites.

## Solution & Architecture
We introduce a clean three-tiered non-owning view hierarchy in `Common::Memory`:
1. **[`View<ElementType>`](../../common/include/common/memory/View.hpp)** (`common/include/common/memory/View.hpp`):
   - Generic non-owning view inheriting from `Common::Compat::span<ElementType>`.
   - Handles typed contiguous containers (`std::vector<T>`, `std::array<T, N>`), pointer/length pairs, and safe covariance from `View<T>` to `View<const T>`.
2. **[`ByteView`](../../common/include/common/memory/ByteView.hpp)** (`common/include/common/memory/ByteView.hpp`):
   - Inherits from `View<const std::byte>` (and transitively `span<const std::byte>`).
   - Automatically converts from arbitrary contiguous containers (`size() * sizeof(T)`), string literals, fixed-size C arrays, and single scalar/POD structs (`ByteView(meta)`).
   - Zero-cost interpretations: `.asStringView()`, `.asChar()`, `.as<T>()`.
   - Prefix/suffix matching: `.startsWith()`, `.endsWith()`.
   - Direct symmetric equality: `==` / `!=` against `std::string_view`, `const char*`, and `ByteView`.
3. **[`MutableByteView`](../../common/include/common/memory/MutableByteView.hpp)** (`common/include/common/memory/MutableByteView.hpp`):
   - Inherits from `View<std::byte>` (and transitively `span<std::byte>`).
   - Non-owning view of contiguous writable bytes for destination write buffers (`receiveInto`, socket reads).
   - Implicitly convertible to immutable `ByteView`.

## Tasks / Checklist
- [x] Create feature branch `feat/byte-span-helper`
- [x] Create feature tracking documentation in `docs/features/byte-span-helper.md`
- [x] Implement generic `View<T>` in `common/include/common/memory/View.hpp`
- [x] Implement `ByteView` inheriting from `View<const std::byte>` in `common/include/common/memory/ByteView.hpp`
- [x] Implement `MutableByteView` inheriting from `View<std::byte>` in `common/include/common/memory/MutableByteView.hpp`
- [x] Add single-object POD constructor `ByteView(const T&)`
- [x] Add prefix/suffix testing (`startsWith`, `endsWith`) and equality operators (`== std::string_view`, `== const char*`)
- [x] Add comprehensive Catch2 unit tests in `test/TestByteView.cpp` (covering generic `View<T>`, `ByteView`, and `MutableByteView`)
- [x] Refactor verbose `reinterpret_cast<const std::byte*>` call sites across core, handlers, tests, tools, and benchmarks to use `ByteView`
- [x] Verify compilation and full test suite passing (86 test cases, 121,566 assertions)
- [x] Format files using `clang-format` (WebKit style)
