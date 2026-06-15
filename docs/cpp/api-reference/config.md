# API Reference: Config

Namespace `elips`. Typed, validated configuration with fluent builder pattern. Declaration at `include/elips/Config.hpp:41`.

Once an `ElipsInstance` is constructed via `open()`, the config becomes immutable (the `Config` object is copied into the instance; further mutations on the original config object have no effect).

## Related Types

### Metric Enum

```cpp
enum class Metric { cosine, euclidean, dot_product };
```

| Value | Description |
|-------|-------------|
| `cosine` | Cosine similarity: `1 - dot(a, b)`. Vectors are L2-normalized on ingest. Default metric. |
| `euclidean` | Euclidean (L2) distance: `sqrt(sum((a_i - b_i)^2))`. |
| `dot_product` | Negated dot product: `-dot(a, b)`. Larger dot => smaller distance => sorts first. |

### IndexType Enum

```cpp
enum class IndexType { graph, exact };
```

| Value | Description |
|-------|-------------|
| `graph` | HierarchicalGraphIndex (HNSW). ANN index for high recall with sub-linear search. Default. |
| `exact` | ExactIndex. Brute-force linear scan. Guaranteed exact results; O(N) per search. |

### Durability Enum

```cpp
enum class Durability { paranoid, standard, relaxed, ephemeral };
```

| Value | Description |
|-------|-------------|
| `paranoid` | WAL flushed (`out_.flush()`) on every record. Syncs before acknowledgment. |
| `standard` | Same as paranoid: WAL flushed on every record. (Group commit window planned.) Default. |
| `relaxed` | WAL buffered: no flush per record. Flushed only on checkpoint/close. Faster writes, less crash protection. |
| `ephemeral` | No WAL created. In-memory databases use this implicitly. |

### GraphParams Struct

```cpp
struct GraphParams {
    std::size_t max_connections{16};    // M
    std::size_t ef_construction{200};   // beam width during build
    std::size_t ef_search{50};          // beam width during search
};
```

Tunable HNSW parameters. Only used when `IndexType::graph` is selected.

| Field | Default | Description |
|-------|---------|-------------|
| `max_connections` | 16 | M: maximum number of outgoing connections per node per layer. Base layer (level 0) uses `2 * M`. Higher layers use M. Value used in `level_mult_ = 1/ln(M)`. |
| `ef_construction` | 200 | Beam search width during index construction. Higher values increase build time and index quality (higher recall). |
| `ef_search` | 50 | Beam search width during query time. Higher values increase search latency and recall. The effective ef is `max(ef_search, k)`. |

**Defaults produce reasonable recall for most workloads.** Tuning typically involves:

- Increase `max_connections` for datasets with high intrinsic dimensionality.
- Increase `ef_construction` for higher recall at the cost of slower builds.
- Increase `ef_search` for higher recall at the cost of slower queries.

### String Conversion Utilities

```cpp
std::string_view to_string(Metric metric) noexcept;
Metric metric_from_string(std::string_view name);
```

- `to_string(Metric::cosine)` returns `"cosine"`, etc.
- `metric_from_string("cosine")` returns `Metric::cosine`, etc.
- `metric_from_string("unknown")` throws `ConfigError`.

---

## Config Class

### Construction

```cpp
Config();  // default constructor
```

Default values:

| Field | Default |
|-------|---------|
| `dimension_` | `0` (must be set explicitly before `open()`) |
| `metric_` | `Metric::cosine` |
| `index_` | `IndexType::graph` |
| `graph_` | `GraphParams{}` (M=16, ef_construction=200, ef_search=50) |
| `durability_` | `Durability::standard` |
| `gpu_` | `GpuConfig{}` (only when `ELIPS_GPU_ENABLED`) |

`Config` is a value type - freely copyable, movable. No virtual methods, no heap allocation.

### Fluent Builder Methods

All setters return `Config&` for method chaining.

#### dimension()

```cpp
Config& dimension(std::uint16_t dim) noexcept;
```

Sets the vector dimension. This is **required** for both in-memory and new file-backed databases. Each vault within the database shares this dimension.

```cpp
auto config = Config{}.dimension(768).metric(Metric::cosine);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `dim` | `std::uint16_t` | Number of float elements per vector. Must be > 0. |

#### metric()

```cpp
Config& metric(Metric metric) noexcept;
```

Sets the similarity metric used for indexing and search.

```cpp
auto config = Config{}.dimension(128).metric(Metric::euclidean);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `metric` | `Metric` | `cosine`, `euclidean`, or `dot_product`. |

#### index()

```cpp
Config& index(IndexType type) noexcept;
```

Selects the index implementation.

```cpp
// exact (ground truth)
auto config = Config{}.dimension(128).index(IndexType::exact);

// graph (HNSW, default)
auto config = Config{}.dimension(128).index(IndexType::graph);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `type` | `IndexType` | `graph` (HNSW) or `exact` (brute-force). |

#### graph_params()

```cpp
Config& graph_params(GraphParams params) noexcept;
```

Sets HNSW tuning parameters. Ignored when `index()` is `IndexType::exact`.

```cpp
auto config = Config{}.dimension(384).graph_params(
    GraphParams{.max_connections = 32, .ef_construction = 400, .ef_search = 100}
);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `params` | `GraphParams` | HNSW parameters struct (by value). |

#### durability()

```cpp
Config& durability(Durability level) noexcept;
```

Sets the write durability level.

```cpp
// fast, no per-write flush
auto config = Config{}.dimension(128).durability(Durability::relaxed);

// safest, flush every record
auto config = Config{}.dimension(128).durability(Durability::paranoid);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `level` | `Durability` | One of `paranoid`, `standard`, `relaxed`, `ephemeral`. |

**Note**: Setting `durability(Durability::ephemeral)` for a file-backed database skips WAL creation, meaning writes are lost on crash until the next checkpoint.

#### gpu() (GPU only)

```cpp
Config& gpu(gpu::GpuConfig config) noexcept;  // only when ELIPS_GPU_ENABLED
```

Sets GPU configuration (device policy, memory limits, algorithm selection, etc.). See `gpu::GpuConfig` for available fields.

```cpp
gpu::GpuConfig gpu_cfg;
gpu_cfg.policy = gpu::GpuPolicy::PreferGpu;
gpu_cfg.device_memory_pool_bytes = 2UL * 1024 * 1024 * 1024;

auto config = Config{}.dimension(768).gpu(std::move(gpu_cfg));
```

This method is only available when `ELIPS_GPU_ENABLED` is defined (i.e., at least one GPU backend was enabled at build time).

### Property Getters

All getters are `const noexcept`. Return by value or const reference.

```cpp
std::uint16_t dimension() const noexcept;
```

Returns the configured vector dimension. Returns `0` if never set.

```cpp
Metric metric() const noexcept;
```

Returns the configured similarity metric.

```cpp
IndexType index() const noexcept;
```

Returns the configured index type.

```cpp
const GraphParams& graph_params() const noexcept;
```

Returns a const reference to the HNSW tuning parameters.

```cpp
Durability durability() const noexcept;
```

Returns the configured durability level.

```cpp
const gpu::GpuConfig& gpu() const noexcept;         // GPU only
bool has_gpu() const noexcept;                       // GPU only
```

`gpu()` returns const reference to GPU config. `has_gpu()` returns true if `gpu_.policy != GpuPolicy::CpuOnly`. Both only available with `ELIPS_GPU_ENABLED`.

### GPU Config Type (`gpu::GpuConfig`)

`include/elips/gpu_engine/GpuConfig.hpp`

```cpp
struct GpuConfig {
    GpuPolicy policy{GpuPolicy::Auto};
    std::string preferred_backend;
    int32_t device_index{-1};
    IndexBuildMode index_build_mode{IndexBuildMode::GpuBuild_GpuServe};
    GpuIndexAlgorithm algorithm{GpuIndexAlgorithm::Auto};
    size_t device_memory_pool_bytes{0};
    size_t pinned_host_pool_bytes{256UL * 1024 * 1024};
    bool use_unified_memory{false};
    size_t default_ef_search_gpu{64};
    size_t dynamic_batch_window_us{500};
    size_t dynamic_batch_max_size{256};
    bool enable_fp16_search{false};
    GraphIndexBuildParams graph_params;
    IvfPqBuildParams ivf_pq_params;
    GpuPrecision search_precision{GpuPrecision::Auto};
    bool auto_rebuild_on_startup{false};
    float rebuild_threshold_ratio{0.1f};
    bool enable_profiling{false};
    bool emit_kernel_timings{false};
};
```

Key enums:

| Enum | Values | Description |
|------|--------|-------------|
| `GpuPolicy` | `Auto`, `PreferGpu`, `RequireGpu`, `CpuOnly`, `Specific` | GPU selection policy |
| `IndexBuildMode` | `GpuBuild_CpuServe`, `GpuBuild_GpuServe`, `Hybrid` | Build/serve location |
| `GpuIndexAlgorithm` | `Auto`, `CagraGraph`, `IvfFlat`, `IvfPq`, `BruteForce` | GPU index algorithm |
| `GpuPrecision` | `FP32`, `FP16`, `Int8`, `Auto` | Search precision |

### Usage Examples

**Minimal in-memory database**:

```cpp
auto db = elips::open(":memory:", Config{}.dimension(128));
```

**Persistent file database with Euclidean metric and exact search**:

```cpp
auto db = elips::open("./db", Config{}.dimension(768)
    .metric(Metric::euclidean)
    .index(IndexType::exact));
```

**High-recall HNSW with tuned parameters**:

```cpp
auto db = elips::open("./db", Config{}.dimension(1024)
    .metric(Metric::cosine)
    .index(IndexType::graph)
    .graph_params(GraphParams{
        .max_connections = 32,
        .ef_construction = 400,
        .ef_search = 100
    })
    .durability(Durability::relaxed));
```

**Reopen existing database (dimension automatically detected)**:

```cpp
auto db = elips::open("./db");  // reads IDENTITY, no dimension needed
std::cout << "dimension: " << db->config().dimension() << '\n';
```

**GPU-enabled configuration**:

```cpp
gpu::GpuConfig gpu;
gpu.policy = gpu::GpuPolicy::PreferGpu;
gpu.device_memory_pool_bytes = 1ULL * 1024 * 1024 * 1024;

auto db = elips::open("./db", Config{}.dimension(768).gpu(std::move(gpu)));
```