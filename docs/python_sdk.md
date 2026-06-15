# Python SDK

ELIPS ships two Python surfaces:

- Low-level bindings around the C++ core: `open()`, `open_with_config()`,
  `Database`, `Vault`, `Config`
- A modern wrapper: `connect()`, `Engine`, and `Arena`

Use the low-level API when you want full control over configuration and exact
parity with the C++ runtime. Use the modern wrapper when you want typed,
text-first ingestion and retrieval ergonomics.

## Build

```bash
cmake -S . -B build -G Ninja -DELIPS_BUILD_PYTHON=ON
cmake --build build --target elips_pymodule
export PYTHONPATH=$PWD/bindings/python
```

## Low-Level API

```python
import elips


def toy_embed(texts: list[str]) -> list[list[float]]:
    return [
        [
            1.0 if "alpha" in text.lower() else 0.0,
            1.0 if "beta" in text.lower() else 0.0,
        ]
        for text in texts
    ]


config = (
    elips.Config()
    .dimension(2)
    .metric("cosine")
    .index("graph")
    .segmented_storage(True)
    .metadata_acceleration(True)
    .text_embedder(toy_embed, provider="demo", model="toy")
)

db = elips.open_with_config("/tmp/elips-sdk", config)
docs = db.vault("documents")
docs.place_document("alpha design note", {"kind": "design"})
docs.place_document("beta runbook", {"kind": "ops"})

hits = docs.seek_text("alpha", top=2)
print(hits[0].document.text, hits[0].lineage.model)

plan = docs.explain_seek(
    [1.0, 0.0],
    top=1,
    where=elips.Filter().field("kind").equals("design"),
    has_text_component=True,
)
print(plan.strategy, plan.metadata_accelerated)
```

### Key low-level types

- `DocumentAttachment`: attached raw text / URI / MIME metadata
- `ChunkInfo`: document key plus positional chunk coordinates
- `EmbeddingLineage`: provider, model, revision, and typed attributes
- `QueryPlan`: planner output for vector and hybrid queries

### Query surfaces

- `Vault.seek(vector, ...)`
- `Vault.seek_text(text, ...)`
- `Vault.seek_hybrid(vector, text, ...)`
- `Vault.explain_seek(vector, ..., has_text_component=True)`
- `Database.query(eql, bindings=...)`

EQL remains the declarative vector/query DSL. Text-first retrieval currently
lives on the vault and modern wrapper APIs.

## Modern Python API

```python
engine = elips.connect(
    "/tmp/elips-modern",
    dimension=2,
    metric="cosine",
    embedder=toy_embed,
)
arena = engine.arena("documents")

keys = arena.ingest(
    texts=["alpha design note", "beta runbook"],
    meta=[{"kind": "design"}, {"kind": "ops"}],
)

rows = arena.pull(keys, include_vectors=True)
hits = arena.probe_text("alpha", top=2)
hybrid = arena.probe_hybrid([0.0, 1.0], "alpha", top=2)
```

`Arena` automatically uses native core text APIs when the database config has a
text embedder. If not, it falls back to Python-side embedding plus
`seek_hybrid()`.

## Persistence & Lifecycle

- `checkpoint()`: write manifest/segments or snapshot and truncate the WAL
- `compact()`: rebuild indexes and checkpoint
- `close()`: checkpoint and release locks
- `abandon()`: testing hook to leave recovery work in the WAL

Read-only open:

```python
reader = elips.open("/tmp/elips-sdk", access_mode="read_only")
print(reader.vault("documents").seek_text("alpha", top=1)[0].data)
```

Read-only mode requires an existing database and rejects writes with
`StorageError`.

## Typing

The package ships `py.typed` and a complete `_core.pyi` stub so IDEs and type
checkers understand the public API, including the modern wrapper classes.
