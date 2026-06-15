# Index Engine

The index engine is the similarity-search backbone of ELIPS. It defines an
abstract interface (`IndexPort`) that decouples query logic from concrete index
implementations, then provides two backends: an exact brute-force scanner used as
a ground-truth oracle, and a production HNSW approximate-nearest-neighbour index.

---

## IndexPort — Abstract Interface

**Header:** `include/elips/index_engine/IndexPort.hpp`

`IndexPort` is a pure-virtual base class following the Dependency Inversion
Principle (DIP). Callers (Vault, QueryExecutor, benchmarks) depend solely on
`IndexPort`, never on a concrete index type. The factory `make_index()` selects
the concrete implementation at construction time.

```cpp
class IndexPort {
public:
    using Hit = std::pair<RecordID, float>;  // (id, ordering-normalized distance)
    virtual void insert(const RecordID& id, std::span<const float> vector) = 0;
    virtual void remove(const RecordID& id) = 0;
    [[nodiscard]] virtual std::vector<Hit> search(std::span<const float> query,
                                                  std::size_t k) const = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
    [[nodiscard]] virtual std::string_view type_name() const noexcept = 0;
};
```

### The `Hit` Type

`Hit` is a `std::pair<RecordID, float>`. The `float` is an **ordering-normalized
distance**: smaller always means more similar, regardless of metric. This
contract is enforced by `distance()` in the vector engine (see
[metrics-engine.md](metrics-engine.md)). Callers sort ascending by `second` and
keep the smallest `k`.

### Non-copyable, Non-movable

Both the copy and move constructors/operators are explicitly deleted:

```cpp
IndexPort(const IndexPort&) = delete;
IndexPort& operator=(const IndexPort&) = delete;
IndexPort(IndexPort&&) = delete;
IndexPort& operator=(IndexPort&&) = delete;
```

This design choice ensures that index instances are exclusively owned by
`std::unique_ptr<IndexPort>` and are never sliced, shared, or accidentally
moved. Indices contain large internal data structures (vectors, graphs)
whose shallow moves would leave dangling references in adjacent state
(e.g. `id_to_node_` map entries pointing into `data_`). Deleting move forces
all transfers to go through the factory and remain behind a pointer.

**Insert contract:** Vectors are pre-validated (finite, correct dimension) and
pre-normalized (for cosine) by the `Vault::prepare()` method before they reach
any index method.

**Remove contract:** Calling `remove()` on an ID that was never inserted is a
no-op for both implementations.

**Search contract:** Returns up to `k` hits, sorted ascending by distance.
May return fewer if the index has fewer records.

---

## ExactIndex — Brute-force Ground Truth

**Header:** `include/elips/index_engine/ExactIndex.hpp`
**Source:** `src/ExactIndex.cpp`

`ExactIndex` performs a brute-force linear scan over every stored vector. It
guarantees exact (lossless) top-K results and serves two roles:

1. The default index for small collections where throughput of O(N × dim) is
   acceptable.
2. The **recall ground-truth oracle**: benchmarks and recall tests compare
   HNSW results against ExactIndex results to measure recall@K.

### Data Structures

```
| id_0 | id_1 | ... | id_{N-1} |
| v_0[0..dim-1] | v_1[0..dim-1] | ... | v_{N-1}[0..dim-1] |
```

- `ids_` — `std::vector<RecordID>`, one entry per record, in insertion order.
- `data_` — `std::vector<float>`, **row-major** flat storage. Row `i` occupies
  indices `[i × dim, (i+1) × dim)`.

### Insert — O(1) amortised

```cpp
void ExactIndex::insert(const RecordID& id, std::span<const float> vector) {
    ids_.push_back(id);
    data_.insert(data_.end(), vector.begin(), vector.end());
}
```

Appends the ID and copies the vector data. No index structure is rebuilt;
inserts are O(1) amortised (vector growth). There are no copies of the full
dataset.

### Search — O(N × dim) + O(K log K)

```cpp
std::vector<IndexPort::Hit> ExactIndex::search(std::span<const float> query,
                                               std::size_t k) const {
    std::vector<Hit> scored;
    scored.reserve(ids_.size());
    for (std::size_t i = 0; i < ids_.size(); ++i) {
        const std::span<const float> row{data_.data() + i * dimension_, dimension_};
        scored.emplace_back(ids_[i], distance(metric_, query, row));
    }
    const std::size_t take = std::min(k, scored.size());
    std::partial_sort(
        scored.begin(), scored.begin() + static_cast<std::ptrdiff_t>(take),
        scored.end(),
        [](const Hit& lhs, const Hit& rhs) { return lhs.second < rhs.second; });
    scored.resize(take);
    return scored;
}
```

**Step 1 — Distance computation (O(N × dim)):** Iterates over every row,
computes `distance()` vs the query, and emits a scored `Hit`. The distance
function dispatches to NEON SIMD kernels at runtime on ARM (see
[metrics-engine.md](metrics-engine.md)).

**Step 2 — Top-K extraction (O(N × log K)):** Uses `std::partial_sort` to
partially order the scored vector so that the smallest K elements are in the
correct positions. This is O(N × log K) in practice, more efficient than a full
sort when K ≪ N.

### Remove — O(N)

```cpp
void ExactIndex::remove(const RecordID& id) {
    const auto it = std::find(ids_.begin(), ids_.end(), id);
    if (it == ids_.end()) return;
    const auto row = static_cast<std::size_t>(it - ids_.begin());
    ids_.erase(it);
    const auto first = data_.begin() + static_cast<std::ptrdiff_t>(row * dimension_);
    data_.erase(first, first + dimension_);
}
```

Removes the ID and its vector row by erasing from both vectors. This is an O(N)
operation (the erase shifts all subsequent elements). Because ExactIndex is
intended for small collections or offline benchmarking, this cost is acceptable.
Note that the removed row leaves the index in a consistent state with no gaps in
the row-major layout.

### Use as Ground-truth Oracle

In recall benchmarks (see `tests/unit/hnsw_recall_test.cpp`,
`benchmarks/BenchMain.cpp`), ExactIndex is populated with the same data as the
HNSW index. For each query, both indices return `k` results. Recall is computed
as:

```
recall@K = |HNSW_topK ∩ Exact_topK| / |Exact_topK|
```

The test asserts `recall@10 ≥ 0.90` on 2000 random vectors in 64 dimensions.

---

## HierarchicalGraphIndex (HNSW)

**Header:** `include/elips/index_engine/HierarchicalGraphIndex.hpp`
**Source:** `src/HierarchicalGraphIndex.cpp`

`HierarchicalGraphIndex` is a from-scratch implementation of the Hierarchical
Navigable Small World (HNSW) algorithm. It provides approximate nearest-neighbour
search with configurable trade-offs between recall, latency, and memory.

### Configuration (`GraphParams`)

| Parameter | Default | Meaning |
|-----------|---------|---------|
| `max_connections` (M) | 16 | Max edges per node per layer above layer 0 |
| `ef_construction` | 200 | Beam width during insert (greedy search for neighbours) |
| `ef_search` | 50 | Beam width during query search (layer 0 only) |

At layer 0, each node may have up to `2 × M` (= 32 by default) links.

The internal parameter `level_mult_` (mL) is derived as `1.0 / ln(M)`, which
drives the geometric level assignment.

### Data Structures

```
data_         [float]   row-major, dimension_ floats per node
ids_          [RecordID]
deleted_      [bool]    soft-delete tombstones
node_levels_  [int]     max layer index for each node (0-based)
links_        [node][level][neighbor_node_id]  3D ragged vector
id_to_node_   unordered_map<RecordID, NodeId>
entry_point_  int       global entry; -1 when empty
max_level_    int       highest level across all nodes; -1 when empty
deleted_count_ size_t   running count of soft-deleted nodes
rng_          mt19937   mutable for random_level()
```

- **`NodeId`** is `std::uint32_t` — each inserted vector is assigned a
  sequentially-increasing node ID (the index into the flat arrays).
- **`data_`** is the same row-major layout as ExactIndex.
- **`links_`** is a 3D vector: `links_[node]` is a vector of layers (length
  `node_levels_[node] + 1`), and each layer is a vector of `NodeId`
  representing out-edges at that level. The graph is bidirectionally
  maintained during construction (every added edge also adds the reverse
  edge at the neighbour).
- **`id_to_node_`** maps external `RecordID` ↔ internal `NodeId`.
- Soft deletes (`deleted_[node] = true`) preserve graph connectivity
  (navigation is undisturbed) while excluding tombstones from results.

### Level Assignment — `random_level()`

```cpp
int HierarchicalGraphIndex::random_level() {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    double u = dist(rng_);
    if (u <= 0.0) u = std::numeric_limits<double>::min();
    return static_cast<int>(-std::log(u) * level_mult_);
}
```

Uses the standard HNSW level-generation formula. `u` is drawn uniformly from
(0, 1], and the level is `floor(-ln(u) × mL)` where `mL = 1/ln(M)`. This yields
an exponentially-decaying level distribution: roughly 1/M fraction of nodes
appear at each successive level. The `u ≤ 0.0` guard avoids `-inf` from
`log(0)`.

### Beam Search — `search_layer()`

```cpp
std::vector<Scored> search_layer(std::span<const float> query, NodeId entry,
                                 std::size_t ef, int level) const;
```

Beam search within a single layer of the graph:

1. **Initialise** an empty visited set, a min-heap (candidates, by distance)
   and a max-heap (results, by distance). Both start with the entry point.
2. **While** the candidate heap is non-empty:
   - Pop the closest candidate. If its distance exceeds the worst result and
     we already have `ef` results, stop (pruning).
   - For each neighbour of the candidate at the given `level`:
     - Skip if already visited.
     - Compute `distance_to(query, neighbour)`.
     - If the distance is better than the worst result or we have fewer than
       `ef` results, add to both heaps, evicting the worst from results if
       it exceeds `ef`.
3. **Return** results sorted ascending by distance (uses `std::sort`).

`search_layer` is used in three contexts:
- **Greedy descent** (insert/search): called with `ef = 1` on upper layers to
  find the best entry point for the next layer.
- **Insert neighbour search:** called with `ef = ef_construction` to find
  candidates for the `connect()` diversity heuristic.
- **Query search:** called with `ef = max(ef_search, k)` on layer 0.

### Connect with Diversity Heuristic — `connect()`

```cpp
void connect(NodeId node, const std::vector<Scored>& candidates,
             int level, std::size_t max_links);
```

Implements HNSW Algorithm 4 (the diversity heuristic), which is critical for
graph quality and recall:

1. **Filter:** Keep a candidate only if it is closer to the new node than to
   any already-selected neighbour. This prevents adding redundant edges to
   neighbours that already have a better path through an existing selection.
2. **Add forward edges:** Add each selected candidate as a neighbour of `node`
   at the given `level`.
3. **Add reverse edges with pruning:** Add `node` as a neighbour of each
   selected candidate. If the candidate's neighbour list at this level exceeds
   `max_links`, sort by distance to the candidate and keep only the closest
   `max_links`.

The diversity heuristic yields better-connected graphs with higher recall than
simply taking the M closest candidates from the beam search.

### Insert Flow

```cpp
void HierarchicalGraphIndex::insert(const RecordID& id, std::span<const float> vector)
```

**Step 1 — Allocate:** Assign the next sequential `NodeId`, generate a random
level, push the ID/vector/deleted flag/level to the flat arrays, size
`links_` to `level + 1` empty neighbour lists, and record in `id_to_node_`.

**Step 2 — Bootstrap:** If this is the first node (`entry_point_ < 0`), set
it as the entry point with `max_level_ = level` and return.

**Step 3 — Greedy descent through upper layers:** Starting from the current
global entry point, descend layer by layer from `max_level_` down to
`level + 1`. At each layer, call `search_layer(ef=1)` to find the single
closest node, which becomes the entry for the next lower layer.

**Step 4 — Insert into intersecting layers:** For each layer from
`min(level, max_level_)` down to 0:
- Call `search_layer(ef = ef_construction)` to find candidate neighbours.
- Call `connect()` with `max_links = 2 × M` at layer 0, or `M` at higher
  layers.

**Step 5 — Update entry point:** If `level > max_level_`, the new node
becomes the global entry point and `max_level_` is updated.

### Soft Delete — `remove()`

```cpp
void HierarchicalGraphIndex::remove(const RecordID& id) {
    const auto it = id_to_node_.find(id);
    if (it == id_to_node_.end() || deleted_[it->second]) return;
    deleted_[it->second] = true;
    ++deleted_count_;
}
```

Nodes are **never physically removed** from the graph. This design choice:
- Preserves graph connectivity (deleted nodes continue to serve as routing
  hops during search).
- Is consistent with ELIPS's MVCC-style delete model (the index mirrors the
  vault's logical deletion).
- Means `size()` returns `ids_.size() - deleted_count_`.
- Means `id_to_node_` accumulates entries forever (maps are small relative
  to vector data).

### Query Search

```cpp
std::vector<IndexPort::Hit> HierarchicalGraphIndex::search(
    std::span<const float> query, std::size_t k) const
```

1. **Greedy descent:** From the entry point, descend layer by layer from
   `max_level_` down to layer 1 using `search_layer(ef=1)`.
2. **Layer-0 beam search:** Call `search_layer(ef = max(ef_search, k), level=0)`
   to get the full candidate set.
3. **Post-filter:** Iterate over results, skip soft-deleted nodes, collect
   up to `k` hits. Deleted nodes are transparently excluded.

### Graph Construction Complexity

The asymptotic construction complexity is **O(N × log(N) × M)**. Each insert
performs beam searches across O(log N) layers (the expected level of a node)
with beam width `ef_construction`, and each search visits O(M) neighbours per
hop. In practice, with `ef_construction = 200` and `M = 16`, insertion scales
sub-linearly in the dataset size.

---

## IndexFactory — Dependency Injection

**Header:** `include/elips/index_engine/IndexFactory.hpp`
**Source:** `src/IndexFactory.cpp`

```cpp
std::unique_ptr<IndexPort> make_index(const Config& config,
                                      std::uint16_t dimension)
```

A free function factory that selects and constructs the appropriate concrete
index based on `Config::index()`:

| `config.index()` | Constructed type |
|-------------------|------------------|
| `IndexType::exact` | `ExactIndex(metric, dimension)` |
| `IndexType::graph` | `HierarchicalGraphIndex(metric, dimension, config.graph_params())` |
| (fallback) | `ExactIndex(metric, dimension)` |

The returned `std::unique_ptr<IndexPort>` is passed to the `Vault` constructor,
which stores it as `std::unique_ptr<IndexPort> index_`. All callers interact
with the index through the abstract interface — no code outside the factory
or benchmarks needs to know which concrete type is in use. This enables:

- **A/B testing** of index algorithms with zero caller changes.
- **Ground-truth verification** in tests (instantiate both ExactIndex and HNSW,
  compare results).
- **Future index types** (e.g. IVF-PQ, CAGRA via GPU engine) added behind the
  same factory with no API breakage.