#ifndef ELIPS_INDEX_ENGINE_INDEX_FACTORY_HPP
#define ELIPS_INDEX_ENGINE_INDEX_FACTORY_HPP

#include <cstdint>
#include <memory>

#include "elips/Config.hpp"
#include "elips/index_engine/IndexPort.hpp"

namespace elips {

#ifdef ELIPS_GPU_ENABLED
namespace gpu {
class GpuPort;
}
#endif

// Builds the IndexPort implementation selected by the configuration (DIP:
// callers depend only on IndexPort, never a concrete index).
[[nodiscard]] std::unique_ptr<IndexPort> make_index(const Config& config,
                                                    std::uint16_t dimension
#ifdef ELIPS_GPU_ENABLED
                                                    , gpu::GpuPort* gpu_backend = nullptr
#endif
);

}  // namespace elips

#endif  // ELIPS_INDEX_ENGINE_INDEX_FACTORY_HPP
