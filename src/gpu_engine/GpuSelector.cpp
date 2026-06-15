#include "elips/gpu_engine/GpuSelector.hpp"
#include "elips/gpu_engine/GpuDeviceManager.hpp"

namespace elips::gpu {

int GpuSelector::rank_backend(std::string_view backend) const noexcept {
    if (backend == "cuda")   return 100;
    if (backend == "hip")    return 90;
    if (backend == "metal")  return 80;
    if (backend == "sycl")   return 50;
    if (backend == "vulkan") return 30;
    if (backend == "cpu")    return 0;
    return 0;
}

std::optional<std::unique_ptr<GpuPort>>
GpuSelector::select(const GpuConfig& config,
                    const std::vector<GpuDeviceInfo>& devices) const {
    if (config.policy == GpuPolicy::CpuOnly) return std::nullopt;
    if (devices.empty()) {
        if (config.policy == GpuPolicy::RequireGpu) return std::nullopt;
        return std::nullopt;
    }

    const GpuDeviceInfo& best = devices.front();

    if (config.policy == GpuPolicy::Specific && !config.preferred_backend.empty()) {
        for (const auto& dev : devices) {
            if (dev.backend == config.preferred_backend) {
                const_cast<GpuDeviceInfo&>(best) = dev;
                break;
            }
        }
    }

    GpuDeviceManager manager;
    return manager.select(config, devices);
}

} // namespace elips::gpu