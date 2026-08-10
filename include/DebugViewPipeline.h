#pragma once
#include <volk.h>
class VulkanContext;

class DebugViewPipeline
{
public:
    struct PushConstants {
        int   mode;
        float nearZ;
        float farZ;
        float _pad;
    };

    void Init(VulkanContext& context, VkFormat swapchainFormat,
        VkShaderModule fullscreenVert, VkShaderModule debugFrag);
    void Cleanup();
    void Bind(VkCommandBuffer cmd, VkImageView view, const PushConstants& pc);

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_set = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};