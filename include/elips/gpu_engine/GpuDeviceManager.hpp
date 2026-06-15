#ifndef ELIPS_GPU_ENGINE_GPU_DEVICE_MANAGER_HPP
#define ELIPS_GPU_ENGINE_GPU_DEVICE_MANAGER_HPP

#include <memory>
#include <optional>
#include <vector>

#include "elips/gpu_engine/GpuConfig.hpp"
#include "elips/gpu_engine/GpuDeviceInfo.hpp"
#include "elips/gpu_engine/GpuPort.hpp"

namespace elips::gpu {

class GpuDeviceManager {
public:
    GpuDeviceManager() = default;

    [[nodiscard]] std::vector<GpuDeviceInfo> probe_all_devices() const;

    [[nodiscard]] std::optional<std::unique_ptr<GpuPort>>
    select(const GpuConfig& config, const std::vector<GpuDeviceInfo>& devices) const;

    [[nodiscard]] bool can_fit_index(
        const GpuDeviceInfo& dev,
        size_t n_vectors,
        size_t dim,
        const GpuConfig& config) const;
};

} // namespace elips::gpu

#endif