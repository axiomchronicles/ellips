# API Reference: Vault

Namespace `elips`. A named partition of records within a database. Owns its index and the authoritative in-memory record store. Declaration at `include/elips/elips.hpp:40`.

## Construction

Vaults are **not directly constructible** by users. Obtain a reference via:

```cpp
Vault& vault = db->vault("name");  // ElipsInstance::vault()
```

Internal constructor signature:

```cpp
Vault(std::string name, const Config& config);
```

- Takes a vault name (stored as `name_`).
- Copies the database config (`config_`).
- Creates the index: `index_ = make_index(config, config.dimension())`.

Vaults share the database's dimension, metric, and index configuration.

## prepare() (Private)

```cpp
Vector Vault::prepare(const Vector& vector) const;
```

Internal validation and normalization pipeline. Called by `place()` and `seek()` before the vector reaches the index.

**Steps:**
1. **Dimension check**: If `vector.dimension() != config_.dimension()`, throws `DimensionMismatch`.
2. **Finiteness check**: If any vector value is NaN or Inf (`!std::isfinite(v)`), throws `InvalidVector`.
3. **Normalization**: If `requires_normalization(config_.metric())` is true (i.e., for `Metric::cosine`), returns `vector.normalized()`. Otherwise returns the vector unchanged.

All vectors stored in the index and record store are the output of `prepare()` — validated and (for cosine) normalized.

---

## Methods

### place()

```cpp
RecordID place(const Vector& vector, Payload payload = {},
               std::optional<RecordID> id = std::nullopt);
```

Inserts (or replaces) a single vector with its metadata payload.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `vector` | `const Vector&` | required | The embedding vector to index |
| `payload` | `Payload` | `{}` | Metadata key-value pairs (optional) |
| `id` | `std::optional<RecordID>` | `std::nullopt` | Explicit RecordID; auto-generated if omitted |

**Returns**: `RecordID` — the assigned record identity (either the provided ID or a newly generated UUIDv7).

**Behavior**:
1. Calls `prepare(vector)` → validates and (if cosine) normalizes.
2. Determines `record_id`: uses the provided `id` if present, otherwise calls `RecordID::generate()`.
3. If WAL is attached (`wal_ != nullptr`): appends a WAL insert entry with vault name, ID, vector values, and payload. This ensures durability before in-memory mutation.
4. Calls `index_->insert(record_id, prepared.values())` to add the vector to the search index.
5. Inserts into `records_` map: `records_[record_id] = Record{record_id, prepared, payload}`.
6. Returns `record_id`.

**Note**: If a record with the same `id` already exists, `std::map::operator[]` replaces it. The index's `insert()` may not remove the old entry — use `erase()` first then `place()` for explicit update semantics.

**Exceptions**:
- `DimensionMismatch` — vector dimension doesn't match vault config.
- `InvalidVector` — vector contains NaN or Inf.
- `StorageError` — WAL append failed (if durability mode requires sync).

---

### place_many()

```cpp
void place_many(const std::vector<Record>& records);
```

Batch insertion of records. Each record in the vector is processed individually via `place()`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `records` | `const std::vector<Record>&` | Vector of records to insert |

**Behavior**: Iterates over `records`. For each record:
- If `record.id` is default-constructed (zero bytes), a new ID is generated.
- Otherwise, the record's existing ID is used.
- Calls `place(record.vector, record.payload, optional_id)`.

Each record is individually prepared, WAL-logged, indexed, and stored. No transactional atomicity — individual inserts within the batch may succeed even if a later one fails. Use `Transaction::commit()` for atomic batch semantics.

---

### seek()

```cpp
std::vector<SearchResult> seek(
    const Vector& query, std::size_t top, const Filter& filter = Filter{},
    std::optional<float> threshold = std::nullopt) const;
```

Finds the `top` nearest neighbors to a query vector, with optional metadata filtering and distance threshold.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `query` | `const Vector&` | required | The query vector |
| `top` | `std::size_t` | required | Maximum number of results to return |
| `filter` | `const Filter&` | `Filter{}` | Metadata predicate. Default-constructed Filter matches all records |
| `threshold` | `std::optional<float>` | `std::nullopt` | Distance threshold. Only results with `distance <= threshold` are included |

**Returns**: `std::vector<SearchResult>` — results sorted ascending by distance. Each `SearchResult` contains `id`, `distance`, and `data` (payload).

**Behavior**:
1. Calls `prepare(query)` → validates and (if cosine) normalizes.
2. Determines index fetch size:
   - If `threshold` is set: fetches **all** records from the index (`records_.size()`), since a range query needs all candidates.
   - If `filtered` (filter is not match-all): over-fetches as `min(records_.size(), top * 20)` to account for post-filter rejections. Minimum 20, minimum 1.
   - Otherwise: fetches exactly `max(top, 1)`.
3. Calls `index_->search(prepared.values(), fetch)`.
4. Post-filters each hit:
   - Skips if `threshold` is set and `distance > threshold`.
   - Skips if `filter.matches(payload)` returns false.
   - Collects results until `results.size() >= top`.
5. Returns accumulated results.

**Examples**:

```cpp
// basic search
auto results = docs.seek(elips::Vector{{0.9F, 0.1F}}, 10);

// filtered search
auto filter = elips::Filter().field("category").equals(std::string{"sports"});
auto results = docs.seek(query, 10, filter);

// threshold search (range query)
auto results = docs.seek(query, 100, elips::Filter{}, 0.5f);

// filtered + threshold
auto results = docs.seek(query, 50, filter, 0.3f);
```

---

### scan()

```cpp
std::vector<Record> scan(
    const Filter& filter = Filter{}, std::size_t offset = 0,
    std::size_t limit = std::numeric_limits<std::size_t>::max()) const;
```

Iterates through all records applying a metadata filter, respecting pagination.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `filter` | `const Filter&` | `Filter{}` | Metadata predicate. Default matches all |
| `offset` | `std::size_t` | `0` | Number of matching records to skip |
| `limit` | `std::size_t` | `SIZE_MAX` | Maximum number of records to return |

**Returns**: `std::vector<Record>` — matching records in map iteration order (sorted by `RecordID`, which is UUIDv7 time order).

**Behavior**: Sequential scan of `records_` (ordered by `RecordID` key). For each record:
1. If `filter.matches(record.payload)` is false, skip.
2. If `skipped < offset`, increment skipped counter and skip.
3. If `out.size() >= limit`, stop.
4. Otherwise, add record to output.

Unlike `seek()`, no vector similarity is involved. This is a pure metadata scan.

**Performance**: O(N) linear scan of all records. Suitable for administrative queries, not for high-throughput time-critical paths.

---

### fetch()

```cpp
std::optional<Record> fetch(const RecordID& id) const;
```

Retrieves a single record by its `RecordID`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `const RecordID&` | The record identity to look up |

**Returns**: `std::optional<Record>` — the record if found, `std::nullopt` otherwise.

**Behavior**: O(log N) lookup in `records_` (`std::map`).

**Example**:

```cpp
auto record = docs.fetch(some_id);
if (record) {
    std::cout << record->id.to_string() << '\n';
}
```

---

### erase()

```cpp
bool erase(const RecordID& id);
```

Removes a record by its `RecordID`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `const RecordID&` | The record identity to remove |

**Returns**: `bool` — `true` if the record existed and was removed, `false` if not found.

**Behavior**:
1. Looks up `id` in `records_`. Returns `false` if not found.
2. If WAL is attached: appends a WAL erase entry with vault name and ID (for durability).
3. Calls `index_->remove(id)` to remove from the search index.
4. Erases from `records_` map.
5. Returns `true`.

**Index behavior**: For `ExactIndex`, the vector row is physically removed. For `HierarchicalGraphIndex`, removal is a **soft tombstone**: the graph structure is preserved (the node continues to serve as a navigation bridge), but the node is flagged as deleted and excluded from search results.

---

### info()

```cpp
VaultInfo info() const noexcept;
```

Returns summary statistics for the vault.

**Returns**: `VaultInfo` — struct with fields:
- `count` (`std::size_t`): Number of records in the vault.
- `dimension` (`std::uint16_t`): Configured vector dimension.
- `metric` (`Metric`): Configured similarity metric.

**Thread safety**: `noexcept` — safe for concurrent reads.

---

### name()

```cpp
const std::string& name() const noexcept;
```

Returns the vault's name.

---

### records()

```cpp
const std::map<RecordID, Record>& records() const noexcept;
```

Returns a const reference to the internal record store. Primarily used by `checkpoint()` for serialization and by tests for inspection. Not recommended for application code — use `scan()`, `fetch()`, or `seek()` instead.

---

### set_wal() (Internal)

```cpp
void set_wal(WAL* wal) noexcept;
```

Sets the WAL pointer for this vault. Called by `ElipsInstance::attach_wal()`. All mutations (`place()`, `erase()`) check `wal_ != nullptr` before writing to the log.

---

## Normalization and Validation Summary

Every vector that enters the vault goes through `prepare()`:

```
                  ┌──────────┐
                  │  Vector  │
                  └────┬─────┘
                       ▼
              dimension == config?
                   │       └── throws DimensionMismatch
                   ▼ yes
              all finite?
                   │       └── throws InvalidVector
                   ▼ yes
           cosine metric?
              │        │
             yes       no
              │        │
              ▼        ▼
         normalized()  pass-through
              │        │
              └───┬────┘
                  ▼
         pre-validated, pre-normalized Vector
                  │
          ┌───────┼───────┐
          ▼       ▼       ▼
       index    WAL    records_
```

This applies uniformly to `place()` and `seek()` (query vector). For `place_many()`, each record is individually prepared.