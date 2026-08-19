cmake_minimum_required(VERSION 3.20)
project(FusLite)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

include(FetchContent)

set(IMGUI_DIR "$ENV{USERPROFILE}/Documents/imgui" CACHE PATH "Path to local ImGui clone")
set(IMGUIZMO_DIR "$ENV{USERPROFILE}/Documents/ImGuizmo/src" CACHE PATH "Path to local ImGuizmo clone")

if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
    message(FATAL_ERROR "ImGui not found at ${IMGUI_DIR} — clone it there or set IMGUI_DIR")
endif()
if(NOT EXISTS "${IMGUIZMO_DIR}/ImGuizmo.h")
    message(FATAL_ERROR "ImGuizmo not found at ${IMGUIZMO_DIR} — clone it there or set IMGUIZMO_DIR")
endif()

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG 1.3.270
)
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.1.0
)

# GLFW: skip building tests/docs/examples, we only need the library
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw glm volk vma)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)
FetchContent_MakeAvailable(stb)

FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v5.4.3
)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(assimp)

add_executable(FusLite 
    src/main.cpp 
    src/core/VulkanContext.cpp
    src/core/Swapchain.cpp
    src/core/VulkanQueue.cpp
    src/core/ShaderModule.cpp
    src/core/RenderPass.cpp
    src/core/GraphicsPipeline.cpp
    src/core/Camera.cpp
    src/core/Model.cpp
    src/core/stb_image_impl.cpp
    src/core/TonemapPipeline.cpp
    src/core/ResolvePipeline.cpp
    src/core/Skybox.cpp
    src/core/ImGuiManager.cpp
    src/core/ComputeTest.cpp
    src/core/ClusterBuilder.cpp
    src/core/LightCuller.cpp
    src/core/ShadowPipeline.cpp
    src/core/ShadowMap.cpp
    src/core/DebugViewPipeline.cpp
    src/core/SSRPipeline.cpp
    src/core/HiZPipeline.cpp
    src/core/SSAOPipeline.cpp

    # ImGui core
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp

    # ImGui backends
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp

    # ImGuizmo
    ${IMGUIZMO_DIR}/ImGuizmo.cpp
)

target_link_libraries(FusLite PRIVATE
    glfw
    volk
    assimp
    Vulkan::Vulkan
)

target_include_directories(FusLite PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${glm_SOURCE_DIR}
    ${vma_SOURCE_DIR}/include
    ${stb_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IMGUIZMO_DIR}
)

target_compile_definitions(FusLite PRIVATE 
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    IMGUI_IMPL_VULKAN_USE_VOLK
)

find_program(GLSLC glslc REQUIRED)

file(GLOB SHADER_SOURCES "${CMAKE_SOURCE_DIR}/shaders/*.vert" "${CMAKE_SOURCE_DIR}/shaders/*.frag" "${CMAKE_SOURCE_DIR}/shaders/*.comp")

foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND ${GLSLC} ${SHADER} -o ${SPIRV_OUTPUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader: ${SHADER_NAME}"
    )
    list(APPEND SPIRV_BINARIES ${SPIRV_OUTPUT})
endforeach()

add_custom_target(Shaders DEPENDS ${SPIRV_BINARIES})
add_dependencies(FusLite Shaders)

add_custom_command(TARGET FusLite POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:FusLite>/assets"
)