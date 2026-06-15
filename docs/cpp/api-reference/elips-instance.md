# API Reference: ElipsInstance

`elips::ElipsInstance` is the top-level database handle returned by
`elips::open()`.

## Factory

```cpp
std::unique_ptr<ElipsInstance> open(const std::string& path,
                                    const Config& config = {});
```

Behavior:

- `":memory:"` opens are in-memory only and require `config.dimension() > 0`
- new persistent databases require a non-zero dimension
- existing persistent databases reuse the persisted identity
- `AccessMode::read_only` requires an existing database and acquires a shared
  advisory lock
- read-write opens acquire an exclusive advisory lock

On open, ELIPS loads segmented state if `elips.manifest` is present, otherwise
falls back to `elips.snapshot`, then replays the WAL.

## Core Methods

### `vault(name)`

Returns a reference to a named vault, creating it lazily.

### `list_vaults()`

Returns all current vault names.

### `begin_transaction()`

Starts an atomic write transaction.

### `query(eql, bindings={})`

Runs a single EQL statement and returns `std::vector<SearchResult>`.

### `checkpoint()`

Writes current state to disk and truncates the WAL.

- segmented mode: rewrites manifest + per-vault segment files
- snapshot mode: rewrites `elips.snapshot`

### `compact()`

Rebuilds every vault index from stored records and then checkpoints.

### `close()`

Graceful shutdown: checkpoint, detach WAL, release the advisory lock.

### `abandon()`

Testing hook that suppresses destructor checkpointing so recovery must come from
the WAL.

### `config()`

Returns the effective `Config`.

### `gpu_info()` / `gpu_stats()`

Available when GPU support is compiled in and a GPU backend is active.

## Lifecycle Notes

- Persistent instances checkpoint on destruction unless already closed or opened
  read-only.
- Read-only instances never attach a WAL writer.
- Vaults created under a read-only instance are immediately marked read-only.

## Common Failure Modes

- `ConfigError`: missing dimension on new open, dimension mismatch, read-only
  open against a missing database
- `LockConflict`: another process already owns the write lock
- `StorageError`: on-disk IO failure
