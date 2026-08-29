#pragma once

// Renderer owns every Vulkan object with a lifetime longer than one frame:
// device, swapchain, render targets, every pipeline, the loaded models, and
// their descriptor sets. main() owns the window, the Camera, the Scene, and the
// frame loop -- nothing else.
//
// Construction and destruction are explicit (Init / Shutdown) rather than RAII
// on the individual members. Vulkan teardown order is load-bearing and does not
// match reverse-declaration order, so an explicit Shutdown keeps that order
// visible in one place instead of scattered across member declarations. The
// destructor calls Shutdown if you forgot, so an exception mid-frame still
// cleans up.

#include <volk.h>
#include <glm/glm.hpp>
#include <vector>

#include <VulkanContext.h>
#include <Swapchain.h>
#include <RenderPass.h>
#include <GraphicsPipeline.h>
#include <TonemapPipeline.h>
#include <ResolvePipeline.h>
#include <DebugViewPipeline.h>
#include <SSRPipeline.h>
#include <SSAOPipeline.h>
#include <HiZPipeline.h>
#include <ShadowMap.h>
#include <ShadowPipeline.h>
#include <Skybox.h>
#include <ClusterBuilder.h>
#include <LightCuller.h>
#include <ImGuiManager.h>
#include <Model.h>
#include <Buffer.h>
#include <VulkanTexture.h>

#include "AppState.h"
#include "ScenePresets.h"
#include "FrameRecorder.h"

struct GLFWwindow;
class Camera;

class Renderer
{
public:
    Renderer() = default;
    ~Renderer();

    // PassResources holds pointers to our own members, so copying or moving
    // a Renderer would leave those pointers dangling.
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    void Init(GLFWwindow* window, ScenePreset preset, uint32_t width, uint32_t height);
    void Shutdown();

    // Per-frame CPU work
    uint32_t AcquireNextImage();
    int      CurrentFrameInFlight();
    void     SubmitAndPresent(uint32_t imageIndex);

    // Reads the 1x1 HDR downsample this frame-in-flight wrote. One frame of
    // latency by design: no stall.
    float MeasureLuminance(int frameInFlight) const;

    void CullAndUploadLights(const Scene& scene, const glm::mat4& view);
    void UploadPenumbraRamps(const Scene& scene);
    void UpdatePerObjectUniforms(const Scene& scene, const Camera& camera,
        uint32_t imageIndex, const glm::mat4& lightViewProj,
        const TemporalState& temporal);
    void UpdateSkybox(uint32_t imageIndex, const Camera& camera);

    // Accessors
    const PassResources& Passes()      const { return m_passResources; }
    const std::vector<Model>& Models()      const { return m_models; }
    const SceneDescription& Description() const { return m_description; }
    ImGuiManager& ImGuiMgr() { return m_imgui; }

    VkCommandBuffer CommandBuffer(uint32_t imageIndex) const { return m_commandBuffers[imageIndex]; }
    uint32_t        ImageCount()  const { return static_cast<uint32_t>(m_swapchain.GetImages().size()); }
    float           AspectRatio() const { return float(m_width) / float(m_height); }

private:
    // Init is split so each stage reads as one idea. Order between them
    // matters; order within them mostly doesn't.
    void CreateDeviceAndTargets(GLFWwindow* window, uint32_t width, uint32_t height);
    void CreateClusteredLighting();
    void CreateSceneAssets(GLFWwindow* window, ScenePreset preset);
    void CreateScenePipelines(GLFWwindow* window);
    void LoadModelsAndDescriptorSets();
    void CreatePostProcessPipelines(GLFWwindow* window);
    void SeedHistoryImageLayouts();
    void CreateEditorAndCommandBuffers(GLFWwindow* window);
    void PublishPassResources();

    VkDevice Device() const { return m_context.GetDevice(); }

    // Core
    VulkanContext m_context;
    Swapchain     m_swapchain;
    RenderPass    m_targets;

    uint32_t m_width = 0;
    uint32_t m_height = 0;

    // Clustered lighting
    ClusterBuilder  m_clusterBuilder;
    LightCuller     m_lightCuller;
    BufferAndMemory m_lightBuffer{};
    BufferAndMemory m_rampBuffer{};

    // Scene assets
    SceneDescription           m_description;
    Skybox                     m_skybox;
    VulkanContext::IBLTextures m_ibl{};

    std::vector<Model>                        m_models;
    std::vector<std::vector<BufferAndMemory>> m_modelUniformBuffers;

    // Pipelines 
    GraphicsPipeline  m_scenePipeline;
    ShadowMap         m_shadowMap;
    ShadowPipeline    m_shadowPipeline;
    TonemapPipeline   m_tonemapPipeline;
    DebugViewPipeline m_debugPipeline;
    ResolvePipeline   m_resolvePipeline;
    SSRPipeline       m_ssrPipeline;
    SSAOPipeline      m_ssaoPipeline;
    HiZPipeline       m_hizPipeline;

    BufferAndMemory m_ssaoKernelBuffer{};
    VulkanTexture   m_ssaoNoiseTex{};
    VkSampler       m_ssaoNoiseSampler = VK_NULL_HANDLE;
    VkSampler       m_hizSampler = VK_NULL_HANDLE;

    // Editor + submission
    ImGuiManager                 m_imgui;
    std::vector<VkCommandBuffer> m_commandBuffers;

    PassResources m_passResources{};
    bool           m_initialized = false;
};