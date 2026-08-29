#pragma once
#include <volk.h>
#include <glm/glm.hpp>
#include <vector>

#include "AppState.h"
#include "RenderParams.h"

class Swapchain;
class RenderPass;
class GraphicsPipeline;
class TonemapPipeline;
class ResolvePipeline;
class DebugViewPipeline;
class SSRPipeline;
class SSAOPipeline;
class HiZPipeline;
class ShadowPipeline;
class ShadowMap;
class Skybox;
class Camera;
class Model;
class ImGuiManager;

// Long-lived rendering objects. Built once in main, passed by const ref every
// frame. Pointers rather than references so the struct stays assignable.
struct PassResources
{
    const Swapchain* swapchain = nullptr;
    const RenderPass* targets = nullptr;   // offscreen render targets
    const GraphicsPipeline* scenePipeline = nullptr;
    const ShadowPipeline* shadowPipeline = nullptr;
    const ShadowMap* shadowMap = nullptr;
    const Skybox* skybox = nullptr;
    const SSRPipeline* ssr = nullptr;
    const SSAOPipeline* ssao = nullptr;
    const HiZPipeline* hiz = nullptr;
    const ResolvePipeline* resolve = nullptr;
    const TonemapPipeline* tonemap = nullptr;
    DebugViewPipeline* debugView = nullptr;
    ImGuiManager* imgui = nullptr;
};

// State that changes every frame.
struct FrameParams
{
    uint32_t     imageIndex = 0;
    int          frameInFlight = 0;
    int          historyRead = 0;
    int          historyWrite = 1;
    bool         firstFrame = false;
    glm::mat4    lightViewProj = glm::mat4(1.0f);
    RenderParams renderParams{};
};

// Records the whole frame: shadow -> scene G-buffer -> Hi-Z -> SSAO -> SSR ->
// composite -> TAA resolve -> auto-exposure readback -> tonemap -> UI -> present.
void RecordFrame(VkCommandBuffer cmd,
    const PassResources& res,
    const FrameParams& frame,
    const std::vector<Model>& models,
    const Scene& scene,
    const Camera& camera,
    const RenderSettings& settings);