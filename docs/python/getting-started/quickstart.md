# Quick Start

This walkthrough uses the current ELIPS v2 Python surface: native document
ingestion, text and hybrid retrieval, segmented persistence, and the modern
`connect()` wrapper.

## 1. Build The Python Module

```bash
cmake -S . -B build -G Ninja -DELIPS_BUILD_PYTHON=ON
cmake --build build --target elips_pymodule
export PYTHONPATH=$PWD/bindings/python
```

## 2. Open A Database

Low-level open:

```python
import elips

db = elips.open(":memory:", dimension=2, metric="cosine")
```

Full configuration:

```python
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
    .segmented_storage(True)
    .metadata_acceleration(True)
    .text_embedder(toy_embed, provider="demo", model="toy")
)

db = elips.open_with_config("/tmp/elips-quickstart", config)
```

## 3. Ingest Documents

```python
docs = db.vault("documents")

chunk = elips.ChunkInfo()
chunk.document_key = "doc-alpha"
chunk.ordinal = 0
chunk.char_start = 0
chunk.char_end = 17

rid = docs.place_document(
    "alpha design note",
    {"kind": "design"},
    chunk=chunk,
)
docs.place_document("beta incident runbook", {"kind": "ops"})
```

You can also attach document metadata to explicit vectors:

```python
attachment = elips.DocumentAttachment(text="gamma appendix", mime_type="text/plain")
docs.place(
    [1.0, 0.0],
    {"kind": "appendix"},
    document=attachment,
)
```

## 4. Query

Vector search:

```python
hits = docs.seek([1.0, 0.0], top=2)
```

Text-first search:

```python
hits = docs.seek_text("alpha", top=2)
print(hits[0].document.text)
```

Hybrid search:

```python
hits = docs.seek_hybrid([0.0, 1.0], "alpha", top=2, lexical_weight=0.35)
```

Planner inspection:

```python
where = elips.Filter().field("kind").equals("design")
plan = docs.explain_seek([1.0, 0.0], top=1, where=where, has_text_component=True)
print(plan.strategy.name, plan.metadata_accelerated, plan.candidate_count)
```

## 5. Scan, Fetch, And Maintain

```python
record = docs.fetch(rid)
print(record["document"].text, record["chunk"].document_key)

rows = docs.scan(where=elips.Filter().field("kind").equals("design"))
print(rows[0]["id"], rows[0]["data"])

db.checkpoint()
db.compact()
db.close()
```

## 6. Shared Read-Only Reopen

```python
reader = elips.open("/tmp/elips-quickstart", access_mode="read_only")
print(reader.vault("documents").seek_text("beta", top=1)[0].data)
reader.close()
```

Read-only mode requires an existing database and rejects writes.

## 7. Modern Wrapper

If you prefer a more Pythonic, typed surface:

```python
engine = elips.connect(
    "/tmp/elips-modern",
    dimension=2,
    metric="cosine",
    embedder=toy_embed,
)
arena = engine.arena("documents")

keys = arena.ingest(
    texts=["alpha design note", "beta incident runbook"],
    meta=[{"kind": "design"}, {"kind": "ops"}],
)
hits = arena.probe_text("alpha", top=2)
hybrid = arena.probe_hybrid([0.0, 1.0], "alpha", top=2)
```

`Arena` maps directly onto the same core APIs and supports typed `Row` and
`Hit` objects.

## Next

- [Python SDK](../../python_sdk.md)
- [Database API Reference](../api-reference/database.md)
- [Vault API Reference](../api-reference/vault.md)
- [Config & Enums](../api-reference/config-and-enums.md)
