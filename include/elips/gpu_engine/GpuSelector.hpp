#ifndef ELIPS_GPU_ENGINE_GPU_SELECTOR_HPP
#define ELIPS_GPU_ENGINE_GPU_SELECTOR_HPP

#include <memory>
#include <optional>
#include <vector>

#include "elips/gpu_engine/GpuConfig.hpp"
#include "elips/gpu_engine/GpuDeviceInfo.hpp"
#include "elips/gpu_engine/GpuPort.hpp"

namespace elips::gpu {

class GpuSelector {
public:
    [[nodiscard]] std::optional<std::unique_ptr<GpuPort>>
    select(const GpuConfig& config, const std::vector<GpuDeviceInfo>& devices) const;

private:
    [[nodiscard]] int rank_backend(std::string_view backend) const noexcept;
};

} // namespace elips::gpu

#endif