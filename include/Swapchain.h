#pragma once
#include <volk.h>
#include <vector>

class VulkanContext;

class Swapchain
{
public:
    Swapchain();
    ~Swapchain();

    void Init(VulkanContext& context, uint32_t windowWidth, uint32_t windowHeight);
    void Cleanup();

    VkSwapchainKHR GetHandle() const { return m_swapchain; }
    VkFormat GetImageFormat() const { return m_imageFormat; }
    VkExtent2D GetExtent() const { return m_extent; }
    const std::vector<VkImageView>& GetImageViews() const { return m_imageViews; }
    const std::vector<VkImage>& GetImages() const { return m_images; }

private:
    uint32_t ChooseNumImages(const VkSurfaceCapabilitiesKHR& capabilities);
    VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t windowWidth, uint32_t windowHeight);

    void CreateImageViews();

    VulkanContext* m_context = nullptr; // non-owning, used only during Init/Cleanup

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_images;       // owned by the swapchain, not destroyed by us
    std::vector<VkImageView> m_imageViews; // owned by us, must be destroyed

    VkFormat m_imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};
};