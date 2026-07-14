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

    const std::vector<VkImageView>& GetDepthImageViews() const { return m_depthImageViews; }
    VkFormat GetDepthFormat() const { return m_depthFormat; }
    const std::vector<VkImage>& GetDepthImages() const { return m_depthImages; }

private:
    void CreateDepthResources(const Swapchain& swapchain);

    VulkanContext* m_context = nullptr;
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> m_depthImages;
    std::vector<VkDeviceMemory> m_depthImageMemories;
    std::vector<VkImageView> m_depthImageViews;
};