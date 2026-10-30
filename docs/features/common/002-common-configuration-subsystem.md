# 002: Strongly-Typed Configuration Subsystem (`Backend::IConfig` & `Core::Config::Configuration`)

## 1. Overview & Problem Statement

### Why It's Needed
In `72cff56` – `c7a3f5e`, configuration parameters needed to be passed cleanly to both transport backends (`backend/*`) and domain handlers (`handler/*`). Early string-based configuration lookups suffered from:
1. **Scattered String Parsing**: Calling `std::stoi` or `std::stod` inside individual handlers risked unhandled exceptions and locale-dependent decimal parsing bugs.
2. **Header Bloat**: Putting parsing logic inline inside `IConfig.hpp` coupled every consumer header to parsing implementation details.

### The Solution
1. **Abstract `Backend::IConfig` Interface (`backend/interface/include/backend/IConfig.hpp`)**: Provides strongly-typed `get<T>(key)` (returning `std::optional<T>`) and `getOr<T>(key, defaultValue)` template methods over a single virtual `getRaw(std::string_view key)` hook.
2. **Non-Allocating Value Parser (`c7a3f5e`)**: Separated `IConfig` parsing implementations into `backend/interface/src/IConfig.cpp`, using `std::from_chars` for fast, exception-free, locale-independent integer and floating-point conversion, plus flexible boolean parsing (`true`/`false`, `1`/`0`, `yes`/`no`, `on`/`off`).
3. **INI / Key-Value File Loader (`Core::Config::Configuration`)**: Implemented `Configuration::fromFile(path)` supporting `[section]` prefixes (flattened as `section.key`), `#` / `;` comments, and programmatic `set(key, value)` overrides.

---

## 2. Architecture & How It Works

### 2.1 Configuration Resolution Pipeline

```mermaid
flowchart LR
    File["Config File (.conf / .ini)"] --> Loader["Core::Config::Configuration::fromFile()"]
    Loader --> Map["Key-Value Store (string -> string)"]
    Map --> IConfig["Backend::IConfig::getRaw(key)"]
    IConfig --> Parse["Detail::parseConfigValue<T>(string_view)"]
    Parse --> Typed["std::optional<T> / getOr<T>(key, default)"]
```

---

## 3. Design Decisions

### 3.1 Separating `IConfig` Interface (`backend/interface`) from Storage (`core/config`)
- **Decision**: Place `Backend::IConfig` in `backend/interface` and `Core::Config::Configuration` in `core`.
- **Rationale**: Transport backends (`file`, `socket`, `uds`, `shm`, `inproc`, `sim`, `vendor`) configure themselves via `const Backend::IConfig&` without depending on `core`.

### 3.2 Exception-Free `std::optional<T>` Semantics
- **Decision**: Return `std::nullopt` when a key is missing or cannot be parsed as `T`, allowing `getOr<T>(key, defaultVal)` to fall back cleanly without throwing exceptions.
- **Rationale**: Keeps startup and channel configuration deterministic and safe against malformed input values.

---

## 4. Concrete Examples

### 4.1 Querying Strongly-Typed Config Values
```cpp
auto config = Core::Config::Configuration::fromFile("proxy.conf");

std::string host = config.getOr<std::string>("ingress.host", "127.0.0.1");
uint16_t port    = config.getOr<uint16_t>("ingress.port", 9001);
bool strictFifo  = config.getOr<bool>("execution.strict_fifo", true);
```

---

## 5. Tasks / Checklist

- [x] Define `Backend::IConfig` with templated `get<T>` and `getOr<T>` (`72cff56`).
- [x] Separate `IConfig` parsing implementation into `backend/interface/src/IConfig.cpp` (`c7a3f5e`).
- [x] Implement `Core::Config::Configuration::fromFile()` and unit tests in `test/TestConfiguration.cpp`.

