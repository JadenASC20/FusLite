#include "Renderer.h"
#include "VulkanHelpers.h"

#include <Camera.h>
#include <ClusterConfig.h>
#include <RampUtil.h>
#include <UniformBufferObject.h>

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>

namespace {

    constexpr float kNearZ = 0.1f;
    constexpr float kFarZ = 1000.0f;

    constexpr int kSSAOKernelSize = 32;
    constexpr int kSSAONoiseDim = 4;

    // Descriptor sets per material, per swapchain image. 256 materials is well
    // clear of anything the current assets need.
    constexpr uint32_t kMaxMaterialsPerImage = 256;

    // SSAO sample kernel: hemisphere-distributed, weighted toward the origin so
    // nearby occluders dominate.
    BufferAndMemory CreateSSAOKernelBuffer(VulkanContext& context, int kernelSize)
    {
        std::vector<glm::vec4> kernel(kernelSize);
        std::uniform_real_distribution<float> rnd(0.0f, 1.0f);
        std::default_random_engine gen;

        for (int i = 0; i < kernelSize; i++) {
            glm::vec3 sample(rnd(gen) * 2.0f - 1.0f, rnd(gen) * 2.0f - 1.0f, rnd(gen));
            sample = glm::normalize(sample) * rnd(gen);
            const float t = float(i) / float(kernelSize);
            kernel[i] = glm::vec4(sample * glm::mix(0.1f, 1.0f, t * t), 0.0f);
        }

        const VkDeviceSize bytes = sizeof(glm::vec4) * kernelSize;
        BufferAndMemory buffer = context.CreateUniformBuffer(bytes);

        void* mapped;
        vkMapMemory(context.GetDevice(), buffer.memory, 0, bytes, 0, &mapped);
        memcpy(mapped, kernel.data(), bytes);
        vkUnmapMemory(context.GetDevice(), buffer.memory);
        return buffer;
    }

    // Random rotation vectors, tiled across the screen to trade banding for noise.
    VulkanTexture CreateSSAONoiseTexture(VulkanContext& context, int dim)
    {
        std::vector<unsigned char> pixels(dim * dim * 4);
        std::uniform_real_distribution<float> rnd(0.0f, 1.0f);
        std::default_random_engine gen;

        for (int i = 0; i < dim * dim; i++) {
            pixels[i * 4 + 0] = static_cast<unsigned char>(rnd(gen) * 255.0f);
            pixels[i * 4 + 1] = static_cast<unsigned char>(rnd(gen) * 255.0f);
            pixels[i * 4 + 2] = 0;
            pixels[i * 4 + 3] = 255;
        }
        return context.CreateTextureFromRawRGBA(pixels.data(), dim, dim, false);
    }

    // Uploads a host-visible buffer in one shot.
    void UploadToBuffer(VkDevice device, const BufferAndMemory& buffer,
        const void* src, VkDeviceSize bytes)
    {
        void* mapped;
        vkMapMemory(device, buffer.memory, 0, bytes, 0, &mapped);
        memcpy(mapped, src, bytes);
        vkUnmapMemory(device, buffer.memory);
    }

}

Renderer::~Renderer()
{
    Shutdown();
}

// 
// Init
// 

void Renderer::Init(GLFWwindow* window, ScenePreset preset, uint32_t width, uint32_t height)
{
    CreateDeviceAndTargets(window, width, height);
    CreateClusteredLighting();
    CreateSceneAssets(window, preset);
    CreateScenePipelines(window);
    LoadModelsAndDescriptorSets();
    CreatePostProcessPipelines(window);
    SeedHistoryImageLayouts();
    CreateEditorAndCommandBuffers(window);
    PublishPassResources();

    m_initialized = true;
}

void Renderer::CreateDeviceAndTargets(GLFWwindow* window, uint32_t width, uint32_t height)
{
    m_width = width;
    m_height = height;

    m_context.Init("FusLite", window);

    m_swapchain.Init(m_context, width, height);
    m_context.CreateQueue(m_swapchain.GetHandle(), ImageCount());

    m_targets.Init(m_context, m_swapchain);
}

void Renderer::CreateClusteredLighting()
{
    m_clusterBuilder.Init(m_context);

    const glm::mat4 proj = glm::perspective(glm::radians(45.0f), AspectRatio(), kNearZ, kFarZ);
    m_clusterBuilder.BuildClusters(m_context, glm::inverse(proj),
        float(m_width), float(m_height), kNearZ, kFarZ);

    m_lightBuffer = m_context.CreateStorageBuffer(sizeof(GPULight) * MAX_LIGHTS);
    m_lightCuller.Init(m_context, m_clusterBuilder.GetClusterBuffer(), m_lightBuffer);

    m_rampBuffer = m_context.CreateStorageBuffer(
        sizeof(glm::vec4) * MAX_RAMP_OBJECTS * RAMP_RESOLUTION);
}

void Renderer::CreateSceneAssets(GLFWwindow* window, ScenePreset preset)
{
    m_description = BuildScene(preset);

    printf("Scene: %s (%zu models)\n", m_description.name, m_description.modelPaths.size());
    printf("CWD:   %s\n", std::filesystem::current_path().string().c_str());

    m_skybox.Init(m_context, window, m_targets.GetHdrFormat(), m_targets.GetDepthFormat(),
        m_description.skyboxHdri.c_str(), ImageCount(),
        m_targets.GetMotionFormat(), m_targets.GetNormalFormat(), m_targets.GetMaterialFormat());

    m_ibl = m_context.CreateIBLFromEquirect(m_description.skyboxHdri.c_str());
}

void Renderer::CreateScenePipelines(GLFWwindow* window)
{
    {
        ScopedShader vert(Device(), "shaders/triangle.vert.spv");
        ScopedShader frag(Device(), "shaders/triangle.frag.spv");
        m_scenePipeline.Init(m_context, window, m_targets.GetHdrFormat(), m_targets.GetDepthFormat(),
            vert, frag, kMaxMaterialsPerImage * ImageCount(),
            m_targets.GetMotionFormat(), m_targets.GetNormalFormat(), m_targets.GetMaterialFormat());
    }

    m_shadowMap.Init(m_context, 2048);
    {
        ScopedShader vert(Device(), "shaders/shadow_depth.vert.spv");
        ScopedShader frag(Device(), "shaders/shadow_depth.frag.spv");
        m_shadowPipeline.Init(m_context, m_shadowMap.GetFormat(), m_shadowMap.GetResolution(), vert, frag);
    }
}

void Renderer::LoadModelsAndDescriptorSets()
{
    const size_t modelCount = m_description.modelPaths.size();
    m_models.resize(modelCount);
    m_modelUniformBuffers.resize(modelCount);

    for (size_t i = 0; i < modelCount; i++) {
        m_models[i].LoadFromFile(m_context, m_description.modelPaths[i]);
        printf(">>> Model %zu (%s): %zu materials\n", i,
            m_description.modelPaths[i].c_str(),
            m_models[i].GetDefaultMaterials().size());
        fflush(stdout);

        m_modelUniformBuffers[i].resize(ImageCount());
        for (auto& ubo : m_modelUniformBuffers[i]) {
            ubo = m_context.CreateUniformBuffer(sizeof(UniformBufferObject));
        }

        m_models[i].CreateDescriptorSets(m_scenePipeline, m_modelUniformBuffers[i],
            sizeof(UniformBufferObject), m_ibl, m_lightBuffer,
            m_lightCuller.GetClusterLightInfoBuffer(), m_lightCuller.GetLightIndexBuffer(),
            m_shadowMap.GetImageView(), m_shadowMap.GetSampler(), m_shadowMap.GetCompareSampler(),
            m_rampBuffer);
    }
}

void Renderer::CreatePostProcessPipelines(GLFWwindow* window)
{
    {
        ScopedShader vert(Device(), "shaders/fullscreen.vert.spv");
        ScopedShader frag(Device(), "shaders/tonemap.frag.spv");
        m_tonemapPipeline.Init(m_context, window, m_swapchain.GetImageFormat(),
            vert, frag, m_targets.GetHdrImageViews());
    }
    {
        ScopedShader vert(Device(), "shaders/fullscreen.vert.spv");
        ScopedShader frag(Device(), "shaders/debug_view.frag.spv");
        m_debugPipeline.Init(m_context, m_swapchain.GetImageFormat(), vert, frag);
    }
    {
        ScopedShader vert(Device(), "shaders/fullscreen.vert.spv");
        ScopedShader ssrFrag(Device(), "shaders/ssr.frag.spv");
        ScopedShader compFrag(Device(), "shaders/composite.frag.spv");
        m_ssrPipeline.Init(m_context, m_targets.GetSSRFormat(), m_targets.GetHdrFormat(),
            vert, ssrFrag, compFrag,
            m_targets.GetHdrImageViews(), m_targets.GetDepthImageViews(),
            m_targets.GetNormalImageViews(), m_targets.GetSSRImageViews(),
            m_targets.GetSSAOImageViews(),
            m_targets.GetHiZSampleView(),
            m_ibl.prefilteredSpecular.view, m_ibl.prefilteredSpecular.sampler,
            m_targets.GetMaterialImageViews());
    }

    m_ssaoKernelBuffer = CreateSSAOKernelBuffer(m_context, kSSAOKernelSize);
    m_ssaoNoiseTex = CreateSSAONoiseTexture(m_context, kSSAONoiseDim);
    // The noise texture must tile, so it needs REPEAT rather than the default clamp.
    m_ssaoNoiseSampler = CreateSimpleSampler(Device(),
        VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_MIPMAP_MODE_NEAREST);
    {
        ScopedShader vert(Device(), "shaders/fullscreen.vert.spv");
        ScopedShader frag(Device(), "shaders/ssao.frag.spv");
        m_ssaoPipeline.Init(m_context, m_targets.GetSSAOFormat(), vert, frag,
            m_targets.GetDepthImageViews(), m_targets.GetNormalImageViews(),
            m_ssaoNoiseTex.view, m_ssaoNoiseSampler,
            m_ssaoKernelBuffer, kSSAOKernelSize);
    }
    {
        ScopedShader vert(Device(), "shaders/fullscreen.vert.spv");
        ScopedShader frag(Device(), "shaders/taa_resolve.frag.spv");
        m_resolvePipeline.Init(m_context, window, m_targets.GetHdrFormat(), vert, frag,
            m_targets.GetCompositeImageViews(), m_targets.GetMotionImageViews(),
            m_targets.GetHistoryImageViews());
    }

    // Nearest + clamp: Hi-Z must not interpolate depth across texels or the
    // conservative-max property of the pyramid breaks.
    m_hizSampler = CreateSimpleSampler(Device(),
        VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_MIPMAP_MODE_NEAREST);
    {
        ScopedShader comp(Device(), "shaders/hiz_build.comp.spv");
        m_hizPipeline.Init(m_context, comp, m_hizSampler,
            m_targets.GetDepthImageViews(), m_targets.GetHiZMipViews(), m_targets.GetHiZMipLevels());
    }
}

void Renderer::SeedHistoryImageLayouts()
{
    // Both history images must start every frame in SHADER_READ for the TAA
    // ping-pong to need no extra barriers. Establish that before frame 0.
    ImmediateSubmit(m_context, [&](VkCommandBuffer cmd) {
        for (int i = 0; i < 2; i++) {
            TransitionImage(cmd, m_targets.GetHistoryImages()[i],
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, VK_ACCESS_SHADER_READ_BIT);
        }
        });
}

void Renderer::CreateEditorAndCommandBuffers(GLFWwindow* window)
{
    m_imgui.Init(m_context, window, m_swapchain.GetImageFormat(), ImageCount());
    ImGui::GetIO().FontGlobalScale = 1.5f;

    m_commandBuffers.resize(m_swapchain.GetImageViews().size());
    m_context.CreateCommandBuffers(
        static_cast<uint32_t>(m_commandBuffers.size()), m_commandBuffers.data());
}

void Renderer::PublishPassResources()
{
    m_passResources.swapchain = &m_swapchain;
    m_passResources.targets = &m_targets;
    m_passResources.scenePipeline = &m_scenePipeline;
    m_passResources.shadowPipeline = &m_shadowPipeline;
    m_passResources.shadowMap = &m_shadowMap;
    m_passResources.skybox = &m_skybox;
    m_passResources.ssr = &m_ssrPipeline;
    m_passResources.ssao = &m_ssaoPipeline;
    m_passResources.hiz = &m_hizPipeline;
    m_passResources.resolve = &m_resolvePipeline;
    m_passResources.tonemap = &m_tonemapPipeline;
    m_passResources.debugView = &m_debugPipeline;
    m_passResources.imgui = &m_imgui;
}

// Shutdown
//
// Order is load-bearing. Device idle first, then anything that references
// another object before the thing it references.

void Renderer::Shutdown()
{
    if (!m_initialized) return;
    m_initialized = false;

    m_context.Shutdown();   // waits for the device to go idle

    m_context.FreeCommandBuffers(
        static_cast<uint32_t>(m_commandBuffers.size()), m_commandBuffers.data());
    m_commandBuffers.clear();

    for (auto& model : m_models) model.Cleanup(Device());
    for (auto& ubos : m_modelUniformBuffers) {
        for (auto& ubo : ubos) ubo.Destroy(Device());
    }
    m_models.clear();
    m_modelUniformBuffers.clear();

    m_clusterBuilder.Cleanup(Device());
    m_skybox.Cleanup(Device());
    m_imgui.Cleanup(Device());

    m_resolvePipeline.Cleanup();
    m_tonemapPipeline.Cleanup();
    m_debugPipeline.Cleanup();
    m_scenePipeline.Cleanup();
    m_shadowPipeline.Cleanup();
    m_hizPipeline.Cleanup();
    m_ssaoPipeline.Cleanup();
    m_ssrPipeline.Cleanup();

    m_targets.Cleanup();

    m_ibl.irradiance.Destroy(Device());
    m_ibl.prefilteredSpecular.Destroy(Device());
    m_ibl.brdfLUT.Destroy(Device());

    m_lightBuffer.Destroy(Device());
    m_lightCuller.Cleanup(Device());
    m_shadowMap.Cleanup(Device());
    m_rampBuffer.Destroy(Device());
    m_ssaoKernelBuffer.Destroy(Device());
    m_ssaoNoiseTex.Destroy(Device());

    if (m_hizSampler != VK_NULL_HANDLE) {
        vkDestroySampler(Device(), m_hizSampler, nullptr);
        m_hizSampler = VK_NULL_HANDLE;
    }
    if (m_ssaoNoiseSampler != VK_NULL_HANDLE) {
        vkDestroySampler(Device(), m_ssaoNoiseSampler, nullptr);
        m_ssaoNoiseSampler = VK_NULL_HANDLE;
    }

    m_swapchain.Cleanup();
}

// Per-frame
uint32_t Renderer::AcquireNextImage()
{
    return m_context.GetQueue()->AcquireNextImage();
}

int Renderer::CurrentFrameInFlight()
{
    return m_context.GetQueue()->GetCurrentFrame();
}

void Renderer::SubmitAndPresent(uint32_t imageIndex)
{
    m_context.GetQueue()->SubmitAsync(m_commandBuffers[imageIndex], imageIndex);
    m_context.GetQueue()->Present(imageIndex);
}

float Renderer::MeasureLuminance(int frameInFlight) const
{
    const uint16_t* px = reinterpret_cast<const uint16_t*>(
        m_targets.GetLumStagingMapped(frameInFlight));
    return Luminance(HalfToFloat(px[0]), HalfToFloat(px[1]), HalfToFloat(px[2]));
}

void Renderer::CullAndUploadLights(const Scene& scene, const glm::mat4& view)
{
    m_lightCuller.CullLights(m_context, view, MAX_LIGHTS);

    // Pad unused slots with inert lights so the culler always sees a full array.
    std::vector<GPULight> lights(MAX_LIGHTS);
    for (int i = 0; i < MAX_LIGHTS; i++) {
        if (i < static_cast<int>(scene.lights.size())) {
            const SceneLight& l = scene.lights[i];
            lights[i].posAndRadius = glm::vec4(l.position, l.radius);
            lights[i].colorAndIntensity = glm::vec4(l.color, l.intensity);
        }
        else {
            lights[i].posAndRadius = glm::vec4(0.0f, 0.0f, 0.0f, 0.001f);
            lights[i].colorAndIntensity = glm::vec4(0.0f);
        }
    }

    UploadToBuffer(Device(), m_lightBuffer, lights.data(), sizeof(GPULight) * MAX_LIGHTS);
}

void Renderer::UploadPenumbraRamps(const Scene& scene)
{
    std::vector<glm::vec4> ramp(MAX_RAMP_OBJECTS * RAMP_RESOLUTION, glm::vec4(1.0f));

    const int count = std::min<int>(MAX_RAMP_OBJECTS, static_cast<int>(scene.objects.size()));
    for (int obj = 0; obj < count; obj++) {
        for (int s = 0; s < RAMP_RESOLUTION; s++) {
            const float t = float(s) / float(RAMP_RESOLUTION - 1);
            ramp[obj * RAMP_RESOLUTION + s] =
                glm::vec4(EvaluateRamp(scene.objects[obj].penumbraStops, t), 1.0f);
        }
    }

    UploadToBuffer(Device(), m_rampBuffer, ramp.data(), sizeof(glm::vec4) * ramp.size());
}

void Renderer::UpdatePerObjectUniforms(const Scene& scene, const Camera& camera,
    uint32_t imageIndex, const glm::mat4& lightViewProj,
    const TemporalState& temporal)
{
    for (size_t i = 0; i < scene.objects.size() && i < m_modelUniformBuffers.size(); i++) {
        const SceneObject& o = scene.objects[i];

        UniformBufferObject ubo{};
        ubo.model = o.transform;
        ubo.view = camera.GetViewMatrix();
        ubo.proj = camera.GetProjectionMatrix();
        ubo.cameraPos = glm::vec4(camera.GetPosition(), 0.0f);
        ubo.lightViewProj = lightViewProj;
        ubo.penumbraParams = glm::vec4(o.penumbraWeight, float(i),
            o.penumbraBands, o.penumbraPatternStrength);
        ubo.penumbraPattern = glm::vec4(float(o.penumbraPattern), o.penumbraPatternScale, 0.0f, 0.0f);
        ubo.prevModel = temporal.prevModel[i];
        ubo.prevViewProj = temporal.prevViewProj;
        ubo.projNoJitter = camera.GetProjectionMatrixNoJitter();

        UploadToBuffer(Device(), m_modelUniformBuffers[i][imageIndex], &ubo, sizeof(ubo));
    }
}

void Renderer::UpdateSkybox(uint32_t imageIndex, const Camera& camera)
{
    m_skybox.Update(imageIndex,
        camera.GetProjectionMatrix() * camera.GetViewMatrixNoTranslate());
}
