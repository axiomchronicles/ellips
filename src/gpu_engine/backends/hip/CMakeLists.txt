# HIP/ROCm backend — requires AMD ROCm SDK
# Uses hipcc for compilation.

find_package(hip QUIET)
if(NOT hip_FOUND)
    message(STATUS "ROCm/HIP not found; HIP backend disabled")
    return()
endif()

add_library(elips_gpu_hip STATIC
    HipBackend.hip
    kernels/cosine_fp32.hip
)

target_include_directories(elips_gpu_hip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(elips_gpu_hip PUBLIC roc::rocblas)
target_compile_options(elips_gpu_hip PRIVATE -O3 --ffast-math)