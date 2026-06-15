#include "elips/gpu_engine/GpuQuantizationPipeline.hpp"
#include <algorithm>
#include <cstring>
#include <random>

namespace elips::gpu {

GpuQuantizationPipeline::GpuQuantizationPipeline(GpuPort& backend)
    : backend_(backend) {}

std::expected<void, GpuError>
GpuQuantizationPipeline::train_pq_codebook(
    std::span<const float> training_vectors, size_t n, size_t dim,
    size_t pq_dim, size_t n_lists, std::span<float> codebook_out) {
    size_t sub_dim = dim / pq_dim;
    size_t centroids_per_subspace = n_lists;
    size_t codebook_size = pq_dim * centroids_per_subspace * sub_dim;
    if (codebook_out.size() < codebook_size) {
        return std::unexpected(GpuError::InsufficientMemory);
    }

    std::mt19937 rng(42);
    for (size_t m = 0; m < pq_dim; ++m) {
        for (size_t c = 0; c < centroids_per_subspace && c < n; ++c) {
            float* centroid = codebook_out.data() +
                m * centroids_per_subspace * sub_dim + c * sub_dim;
            const float* example = training_vectors.data() + c * dim + m * sub_dim;
            std::memcpy(centroid, example, sub_dim * sizeof(float));
        }
    }
    return {};
}

std::expected<void, GpuError>
GpuQuantizationPipeline::encode_pq(
    std::span<const float> vectors, std::span<const float> codebook,
    size_t n, size_t dim, size_t pq_dim, std::span<uint8_t> codes_out) {
    size_t sub_dim = dim / pq_dim;
    size_t centroids_per_subspace = codebook.size() / (pq_dim * sub_dim);

    for (size_t i = 0; i < n; ++i) {
        for (size_t m = 0; m < pq_dim; ++m) {
            size_t best_c = 0;
            float best_dist = std::numeric_limits<float>::max();
            for (size_t c = 0; c < centroids_per_subspace; ++c) {
                float dist = 0;
                for (size_t d = 0; d < sub_dim; ++d) {
                    float diff = vectors[i * dim + m * sub_dim + d] -
                        codebook[m * centroids_per_subspace * sub_dim + c * sub_dim + d];
                    dist += diff * diff;
                }
                if (dist < best_dist) {
                    best_dist = dist;
                    best_c = c;
                }
            }
            codes_out[i * pq_dim + m] = static_cast<uint8_t>(best_c);
        }
    }
    return {};
}

} // namespace elips::gpu