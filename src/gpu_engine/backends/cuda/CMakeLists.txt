# CUDA backend — requires NVIDIA CUDA Toolkit 12.0+
# Not compiled on this system; requires nvcc.

find_package(CUDAToolkit 12.0 QUIET)
if(NOT CUDAToolkit_FOUND)
    message(STATUS "CUDA Toolkit not found; CUDA backend disabled")
    return()
endif()

enable_language(CUDA)
set(CMAKE_CUDA_ARCHITECTURES "70;80;86;89;90;100")
set(CMAKE_CUDA_STANDARD 17)

add_library(elips_gpu_cuda STATIC
    CudaBackend.cu
    kernels/cosine_fp32.cu
)

target_include_directories(elips_gpu_cuda PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(elips_gpu_cuda PUBLIC
    CUDA::cudart
    CUDA::cublas
)

target_compile_options(elips_gpu_cuda PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:-O3 --use_fast_math --expt-relaxed-constexpr>
)