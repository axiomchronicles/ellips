# ELIPS

Embedded Local Index & Persistence System.

ELIPS is an in-process vector and document retrieval engine built in C++23 with
native Python bindings. It keeps the embedded deployment model of SQLite, but
adds ANN indexes, typed metadata filters, first-class document lineage, hybrid
retrieval, segmented persistence, and optional GPU-backed indexes.

```python
from __future__ import annotations

import elips


def toy_embed(texts: list[str]) -> list[list[float]]:
    return [
        [
            1.0 if "alpha" in text.lower() else 0.0,
            1.0 if "beta" in text.lower() else 0.0,
        ]
        for text in texts
    ]


engine = elips.connect(":memory:", dimension=2, embedder=toy_embed)
arena = engine.arena("documents")
arena.ingest(
    texts=["alpha design note", "beta incident runbook"],
    meta=[{"kind": "design"}, {"kind": "ops"}],
)

for hit in arena.probe_text("alpha", top=2):
    print(hit.key, hit.distance, hit.text, hit.meta)
```

## What Is Implemented

- Vector search with `graph` (HNSW) and `exact` indexes
- GPU index selection through the main `open()` / index factory path
- First-class `DocumentAttachment`, `ChunkInfo`, and `EmbeddingLineage`
- Native `place_document()`, `seek_text()`, `seek_hybrid()`, and `explain_seek()`
- Metadata acceleration for equality filters via `MetadataIndex`
- Segmented persistence with `elips.manifest` plus per-vault segment files
- `compact()` to rebuild indexes and rewrite the segment set
- Shared read-only mode with advisory locks
- WAL crash recovery, snapshot compatibility, typed filters, EQL, Python bindings

## Quick Start

Build the core and run the C++ and Python tests:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DELIPS_BUILD_PYTHON=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
PYTHONPATH=bindings/python python3 tests/python/test_bindings.py
```

Minimal low-level Python usage:

```python
import elips

config = (
    elips.Config()
    .dimension(2)
    .metric("cosine")
    .segmented_storage(True)
    .metadata_acceleration(True)
    .text_embedder(toy_embed, provider="demo", model="toy")
)

db = elips.open_with_config("/tmp/elips-demo", config)
docs = db.vault("documents")
docs.place_document("alpha design note", {"kind": "design"})
docs.place_document("beta runbook", {"kind": "ops"})
print(docs.seek_text("alpha", top=1)[0].document.text)
db.compact()
db.close()
```

## Storage Model

Persistent databases use:

```text
/my_db/
├── LOCK
├── IDENTITY
├── wal.log
├── elips.manifest          # when segmented storage is enabled
├── segments/
│   ├── vault_0_<epoch>.segment
│   └── vault_1_<epoch>.segment
└── elips.snapshot          # compatibility / non-segmented mode
```

Single-writer opens take an exclusive lock. Read-only opens take a shared lock
and reject writes.

## Documentation

- [Architecture](docs/architecture.md)
- [Storage](docs/storage.md)
- [Python SDK](docs/python_sdk.md)
- [C++ SDK](docs/cpp_sdk.md)
- [Python Quickstart](docs/python/getting-started/quickstart.md)
- [C++ Quickstart](docs/cpp/getting-started/quickstart.md)
- [API references](docs/python/api-reference/) / [C++ API references](docs/cpp/api-reference/)

## Status

The remaining roadmap is now focused on future work such as MVCC/versioned
reads, deeper text planning in EQL, quantized indexes, and broader distributed
or cloud-oriented integrations. See [docs/roadmap.md](docs/roadmap.md).
