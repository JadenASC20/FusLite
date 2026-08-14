#pragma once
#include <volk.h>
#include <vector>
#include <glm/glm.hpp>
class VulkanContext;

class HiZPipeline {

public:
    struct HiZPush {
        glm::ivec2 dstSize;
        int   mode;      // 0 = linearize depth->mip0, 1 = min/max downsample
        float nearZ;
        float farZ;
        float _pad0, _pad1, _pad2;
    };

    void Init(VulkanContext& context, VkShaderModule comp, VkSampler sampler,
        const std::vector<VkImageView>& depthViews,   // per swapchain image
        const std::vector<VkImageView>& hizMipViews,  // per mip (single image)
        uint32_t mipLevels);
    // Build the whole pyramid for the given swapchain image.

    void Build(VkCommandBuffer cmd, uint32_t imageIndex, VkImage hizImage,
        VkExtent2D baseExtent, uint32_t mipLevels,
        float nearZ, float farZ) const;

    void Cleanup();

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    // m_sets[img][mip]: descriptor for building mip `mip` on swapchain image `img`.
    std::vector<std::vector<VkDescriptorSet>> m_sets;
    uint32_t m_mipLevels = 1;
};