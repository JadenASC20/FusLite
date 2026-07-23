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
    const std::vector<VkImage>& GetDepthImages() const { return m_depthImages; }
    VkFormat GetDepthFormat() const { return m_depthFormat; }

    const std::vector<VkImageView>& GetHdrImageViews() const { return m_hdrImageViews; }
    const std::vector<VkImage>& GetHdrImages() const { return m_hdrImages; }
    VkFormat GetHdrFormat() const { return m_hdrFormat; }

private:
    void CreateDepthResources(const Swapchain& swapchain);
    void CreateHdrResources(const Swapchain& swapchain);

    VulkanContext* m_context = nullptr;

    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> m_depthImages;
    std::vector<VkDeviceMemory> m_depthImageMemories;
    std::vector<VkImageView> m_depthImageViews;

    VkFormat m_hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    std::vector<VkImage> m_hdrImages;
    std::vector<VkDeviceMemory> m_hdrImageMemories;
    std::vector<VkImageView> m_hdrImageViews;
};