# Config & Enums

`Config` is the authoritative Python configuration builder for advanced ELIPS
features.

## `Config`

```python
config = (
    elips.Config()
    .dimension(2)
    .metric("cosine")
    .index("graph")
    .durability("standard")
    .access_mode("read_write")
    .segmented_storage(True)
    .metadata_acceleration(True)
    .text_embedder(toy_embed, provider="demo", model="toy")
)
```

### Builder Methods

- `dimension(dim: int)`
- `metric(metric: str)`
- `index(type: str)`
- `graph_params(params: GraphParams)`
- `durability(level: str)`
- `access_mode(mode: str)`
- `segmented_storage(enabled: bool)`
- `metadata_acceleration(enabled: bool)`
- `text_embedder(embedder, provider="...", model="...")`
- `gpu(config)` when GPU support is built

### Read-Only Properties

- `dimension_val`
- `metric_val` / `metric_enum`
- `index_val` / `index_enum`
- `graph_params_val`
- `durability_enum`
- `access_mode_val` / `access_mode_enum`
- `segmented_storage_enabled`
- `metadata_acceleration_enabled`
- `has_text_embedder`
- `gpu_val`

## `GraphParams`

```python
params = elips.GraphParams(
    max_connections=32,
    ef_construction=400,
    ef_search=100,
)
```

These tune the graph index when `index("graph")` is used.

## Core Enums

### `Metric`

- `cosine`
- `euclidean`
- `dot_product`

### `IndexType`

- `graph`
- `exact`

### `Durability`

- `paranoid`
- `standard`
- `relaxed`
- `ephemeral`

### `AccessMode`

- `read_write`
- `read_only`

### `Comparator`

- `eq`
- `ne`
- `lt`
- `le`
- `gt`
- `ge`

### `QueryStrategy`

Planner output enum used by `QueryPlan`:

- `ann_index`
- `exact_candidates`
- `full_scan`
- `text_probe`
- `hybrid_fusion`

## Query Planning Types

### `QueryPlan`

Returned by `Vault.explain_seek(...)`.

Fields:

- `strategy`
- `candidate_count`
- `metadata_accelerated`
- `gpu_index`
- `index_type`

## Lineage Types

### `DocumentAttachment`

- `text`
- `uri`
- `mime_type`

### `ChunkInfo`

- `document_key`
- `ordinal`
- `char_start`
- `char_end`

### `EmbeddingLineage`

- `provider`
- `model`
- `revision`
- `attributes`

## GPU Types

When the extension is built with GPU support, ELIPS also exposes:

- `GpuConfig`
- `GpuPolicy`
- `IndexBuildMode`
- `GpuIndexAlgorithm`
- `GpuPrecision`
- `GpuError`
- `GraphBuildAlgo`
- `GraphIndexBuildParams`
- `IvfPqBuildParams`
- `GpuDeviceInfo`
- `GpuMetricsSnapshot`

Use `elips._has_gpu` to detect availability at runtime.
