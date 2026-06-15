#include "elips/gpu_engine/GpuProfiler.hpp"

namespace elips::gpu {

void GpuProfiler::record(const std::string& kernel,
                         std::chrono::microseconds duration, size_t items) {
    timings_.push_back({kernel, duration, items});
    total_launches_.fetch_add(1, std::memory_order_relaxed);
}

std::vector<KernelTiming> GpuProfiler::recent_timings(size_t max_count) const {
    if (timings_.size() <= max_count) return timings_;
    return {timings_.end() - static_cast<ptrdiff_t>(max_count), timings_.end()};
}

size_t GpuProfiler::total_launches() const noexcept {
    return total_launches_.load(std::memory_order_relaxed);
}

void GpuProfiler::clear() {
    timings_.clear();
    total_launches_.store(0, std::memory_order_relaxed);
}

} // namespace elips::gpu