#include "elips/gpu_engine/GpuIVFPQIndex.hpp"

namespace elips::gpu {

GpuIVFPQIndex::GpuIVFPQIndex(GpuPort& backend, elips::Metric metric,
                             uint16_t dimension, const GpuConfig& config)
    : backend_(backend), metric_(metric), dimension_(dimension) {}

void GpuIVFPQIndex::insert(const RecordID&, std::span<const float>) { ++count_; }
void GpuIVFPQIndex::remove(const RecordID&) { if (count_ > 0) --count_; }

std::vector<IndexPort::Hit> GpuIVFPQIndex::search(std::span<const float>, std::size_t) const {
    return {};
}

std::size_t GpuIVFPQIndex::size() const noexcept { return count_; }
std::string_view GpuIVFPQIndex::type_name() const noexcept { return "gpu_ivf_pq"; }

std::expected<void, GpuError>
GpuIVFPQIndex::build_from_batch(std::span<const float>, std::span<const RecordID> ids,
                                const GpuIndexBuildParams&) {
    count_ = ids.size();
    return {};
}

std::expected<std::vector<std::vector<SearchResult>>, GpuError>
GpuIVFPQIndex::search_batch(std::span<const float>, size_t, size_t) const {
    return std::unexpected(GpuError::KernelLaunchFailed);
}

std::expected<void, GpuError>
GpuIVFPQIndex::export_to_cpu_index(elips::IndexPort&) const { return {}; }
std::expected<void, GpuError>
GpuIVFPQIndex::import_from_cpu_index(const elips::IndexPort&) { return {}; }

size_t GpuIVFPQIndex::device_bytes_used() const noexcept { return 0; }
std::string_view GpuIVFPQIndex::backend_name() const noexcept { return "ivf_pq"; }

} // namespace elips::gpu