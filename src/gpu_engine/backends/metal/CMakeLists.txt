# Metal backend for Apple GPUs (macOS / iOS)

find_library(METAL_FRAMEWORK Metal REQUIRED)
find_library(MPS_FRAMEWORK MetalPerformanceShaders REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)

add_library(elips_gpu_metal STATIC
    MetalBackend.mm
)

target_include_directories(elips_gpu_metal PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../include
)

target_compile_definitions(elips_gpu_metal PRIVATE ELIPS_METAL_ENABLED)

set_source_files_properties(MetalBackend.mm PROPERTIES
    COMPILE_FLAGS "-fobjc-arc -O3 -ffast-math"
)
target_link_libraries(elips_gpu_metal PUBLIC
    ${METAL_FRAMEWORK}
    ${MPS_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
)

set_source_files_properties(MetalBackend.mm PROPERTIES
    COMPILE_FLAGS "-fobjc-arc -O3 -ffast-math"
)

target_compile_options(elips_gpu_metal PRIVATE -Wall -Wextra -Wpedantic)