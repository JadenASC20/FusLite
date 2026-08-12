#pragma once
#include <volk.h>
#include <vector>
#include <VulkanQueue.h>

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
    
    const std::vector<VkImage>& GetNormalImages() const { return m_normalImages; }
    const std::vector<VkImageView>& GetNormalImageViews() const { return m_normalImageViews; }
    VkFormat GetNormalFormat() const { return m_normalFormat; }

    const std::vector<VkImage>& GetHistoryImages() const { return m_historyImages; }
    const std::vector<VkImageView>& GetHistoryImageViews() const { return m_historyImageViews; }
    VkFormat GetHistoryFormat() const { return m_hdrFormat; }

    VkImage GetLumImage() const { return m_lumImage; }
    VkBuffer GetLumStagingBuffer(int frame) const { return m_lumStagingBuffers[frame]; }
    void* GetLumStagingMapped(int frame) const { return m_lumStagingMapped[frame]; }

    const std::vector<VkImage>& GetSSRImages() const { return m_ssrImages; }
    const std::vector<VkImageView>& GetSSRImageViews() const { return m_ssrImageViews; }
    VkFormat GetSSRFormat() const { return m_ssrFormat; }

    const std::vector<VkImage>& GetCompositeImages() const { return m_compositeImages; }
    const std::vector<VkImageView>& GetCompositeImageViews() const { return m_compositeImageViews; }
    VkFormat GetCompositeFormat() const { return m_compositeFormat; }

private:
    void CreateDepthResources(const Swapchain& swapchain);
    void CreateHdrResources(const Swapchain& swapchain);
    void CreateMotionResources(const Swapchain& swapchain);
    void CreateNormalResources(const Swapchain& swapchain);
    void CreateHistoryResources(const Swapchain& swapchain);
    void CreateLuminanceResources();
    void CreateSSRResources(const Swapchain& swapchain);
    void CreateCompositeResources(const Swapchain& swapchain);

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

    VkFormat m_normalFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    std::vector<VkImage> m_normalImages;
    std::vector<VkDeviceMemory> m_normalMemory;
    std::vector<VkImageView> m_normalImageViews;

    VkFormat m_historyFormat = VK_FORMAT_R16G16B16A16_SFLOAT; // matches HDR format
    std::vector<VkImage> m_historyImages;        // size 2, ping-pong
    std::vector<VkDeviceMemory> m_historyMemory;
    std::vector<VkImageView> m_historyImageViews;

    static constexpr int kLumFrames = MAX_FRAMES_IN_FLIGHT;
    VkImage m_lumImage = VK_NULL_HANDLE;
    VkDeviceMemory m_lumImageMemory = VK_NULL_HANDLE;
    VkImageView m_lumImageView = VK_NULL_HANDLE;
    VkBuffer m_lumStagingBuffers[2] = {};
    VkDeviceMemory m_lumStagingMemories[2] = {};
    void* m_lumStagingMapped[2] = {};

    VkFormat m_ssrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    std::vector<VkImage> m_ssrImages;
    std::vector<VkDeviceMemory> m_ssrMemory;
    std::vector<VkImageView> m_ssrImageViews;

    VkFormat m_compositeFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    std::vector<VkImage> m_compositeImages;
    std::vector<VkDeviceMemory> m_compositeMemory;
    std::vector<VkImageView> m_compositeImageViews;
};