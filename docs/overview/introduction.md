# ELIPS — Introduction

ELIPS is an embedded retrieval engine for vectors and documents.

It is designed for applications that want local, in-process retrieval without
operating a separate service: CLIs, desktop apps, notebooks, edge workloads,
single-process APIs, and test environments.

## What You Get

- ANN and exact vector search
- First-class documents, chunk coordinates, and embedding lineage
- Native text-first and hybrid query APIs
- Metadata filters with planner-side candidate narrowing
- WAL recovery plus segmented persistence
- Shared read-only mode for multi-reader serving
- Python and C++ SDKs over the same core

## Mental Model

An ELIPS database is a directory. A vault is a named partition inside that
database. Each record can carry:

- a vector
- metadata
- an optional source document
- optional chunk coordinates
- optional embedding provenance

The core APIs are:

- `place()` for explicit vectors
- `place_document()` for text ingestion through a configured embedder
- `seek()`, `seek_text()`, and `seek_hybrid()`
- `fetch()` and `scan()`
- `checkpoint()` and `compact()`

## Example

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


db = elips.open_with_config(
    ":memory:",
    elips.Config().dimension(2).text_embedder(toy_embed, provider="demo", model="toy"),
)
docs = db.vault("documents")
docs.place_document("alpha design note", {"kind": "design"})
print(docs.seek_text("alpha", top=1)[0].document.text)
```

## Read More

- [Architecture](../architecture.md)
- [Storage](../storage.md)
- [Python SDK](../python_sdk.md)
- [C++ SDK](../cpp_sdk.md)
