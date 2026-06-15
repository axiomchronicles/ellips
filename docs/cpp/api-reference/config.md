# API Reference: Config

`elips::Config` is the typed configuration builder used by `elips::open()`.

## Core Enums

```cpp
enum class Metric { cosine, euclidean, dot_product };
enum class IndexType { graph, exact };
enum class Durability { paranoid, standard, relaxed, ephemeral };
enum class AccessMode { read_write, read_only };
```

## Graph Parameters

```cpp
struct GraphParams {
    std::size_t max_connections{16};
    std::size_t ef_construction{200};
    std::size_t ef_search{50};
};
```

## Builder Methods

```cpp
elips::Config{}
    .dimension(768)
    .metric(elips::Metric::cosine)
    .index(elips::IndexType::graph)
    .graph_params({.max_connections = 32, .ef_construction = 400, .ef_search = 100})
    .durability(elips::Durability::standard)
    .access_mode(elips::AccessMode::read_write)
    .segmented_storage(true)
    .metadata_acceleration(true)
    .text_embedder(embedder);
```

Available setters:

- `dimension(std::uint16_t)`
- `metric(Metric)`
- `index(IndexType)`
- `graph_params(GraphParams)`
- `durability(Durability)`
- `access_mode(AccessMode)`
- `segmented_storage(bool)`
- `metadata_acceleration(bool)`
- `text_embedder(TextEmbedderPtr)`
- `gpu(gpu::GpuConfig)` when GPU support is compiled in

## Getters

- `dimension()`
- `metric()`
- `index()`
- `graph_params()`
- `durability()`
- `access_mode()`
- `segmented_storage()`
- `metadata_acceleration()`
- `text_embedder()`
- `has_text_embedder()`
- `gpu()` / `has_gpu()` when GPU support is available

## Behavioral Notes

- `dimension()` must be non-zero for new persistent databases and all
  `":memory:"` opens.
- Existing databases always reopen with the persisted identity.
- `access_mode(read_only)` requires an existing database.
- `segmented_storage(true)` is the default and enables manifest + segment files.
- `metadata_acceleration(true)` enables exact-match candidate narrowing through
  `MetadataIndex`.
- `text_embedder()` activates `Vault::place_document()` and native
  `Vault::seek_text()` embedding.

## Text Embedding

`text_embedder()` accepts `elips::TextEmbedderPtr`, typically a
`std::shared_ptr` to a user implementation of `TextEmbedderPort`:

```cpp
class TextEmbedderPort {
public:
    virtual ~TextEmbedderPort() = default;
    virtual Vector embed(std::string_view text) const = 0;
    virtual std::vector<Vector> embed_batch(
        const std::vector<std::string>& texts) const;
    virtual std::string_view provider_name() const noexcept = 0;
    virtual std::string_view model_name() const noexcept = 0;
};
```

When lineage is omitted during `place_document()`, ELIPS derives provider/model
from this interface.
