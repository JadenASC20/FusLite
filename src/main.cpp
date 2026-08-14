#include <VulkanContext.h>
#include <Swapchain.h>
#include <VulkanQueue.h>
#include <RenderPass.h>
#include <GraphicsPipeline.h>
#include <TonemapPipeline.h>
#include <ResolvePipeline.h>
#include <Skybox.h>
#include <ImGuiManager.h>
#include <ShaderModule.h>
#include <ComputeTest.h>
#include <LightCuller.h>
#include <Vertex.h>
#include <Buffer.h>
#include <UniformBufferObject.h>
#include <VulkanTexture.h>
#include <Model.h>
#include <glm/gtc/matrix_transform.hpp>
#include <Camera.h>
#include <chrono>
#include <RenderParams.h>
#include <glm/gtc/type_ptr.hpp>
#include <ClusterBuilder.h>
#include <ClusterConfig.h>
#include <SceneTypes.h>
#include <ColorTemperature.h>
#include <ShadowMap.h>
#include <ShadowPipeline.h>
#include <RampUtil.h>
#include <Halton.h>
#include <imgui.h>
#include <ImGuizmo.h>
#include <DebugViewPipeline.h>
#include <SSRPipeline.h>
#include <HiZPipeline.h>


#define GLFW_INCLUDE_NONE

#include <GLFW/glfw3.h>

#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

enum class SelectionType { None, Object, Light };
static std::vector<SceneObject> g_sceneObjects;
static std::vector<SceneLight> g_sceneLights;
static SelectionType g_selectionType = SelectionType::None;
static int g_selectedIndex = -1;
static float g_sunKelvin = 5500.0f;
static glm::vec3 g_sunDirection = { -0.5f, 1.0f, -0.3f };
static float g_sunIntensity = 3.0f;
static float g_lightSize = 0.05f;
static Camera* g_camera = nullptr;
static bool g_showGui = true;
static bool g_taaJitterEnabled = true;
static bool g_taaResolveEnabled = true;
static float g_taaBlendAlpha = 0.1f;          // TAA: 1.0 = passthrough, 0.1 = normal accumulation
static int g_frameIndex = 0;
constexpr int TAA_JITTER_PHASES = 8;
static std::vector<glm::mat4> g_prevModelMatrices;
static glm::mat4 g_prevViewProj = glm::mat4(1.0f);

static int tonemapMode = TM_ACES;
static float g_exposureEV = 0.0f;
static float g_exposure = 1.0f;      // exp2(EV)

static float g_measuredLuminance = 0.0f;

const char* tonemapNames[] = { "Reinhard", "Reinhard Extended", "ACES", "AgX", "AgX Punchy", "GT7" };

static int g_debugView = 0;  // 0=Off, 1=HDR, 2=Motion, 3=Normal, 4=Depth

static bool  g_autoExposureEnabled = true;
static float g_keyValue = 0.18f;   // middle-grey target

// auto exposure clamp
static float g_minExposure = 0.25f;
static float g_maxExposure = 4.0f;   

static float g_currentExposure = 1.0f;      // the smoothed, displayed exposure
static float g_adaptSpeedUp = 3.0f;       // fast = adapting to a brighter scene
static float g_adaptSpeedDown = 1.0f;       // slow = adapting to a darker scene

static bool  g_ssrEnabled = true;
static float g_ssrReflectivity = 0.6f;
static int   g_ssrMaxSteps = 64;
static float g_ssrStepSize = 0.25f;
static float g_ssrThickness = 0.5f;

const char* debugViewNames[] = { "Off (normal render)", "HDR", "Motion", "Normal", "Depth", "SSR" };

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
        g_showGui = !g_showGui;
    }

    if (g_camera) g_camera->OnKey(key, action);
}
void MouseMoveCallback(GLFWwindow* window, double x, double y)
{
    if (g_camera) g_camera->OnMouseMove(x, y);
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    if (!ImGuiManager::IsMouseControlledByImGui() && g_camera) {
        g_camera->OnMouseButton(button, action);
    }
}

void TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
    VkImageAspectFlags aspect, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
    VkAccessFlags srcAccess, VkAccessFlags dstAccess)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { aspect, 0, 1, 0, 1 };
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}


// HDR Measurement Helpers
static float HalfToFloat(uint16_t h) {
    uint32_t sign = (h & 0x8000u) << 16;
    uint32_t exp = (h & 0x7C00u) >> 10;
    uint32_t mant = h & 0x03FFu;
    uint32_t f;
    if (exp == 0) {
        if (mant == 0) f = sign;
        else { // subnormal
            exp = 127 - 15 + 1;
            while ((mant & 0x0400u) == 0) { mant <<= 1; exp--; }
            mant &= 0x03FFu;
            f = sign | (exp << 23) | (mant << 13);
        }
    }
    else if (exp == 0x1F) {
        f = sign | 0x7F800000u | (mant << 13);
    }
    else {
        f = sign | ((exp - 15 + 127) << 23) | (mant << 13);
    }
    float out; memcpy(&out, &f, 4); return out;
}
static float dot_luma(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

void RecordFrame(VkCommandBuffer cmd, uint32_t imageIndex, const Swapchain& swapchain,
    const RenderPass& renderResources, const GraphicsPipeline& pipeline, const TonemapPipeline& tonemapPipeline,
    const ResolvePipeline& resolvePipeline, int histRead, int histWrite, bool firstFrame,
    const ShadowMap& shadowMap, const ShadowPipeline& shadowPipeline, const glm::mat4& lightViewProj,
    const Skybox& skybox, const std::vector<Model>& showcaseSpheres,
    const std::vector<SceneObject>& sceneObjects,
    ImGuiManager& imguiManager, bool showGui, RenderParams params, int tonemapMode, float exposure,
    int frameInFlight, 
    DebugViewPipeline& debugPipeline, int debugMode,
    const SSRPipeline& ssrPipeline, const Camera& camera,
    bool ssrEnabled, float ssrReflectivity, int ssrMaxSteps, float ssrStepSize, float ssrThickness, 
    const HiZPipeline& hizPipeline)
{

    VkExtent2D extent = swapchain.GetExtent();
    VkImage colorImage = swapchain.GetImages()[imageIndex];
    VkImageView colorView = swapchain.GetImageViews()[imageIndex];
    
    VkImageView depthView = renderResources.GetDepthImageViews()[imageIndex];
    VkImage depthImage = renderResources.GetDepthImages()[imageIndex];
    
    VkImage hdrImage = renderResources.GetHdrImages()[imageIndex];
    VkImageView hdrView = renderResources.GetHdrImageViews()[imageIndex];
    
    VkImage motionImage = renderResources.GetMotionImages()[imageIndex];
    VkImageView motionView = renderResources.GetMotionImageViews()[imageIndex];
    
    VkImage normalImage = renderResources.GetNormalImages()[imageIndex];
    VkImageView normalView = renderResources.GetNormalImageViews()[imageIndex];

    VkImage historyWriteImage = renderResources.GetHistoryImages()[histWrite];
    VkImageView historyWriteView = renderResources.GetHistoryImageViews()[histWrite];

    VkImage ssrImage = renderResources.GetSSRImages()[imageIndex];
    VkImageView ssrView = renderResources.GetSSRImageViews()[imageIndex];

    VkImage compositeImage = renderResources.GetCompositeImages()[imageIndex];
    VkImageView compositeView = renderResources.GetCompositeImageViews()[imageIndex];

    VkImageView debugView = VK_NULL_HANDLE;
    switch (debugMode) {
        case 1: debugView = hdrView; break;                                        // already SHADER_READ after resolve
        case 2: debugView = renderResources.GetMotionImageViews()[imageIndex]; break;
        case 3: debugView = normalView; break;
        case 4: debugView = depthView; break;   // needs the depth->read barrier
        case 5: debugView = ssrView; break;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Pass 0: Shadow map (depth-only, from the light's POV)
    TransitionImage(cmd, shadowMap.GetImage(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo shadowDepthAttachment{};
    shadowDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    shadowDepthAttachment.imageView = shadowMap.GetImageView();
    shadowDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    shadowDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    shadowDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    shadowDepthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo shadowRenderingInfo{};
    shadowRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    shadowRenderingInfo.renderArea = { {0, 0}, { shadowMap.GetResolution(), shadowMap.GetResolution() } };
    shadowRenderingInfo.layerCount = 1;
    shadowRenderingInfo.colorAttachmentCount = 0;
    shadowRenderingInfo.pDepthAttachment = &shadowDepthAttachment;
    vkCmdBeginRendering(cmd, &shadowRenderingInfo);
    shadowPipeline.Bind(cmd);
    for (size_t i = 0; i < showcaseSpheres.size(); i++) {
        ShadowPushConstants spc{};
        spc.lightViewProj = lightViewProj;
        spc.model = sceneObjects[i].transform;
        shadowPipeline.PushConstants(cmd, spc);
        showcaseSpheres[i].DrawGeometryOnly(cmd);
    }

    vkCmdEndRendering(cmd);

    // Transition shadow map for shader reading in the main pass
    TransitionImage(cmd, shadowMap.GetImage(),
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    // Pass 1: render scene + skybox into the HDR offscreen target
    TransitionImage(cmd, hdrImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

    TransitionImage(cmd, motionImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

    TransitionImage(cmd, normalImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

    TransitionImage(cmd, depthImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo hdrColorAttachment{};
    hdrColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    hdrColorAttachment.imageView = hdrView;
    hdrColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    hdrColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    hdrColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    hdrColorAttachment.clearValue.color = { 0.1f, 0.1f, 0.2f, 1.0f };

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depthView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingAttachmentInfo motionAttachment{};
    motionAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    motionAttachment.imageView = renderResources.GetMotionImageViews()[imageIndex];
    motionAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    motionAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    motionAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    motionAttachment.clearValue.color = { 0.0f, 0.0f, 0.0f, 0.0f };

    VkRenderingAttachmentInfo normalAttachment{};
    normalAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    normalAttachment.imageView = normalView;
    normalAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    normalAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    normalAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    normalAttachment.clearValue.color = { 0.5f, 0.5f, 1.0f, 1.0f };  // encodes N=(0,0,1)

    VkRenderingAttachmentInfo sceneAttachments[3] = { hdrColorAttachment, motionAttachment, normalAttachment };
    VkRenderingInfo sceneRenderingInfo{};
    sceneRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    sceneRenderingInfo.renderArea = { {0, 0}, extent };
    sceneRenderingInfo.layerCount = 1;
    sceneRenderingInfo.colorAttachmentCount = 3;
    sceneRenderingInfo.pColorAttachments = sceneAttachments;
    sceneRenderingInfo.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(cmd, &sceneRenderingInfo);

    skybox.Draw(cmd, imageIndex);
    pipeline.Bind(cmd);
    pipeline.PushParams(cmd, params);

    // Showcase spheres
    for (size_t i = 0; i < showcaseSpheres.size(); i++) {
        const SceneObject& obj = sceneObjects[i];
        params.colorTint = glm::vec4(obj.colorTint, 0.0f);
        params.roughness = obj.roughness;
        params.metallic = obj.metallic;
        params.clearcoatFactor = obj.clearcoatFactor;
        params.clearcoatRoughness = obj.clearcoatRoughness;
        params.flakeStrength = obj.flakeStrength;
        params.flakeScale = obj.flakeScale;
        pipeline.PushParams(cmd, params);
        showcaseSpheres[i].Draw(cmd, pipeline.GetLayout(), imageIndex);
    }

    vkCmdEndRendering(cmd);

    // SSR: scene outputs (HDR, depth, normal) -> shader read; march; write SSR target
    {
        VkViewport vpD{ 0,0,(float)extent.width,(float)extent.height,0,1 };
        VkRect2D scD{ {0,0}, extent };

        // Inputs -> shader read
        TransitionImage(cmd, hdrImage,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        TransitionImage(cmd, normalImage,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        TransitionImage(cmd, depthImage,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        hizPipeline.Build(cmd, imageIndex, renderResources.GetHiZImage(),
            renderResources.GetHiZBaseExtent(),
            renderResources.GetHiZMipLevels(),
            0.1f, 1000.0f);

        // SSR target -> color attachment
        TransitionImage(cmd, ssrImage,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

        VkRenderingAttachmentInfo ssrAtt{};
        ssrAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        ssrAtt.imageView = ssrView;
        ssrAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ssrAtt.loadOp = ssrEnabled ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_CLEAR;
        ssrAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ssrAtt.clearValue.color = { 0.0f, 0.0f, 0.0f, 0.0f };
        VkRenderingInfo ssrRI{};
        ssrRI.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ssrRI.renderArea = { {0,0}, extent };
        ssrRI.layerCount = 1; ssrRI.colorAttachmentCount = 1; ssrRI.pColorAttachments = &ssrAtt;

        vkCmdBeginRendering(cmd, &ssrRI);
        if (ssrEnabled) {
            vkCmdSetViewport(cmd, 0, 1, &vpD);
            vkCmdSetScissor(cmd, 0, 1, &scD);
            SSRPipeline::SSRPush sp{};
            glm::mat4 projNJ = camera.GetProjectionMatrixNoJitter();
            glm::mat4 viewM = camera.GetViewMatrix();
            sp.invProj = glm::inverse(projNJ);
            sp.view = viewM; sp.proj = projNJ;
            sp.screenSize = glm::vec2((float)extent.width, (float)extent.height);
            sp.nearZ = 0.1f; sp.farZ = 1000.0f;
            sp.maxSteps = ssrMaxSteps; 
            sp.stepSize = ssrStepSize; 
            sp.thickness = ssrThickness;
            sp.hizMipCount = (int)renderResources.GetHiZMipLevels();
            ssrPipeline.BindSSR(cmd, imageIndex, sp);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        }
        vkCmdEndRendering(cmd);

        // SSR target -> shader read
        TransitionImage(cmd, ssrImage,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        // Composite: HDR(read) + SSR(read) -> composite target
        TransitionImage(cmd, compositeImage,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        VkRenderingAttachmentInfo compAtt{};
        compAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        compAtt.imageView = compositeView;
        compAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        compAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        compAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo compRI{};
        compRI.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        compRI.renderArea = { {0,0}, extent };
        compRI.layerCount = 1; compRI.colorAttachmentCount = 1; compRI.pColorAttachments = &compAtt;

        vkCmdBeginRendering(cmd, &compRI);
        vkCmdSetViewport(cmd, 0, 1, &vpD);
        vkCmdSetScissor(cmd, 0, 1, &scD);
        SSRPipeline::CompPush cpc{};
        cpc.reflectivity = ssrReflectivity;
        ssrPipeline.BindComposite(cmd, imageIndex, cpc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);

        // Composite -> shader read (resolve reads it)
        TransitionImage(cmd, compositeImage,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }
    // HDR is now SHADER_READ; normal/depth are SHADER_READ. Resolve reads COMPOSITE.

    // Reads current HDR + motion + history[histRead], writes history[histWrite],
    // then copies the resolved result back into hdrImage so the (unchanged)
    // tonemap pass keeps reading HDR.
    // 
    // current HDR + motion: color attachment -> shader read (resolve samples them)

    TransitionImage(cmd, motionImage,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    // history[histWrite]: shader read (from last frame / init) -> color attachment
    TransitionImage(cmd, historyWriteImage,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

    // history[histRead] is already SHADER_READ_ONLY (last frame left it there / init) -> no barrier.
    VkRenderingAttachmentInfo resolveAttachment{};
    resolveAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    resolveAttachment.imageView = historyWriteView;
    resolveAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    resolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // overwrite every pixel
    resolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo resolveRenderingInfo{};
    resolveRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    resolveRenderingInfo.renderArea = { {0, 0}, extent };
    resolveRenderingInfo.layerCount = 1;
    resolveRenderingInfo.colorAttachmentCount = 1;
    resolveRenderingInfo.pColorAttachments = &resolveAttachment;
    vkCmdBeginRendering(cmd, &resolveRenderingInfo);

    ResolvePipeline::PushConstants rpc{};
    rpc.texelSize[0] = 1.0f / float(extent.width);
    rpc.texelSize[1] = 1.0f / float(extent.height);
    rpc.blendAlpha = (g_taaResolveEnabled ? g_taaBlendAlpha : 1.0f);
    rpc.firstFrame = firstFrame ? 1 : 0;
    resolvePipeline.Bind(cmd, imageIndex, histRead, rpc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);

    // Copy resolved (history[histWrite]) back into HDR so tonemap reads it unchanged.
    TransitionImage(cmd, historyWriteImage,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);

    TransitionImage(cmd, hdrImage,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

    VkImageCopy copy{};
    copy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copy.extent = { extent.width, extent.height, 1 };

    vkCmdCopyImage(cmd,
        historyWriteImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        hdrImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &copy);

    // history[histWrite]: transfer src -> shader read (becomes NEXT frame's histRead)
    TransitionImage(cmd, historyWriteImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);

    // hdr: transfer dst -> shader read (tonemap samples it, unchanged downstream)
    TransitionImage(cmd, hdrImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    // Auto-exposure: blit resolved HDR down to 1x1, then to staging (blocking readback)
    // hdrImage is currently SHADER_READ_ONLY (resolve left it there). Take it to TRANSFER_SRC.
    TransitionImage(cmd, hdrImage,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);

    VkImage lumImage = renderResources.GetLumImage();
    TransitionImage(cmd, lumImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, VK_ACCESS_TRANSFER_WRITE_BIT);

    VkImageBlit blit{};
    blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.srcOffsets[0] = { 0, 0, 0 };
    blit.srcOffsets[1] = { (int32_t)extent.width, (int32_t)extent.height, 1 };
    blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.dstOffsets[0] = { 0, 0, 0 };
    blit.dstOffsets[1] = { 1, 1, 1 };
    vkCmdBlitImage(cmd,
        hdrImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        lumImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blit, VK_FILTER_LINEAR);

    // lum 1x1 -> staging buffer
    TransitionImage(cmd, lumImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);

    VkBufferImageCopy toBuf{};
    toBuf.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    toBuf.imageExtent = { 1, 1, 1 };
    vkCmdCopyImageToBuffer(cmd, lumImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        renderResources.GetLumStagingBuffer(frameInFlight), 1, &toBuf);

    // Restore hdrImage to SHADER_READ_ONLY so the tonemap pass reads it unchanged.
    TransitionImage(cmd, hdrImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);

    if (debugMode == 3) {
        TransitionImage(cmd, normalImage,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    if (debugMode == 4) {
        TransitionImage(cmd, depthImage,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    // Pass 2: tonemap HDR -> swapchain
    TransitionImage(cmd, colorImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo swapchainAttachment{};
    swapchainAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    swapchainAttachment.imageView = colorView;
    swapchainAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swapchainAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    swapchainAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo tonemapRenderingInfo{};
    tonemapRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    tonemapRenderingInfo.renderArea = { {0, 0}, extent };
    tonemapRenderingInfo.layerCount = 1;
    tonemapRenderingInfo.colorAttachmentCount = 1;
    tonemapRenderingInfo.pColorAttachments = &swapchainAttachment;
    
    vkCmdBeginRendering(cmd, &tonemapRenderingInfo);
    if (debugMode == 0) {
        tonemapPipeline.Bind(cmd, imageIndex, tonemapMode, exposure);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    else {
        // Debug pass replaces tonemap. Dynamic viewport/scissor (pipeline declares them dynamic).
        VkViewport vpDyn{ 0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f };
        VkRect2D scDyn{ {0,0}, extent };
        vkCmdSetViewport(cmd, 0, 1, &vpDyn);
        vkCmdSetScissor(cmd, 0, 1, &scDyn);
        DebugViewPipeline::PushConstants dpc{};
        dpc.mode = debugMode;
        dpc.nearZ = 0.1f;   // matches your camera near/far
        dpc.farZ = 1000.0f;
        debugPipeline.Bind(cmd, debugView, dpc);
    }
    vkCmdEndRendering(cmd);

    // Pass 3: ImGui overlay, drawn directly onto the swapchain image

    if (showGui) {
        VkRenderingAttachmentInfo imguiAttachment{};
        imguiAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        imguiAttachment.imageView = colorView;
        imguiAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imguiAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // keep the tonemapped scene, don't clear
        imguiAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo imguiRenderingInfo{};
        imguiRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        imguiRenderingInfo.renderArea = { {0, 0}, extent };
        imguiRenderingInfo.layerCount = 1;
        imguiRenderingInfo.colorAttachmentCount = 1;
        imguiRenderingInfo.pColorAttachments = &imguiAttachment;
        vkCmdBeginRendering(cmd, &imguiRenderingInfo);

        imguiManager.RecordDrawCommands(cmd);
        vkCmdEndRendering(cmd);
    }

    TransitionImage(cmd, colorImage,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0);

    VkResult endRes = vkEndCommandBuffer(cmd);
    if (endRes != VK_SUCCESS) fprintf(stderr, ">>> vkEndCommandBuffer FAILED: %d\n", endRes);
}

int main() {
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "FusLite", nullptr, nullptr);
     
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwSetKeyCallback(window, KeyCallback);
    glfwSetCursorPosCallback(window, MouseMoveCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);

    try {
        VulkanContext context;
        context.Init("FusLite", window);

        Swapchain swapchain;
        swapchain.Init(context, WINDOW_WIDTH, WINDOW_HEIGHT);

        context.CreateQueue(swapchain.GetHandle(), static_cast<uint32_t>(swapchain.GetImages().size()));
        
        ClusterBuilder clusterBuilder;
        clusterBuilder.Init(context);

        glm::mat4 testProj = glm::perspective(glm::radians(45.0f),
            static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT), 0.1f, 1000.0f);
        glm::mat4 invProj = glm::inverse(testProj);
        clusterBuilder.BuildClusters(context, invProj,
            static_cast<float>(WINDOW_WIDTH), static_cast<float>(WINDOW_HEIGHT), 0.1f, 1000.0f);

        RenderPass renderPass;
        renderPass.Init(context, swapchain);

        std::vector<BufferAndMemory> uniformBuffers(swapchain.GetImages().size());
        BufferAndMemory lightBuffer = context.CreateStorageBuffer(sizeof(GPULight) * MAX_LIGHTS);
        
        LightCuller lightCuller;
        lightCuller.Init(context, clusterBuilder.GetClusterBuffer(), lightBuffer);
        
        Skybox skybox;
        skybox.Init(context, window, renderPass.GetHdrFormat(), renderPass.GetDepthFormat(),
            "assets/Skybox.hdr", static_cast<uint32_t>(uniformBuffers.size()),
            renderPass.GetMotionFormat(), renderPass.GetNormalFormat());
        VulkanContext::IBLTextures iblTextures = context.CreateIBLFromEquirect("assets/Skybox.hdr");
        
        VkShaderModule vertShader = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/triangle.vert.spv");
        VkShaderModule fragShader = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/triangle.frag.spv");
        
        GraphicsPipeline pipeline;
        uint32_t maxDescriptorSets = 32 * static_cast<uint32_t>(uniformBuffers.size());
        pipeline.Init(context, window, renderPass.GetHdrFormat(), renderPass.GetDepthFormat(),
            vertShader, fragShader, maxDescriptorSets, renderPass.GetMotionFormat(), renderPass.GetNormalFormat());
        vkDestroyShaderModule(context.GetDevice(), vertShader, nullptr);
        vkDestroyShaderModule(context.GetDevice(), fragShader, nullptr);
        
        ShadowMap shadowMap;
        shadowMap.Init(context, 2048);
        VkShaderModule shadowVert = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/shadow_depth.vert.spv");
        VkShaderModule shadowFrag = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/shadow_depth.frag.spv");
        ShadowPipeline shadowPipeline;
        shadowPipeline.Init(context, shadowMap.GetFormat(), shadowMap.GetResolution(), shadowVert, shadowFrag);
        
        vkDestroyShaderModule(context.GetDevice(), shadowVert, nullptr);
        vkDestroyShaderModule(context.GetDevice(), shadowFrag, nullptr);
        
        BufferAndMemory rampBuffer = context.CreateStorageBuffer(
            sizeof(glm::vec4) * MAX_RAMP_OBJECTS * RAMP_RESOLUTION);
        
        const std::vector<std::string> modelPaths = {
            "assets/ShaderBall.obj",
            "assets/ShaderBall.obj",
            "assets/ShaderBall.obj",
            "assets/ShaderBall.obj",
            "assets/ShaderBall.obj",
            "assets/floor.obj"
        };
        
        const int NUM_SCENE_MODELS = static_cast<int>(modelPaths.size());
        
        std::vector<Model> showcaseSpheres(NUM_SCENE_MODELS);
        std::vector<std::vector<BufferAndMemory>> showcaseUniformBuffers(NUM_SCENE_MODELS);
        
        for (int i = 0; i < NUM_SCENE_MODELS; i++) {
            showcaseSpheres[i].LoadFromFile(context, modelPaths[i]);
            showcaseUniformBuffers[i].resize(swapchain.GetImages().size());
            
            for (auto& ubo : showcaseUniformBuffers[i]) {
                ubo = context.CreateUniformBuffer(sizeof(UniformBufferObject));
            }
            showcaseSpheres[i].CreateDescriptorSets(pipeline, showcaseUniformBuffers[i], sizeof(UniformBufferObject),
                iblTextures, lightBuffer, lightCuller.GetClusterLightInfoBuffer(), lightCuller.GetLightIndexBuffer(),
                shadowMap.GetImageView(), shadowMap.GetSampler(), rampBuffer);
        }
        
        VkShaderModule fullscreenVert = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/fullscreen.vert.spv");
        VkShaderModule tonemapFrag = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/tonemap.frag.spv");
        TonemapPipeline tonemapPipeline;
        tonemapPipeline.Init(context, window, swapchain.GetImageFormat(),
            fullscreenVert, tonemapFrag, renderPass.GetHdrImageViews());
        vkDestroyShaderModule(context.GetDevice(), fullscreenVert, nullptr);
        vkDestroyShaderModule(context.GetDevice(), tonemapFrag, nullptr);
        
        // Debug view pipeline
        VkShaderModule debugVert = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/fullscreen.vert.spv");
        VkShaderModule debugFrag = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/debug_view.frag.spv");
        DebugViewPipeline debugPipeline;
        debugPipeline.Init(context, swapchain.GetImageFormat(), debugVert, debugFrag);
        vkDestroyShaderModule(context.GetDevice(), debugVert, nullptr);
        vkDestroyShaderModule(context.GetDevice(), debugFrag, nullptr);

        VkShaderModule ssrVert = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/fullscreen.vert.spv");
        VkShaderModule ssrFrag = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/ssr.frag.spv");
        VkShaderModule compFrag = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/composite.frag.spv");
        SSRPipeline ssrPipeline;
        printf("Passing Hi-Z view to SSR: %p\n", (void*)renderPass.GetHiZSampleView());
        ssrPipeline.Init(context, renderPass.GetSSRFormat(), renderPass.GetHdrFormat(),
            ssrVert, ssrFrag, compFrag,
            renderPass.GetHdrImageViews(), renderPass.GetDepthImageViews(),
            renderPass.GetNormalImageViews(), renderPass.GetSSRImageViews(), 
            renderPass.GetHiZSampleView());
        vkDestroyShaderModule(context.GetDevice(), ssrVert, nullptr);
        vkDestroyShaderModule(context.GetDevice(), ssrFrag, nullptr);
        vkDestroyShaderModule(context.GetDevice(), compFrag, nullptr);

        // TAA resolve pipeline
        VkShaderModule resolveVert = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/fullscreen.vert.spv");
        VkShaderModule resolveFrag = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/taa_resolve.frag.spv");
        ResolvePipeline resolvePipeline;
        resolvePipeline.Init(context, window, renderPass.GetHdrFormat(),
            resolveVert, resolveFrag,
            renderPass.GetCompositeImageViews(), renderPass.GetMotionImageViews(),
            renderPass.GetHistoryImageViews());
        vkDestroyShaderModule(context.GetDevice(), resolveVert, nullptr);
        vkDestroyShaderModule(context.GetDevice(), resolveFrag, nullptr);

        // Nearest-clamp sampler for Hi-Z (depth + mip sampling; nearest = no depth interpolation)
        VkSampler hizSampler;
        {
            VkSamplerCreateInfo s{};
            s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            s.magFilter = VK_FILTER_NEAREST; s.minFilter = VK_FILTER_NEAREST;
            s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            s.minLod = 0.0f; 
            s.maxLod = VK_LOD_CLAMP_NONE;
            if (vkCreateSampler(context.GetDevice(), &s, nullptr, &hizSampler) != VK_SUCCESS)
                throw std::runtime_error("Failed to create Hi-Z sampler");
        }

        VkShaderModule hizComp = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/hiz_build.comp.spv");
        HiZPipeline hizPipeline;
        hizPipeline.Init(context, hizComp, hizSampler,
            renderPass.GetDepthImageViews(), renderPass.GetHiZMipViews(),
            renderPass.GetHiZMipLevels());
        vkDestroyShaderModule(context.GetDevice(), hizComp, nullptr);

        // One-time: transition both history images UNDEFINED -> SHADER_READ so the
        // steady-state assumption (both start each frame in SHADER_READ) holds on frame 0.
        // Inlined one-shot command buffer, same pattern as VulkanContext::CopyBuffer.
        {
            VkCommandBufferAllocateInfo initAllocInfo{};
            initAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            initAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            initAllocInfo.commandPool = context.GetCommandPool();
            initAllocInfo.commandBufferCount = 1;
            VkCommandBuffer initCmd;
            vkAllocateCommandBuffers(context.GetDevice(), &initAllocInfo, &initCmd);

            VkCommandBufferBeginInfo initBeginInfo{};
            initBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            initBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(initCmd, &initBeginInfo);

            for (int i = 0; i < 2; i++) {
                TransitionImage(initCmd, renderPass.GetHistoryImages()[i],
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, VK_ACCESS_SHADER_READ_BIT);
            }

            vkEndCommandBuffer(initCmd);

            VkSubmitInfo initSubmit{};
            initSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            initSubmit.commandBufferCount = 1;
            initSubmit.pCommandBuffers = &initCmd;

            VkQueue gfxQueue;
            vkGetDeviceQueue(context.GetDevice(),
                context.GetSelectedDevice().queueFamilyIndices.graphicsFamily.value(), 0, &gfxQueue);
            vkQueueSubmit(gfxQueue, 1, &initSubmit, VK_NULL_HANDLE);
            vkQueueWaitIdle(gfxQueue);
            vkFreeCommandBuffers(context.GetDevice(), context.GetCommandPool(), 1, &initCmd);
        }

        ImGuiManager imguiManager;
        imguiManager.Init(context, window, swapchain.GetImageFormat(),
            static_cast<uint32_t>(swapchain.GetImages().size()));
        ImGuiIO& io = ImGui::GetIO();

        io.FontGlobalScale = 1.5f;

        std::vector<VkCommandBuffer> commandBuffers(swapchain.GetImageViews().size());
        context.CreateCommandBuffers(static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
       
        Camera camera(
            glm::vec3(0.0f, 1.0f, 5.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            45.0f,
            static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT),
            0.1f, 1000.0f
        );
        g_camera = &camera;

        auto startTime = std::chrono::high_resolution_clock::now();
        float lastFrameTime = 0.0f;
        static glm::mat4 modelMatrix = glm::mat4(1.0f);
        static RenderParams g_renderParams;
        
        g_renderParams.clusterGridAndScreen = glm::vec4(CLUSTER_GRID_X, CLUSTER_GRID_Y, CLUSTER_GRID_Z, 0.0f);
        g_renderParams.screenSize = glm::vec2(WINDOW_WIDTH, WINDOW_HEIGHT);
        g_renderParams.nearZ = 0.1f;
        g_renderParams.farZ = 1000.0f;
       
        float spacing = 2.5f;
        
        for (int i = 0; i < 5; i++) {
            SceneObject sphere;
            sphere.name = "pSphere" + std::string(1, 'A' + i);
            sphere.transform = glm::translate(glm::mat4(1.0f), glm::vec3((i - 2) * spacing, 0.0f, 0.0f));
            sphere.colorTint = glm::vec3(0.7f, 0.1f, 0.1f);
            sphere.clearcoatFactor = 0.2f + i * 0.15f;
            sphere.flakeStrength = 0.0f;
            g_sceneObjects.push_back(sphere);
        }
        
        SceneObject ground;
        ground.name = "pGroundPlane";
        ground.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f))
            * glm::scale(glm::mat4(1.0f), glm::vec3(10.0f));
        ground.colorTint = glm::vec3(0.55f, 0.55f, 0.55f);
        ground.clearcoatFactor = 0.0f;
        ground.flakeStrength = 0.0f;
        
        g_sceneObjects.push_back(ground);
        g_sceneLights.push_back({ "fPointLightA", { 2.0f, 1.5f, 2.0f }, { 1.0f, 0.0f, 0.0f }, 8.0f, 5.0f });
        g_sceneLights.push_back({ "fPointLightB", { -2.0f, 1.5f, 2.0f }, { 0.0f, 0.0f, 1.0f }, 8.0f, 5.0f });
        g_sceneLights.push_back({ "fPointLightC", { 0.0f, 2.5f, -2.0f }, { 0.0f, 1.0f, 0.0f }, 8.0f, 5.0f });
        
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            float currentTime = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();
            float deltaTime = currentTime - lastFrameTime;
            lastFrameTime = currentTime;
            camera.Update(deltaTime);

            // TAA history parity: snapshot BEFORE
            int histRead = g_frameIndex & 1;
            int histWrite = histRead ^ 1;
            bool firstFrame = (g_frameIndex == 0);
            int haltonIndex = (g_frameIndex % TAA_JITTER_PHASES) + 1;
            float hx = Halton(haltonIndex, 2) - 0.5f;
            float hy = Halton(haltonIndex, 3) - 0.5f;

            // Halton gives [0,1); recentre to [-0.5,0.5) then convert pixels to NDC units.
            float jitterX = g_taaJitterEnabled ? (hx * 2.0f / float(WINDOW_WIDTH)) : 0.0f;
            float jitterY = g_taaJitterEnabled ? (hy * 2.0f / float(WINDOW_HEIGHT)) : 0.0f;
            camera.SetJitter(jitterX, jitterY);
            g_frameIndex++;
            lightCuller.CullLights(context, camera.GetViewMatrix(), MAX_LIGHTS);

            // static int frameCount = 0;
            // frameCount++;
            // if (frameCount % 60 == 0) {
            //      printf("Frame %d — light culling complete.\n", frameCount);
            // }

            std::vector<GPULight> lights(MAX_LIGHTS);
            for (int i = 0; i < MAX_LIGHTS; i++) {
                if (i < static_cast<int>(g_sceneLights.size())) {
                    const SceneLight& light = g_sceneLights[i];
                    lights[i].posAndRadius = glm::vec4(light.position, light.radius);
                    lights[i].colorAndIntensity = glm::vec4(light.color, light.intensity);
                }

                else {
                    lights[i].posAndRadius = glm::vec4(0.0f, 0.0f, 0.0f, 0.001f); // tiny radius, effectively inert
                    lights[i].colorAndIntensity = glm::vec4(0.0f); // zero intensity — contributes nothing
                }
            }

            void* lightData;
            vkMapMemory(context.GetDevice(), lightBuffer.memory, 0, sizeof(GPULight) * MAX_LIGHTS, 0, &lightData);
            memcpy(lightData, lights.data(), sizeof(GPULight) * MAX_LIGHTS);
            vkUnmapMemory(context.GetDevice(), lightBuffer.memory);
            
            std::vector<glm::vec4> rampData(MAX_RAMP_OBJECTS * RAMP_RESOLUTION, glm::vec4(1.0f));
            
            for (int obj = 0; obj < MAX_RAMP_OBJECTS && obj < static_cast<int>(g_sceneObjects.size()); obj++) {
                for (int s = 0; s < RAMP_RESOLUTION; s++) {
                    float t = float(s) / float(RAMP_RESOLUTION - 1);
                    rampData[obj * RAMP_RESOLUTION + s] =
                        glm::vec4(EvaluateRamp(g_sceneObjects[obj].penumbraStops, t), 1.0f);
                }
            }

            void* rampPtr;
            vkMapMemory(context.GetDevice(), rampBuffer.memory, 0,
                sizeof(glm::vec4) * rampData.size(), 0, &rampPtr);
            memcpy(rampPtr, rampData.data(), sizeof(glm::vec4) * rampData.size());
            vkUnmapMemory(context.GetDevice(), rampBuffer.memory);
            
            glm::mat4 lightViewProj = ShadowMap::ComputeLightViewProj(g_sunDirection, 15.0f, 0.1f, 100.0f);
            uint32_t imageIndex = context.GetQueue()->AcquireNextImage();
            int frameInFlight = context.GetQueue()->GetCurrentFrame();

            const uint16_t* px = reinterpret_cast<const uint16_t*>(
                renderPass.GetLumStagingMapped(frameInFlight));
            float r = HalfToFloat(px[0]);
            float g = HalfToFloat(px[1]);
            float b = HalfToFloat(px[2]);
            g_measuredLuminance = dot_luma(r, g, b);

            for (size_t i = 0; i < g_sceneObjects.size(); i++) {

                const SceneObject& o = g_sceneObjects[i];
                UniformBufferObject objUbo{};
                objUbo.model = o.transform;
                objUbo.view = camera.GetViewMatrix();
                objUbo.proj = camera.GetProjectionMatrix();
                objUbo.cameraPos = glm::vec4(camera.GetPosition(), 0.0f);
                objUbo.lightViewProj = lightViewProj;
                objUbo.penumbraParams = glm::vec4(o.penumbraWeight, float(i), o.penumbraBands, o.penumbraPatternStrength);
                objUbo.penumbraPattern = glm::vec4(float(o.penumbraPattern), o.penumbraPatternScale, 0.0f, 0.0f);
                if (g_prevModelMatrices.size() != g_sceneObjects.size()) {
                    g_prevModelMatrices.assign(g_sceneObjects.size(), glm::mat4(1.0f));
                }
                objUbo.prevModel = g_prevModelMatrices[i];
                objUbo.prevViewProj = g_prevViewProj;
                objUbo.projNoJitter = camera.GetProjectionMatrixNoJitter();
                void* objData;
                vkMapMemory(context.GetDevice(), showcaseUniformBuffers[i][imageIndex].memory, 0, sizeof(objUbo), 0, &objData);
                memcpy(objData, &objUbo, sizeof(objUbo));
                vkUnmapMemory(context.GetDevice(), showcaseUniformBuffers[i][imageIndex].memory);
            }
            
            glm::mat4 vpNoTranslate = camera.GetProjectionMatrix() * camera.GetViewMatrixNoTranslate();
            skybox.Update(imageIndex, vpNoTranslate);
            glm::mat4 gizmoProj = glm::perspective(
                glm::radians(45.0f),
                static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT),
                0.1f, 1000.0f
            );

            g_renderParams.lightDirAndIntensity = glm::vec4(g_sunDirection, g_sunIntensity);
            g_renderParams.sunColor = glm::vec4(KelvinToRGB(g_sunKelvin), 0.0f);
            g_renderParams.lightSize = g_lightSize;
            
            if (g_showGui) {
                imguiManager.BeginFrame();
                ImGuizmo::SetOrthographic(false);
                ImGuizmo::BeginFrame();
                ImGuizmo::SetRect(0, 0, static_cast<float>(WINDOW_WIDTH), static_cast<float>(WINDOW_HEIGHT));
                glm::mat4 view = camera.GetViewMatrix();
                if (g_selectionType == SelectionType::Object && g_selectedIndex >= 0 && g_selectedIndex < static_cast<int>(g_sceneObjects.size())) {
                    ImGuizmo::Manipulate(
                        glm::value_ptr(view),
                        glm::value_ptr(gizmoProj),
                        ImGuizmo::TRANSLATE,
                        ImGuizmo::WORLD,
                        glm::value_ptr(g_sceneObjects[g_selectedIndex].transform)
                    );
                }

                // Window 1: Debug
                ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSizeConstraints(ImVec2(300, 0), ImVec2(FLT_MAX, FLT_MAX));
                ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
                {
                    ImGui::Text("Frame Time: %.2fms/frame (%.0f FPS)", 1000.0f / io.Framerate, io.Framerate);
                    static float frameTimes[90] = {};
                    static int frameTimeOffset = 0;
                    frameTimes[frameTimeOffset] = deltaTime * 1000.0f;
                    frameTimeOffset = (frameTimeOffset + 1) % IM_ARRAYSIZE(frameTimes);
                    ImGui::PlotLines("Frame Time Graph", frameTimes, IM_ARRAYSIZE(frameTimes), frameTimeOffset,
                        nullptr, 0.0f, 33.0f, ImVec2(0, 60));
                    
                    ImGui::Separator();
                    ImGui::Text("Lighting (Sun)");
                    ImGui::SliderFloat3("Light Dir", &g_sunDirection.x, -1.0f, 1.0f);
                    ImGui::SliderFloat("Light Intensity", &g_sunIntensity, 0.0f, 10.0f);
                    ImGui::SliderFloat("Light Size", &g_lightSize, 0.001f, 0.2f, "%.3f");
                    glm::vec3 previewColor = KelvinToRGB(g_sunKelvin);
                    ImGui::ColorButton("##sunPreview", ImVec4(previewColor.r, previewColor.g, previewColor.b, 1.0f),
                        0, ImVec2(40, 25));
                    ImGui::SameLine();
                    ImGui::SliderFloat("Light Color (K)", &g_sunKelvin, 1000.0f, 12000.0f, "%.0f K");
                    
                    ImGui::Separator();
                    ImGui::Text("TAA");
                    ImGui::Checkbox("Jitter enabled", &g_taaJitterEnabled);
                    ImGui::Checkbox("Resolve enabled", &g_taaResolveEnabled);
                    ImGui::SliderFloat("Blend alpha", &g_taaBlendAlpha, 0.02f, 1.0f, "%.3f");
                    ImGui::Text("Jitter phase: %d/%d", haltonIndex, TAA_JITTER_PHASES);
                    ImGui::Combo("Tonemap", &tonemapMode, tonemapNames, IM_ARRAYSIZE(tonemapNames));
                    
                    ImGui::Separator();
                    ImGui::Text("Auto-Exposure");
                    ImGui::Text("Measured avg luminance: %.4f", g_measuredLuminance);
                    ImGui::Text("Auto exposure value: %.3f", g_exposure);   // debug: watch it
                    ImGui::Checkbox("Auto exposure", &g_autoExposureEnabled);
                    if (g_autoExposureEnabled) {
                        ImGui::SliderFloat("Key value", &g_keyValue, 0.05f, 0.5f, "%.3f");
                        ImGui::SliderFloat("Min exposure", &g_minExposure, 0.05f, 1.0f, "%.2f");
                        ImGui::SliderFloat("Max exposure", &g_maxExposure, 1.0f, 16.0f, "%.2f");
                        ImGui::SliderFloat("Adapt speed (bright)", &g_adaptSpeedUp, 0.5f, 8.0f, "%.2f");
                        ImGui::SliderFloat("Adapt speed (dark)", &g_adaptSpeedDown, 0.2f, 8.0f, "%.2f");
                    }
                    else {
                        ImGui::SliderFloat("Exposure (EV)", &g_exposureEV, -4.0f, 4.0f);
                        g_exposure = exp2(g_exposureEV);
                    }

                    ImGui::Combo("Debug View", &g_debugView, debugViewNames, IM_ARRAYSIZE(debugViewNames));
                
                    ImGui::Separator();
                    ImGui::Text("SSR (CP1 mirror)");
                    ImGui::Checkbox("SSR enabled", &g_ssrEnabled);
                    ImGui::SliderFloat("Reflectivity", &g_ssrReflectivity, 0.0f, 1.0f);
                    ImGui::SliderInt("Max steps", &g_ssrMaxSteps, 8, 256);
                    ImGui::SliderFloat("Step size", &g_ssrStepSize, 0.02f, 1.0f, "%.3f");
                    ImGui::SliderFloat("Thickness", &g_ssrThickness, 0.05f, 2.0f, "%.3f");
                }
                ImGui::End();

                //Window 2: Property Window

                ImGui::SetNextWindowPos(ImVec2(20, 420), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSizeConstraints(ImVec2(300, 0), ImVec2(FLT_MAX, FLT_MAX));
                ImGui::Begin("Property Window", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
                {
                    if (g_selectionType == SelectionType::Object && g_selectedIndex >= 0 && g_selectedIndex < static_cast<int>(g_sceneObjects.size())) {
                        SceneObject& obj = g_sceneObjects[g_selectedIndex];
                        ImGui::Text("Object: %s", obj.name.c_str());
                        
                        ImGui::Separator();
                        ImGui::ColorEdit3("Color", &obj.colorTint.x);
                        ImGui::SliderFloat("Roughness", &obj.roughness, 0.0f, 1.0f);
                        ImGui::SliderFloat("Metallic", &obj.metallic, 0.0f, 1.0f);
                        ImGui::SliderFloat("Clearcoat Factor", &obj.clearcoatFactor, 0.0f, 1.0f);
                        ImGui::SliderFloat("Clearcoat Roughness", &obj.clearcoatRoughness, 0.01f, 0.5f);
                        ImGui::SliderFloat("Flake Strength", &obj.flakeStrength, 0.0f, 0.3f);
                        ImGui::SliderFloat("Flake Scale", &obj.flakeScale, 50.0f, 1000.0f);
                        
                        ImGui::Separator();
                        ImGui::Text("Shadow Penumbra");
                        ImVec2 p = ImGui::GetCursorScreenPos();
                        float w = ImGui::GetContentRegionAvail().x, h = 22.0f;
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        for (int s = 0; s < 48; s++) {
                            glm::vec3 c = EvaluateRamp(obj.penumbraStops, float(s) / 47.0f);
                            dl->AddRectFilled(ImVec2(p.x + w * s / 48.0f, p.y),
                                ImVec2(p.x + w * (s + 1) / 48.0f, p.y + h),
                                IM_COL32(int(c.r * 255), int(c.g * 255), int(c.b * 255), 255));
                        }
                        ImGui::Dummy(ImVec2(w, h + 4));
                        for (int s = 0; s < static_cast<int>(obj.penumbraStops.size()); s++) {
                            ImGui::PushID(s);
                            ImGui::ColorEdit3("##col", &obj.penumbraStops[s].color.x, ImGuiColorEditFlags_NoInputs);
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(120);
                            ImGui::SliderFloat("##pos", &obj.penumbraStops[s].position, 0.0f, 1.0f, "%.2f");
                            if (obj.penumbraStops.size() > 2) {
                                ImGui::SameLine();
                                if (ImGui::SmallButton("x")) {
                                    obj.penumbraStops.erase(obj.penumbraStops.begin() + s);
                                    ImGui::PopID();
                                    break;
                                }
                            }
                            ImGui::PopID();
                        }
                        if (static_cast<int>(obj.penumbraStops.size()) < MAX_RAMP_STOPS && ImGui::Button("Add stop")) {
                            obj.penumbraStops.push_back({ glm::vec3(1.0f), 0.5f });
                        }
                        ImGui::SliderFloat("Layer Weight", &obj.penumbraWeight, 0.0f, 1.0f);
                        ImGui::SliderFloat("Ramp Bands", &obj.penumbraBands, 0.0f, 12.0f, "%.0f");
                        ImGui::Combo("Pattern", &obj.penumbraPattern, "None\0Noise\0Hatch\0Halftone\0");
                        ImGui::SliderFloat("Pattern Scale", &obj.penumbraPatternScale, 1.0f, 200.0f);
                        ImGui::SliderFloat("Pattern Strength", &obj.penumbraPatternStrength, 0.0f, 0.5f);
                    }
                    else if (g_selectionType == SelectionType::Light && g_selectedIndex >= 0 && g_selectedIndex < static_cast<int>(g_sceneLights.size())) {
                        SceneLight& light = g_sceneLights[g_selectedIndex];
                        ImGui::Text("Light: %s", light.name.c_str());
                        ImGui::Separator();
                        ImGui::SliderFloat3("Position", &light.position.x, -10.0f, 10.0f);
                        ImGui::ColorEdit3("Color", &light.color.x);
                        ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 20.0f);
                        ImGui::SliderFloat("Radius", &light.radius, 0.5f, 20.0f);
                    }
                    else {
                        ImGui::TextDisabled("Nothing selected.");
                    }
                }
                ImGui::End();

                //Window 3: Scene Outliner
                ImGui::SetNextWindowPos(ImVec2(1580, 20), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSizeConstraints(ImVec2(300, 0), ImVec2(FLT_MAX, FLT_MAX));
                ImGui::Begin("Scene Outliner", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
                {
                    ImGui::Text("Scene Objects:");
                    for (int i = 0; i < static_cast<int>(g_sceneObjects.size()); i++) {
                        bool isSelected = (g_selectionType == SelectionType::Object && g_selectedIndex == i);
                        if (ImGui::Selectable(g_sceneObjects[i].name.c_str(), isSelected)) {
                            g_selectionType = SelectionType::Object;
                            g_selectedIndex = i;
                        }
                    }
                    
                    ImGui::Separator();
                    ImGui::Text("Scene Lights:");
                    for (int i = 0; i < static_cast<int>(g_sceneLights.size()); i++) {
                        bool isSelected = (g_selectionType == SelectionType::Light && g_selectedIndex == i);
                        if (ImGui::Selectable(g_sceneLights[i].name.c_str(), isSelected)) {
                            g_selectionType = SelectionType::Light;
                            g_selectedIndex = i;
                        }
                    }
                }

                ImGui::End();
                imguiManager.EndFrame();
            }

            RecordFrame(commandBuffers[imageIndex], imageIndex, swapchain, renderPass,
                pipeline, tonemapPipeline, resolvePipeline, histRead, histWrite, firstFrame,
                shadowMap, shadowPipeline, lightViewProj, skybox, showcaseSpheres, g_sceneObjects,
                imguiManager, g_showGui, g_renderParams, tonemapMode, g_exposure, frameInFlight,
                debugPipeline, g_debugView, ssrPipeline, camera, g_ssrEnabled, g_ssrReflectivity, g_ssrMaxSteps, g_ssrStepSize, g_ssrThickness,
                hizPipeline);
            
            for (size_t i = 0; i < g_sceneObjects.size(); i++) {
                g_prevModelMatrices[i] = g_sceneObjects[i].transform;
            }
            
            g_prevViewProj = camera.GetProjectionMatrixNoJitter() * camera.GetViewMatrix();
            context.GetQueue()->SubmitAsync(commandBuffers[imageIndex], imageIndex);
            context.GetQueue()->Present(imageIndex);

            // ease toward target with asymmetric temporal adaptation
            if (g_autoExposureEnabled) {
                float avgLum = std::max(g_measuredLuminance, 1e-4f);
                float targetExposure = g_keyValue / avgLum;
                targetExposure = std::clamp(targetExposure, g_minExposure, g_maxExposure);


                // faster when brightening (target < current: scene got brighter, exposure drops fast)
                // slower when darkening (target > current: scene got darker, exposure rises slowly)
                float rate = (targetExposure < g_currentExposure) ? g_adaptSpeedUp : g_adaptSpeedDown;

                // exponential approach, framerate-independent via dt
                float t = 1.0f - expf(-rate * deltaTime);
                g_currentExposure += (targetExposure - g_currentExposure) * t;

                g_exposure = g_currentExposure;
            }
            else {
                // keep the smoothed state in sync so toggling back doesn't jump (manual mode)
                g_currentExposure = g_exposure;
            }
        }

        g_camera = nullptr;
        context.Shutdown();
        context.FreeCommandBuffers(static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
        
        clusterBuilder.Cleanup(context.GetDevice());
        skybox.Cleanup(context.GetDevice());
        imguiManager.Cleanup(context.GetDevice());

        for (auto& sphere : showcaseSpheres) sphere.Cleanup(context.GetDevice());
        for (auto& ubos : showcaseUniformBuffers) {
            for (auto& ubo : ubos) ubo.Destroy(context.GetDevice());
        }

        resolvePipeline.Cleanup();
        tonemapPipeline.Cleanup();
        debugPipeline.Cleanup();
        pipeline.Cleanup();
        renderPass.Cleanup();

        iblTextures.irradiance.Destroy(context.GetDevice());
        iblTextures.prefilteredSpecular.Destroy(context.GetDevice());
        iblTextures.brdfLUT.Destroy(context.GetDevice());

        lightBuffer.Destroy(context.GetDevice());
        lightCuller.Cleanup(context.GetDevice());

        shadowMap.Cleanup(context.GetDevice());
        shadowPipeline.Cleanup();

        rampBuffer.Destroy(context.GetDevice());

        hizPipeline.Cleanup();
        vkDestroySampler(context.GetDevice(), hizSampler, nullptr);

        swapchain.Cleanup();
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Fatal error: %s\n", e.what());
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}