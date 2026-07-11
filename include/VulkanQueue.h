#pragma once
#include <volk.h>
#include <vector>

constexpr int MAX_FRAMES_IN_FLIGHT = 2;

class VulkanQueue
{
public:
    VulkanQueue();
    ~VulkanQueue();

    void Init(VkDevice device, VkSwapchainKHR swapchain, uint32_t queueFamily, uint32_t queueIndex, uint32_t numSwapchainImages);
    void Destroy();

    uint32_t AcquireNextImage();
    void SubmitAsync(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void Present(uint32_t imageIndex);
    void WaitIdle();

private:
    VkSemaphore CreateSemaphore();
    VkFence CreateFence();

    VkDevice m_device = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;

    // Indexed by frame-in-flight (paces CPU submission)
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkFence> m_inFlightFences;

    // Indexed by swapchain image (tied to when THAT image is safe to present)
    std::vector<VkSemaphore> m_renderFinishedSemaphores;

    int m_currentFrame = 0;
};