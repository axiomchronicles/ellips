# Architecture

ELIPS is layered with dependencies pointing inward toward the domain, following
hexagonal / clean-architecture principles. Every layer talks to the next through
a narrow port (abstract interface). A layer never depends on a concrete
implementation in an outer layer.

---

## The Seven Layers

```
Layer 7 (outermost):  SDK
Layer 6:              Storage
Layer 5:              Query Engine
Layer 4:              Metadata
Layer 3:              Index Engine
Layer 2:              Vector Engine
Layer 1 (innermost):  Domain

GPU Engine (parallel to Index Engine):
  GpuPort → GpuMemoryPort → GpuKernelPort → GpuStreamPort → GpuIndexPort
```

Dependencies flow inward: the SDK depends on Storage, Query Engine, and
Metadata; Storage depends on Domain; Index Engine depends on Vector Engine and
Domain. No outer layer is referenced by an inner layer.

---

## Layer 1: Domain (`include/elips/domain/`)

The innermost layer. Pure value types with **zero infrastructure dependencies**.
These types are used by every other layer.

### Types

| Type | File | Description |
|------|------|-------------|
| `Vector` | `Vector.hpp:14` | Owned `float32` vector with rich value semantics. Not a raw float array. Provides `values()`, `dimension()`, `magnitude()`, and `normalized()` (L2-normalized copy). |
| `RecordID` | `RecordID.hpp:13` | Stable, time-ordered 128-bit identity (UUIDv7). Byte order equals insertion-time order, so `std::map<RecordID, Record>` iterates deterministically. |
| `Record` | `Record.hpp:22` | A vector with identity and payload: `{RecordID id; Vector vector; Payload payload;}`. |
| `Payload` | `Record.hpp:18` | Dynamic metadata schema: `std::map<std::string, MetaValue>`. |
| `MetaValue` | `Record.hpp:15` | Typed scalar: `std::variant<std::int64_t, double, bool, std::string>`. |
| `SearchResult` | `SearchResult.hpp:11` | One hit from a `seek()`: `{RecordID id; float distance; Payload data;}`. Distance is ordering-normalized (smaller = more similar). |

### Error Hierarchy (`Errors.hpp`)

All ELIPS errors derive from `ElipsError : std::runtime_error`:

```
ElipsError
├── DimensionMismatch   — vector dimension ≠ vault dimension
├── InvalidVector       — NaN/Inf values
├── ConfigError         — invalid or conflicting configuration
├── NotFound            — record/vault does not exist
├── StorageError        — IO/persistence failure
├── LockConflict        — database held by another writer (extends ElipsError, in kernel/)
└── eql::ParseError     — malformed EQL input (extends ElipsError, in query_engine/)
```

Errors are thrown by value, caught by reference.

### Design Rules

- No includes from outside `domain/`.
- No `#include <fstream>`, no filesystem operations, no IO.
- Value semantics: copyable, movable, `<=>` comparable.
- `[[nodiscard]]` on all pure functions.

---

## Layer 2: Vector Engine (`include/elips/vector_engine/`)

A single header, `Metrics.hpp`, with one implementation file `src/Metrics.cpp`.

### Core API

```cpp
[[nodiscard]] float distance(Metric metric,
    std::span<const float> a, std::span<const float> b) noexcept;

[[nodiscard]] bool requires_normalization(Metric metric) noexcept;
```

### Ordering Normalization

All three metrics return values where **smaller = more similar**, so every
consumer sorts ascending and keeps the k smallest:

| Metric | Raw formula | Normalized |
|--------|------------|------------|
| `cosine` | `dot(a, b)` | `1 − dot(a, b)` |
| `euclidean` | `√ Σ(aᵢ − bᵢ)²` | `√ Σ(aᵢ − bᵢ)²` (already ascending) |
| `dot_product` | `dot(a, b)` | `−dot(a, b)` (negated) |

### SIMD Dispatch

`src/Metrics.cpp:76-92` defines a `Dispatch` struct with function pointers
(`KernelFn = float(*)(const float*, const float*, size_t) noexcept`):

```cpp
struct Dispatch {
    KernelFn dot;
    KernelFn sql2;
    Dispatch() {
#if defined(__ARM_NEON)
        dot = &dot_neon;      // FMA via vmlaq_f32
        sql2 = &sql2_neon;    // squared diff via vsubq_f32 + vmlaq_f32
#else
        dot = &dot_scalar;
        sql2 = &sql2_scalar;
#endif
    }
};
```

The dispatch is resolved once at static initialization via a Meyer's singleton.
On ARM (Apple Silicon), NEON kernels use 4-wide FMA operations with scalar tail
loops. On x86, the same function-pointer seam selects AVX2/AVX-512 kernels via
CPUID (deferred feature).

---

## Layer 3: Index Engine (`include/elips/index_engine/`)

### IndexPort — The Core Abstraction

`IndexPort` (`IndexPort.hpp:16`) is the dependency-inversion seam for all index
implementations:

```cpp
class IndexPort {
public:
    using Hit = std::pair<RecordID, float>;

    virtual void insert(const RecordID& id, std::span<const float> vector) = 0;
    virtual void remove(const RecordID& id) = 0;
    [[nodiscard]] virtual std::vector<Hit> search(
        std::span<const float> query, std::size_t k) const = 0;

    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
    [[nodiscard]] virtual std::string_view type_name() const noexcept = 0;
};
```

`Vault` holds a `std::unique_ptr<IndexPort>` and never knows which concrete index
is behind it. This is pure dependency inversion: the consumer (`Vault`) depends
only on the abstraction.

### HierarchicalGraphIndex (`HierarchicalGraphIndex.hpp`)

The primary ANN index. A from-scratch HNSW (Hierarchical Navigable Small World):

- **Data layout:** Row-major `std::vector<float> data_` with `dimension_` floats per node.
- **Graph structure:** `std::vector<std::vector<std::vector<NodeId>>> links_` — per-node, per-level neighbor lists.
- **Level assignment:** Probabilistic `mL = 1/ln(M)` where `M = GraphParams::max_connections`.
- **Search:** Beam search via `search_layer()` with `ef` parameter. Uses min-heap for candidate frontier, max-heap for result set (easy eviction of worst candidate).
- **Insertion:** Random level → find entry point → search each layer top-down → connect to neighbors with diversity heuristic (`connect()`).
- **Deletion:** Soft tombstone (`std::vector<bool> deleted_`). Node stays in graph for navigation but is excluded from result enumeration. `size()` returns `ids_.size() - deleted_count_`.
- **Internal node IDs:** `NodeId = std::uint32_t`, mapped from `RecordID` via `std::unordered_map<RecordID, NodeId> id_to_node_`.

### ExactIndex (`ExactIndex.hpp`)

Brute-force linear scan. Stores `ids_` and row-major `data_`. `search()` computes
distance to every vector, sorts ascending, returns top-k. Used as ground-truth
oracle for benchmark recall validation and as the index for small collections.

### IndexFactory (`IndexFactory.hpp`)

```cpp
[[nodiscard]] std::unique_ptr<IndexPort> make_index(
    const Config& config, std::uint16_t dimension);
```

Builds the `IndexPort` implementation selected by `Config::index()`. Callers
depend only on `IndexPort`, never on concrete index types.

---

## Layer 4: Metadata (`include/elips/metadata/`)

### Filter (`Filter.hpp`)

A predicate tree over a record's `Payload`. A default-constructed `Filter`
matches everything (`root_ == nullptr`).

**Node types:**

| Kind | Description |
|------|-------------|
| `cmp` | Field comparison: `field op value` where `op ∈ {eq, ne, lt, le, gt, ge}` |
| `in` | Set membership: `field IN [v1, v2, ...]` |
| `contains` | Substring match on string fields |
| `conj` | Logical AND of two sub-trees |
| `disj` | Logical OR of two sub-trees |
| `neg` | Logical NOT of a sub-tree |

**Two construction APIs:**

1. **Fluent builder (SDK):**
   ```cpp
   Filter().field("year").gte(2023).field("cat").equals("tech")
   ```
   Chained predicates are implicitly AND-ed together.

2. **Leaf factories + combinators (EQL executor):**
   ```cpp
   Filter::compare("year", Comparator::ge, std::int64_t{2023})
       .and_(Filter::in_set("country", {...}))
   ```

**Evaluation:** `matches(const Payload& payload) const` recursively walks the
tree. Missing keys evaluate to "no match" (null-like semantics). Numeric
cross-comparison between `int64` and `double` is supported. `contains` is a
substring match on string fields only.

---

## Layer 5: Query Engine (`include/elips/query_engine/`)

### EQL — ELIPS Query Language

A small, expression-oriented language with four components:

1. **Lexer (`EQLLexer.hpp:20`):** `tokenize(std::string_view)` → `std::vector<Token>`. Tokens: `word`, `number`, `string`, `punct`, `end`. Supports `#` line comments.

2. **Parser (`EQLParser.hpp:18`):** `parse(std::string_view)` → `Statement`. Recursive-descent. Throws `ParseError` on invalid syntax.

3. **AST (`AST.hpp:53`):** `Statement = std::variant<SearchStatement, FetchStatement, ScanStatement, InsertStatement, DeleteStatement>`. Each variant carries typed fields (vault name, vector ref, top-k, threshold, filter, projection, etc.).

4. **Executor (`QueryExecutor.hpp:20`):** `execute(statement, db, bindings)` → `std::vector<SearchResult>`. Dispatches on statement variant and calls `Vault::seek()`, `Vault::fetch()`, `Vault::scan()`, `Vault::place()`, or `Vault::erase()`.

### Statement Reference

| Statement | Syntax | Description |
|-----------|--------|-------------|
| `seek` | `seek in <vault> nearest <vec> [top N] [threshold T] [where F] [rank_by R] [project P] yield` | KNN search with filter, threshold, ranking, projection |
| `fetch` | `fetch from <vault> id "<uuid>" yield` | Retrieve a single record by ID |
| `scan` | `scan in <vault> [where F] [offset N] [limit N] yield` | Metadata-only scan, no vector |
| `place` | `place in <vault> vector [...] [data {...}]` | Insert a record |
| `erase` | `erase from <vault> id "<uuid>"` | Delete a record |

The `where` clause reuses `Filter`, keeping one predicate engine across the SDK
and the language.

---

## Layer 6: Storage (`include/elips/storage/`, `include/elips/kernel/`)

### WAL (`WAL.hpp`)

Write-ahead log for crash-safe durability. Every mutation is appended to disk
**before** in-memory state is mutated.

**Record frame:**

```
magic(0xE1105E01) | op(u8) | vault(len+bytes) | id(16B)
                  | [dim(u16) | float32[dim] | payload]     # insert only
                  | crc32c(u32)
```

**Operations:** `insert` (op=1), `erase` (op=3).

**Replay:** `WAL::replay(path)` reads all records, validates CRC32C per record,
stops cleanly at the first corrupt/truncated record, returns the valid prefix.
Deterministic: the same on-disk state always yields the same recovered state.

### Serialization (`Serialization.hpp`)

Internal binary primitives in `elips::detail`:

- `put<T>(ostream, value)` / `get<T>(istream)` — native-endian scalar I/O.
- `put_string` / `get_string` — length-prefixed (`uint32_t` len + bytes).
- `put_payload` / `get_payload` — count-prefixed key-value pairs with type tags (`uint8_t`).
- `crc32c(data, len)` — Castagnoli CRC, software table computed once at static init. Used for WAL record integrity.

Native byte order is used (single-machine embedded use). Cross-platform
normalization is deferred.

### LockManager (`kernel/LockManager.hpp`)

RAII advisory file lock enforcing the single-writer contract across processes
sharing a database directory.

- **Acquisition:** `flock(LOCK_EX | LOCK_NB)` on `<dir>/LOCK` at `open()`. Non-blocking: throws `LockConflict` if another writer holds it.
- **Release:** `flock(LOCK_UN)` + `close(fd)` on destruction.
- **Move semantics:** Movable (transfers fd ownership via `std::exchange`), non-copyable.
- **Platform:** POSIX `flock()` in v1.0. Windows `LockFileEx` at the same seam.

### On-Disk Layout

```
/my_db/
├── LOCK             # advisory writer lock
├── IDENTITY         # magic(0xE11D0001) | version | dim(u16) | metric(u8) | index(u8)
├── elips.snapshot   # full state, atomically written (temp + rename)
└── wal.log          # WAL entries since last checkpoint
```

### Recovery Protocol (`Database.cpp`)

`open()` performs:
1. Acquire writer lock (`LockConflict` if held).
2. Read `IDENTITY` (or create it for a new database). Validate against passed `Config`.
3. Load `elips.snapshot` if present — rebuild vaults and indexes from stored vectors.
4. Replay `wal.log` on top, applying inserts/erases.
5. Validate CRC32C per record; stop at first corrupt record (valid prefix recovered).
6. Attach a live WAL for subsequent appends.

`checkpoint()` serializes all vaults to `elips.snapshot.tmp`, atomically renames
it over `elips.snapshot`, then truncates the WAL.

---

## Layer 7: SDK (`include/elips/elips.hpp`)

The public API surface. Everything a consumer needs in one header.

### Config (`Config.hpp`)

Fluent builder, immutable intent after `open()`:

```cpp
Config{}.dimension(1536).metric(Metric::cosine).index(IndexType::graph)
        .graph_params({.max_connections=16, .ef_construction=200, .ef_search=50})
        .durability(Durability::standard)
```

Scoped enums: `Metric`, `IndexType`, `Durability`. Configurable `GraphParams`
(HNSW M, ef_construction, ef_search). Optional `GpuConfig` behind `#ifdef ELIPS_GPU_ENABLED`.

### ElipsInstance (`elips.hpp:79`)

Top-level database handle. Owns all vaults, the WAL, and the lock manager.
Non-copyable, non-movable.

- `vault(name)` — get or create a named vault.
- `begin_transaction()` — start an atomic batched transaction.
- `query(eql, bindings)` — execute EQL against the database.
- `checkpoint()` — flush snapshot, truncate WAL.
- `close()` — checkpoint + release lock (idempotent).
- `abandon()` — close without checkpointing (emergency teardown).

### Vault (`elips.hpp:40`)

A named partition of records. Owns its `IndexPort` and authoritative record store
(`std::map<RecordID, Record>`).

- `place(vector, payload, id?)` — insert a record. WAL-before-mutation. Returns `RecordID`.
- `seek(query, top, filter?, threshold?)` — KNN search with optional filter and threshold.
- `scan(filter?, offset?, limit?)` — metadata-only linear scan (no vector).
- `fetch(id)` — retrieve a record by ID.
- `erase(id)` — delete a record. WAL-before-mutation.
- `info()` — summary statistics.

Vectors are validated on `place`/`seek` (dimension check, NaN/Inf rejection) and
L2-normalized for cosine metric via `prepare()`.

### Transaction (`elips.hpp:149`)

Atomic batched operation buffer:
- Operations enqueued on `TransactionVault::place()`/`erase()`.
- Eagerly validated at enqueue time (so `commit()` cannot fail mid-batch).
- `commit()` applies all operations atomically.
- Un-committed transaction rolls back on destruction.

---

## GPU Engine (Parallel to Index Engine)

When `ELIPS_GPU_ENABLED` is defined, a parallel GPU engine extends the Index
Engine. It follows the same dependency-inversion pattern with five narrow ports.

### Interface Segregation

| Port | Responsibility |
|------|---------------|
| `GpuPort` | Device lifecycle, allocation, data transfer, distance computation, top-K |
| `GpuMemoryPort` | Pool allocation, pinned host memory, usage tracking |
| `GpuKernelPort` | Per-metric distance kernels (cosine, euclidean, dot_product) |
| `GpuStreamPort` | Async operation synchronization |
| `GpuIndexPort` | Index build/batch search/import/export (extends `IndexPort`) |

### Backend Hierarchy

```
GpuPort (abstract interface)
  ← CudaBackend   (cuVS/CAGRA, cuBLAS)
  ← HipBackend    (hipVS/CAGRA, rocBLAS)
  ← MetalBackend  (MPSGraph, MSL compute kernels)
  ← SyclBackend   (oneMKL, SYCL compute)
  ← VulkanBackend (SPIR-V compute shaders)
```

### Device Selection (`GpuDeviceManager`)

1. `probe_all_devices()` — enumerate available GPUs across all compiled backends.
2. `select(config, devices)` — pick the best device per `GpuPolicy::Auto` ranking
   (CUDA → HIP → Metal → SYCL → Vulkan) and `GpuConfig::preferred_backend` override.
3. `can_fit_index(dev, n_vectors, dim, config)` — memory budget check.

### Fallback Chain

```
Level 0: GPU Index (CAGRA / IVF-PQ / IVF-Flat)
  ↓ failure
Level 1: GPU BruteForce (exact results, no index needed)
  ↓ failure
Level 2: CPU HierarchicalGraphIndex (HNSW, approximate)
  ↓ failure
Level 3: CPU ExactIndex (brute force, always available)
```

### Key GPU Components

| Component | Role |
|-----------|------|
| `GpuMemoryManager` | Best-fit pool allocator from device VRAM |
| `GpuMemoryPool` | Fixed-size pool for index storage |
| `PinnedBuffer` | RAII pinned host memory for async DMA |
| `UnifiedBuffer` | Zero-copy shared memory on Apple Silicon |
| `DynamicBatcher` | Push-model query coalescing with configurable time window (500µs) |
| `GpuSearchPipeline` | Orchestrates search: upload → compute → download |
| `GpuIngestionPipeline` | Orchestrates batch ingestion into GPU indexes |
| `GpuQuantizationPipeline` | FP32 → FP16/INT8 conversion |
| `GpuProfiler` | Kernel timing and memory statistics |
| `GpuSelector` | Backend ranking and selection logic |
| `GpuHybridIndex` | Wraps GPU + CPU indexes, routes batch ops to GPU |
| `GpuGraphIndex` | CAGRA graph index (CUDA/HIP) |
| `GpuIVFFlatIndex` | IVF with flat quantization |
| `GpuIVFPQIndex` | IVF with product quantization |
| `GpuBruteForceIndex` | GPU brute-force exact search |

---

## Key Invariants

1. **`distance()` is ordering-normalized.** All result lists sort ascending. No
   consumer flips sign directionally.

2. **Cosine vectors are L2-normalized on ingest and query.**
   `Vault::prepare()` calls `vector.normalized()` when
   `requires_normalization(metric)` is true.

3. **WAL-before-mutation.** `Vault::place()` and `Vault::erase()` append to the
   WAL and flush (per durability level) **before** mutating the in-memory index
   or record map.

4. **`RecordID` byte order = UUIDv7 time order.** The `std::map<RecordID,
   Record>` iterates in insertion-time order, producing deterministic snapshots.

5. **Indexes are rebuilt on load.** The snapshot stores vectors, not index
   internals. On `open()`, vectors are re-inserted into freshly constructed
   indexes. For HNSW, `ml` and random level assignment are deterministic given
   the same PRNG seed (future: persist seed for bit-reproducible indexes).

6. **GPU backends never included in domain code.** All GPU headers are gated
   behind `#ifdef ELIPS_GPU_ENABLED` in `elips.hpp`. Domain code calls through
   `GpuPort` only.

---

## Design Principles Applied

- **SOLID.** Single responsibility per class. Open for extension (new `IndexPort`,
  new `GpuPort` backends), closed for modification of core.
- **Dependency Inversion.** Consumers depend on `IndexPort`, `Filter`, `GpuPort`
  — never concrete implementations.
- **RAII.** `LockManager` acquires on construct, releases on destruct. WAL opens
  on construct, closes on destruct. `ElipsInstance` checkpoints on destruct.
  `Transaction` rolls back on destruct if not committed.
- **Immutability by default.** `Config` is immutable after `open()`. `Filter`
  tree nodes are `shared_ptr<const Node>`. `Vector` is a value type.
- **Value semantics.** `Vector`, `RecordID`, `Record`, `SearchResult` are
  copyable, movable, and comparable.
- **Scoped enums.** `Metric`, `IndexType`, `Durability`, `Comparator`,
  `GpuPolicy`, `GpuIndexAlgorithm`, `GpuPrecision`, `TokenKind` — all
  `enum class`.
- **`[[nodiscard]]`** on all query functions, factory functions, and pure
  computations.
- **No raw `new`/`delete`.** `std::unique_ptr`, `std::shared_ptr`,
  `std::make_unique` throughout.

---

## Dependency Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        SDK (elips.hpp)                          │
│  ElipsInstance · Vault · Transaction · Config · open()          │
│  #ifdef ELIPS_GPU_ENABLED → GpuDeviceInfo, GpuMetricsSnapshot   │
└──────────┬──────────┬──────────┬──────────┬────────────────────┘
           │          │          │          │
    ┌──────┘    ┌─────┘    ┌────┘    ┌─────┘
    ▼           ▼          ▼         ▼
┌────────┐ ┌─────────┐ ┌───────┐ ┌──────────────┐
│ Query  │ │ Vault   │ │ Filter│ │   Storage    │
│ Engine │ │▸ holds  │ │       │ │              │
│        │ │  IndexPort      │ │ │ WAL          │
│ EQL    │ │▸ holds  │ │       │ │ Serialization│
│ Lexer  │ │  records│ │       │ │ LockManager  │
│ Parser │ │         │ │       │ │              │
│ AST    │ │ Vector  │ │       │ │   kernel/    │
│Executor│ │ Engine  │ │       │ │ LockManager  │
└───┬────┘ └────┬────┘ └───┬───┘ └──────┬───────┘
    │           │           │            │
    ▼           ▼           ▼            ▼
┌──────────────────────────────────────────────────────────┐
│                    DOMAIN (innermost)                     │
│  Vector · RecordID · Record · Payload · SearchResult      │
│  ElipsError → DimensionMismatch · InvalidVector · ...     │
└──────────────────────────────────────────────────────────┘

GPU Engine (parallel to Index Engine, compiled conditionally):
┌─────────────────────────────────────────────────────────────┐
│  GpuPort ← CudaBackend / HipBackend / MetalBackend /        │
│             SyclBackend / VulkanBackend                     │
│  GpuMemoryPort → GpuMemoryManager · GpuMemoryPool           │
│  GpuKernelPort → cosine_fp32 / euclidean_fp32 / dot_product │
│  GpuStreamPort → synchronize / is_complete                   │
│  GpuIndexPort → GpuGraphIndex · GpuIVFFlatIndex ·           │
│                  GpuIVFPQIndex · GpuBruteForceIndex ·       │
│                  GpuHybridIndex                             │
│  Support: DynamicBatcher · GpuProfiler · GpuSelector         │
│            GpuSearchPipeline · GpuIngestionPipeline          │
│            GpuQuantizationPipeline                           │
└─────────────────────────────────────────────────────────────┘
```

---

## Build System

CMake ≥ 3.24, Ninja, Clang 17+ or GCC 13+ for C++23.

**Targets:**

| Target | Description |
|--------|-------------|
| `elips_core` | Static library, the ELIPS core |
| `elips` (elips_cli) | CLI binary |
| `elips_bench` | Benchmark suite |
| `elips_gpu_bench` | GPU benchmark suite (conditional) |
| `elips_tests` | Unit + integration + recovery + concurrency tests (GoogleTest) |
| `elips_pymodule` | Python extension module (PyBind11, conditional) |

**GPU backend flags:** `ELIPS_GPU_CUDA`, `ELIPS_GPU_HIP`, `ELIPS_GPU_METAL`,
`ELIPS_GPU_SYCL`, `ELIPS_GPU_VULKAN`. If any is `ON`, `ELIPS_GPU_ENABLED` is
`ON`, `elips_gpu` is built and linked, and the preprocessor flag
`ELIPS_GPU_ENABLED` is set.

**Test structure:** `tests/unit/` (per-component), `tests/integration/`
(cross-component), `tests/recovery/` (crash recovery scenarios),
`tests/concurrency/` (locking), `tests/parity/` (CPU vs GPU recall comparison).