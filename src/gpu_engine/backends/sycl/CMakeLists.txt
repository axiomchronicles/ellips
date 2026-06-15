# SYCL backend — requires Intel oneAPI DPC++

find_package(IntelSYCL QUIET)
if(NOT IntelSYCL_FOUND)
    message(STATUS "Intel SYCL not found; SYCL backend disabled")
    return()
endif()

add_library(elips_gpu_sycl STATIC
    SyclBackend.cpp
    kernels/cosine_fp32.cpp
)

target_include_directories(elips_gpu_sycl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_options(elips_gpu_sycl PRIVATE -O3 -fsycl)
target_link_options(elips_gpu_sycl PRIVATE -fsycl)