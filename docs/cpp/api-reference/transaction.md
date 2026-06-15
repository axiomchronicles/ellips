# API Reference: Transaction & TransactionVault

Namespace `elips`. Buffered atomic batch mutations. Declaration at `include/elips/elips.hpp:133` (TransactionVault) and `line 149` (Transaction).

Transactions buffer multiple `place()` and `erase()` operations, then apply them atomically via `commit()`. Operations are validated eagerly during enqueue so `commit()` cannot fail mid-batch.

---

## TransactionVault

`include/elips/elips.hpp:135`

A vault-scoped handle within a transaction. Obtained from `Transaction::vault()`. Tied to its parent transaction's lifetime.

```cpp
class TransactionVault {
public:
    RecordID place(const Vector& vector, Payload payload = {},
                   std::optional<RecordID> id = std::nullopt);
    void erase(const RecordID& id);

private:
    friend class Transaction;
    TransactionVault(Transaction& txn, std::string vault);
    Transaction* txn_;
    std::string vault_;
};
```

### place()

```cpp
RecordID place(const Vector& vector, Payload payload = {},
               std::optional<RecordID> id = std::nullopt);
```

Enqueues a vector insertion for batch commit.

| Parameter | Type | Description |
|-----------|------|-------------|
| `vector` | `const Vector&` | The embedding vector to insert |
| `payload` | `Payload` | Metadata key-value pairs |
| `id` | `std::optional<RecordID>` | Explicit RecordID; auto-generated if omitted |

**Returns**: `RecordID` - the assigned record identity. Generated eagerly (if not provided) so the caller has the ID immediately, even before `commit()`.

**Validation** (eager, at enqueue time):
1. Checks `vector.dimension() != db_->config().dimension()` - throws `DimensionMismatch`.
2. Checks vector values are finite - throws `InvalidVector` if any NaN/Inf.

This ensures that if enqueue succeeds for all operations in the transaction, `commit()` cannot fail due to validation errors (maintaining atomicity).

**Note**: Unlike `Vault::place()`, the vector is **not normalized** at enqueue time. Normalization happens inside `Vault::place()` when `commit()` is called.

### erase()

```cpp
void erase(const RecordID& id);
```

Enqueues a record deletion for batch commit.

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `const RecordID&` | The record identity to remove |

No immediate validation - the erase is applied during `commit()` via `Vault::erase()`, which will silently return `false` if the record does not exist.

### Constructor (Private)

```cpp
TransactionVault(Transaction& txn, std::string vault);
```

Constructed by `Transaction::vault()`. Associates the vault name with the parent transaction. Not directly constructible by users.

---

## Transaction

`include/elips/elips.hpp:149`

```cpp
class Transaction {
public:
    explicit Transaction(ElipsInstance& db);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = delete;
    Transaction& operator=(Transaction&&) = delete;

    TransactionVault vault(const std::string& name);
    void commit();
    void rollback() noexcept;

private:
    struct PendingOp {
        bool is_erase{false};
        std::string vault;
        Vector vector;
        Payload payload;
        std::optional<RecordID> id;
    };
    void enqueue_place(std::string vault, const Vector& vector,
                       Payload payload, std::optional<RecordID> id);
    void enqueue_erase(std::string vault, const RecordID& id);

    ElipsInstance* db_;
    std::vector<PendingOp> ops_;
    bool done_{false};
};
```

### Construction

```cpp
explicit Transaction(ElipsInstance& db);
```

Created via `ElipsInstance::begin_transaction()`:

```cpp
auto txn = db->begin_transaction();
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `db` | `ElipsInstance&` | The database instance this transaction operates on |

The transaction holds a raw pointer to the database instance. The database must outlive the transaction.

**Not copyable, not movable.** A `Transaction` is a stack-local value.

### destructor (Auto-Rollback)

```cpp
~Transaction();
```

If `commit()` was not called (`done_ == false`):
1. Calls `rollback()` to clear buffered operations.
2. No operations are applied to the database.

This ensures that abandoned transactions never leave partial state. Always explicitly call `commit()` or `rollback()` for clarity, even though the destructor handles cleanup.

### vault()

```cpp
TransactionVault vault(const std::string& name);
```

Obtains a `TransactionVault` handle to enqueue operations on the named vault.

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `const std::string&` | Vault name to operate on |

**Returns**: `TransactionVault` - a value type tied to this transaction. All operations enqueued through it are buffered in the transaction's `ops_` vector.

Multiple vaults can be used within the same transaction:

```cpp
auto txn = db->begin_transaction();
txn.vault("docs").place(vec1, payload1);
txn.vault("images").place(vec2, payload2);
txn.vault("docs").erase(some_id);
txn.commit();  // atomic across both vaults
```

### commit()

```cpp
void commit();
```

Applies all buffered operations to the database atomically.

**Behavior**:
1. Iterates `ops_` in order.
2. For each operation: resolves the vault via `db_->vault(op.vault)`, then calls `Vault::place()` or `Vault::erase()`.
3. Each `Vault::place()` call triggers validation, normalization, WAL logging, index insertion, and record store update. Each `Vault::erase()` call triggers WAL logging, index removal, and record store removal.
4. After all operations are applied, clears `ops_` and sets `done_ = true`.

**After commit**: The transaction is finalized. Calling `commit()` again is a no-op. Calling `vault()` after commit will enqueue operations on a finalized transaction that will be rolled back on destruction.

**Exceptions**: May throw from `Vault::place()` (dimension mismatch, invalid vector) or `Vault::erase()`. Since operations are validated eagerly at enqueue time, this should not happen in normal use. If a late failure occurs, the already-applied operations are NOT rolled back.

### rollback()

```cpp
void rollback() noexcept;
```

Discards all buffered operations without applying them.

**Behavior**:
1. Clears `ops_`.
2. Sets `done_ = true`.

No database mutations occur. Guaranteed not to throw (`noexcept`).

### Lifecycle

```
begin_transaction()
    │
    ├─ vault("X").place(...)  ──► enqueue_place
    ├─ vault("Y").place(...)  ──► enqueue_place
    ├─ vault("X").erase(id)  ──► enqueue_erase
    │
    ├─ commit()
    │     └─ for each op:
    │           db->vault(op.vault).place(...)  or  .erase(...)
    │     └─ done_ = true
    │
    └─ ~Transaction()
          └─ if (!done_) rollback()  // auto-cleanup
```

### Internal Methods (Private)

| Method | Description |
|--------|-------------|
| `enqueue_place(vault, vector, payload, id)` | Validates vector (dimension, finiteness) eagerly, then pushes to `ops_`. |
| `enqueue_erase(vault, id)` | Pushes an erase operation to `ops_`. |

Both are called by `TransactionVault` methods.

---

## Usage Patterns

### Basic Atomic Batch

```cpp
auto txn = db->begin_transaction();

txn.vault("documents").place(elips::Vector{{1.0F, 0.0F, 0.0F}},
                              {{"title", std::string{"gamma"}}});
txn.vault("documents").place(elips::Vector{{0.0F, 1.0F, 0.0F}},
                              {{"title", std::string{"delta"}}});

txn.commit();  // both inserted atomically
```

### Cross-Vault Atomicity

```cpp
auto txn = db->begin_transaction();

txn.vault("source").erase(source_id);
txn.vault("target").place(source_vector, source_payload);

txn.commit();  // erase + insert as one atomic unit
```

### Explicit Rollback

```cpp
auto txn = db->begin_transaction();

try {
    txn.vault("docs").place(vec1, payload1);
    // ... something fails ...
    txn.commit();
} catch (...) {
    txn.rollback();  // explicitly discard (destructor would also do this)
    throw;
}
```

### RAII Guard (Destructor Auto-Rollback)

```cpp
{
    auto txn = db->begin_transaction();
    txn.vault("docs").place(vec1, payload1);
    // If we exit scope without commit(), destructor auto-rolls back
}
// nothing was applied
```