#include "elips/gpu_engine/GpuBruteForceIndex.hpp"
#include "elips/gpu_engine/GpuSearchPipeline.hpp"
#include "elips/vector_engine/Metrics.hpp"

namespace elips::gpu {

GpuBruteForceIndex::GpuBruteForceIndex(GpuPort& backend, elips::Metric metric,
                                       uint16_t dimension, const GpuConfig& config)
    : backend_(backend), metric_(metric), dimension_(dimension) {}

void GpuBruteForceIndex::insert(const RecordID& id, std::span<const float> vector) {
    ids_.push_back(id);
    ++count_;
}

void GpuBruteForceIndex::remove(const RecordID& id) {
    auto it = std::find(ids_.begin(), ids_.end(), id);
    if (it != ids_.end()) {
        *it = RecordID{};
        --count_;
    }
}

std::vector<IndexPort::Hit> GpuBruteForceIndex::search(std::span<const float> query,
                                                        std::size_t k) const {
    if (count_ == 0 || k == 0 || !database_buffer_) return {};

    std::vector<float> distances(count_);
    auto result = backend_.compute_distances_batch(
        query, std::span<const float>{static_cast<const float*>(database_buffer_.device_ptr()), count_ * dimension_},
        distances, 1, count_, dimension_, metric_);
    if (!result.has_value()) return {};

    std::vector<uint32_t> indices(k);
    std::vector<float> vals(k);
    auto tk = backend_.top_k(distances, indices, vals, 1, count_, k);
    if (!tk.has_value()) return {};

    std::vector<Hit> hits;
    for (size_t i = 0; i < k && indices[i] < ids_.size(); ++i) {
        hits.emplace_back(ids_[indices[i]], vals[i]);
    }
    return hits;
}

std::size_t GpuBruteForceIndex::size() const noexcept { return count_; }
std::string_view GpuBruteForceIndex::type_name() const noexcept { return "gpu_brute_force"; }

std::expected<void, GpuError>
GpuBruteForceIndex::build_from_batch(std::span<const float> vectors,
                                     std::span<const RecordID> ids,
                                     const GpuIndexBuildParams& params) {
    ids_.assign(ids.begin(), ids.end());
    count_ = ids_.size();

    auto alloc = backend_.allocate_device(vectors.size_bytes());
    if (!alloc.has_value()) return std::unexpected(alloc.error());
    database_buffer_ = std::move(*alloc);
    return backend_.upload(vectors.data(), database_buffer_, vectors.size_bytes());
}

std::expected<std::vector<std::vector<SearchResult>>, GpuError>
GpuBruteForceIndex::search_batch(std::span<const float> queries, size_t k,
                                  size_t ef_search) const {
    if (!database_buffer_) return std::unexpected(GpuError::IndexBuildFailed);

    size_t nq = queries.size() / dimension_;
    GpuSearchPipeline pipeline(const_cast<GpuPort&>(backend_));
    return pipeline.batch_search(queries, nq,
        std::span<const float>{static_cast<const float*>(database_buffer_.device_ptr()), count_ * dimension_},
        ids_, count_, dimension_, k, metric_);
}

std::expected<void, GpuError>
GpuBruteForceIndex::export_to_cpu_index(elips::IndexPort& cpu_index_out) const {
    for (size_t i = 0; i < ids_.size(); ++i) {
        if (ids_[i] == RecordID{}) continue;
        auto* data = static_cast<const float*>(database_buffer_.device_ptr());
        if (!data) continue;
        cpu_index_out.insert(ids_[i], std::span<const float>{data + i * dimension_, dimension_});
    }
    return {};
}

std::expected<void, GpuError>
GpuBruteForceIndex::import_from_cpu_index(const elips::IndexPort&) { return {}; }

size_t GpuBruteForceIndex::device_bytes_used() const noexcept {
    return database_buffer_.bytes();
}

std::string_view GpuBruteForceIndex::backend_name() const noexcept {
    static const std::string name = backend_.device_info().backend;
    return name;
}

} // namespace elips::gpu