# API Reference: ElipsInstance

Namespace `elips`. Top-level database handle — one per directory. Owns all vaults, the WAL, and the advisory lock. Full declaration at `include/elips/elips.hpp:79`.

## Construction (via factory)

`ElipsInstance` is not directly constructible by users. Use the factory function:

```cpp
std::unique_ptr<ElipsInstance> open(const std::string& path,
                                     const Config& config = {});
```

### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `path` | `const std::string&` | Filesystem path for persistent databases, or `":memory:"` for in-memory |
| `config` | `const Config&` | Configuration (dimension, metric, index type, durability, graph params). Default: `Config{}`. |

### Behavior

**`:memory:` path:**
- Requires `config.dimension() > 0` (throws `ConfigError` otherwise).
- Non-persistent, no WAL, no advisory lock, no checkpointing.
- Vaults and records exist only in process memory.

**Filesystem path (new database):**
- Creates the directory (via `std::filesystem::create_directories`).
- Acquires an exclusive `flock(LOCK_EX | LOCK_NB)` on `{path}/LOCK` (throws `LockConflict` if held).
- Writes `IDENTITY` file with dimension, metric, and index type.
- Requires `config.dimension() > 0` (throws `ConfigError` otherwise).

**Filesystem path (existing database):**
- Reads `IDENTITY` to recover dimension, metric, index type.
- If `config.dimension() > 0` and doesn't match `IDENTITY`, throws `ConfigError`.
- Otherwise merges: dimension, metric, index from `IDENTITY` override config defaults.
- Loads snapshot from `elips.snapshot`.
- Replays WAL entries from `wal.log` on top of snapshot.
- Creates WAL writer unless durability is `ephemeral`.

**GPU probe** (when `ELIPS_GPU_ENABLED` and config has GPU enabled):
- Probes all devices via `GpuDeviceManager::probe_all_devices()`.
- Sets the first device as available GPU info on the instance.

### Return Value

`std::unique_ptr<ElipsInstance>` — owned database handle. Destroying the `unique_ptr` triggers the destructor.

### Exceptions

| Exception | Condition |
|-----------|-----------|
| `ConfigError` | In-memory without dimension, new database without dimension, dimension conflict on reopen |
| `LockConflict` | Another writer holds the `LOCK` directory lock |
| `StorageError` | Can't write IDENTITY, can't open/parse snapshot, can't replay WAL |

---

## Destructor

```cpp
~ElipsInstance();
```

**Auto-checkpoint**: If the instance is persistent (`persistent_ == true`) and not yet closed (`!closed_`):
1. Calls `checkpoint()` to write a snapshot and truncate the WAL.
2. Catches any exception silently (C++ Core Guidelines E.16: destructors must not throw). The checkpoint is best-effort.

**Note**: For guaranteed durability, always call `close()` or `checkpoint()` explicitly before destruction. The destructor's best-effort checkpoint may silently swallow I/O errors.

### Copy and Move

```cpp
ElipsInstance(const ElipsInstance&) = delete;
ElipsInstance& operator=(const ElipsInstance&) = delete;
ElipsInstance(ElipsInstance&&) = delete;
ElipsInstance& operator=(ElipsInstance&&) = delete;
```

Not copyable, not movable. Access must go through the `unique_ptr` returned by `open()`.

### Thread Safety

`ElipsInstance` is **not thread-safe** for concurrent mutation. Vaults share the same WAL and index structures without internal synchronization. The advisory lock enforces single-writer across processes, but within a single process, the caller must serialize access.

---

## Methods

### vault()

```cpp
Vault& vault(const std::string& name);
```

Gets or creates a vault by name.

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `const std::string&` | Vault name (partition key) |

**Returns**: `Vault&` — reference to the named vault. The reference is valid as long as the `ElipsInstance` is alive.

**Behavior**: If a vault with `name` already exists, returns it. Otherwise, creates a new `Vault` (shares the instance's config), attaches the WAL, inserts it into `vaults_`, and returns the new reference.

**Ownership**: The vault is owned by the instance (`vaults_` map holds `unique_ptr<Vault>`). The returned reference must not outlive the instance.

---

### list_vaults()

```cpp
std::vector<std::string> list_vaults() const;
```

Lists all vault names in the database.

**Returns**: `std::vector<std::string>` — vault names in no guaranteed order (stored in `std::map`, so iteration order is lexicographic by name).

**Thread safety**: Safe for concurrent reads if no vault creation is happening.

---

### begin_transaction()

```cpp
Transaction begin_transaction();
```

Starts a new transaction.

**Returns**: `Transaction` — a handle for buffering and atomically committing mutations.

**Lifecycle**: The returned `Transaction` is a value type. Operations are enqueued via `txn.vault("name").place(...)` and `txn.vault("name").erase(...)`. Call `txn.commit()` to apply all buffered mutations or let it go out of scope for auto-rollback.

See [Transaction API Reference](transaction.md) for details.

---

### query()

```cpp
std::vector<SearchResult> query(
    const std::string& eql,
    const std::map<std::string, Vector>& bindings = {});
```

Executes an EQL query string against the database.

| Parameter | Type | Description |
|-----------|------|-------------|
| `eql` | `const std::string&` | EQL query text |
| `bindings` | `const std::map<std::string, Vector>&` | Named query vector bindings (for `$name` references). Default: empty map. |

**Returns**: `std::vector<SearchResult>` — query results. Read queries (seek, fetch, scan) return matched rows; `place` returns a single row with the new RecordID; `erase` returns empty.

**Internals**: Calls `eql::parse(eql)` then `eql::execute(ast, *this, bindings)`.

**Exceptions**:
- `eql::ParseError` — malformed EQL syntax.
- `DimensionMismatch` — bound vector dimension mismatch.
- `NotFound` — unbound query vector variable (`$name` not in bindings).
- `ConfigError` — malformed RecordID in fetch/erase.

**EQL Examples**:

```cpp
// vector search with filter
db->query("seek in docs nearest [0.1, 0.2, 0.3] top 5 where year >= 2023 yield");

// fetch by ID
db->query(R"(fetch from docs id "01931d0a-1234-7abc-8000-abcdef123456" yield)");

// scan with filter
db->query("scan in docs where status = \"active\" offset 0 limit 50 yield");

// insert with payload
db->query(R"(place in docs vector [0.1, 0.2, 0.3] data {"title": "doc"} )");

// erase by ID
db->query(R"(erase from docs id "01931d0a-1234-7abc-8000-abcdef123456" )");
```

---

### checkpoint()

```cpp
void checkpoint();
```

Writes a durable snapshot of all vaults to `{path}/elips.snapshot` and truncates the WAL.

**Behavior**:
1. No-op for in-memory databases (`persistent_ == false`).
2. Creates the database directory if absent.
3. Writes snapshot to `elips.snapshot.tmp` with header (magic, version, dimension, metric), then per-vault records (name, count, per-record: ID bytes, dimension, float vector, payload).
4. Atomically renames `elips.snapshot.tmp` → `elips.snapshot` via `std::filesystem::rename`.
5. Calls `wal_->reset()` to truncate the WAL (all records now captured in snapshot).

**Exceptions**: `StorageError` — can't open snapshot for writing, error during write.

**Thread safety**: Must be called with exclusive access (no concurrent mutations).

---

### close()

```cpp
void close();
```

Gracefully shuts down the database instance.

**Behavior**:
1. No-op if already closed (`closed_ == true`).
2. Calls `checkpoint()` to write final snapshot and truncate WAL.
3. Detaches WAL from all vaults (sets each vault's WAL pointer to `nullptr`).
4. Destroys `wal_` (`unique_ptr` reset).
5. Destroys `lock_` (`optional` reset), releasing the `flock(LOCK_UN)` advisory lock so the directory can be reopened.
6. Sets `closed_ = true`.

After `close()`, the instance is inert. Vault references obtained before `close()` are invalid (WAL detached, instance-owned index still valid but won't log writes).

**Exceptions**: May throw from `checkpoint()`.

---

### abandon()

```cpp
void abandon() noexcept;
```

Marks the instance as closed without writing a snapshot or releasing the lock.

**Behavior**: Simply sets `closed_ = true`. No checkpoint, no WAL truncation, no lock release. The destructor will see `closed_ == true` and skip its auto-checkpoint.

**Use case**: Testing or scenarios where the caller intentionally discards state.

**Thread safety**: `noexcept` — guaranteed not to throw.

---

### config()

```cpp
const Config& config() const noexcept;
```

Returns the effective configuration used by this instance.

**Returns**: `const Config&` — includes dimension, metric, index type, durability, graph params, and (if GPU enabled) GPU config.

---

### adopt_vault()

```cpp
Vault& adopt_vault(std::unique_ptr<Vault> vault);
```

Takes ownership of a pre-constructed vault and inserts it into the instance's vault map.

| Parameter | Type | Description |
|-----------|------|-------------|
| `vault` | `std::unique_ptr<Vault>` | Rvalue; ownership transferred to instance |

**Returns**: `Vault&` — reference to the adopted vault.

**Use case**: Internal use during snapshot loading (`load_snapshot()`). Publicly available but rarely needed by users.

---

### attach_wal()

```cpp
void attach_wal(std::unique_ptr<WAL> wal);
```

Replaces the instance's WAL and re-associates it with all existing vaults.

| Parameter | Type | Description |
|-----------|------|-------------|
| `wal` | `std::unique_ptr<WAL>` | Rvalue; ownership transferred to instance |

**Behavior**: Sets `wal_`, then iterates all vaults calling `vault->set_wal(wal_.get())`.

**Use case**: Internal use during `open()` after snapshot load and WAL replay. Also used in tests for durability testing.

---

### gpu_info() (GPU only)

```cpp
gpu::GpuDeviceInfo gpu_info() const;  // only when ELIPS_GPU_ENABLED defined
```

Returns information about the detected GPU device.

**Returns**: `gpu::GpuDeviceInfo` — struct containing name, vendor, backend, memory info, compute capabilities, bandwidth, and feature flags.

**Availability**: Only compiled when `ELIPS_GPU_ENABLED` is defined (any GPU backend enabled).

---

### gpu_stats() (GPU only)

```cpp
gpu::GpuMetricsSnapshot gpu_stats() const;  // only when ELIPS_GPU_ENABLED defined
```

Returns a snapshot of GPU runtime metrics.

**Returns**: `gpu::GpuMetricsSnapshot` — struct containing backend, memory usage, build counts, search latency percentiles (p50/p99), batch stats, kernel error counts.

**Availability**: Only compiled when `ELIPS_GPU_ENABLED` is defined.

---

### Private Methods (internal, documented for completeness)

| Method | Description |
|--------|-------------|
| `constructor(path, config, persistent, lock)` | Package-private. Called by `open()`. |
| `set_gpu_available(bool)` | Sets GPU availability flag. Called during `open()` GPU probe. |
| `set_gpu_info(GpuDeviceInfo)` | Sets GPU device info. Called during `open()` GPU probe. |

---

## Lifecycle Summary

```
open(path, config)
    │
    ├─ :memory: ──► ElipsInstance (persistent=false, no lock, no WAL)
    │
    └─ filesystem path:
         ├─ create_directories(path)
         ├─ LockManager(path/LOCK)  ← acquires flock(LOCK_EX|LOCK_NB)
         ├─ read/write IDENTITY
         ├─ load elips.snapshot  → vaults populated
         ├─ replay wal.log  → apply mutations on top
         ├─ WAL(path/wal.log)  ← append writer
         └─ GPU probe (if configured)
              │
              ▼
         ElipsInstance (persistent=true)
              │
              ├─ vault(name)  → create/return Vault
              ├─ begin_transaction() → Transaction
              ├─ query(eql) → EQL parse + execute
              │
              ├─ checkpoint()
              │    ├─ write elips.snapshot.tmp
              │    ├─ rename → elips.snapshot
              │    └─ wal->reset()
              │
              ├─ close()
              │    ├─ checkpoint()
              │    ├─ vaults detach WAL
              │    ├─ wal_.reset()
              │    └─ lock_.reset()  ← releases flock(LOCK_UN)
              │
              └─ ~ElipsInstance()
                   └─ if (persistent && !closed) checkpoint() catch (...)
```