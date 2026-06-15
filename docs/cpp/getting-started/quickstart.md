# Quickstart

A minimal working example that creates an in-memory database, inserts vectors, searches, and filters.

## Minimal Example

```cpp
#include <iostream>
#include "elips/elips.hpp"

int main() {
    auto db = elips::open(
        ":memory:",
        elips::Config{}.dimension(3).metric(elips::Metric::cosine));

    auto& docs = db->vault("documents");

    docs.place(elips::Vector{{1.0F, 0.0F, 0.0F}},
               {{"title", std::string{"alpha"}}, {"year", std::int64_t{2024}}});
    docs.place(elips::Vector{{0.0F, 1.0F, 0.0F}},
               {{"title", std::string{"beta"}}, {"year", std::int64_t{2019}}});

    std::cout << "nearest to [1, 0, 0]:\n";
    for (const auto& hit : docs.seek(elips::Vector{{0.9F, 0.1F, 0.0F}}, 2)) {
        std::cout << "  " << std::get<std::string>(hit.data.at("title"))
                  << "  " << hit.distance << '\n';
    }

    const auto recent = elips::Filter().field("year").ge(std::int64_t{2023});
    std::cout << "filtered (year >= 2023):\n";
    for (const auto& hit : docs.seek(elips::Vector{{1.0F, 0.0F, 0.0F}}, 5, recent)) {
        std::cout << "  " << std::get<std::string>(hit.data.at("title")) << '\n';
    }

    return 0;
}
```

Output:
```
nearest to [1, 0, 0]:
  alpha  0.0116973
  beta   0.988302
filtered (year >= 2023):
  alpha
```

Compile and run (assuming ELIPS built in `./build`):
```bash
c++ -std=c++23 -I./include example.cpp ./build/libelips_core.a -o example
./example
```

## Opening a Database

### In-Memory Database

Use the special path `":memory:"`. The dimension must be specified in the config:

```cpp
auto db = elips::open(":memory:", elips::Config{}.dimension(128));
```

In-memory databases are non-persistent (`persistent_ = false`), skip the WAL and advisory lock, and are destroyed when the `ElipsInstance` goes out of scope.

### File-Backed (Persistent) Database

Use a filesystem path. The directory is created if it doesn't exist:

```cpp
auto db = elips::open("./data/my_vector_db", elips::Config{}.dimension(768));
```

On first open, an `IDENTITY` file is written recording the dimension, metric, and index type. Subsequent opens read this identity; providing a conflicting dimension throws `ConfigError`. A `LOCK` file enforces single-writer access via `flock(LOCK_EX | LOCK_NB)`. A `wal.log` captures writes durably before the next checkpoint. The `elips.snapshot` file stores the last checkpoint snapshot.

### Reopening

When reopening an existing database, dimension defaults may be omitted and are read from `IDENTITY`:

```cpp
auto db = elips::open("./data/my_vector_db"); // dimension inherited from IDENTITY
```

## Creating a Vault

A vault is a named partition of records within a database. Each vault shares the database's dimension, metric, and index configuration. Vaults are lazily created on first access:

```cpp
auto& images = db->vault("images");    // creates "images" vault if not present
auto& docs   = db->vault("documents"); // creates "documents" vault if not present
```

The vault name is the partition key. All mutations in a vault use the same index and config.

## Inserting Vectors

### Single Insert

```cpp
elips::RecordID id = docs.place(
    elips::Vector{{0.1F, 0.2F, 0.3F}},
    {{"category", std::string{"article"}}, {"score", std::int64_t{95}}}
);
```

- The vector is validated: dimension must match the database config (throws `DimensionMismatch`), and all values must be finite (throws `InvalidVector`).
- If the metric requires normalization (`Metric::cosine`), the vector is L2-normalized before insertion.
- A UUIDv7 `RecordID` is generated automatically if not provided.

### Insert with Explicit ID

```cpp
auto id = elips::RecordID::from_string("01931d0a-1234-7abc-8000-abcdef123456");
docs.place(elips::Vector{{0.1F, 0.2F}}, {}, id);
```

### Batch Insert

```cpp
std::vector<elips::Record> batch;
batch.push_back({elips::RecordID::generate(), elips::Vector{{1.0F, 0.0F}},
                  {{"tag", std::string{"a"}}}});
batch.push_back({elips::RecordID::generate(), elips::Vector{{0.0F, 1.0F}},
                  {{"tag", std::string{"b"}}}});
docs.place_many(batch);
```

Records with a default-constructed (zero) `RecordID` get auto-generated IDs; records with non-zero IDs use the provided ID.

## Searching (Vector Similarity)

The `seek()` method finds the `k` nearest neighbors to a query vector:

```cpp
auto results = docs.seek(
    elips::Vector{{0.9F, 0.1F, 0.0F}}, // query vector
    5                                     // top-k
);
```

Returns `std::vector<SearchResult>`, sorted by distance ascending (smaller = more similar, normalized across all metrics). Each `SearchResult` contains:

- `id` (`RecordID`) — the record identity
- `distance` (`float`) — the ordering-normalized distance
- `data` (`Payload`) — the record's metadata

### With Threshold

Limit results to those within a distance threshold:

```cpp
auto results = docs.seek(query, 100, elips::Filter{}, 0.5f);
```

### With Metadata Filter

Filter results by payload fields before returning:

```cpp
auto filter = elips::Filter()
    .field("category").equals(std::string{"article"})
    .field("year").ge(std::int64_t{2023});
auto results = docs.seek(query, 10, filter);
```

When a filter is present, the engine over-fetches candidates from the index (20x `top`) to account for post-filter drops.

## Filtering (Metadata Search Without Vectors)

Use `scan()` to iterate records by metadata without vector similarity:

```cpp
auto filter = elips::Filter().field("status").equals(std::string{"active"});
auto records = docs.scan(filter, 0, 50);  // offset=0, limit=50
```

`scan()` iterates through the in-memory record store sequentially, applying the filter predicate and respecting offset/limit.

## Fetching by ID

Retrieve a specific record by its `RecordID`:

```cpp
auto record = docs.fetch(id);
if (record) {
    std::cout << "found: " << record->id.to_string() << '\n';
}
```

Returns `std::nullopt` if the record is not found.

## Deleting by ID

```cpp
bool removed = docs.erase(id);
```

Returns `false` if the record was not found. For HNSW indexes, removal is a soft tombstone (graph structure preserved; deleted nodes excluded from search results).

## Transactions

Transactions allow buffering multiple mutations and applying them atomically:

```cpp
auto txn = db->begin_transaction();

auto& vault = txn.vault("documents");
vault.place(elips::Vector{{1.0F, 0.0F, 0.0F}},
            {{"title", std::string{"gamma"}}});
vault.place(elips::Vector{{0.0F, 1.0F, 0.0F}},
            {{"title", std::string{"delta"}}});
vault.erase(existing_id);

txn.commit();  // all ops applied atomically
```

Key behaviors:
- Operations are validated eagerly (dimension, vector finiteness) at enqueue time, so `commit()` cannot fail mid-batch.
- If a transaction is destroyed without `commit()`, all buffered operations are silently discarded (auto-rollback in destructor).
- A default-constructed `TransactionVault` is obtained via `txn.vault("name")` and is tied to its parent transaction's lifetime.

## Checkpoint and Close Lifecycle

```cpp
db->checkpoint();  // force a snapshot write (atomically renames over elips.snapshot)
                   // then truncates the WAL
db->close();       // checkpoint + release lock + null WAL references
                   // subsequent operations on the instance are no-ops
```

The destructor of `ElipsInstance` performs a best-effort checkpoint for persistent databases. If `close()` is never called, the destructor catches any exception and silently continues (C++ Core Guidelines E.16).

For in-memory databases (`":memory:"`), `checkpoint()` is a no-op and `close()` is a no-op (no on-disk state exists).

## Error Handling

All ELIPS errors inherit from `elips::ElipsError`, which inherits from `std::runtime_error`:

| Exception | When |
|-----------|------|
| `DimensionMismatch` | Vector dimension != database dimension |
| `InvalidVector` | Vector contains NaN or Inf |
| `ConfigError` | Invalid configuration, dimension conflict on reopen, or malformed RecordID string |
| `NotFound` | EQL query references an unbound variable |
| `StorageError` | Persistence/IO failure: corrupt snapshot, WAL truncation, disk write error |
| `LockConflict` | Another writer holds the directory lock |

Always wrap operations in try/catch when handling user input or IO operations:

```cpp
try {
    auto db = elips::open("./data/db", elips::Config{}.dimension(768));
} catch (const elips::LockConflict& e) {
    std::cerr << "Database locked: " << e.what() << '\n';
} catch (const elips::ElipsError& e) {
    std::cerr << "ELIPS error: " << e.what() << '\n';
}
```