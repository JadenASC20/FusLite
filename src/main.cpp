#include <VulkanContext.h>
#include <Swapchain.h>
#include <VulkanQueue.h>
#include <RenderPass.h>
#include <GraphicsPipeline.h>
#include <TonemapPipeline.h>
#include <Skybox.h>
#include <ImGuiManager.h>
#include <ShaderModule.h>
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

#include <imgui.h>
#include <ImGuizmo.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

static Camera* g_camera = nullptr;
static bool g_showGui = true;

static glm::vec3 g_lightPositions[MAX_LIGHTS] = {
    { 2.0f, 1.5f, 2.0f },
    { -2.0f, 1.5f, 2.0f },
    { 0.0f, 2.5f, -2.0f },
    { 0.0f, 0.5f, 3.0f }
};
static glm::vec3 g_lightColors[MAX_LIGHTS] = {
    { 1.0f, 0.3f, 0.3f },
    { 0.3f, 0.3f, 1.0f },
    { 0.3f, 1.0f, 0.3f },
    { 1.0f, 1.0f, 1.0f }
};
static float g_lightIntensities[MAX_LIGHTS] = { 5.0f, 5.0f, 5.0f, 5.0f };
static float g_lightRadii[MAX_LIGHTS] = { 5.0f, 5.0f, 5.0f, 5.0f };
static int g_numActiveLights = 2;

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

void RecordFrame(VkCommandBuffer cmd, uint32_t imageIndex, const Swapchain& swapchain,
    const RenderPass& renderResources, const GraphicsPipeline& pipeline, const TonemapPipeline& tonemapPipeline,
    const Skybox& skybox, const Model& model, ImGuiManager& imguiManager, bool showGui,
    const RenderParams& params)
{
    VkExtent2D extent = swapchain.GetExtent();
    VkImage colorImage = swapchain.GetImages()[imageIndex];
    VkImageView colorView = swapchain.GetImageViews()[imageIndex];
    VkImageView depthView = renderResources.GetDepthImageViews()[imageIndex];
    VkImage depthImage = renderResources.GetDepthImages()[imageIndex];
    VkImage hdrImage = renderResources.GetHdrImages()[imageIndex];
    VkImageView hdrView = renderResources.GetHdrImageViews()[imageIndex];

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // --- Pass 1: render scene + skybox into the HDR offscreen target ---
    TransitionImage(cmd, hdrImage,
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
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo sceneRenderingInfo{};
    sceneRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    sceneRenderingInfo.renderArea = { {0, 0}, extent };
    sceneRenderingInfo.layerCount = 1;
    sceneRenderingInfo.colorAttachmentCount = 1;
    sceneRenderingInfo.pColorAttachments = &hdrColorAttachment;
    sceneRenderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &sceneRenderingInfo);

    skybox.Draw(cmd, imageIndex);

    pipeline.Bind(cmd);
    pipeline.PushParams(cmd, params);
    model.Draw(cmd, pipeline.GetLayout(), imageIndex);

    vkCmdEndRendering(cmd);

    TransitionImage(cmd, hdrImage,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    // --- Pass 2: tonemap HDR -> swapchain ---
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

    tonemapPipeline.Bind(cmd, imageIndex);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);

    // --- Pass 3: ImGui overlay, drawn directly onto the swapchain image ---
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

    vkEndCommandBuffer(cmd);
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

        RenderPass renderPass;
        renderPass.Init(context, swapchain);

        Model model;
        model.LoadFromFile(context, "assets/carTEST1_2.glb");

        std::vector<BufferAndMemory> uniformBuffers(swapchain.GetImages().size());
        for (auto& ubo : uniformBuffers) {
            ubo = context.CreateUniformBuffer(sizeof(UniformBufferObject));
        }

        Skybox skybox;
        skybox.Init(context, window, renderPass.GetHdrFormat(), renderPass.GetDepthFormat(),
            "assets/Skybox.hdr", static_cast<uint32_t>(uniformBuffers.size()));

        VulkanContext::IBLTextures iblTextures = context.CreateIBLFromEquirect("assets/Skybox.hdr");
        VkShaderModule vertShader = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/triangle.vert.spv");
        VkShaderModule fragShader = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/triangle.frag.spv");

        GraphicsPipeline pipeline;
        uint32_t maxDescriptorSets = 32 * static_cast<uint32_t>(uniformBuffers.size());
        pipeline.Init(context, window, renderPass.GetHdrFormat(), renderPass.GetDepthFormat(),
            vertShader, fragShader, maxDescriptorSets);

        vkDestroyShaderModule(context.GetDevice(), vertShader, nullptr);
        vkDestroyShaderModule(context.GetDevice(), fragShader, nullptr);

        model.CreateDescriptorSets(pipeline, uniformBuffers, sizeof(UniformBufferObject), iblTextures);

        VkShaderModule fullscreenVert = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/fullscreen.vert.spv");
        VkShaderModule tonemapFrag = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/tonemap.frag.spv");

        TonemapPipeline tonemapPipeline;
        tonemapPipeline.Init(context, window, swapchain.GetImageFormat(),
            fullscreenVert, tonemapFrag, renderPass.GetHdrImageViews());

        vkDestroyShaderModule(context.GetDevice(), fullscreenVert, nullptr);
        vkDestroyShaderModule(context.GetDevice(), tonemapFrag, nullptr);

        ImGuiManager imguiManager;
        imguiManager.Init(context, window, swapchain.GetImageFormat(),
            static_cast<uint32_t>(swapchain.GetImages().size()));

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

        static glm::mat4 modelMatrix = glm::mat4(1.0f); // move this OUTSIDE the if(g_showGui) block
        static RenderParams g_renderParams;

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            float currentTime = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();
            float deltaTime = currentTime - lastFrameTime;
            lastFrameTime = currentTime;

            camera.Update(deltaTime);

            uint32_t imageIndex = context.GetQueue()->AcquireNextImage();

            UniformBufferObject ubo{};
            ubo.model = modelMatrix;
            ubo.view = camera.GetViewMatrix();
            ubo.proj = camera.GetProjectionMatrix();
            ubo.cameraPos = glm::vec4(camera.GetPosition(), 0.0f);

            for (int i = 0; i < MAX_LIGHTS; i++) {
                ubo.lightPosAndRadius[i] = glm::vec4(g_lightPositions[i], g_lightRadii[i]);
                ubo.lightColorAndIntensity[i] = glm::vec4(g_lightColors[i], g_lightIntensities[i]);
            }
            ubo.numLightsPacked = glm::vec4(static_cast<float>(g_numActiveLights), 0, 0, 0);

            void* data;
            vkMapMemory(context.GetDevice(), uniformBuffers[imageIndex].memory, 0, sizeof(ubo), 0, &data);
            memcpy(data, &ubo, sizeof(ubo));
            vkUnmapMemory(context.GetDevice(), uniformBuffers[imageIndex].memory);

            glm::mat4 vpNoTranslate = camera.GetProjectionMatrix() * camera.GetViewMatrixNoTranslate();
            skybox.Update(imageIndex, vpNoTranslate);

            glm::mat4 gizmoProj = glm::perspective(
                glm::radians(45.0f),
                static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT),
                0.1f, 1000.0f
            );

            if (g_showGui) {
                imguiManager.BeginFrame();

                ImGuizmo::SetOrthographic(false);
                ImGuizmo::BeginFrame();
                ImGuizmo::SetRect(0, 0, static_cast<float>(WINDOW_WIDTH), static_cast<float>(WINDOW_HEIGHT));

                glm::mat4 view = camera.GetViewMatrix();

                ImGuizmo::Manipulate(
                    glm::value_ptr(view),
                    glm::value_ptr(gizmoProj),
                    ImGuizmo::TRANSLATE,
                    ImGuizmo::WORLD,
                    glm::value_ptr(modelMatrix)
                );

                ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
                ImGuiIO& io = ImGui::GetIO();
                ImGui::Text("Frame time: %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
                ImGui::Text("Press SPACE to toggle this window");

                ImGui::Separator();
                ImGui::Text("Material Tuning");
                ImGui::SliderFloat("Clearcoat Factor", &g_renderParams.clearcoatFactor, 0.0f, 1.0f);
                ImGui::SliderFloat("Clearcoat Roughness", &g_renderParams.clearcoatRoughness, 0.01f, 0.5f);
                ImGui::SliderFloat("Flake Strength", &g_renderParams.flakeStrength, 0.0f, 0.3f);
                ImGui::SliderFloat("Flake Scale", &g_renderParams.flakeScale, 50.0f, 1000.0f);

                ImGui::Separator();
                ImGui::Text("Lighting");
                ImGui::SliderFloat3("Light Direction", &g_renderParams.lightDirAndIntensity.x, -1.0f, 1.0f);
                ImGui::SliderFloat("Light Intensity", &g_renderParams.lightDirAndIntensity.w, 0.0f, 10.0f);

                ImGui::Separator();
                ImGui::Text("Point Lights");
                ImGui::SliderInt("Active Lights", &g_numActiveLights, 0, MAX_LIGHTS);
                for (int i = 0; i < g_numActiveLights; i++) {
                    ImGui::PushID(i);
                    ImGui::Text("Light %d", i);
                    ImGui::SliderFloat3("Position", &g_lightPositions[i].x, -5.0f, 5.0f);
                    ImGui::ColorEdit3("Color", &g_lightColors[i].x);
                    ImGui::SliderFloat("Intensity", &g_lightIntensities[i], 0.0f, 20.0f);
                    ImGui::SliderFloat("Radius", &g_lightRadii[i], 0.5f, 20.0f);
                    ImGui::PopID();
                }

                ImGui::Separator();
                static float frameTimes[90] = {};
                static int frameTimeOffset = 0;
                frameTimes[frameTimeOffset] = deltaTime * 1000.0f;
                frameTimeOffset = (frameTimeOffset + 1) % IM_ARRAYSIZE(frameTimes);
                ImGui::PlotLines("Frame Time (ms)", frameTimes, IM_ARRAYSIZE(frameTimes), frameTimeOffset,
                    nullptr, 0.0f, 33.0f, ImVec2(0, 60));

                ImGui::End();

                imguiManager.EndFrame();
            }

            RecordFrame(commandBuffers[imageIndex], imageIndex, swapchain, renderPass,
                pipeline, tonemapPipeline, skybox, model, imguiManager, g_showGui, g_renderParams);

            context.GetQueue()->SubmitAsync(commandBuffers[imageIndex], imageIndex);
            context.GetQueue()->Present(imageIndex);
        }

        g_camera = nullptr;

        context.Shutdown();
        context.FreeCommandBuffers(static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
        model.Cleanup(context.GetDevice());
        skybox.Cleanup(context.GetDevice());
        imguiManager.Cleanup(context.GetDevice());
        for (auto& ubo : uniformBuffers) { ubo.Destroy(context.GetDevice()); }
        tonemapPipeline.Cleanup();
        pipeline.Cleanup();
        renderPass.Cleanup();
        iblTextures.irradiance.Destroy(context.GetDevice());
        iblTextures.prefilteredSpecular.Destroy(context.GetDevice());
        iblTextures.brdfLUT.Destroy(context.GetDevice());
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