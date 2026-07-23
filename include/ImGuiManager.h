#pragma once
#include <volk.h>

struct GLFWwindow;
class VulkanContext;

class ImGuiManager
{
public:
    ImGuiManager();
    ~ImGuiManager();

    void Init(VulkanContext& context, GLFWwindow* window, VkFormat swapchainColorFormat, uint32_t numImages);
    void Cleanup(VkDevice device);

    void BeginFrame();
    void EndFrame(); // calls ImGui::Render() — call after building your windows, before recording

    void RecordDrawCommands(VkCommandBuffer commandBuffer);

    static bool IsMouseControlledByImGui();

private:
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
};