# ELIPS — Introduction

**E**mbedded **L**ocal **I**ndex & **P**ersistence **S**ystem

---

## What is ELIPS?

ELIPS is an embedded, in-process vector database written in C++23 from first
principles. It stores dense float32 vectors with dynamic metadata, provides
approximate and exact nearest-neighbor search across them, and persists
everything to disk with crash-safe durability — all without a server, daemon, or
network port.

The elevator pitch: **SQLite for vectors.** Include the library, point it at a
directory, and go. No `pip install chromadb && chroma run`, no Docker Compose
with three services, no cloud subscription.

```cpp
#include <elips/elips.hpp>

auto db = elips::open("/data/vectors",
    elips::Config{}.dimension(1536).metric(elips::Metric::cosine));
auto& docs = db->vault("documents");
docs.place(elips::Vector{embedding}, {{"title", std::string{"Example"}}});
for (auto& hit : docs.seek(elips::Vector{query}, 10)) { /* ... */ }
```

Same from Python:

```python
import elips
db = elips.open("/data/vectors", dimension=1536, metric="cosine")
docs = db.vault("documents")
docs.place(vector=embedding, data={"title": "Example"})
for hit in docs.seek(vector=query, top=10):
    print(hit.id, hit.distance, hit.data)
```

---

## Core Philosophy

ELIPS is built around a single conviction: vector search should not require
infrastructure. Most applications that need semantic search, RAG retrieval, or
recommendation are themselves libraries or single-process services. Spinning up a
separate vector database — managing its port, its daemon, its authentication, its
upgrades — is disproportionate for teams building CLIs, desktop apps, edge
devices, research notebooks, or CI pipelines.

Every design decision flows from this:

- **No server, no port, no daemon.** An ELIPS database is a directory on the
  local filesystem. The library loads it, serves queries from the calling
  process, and checkpoints on close.

- **One library, one wheel.** Distribution is a static/shared C++ library or a
  single `pip install` wheel. No runtime dependencies beyond a C++23 standard
  library.

- **Crash-safe by default.** Every write flows through a CRC32C-protected
  write-ahead log before in-memory state is mutated. `open()` recovers by loading
  the last atomic snapshot and replaying the WAL. A torn final write is cleanly
  detected and dropped.

- **Idiomatic in every language.** The C++ SDK exposes RAII value types and
  scoped enums. The Python SDK is `py.typed` with complete stubs, context
  managers, and idiomatic exception translation.

---

## Key Features

### Vector Search

Three similarity metrics, ordering-normalized so smaller distance = more similar
in every case:

| Metric | Formula | Normalization |
|--------|---------|---------------|
| `cosine` | `1 − dot(a, b)` | L2 on ingest + query |
| `euclidean` | `sqrt(Σ(aᵢ − bᵢ)²)` | none |
| `dot_product` | `−dot(a, b)` | none |

Two index backends behind the `IndexPort` interface:

- **HierarchicalGraphIndex** — a from-scratch HNSW implementation with
  probabilistic level assignment (`mL = 1/ln(M)`), beam search (`ef`), and the
  diversity neighbor-selection heuristic. Soft-tombstone deletes preserve graph
  navigation. Delivers ~0.97 recall@10 with sub-millisecond latency.
- **ExactIndex** — brute-force linear scan. Ground truth for benchmarks, and the
  right choice for small collections (≤10⁴ vectors).

### Metadata Filtering

Dynamic schema: metadata is a `map<string, variant<int64, double, bool,
string>>`. No schema declaration required. The `Filter` predicate tree supports:

- Comparison operators: `=`, `≠`, `<`, `≤`, `>`, `≥`
- Set membership: `IN [...]`
- Substring containment: `CONTAINS "…"`
- Boolean composition: `AND`, `OR`, `NOT`
- Fluent builder: `Filter().field("year").gte(2023).field("cat").equals("tech")`

Filters are evaluated over in-memory payloads. The same predicate engine is
shared by the SDK and the EQL query language.

### EQL — ELIPS Query Language

A small, expression-oriented language with a lexer, recursive-descent parser,
AST, and executor. Statements: `seek` (KNN + filter + threshold + rank_by +
project), `fetch`, `scan`, `place`, `erase`. Runs identically from C++, Python,
and CLI.

```eql
seek in articles nearest $q top 10
    where category = "finance" and year >= 2023
    project title, author yield
```

### GPU Acceleration (Optional)

Five backends behind the `GpuPort` interface, selected at startup via
`GpuDeviceManager`:

| Backend | Priority | Technology | Key Feature |
|---------|----------|------------|-------------|
| CUDA | 1 | cuVS/CAGRA, cuBLAS | 8−12× build speedup |
| HIP | 2 | hipVS/CAGRA, rocBLAS | AMD ROCm |
| Metal | 3 | MPSGraph, MSL kernels | Apple unified memory (zero-copy) |
| SYCL | 4 | oneMKL | Intel Arc/Flex/Max |
| Vulkan | 5 | SPIR-V compute shaders | Universal fallback |

GPU acceleration includes CAGRA graph construction, IVF-Flat/IVF-PQ compression,
dynamic query batching, mixed-precision search (FP16/INT8), and a multi-level
fallback chain (GPU index → GPU brute-force → CPU HNSW → CPU exact).

### Crash-Safe Persistence

Every database is a directory:

```
/my_db/
├── LOCK             # advisory writer lock (flock)
├── IDENTITY         # dimension, metric, index type
├── elips.snapshot   # atomic full state (temp-write + rename)
└── wal.log          # write-ahead log with CRC32C per record
```

Four durability levels: `paranoid` (flush every record), `standard` (flush every
record, group-commit window deferred), `relaxed` (buffer until checkpoint), and
`ephemeral` (no WAL, in-memory only via `:memory:` path).

### Python SDK

PyBind11 bindings with complete `py.typed` stubs. Context-manager support,
idiomatic exception translation (`ElipsError` hierarchy → Python exceptions),
fluent configuration, GPU inspection, and EQL execution. See
[Python SDK](../python_sdk.md).

### C++ SDK

Header-only public API via `#include <elips/elips.hpp>`. RAII resource
management, value semantics, scoped enums, `[[nodiscard]]` everywhere. See
[C++ SDK](../cpp_sdk.md).

### CLI

`elips` binary for inspection (`info`, `vaults`, `stats`, `verify`), EQL
execution (`query`), maintenance (`checkpoint`), import/export (JSON Lines), and
benchmarking. See [CLI](../cli.md).

---

## Design Goals

1. **Zero infrastructure.** A database is a directory. Link the library, no
   daemon to manage, no port to open, no configuration file.

2. **Crash-safe durability.** WAL-before-mutation with CRC32C per record. Atomic
   snapshot via temp-file + rename. Deterministic recovery: the same on-disk
   state always yields the same in-memory state.

3. **SIMD-accelerated metrics.** ARM NEON kernels for Apple Silicon (FMA
   dot-product, squared-L2), with a runtime-dispatch seam (`KernelFn` function
   pointers) for AVX2/AVX-512 on x86. Scalar fallback always available.

4. **Pluggable indexes.** `IndexPort` abstracts over `ExactIndex` and
   `HierarchicalGraphIndex`. Quantized indexes (PQ/OPQ/SQ), DiskANN, and segment
   compaction plug into the same interface.

5. **Pluggable GPU backends.** `GpuPort`, `GpuMemoryPort`, `GpuKernelPort`,
   `GpuStreamPort`, and `GpuIndexPort` form five narrow interfaces that CUDA,
   HIP, Metal, SYCL, and Vulkan each implement. Domain code never includes a
   backend header.

6. **C++ Core Guidelines compliance.** RAII everywhere, value semantics by
   default, immutability by default, scoped enums, no raw `new`/`delete`,
   `[[nodiscard]]` on all query functions.

---

## Comparison to Other Vector Databases

| Feature | ELIPS | Chroma | FAISS | Milvus | Qdrant | Pinecone |
|---------|-------|--------|-------|--------|--------|----------|
| **Deployment** | In-process library | Embedded + client/server | In-process library | Server + etcd | Server | Cloud only |
| **Infrastructure** | Zero | Minimal (embedded) | Zero | Significant | Significant | None (cloud) |
| **Language** | C++23 + Python | Python + Rust | C++ + Python | Go + C++ | Rust | Closed source |
| **Index** | HNSW + exact | HNSW | HNSW + IVF + PQ + … | HNSW + IVF + DiskANN + … | HNSW | Proprietary |
| **GPU** | CUDA/HIP/Metal/SYCL/Vulkan | None | Full GPU | GPU indexes | None | Included |
| **Query language** | EQL (parsed) | Where filter | None | Boolean expressions | Filter conditions | Metadata filtering |
| **Transactions** | Atomic batched | None | None | None | None | N/A |
| **Durability** | WAL + snapshot | SQLite-backed (ChromaDB) | None (in-memory) | WAL + object storage | WAL | Cloud-managed |
| **Locking** | Advisory file locks | None | N/A | N/A | N/A | N/A |
| **Distribution** | Library + wheel | pip install | pip/conda | Docker + pip | Docker | API key |

### When to use ELIPS

- You are building a library, CLI, desktop app, or single-process service and
  want to embed vector search directly.
- You need crash-safe durability without managing a separate database process.
- You want the same query language (EQL) to work identically from C++, Python,
  and a command-line tool.
- You need GPU acceleration on Apple Silicon (Metal, unified memory) or across
  multiple vendors via a unified port.
- You value static typing, `py.typed` stubs, and predictable C++23 performance.

### When to use something else

- You need horizontal scaling, replication, or sharding across many nodes
  (Milvus, Qdrant).
- You need a managed cloud service with zero operational overhead (Pinecone).
- You need production-hardened disk-resident indexes for billion-scale datasets
  today (FAISS with IVF-PQ on GPU).
- You prefer a pure-Python stack with no native compilation (Chroma in embedded
  mode, though ELIPS ships wheels for common platforms).

---

## Layered Hexagonal Architecture

ELIPS is organized as a hexagonal (ports-and-adapters) architecture with
dependencies pointing inward toward the domain layer. Each layer communicates
with the next through a narrow port (abstract interface), never depending on
concrete implementations in outer layers.

```
        Python SDK (PyBind11)        C++ SDK (elips.hpp)        elips CLI
                     \                    |                    /
                      \                   |                   /
                                 ElipsInstance
                  (lifecycle · config · vault registry · txn)
                                       |
        ┌──────────────┬──────────────┼──────────────┬───────────────┐
   Query Engine    Vault /       Index Engine     Metadata        Storage
     (EQL)       Vector Engine    (IndexPort)      (Filter)     (WAL+snapshot)
                                       |                              |
                              ExactIndex / HNSW              LockManager · disk
                                       |
                              GPU Engine (GpuPort)
                        CUDA / HIP / Metal / SYCL / Vulkan
```

The seven layers:

1. **Domain** — pure value types with no infrastructure dependencies.
2. **Vector Engine** — SIMD-accelerated distance functions with runtime dispatch.
3. **Index Engine** — pluggable index implementations behind `IndexPort`.
4. **Metadata** — predicate tree and fluent builder for filtering.
5. **Query Engine** — EQL lexer, parser, AST, and executor.
6. **Storage** — WAL, snapshot serialization, CRC32C integrity.
7. **SDK** — public API surface for C++ and Python consumers.

GPU acceleration extends the Index Engine with a parallel hierarchy
(`GpuPort` → `GpuMemoryPort` → `GpuKernelPort` → `GpuStreamPort` →
`GpuIndexPort`), each a narrow, single-responsibility interface.

See [Architecture](architecture.md) for a detailed treatment.

---

## License and Project Status

ELIPS is an open-source project. See the repository for license terms.

**v1.0 status:** The core and all surfaces are implemented from first principles
in C++23:

| Subsystem | Status |
|-----------|--------|
| Domain model (Vector, Record, RecordID/UUIDv7, Payload) | Done |
| Metrics: cosine, euclidean, dot product (NEON SIMD + scalar) | Done |
| Indexes: HNSW + ExactIndex behind IndexPort | Done |
| Storage: snapshots, WAL, crash recovery | Done |
| Single-writer / multi-reader file locking | Done |
| Metadata filtering + atomic transactions | Done |
| EQL (lexer, parser, AST, executor) | Done |
| CLI (info/vaults/query/export/import/verify/bench) | Done |
| Python bindings (PyBind11) + `py.typed` | Done |
| Benchmark suite | Done |
| GPU acceleration (CUDA/HIP/Metal/SYCL/Vulkan) | Interfaces defined |

See [Roadmap](../roadmap.md) for deferred capabilities and v1.0 hooks.