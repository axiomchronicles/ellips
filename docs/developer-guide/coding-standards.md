# C++ Coding Standards

ELIPS is implemented in C++23 following the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines). This document describes the conventions enforced and observed in the codebase.

## Language Standard

- **C++23** (`CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_STANDARD_REQUIRED ON`)
- Extensions disabled (`CMAKE_CXX_EXTENSIONS OFF`)
- Compiler warnings: `-Wall -Wextra -Wpedantic` on every target
- Supported compilers: Clang 17+, GCC 13+

## RAII (Resource Acquisition Is Initialization)

All resources are managed through RAII. Examples from the codebase:

```cpp
// File lock acquired on construction, released on destruction
class LockManager {
public:
    explicit LockManager(const std::string& lock_path);  // acquires exclusive lock
    ~LockManager();  // releases lock
    LockManager(const LockManager&) = delete;
    LockManager& operator=(const LockManager&) = delete;
    LockManager(LockManager&&) noexcept;  // movable, transfers fd ownership
private:
    int fd_{-1};
};

// WAL file stream opened in constructor, flushed/closed in destructor
class WAL {
public:
    explicit WAL(std::filesystem::path path, bool sync_each_write = true);
    // ...
private:
    std::ofstream out_;
};

// Database handle owns vaults, WAL, lock manager
class ElipsInstance {
public:
    ~ElipsInstance();  // auto-checkpoint on destruction
    ElipsInstance(const ElipsInstance&) = delete;
    ElipsInstance& operator=(const ElipsInstance&) = delete;
    ElipsInstance(ElipsInstance&&) = delete;
    ElipsInstance& operator=(ElipsInstance&&) = delete;
};
```

Smart pointers (`std::unique_ptr`) are used for polymorphic ownership (e.g., `IndexPort`, `WAL`). Raw pointers are non-owning (e.g., `Vault::wal_` is `WAL*` for back-reference).

## Value Semantics

Domain types have value semantics — copyable, comparable, default-constructible:

```cpp
class RecordID {
public:
    bool operator==(const RecordID&) const = default;
    auto operator<=>(const RecordID&) const = default;
    // ...
};

class Vector {
public:
    Vector() = default;
    explicit Vector(std::vector<float> values);
    // ...
};

struct Record {
    RecordID id;
    Vector vector;
    Payload payload;
};
```

Classes that manage unique resources (database handles, locks, indexes) are non-copyable and non-movable.

## Scoped Enums (Enum.3)

All enums are `enum class`. No unscoped enums anywhere in the codebase:

```cpp
enum class Metric { cosine, euclidean, dot_product };
enum class IndexType { graph, exact };
enum class Durability { paranoid, standard, relaxed, ephemeral };
enum class Comparator { eq, ne, lt, le, gt, ge };
enum class TokenKind { word, number, string, punct, end };
enum class GpuPolicy { Auto, PreferGpu, RequireGpu, CpuOnly, Specific };
enum class GpuIndexAlgorithm { Auto, CagraGraph, IvfFlat, IvfPq, BruteForce };
enum class GpuPrecision { Fp32, Fp16, Int8, Auto };
```

Conversion to strings is done via explicit `to_string()` / `from_string()` functions, not implicit casts.

## `noexcept` Usage

Functions that cannot fail are marked `noexcept`:

- All simple accessors: `dimension()`, `metric()`, `values()`, `name()`, `info()`
- `distance()` in the metrics engine
- `empty()`, `magnitude()` on Vector
- `type_name()` on index implementations
- `matches_all()` on Filter

```cpp
[[nodiscard]] std::span<const float> values() const noexcept { return values_; }
[[nodiscard]] std::uint16_t dimension() const noexcept {
    return static_cast<std::uint16_t>(values_.size());
}
[[nodiscard]] float distance(Metric, std::span<const float>, std::span<const float>) noexcept;
```

## Const Correctness

Member functions that do not mutate state are `const`. Getters are always `const`:

```cpp
[[nodiscard]] const Config& config() const noexcept { return config_; }
[[nodiscard]] const std::string& name() const noexcept { return name_; }
[[nodiscard]] VaultInfo info() const noexcept;
[[nodiscard]] std::vector<SearchResult> seek(...) const;  // search is read-only
[[nodiscard]] std::size_t size() const noexcept override { return ids_.size(); }
```

`const` references are used for parameters that are not copied or moved:

```cpp
Vault& vault(const std::string& name);
void place_many(const std::vector<Record>& records);
[[nodiscard]] static Statement parse(std::string_view source);
```

## `[[nodiscard]]` Annotations

`[[nodiscard]]` is used on every function where ignoring the return value would indicate a bug:

- All factory functions: `open()`, `make_index()`, `RecordID::generate()`
- All search/query operations: `seek()`, `scan()`, `fetch()`, `query()`
- All getters and accessors
- All combinator/transformation functions: `normalized()`, `and_()`, `or_()`, `not_()`
- Type introspection: `type_name()`, `to_string()`, `bytes()`, `values()`
- Every `const` member function that returns a value

```cpp
[[nodiscard]] static std::unique_ptr<ElipsInstance> open(const std::string& path,
                                                         const Config& config = {});
[[nodiscard]] static Filter compare(std::string field, Comparator op, MetaValue value);
[[nodiscard]] Filter and_(const Filter& other) const;
[[nodiscard]] Vector normalized() const;
[[nodiscard]] std::vector<Token> tokenize(std::string_view source);
```

The codebase consistently applies `[[nodiscard]]` — there are no "bare" return-by-value functions in the public API.

## Header Guards

Traditional `#ifndef`/`#define`/`#endif` guards with a consistent `ELIPS_` prefix mirroring the file path:

```cpp
#ifndef ELIPS_DOMAIN_ERRORS_HPP         // include/elips/domain/Errors.hpp
#define ELIPS_DOMAIN_ERRORS_HPP

#ifndef ELIPS_INDEX_ENGINE_EXACT_INDEX_HPP  // include/elips/index_engine/ExactIndex.hpp
#define ELIPS_INDEX_ENGINE_EXACT_INDEX_HPP

#ifndef ELIPS_STORAGE_WAL_HPP           // include/elips/storage/WAL.hpp
#define ELIPS_STORAGE_WAL_HPP

#ifndef ELIPS_VECTOR_ENGINE_METRICS_HPP // include/elips/vector_engine/Metrics.hpp
#define ELIPS_VECTOR_ENGINE_METRICS_HPP
```

The closing comment is mandatory: `#endif  // ELIPS_<SUBSYSTEM>_<FILE>_HPP`.

## Naming Conventions

| Category | Convention | Examples |
|---|---|---|
| Files | `snake_case` | `exact_index_test.cpp`, `wal_recovery_test.cpp`, `Config.hpp`, `Metrics.hpp` |
| Namespaces | `snake_case` | `elips`, `elips::eql`, `elips::detail` |
| Classes, structs, enums | `PascalCase` | `ElipsInstance`, `HierarchicalGraphIndex`, `RecordID`, `Metric` |
| Enumerator values | `snake_case` | `Metric::cosine`, `Durability::paranoid`, `Comparator::ge` |
| Functions | `snake_case` | `make_index()`, `from_string()`, `requires_normalization()`, `place_many()` |
| Variables, members | `snake_case` with trailing `_` for private | `name_`, `config_`, `dimension_`, `wal_` |
| Constants | `snake_case` (no `k` prefix) | `snapshot_magic`, `identity_magic` |
| Template parameters | `PascalCase` single letter or word | `T`, `Value` |

## Dependency Inversion Principle (DIP)

The codebase follows hexagonal architecture. Internal layers depend on interfaces (ports), never on concrete implementations:

```cpp
// Port: all index consumers depend on this interface
class IndexPort {
public:
    virtual void insert(const RecordID& id, std::span<const float> vector) = 0;
    virtual void remove(const RecordID& id) = 0;
    [[nodiscard]] virtual std::vector<Hit> search(std::span<const float>, size_t k) const = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
    [[nodiscard]] virtual std::string_view type_name() const noexcept = 0;
};

// Factory: creates the concrete implementation, returns the interface
[[nodiscard]] std::unique_ptr<IndexPort> make_index(const Config& config,
                                                     std::uint16_t dimension);

// Consumers own an IndexPort, never an ExactIndex or HierarchicalGraphIndex directly
class Vault {
    std::unique_ptr<IndexPort> index_;  // concrete type injected at construction
};
```

The same pattern applies to GPU backends (`GpuPort` interface, `GpuSelector` factory).

## Error Handling

ELIPS uses **exceptions exclusively** for error reporting. There are no error codes, no `std::expected` return types, no `errno`-style patterns in the public API.

### Exception Hierarchy (E.14: purpose-designed exception types)

```
std::runtime_error
  └── ElipsError                  # base for all ELIPS errors
        ├── DimensionMismatch     # vector dimension != vault configuration
        ├── InvalidVector         # NaN, Inf, or malformed vector
        ├── ConfigError           # invalid or conflicting configuration
        ├── NotFound              # requested record/vault does not exist
        ├── StorageError          # IO/persistence failure
        └── LockConflict          # second writer opens a held database
              └── ParseError      # malformed EQL input (in eql namespace)
```

Each exception type inherits constructors from its base:

```cpp
class DimensionMismatch : public ElipsError {
public:
    using ElipsError::ElipsError;
};
```

### Exception Usage (E.16: throw by value, catch by reference)

```cpp
// Throwing — throw by value with descriptive message
throw DimensionMismatch{"expected dimension " + std::to_string(dim_) +
                        ", got " + std::to_string(vector.dimension())};
throw ConfigError{"database identity dimension mismatch"};
throw LockConflict{"database is already open by another writer: " + lock_path};
throw ParseError{"expected 'seek', 'fetch', 'scan', 'place', or 'erase'"};

// Catching — catch by const reference, most-derived first
try {
    vault.place(vector, payload);
} catch (const DimensionMismatch& e) {
    // handle dimension error
} catch (const InvalidVector& e) {
    // handle invalid vector
} catch (const ElipsError& e) {
    // handle any ELIPS error
}
```

### Internal error patterns

- `WAL::replay()` uses CRC32C validation to detect corrupt records; truncated tails are silently dropped (clean recovery, no exception).
- `LockManager` constructor throws `LockConflict` on `flock()` failure (non-blocking lock).
- `metric_from_string()` throws `ConfigError` for unknown metric names.
- Index operations (`insert`, `remove`, `search`) assume validated inputs; the Vault and Transaction layers perform pre-validation.

## Header Include Style

External includes first, then project headers alphabetically:

```cpp
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "elips/Config.hpp"
#include "elips/domain/Errors.hpp"
#include "elips/storage/Serialization.hpp"
#include "elips/storage/WAL.hpp"
```

All project includes use quoted paths with the `elips/` prefix.

## GPU Code Conventions

GPU engine code follows the same conventions with some additions:

- Objective-C++ (`.mm` files) for Metal backend on Apple platforms.
- `#ifdef ELIPS_GPU_ENABLED` guards all GPU-conditional code.
- GPU-specific configuration uses a separate `GpuConfig` struct, not polluting the core `Config`.
- Backend selection uses a `GpuSelector` strategy pattern over the `GpuPort` interface.
- GPU memory management follows RAII via `GpuBuffer`, `GpuMemoryManager`, and `GpuMemoryPool`.

## Additional Patterns

### Anonymous namespaces for internal linkage

Test files and implementation files use anonymous namespaces for file-local symbols:

```cpp
namespace {
    float all_finite(std::span<const float> values) noexcept { ... }
    constexpr std::uint32_t snapshot_magic = 0xE1105E01U;
}
```

### Constexpr for compile-time constants

```cpp
constexpr std::uint32_t identity_magic = 0xE11D0001U;
constexpr const char* snapshot_file = "elips.snapshot";
```

### Static factory functions

```cpp
[[nodiscard]] static RecordID generate();       // UUIDv7 generation
[[nodiscard]] static RecordID from_string(...);  // parse from canonical form
[[nodiscard]] static Filter compare(...);        // leaf predicate factory
[[nodiscard]] static Filter not_(...);           // negation combinator
```

### Fluent builder pattern

Used for `Config` and `Filter`:

```cpp
auto config = Config{}
    .dimension(768)
    .metric(Metric::euclidean)
    .index(IndexType::graph)
    .durability(Durability::standard);

auto filter = Filter{}
    .field("category").equals("tech")
    .field("year").ge(2023);
```