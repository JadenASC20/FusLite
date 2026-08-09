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

    const std::vector<VkImage>& GetMotionImages() const { return m_motionImages; }
    const std::vector<VkImageView>& GetMotionImageViews() const { return m_motionImageViews; }
    VkFormat GetMotionFormat() const { return m_motionFormat; }

    const std::vector<VkImage>& GetHistoryImages() const { return m_historyImages; }
    const std::vector<VkImageView>& GetHistoryImageViews() const { return m_historyImageViews; }
    VkFormat GetHistoryFormat() const { return m_hdrFormat; }

    VkImage GetLumImage() const { return m_lumImage; }
    VkImageView GetLumImageView() const { return m_lumImageView; }
    VkBuffer GetLumStagingBuffer() const { return m_lumStagingBuffer; }
    void* GetLumStagingMapped() const { return m_lumStagingMapped; }

private:
    void CreateDepthResources(const Swapchain& swapchain);
    void CreateHdrResources(const Swapchain& swapchain);
    void CreateMotionResources(const Swapchain& swapchain);
    void CreateHistoryResources(const Swapchain& swapchain);

    VulkanContext* m_context = nullptr;

    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> m_depthImages;
    std::vector<VkDeviceMemory> m_depthImageMemories;
    std::vector<VkImageView> m_depthImageViews;

    VkFormat m_hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    std::vector<VkImage> m_hdrImages;
    std::vector<VkDeviceMemory> m_hdrImageMemories;
    std::vector<VkImageView> m_hdrImageViews;

    VkFormat m_motionFormat = VK_FORMAT_R16G16_SFLOAT;
    std::vector<VkImage> m_motionImages;
    std::vector<VkDeviceMemory> m_motionMemory;
    std::vector<VkImageView> m_motionImageViews;

    VkFormat m_historyFormat = VK_FORMAT_R16G16B16A16_SFLOAT; // matches HDR format
    std::vector<VkImage> m_historyImages;        // size 2, ping-pong
    std::vector<VkDeviceMemory> m_historyMemory;
    std::vector<VkImageView> m_historyImageViews;

    // Auto-exposure CP1: 1x1 luminance readback
    VkImage m_lumImage = VK_NULL_HANDLE;
    VkDeviceMemory m_lumImageMemory = VK_NULL_HANDLE;
    VkImageView m_lumImageView = VK_NULL_HANDLE;
    VkBuffer m_lumStagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_lumStagingMemory = VK_NULL_HANDLE;
    void* m_lumStagingMapped = nullptr;
    void CreateLuminanceResources();
};