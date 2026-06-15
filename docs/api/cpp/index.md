# C++ API Reference

Comprehensive index of the ELIPS C++ API, organized by subsystem.

## Core API

- [**elips.hpp**](core/elips.md) — Main entry point
  - `elips::open()` — Open or create a database
  - `elips::ElipsInstance` — Database handle (lifecycle, vaults, queries, persistence)
  - `elips::Vault` — Named partition (place, seek, scan, fetch, erase)
  - `elips::VaultInfo` — Summary statistics for a vault
  - `elips::Transaction` — Atomic batched writes
  - `elips::TransactionVault` — Transaction-scoped vault proxy

- [**Config.hpp**](core/config.md) — Configuration
  - `elips::Config` — Fluent configuration builder (dimension, metric, index, durability, graph params)
  - `elips::Metric` — Similarity metric enum (`cosine`, `euclidean`, `dot_product`)
  - `elips::IndexType` — Index backend enum (`graph`, `exact`)
  - `elips::Durability` — Durability level enum (`paranoid`, `standard`, `relaxed`, `ephemeral`)
  - `elips::GraphParams` — HNSW tunable parameters (M, ef_construction, ef_search)

## Domain Types

- [**Vector.hpp**](domain/vector.md) — Vector value type
  - `elips::Vector` — Owned float32 vector with magnitude and L2-normalization
  - `values()` — Span of float components
  - `dimension()` — Number of components
  - `magnitude()` — L2 norm
  - `normalized()` — L2-normalized copy

- [**RecordID.hpp**](domain/record_id.md) — Record identity
  - `elips::RecordID` — UUIDv7 identifier (128-bit, time-ordered)
  - `generate()` — Generate new monotonic UUIDv7
  - `from_string()` — Parse canonical UUID string
  - `to_string()` — Convert to 8-4-4-4-12 format
  - `bytes()` — Raw 16-byte array

- [**Record.hpp**](domain/record.md) — Record
  - `elips::Record` — Vector + identity + metadata payload
  - `elips::MetaValue` — Typed metadata value (`variant<int64_t, double, bool, string>`)
  - `elips::Payload` — Key-value metadata map

- [**SearchResult.hpp**](domain/search_result.md) — Search results
  - `elips::SearchResult` — Record id, distance, and data payload

- [**Errors.hpp**](domain/errors.md) — Error hierarchy
  - `elips::ElipsError` — Base exception
  - `elips::DimensionMismatch` — Vector dimension mismatch
  - `elips::InvalidVector` — Non-finite vector values
  - `elips::ConfigError` — Invalid or conflicting configuration
  - `elips::NotFound` — Missing record or vault
  - `elips::StorageError` — IO or persistence failure

## Vector Engine

- [**Metrics.hpp**](vector_engine/metrics.md) — Distance functions
  - `elips::distance()` — Ordering-normalized distance (smaller = more similar)
  - `elips::requires_normalization()` — Whether a metric needs L2-normalized inputs
  - `elips::to_string()` — Metric enum to string
  - `elips::metric_from_string()` — String to metric enum

## Index Engine

- [**IndexPort.hpp**](index_engine/index_port.md) — Index interface (DIP)
  - `elips::IndexPort` — Abstract index with insert, remove, search
  - `Hit` — (RecordID, distance) pair type alias

- [**ExactIndex.hpp**](index_engine/exact_index.md) — Brute-force index
  - `elips::ExactIndex` — Linear scan, exact results, ground truth for benchmarks

- [**HierarchicalGraphIndex.hpp**](index_engine/hierarchical_graph_index.md) — HNSW index
  - `elips::HierarchicalGraphIndex` — Primary ANN index, layered navigable small world graph
  - Configurable via `GraphParams`

- [**IndexFactory.hpp**](index_engine/index_factory.md) — Index construction
  - `elips::make_index()` — Build the configured index implementation

## Kernel

- [**LockManager.hpp**](kernel/lock_manager.md) — File locking
  - `elips::LockManager` — RAII exclusive file lock (flock-based, single-writer enforcement)
  - `elips::LockConflict` — Exception for lock acquisition failure

## Metadata

- [**Filter.hpp**](metadata/filter.md) — Predicate Engine
  - `elips::Filter` — Predicate tree with fluent builder and combinators
  - `elips::Comparator` — Comparison operator enum (`eq`, `ne`, `lt`, `le`, `gt`, `ge`)
  - `field()` — Select metadata field
  - `equals()`, `not_equals()`, `lt()`, `le()`, `gt()`, `ge()` — Comparison predicates
  - `one_of()` — Set membership predicate
  - `contains()` — Substring match predicate
  - `and_()`, `or_()`, `not_()` — Boolean combinators
  - `matches()` — Evaluate against a payload
  - `matches_all()` — Check if filter is a pass-through

## Query Engine (EQL)

- [**AST.hpp**](query_engine/ast.md) — Abstract Syntax Tree
  - `elips::eql::Statement` — Variant of all statement types
  - `elips::eql::SearchStatement`, `FetchStatement`, `ScanStatement`, `InsertStatement`, `DeleteStatement`
  - `elips::eql::VectorRef` — Inline literal or bound variable vector reference

- [**EQLLexer.hpp**](query_engine/eql_lexer.md) — Lexer
  - `elips::eql::tokenize()` — Tokenize EQL source
  - `elips::eql::Token` — Token with kind, text, and numeric value
  - `elips::eql::TokenKind` — `word`, `number`, `string`, `punct`, `end`

- [**EQLParser.hpp**](query_engine/eql_parser.md) — Parser
  - `elips::eql::parse()` — Parse EQL source into AST
  - `elips::eql::ParseError` — Malformed EQL exception

- [**QueryExecutor.hpp**](query_engine/query_executor.md) — Executor
  - `elips::eql::execute()` — Execute a parsed statement against a database
  - Supports bindings for parameterized queries

## Storage

- [**WAL.hpp**](storage/wal.md) — Write-Ahead Log
  - `elips::WAL` — Append-only log with CRC32C integrity
  - `append_insert()` — Log a vector insertion
  - `append_erase()` — Log a record deletion
  - `reset()` — Truncate after checkpoint
  - `replay()` — Static: replay a log file, stopping at first corrupt record
  - `Entry` — Log entry struct with op, vault, id, vector, payload
  - `Op` — Operation enum (`insert`, `erase`)

- [**Serialization.hpp**](storage/serialization.md) — Binary Serialization
  - `elips::detail::put()` / `elips::detail::get()` — Binary read/write primitives
  - `elips::detail::put_string()` / `elips::detail::get_string()` — Length-prefixed strings
  - `elips::detail::put_payload()` / `elips::detail::get_payload()` — Metadata payload serialization
  - `elips::detail::crc32c()` — CRC32C checksum for WAL integrity

## GPU Engine

Conditional on `ELIPS_GPU_ENABLED`.

- [**GpuPort.hpp**](gpu_engine/gpu_port.md) — GPU backend interface
  - `elips::gpu::GpuPort` — Abstract GPU device (allocate, upload, download, compute)
  - `device_info()`, `is_available()`, `allocate_device()`, `upload()`, `download()`, `free_device()`
  - `compute_distances_batch()` — Batched distance computation on GPU

- [**GpuConfig.hpp**](gpu_engine/gpu_config.md) — GPU Configuration
  - `elips::gpu::GpuConfig` — GPU policy, algorithm, memory, precision settings
  - `elips::gpu::GpuPolicy` — Auto, PreferGpu, RequireGpu, CpuOnly, Specific
  - `elips::gpu::GpuIndexAlgorithm` — Auto, CagraGraph, IvfFlat, IvfPq, BruteForce
  - `elips::gpu::GpuPrecision` — Fp32, Fp16, Int8, Auto

- [**GpuDeviceInfo.hpp**](gpu_engine/gpu_device_info.md) — Device information
  - `elips::gpu::GpuDeviceInfo` — Name, vendor, memory, compute capability, bandwidth
  - `supports_cagra`, `supports_dynamic_batching`, `supports_half_precision_search`

- [**GpuDeviceManager.hpp**](gpu_engine/gpu_device_manager.md) — Device discovery
  - `elips::gpu::GpuDeviceManager` — Probe all available GPU devices
  - `probe_all_devices()`, `can_fit_index()`

- [**GpuSelector.hpp**](gpu_engine/gpu_selector.md) — Backend selection
  - `elips::gpu::GpuSelector` — Select best GPU backend based on policy and available devices
  - `select()` — Returns `std::optional<std::unique_ptr<GpuPort>>`

- [**GpuMemoryManager.hpp**](gpu_engine/gpu_memory_manager.md) — Memory management
  - `elips::gpu::GpuMemoryManager` — Allocate, deallocate, track peak usage
  - `initialize()`, `allocate()`, `deallocate()`, `allocate_pinned()`, `bytes_used()`, `peak_bytes_used()`

- [**GpuMemoryPool.hpp**](gpu_engine/gpu_memory_pool.md) — Memory pool
  - `elips::gpu::GpuMemoryPool` — Pooled GPU memory allocation

- [**DynamicBatcher.hpp**](gpu_engine/dynamic_batcher.md) — Dynamic batching
  - `elips::gpu::DynamicBatcher` — Coalesce concurrent queries into GPU batches
  - `enqueue()`, `start()`, `stop()`, `stats()`

- [**GpuBruteForceIndex.hpp**](gpu_engine/gpu_brute_force_index.md) — GPU brute-force
  - `elips::gpu::GpuBruteForceIndex` — GPU-accelerated exact search
  - `build_from_batch()`, `search()`, `size()`

- [**GpuGraphIndex.hpp**](gpu_engine/gpu_graph_index.md) — GPU graph index
  - `elips::gpu::GpuGraphIndex` — GPU-accelerated CAGRA graph index
  - `build_from_batch()`, `search()`, `size()`

- [**GpuMetricsSnapshot.hpp**](gpu_engine/gpu_metrics_snapshot.md) — GPU metrics
  - `elips::gpu::GpuMetricsSnapshot` — Performance statistics for GPU operations

## Getting Started

- [Installation & build](../cpp/getting-started/installation.md)
- [C++ SDK overview](../../cpp_sdk.md)