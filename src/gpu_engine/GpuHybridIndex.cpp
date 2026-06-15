#include "elips/gpu_engine/GpuHybridIndex.hpp"
#include "elips/gpu_engine/GpuGraphIndex.hpp"

namespace elips::gpu {

GpuHybridIndex::GpuHybridIndex(GpuPort& backend,
                               std::unique_ptr<elips::IndexPort> cpu_index,
                               elips::Metric metric,
                               uint16_t dimension,
                               const GpuConfig& config)
    : backend_(backend)
    , cpu_index_(std::move(cpu_index))
    , gpu_index_(std::make_unique<GpuGraphIndex>(backend, metric, dimension, config))
    , metric_(metric)
    , dimension_(dimension) {}

void GpuHybridIndex::insert(const RecordID& id, std::span<const float> vector) {
    cpu_index_->insert(id, vector);
    gpu_index_->insert(id, vector);
}

void GpuHybridIndex::remove(const RecordID& id) {
    cpu_index_->remove(id);
    gpu_index_->remove(id);
}

std::vector<IndexPort::Hit> GpuHybridIndex::search(std::span<const float> query,
                                                    std::size_t k) const {
    return cpu_index_->search(query, k);
}

std::size_t GpuHybridIndex::size() const noexcept { return cpu_index_->size(); }
std::string_view GpuHybridIndex::type_name() const noexcept { return "gpu_hybrid"; }

std::expected<void, GpuError>
GpuHybridIndex::build_from_batch(std::span<const float> vectors,
                                 std::span<const RecordID> ids,
                                 const GpuIndexBuildParams& params) {
    return gpu_index_->build_from_batch(vectors, ids, params);
}

std::expected<std::vector<std::vector<SearchResult>>, GpuError>
GpuHybridIndex::search_batch(std::span<const float> queries, size_t k, size_t ef_search) const {
    return gpu_index_->search_batch(queries, k, ef_search);
}

std::expected<void, GpuError>
GpuHybridIndex::export_to_cpu_index(elips::IndexPort& cpu_index_out) const {
    return gpu_index_->export_to_cpu_index(cpu_index_out);
}

std::expected<void, GpuError>
GpuHybridIndex::import_from_cpu_index(const elips::IndexPort& cpu_index) {
    return gpu_index_->import_from_cpu_index(cpu_index);
}

size_t GpuHybridIndex::device_bytes_used() const noexcept {
    return gpu_index_->device_bytes_used();
}

std::string_view GpuHybridIndex::backend_name() const noexcept {
    return gpu_index_->backend_name();
}

} // namespace elips::gpu