#pragma once
#include <volk.h>
#include <vector>

class VulkanContext;
class Swapchain;

class RenderPass
{
public:
    RenderPass();
    ~RenderPass();

    void Init(VulkanContext& context, const Swapchain& swapchain);
    void Cleanup();

    VkRenderPass GetHandle() const { return m_renderPass; }
    const std::vector<VkFramebuffer>& GetFramebuffers() const { return m_framebuffers; }

private:
    void CreateRenderPass(VkFormat swapchainFormat);
    void CreateFramebuffers(const Swapchain& swapchain);

    VulkanContext* m_context = nullptr;

    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;
};