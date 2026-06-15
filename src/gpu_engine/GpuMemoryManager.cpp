#include "elips/gpu_engine/GpuMemoryManager.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace elips::gpu {

GpuMemoryManager::GpuMemoryManager(GpuPort& backend)
    : backend_(backend) {}

GpuMemoryManager::~GpuMemoryManager() {
    shutdown();
}

std::expected<void, GpuError> GpuMemoryManager::initialize(size_t pool_bytes) {
    if (pool_bytes == 0) {
        auto info = backend_.device_info();
        pool_bytes = static_cast<size_t>(static_cast<double>(info.total_device_memory_bytes) * 0.8);
    }
    pool_bytes_ = pool_bytes;
    return {};
}

std::expected<GpuBuffer, GpuError>
GpuMemoryManager::allocate(size_t bytes, size_t alignment) {
    std::lock_guard lock(mutex_);

    size_t best_idx = SIZE_MAX;
    size_t best_waste = SIZE_MAX;
    for (size_t i = 0; i < free_blocks_.size(); ++i) {
        if (free_blocks_[i].bytes >= bytes) {
            size_t waste = free_blocks_[i].bytes - bytes;
            if (waste < best_waste) {
                best_waste = waste;
                best_idx = i;
            }
        }
    }

    if (best_idx != SIZE_MAX) {
        FreeBlock block = free_blocks_[best_idx];
        free_blocks_.erase(free_blocks_.begin() + static_cast<ptrdiff_t>(best_idx));
        allocated_ += bytes;
        peak_allocated_ = std::max(peak_allocated_, allocated_);
        return GpuBuffer{block.ptr, bytes, nullptr};
    }

    size_t alloc_size = std::max(bytes, pool_bytes_ / 16);
    if (allocated_ + alloc_size > pool_bytes_) {
        return std::unexpected(GpuError::InsufficientMemory);
    }

    auto result = backend_.allocate_device(alloc_size);
    if (!result.has_value()) return std::unexpected(result.error());

    allocated_ += bytes;
    peak_allocated_ = std::max(peak_allocated_, allocated_);

    if (alloc_size > bytes) {
        void* remaining = static_cast<char*>(result->device_ptr()) + bytes;
        free_blocks_.push_back({remaining, alloc_size - bytes});
    }

    return GpuBuffer{result->device_ptr(), bytes, nullptr};
}

void GpuMemoryManager::deallocate(GpuBuffer&& buf) noexcept {
    if (!buf) return;
    std::lock_guard lock(mutex_);
    allocated_ -= buf.bytes();
    free_blocks_.push_back({buf.device_ptr(), buf.bytes()});
}

std::expected<void*, GpuError> GpuMemoryManager::allocate_pinned(size_t bytes) {
#if defined(__APPLE__)
    void* ptr = std::aligned_alloc(4096, bytes);
    if (!ptr) return std::unexpected(GpuError::InsufficientMemory);
    std::lock_guard lock(mutex_);
    pinned_blocks_.push_back(ptr);
    return ptr;
#else
    void* ptr = std::aligned_alloc(4096, bytes);
    if (!ptr) return std::unexpected(GpuError::InsufficientMemory);
    std::lock_guard lock(mutex_);
    pinned_blocks_.push_back(ptr);
    return ptr;
#endif
}

void GpuMemoryManager::deallocate_pinned(void* ptr) noexcept {
    if (!ptr) return;
    std::lock_guard lock(mutex_);
    auto it = std::find(pinned_blocks_.begin(), pinned_blocks_.end(), ptr);
    if (it != pinned_blocks_.end()) {
        pinned_blocks_.erase(it);
    }
    std::free(ptr);
}

size_t GpuMemoryManager::bytes_used() const noexcept {
    std::lock_guard lock(mutex_);
    return allocated_;
}

size_t GpuMemoryManager::bytes_available() const noexcept {
    std::lock_guard lock(mutex_);
    return pool_bytes_ - allocated_;
}

size_t GpuMemoryManager::peak_bytes_used() const noexcept {
    std::lock_guard lock(mutex_);
    return peak_allocated_;
}

void GpuMemoryManager::shutdown() noexcept {
    std::lock_guard lock(mutex_);
    for (auto& block : free_blocks_) {
        backend_.free_device(GpuBuffer{block.ptr, block.bytes, nullptr});
    }
    free_blocks_.clear();
    for (auto* ptr : pinned_blocks_) {
        std::free(ptr);
    }
    pinned_blocks_.clear();
    pool_bytes_ = 0;
}

} // namespace elips::gpu