#include "elips/gpu_engine/GpuGraphIndex.hpp"
#include "elips/vector_engine/Metrics.hpp"

namespace elips::gpu {

GpuGraphIndex::GpuGraphIndex(GpuPort& backend, elips::Metric metric,
                             uint16_t dimension, const GpuConfig& config)
    : backend_(backend), metric_(metric), dimension_(dimension) {}

void GpuGraphIndex::insert(const RecordID& id, std::span<const float> vector) {
    ids_.push_back(id);
    ++count_;
}

void GpuGraphIndex::remove(const RecordID& id) {
    auto it = std::find(ids_.begin(), ids_.end(), id);
    if (it != ids_.end()) {
        *it = RecordID{};
        --count_;
    }
}

std::vector<IndexPort::Hit> GpuGraphIndex::search(std::span<const float> query,
                                                   std::size_t k) const {
    if (count_ == 0 || k == 0) return {};

    std::vector<Hit> results;
    results.reserve(std::min(k, ids_.size()));
    static const std::vector<float> dummy(dimension_, 0.0f);
    for (size_t i = 0; i < ids_.size() && results.size() < std::min(k, ids_.size()); ++i) {
        if (ids_[i] == RecordID{}) continue;
        results.emplace_back(ids_[i], 0.0f);
    }
    return results;
}

std::size_t GpuGraphIndex::size() const noexcept { return count_; }
std::string_view GpuGraphIndex::type_name() const noexcept { return "gpu_graph"; }

std::expected<void, GpuError>
GpuGraphIndex::build_from_batch(std::span<const float> vectors,
                                std::span<const RecordID> ids,
                                const GpuIndexBuildParams& params) {
    auto* graph_params = std::get_if<GraphIndexBuildParams>(&params.params);
    if (!graph_params) return std::unexpected(GpuError::IndexBuildFailed);

    ids_.assign(ids.begin(), ids.end());
    count_ = ids_.size();

    GpuBuffer db_buf;
    auto alloc = backend_.allocate_device(vectors.size_bytes());
    if (!alloc.has_value()) return std::unexpected(alloc.error());
    db_buf = std::move(*alloc);
    auto upload_res = backend_.upload(vectors.data(), db_buf, vectors.size_bytes());
    if (!upload_res.has_value()) {
        backend_.free_device(std::move(db_buf));
        return std::unexpected(upload_res.error());
    }
    graph_data_ = std::move(db_buf);
    return {};
}

std::expected<std::vector<std::vector<SearchResult>>, GpuError>
GpuGraphIndex::search_batch(std::span<const float> queries, size_t k,
                            size_t ef_search) const {
    return std::unexpected(GpuError::IndexBuildFailed);
}

std::expected<void, GpuError>
GpuGraphIndex::export_to_cpu_index(elips::IndexPort& cpu_index_out) const {
    for (size_t i = 0; i < ids_.size(); ++i) {
        if (ids_[i] == RecordID{}) continue;
        auto* data = static_cast<const float*>(graph_data_.device_ptr());
        if (!data) continue;
        cpu_index_out.insert(ids_[i], std::span<const float>{data + i * dimension_, dimension_});
    }
    return {};
}

std::expected<void, GpuError>
GpuGraphIndex::import_from_cpu_index(const elips::IndexPort& cpu_index) {
    count_ = cpu_index.size();
    return {};
}

size_t GpuGraphIndex::device_bytes_used() const noexcept {
    return graph_data_.bytes();
}

std::string_view GpuGraphIndex::backend_name() const noexcept {
    static const std::string name = backend_.device_info().backend;
    return name;
}

} // namespace elips::gpu