#include <gtest/gtest.h>

#include "elips/gpu_engine/GpuDeviceManager.hpp"
#include "elips/gpu_engine/GpuSelector.hpp"
#include "elips/gpu_engine/GpuConfig.hpp"

namespace elips::gpu {
namespace {

TEST(GpuDeviceManagerTest, probe_returns_results_on_supported_system) {
    GpuDeviceManager manager;
    auto devices = manager.probe_all_devices();

#ifdef ELIPS_METAL_ENABLED
    if (devices.empty()) {
        GTEST_SKIP() << "No Metal device detected on this host.";
    }
    EXPECT_EQ(devices[0].backend, "metal");
    EXPECT_EQ(devices[0].vendor, "Apple");
    EXPECT_FALSE(devices[0].name.empty());
#else
    SUCCEED();
#endif
}

TEST(GpuDeviceManagerTest, selector_chooses_best_available) {
    GpuDeviceManager manager;
    auto devices = manager.probe_all_devices();
    GpuSelector selector;

    GpuConfig config;
    config.policy = GpuPolicy::Auto;

    auto backend = selector.select(config, devices);
    if (!devices.empty()) {
        EXPECT_TRUE(backend.has_value());
    }
}

TEST(GpuDeviceManagerTest, policy_cpu_only_returns_no_backend) {
    GpuDeviceManager manager;
    auto devices = manager.probe_all_devices();
    GpuSelector selector;

    GpuConfig config;
    config.policy = GpuPolicy::CpuOnly;

    auto backend = selector.select(config, devices);
    EXPECT_FALSE(backend.has_value());
}

TEST(GpuDeviceManagerTest, can_fit_index_estimation) {
    GpuDeviceManager manager;
    GpuDeviceInfo info;
    info.total_device_memory_bytes = 1024ULL * 1024 * 1024;
    info.free_device_memory_bytes = 512ULL * 1024 * 1024;
    info.supports_cagra = true;

    GpuConfig config;
    config.algorithm = GpuIndexAlgorithm::CagraGraph;

    EXPECT_TRUE(manager.can_fit_index(info, 10000, 128, config));
    EXPECT_TRUE(manager.can_fit_index(info, 10000, 1536, config));
}

TEST(GpuDeviceManagerTest, selector_ranks_cuda_over_vulkan) {
    GpuSelector selector;
    std::vector<GpuDeviceInfo> devices;

    GpuDeviceInfo cuda_dev;
    cuda_dev.backend = "cuda";
    cuda_dev.name = "Test NVIDIA";
    cuda_dev.supports_cagra = true;
    cuda_dev.total_device_memory_bytes = 1024ULL * 1024 * 1024;
    devices.push_back(cuda_dev);

    GpuDeviceInfo vulkan_dev;
    vulkan_dev.backend = "vulkan";
    vulkan_dev.name = "Test Vulkan";
    devices.push_back(vulkan_dev);

    GpuConfig config;
    config.policy = GpuPolicy::Auto;

    auto backend = selector.select(config, devices);
    if (backend.has_value()) {
        EXPECT_EQ((*backend)->device_info().vendor, "NVIDIA");
    }
}

}  // namespace
}  // namespace elips::gpu
