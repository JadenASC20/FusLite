#include <VulkanContext.h>
#include <Swapchain.h>
#include <VulkanQueue.h>
#include <RenderPass.h>
#include <GraphicsPipeline.h>
#include <ShaderModule.h>
#include <Vertex.h>
#include <Buffer.h>
#include <UniformBufferObject.h>
#include <glm/gtc/matrix_transform.hpp>
#include <Camera.h>
#include <chrono>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

static Camera* g_camera = nullptr;

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    if (g_camera) g_camera->OnKey(key, action);
}

void MouseMoveCallback(GLFWwindow* window, double x, double y)
{
    if (g_camera) g_camera->OnMouseMove(x, y);
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    if (g_camera) g_camera->OnMouseButton(button, action);
}

void RecordCommandBuffers(std::vector<VkCommandBuffer>& commandBuffers, const Swapchain& swapchain,
    const RenderPass& renderPass, const GraphicsPipeline& pipeline, VkBuffer vertexBuffer)
{
    VkClearValue clearValue{};
    clearValue.color = { 0.1f, 0.1f, 0.2f, 1.0f };

    VkExtent2D extent = swapchain.GetExtent();
    const auto& framebuffers = renderPass.GetFramebuffers();

    for (size_t i = 0; i < commandBuffers.size(); i++) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        if (vkBeginCommandBuffer(commandBuffers[i], &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("Failed to begin recording command buffer");
        }

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass.GetHandle();
        renderPassInfo.framebuffer = framebuffers[i];
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = extent;
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        pipeline.Bind(commandBuffers[i], static_cast<uint32_t>(i));

        VkBuffer vertexBuffers[] = { vertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);

        vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);

        vkCmdEndRenderPass(commandBuffers[i]);

        if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to end recording command buffer");
        }
    }

    printf("Command buffers recorded.\n");
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

        std::vector<Vertex> vertices = {
            {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
            {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
            {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
        };

        BufferAndMemory vertexBuffer = context.CreateVertexBuffer(vertices.data(), sizeof(vertices[0]) * vertices.size());

        std::vector<BufferAndMemory> uniformBuffers(swapchain.GetImages().size());
        for (auto& ubo : uniformBuffers) {
            ubo = context.CreateUniformBuffer(sizeof(UniformBufferObject));
        }

        VkShaderModule vertShader = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/triangle.vert.spv");
        VkShaderModule fragShader = CreateShaderModuleFromBinary(context.GetDevice(), "shaders/triangle.frag.spv");

        GraphicsPipeline pipeline;
        pipeline.Init(context, window, renderPass, vertShader, fragShader, uniformBuffers, sizeof(UniformBufferObject));

        vkDestroyShaderModule(context.GetDevice(), vertShader, nullptr);
        vkDestroyShaderModule(context.GetDevice(), fragShader, nullptr);

        std::vector<VkCommandBuffer> commandBuffers(swapchain.GetImageViews().size());
        context.CreateCommandBuffers(static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
        RecordCommandBuffers(commandBuffers, swapchain, renderPass, pipeline, vertexBuffer.buffer);

        Camera camera(
            glm::vec3(0.0f, 0.0f, 3.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            45.0f,
            static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT),
            0.1f, 100.0f
        );
        g_camera = &camera;

        auto startTime = std::chrono::high_resolution_clock::now();
        float lastFrameTime = 0.0f;

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            float currentTime = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();
            float deltaTime = currentTime - lastFrameTime;
            lastFrameTime = currentTime;

            camera.Update(deltaTime);

            uint32_t imageIndex = context.GetQueue()->AcquireNextImage();

            UniformBufferObject ubo{};
            ubo.model = glm::rotate(glm::mat4(1.0f), currentTime * glm::radians(90.0f), glm::vec3(0, 0, 1));
            ubo.view = camera.GetViewMatrix();
            ubo.proj = camera.GetProjectionMatrix();

            void* data;
            vkMapMemory(context.GetDevice(), uniformBuffers[imageIndex].memory, 0, sizeof(ubo), 0, &data);
            memcpy(data, &ubo, sizeof(ubo));
            vkUnmapMemory(context.GetDevice(), uniformBuffers[imageIndex].memory);

            context.GetQueue()->SubmitAsync(commandBuffers[imageIndex], imageIndex);
            context.GetQueue()->Present(imageIndex);
        }

        g_camera = nullptr;

        context.Shutdown();
        context.FreeCommandBuffers(static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
        vertexBuffer.Destroy(context.GetDevice());
        for (auto& ubo : uniformBuffers) { ubo.Destroy(context.GetDevice()); }
        pipeline.Cleanup();
        renderPass.Cleanup();
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