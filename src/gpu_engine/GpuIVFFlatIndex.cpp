#include "elips/gpu_engine/GpuIVFFlatIndex.hpp"
#include "elips/vector_engine/Metrics.hpp"

namespace elips::gpu {

GpuIVFFlatIndex::GpuIVFFlatIndex(GpuPort& backend, elips::Metric metric,
                                 uint16_t dimension, const GpuConfig& config)
    : backend_(backend), metric_(metric), dimension_(dimension) {}

void GpuIVFFlatIndex::insert(const RecordID& id, std::span<const float> vector) { ++count_; }
void GpuIVFFlatIndex::remove(const RecordID& id) { if (count_ > 0) --count_; }

std::vector<IndexPort::Hit> GpuIVFFlatIndex::search(std::span<const float> query,
                                                     std::size_t k) const {
    return {};
}

std::size_t GpuIVFFlatIndex::size() const noexcept { return count_; }
std::string_view GpuIVFFlatIndex::type_name() const noexcept { return "gpu_ivf_flat"; }

std::expected<void, GpuError>
GpuIVFFlatIndex::build_from_batch(std::span<const float> vectors,
                                  std::span<const RecordID> ids,
                                  const GpuIndexBuildParams& params) {
    count_ = ids.size();
    return {};
}

std::expected<std::vector<std::vector<SearchResult>>, GpuError>
GpuIVFFlatIndex::search_batch(std::span<const float> queries, size_t k, size_t ef_search) const {
    return std::unexpected(GpuError::KernelLaunchFailed);
}

std::expected<void, GpuError>
GpuIVFFlatIndex::export_to_cpu_index(elips::IndexPort&) const { return {}; }

std::expected<void, GpuError>
GpuIVFFlatIndex::import_from_cpu_index(const elips::IndexPort&) { return {}; }

size_t GpuIVFFlatIndex::device_bytes_used() const noexcept { return 0; }
std::string_view GpuIVFFlatIndex::backend_name() const noexcept { return "ivf_flat"; }

} // namespace elips::gpu