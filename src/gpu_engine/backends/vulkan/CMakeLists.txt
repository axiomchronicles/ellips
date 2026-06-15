# Vulkan compute backend — requires Vulkan SDK 1.2+
# Requires glslangValidator for SPIR-V compilation

find_package(Vulkan 1.2 QUIET)
if(NOT Vulkan_FOUND)
    message(STATUS "Vulkan SDK not found; Vulkan backend disabled")
    return()
endif()

find_program(GLSLANG_VALIDATOR glslangValidator)
if(NOT GLSLANG_VALIDATOR)
    message(STATUS "glslangValidator not found; Vulkan backend disabled")
    return()
endif()

add_library(elips_gpu_vulkan STATIC
    VulkanBackend.cpp
)

target_include_directories(elips_gpu_vulkan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(elips_gpu_vulkan PUBLIC Vulkan::Vulkan)