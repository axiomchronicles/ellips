# Quick Start

This guide walks you through the core ELIPS Python API in five minutes.

## Import

```python
import elips

print(elips.__version__)
```

## Open a Database

`elips.open()` accepts a filesystem path or `":memory:"` for ephemeral databases:

```python
db = elips.open("/data/vectors", dimension=1536, metric="cosine")
```

Available parameters:

| Parameter | Type | Default | Description |
|---|---|---|---|
| `path` | `str` | required | Filesystem path or `":memory:"` |
| `dimension` | `int` | `0` | Vector dimension (required for new databases) |
| `metric` | `str` | `"cosine"` | Similarity metric: `"cosine"`, `"euclidean"`, `"dot_product"` |
| `index` | `str` | `"graph"` | Index backend: `"graph"` (HNSW) or `"exact"` (brute-force) |

### Context Manager

Use the database as a context manager for automatic checkpoint and lock release:

```python
with elips.open("/data/vectors", dimension=384) as db:
    docs = db.vault("documents")
    docs.place([1.0, 2.0, 3.0], {"title": "Hello"})
# db.close() called automatically on exit
```

## Configuration Builder

For fine-grained control, use the `Config` builder with `open_with_config()`:

```python
from elips import Config, GraphParams

config = (Config()
    .dimension(1536)
    .metric("cosine")
    .index("graph")
    .graph_params(GraphParams(max_connections=32, ef_construction=400, ef_search=80))
    .durability("standard"))

db = elips.open_with_config("/data/vectors", config)
```

## Vaults

A vault is a named partition of records within a database. Each vault owns its own index.

```python
docs = db.vault("documents")       # Access or lazily create
print(docs.name)                    # "documents"
```

List all vaults in a database:

```python
print(db.list_vaults())             # ["documents", "images", ...]
```

## CRUD Operations

### Place (Insert)

Insert a single record with an embedding vector and optional metadata:

```python
rid = docs.place(
    vector=[0.1, 0.2, 0.3],
    data={"title": "Getting Started", "year": 2024, "pinned": True},
)
print(rid)  # "018f3c..." (UUIDv7 hex string, 36 characters)
```

You can optionally specify a custom UUIDv7 ID:

```python
rid = docs.place(
    vector=[0.4, 0.5, 0.6],
    data={"title": "Advanced"},
    id="00000000-0000-7000-8000-000000000001",
)
```

### Place Many (Batch Insert)

Insert multiple records at once:

```python
docs.place_many([
    {"vector": [1.0, 0.0, 0.0], "data": {"n": 1}},
    {"vector": [0.0, 1.0, 0.0], "data": {"n": 2}},
    {"vector": [0.0, 0.0, 1.0], "id": "00000000-0000-7000-8000-000000000002", "data": {"n": 3}},
])
```

### Fetch

Retrieve a full record by ID:

```python
record = docs.fetch(rid)
if record:
    print(record["id"])        # "018f3c..."
    print(record["vector"])    # (0.1, 0.2, 0.3)
    print(record["data"])      # {"title": "Getting Started", ...}
```

Returns `None` if the record does not exist.

### Count and Info

```python
n = docs.count()              # 2
info = docs.info()            # VaultInfo(count=2, dimension=3, metric="cosine")
print(info.count, info.dimension, info.metric)
```

### Erase

```python
removed = docs.erase(rid)     # True if found and removed, False if not found
```

## Search (Seek)

Find the top-*k* nearest neighbors to a query vector:

```python
query = [1.0, 0.0, 0.0]
for hit in docs.seek(query, top=10):
    print(f"{hit.id}  distance={hit.distance:.4f}  data={hit.data}")
```

Parameters:

| Parameter | Type | Default | Description |
|---|---|---|---|
| `vector` | `Vector` | required | Query embedding |
| `top` | `int` | `10` | Number of results to return |
| `where` | `Filter` | `Filter()` | Optional metadata filter |
| `threshold` | `float` or `None` | `None` | Max distance for range search |

Results are sorted by distance ascending (closer = more similar). Each `Result` has:

| Attribute | Type | Description |
|---|---|---|
| `id` | `str` | Record UUIDv7 hex string |
| `distance` | `float` | Distance from query (smaller = more similar) |
| `data` | `dict` | Metadata payload |

## Filtering

Build metadata filters with the fluent `Filter` API. Chained predicates on the same field are AND-ed:

```python
f = (elips.Filter()
     .field("category").equals("tech")
     .field("score").gte(0.8)
     .field("country").one_of(["US", "GB"])
     .field("title").contains("intro"))

results = docs.seek(query, top=10, where=f)
```

### Boolean Combinators

```python
# OR: either condition matches
either = elips.Filter().field("tier").equals("pro").or_(
    elips.Filter().field("year").gte(2023)
)

# AND: explicit conjunction
combined = elips.Filter().field("cat").equals("tech").and_(
    elips.Filter().field("score").gte(90)
)

# NOT: negate a filter
not_draft = elips.Filter.not_(elips.Filter().field("draft").equals(True))
```

Full list of filter operators:

| Method | Description |
|---|---|
| `.field(name).equals(value)` | Field equals value |
| `.field(name).not_equals(value)` | Field does not equal value |
| `.field(name).lt(value)` | Field less than value |
| `.field(name).le(value)` | Field less than or equal to value |
| `.field(name).gt(value)` | Field greater than value |
| `.field(name).gte(value)` | Field greater than or equal to value |
| `.field(name).one_of([values])` | Field is one of a set of values |
| `.field(name).contains(substring)` | String field contains substring |
| `.and_(other)` | Logical AND with another filter |
| `.or_(other)` | Logical OR with another filter |
| `Filter.not_(inner)` | Logical NOT (static method) |

## Scan

Iterate records matching a filter in insertion order (no vector search):

```python
rows = docs.scan(
    where=elips.Filter().field("year").gte(2023),
    offset=0,
    limit=100,
)
for row in rows:
    print(row["id"], row["data"])
```

Parameters:

| Parameter | Type | Default | Description |
|---|---|---|---|
| `where` | `Filter` | `Filter()` | Metadata filter |
| `offset` | `int` | `0` | Records to skip |
| `limit` | `int` | `-1` | Max records (`-1` = all) |

Each scan row is a `dict` with `"id"` and `"data"` keys.

## Transactions

Group writes into an atomic, all-or-nothing batch:

```python
with db.begin_transaction() as txn:
    v = txn.vault("documents")
    v.place([1.0, 2.0], {"title": "A"})
    v.place([3.0, 4.0], {"title": "B"})
    # Committed automatically on clean exit
```

On exception, the transaction rolls back:

```python
try:
    with db.begin_transaction() as txn:
        txn.vault("docs").place([1.0, 2.0])
        raise RuntimeError("abort")
except RuntimeError:
    pass
# Record was NOT persisted
```

Manual commit/rollback:

```python
txn = db.begin_transaction()
txn.vault("docs").place([1.0, 2.0], {"manual": True})
txn.commit()       # persist

txn2 = db.begin_transaction()
txn2.vault("docs").place([3.0, 4.0])
txn2.rollback()    # discard
```

The `TransactionVault` (obtained via `txn.vault(name)`) supports `place()` and `erase()` only. Search operations must go through the regular `Vault`.

## EQL Queries

Execute queries using the ELIPS Query Language:

```python
results = db.query(
    "seek in documents nearest $q top 10 where year >= 2022 yield",
    bindings={"q": [1.0, 0.0, 0.0]},
)

for hit in results:
    print(hit.id, hit.distance, hit.data)
```

EQL supports `place`, `seek`, `fetch`, `scan`, and `erase` statements with parameterized vectors via `$bindings`.

### Validate EQL

Check syntax without executing:

```python
elips.validate_eql("seek in docs nearest [1.0] top 5 yield")  # None if valid
elips.validate_eql("garbage !!!")  # raises ParseError
```

### Tokenize EQL

Inspect the token stream for tooling:

```python
tokens = elips.tokenize_eql("seek in docs nearest $q top 5")
for t in tokens:
    print(t.kind, t.text)   # TokenKind.word, "seek" / TokenKind.punct, "$" / ...
```

## Checkpoint & Close

```python
db.checkpoint()   # Flush snapshot to disk, truncate WAL
db.close()        # Checkpoint + release the file lock (idempotent)
db.abandon()      # Drop handle without checkpointing (simulates crash exit)
```

After `abandon()`, only the WAL remains on disk. The next `open()` recovers via WAL replay.

## Error Handling

All ELIPS errors derive from `elips.ElipsError` (itself a `RuntimeError`):

```python
try:
    docs.place([1.0, 2.0], {"key": "value"})
except elips.DimensionMismatch:
    print("Vector dimension doesn't match vault")
except elips.InvalidVector:
    print("Vector contains NaN or Inf")
except elips.ConfigError:
    print("Configuration conflict")
except elips.NotFound:
    print("Record or vault not found")
except elips.StorageError:
    print("IO or persistence failure")
except elips.LockConflict:
    print("Another writer holds the database lock")
except elips.ParseError:
    print("Malformed EQL query")
except elips.ElipsError:
    print("Some other ELIPS error")
```

## GPU Acceleration

Configure GPU when a compatible backend is available:

```python
if elips._has_gpu:
    from elips import GpuConfig, GpuPolicy, GpuIndexAlgorithm

    gpu = GpuConfig()
    gpu.policy = GpuPolicy.prefer_gpu
    gpu.algorithm = GpuIndexAlgorithm.brute_force
    gpu.device_memory_pool_mb = 512

    config = Config().dimension(1536).gpu(gpu)
    db = elips.open_with_config("/data/vectors", config)

    info = db.gpu_info()
    print(info.name, info.vendor, info.memory_gb)
```

## Next Steps

- [Configuration & Enums Reference](../api-reference/config-and-enums.md)
- [Database API Reference](../api-reference/database.md)
- [Vault API Reference](../api-reference/vault.md)
- [Filter API Reference](../api-reference/filter.md)
- [EQL API Reference](../api-reference/eql.md)
- [Type Stubs & IDE Support](../typing/type-stubs.md)