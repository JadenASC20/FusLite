#include <VulkanContext.h>
#include <Swapchain.h>
#include <VulkanQueue.h>
#include <RenderPass.h>
#include <GraphicsPipeline.h>
#include <TonemapPipeline.h>
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
    const ShadowMap& shadowMap, const ShadowPipeline& shadowPipeline, const glm::mat4& lightViewProj,
    const Skybox& skybox, const std::vector<Model>& showcaseSpheres,
    const std::vector<SceneObject>& sceneObjects,
    ImGuiManager& imguiManager, bool showGui, RenderParams params)
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

    // --- Pass 0: Shadow map (depth-only, from the light's POV) ---
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
    shadowDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // we need to read it later
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

        static glm::mat4 modelMatrix = glm::mat4(1.0f); // move this OUTSIDE the if(g_showGui) block
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

                // --- Window 1: Debug ---
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
                }
                ImGui::End();

                // --- Window 2: Property Window ---
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

                // --- Window 3: Scene Outliner ---
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
                pipeline, tonemapPipeline, shadowMap, shadowPipeline, lightViewProj,
                skybox, showcaseSpheres, g_sceneObjects,
                imguiManager, g_showGui, g_renderParams);

            context.GetQueue()->SubmitAsync(commandBuffers[imageIndex], imageIndex);
            context.GetQueue()->Present(imageIndex);
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
        tonemapPipeline.Cleanup();
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